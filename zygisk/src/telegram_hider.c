/*
 * Telegram Chat Hider — Zygisk Library (v1.3.0)
 *
 * Hooks Telegram's Java methods via ART ArtMethod entry-point replacement
 * (equivalent to Pine/SandHook approach — works on ALL Java methods, not just
 * JNI-registered natives that hook_jni_native_methods can intercept).
 *
 * Hooked methods:
 *  - MessagesController.getDialogs(I)          → filter hidden dialogs from chat list
 *  - NotificationCenter.postNotificationName  → suppress notifications for hidden chats
 *  - View.dispatchTouchEvent(MotionEvent)     → 5-tap header gesture to reveal/hide
 *
 * Config: chat_hider.json (0600 perms), parsed with cJSON.
 * IPC:    Dialog catalog served via Unix domain socket (SO_PEERCREED root-only).
 * Threads: all shared state mutex-protected.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <jni.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <limits.h>
#include <linux/limits.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <pthread.h>

#include "cJSON.h"
#include "art_hook.h"
#include "module.h"
#include "runtime_utils.h"

#define LOG_TAG "TelegramChatHider"
#include "logging.h"

/* ── Constants ─────────────────────────── */
#define SOCK_PATH "/data/adb/modules/telegram_chat_hider/chat_hider.sock"
#define CONFIG_MAX 8192
#define CATALOG_MAX (50 * 1024)
#define MAX_HIDDEN 512
#define TAP_WINDOW_MS 800

/* ── Config state (mutex-protected) ───── */
struct hidden_config {
    int64_t hidden_ids[MAX_HIDDEN];
    int     hidden_count;
    bool    hide_in_list;
    bool    hide_in_search;
    bool    hide_in_share;
    bool    hide_in_notifications;
    bool    experimental_art_hooks;
    int     tap_count;
    int64_t last_tap_ms;
    bool    revealed;
    int64_t last_tap_event_id;  /* MotionEvent.getDownTime() to dedupe */
};

static struct hidden_config g_cfg;
static char g_module_dir[PATH_MAX];
static pthread_mutex_t g_cfg_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_cat_mtx = PTHREAD_MUTEX_INITIALIZER;
static char *g_dialog_cache = NULL;       /* in-memory JSON catalog */

static JavaVM *g_jvm = NULL;
static bool g_is_target_process = false;
static bool g_hooks_registered = false;

/* NotificationCenter constant IDs (resolved at runtime via reflection) */
static int g_nc_did_receive_new_messages = -1;
static int g_nc_push_messages_updated = -1;

/* Cached JNI IDs (resolved once in register_telegram_hooks, used in hot paths) */
static jclass g_MotionEvent_cls = NULL;
static jmethodID g_MotionEvent_getActionMasked = NULL;
static jmethodID g_MotionEvent_getRawY = NULL;
static jmethodID g_MotionEvent_getDownTime = NULL;

static jclass g_ArrayList_cls = NULL;
static jmethodID g_ArrayList_size = NULL;
static jmethodID g_ArrayList_get = NULL;
static jmethodID g_ArrayList_remove = NULL;

static jclass g_Dialog_cls = NULL;
static jfieldID g_Dialog_id = NULL;

/* MotionEvent.ACTION_DOWN static int (resolved at registration) */
static jint g_ActionDown = 0;
static float g_header_threshold_px = 220.0f;

/* ── Config loader (cJSON) ────────────── */

static void load_config(void) {
    pthread_mutex_lock(&g_cfg_mtx);
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.hide_in_list = true;
    pthread_mutex_unlock(&g_cfg_mtx);

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/chat_hider.json",
             g_module_dir[0] ? g_module_dir : "/data/adb/modules/telegram_chat_hider");

    int fd = open(path, O_RDONLY);
    if (fd < 0) { LOGW("No config at %s — using defaults", path); return; }

    char buf[CONFIG_MAX + 1];
    ssize_t n = read(fd, buf, CONFIG_MAX);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) { LOGW("Config JSON parse error — using defaults"); return; }

    pthread_mutex_lock(&g_cfg_mtx);

    cJSON *b;
    b = cJSON_GetObjectItemCaseSensitive(root, "hide_in_list");
    if (cJSON_IsBool(b)) g_cfg.hide_in_list = cJSON_IsTrue(b);
    b = cJSON_GetObjectItemCaseSensitive(root, "hide_in_search");
    if (cJSON_IsBool(b)) g_cfg.hide_in_search = cJSON_IsTrue(b);
    b = cJSON_GetObjectItemCaseSensitive(root, "hide_in_share");
    if (cJSON_IsBool(b)) g_cfg.hide_in_share = cJSON_IsTrue(b);
    b = cJSON_GetObjectItemCaseSensitive(root, "hide_in_notifications");
    if (cJSON_IsBool(b)) g_cfg.hide_in_notifications = cJSON_IsTrue(b);
    b = cJSON_GetObjectItemCaseSensitive(root, "experimental_art_hooks");
    if (cJSON_IsBool(b)) g_cfg.experimental_art_hooks = cJSON_IsTrue(b);

    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "selected_dialogs");
    if (cJSON_IsArray(arr)) {
        int idx = 0;
        cJSON *item;
        cJSON_ArrayForEach(item, arr) {
            if (idx >= MAX_HIDDEN) break;
            int64_t parsed = 0;
            if (cJSON_IsString(item) && tch_parse_dialog_id(item->valuestring, &parsed)) {
                g_cfg.hidden_ids[idx++] = parsed;
            } else if (cJSON_IsNumber(item)) {
                /* Legacy numeric values are accepted only when exactly representable. */
                double value = item->valuedouble;
                if (value >= -9223372036854775808.0 && value < 9223372036854775808.0) {
                    parsed = (int64_t)value;
                    if (parsed != 0 && (double)parsed == value) {
                        g_cfg.hidden_ids[idx++] = parsed;
                    }
                }
            }
        }
        g_cfg.hidden_count = idx;
    }

    int hidden_count = g_cfg.hidden_count;
    bool hide_in_list = g_cfg.hide_in_list;
    bool hide_in_notifications = g_cfg.hide_in_notifications;
    bool experimental_art_hooks = g_cfg.experimental_art_hooks;
    pthread_mutex_unlock(&g_cfg_mtx);
    cJSON_Delete(root);
    LOGI("Config: %d hidden, list=%d notif=%d experimental_hooks=%d",
         hidden_count, hide_in_list, hide_in_notifications, experimental_art_hooks);
}

/* ── JNI env helper (for hook callbacks) ── */

static JNIEnv *get_env(void) {
    JNIEnv *env = NULL;
    if (g_jvm) (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);
    return env;
}

/* ── Filtering helpers ────────────────── */

static bool is_hidden(int64_t id) {
    pthread_mutex_lock(&g_cfg_mtx);
    bool hidden = false;
    if (!g_cfg.revealed) {
        for (int i = 0; i < g_cfg.hidden_count; i++)
            if (g_cfg.hidden_ids[i] == id) { hidden = true; break; }
    }
    pthread_mutex_unlock(&g_cfg_mtx);
    return hidden;
}

static bool should_hide_surface(const char *surf) {
    pthread_mutex_lock(&g_cfg_mtx);
    bool h;
    if (strcmp(surf, "list") == 0)        h = g_cfg.hide_in_list;
    else if (strcmp(surf, "search") == 0)  h = g_cfg.hide_in_search;
    else if (strcmp(surf, "share") == 0)   h = g_cfg.hide_in_share;
    else                                   h = false;
    pthread_mutex_unlock(&g_cfg_mtx);
    return h;
}

/* Remove hidden dialogs from an ArrayList in-place (reverse iteration) */
static void filter_dialogs_list(JNIEnv *env, jobject list, const char *surface) {
    if (!list) return;

    /* Build and cache catalog from full list */
    cJSON *arr = cJSON_CreateArray();
    jint n = (*env)->CallIntMethod(env, list, g_ArrayList_size);
    for (jint i = 0; i < n && i < 500; i++) {
        jobject d = (*env)->CallObjectMethod(env, list, g_ArrayList_get, i);
        if (d) {
            jlong id_val = g_Dialog_id ? (*env)->GetLongField(env, d, g_Dialog_id) : 0;
            cJSON *entry = cJSON_CreateObject();
            char id_str[32];
            snprintf(id_str, sizeof(id_str), "%lld", (long long)id_val);
            cJSON_AddStringToObject(entry, "id", id_str);
            cJSON_AddItemToArray(arr, entry);
            (*env)->DeleteLocalRef(env, d);
        }
    }
    char *json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (json_str) {
        pthread_mutex_lock(&g_cat_mtx);
        free(g_dialog_cache);
        g_dialog_cache = json_str;
        pthread_mutex_unlock(&g_cat_mtx);
    }

    /* Filter if needed */
    if (!should_hide_surface(surface)) {
        return;
    }

    for (jint i = n - 1; i >= 0; i--) {
        jobject d = (*env)->CallObjectMethod(env, list, g_ArrayList_get, i);
        if (!d) continue;
        jlong id_val = g_Dialog_id ? (*env)->GetLongField(env, d, g_Dialog_id) : 0;
        if (id_val != 0 && is_hidden((int64_t)id_val)) {
            (*env)->CallObjectMethod(env, list, g_ArrayList_remove, i);
            LOGD("Filtered dialog %lld on %s", (long long)id_val, surface);
        }
        (*env)->DeleteLocalRef(env, d);
    }
}

/* ── Unix socket server (replaces dialogs.json) ─────────────── */
/* SO_PEERCRED: kernel provides connecting UID. Only uid==0 (root) allowed. */

static void *socket_server_thread(void *arg) {
    (void)arg;
    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv < 0) { LOGE("socket: %s", strerror(errno)); return NULL; }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);
    unlink(SOCK_PATH);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOGE("bind: %s", strerror(errno)); close(srv); return NULL;
    }
    if (chmod(SOCK_PATH, 0600) != 0) {
        LOGE("chmod socket: %s", strerror(errno));
        close(srv);
        unlink(SOCK_PATH);
        return NULL;
    }
    if (listen(srv, 8) < 0) { LOGE("listen: %s", strerror(errno)); close(srv); return NULL; }
    LOGI("Socket server on %s", SOCK_PATH);

    for (;;) {
        int client = accept(srv, NULL, NULL);
        if (client < 0) { LOGW("accept: %s", strerror(errno)); sleep(1); continue; }

        struct timeval timeout = { .tv_sec = 5, .tv_usec = 0 };
        if (setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
            LOGW("socket timeout setup failed — rejecting client");
            close(client);
            continue;
        }

        struct ucred cred;
        socklen_t cl = sizeof(cred);
        if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &cred, &cl) != 0) {
            LOGW("getsockopt(SO_PEERCRED) failed — rejecting");
            close(client); continue;
        }
        if (cred.uid != 0) {
            LOGW("Reject uid=%d (non-root)", cred.uid); close(client); continue;
        }

        pthread_mutex_lock(&g_cat_mtx);
        const char *catalog = g_dialog_cache ? g_dialog_cache : "[]";
        size_t catalog_length = strnlen(catalog, CATALOG_MAX + 1);
        if (catalog_length > CATALOG_MAX) {
            catalog = "[]";
            catalog_length = 2;
        }

        char *payload = malloc(catalog_length + 2);
        if (payload) {
            memcpy(payload, catalog, catalog_length);
            payload[catalog_length] = '\n';
            payload[catalog_length + 1] = '\0';
        }
        pthread_mutex_unlock(&g_cat_mtx);

        if (!payload) {
            errno = ENOMEM;
            LOGW("catalog send failed: %s", strerror(errno));
        } else if (tch_send_all(client, payload, catalog_length + 1) != 0) {
            LOGW("catalog send failed: %s", strerror(errno));
        }
        free(payload);
        close(client);
    }
    close(srv);
    return NULL;
}

static void start_socket_server(void) {
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, socket_server_thread, NULL) != 0)
        LOGE("socket thread: %s", strerror(errno));
    pthread_attr_destroy(&attr);
}

/* ── Hook callbacks (entry-point replacement) ───────────────── */

/*
 * MessagesController.getDialogs(int folderId) → ArrayList
 * ARM64 quick ABI: x0=this(jobject), x1=folderId(jint), return x0(jobject)
 */
static jobject (*orig_get_dialogs)(jobject thiz, jint folder_id) = NULL;

static jobject hk_get_dialogs(jobject thiz, jint folder_id) {
    JNIEnv *env = get_env();
    if (!env || !orig_get_dialogs)
        return orig_get_dialogs ? orig_get_dialogs(thiz, folder_id) : NULL;

    jobject list = orig_get_dialogs(thiz, folder_id);
    if (list) filter_dialogs_list(env, list, "list");
    return list;
}

/*
 * NotificationCenter.postNotificationName(int id, Object[] args)
 * ARM64 ABI: x0=this, x1=id, x2=args(jobjectArray), return void
 */
static void (*orig_post_notification)(jobject thiz, jint id, jobjectArray args) = NULL;

static int64_t extract_dialog_id(JNIEnv *env, jobject arg) {
    if (!arg) return 0;
    jclass cls = (*env)->GetObjectClass(env, arg);
    if (!cls) return 0;
    int64_t result = 0;

    jfieldID fid = (*env)->GetFieldID(env, cls, "dialog_id", "J");
    if (fid) result = (*env)->GetLongField(env, arg, fid);

    if (result == 0) {
        jfieldID om_fid = (*env)->GetFieldID(env, cls, "messageOwner",
                                              "Lorg/telegram/tgnet/TLRPC$Message;");
        if (om_fid) {
            jobject owner = (*env)->GetObjectField(env, arg, om_fid);
            if (owner) {
                jclass oc = (*env)->GetObjectClass(env, owner);
                jfieldID dfid = (*env)->GetFieldID(env, oc, "dialog_id", "J");
                if (dfid) result = (*env)->GetLongField(env, owner, dfid);
                (*env)->DeleteLocalRef(env, oc);
                (*env)->DeleteLocalRef(env, owner);
            }
        }
    }
    (*env)->DeleteLocalRef(env, cls);
    return result;
}

static bool notif_for_hidden_chat(JNIEnv *env, jint id, jobjectArray args) {
    if (id != g_nc_did_receive_new_messages && id != g_nc_push_messages_updated)
        return false;
    if (!args) return false;
    jsize len = (*env)->GetArrayLength(env, args);
    for (jsize i = 0; i < len; i++) {
        jobject arg = (*env)->GetObjectArrayElement(env, args, i);
        if (arg) {
            int64_t dlg = extract_dialog_id(env, arg);
            (*env)->DeleteLocalRef(env, arg);
            if (dlg != 0 && is_hidden(dlg)) return true;
        }
    }
    return false;
}

static void hk_post_notification(jobject thiz, jint id, jobjectArray args) {
    JNIEnv *env = get_env();
    pthread_mutex_lock(&g_cfg_mtx);
    bool suppress = g_cfg.hide_in_notifications && !g_cfg.revealed;
    pthread_mutex_unlock(&g_cfg_mtx);

    if (suppress && env && notif_for_hidden_chat(env, id, args)) {
        LOGD("Suppressing notification id=%d for hidden chat", id);
        return;  /* don't call original */
    }
    if (orig_post_notification) orig_post_notification(thiz, id, args);
}

/*
 * View.dispatchTouchEvent(MotionEvent) → boolean
 * ARM64 ABI: x0=this, x1=event, return x0(jboolean)
 */
static jboolean (*orig_dispatch_touch)(jobject thiz, jobject event) = NULL;

static jboolean hk_dispatch_touch(jobject thiz, jobject event) {
    JNIEnv *env = get_env();

    if (env && event && g_MotionEvent_cls &&
        g_MotionEvent_getActionMasked && g_MotionEvent_getRawY) {

        jint masked = (*env)->CallIntMethod(env, event, g_MotionEvent_getActionMasked);
        if (!(*env)->ExceptionCheck(env)) {
            if (masked == g_ActionDown) {
                float y = (*env)->CallFloatMethod(env, event, g_MotionEvent_getRawY);
                /* Scale with density and cap at 18% of screen height. */
                if (y >= 0.0f && y < g_header_threshold_px) {
                    /* Use downTime to deduplicate — a single ACTION_DOWN can
                       be dispatched to multiple Views in the hierarchy. */
                    jlong down_time = (*env)->CallLongMethod(env, event, g_MotionEvent_getDownTime);
                    struct timespec ts;
                    clock_gettime(CLOCK_MONOTONIC, &ts);
                    int64_t now = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

                    pthread_mutex_lock(&g_cfg_mtx);
                    if (down_time != g_cfg.last_tap_event_id) {
                        g_cfg.last_tap_event_id = down_time;
                        if (now - g_cfg.last_tap_ms > TAP_WINDOW_MS)
                            g_cfg.tap_count = 0;
                        g_cfg.tap_count++;
                        g_cfg.last_tap_ms = now;
                        if (g_cfg.tap_count >= 5) {
                            g_cfg.tap_count = 0;
                            g_cfg.revealed = !g_cfg.revealed;
                            LOGI("5-tap → reveal=%s", g_cfg.revealed ? "ON" : "OFF");
                        }
                    }
                    pthread_mutex_unlock(&g_cfg_mtx);
                }
            }
        }
    }

    if (orig_dispatch_touch) return orig_dispatch_touch(thiz, event);
    return JNI_FALSE;
}

/* ── Resolve NotificationCenter constants at runtime ───────── */

static int resolve_nc_const(JNIEnv *env, const char *name) {
    jclass nc = (*env)->FindClass(env, "org/telegram/messenger/NotificationCenter");
    if (!nc) { LOGW("NotificationCenter class not found"); return -1; }
    jfieldID fid = (*env)->GetStaticFieldID(env, nc, name, "I");
    if (!fid) { LOGW("NC.%s field not found", name); return -1; }
    return (*env)->GetStaticIntField(env, nc, fid);
}

static void resolve_nc_constants(JNIEnv *env) {
    g_nc_did_receive_new_messages = resolve_nc_const(env, "didReceiveNewMessages");
    g_nc_push_messages_updated = resolve_nc_const(env, "pushMessagesUpdated");
    LOGI("NC constants: didReceiveNewMessages=%d pushMessagesUpdated=%d",
         g_nc_did_receive_new_messages, g_nc_push_messages_updated);
}

/* ── Cache JNI IDs for hot-path hooks ────────── */

static void clear_jni_exception(JNIEnv *env) {
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
}

static void cache_header_threshold(JNIEnv *env) {
    jclass resources_class = (*env)->FindClass(env, "android/content/res/Resources");
    if (!resources_class) {
        clear_jni_exception(env);
        return;
    }

    jmethodID get_system = (*env)->GetStaticMethodID(
        env, resources_class, "getSystem", "()Landroid/content/res/Resources;");
    jmethodID get_metrics = (*env)->GetMethodID(
        env, resources_class, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;");
    if (!get_system || !get_metrics) {
        clear_jni_exception(env);
        (*env)->DeleteLocalRef(env, resources_class);
        return;
    }

    jobject resources = (*env)->CallStaticObjectMethod(env, resources_class, get_system);
    jobject metrics = resources ? (*env)->CallObjectMethod(env, resources, get_metrics) : NULL;
    if (metrics) {
        jclass metrics_class = (*env)->GetObjectClass(env, metrics);
        jfieldID height_field = metrics_class
            ? (*env)->GetFieldID(env, metrics_class, "heightPixels", "I") : NULL;
        jfieldID density_field = metrics_class
            ? (*env)->GetFieldID(env, metrics_class, "density", "F") : NULL;
        if (height_field && density_field) {
            jint height = (*env)->GetIntField(env, metrics, height_field);
            jfloat density = (*env)->GetFloatField(env, metrics, density_field);
            g_header_threshold_px = tch_header_threshold_px(height, density);
        } else {
            clear_jni_exception(env);
        }
        if (metrics_class) (*env)->DeleteLocalRef(env, metrics_class);
        (*env)->DeleteLocalRef(env, metrics);
    } else {
        clear_jni_exception(env);
    }
    if (resources) (*env)->DeleteLocalRef(env, resources);
    (*env)->DeleteLocalRef(env, resources_class);
}

static void cache_jni_ids(JNIEnv *env) {
    jclass c;

    cache_header_threshold(env);

    c = (*env)->FindClass(env, "android/view/MotionEvent");
    if (c) {
        g_MotionEvent_cls = (jclass)(*env)->NewGlobalRef(env, c);
        g_MotionEvent_getActionMasked = (*env)->GetMethodID(env, c, "getActionMasked", "()I");
        g_MotionEvent_getRawY = (*env)->GetMethodID(env, c, "getRawY", "()F");
        g_MotionEvent_getDownTime = (*env)->GetMethodID(env, c, "getDownTime", "()J");
        jfieldID af = (*env)->GetStaticFieldID(env, c, "ACTION_DOWN", "I");
        if (af) g_ActionDown = (*env)->GetStaticIntField(env, c, af);
        (*env)->DeleteLocalRef(env, c);
    }

    c = (*env)->FindClass(env, "java/util/ArrayList");
    if (c) {
        g_ArrayList_cls = (jclass)(*env)->NewGlobalRef(env, c);
        g_ArrayList_size = (*env)->GetMethodID(env, c, "size", "()I");
        g_ArrayList_get = (*env)->GetMethodID(env, c, "get", "(I)Ljava/lang/Object;");
        g_ArrayList_remove = (*env)->GetMethodID(env, c, "remove", "(I)Ljava/lang/Object;");
        (*env)->DeleteLocalRef(env, c);
    }

    c = (*env)->FindClass(env, "org/telegram/tgnet/TLRPC$Dialog");
    if (c) {
        g_Dialog_cls = (jclass)(*env)->NewGlobalRef(env, c);
        g_Dialog_id = (*env)->GetFieldID(env, c, "id", "J");
        (*env)->DeleteLocalRef(env, c);
    }

    if (g_MotionEvent_getActionMasked && g_ArrayList_size && g_Dialog_id)
        LOGI("JNI ID cache: OK (MotionEvent/ArrayList/Dialogs)");
    else
        LOGW("JNI ID cache: partial (some classes not found in this Telegram build)");
}

static void register_telegram_hooks(JNIEnv *env) {
    /* Cache JNI IDs used in hot-path hooks (touch events, dialog filtering) */
    cache_jni_ids(env);

    /* 1. MessagesController.getDialogs(int) → ArrayList  */
    if (art_hook_method(env,
        "org/telegram/messenger/MessagesController",
        "getDialogs", "(I)Ljava/util/ArrayList;",
        (void *)hk_get_dialogs, (void **)&orig_get_dialogs) == 0) {
        LOGI("Hooked MessagesController.getDialogs(I)");
    } else {
        LOGW("getDialogs hook failed — chat list hiding unavailable");
    }

    /* 2. NotificationCenter.postNotificationName(int, Object[]) → void */
    resolve_nc_constants(env);
    if (art_hook_method(env,
        "org/telegram/messenger/NotificationCenter",
        "postNotificationName", "(I[Ljava/lang/Object;)V",
        (void *)hk_post_notification, (void **)&orig_post_notification) == 0) {
        LOGI("Hooked NotificationCenter.postNotificationName");
    } else {
        LOGW("postNotificationName hook failed — notification hiding unavailable");
    }

    /* 3. View.dispatchTouchEvent(MotionEvent) → boolean */
    if (art_hook_method(env,
        "android/view/View",
        "dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z",
        (void *)hk_dispatch_touch, (void **)&orig_dispatch_touch) == 0) {
        LOGI("Hooked View.dispatchTouchEvent for 5-tap gesture");
    } else {
        LOGW("dispatchTouchEvent hook failed — 5-tap gesture unavailable");
    }
}

/* ── ABI callbacks ────────────── */

static void my_pre_app_specialize(void *impl, void *raw_args) {
    (void)impl;
    g_is_target_process = false;

    struct app_specialize_args_v5 *args = raw_args;
    if (!args || !args->nice_name || !*args->nice_name) {
        return;
    }

    JNIEnv *env = get_env();
    if (!env) {
        return;
    }

    const char *nice_name = (*env)->GetStringUTFChars(env, *args->nice_name, NULL);
    if (!nice_name) {
        return;
    }
    g_is_target_process = tch_is_target_process(nice_name);
    (*env)->ReleaseStringUTFChars(env, *args->nice_name, nice_name);
}

static void my_post_app_specialize(void *impl, const void *args) {
    (void)impl;
    (void)args;
    if (!g_is_target_process || g_hooks_registered) {
        return;
    }
    g_hooks_registered = true;

    load_config();
    if (!g_cfg.experimental_art_hooks) {
        LOGE("Legacy ART hooks are disabled: the direct Quick-ABI bridge is unsafe");
        return;
    }

    JNIEnv *env = get_env();
    if (!env) {
        LOGE("Cannot attach thread for experimental hooks");
        return;
    }

    LOGW("Experimental ART hooks explicitly enabled; crashes and data corruption are possible");
    register_telegram_hooks(env);
    start_socket_server();
}

/* ── Zygisk entry ─────────────── */

static struct rezygisk_abi g_abi = {
    .api_version = REZYGISK_API_VERSION,
    .impl = NULL,
    .pre_app_specialize = my_pre_app_specialize,
    .post_app_specialize = my_post_app_specialize,
};

static void resolve_module_dir(struct rezygisk_api *api) {
    strcpy(g_module_dir, "/data/adb/modules/telegram_chat_hider");
    if (!api->get_module_dir) {
        return;
    }

    int dir_fd = api->get_module_dir(api->impl);
    if (dir_fd < 0) {
        return;
    }

    char fd_path[64];
    snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", dir_fd);
    ssize_t length = readlink(fd_path, g_module_dir, sizeof(g_module_dir) - 1);
    close(dir_fd);
    if (length <= 0) {
        strcpy(g_module_dir, "/data/adb/modules/telegram_chat_hider");
        return;
    }
    g_module_dir[length] = '\0';
}

void zygisk_module_entry(struct rezygisk_api *api, void *env) {
    JNIEnv *jenv = env;
    if (!api || !api->register_module || !jenv) {
        LOGE("Invalid MeowZygisk API or JNI environment");
        return;
    }

    if ((*jenv)->GetJavaVM(jenv, &g_jvm) != JNI_OK || !g_jvm) {
        LOGE("GetJavaVM failed");
        return;
    }

    if (!api->register_module(api, &g_abi)) {
        LOGE("Failed to register MeowZygisk API v%d module", REZYGISK_API_VERSION);
        return;
    }

    resolve_module_dir(api);
    LOGI("Module registered; legacy hooks are disabled by default");
}
