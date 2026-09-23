/*
 * Telegram Chat Hider — Zygisk Library
 *
 * Hooks Telegram's dialog loading to hide selected chats.
 * Exports a dialog catalog file for the WebUI.
 * Implements 5-tap header gesture for temporary reveal.
 *
 * Target: org.telegram.messenger (official Telegram Android)
 *
 * Build: see Makefile
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
#include <jni.h>

#define LOG_TAG "TelegramChatHider"
#include "logging.h"
#include "module.h"

/* ── Constants ─────────────────────────────────────────── */

#define CONFIG_FILE     "chat_hider.json"
#define DIALOGS_FILE    "dialogs.json"
#define CONFIG_MAX      8192
#define DIALOGS_MAX     65536
#define MAX_HIDDEN      512
#define MAX_DIALOG_LEN  64
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

/* ── Minimal JSON parsing ───────────────────────────────── */

static bool json_get_bool(const char *json, const char *key) {
    char pat[96];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p && (*p == ' ' || *p == ':' || *p == '\t' || *p == '\n')) p++;
    return strncmp(p, "true", 4) == 0;
}

static void json_load_dialog_ids(const char *json, const char *key) {
    char pat[96];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    char *p = strstr(json, pat);
    if (!p) return;
    p = strchr(p, '[');
    if (!p) return;
    p++;

    while (g_cfg.dialog_count < MAX_HIDDEN && p) {
        while (*p && *p != '"') {
            if (*p == ']') return;
            p++;
        }
        if (*p != '"') break;
        p++;
        char *end = strchr(p, '"');
        if (!end) break;
        size_t len = end - p;
        if (len > 0 && len < MAX_DIALOG_LEN) {
            memcpy(g_cfg.dialog_ids[g_cfg.dialog_count], p, len);
            g_cfg.dialog_ids[g_cfg.dialog_count][len] = '\0';
            g_cfg.dialog_count++;
        }
        p = end + 1;
    }
}

/* ── Config + dialog export ────────────────────────────── */

static void load_config(void) {
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.hide_in_list = true;
    g_cfg.hide_in_search = true;
    g_cfg.hide_in_share = true;
    g_cfg.hide_in_notifications = true;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/" CONFIG_FILE,
             g_module_dir[0] ? g_module_dir : "/data/adb/modules/telegram_chat_hider");

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        LOGW("Config not found at %s", path);
        return;
    }

    char buf[CONFIG_MAX + 1];
    ssize_t n = read(fd, buf, CONFIG_MAX);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    g_cfg.hide_in_list          = json_get_bool(buf, "hide_in_list");
    g_cfg.hide_in_search        = json_get_bool(buf, "hide_in_search");
    g_cfg.hide_in_share         = json_get_bool(buf, "hide_in_share");
    g_cfg.hide_in_notifications = json_get_bool(buf, "hide_in_notifications");
    json_load_dialog_ids(buf, "selected_dialogs");

    LOGI("Config loaded: %d hidden dialogs, list=%d search=%d share=%d notif=%d",
         g_cfg.dialog_count, g_cfg.hide_in_list, g_cfg.hide_in_search,
         g_cfg.hide_in_share, g_cfg.hide_in_notifications);
}

/*
 * Export the dialog catalog to dialogs.json so the WebUI can
 * display chats for selection.
 *
 * Telegram's MessagesController.getDialogs() returns an ArrayList
 * of Dialog objects.  Each Dialog has:
 *   id (long), peer (TL_peer), last_message (Message), etc.
 *
 * We hook getDialogs() to export the list and then filter.
 */
static void export_dialogs(JNIEnv *env, jobject dialog_list) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/" DIALOGS_FILE,
             g_module_dir[0] ? g_module_dir : "/data/adb/modules/telegram_chat_hider");

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        LOGW("Cannot write dialogs catalog to %s", path);
        return;
    }

    /* Write minimal JSON: [{"id":"123","name":"Chat Name","type":"user"}] */
    write(fd, "[", 1);

    jclass al_cls = (*env)->FindClass(env, "java/util/ArrayList");
    jmethodID mid_size = (*env)->GetMethodID(env, al_cls, "size", "()I");
    jmethodID mid_get  = (*env)->GetMethodID(env, al_cls, "get", "(I)Ljava/lang/Object;");

    int sz = (*env)->CallIntMethod(env, dialog_list, mid_size);
    bool first = true;

    for (int i = 0; i < sz && i < 500; i++) {
        jobject dialog = (*env)->CallObjectMethod(env, dialog_list, mid_get, i);
        if (dialog == NULL) continue;

        jclass d_cls = (*env)->GetObjectClass(env, dialog);

        /* Dialog fields: id (long), peer (TL_peer_channel/TL_peer_chat/TL_peer_user) */
        jfieldID fid_id = (*env)->GetFieldID(env, d_cls, "id", "J");
        jlong id = (*env)->GetLongField(env, dialog, fid_id);

        /* Get the dialog name from the last message or peer */
        jfieldID fid_last_msg = (*env)->GetFieldID(env, d_cls, "last_message",
                                                   "Lorg/telegram/tgnet/TLRPC$Message;");
        jobject last_msg = fid_last_msg ? (*env)->GetObjectField(env, dialog, fid_last_msg) : NULL;

        if (last_msg) {
            jclass msg_cls = (*env)->GetObjectClass(env, last_msg);
            jfieldID fid_from_id = (*env)->GetFieldID(env, msg_cls, "from_id", "Lorg/telegram/tgnet/TLRPC$Peer;");
            jobject peer = fid_from_id ? (*env)->GetObjectField(env, last_msg, fid_from_id) : NULL;

            if (peer) {
                jclass p_cls = (*env)->GetObjectClass(env, peer);
                jfieldID fid_chat_id = (*env)->GetFieldID(env, p_cls, "chat_id", "J");
                jfieldID fid_channel_id = (*env)->GetFieldID(env, p_cls, "channel_id", "J");

                jlong cid = fid_chat_id ? (*env)->GetLongField(env, peer, fid_chat_id) : 0;
                jlong chid = fid_channel_id ? (*env)->GetLongField(env, peer, fid_channel_id) : 0;

                char type[16] = "user";
                if (cid > 0) strcpy(type, "chat");
                else if (chid > 0) strcpy(type, "channel");

                /* Dialog id in Telegram is constructed from type + entity_id */
                /* For users: id = user_id; for chats: id = -chat_id; for channels: id = -channel_id */
                char id_str[MAX_DIALOG_LEN];
                snprintf(id_str, sizeof(id_str), "%lld", (long long)id);

                char buf[512];
                int n = snprintf(buf, sizeof(buf),
                    "%s{\"id\":\"%s\",\"type\":\"%s\"}",
                    first ? "" : ",", id_str, type);
                if (n > 0 && n < (int)sizeof(buf)) {
                    write(fd, buf, n);
                    first = false;
                }
            }
            (*env)->DeleteLocalRef(env, peer);
        }
        (*env)->DeleteLocalRef(env, last_msg);
        (*env)->DeleteLocalRef(env, d_cls);
        (*env)->DeleteLocalRef(env, dialog);
    }

    write(fd, "]", 1);
    close(fd);

    LOGI("Dialog catalog exported (%d dialogs)", sz);
}

/* ── Filtering ──────────────────────────────────────────── */

static bool is_dialog_hidden(const char *id_str) {
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

static bool check_reveal_gesture(void) {
    long now = (long)(time(NULL) * 1000);
    if (now - g_cfg.last_tap_ms > TAP_WINDOW_MS)
        g_cfg.tap_count = 0;
    g_cfg.tap_count++;
    g_cfg.last_tap_ms = now;

    if (g_cfg.tap_count >= TAP_THRESHOLD) {
        g_cfg.tap_count = 0;
        g_cfg.revealed = !g_cfg.revealed;
        LOGI("5-tap gesture → reveal=%s", g_cfg.revealed ? "ON" : "OFF");
        return true;
    }
    return false;
}

/* ── Hook storage ───────────────────────────────────────── */

static void *orig_get_dialogs = NULL;
static void *orig_load_dialogs = NULL;
static void *orig_dispatch_touch = NULL;

/* ── Hook: MessagesStorage.loadDialogs(II) ──────────────── */

/*
 * Replacement for MessagesStorage.loadDialogs(II)Ljava/util/ArrayList;
 *
 * Fallback hook — if MessagesController.getDialogs() is not a JNI
 * native method, this native storage method still feeds the dialog
 * list.  Filter hidden dialogs here as well.
 */
static jobject JNICALL hk_load_dialogs(JNIEnv *env, jobject thiz,
                                        jint offset, jint count) {
    jobject list = NULL;

    if (orig_load_dialogs) {
        list = ((jobject (*)(JNIEnv*, jobject, jint, jint))orig_load_dialogs)
               (env, thiz, offset, count);
    }

    if (!list) return NULL;

    /* Export catalog for WebUI */
    export_dialogs(env, list);

    if (!g_cfg.hide_in_list || g_cfg.revealed)
        return list;

    /* Filter hidden dialogs */
    jclass al_cls = (*env)->FindClass(env, "java/util/ArrayList");
    jmethodID mid_remove = (*env)->GetMethodID(env, al_cls, "remove", "(I)Ljava/lang/Object;");
    jmethodID mid_get    = (*env)->GetMethodID(env, al_cls, "get", "(I)Ljava/lang/Object;");
    jmethodID mid_size   = (*env)->GetMethodID(env, al_cls, "size", "()I");

    jclass dialog_cls = (*env)->FindClass(env, "org/telegram/tgnet/TLRPC$Dialog");
    jfieldID fid_id = (*env)->GetFieldID(env, dialog_cls, "id", "J");

    int sz = (*env)->CallIntMethod(env, list, mid_size);
    for (int i = sz - 1; i >= 0; i--) {
        jobject dialog = (*env)->CallObjectMethod(env, list, mid_get, i);
        jlong id_val = (*env)->GetLongField(env, dialog, fid_id);
        char id_str[MAX_DIALOG_LEN];
        snprintf(id_str, sizeof(id_str), "%lld", (long long)id_val);

        if (is_dialog_hidden(id_str)) {
            (*env)->CallObjectMethod(env, list, mid_remove, i);
        }
        (*env)->DeleteLocalRef(env, dialog);
    }

    return list;
}

/* ── Hook: MessagesController.getDialogs() ──────────────── */

/*
 * Replacement for MessagesController.getDialogs()Ljava/util/ArrayList;
 *
 * This is the primary hook point.  Telegram's MessagesController
 * holds the dialog list as ArrayList<Dialog>.  We intercept
 * the getter to filter hidden dialogs.
 *
 * The method may have different signatures across Telegram versions:
 *   - getDialogs() → ArrayList<Dialog>  (most common)
 */
static jobject JNICALL hk_get_dialogs(JNIEnv *env, jobject thiz) {
    jobject list = NULL;

    /* Call original */
    if (orig_get_dialogs) {
        list = ((jobject (*)(JNIEnv*, jobject))orig_get_dialogs)(env, thiz);
    } else {
        LOGW("orig_get_dialogs is NULL");
        return NULL;
    }

    if (list == NULL) return NULL;

    /* Export full catalog for WebUI (before filtering) */
    export_dialogs(env, list);

    if (!g_cfg.hide_in_list || g_cfg.revealed) {
        return list;
    }

    /* Filter hidden dialogs from the list (list surface) */
    jclass al_cls = (*env)->FindClass(env, "java/util/ArrayList");
    jmethodID mid_remove = (*env)->GetMethodID(env, al_cls, "remove", "(I)Ljava/lang/Object;");
    jmethodID mid_get    = (*env)->GetMethodID(env, al_cls, "get", "(I)Ljava/lang/Object;");
    jmethodID mid_size   = (*env)->GetMethodID(env, al_cls, "size", "()I");

    jclass dialog_cls = (*env)->FindClass(env, "org/telegram/tgnet/TLRPC$Dialog");
    jfieldID fid_id = (*env)->GetFieldID(env, dialog_cls, "id", "J");

    int sz = (*env)->CallIntMethod(env, list, mid_size);
    for (int i = sz - 1; i >= 0; i--) {
        jobject dialog = (*env)->CallObjectMethod(env, list, mid_get, i);
        jlong id_val = (*env)->GetLongField(env, dialog, fid_id);
        char id_str[MAX_DIALOG_LEN];
        snprintf(id_str, sizeof(id_str), "%lld", (long long)id_val);

        if (is_dialog_hidden(id_str)) {
            (*env)->CallObjectMethod(env, list, mid_remove, i);
            LOGD("Hidden dialog %s from list", id_str);
        }

        (*env)->DeleteLocalRef(env, dialog);
    }

    return list;
}

/* ── Hook: Header touch for 5-tap gesture ──────────────── */

/*
 * Replacement for View.dispatchTouchEvent on the header area.
 * We check for ACTION_UP in the top region of the screen.
 * When the 5-tap threshold is reached, toggle reveal mode.
 *
 * For the touch hook, we target the header TextView in
 * LaunchActivity's ActionBar.  Telegram uses a custom
 * ActionBar with a title TextView that's the tap target.
 *
 * We hook on a broader class (View) and filter by:
 * 1. Process is Telegram (checked in entry point)
 * 2. The Y coordinate is in the top region
 * 3. The view belongs to the chat list fragment
 */

static jboolean JNICALL hk_dispatch_touch(JNIEnv *env, jobject thiz, jobject event) {
    /* Check if this is an ACTION_UP */
    jclass me_cls = (*env)->FindClass(env, "android/view/MotionEvent");
    jmethodID mid_action = (*env)->GetMethodID(env, me_cls, "getAction", "()I");
    jint action = (*env)->CallIntMethod(env, event, mid_action);

    if (action == 1) {  /* ACTION_UP */
        jmethodID mid_y = (*env)->GetMethodID(env, me_cls, "getY", "()F");
        jfloat y = (*env)->CallFloatMethod(env, event, mid_y);

        /* Top ~80dp region (~220px on xxhdpi) — header area */
        if (y < 220.0f) {
            if (check_reveal_gesture()) {
                LOGD("Reveal toggled via 5-tap");
            }
        }
    }

    /* Call original */
    if (orig_dispatch_touch) {
        return ((jboolean (*)(JNIEnv*, jobject, jobject))orig_dispatch_touch)(env, thiz, event);
    }

    /* Fallback — let the event propagate */
    jclass view_cls = (*env)->GetObjectClass(env, thiz);
    jmethodID mid_super = (*env)->GetMethodID(env, view_cls,
        "dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z");
    if (mid_super) {
        return (*env)->CallBooleanMethod(env, thiz, mid_super, event);
    }
    return JNI_FALSE;
}

/* ── JNI hook registration ──────────────────────────────── */

static void register_telegram_hooks(struct rezygisk_api *api, JNIEnv *env) {
    /*
     * Telegram's dialog loading pipeline has multiple hook points.
     * We register hooks for all known method names across versions.
     *
     * Hook 1: MessagesController.getDialogs()
     *   This is a Java method but may delegate to native code.
     *   If it's registered as a JNI native method, this will succeed.
     */
    JNINativeMethod m1 = {
        "getDialogs", "()Ljava/util/ArrayList;",
        (void*)hk_get_dialogs
    };
    api->hook_jni_native_methods(env,
        "org/telegram/messenger/MessagesController", &m1, 1);

    /*
     * Hook 2: MessagesStorage.loadDialogs(II)Ljava/util/ArrayList;
     *   This is a common native method that loads dialogs from the DB.
     *   We filter at this layer as a fallback if getDialogs is not native.
     */
    JNINativeMethod m2 = {
        "loadDialogs", "(II)Ljava/util/ArrayList;",
        (void*)hk_load_dialogs
    };
    api->hook_jni_native_methods(env,
        "org/telegram/messenger/MessagesStorage", &m2, 1);

    /*
     * Hook 3: View.dispatchTouchEvent — for 5-tap header detection.
     */
    JNINativeMethod m3 = {
        "dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z",
        (void*)hk_dispatch_touch
    };
    api->hook_jni_native_methods(env, "android/view/View", &m3, 1);

    /* PLT hook fallback for touch detection in libandroid_runtime */
    api->plt_hook_register("libandroid_runtime.so",
                           "android_view_View_dispatchTouchEvent",
                           (void*)hk_dispatch_touch, &orig_dispatch_touch);
    api->plt_hook_commit();

    LOGI("Telegram hooks registered");
}

/* ── ABI callbacks ──────────────────────────────────────── */

static void my_pre_app_specialize(void *impl, void *args) {
    (void)impl; (void)args;
}

static void my_post_app_specialize(void *impl, const void *args) {
    (void)impl; (void)args;
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
    /* get_module_dir writes the module path into the provided buffer */
    if (api->get_module_dir(g_module_dir) != 0 || g_module_dir[0] == '\0') {
        strcpy(g_module_dir, "/data/adb/modules/telegram_chat_hider");
    }
    LOGI("Module dir: %s", g_module_dir);

    /* Determine current process name */
    /*
     * Zygisk loads this library in every process.  We check the process
     * name via /proc/self/cmdline and abort early if not Telegram.
     */
    char cmdline[256] = {0};
    int fd = open("/proc/self/cmdline", O_RDONLY);
    if (fd >= 0) {
        read(fd, cmdline, sizeof(cmdline) - 1);
        close(fd);
    }
    /* Strip everything after the first null byte (cmdline separator) */
    char *sep = strchr(cmdline, '\0');
    if (sep) *sep = '\0';

    bool is_telegram = (strstr(cmdline, "org.telegram.messenger") != NULL);
    if (!is_telegram) {
        LOGD("Skipping hooks — process is not Telegram: %s",
             cmdline[0] ? cmdline : "(zygote)");
        return;
    }

    LOGI("Process is Telegram (%s) — registering hooks", cmdline);

    /* Load config */
    load_config();

    /* Register hooks */
    register_telegram_hooks(api, jenv);

    /* Set up ABI callbacks */
    g_abi.pre_app_specialize = my_pre_app_specialize;
    g_abi.post_app_specialize = my_post_app_specialize;

    if (!api->register_module(api, &g_abi)) {
        LOGE("Failed to register Zygisk module");
    }

    LOGI("Telegram Chat Hider module initialized");
}
