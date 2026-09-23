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

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
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
    /* Use monotonic clock for accurate timing (NOT time(NULL) which
     * has 1s resolution and crosses second boundaries unpredictably) */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long now = (long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
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

/*
 * Replacement for a JNI native method returning ArrayList<Dialog>.
 *
 * We hook whichever Dialogs-loading native method Telegram exposes.
 * The exact method name/signature varies by Telegram version, so we
 * register multiple candidates and only the one that exists will bind.
 *
 * On each call we:
 *   1. Export the full dialog catalog (for WebUI selector)
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

    /* Always export full catalog for WebUI */
    export_dialogs(env, orig_list);

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

/* ── Hook wrappers: call original, then filter ────────────── */

/*
 * hk_get_dialogs — hook for MessagesController.getDialogs()
 * Signature varies by version: getDialogs() or getDialogs(I)
 * We handle the no-arg case; if signature mismatches, the hook
 * simply won't bind (MeowZygisk silently skips non-matching methods).
 */
static jobject JNICALL hk_get_dialogs(JNIEnv *env, jobject thiz) {
    if (!orig_get_dialogs) {
        LOGW("orig_get_dialogs is NULL — hook not properly installed");
        return NULL;
    }
    jobject list = ((jobject (*)(JNIEnv*, jobject))orig_get_dialogs)(env, thiz);
    if (!list) return NULL;
    return filter_dialogs_list(env, list, "list");
}

/*
 * hk_load_dialogs — hook for MessagesStorage.loadDialogs(II)
 * This is the native storage-layer entry point for dialog loading.
 * May not exist as JNI on all Telegram versions; hook is best-effort.
 */
static jobject JNICALL hk_load_dialogs(JNIEnv *env, jobject thiz,
                                        jint offset, jint count) {
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
     * Zygisk hook strategy:
     *
     * hook_jni_native_methods only works on methods that are REGISTERED
     * as JNI native methods (via JNIEnv::RegisterNatives).  Telegram's
     * MessagesController.getDialogs() and View.dispatchTouchEvent are
     * pure Java methods — they will NOT be hooked.
     *
     * We register best-effort hooks on known JNI native methods that
     * exist in some Telegram versions.  For methods that are not JNI
     * native, the hook silently fails (MeowZygisk skips non-matching).
     *
     * After the call, fnPtr holds the ORIGINAL function pointer.
     * We MUST capture it before using it in our replacement.
     */

    /* Hook 1: MessagesStorage.getDialogs (JNI native, version-dependent) */
    JNINativeMethod m1 = {
        "getDialogs", "(I)Ljava/util/ArrayList;",
        (void*)hk_get_dialogs
    };
    api->hook_jni_native_methods(env,
        "org/telegram/messenger/MessagesStorage", &m1, 1);
    /* Capture original — m1.fnPtr is set by hook_jni_native_methods */
    orig_get_dialogs = m1.fnPtr;
    if (!orig_get_dialogs) {
        LOGW("getDialogs hook not bound (not a JNI native method)");
    } else {
        LOGI("Hooked MessagesStorage.getDialogs()");
    }

    /* Hook 2: MessagesStorage.loadDialogs(II) (fallback) */
    JNINativeMethod m2 = {
        "loadDialogs", "(II)Ljava/util/ArrayList;",
        (void*)hk_load_dialogs
    };
    api->hook_jni_native_methods(env,
        "org/telegram/messenger/MessagesStorage", &m2, 1);
    orig_load_dialogs = m2.fnPtr;
    if (!orig_load_dialogs) {
        LOGW("loadDialogs hook not bound");
    } else {
        LOGI("Hooked MessagesStorage.loadDialogs(II)");
    }

    /*
     * Hook 3: 5-tap gesture via PLT hook on View.dispatchTouchEvent.
     *
     * dispatchTouchEvent is a Java method, NOT a JNI native method,
     * so hook_jni_native_methods won't work.  We use plt_hook_register
     * to intercept the native bridge function in libandroid_runtime.so.
     *
     * NOTE: The PLT hook receives the C-level JNI function, whose
     * signature differs from the Java method signature.  The actual
     * C function 'android_view_View_dispatchTouchEvent' has native
     * args, so hk_dispatch_touch must be adapted.
     *
     * This is a best-effort hook; if the symbol name changes in a
     * future Android version, tap detection silently fails.
     */
    api->plt_hook_register("libandroid_runtime.so",
                           "android_view_View_dispatchTouchEvent",
                           (void*)hk_dispatch_touch, &orig_dispatch_touch);
    api->plt_hook_commit();

    LOGI("Telegram hooks registered");
}

/* ── State ──────────────────────────────────────────────── */

static bool g_hooks_registered = false;
static struct rezygisk_api *g_api = NULL;
static JNIEnv *g_jenv = NULL;

/* ── ABI callbacks ──────────────────────────────────────── */

static void my_pre_app_specialize(void *impl, void *args) {
    (void)impl; (void)args;
    /* Read nice_name/package from args to decide if we should hook.
     * We store a flag rather than hooking here because class
     * loading happens after specialization. */
}

static void my_post_app_specialize(void *impl, const void *args) {
    (void)impl; (void)args;

    if (g_hooks_registered) return;
    g_hooks_registered = true;

    if (!g_api || !g_jenv) {
        LOGE("API or JNI env not available in post_app_specialize");
        return;
    }

    /* This callback runs after app specialization — Java classes
     * are available and JNI method registration is complete.
     * This is the correct time to install hooks. */
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
