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
#include <sys/un.h>
#include <pthread.h>

#include "cJSON.h"
#include "art_hook.h"
#include "module.h"

#define LOG_TAG "TelegramChatHider"
#include "logging.h"

/* ── Constants ─────────────────────────── */
#define SOCK_PATH "/data/adb/modules/telegram_chat_hider/chat_hider.sock"
#define CONFIG_MAX 8192
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
    int     tap_count;
    int64_t last_tap_ms;
    bool    revealed;
};

static struct hidden_config g_cfg;
static char g_module_dir[PATH_MAX];
static pthread_mutex_t g_cfg_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_cat_mtx = PTHREAD_MUTEX_INITIALIZER;
static char *g_dialog_cache = NULL;       /* in-memory JSON catalog */

static JavaVM *g_jvm = NULL;
static bool g_hooks_registered = false;
static pthread_once_t g_jvm_once = PTHREAD_ONCE_INIT;

/* NotificationCenter constant IDs (resolved at runtime via reflection) */
static int g_nc_did_receive_new_messages = -1;
static int g_nc_push_messages_updated = -1;

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

    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "selected_dialogs");
    if (cJSON_IsArray(arr)) {
        int idx = 0;
        cJSON *item;
        cJSON_ArrayForEach(item, arr) {
            if (idx >= MAX_HIDDEN) break;
            if (cJSON_IsNumber(item)) {
                g_cfg.hidden_ids[idx++] = (int64_t)item->valuedouble;
            } else if (cJSON_IsString(item)) {
                g_cfg.hidden_ids[idx++] = (int64_t)strtoll(item->valuestring, NULL, 10);
            }
        }
        g_cfg.hidden_count = idx;
    }

    pthread_mutex_unlock(&g_cfg_mtx);
    cJSON_Delete(root);
    LOGI("Config: %d hidden, list=%d notif=%d",
         g_cfg.hidden_count, g_cfg.hide_in_list, g_cfg.hide_in_notifications);
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

    jclass al = (*env)->FindClass(env, "java/util/ArrayList");
    jmethodID sz = (*env)->GetMethodID(env, al, "size", "()I");
    jmethodID getm = (*env)->GetMethodID(env, al, "get", "(I)Ljava/lang/Object;");
    jmethodID remm = (*env)->GetMethodID(env, al, "remove", "(I)Ljava/lang/Object;");
    if (!sz || !getm || !remm) return;

    /* Cache full catalog before filtering */
    jclass dlg_cls = (*env)->FindClass(env, "org/telegram/tgnet/TLRPC$Dialog");
    jfieldID id_fid = dlg_cls ? (*env)->GetFieldID(env, dlg_cls, "id", "J") : NULL;

    /* Build catalog from full list */
    cJSON *arr = cJSON_CreateArray();
    jint n = (*env)->CallIntMethod(env, list, sz);
    for (jint i = 0; i < n && i < 500; i++) {
        jobject d = (*env)->CallObjectMethod(env, list, getm, i);
        if (d) {
            jlong id_val = id_fid ? (*env)->GetLongField(env, d, id_fid) : 0;
            cJSON *entry = cJSON_CreateObject();
            cJSON *id_item = cJSON_CreateNumber((double)id_val);
            if (id_item) cJSON_AddItemToObject(entry, "id", id_item);
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
    if (g_cfg.revealed || !should_hide_surface(surface)) {
        if (dlg_cls) (*env)->DeleteLocalRef(env, dlg_cls);
        return;
    }

    for (jint i = n - 1; i >= 0; i--) {
        jobject d = (*env)->CallObjectMethod(env, list, getm, i);
        if (!d) continue;
        jlong id_val = id_fid ? (*env)->GetLongField(env, d, id_fid) : 0;
        if (id_val != 0 && is_hidden((int64_t)id_val)) {
            (*env)->CallObjectMethod(env, list, remm, i);
            LOGD("Filtered dialog %lld on %s", (long long)id_val, surface);
        }
        (*env)->DeleteLocalRef(env, d);
    }

    if (dlg_cls) (*env)->DeleteLocalRef(env, dlg_cls);
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
    chmod(SOCK_PATH, 0600);
    if (listen(srv, 5) < 0) { LOGE("listen: %s", strerror(errno)); close(srv); return NULL; }
    LOGI("Socket server on %s", SOCK_PATH);

    for (;;) {
        int client = accept(srv, NULL, NULL);
        if (client < 0) { LOGW("accept: %s", strerror(errno)); sleep(1); continue; }

        struct ucred cred;
        socklen_t cl = sizeof(cred);
        if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &cred, &cl) == 0 && cred.uid != 0) {
            LOGW("Reject uid=%d", cred.uid); close(client); continue;
        }

        char req[32] = {0};
        ssize_t rn = read(client, req, sizeof(req) - 1);
        if (rn > 0) {
            req[rn] = '\0';
            for (ssize_t i = rn - 1; i >= 0 && (req[i]=='\n'||req[i]=='\r'||req[i]==' '); i--)
                req[i] = '\0';

            pthread_mutex_lock(&g_cat_mtx);
            const char *data = g_dialog_cache ? g_dialog_cache : "[]";
            pthread_mutex_unlock(&g_cat_mtx);
            write(client, data, strlen(data));
            write(client, "\n", 1);
        }
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

    if (env && event) {
        jclass mc = (*env)->FindClass(env, "android/view/MotionEvent");
        if (mc) {
            jmethodID am = (*env)->GetStaticMethodID(env, mc, "getActionMasked", "(I)I");
            jfieldID af = (*env)->GetStaticFieldID(env, mc, "ACTION_DOWN", "I");
            jfieldID yf = (*env)->GetFieldID(env, mc, "mY", "F");

            if (am && af) {
                jint action_down = (*env)->GetStaticIntField(env, mc, af);
                jint raw = (*env)->CallStaticIntMethod(env, mc, am, 0);
                jint masked = raw & 0xFFF;
                if (masked == action_down) {
                    float y = yf ? (*env)->GetFloatField(env, event, yf) : 0;
                    if (y >= 0 && y < 220.0f) {  /* header region */
                        struct timespec ts;
                        clock_gettime(CLOCK_MONOTONIC, &ts);
                        int64_t now = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

                        pthread_mutex_lock(&g_cfg_mtx);
                        if (now - g_cfg.last_tap_ms > TAP_WINDOW_MS)
                            g_cfg.tap_count = 0;
                        g_cfg.tap_count++;
                        g_cfg.last_tap_ms = now;
                        if (g_cfg.tap_count >= 5) {
                            g_cfg.tap_count = 0;
                            g_cfg.revealed = !g_cfg.revealed;
                            LOGI("5-tap → reveal=%s", g_cfg.revealed ? "ON" : "OFF");
                        }
                        pthread_mutex_unlock(&g_cfg_mtx);
                    }
                }
            }
            (*env)->DeleteLocalRef(env, mc);
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

/* ── Hook registration (ART entry-point replacement) ────────── */

static void register_telegram_hooks(JNIEnv *env) {
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

static void my_pre_app_specialize(void *impl, void *args) { (void)impl; (void)args; }

static void my_post_app_specialize(void *impl, const void *args) {
    (void)impl; (void)args;
    if (g_hooks_registered) return;
    g_hooks_registered = true;

    JNIEnv *env = get_env();
    if (!env) { LOGE("Cannot attach thread for hooks"); return; }

    LOGI("post_app_specialize: registering hooks (ART entry-point replacement)");
    register_telegram_hooks(env);
    start_socket_server();
}

/* ── Zygisk entry ─────────────── */

static struct rezygisk_abi g_abi = {
    .api_version = REZYGISK_API_VERSION,
    .impl = NULL,
};

void zygisk_module_entry(struct rezygisk_api *api, void *env) {
    JNIEnv *jenv = (JNIEnv *)env;
    LOGI("Telegram Chat Hider loading");

    if (api->get_module_dir(g_module_dir) != 0 || g_module_dir[0] == '\0')
        strcpy(g_module_dir, "/data/adb/modules/telegram_chat_hider");

    char cmdline[256] = {0};
    int fd = open("/proc/self/cmdline", O_RDONLY);
    if (fd >= 0) { read(fd, cmdline, sizeof(cmdline)-1); close(fd); }
    char *sep = strchr(cmdline, '\0');
    if (sep) *sep = '\0';

    if (strstr(cmdline, "org.telegram.messenger") == NULL) {
        LOGD("Skipping (not Telegram)");
        return;
    }

    LOGI("Telegram process detected — installing hooks");
    load_config();

    if (jenv) {
        (*jenv)->GetJavaVM(jenv, &g_jvm);
    }

    g_abi.pre_app_specialize  = my_pre_app_specialize;
    g_abi.post_app_specialize = my_post_app_specialize;
    if (!api->register_module(api, &g_abi))
        LOGE("Failed to register Zygisk module");

    LOGI("Module initialized (hooks on post_app_specialize)");
}
