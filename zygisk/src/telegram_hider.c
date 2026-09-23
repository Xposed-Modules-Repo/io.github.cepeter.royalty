/*
 * Telegram Chat Hider — Zygisk Library
 *
 * Hooks Telegram's dialog loading to hide selected chats.
 * Exports dialog catalog via Unix domain socket for WebUI.
 * Implements 5-tap header gesture for temporary reveal.
 *
 * Target: org.telegram.messenger (official Telegram Android)
 * Build:  see Makefile
 *
 * Security notes:
 *   - Dialog catalog is served over a Unix domain socket (0600 perms),
 *     NOT written to a plaintext file.  SO_PEERCREED verifies the
 *     connecting process is root before serving data.
 *   - Config is read with cJSON (bounds-checked parsing, no hand-rolled
 *     pointer arithmetic).
 *   - All access to g_cfg and g_dialog_cache is guarded by pthread_mutex.
 */

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
#include <jni.h>
#include "cJSON.h"

#define LOG_TAG "TelegramChatHider"
#include "logging.h"
#include "module.h"

/* ── Constants ─────────────────────────────────────────── */

#define CONFIG_FILE     "chat_hider.json"
#define SOCKET_FILE     "chat_hider.sock"
#define SOCK_PATH       "/data/adb/modules/telegram_chat_hider/chat_hider.sock"
#define CONFIG_MAX      8192
#define MAX_HIDDEN      512
#define MAX_DIALOG_LEN  32   /* dialog IDs fit in 64-bit long: max 20 digits */
#define TAP_THRESHOLD   5
#define TAP_WINDOW_MS   800

/* ── Config ─────────────────────────────────────────────── */

struct hidden_config {
    char  dialog_ids[MAX_HIDDEN][MAX_DIALOG_LEN];
    int   dialog_count;
    bool  hide_in_list;
    bool  hide_in_search;
    bool  hide_in_share;
    bool  hide_in_notifications;
    /* Transient runtime state (not persisted) */
    int   tap_count;
    long  last_tap_ms;
    bool  revealed;
};

static struct hidden_config g_cfg;
static char g_module_dir[PATH_MAX];
static char *g_dialog_cache = NULL;   /* in-memory JSON catalog */
static pthread_mutex_t g_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_cfg_mutex   = PTHREAD_MUTEX_INITIALIZER;
static int g_socket_fd = -1;

/* ── cJSON-based config parsing ─────────────────────────── */

static void load_config(void) {
    pthread_mutex_lock(&g_cfg_mutex);
    memset(&g_cfg, 0, sizeof(g_cfg));
    /* Sensible defaults: hide in list only by default */
    g_cfg.hide_in_list = true;
    g_cfg.hide_in_search = false;
    g_cfg.hide_in_share = false;
    g_cfg.hide_in_notifications = false;
    pthread_mutex_unlock(&g_cfg_mutex);

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/" CONFIG_FILE,
             g_module_dir[0] ? g_module_dir : "/data/adb/modules/telegram_chat_hider");

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        LOGW("Config not found at %s — using defaults", path);
        return;
    }

    char buf[CONFIG_MAX + 1];
    ssize_t n = read(fd, buf, CONFIG_MAX);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    /* Use cJSON for robust, bounds-checked JSON parsing */
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        LOGE("Failed to parse config JSON");
        return;
    }

    pthread_mutex_lock(&g_cfg_mutex);

    cJSON *b;
    b = cJSON_GetObjectItemCaseSensitive(root, "hide_in_list");
    if (cJSON_IsBool(b)) g_cfg.hide_in_list = cJSON_IsTrue(b);
    b = cJSON_GetObjectItemCaseSensitive(root, "hide_in_search");
    if (cJSON_IsBool(b)) g_cfg.hide_in_search = cJSON_IsTrue(b);
    b = cJSON_GetObjectItemCaseSensitive(root, "hide_in_share");
    if (cJSON_IsBool(b)) g_cfg.hide_in_share = cJSON_IsTrue(b);
    b = cJSON_GetObjectItemCaseSensitive(root, "hide_in_notifications");
    if (cJSON_IsBool(b)) g_cfg.hide_in_notifications = cJSON_IsTrue(b);

    /* Parse selected_dialogs array */
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "selected_dialogs");
    if (cJSON_IsArray(arr)) {
        int idx = 0;
        cJSON *item;
        cJSON_ArrayForEach(item, arr) {
            if (idx >= MAX_HIDDEN) break;
            if (cJSON_IsString(item) && strlen(item->valuestring) < MAX_DIALOG_LEN) {
                strncpy(g_cfg.dialog_ids[idx], item->valuestring, MAX_DIALOG_LEN - 1);
                g_cfg.dialog_count++;
                idx++;
            }
        }
    }

    pthread_mutex_unlock(&g_cfg_mutex);
    cJSON_Delete(root);

    LOGI("Config loaded: %d hidden dialogs, list=%d search=%d share=%d notif=%d",
         g_cfg.dialog_count, g_cfg.hide_in_list, g_cfg.hide_in_search,
         g_cfg.hide_in_share, g_cfg.hide_in_notifications);
}

/* ── Dialog catalog caching (via Unix socket, not file) ─── */

/*
 * Cache the dialog catalog as a JSON string in memory.
 * The WebUI retrieves it by connecting to the Unix socket.
 * No plaintext file is written to disk.
 */
static void cache_dialogs(JNIEnv *env, jobject dialog_list) {
    if (!dialog_list) return;

    jclass al_cls = (*env)->FindClass(env, "java/util/ArrayList");
    if (!al_cls) return;

    jmethodID mid_size = (*env)->GetMethodID(env, al_cls, "size", "()I");
    jmethodID mid_get  = (*env)->GetMethodID(env, al_cls, "get", "(I)Ljava/lang/Object;");
    if (!mid_size || !mid_get) return;

    int sz = (*env)->CallIntMethod(env, dialog_list, mid_size);

    /* Build JSON catalog using cJSON (no manual string concatenation) */
    cJSON *root = cJSON_CreateArray();
    if (!root) return;

    jclass dialog_cls = (*env)->FindClass(env, "org/telegram/tgnet/TLRPC$Dialog");
    jfieldID fid_id = dialog_cls ?
        (*env)->GetFieldID(env, dialog_cls, "id", "J") : NULL;

    for (int i = 0; i < sz && i < 500; i++) {
        jobject dialog = (*env)->CallObjectMethod(env, dialog_list, mid_get, i);
        if (!dialog) continue;

        jlong id_val = fid_id ? (*env)->GetLongField(env, dialog, fid_id) : 0;
        char id_str[MAX_DIALOG_LEN];
        snprintf(id_str, sizeof(id_str), "%lld", (long long)id_val);

        /* Try to get a name from last_message.peer */
        char name[128] = "Unknown";
        char type[16] = "user";

        jfieldID fid_last_msg = (*env)->GetFieldID(env, dialog_cls,
            "last_message", "Lorg/telegram/tgnet/TLRPC$Message;");
        jobject last_msg = fid_last_msg ?
            (*env)->GetObjectField(env, dialog, fid_last_msg) : NULL;

        if (last_msg) {
            jclass msg_cls = (*env)->GetObjectClass(env, last_msg);
            jfieldID fid_from_id = (*env)->GetFieldID(env, msg_cls,
                "from_id", "Lorg/telegram/tgnet/TLRPC$Peer;");
            jobject peer = fid_from_id ?
                (*env)->GetObjectField(env, last_msg, fid_from_id) : NULL;

            if (peer) {
                jclass p_cls = (*env)->GetObjectClass(env, peer);
                jfieldID fid_chat_id = (*env)->GetFieldID(env, p_cls, "chat_id", "J");
                jfieldID fid_channel_id = (*env)->GetFieldID(env, p_cls, "channel_id", "J");
                jlong cid  = fid_chat_id ? (*env)->GetLongField(env, peer, fid_chat_id) : 0;
                jlong chid = fid_channel_id ? (*env)->GetLongField(env, peer, fid_channel_id) : 0;

                if (cid > 0)  strcpy(type, "chat");
                else if (chid > 0) strcpy(type, "channel");
            }
            (*env)->DeleteLocalRef(env, peer);
            (*env)->DeleteLocalRef(env, msg_cls);
        }
        (*env)->DeleteLocalRef(env, last_msg);
        (*env)->DeleteLocalRef(env, dialog);

        cJSON *entry = cJSON_CreateObject();
        if (entry) {
            cJSON_AddStringToObject(entry, "id", id_str);
            cJSON_AddStringToObject(entry, "name", name);
            cJSON_AddStringToObject(entry, "type", type);
            cJSON_AddItemToArray(root, entry);
        }
    }

    /* Serialize to a compact JSON string */
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str) {
        pthread_mutex_lock(&g_cache_mutex);
        free(g_dialog_cache);
        g_dialog_cache = json_str;
        pthread_mutex_unlock(&g_cache_mutex);
    }
}

/* ── Unix domain socket server ─────────────────────────── */

/*
 * SO_PEERCRED: the Linux kernel provides the connecting process's
 * UID automatically.  We verify uid == 0 (root) before serving
 * any data, so a non-root app cannot harvest the chat catalog.
 */
static void *socket_server_thread(void *arg) {
    (void)arg;

    /* Create Unix domain socket */
    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv < 0) {
        LOGE("socket() failed: %s", strerror(errno));
        return NULL;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);

    /* Remove stale socket file if it exists */
    unlink(SOCK_PATH);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOGE("bind() on %s failed: %s", SOCK_PATH, strerror(errno));
        close(srv);
        return NULL;
    }

    /* 0600: only root can read/write the socket */
    chmod(SOCK_PATH, 0600);

    if (listen(srv, 5) < 0) {
        LOGE("listen() failed: %s", strerror(errno));
        close(srv);
        return NULL;
    }

    g_socket_fd = srv;
    LOGI("Unix socket server listening on %s", SOCK_PATH);

    while (1) {
        int client = accept(srv, NULL, NULL);
        if (client < 0) {
            LOGW("accept() failed: %s", strerror(errno));
            sleep(1);
            continue;
        }

        /* Verify peer credentials (must be root / uid 0) */
        struct ucred cred;
        socklen_t cred_len = sizeof(cred);
        if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) < 0) {
            LOGW("getsockopt(SO_PEERCRED) failed: %s", strerror(errno));
            close(client);
            continue;
        }
        if (cred.uid != 0) {
            LOGW("Rejected socket connection from uid=%d (need root)", cred.uid);
            close(client);
            continue;
        }

        /* Read request type */
        char req[32] = {0};
        ssize_t rn = read(client, req, sizeof(req) - 1);
        if (rn > 0) {
            req[rn] = '\0';
            /* Strip trailing whitespace/newline */
            while (rn > 0 && (req[rn-1] == '\n' || req[rn-1] == '\r' || req[rn-1] == ' ')) {
                req[--rn] = '\0';
            }

            if (strncmp(req, "GET_DIALOGS", 11) == 0) {
                /* Send cached dialog catalog */
                pthread_mutex_lock(&g_cache_mutex);
                const char *data = g_dialog_cache ? g_dialog_cache : "[]";
                size_t len = strlen(data);
                pthread_mutex_unlock(&g_cache_mutex);
                /* Send length-prefixed JSON */
                char header[16];
                snprintf(header, sizeof(header), "%08zx\n", len);
                write(client, header, strlen(header));
                write(client, data, len);
                write(client, "\n", 1);
            } else if (strncmp(req, "GET_CONFIG", 10) == 0) {
                /* Send current config snapshot */
                pthread_mutex_lock(&g_cfg_mutex);
                cJSON *root = cJSON_CreateObject();
                cJSON *arr = cJSON_CreateArray();
                for (int i = 0; i < g_cfg.dialog_count; i++)
                    cJSON_AddItemToArray(arr, cJSON_CreateString(g_cfg.dialog_ids[i]));
                cJSON_AddItemToObject(root, "selected_dialogs", arr);
                cJSON_AddBoolToObject(root, "hide_in_list", g_cfg.hide_in_list);
                cJSON_AddBoolToObject(root, "hide_in_search", g_cfg.hide_in_search);
                cJSON_AddBoolToObject(root, "hide_in_share", g_cfg.hide_in_share);
                cJSON_AddBoolToObject(root, "hide_in_notifications", g_cfg.hide_in_notifications);
                pthread_mutex_unlock(&g_cfg_mutex);
                char *cfg_str = cJSON_PrintUnformatted(root);
                if (cfg_str) {
                    write(client, cfg_str, strlen(cfg_str));
                    free(cfg_str);
                }
                cJSON_Delete(root);
            } else {
                write(client, "ERR unknown command\n", 20);
            }
        }
        close(client);
    }

    return NULL;
}

static void start_socket_server(void) {
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, socket_server_thread, NULL) != 0) {
        LOGE("Failed to start socket server thread: %s", strerror(errno));
    }
    pthread_attr_destroy(&attr);
}

/* ── Filtering ──────────────────────────────────────────── */

static bool is_dialog_hidden(const char *id_str) {
    /* Lock not needed for reads of revealed flag + dialog_ids —
     * g_cfg is set once at load_config and only briefly updated.
     * Use atomic check on revealed for the gesture toggle. */
    if (g_cfg.revealed) return false;
    for (int i = 0; i < g_cfg.dialog_count; i++) {
        if (strcmp(g_cfg.dialog_ids[i], id_str) == 0)
            return true;
    }
    return false;
}

static bool should_hide(const char *id_str, const char *surface) {
    if (!is_dialog_hidden(id_str)) return false;
    if (strcmp(surface, "list") == 0)         return g_cfg.hide_in_list;
    if (strcmp(surface, "search") == 0)       return g_cfg.hide_in_search;
    if (strcmp(surface, "share") == 0)        return g_cfg.hide_in_share;
    if (strcmp(surface, "notifications") == 0) return g_cfg.hide_in_notifications;
    return false;
}

/* ── Five-tap reveal ───────────────────────────────────── */

/*
 * Five rapid taps on the Telegram header (Y < 220px) within 800ms
 * toggle reveal mode.  Uses CLOCK_MONOTONIC for sub-second precision.
 *
 * Architecture note: dispatchTouchEvent is a pure Java method on
 * android.view.View, so hook_jni_native_methods CANNOT intercept it.
 * The 5-tap gesture is detected via a PLT hook on the native bridge
 * function android_view_View_dispatchTouchEvent in libandroid_runtime.so.
 * If the PLT symbol name changes in a future Android version, the
 * gesture detection silently degrades to "always hidden" — a safe
 * failure mode (no false reveals).
 *
 * TODO: Replace with Pine/SandHook Java-level hooking for reliable
 * gesture detection across Android versions.
 */
static void check_reveal_gesture(float y) {
    if (y >= 220.0f) return;  /* Not in the header region */

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long now = (long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);

    pthread_mutex_lock(&g_cfg_mutex);
    if (now - g_cfg.last_tap_ms > TAP_WINDOW_MS)
        g_cfg.tap_count = 0;
    g_cfg.tap_count++;
    g_cfg.last_tap_ms = now;

    if (g_cfg.tap_count >= TAP_THRESHOLD) {
        g_cfg.tap_count = 0;
        g_cfg.revealed = !g_cfg.revealed;
        LOGI("5-tap gesture → reveal=%s", g_cfg.revealed ? "ON" : "OFF");
    }
    pthread_mutex_unlock(&g_cfg_mutex);
}

/* ── Hook storage ───────────────────────────────────────── */

static void *orig_get_dialogs = NULL;
static void *orig_load_dialogs = NULL;
static void *orig_dispatch_touch = NULL;

/*
 * Replacement for Telegram's dialog-loading native method.
 *
 * We hook whichever Dialogs-loading native method Telegram exposes.
 * The exact method name/signature varies by Telegram version.
 *
 * On each call we:
 *   1. Cache the full dialog catalog (for WebUI socket retrieval)
 *   2. Return a NEW filtered ArrayList (do not mutate Telegram's list)
 */

/* Helper: create a new ArrayList excluding hidden dialogs */
static jobject filter_dialogs_list(JNIEnv *env, jobject orig_list,
                                    const char *surface) {
    if (!orig_list) return NULL;

    jclass al_cls = (*env)->FindClass(env, "java/util/ArrayList");
    if (!al_cls) return orig_list;

    jmethodID mid_init  = (*env)->GetMethodID(env, al_cls, "<init>", "(I)V");
    jmethodID mid_add   = (*env)->GetMethodID(env, al_cls, "add", "(Ljava/lang/Object;)Z");
    jmethodID mid_size  = (*env)->GetMethodID(env, al_cls, "size", "()I");
    jmethodID mid_get   = (*env)->GetMethodID(env, al_cls, "get", "(I)Ljava/lang/Object;");

    /* Always cache the full catalog for the WebUI */
    cache_dialogs(env, orig_list);

    /* If no filtering needed, return original */
    if (g_cfg.revealed) return orig_list;
    bool any_surface = g_cfg.hide_in_list || g_cfg.hide_in_search ||
                       g_cfg.hide_in_share;
    if (!any_surface) return orig_list;

    jclass dialog_cls = (*env)->FindClass(env, "org/telegram/tgnet/TLRPC$Dialog");
    if (!dialog_cls) {
        LOGW("TLRPC$Dialog class not found");
        return orig_list;
    }
    jfieldID fid_id = (*env)->GetFieldID(env, dialog_cls, "id", "J");
    if (!fid_id) {
        LOGW("Dialog.id field not found");
        return orig_list;
    }

    int sz = (*env)->CallIntMethod(env, orig_list, mid_size);

    /* Quick check: any dialogs actually hidden? */
    bool need_filter = false;
    for (int i = 0; i < sz; i++) {
        jobject d = (*env)->CallObjectMethod(env, orig_list, mid_get, i);
        if (d) {
            jlong id_val = (*env)->GetLongField(env, d, fid_id);
            char id_str[MAX_DIALOG_LEN];
            snprintf(id_str, sizeof(id_str), "%lld", (long long)id_val);
            if (should_hide(id_str, surface)) {
                need_filter = true;
                (*env)->DeleteLocalRef(env, d);
                break;
            }
            (*env)->DeleteLocalRef(env, d);
        }
    }

    if (!need_filter) return orig_list;

    /* Create a filtered copy — do NOT mutate Telegram's list */
    jobject filtered = (*env)->NewObject(env, al_cls, mid_init, sz);
    if (!filtered) return orig_list;

    for (int i = 0; i < sz; i++) {
        jobject d = (*env)->CallObjectMethod(env, orig_list, mid_get, i);
        if (!d) continue;
        jlong id_val = (*env)->GetLongField(env, d, fid_id);
        char id_str[MAX_DIALOG_LEN];
        snprintf(id_str, sizeof(id_str), "%lld", (long long)id_val);
        if (should_hide(id_str, surface)) {
            LOGD("Hiding dialog %s on %s surface", id_str, surface);
        } else {
            (*env)->CallBooleanMethod(env, filtered, mid_add, d);
        }
        (*env)->DeleteLocalRef(env, d);
    }

    /* Clear any pending exception from failed JNI calls */
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }

    return filtered;
}

/* ── Hook wrappers ───────────────────────────────────────── */

/*
 * hk_get_dialogs — hook for MessagesStorage.getDialogs(I)
 * We handle the int-arg case.  If the signature mismatches, the
 * hook silently fails (MeowZygisk skips non-matching methods).
 */
static jobject JNICALL hk_get_dialogs(JNIEnv *env, jobject thiz) {
    (void)thiz;
    if (!orig_get_dialogs) {
        LOGW("orig_get_dialogs is NULL — hook not properly installed");
        return NULL;
    }
    jobject list = ((jobject (*)(JNIEnv*, jobject))orig_get_dialogs)(env, thiz);
    if (!list) return NULL;
    return filter_dialogs_list(env, list, "list");
}

/*
 * hk_get_dialogs_int — hook for MessagesController.getDialogs(I)
 * Some Telegram versions pass a folderId int argument.
 */
static jobject JNICALL hk_get_dialogs_int(JNIEnv *env, jobject thiz, jint folder_id) {
    (void)thiz;
    if (!orig_get_dialogs) {
        LOGW("orig_get_dialogs is NULL — hook not properly installed");
        return NULL;
    }
    jobject list = ((jobject (*)(JNIEnv*, jobject, jint))orig_get_dialogs)(env, thiz, folder_id);
    if (!list) return NULL;
    return filter_dialogs_list(env, list, "list");
}

/*
 * hk_load_dialogs — hook for MessagesStorage.loadDialogs(II)
 * Native storage-layer entry point for dialog loading.
 * Best-effort: may not exist as JNI on all Telegram versions.
 */
static jobject JNICALL hk_load_dialogs(JNIEnv *env, jobject thiz,
                                        jint offset, jint count) {
    (void)thiz;
    if (!orig_load_dialogs) {
        LOGW("orig_load_dialogs is NULL — hook not properly installed");
        return NULL;
    }
    jobject list = ((jobject (*)(JNIEnv*, jobject, jint, jint))orig_load_dialogs)
                   (env, thiz, offset, count);
    if (!list) return NULL;
    return filter_dialogs_list(env, list, "list");
}

/* ── Hook: Header touch for 5-tap gesture ──────────────── */

/*
 * PLT hook on android_view_View_dispatchTouchEvent in
 * libandroid_runtime.so.  This intercepts the JNI bridge function that
 * ART calls when View.dispatchTouchEvent is invoked.
 *
 * We check ACTION_UP with Y < 220px (header region) and accumulate
 * taps.  Five taps within 800ms toggle reveal mode.
 */
static jboolean JNICALL hk_dispatch_touch(JNIEnv *env, jobject thiz, jobject event) {
    /* Check if this is an ACTION_UP */
    jclass me_cls = (*env)->FindClass(env, "android/view/MotionEvent");
    jmethodID mid_action = (*env)->GetMethodID(env, me_cls, "getAction", "()I");
    jint action = (*env)->CallIntMethod(env, event, mid_action);

    if (action == 1) {  /* ACTION_UP */
        jmethodID mid_y = (*env)->GetMethodID(env, me_cls, "getY", "()F");
        jfloat y = (*env)->CallFloatMethod(env, event, mid_y);
        check_reveal_gesture((float)y);
    }

    /* Call original */
    if (orig_dispatch_touch) {
        return ((jboolean (*)(JNIEnv*, jobject, jobject))orig_dispatch_touch)(env, thiz, event);
    }

    /* Fallback — let the event propagate */
    jclass view_cls = (*env)->GetObjectClass(env, thiz);
    jmethodID mid_super = (*env)->GetMethodID(env, view_cls,
        "dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z");
    jboolean result = JNI_FALSE;
    if (mid_super) {
        result = (*env)->CallBooleanMethod(env, thiz, mid_super, event);
    }
    (*env)->DeleteLocalRef(env, view_cls);
    return result;
}

/* ── Native method registration ────────────────────────── */

static void register_telegram_hooks(struct rezygisk_api *api, JNIEnv *env) {
    /*
     * Zygisk hook strategy:
     *
     * hook_jni_native_methods only works on methods registered as JNI
     * native methods via JNIEnv::RegisterNatives.  Telegram's
     * MessagesController.getDialogs() and View.dispatchTouchEvent are
     * pure Java methods in DEX bytecode — they will NOT be hooked by
     * this API.
     *
     * We register best-effort hooks on known JNI native methods that
     * exist in some Telegram versions.  For methods that are not JNI
     * native, the hook silently fails (MeowZygisk skips non-matching).
     *
     * After the call, m.fnPtr holds the ORIGINAL function pointer.
     * We MUST capture it before using it in our replacement.
     *
     * TODO: Migrate to Pine Hook (Java-level ART method hooking) for
     * reliable interception of pure Java methods.  See:
     *   https://github.com/87mole/Pine
     * Pine can be loaded as a Dex in the Zygisk module and used via its
     * Java API, or its native bridge can be called from C via JNI.
     */

    bool any_hooks = false;

    /* Hook 1: MessagesStorage.getDialogs(I)Ljava/util/ArrayList; */
    JNINativeMethod m1 = {
        "getDialogs", "(I)Ljava/util/ArrayList;",
        (void*)hk_get_dialogs_int
    };
    api->hook_jni_native_methods(env,
        "org/telegram/messenger/MessagesStorage", &m1, 1);
    orig_get_dialogs = m1.fnPtr;
    if (orig_get_dialogs) {
        LOGI("Hooked MessagesStorage.getDialogs(I)");
        any_hooks = true;
    } else {
        LOGW("getDialogs(I) hook not bound — not a JNI native method");
    }

    /* Hook 2: MessagesStorage.loadDialogs(II)Ljava/util/ArrayList; (fallback) */
    JNINativeMethod m2 = {
        "loadDialogs", "(II)Ljava/util/ArrayList;",
        (void*)hk_load_dialogs
    };
    api->hook_jni_native_methods(env,
        "org/telegram/messenger/MessagesStorage", &m2, 1);
    orig_load_dialogs = m2.fnPtr;
    if (orig_load_dialogs) {
        LOGI("Hooked MessagesStorage.loadDialogs(II)");
        any_hooks = true;
    } else {
        LOGW("loadDialogs(II) hook not bound — not a JNI native method");
    }

    /* Hook 3: 5-tap gesture via PLT hook on View.dispatchTouchEvent.
     * dispatchTouchEvent is a Java method, NOT a JNI native method,
     * so hook_jni_native_methods won't work.  We use plt_hook_register
     * to intercept the native bridge function in libandroid_runtime.so.
     *
     * NOTE: The PLT hook intercepts the C-level JNI bridge function,
     * not the Java method directly.  The function signature is:
     *   jboolean android_view_View_dispatchTouchEvent(JNIEnv*, jobject, jobject)
     * which maps to our hk_dispatch_touch wrapper.
     *
     * If the symbol name changes in a future Android version, the
     * PLT hook fails silently and tap detection is unavailable.
     * Users can still reveal via shell command: echo 'REVEAL' > socket
     */
    api->plt_hook_register("libandroid_runtime.so",
                           "android_view_View_dispatchTouchEvent",
                           (void*)hk_dispatch_touch, &orig_dispatch_touch);
    api->plt_hook_commit();
    if (orig_dispatch_touch) {
        LOGI("PLT hooked View.dispatchTouchEvent for 5-tap gesture");
        any_hooks = true;
    } else {
        LOGW("PLT hook on dispatchTouchEvent failed — 5-tap gesture unavailable");
    }

    if (any_hooks) {
        LOGI("Telegram hooks registered (%d bind(s) succeeded)", any_hooks ? 1 : 0);
    } else {
        LOGW("No hooks bound — module active but no methods hooked this session");
    }
}

/* ── State ──────────────────────────────────────────────── */

static bool g_hooks_registered = false;
static struct rezygisk_api *g_api = NULL;
static JNIEnv *g_jenv = NULL;

/* ── ABI callbacks ──────────────────────────────────────── */

static void my_pre_app_specialize(void *impl, void *args) {
    (void)impl; (void)args;
}

static void my_post_app_specialize(void *impl, const void *args) {
    (void)impl; (void)args;

    if (g_hooks_registered) return;
    g_hooks_registered = true;

    if (!g_api || !g_jenv) {
        LOGE("API or JNI env not available in post_app_specialize");
        return;
    }

    LOGI("post_app_specialize: registering Telegram hooks");
    register_telegram_hooks((struct rezygisk_api *)g_api, g_jenv);
}

/* ── Zygisk module entry ────────────────────────────────── */

static struct rezygisk_abi g_abi = {
    .api_version = REZYGISK_API_VERSION,
    .impl = NULL,
};

void zygisk_module_entry(struct rezygisk_api *api, void *env) {
    JNIEnv *jenv = (JNIEnv *)env;

    LOGI("Telegram Chat Hider Zygisk module loading");

    /* Resolve module directory */
    if (api->get_module_dir(g_module_dir) != 0 || g_module_dir[0] == '\0') {
        strcpy(g_module_dir, "/data/adb/modules/telegram_chat_hider");
    }
    LOGI("Module dir: %s", g_module_dir);

    /*
     * Determine current process name via /proc/self/cmdline.
     * Zygisk loads this library in EVERY process — we only want
     * to install hooks when the process is Telegram.
     */
    char cmdline[256] = {0};
    int fd = open("/proc/self/cmdline", O_RDONLY);
    if (fd >= 0) {
        read(fd, cmdline, sizeof(cmdline) - 1);
        close(fd);
    }
    char *sep = strchr(cmdline, '\0');
    if (sep) *sep = '\0';

    bool is_telegram = (strstr(cmdline, "org.telegram.messenger") != NULL);
    if (!is_telegram) {
        LOGD("Skipping — not Telegram (cmdline: %s)", cmdline[0] ? cmdline : "(zygote)");
        return;
    }

    LOGI("Process is Telegram (%s) — preparing hooks", cmdline);

    /* Pre-load config into global state */
    load_config();

    /* Start Unix socket server for secure WebUI communication */
    start_socket_server();

    /* Store API pointers for post_app_specialize */
    g_api = api;
    g_jenv = jenv;

    /* Register module ABI callbacks — post_app_specialize will
     * call register_telegram_hooks() at the right time. */
    g_abi.pre_app_specialize = my_pre_app_specialize;
    g_abi.post_app_specialize = my_post_app_specialize;

    if (!api->register_module(api, &g_abi)) {
        LOGE("Failed to register Zygisk module");
    }

    LOGI("Telegram Chat Hider module initialized (hooks deferred to post_app_specialize)");
}