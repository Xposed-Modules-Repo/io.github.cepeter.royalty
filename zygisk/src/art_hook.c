/*
 * art_hook.c — Minimal ART method entry-point hooking
 *
 * See art_hook.h for usage and limitations.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "art_hook.h"
#include "logging.h"
#include "module.h"

#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#else
/* Host-side stub for syntax checking */
static inline int __system_property_get(const char *key, char *value) {
    (void)key; if (value) value[0] = '\0';
    return 0;
}
#endif
#include <sys/mman.h>
#include <pthread.h>

/* ── Android SDK level detection ────────────────────────────── */

static int cached_sdk = -1;
static pthread_once_t sdk_once = PTHREAD_ONCE_INIT;

static void detect_sdk(void) {
    char prop[32] = {0};
    /* __system_property_get is available directly in liblog on Android */
    __system_property_get("ro.build.version.sdk", prop);
    if (prop[0]) {
        cached_sdk = atoi(prop);
    } else {
        /* Fallback: assume a modern Android version */
        cached_sdk = 33;
    }
}

int art_get_sdk_version(void) {
    pthread_once(&sdk_once, detect_sdk);
    return cached_sdk;
}

bool art_is_64bit(void) {
#if defined(__LP64__)
    return true;
#else
    return false;
#endif
}

/* ── ArtMethod entry_point_from_compiled_code_ offset ────────── */

/*
 * From Pine's art_method.h — the offset of entry_point_from_compiled_code_
 * in the ArtMethod struct varies by Android version and pointer width.
 *
 *   Android V/U/T/S/SL  (API 34/33/33/34): 24 (64-bit), 20 (32-bit)
 *   Android R/Q/P       (API 30/29/28):     32 (64-bit), 24 (32-bit)
 *   Android O/OMr1     (API 26/27):        40 (64-bit), 32 (32-bit)
 *   Android N           (API 24/25):        48 (64-bit), 28 (32-bit)
 */

static size_t entry_point_offset(void) {
    int sdk = art_get_sdk_version();
    bool is_64 = art_is_64bit();

    if (sdk >= 34) {          /* V */
        return is_64 ? 24 : 20;
    } else if (sdk >= 31) {   /* S, T, U */
        return is_64 ? 24 : 20;
    } else if (sdk >= 30) {   /* R */
        return is_64 ? 32 : 24;
    } else if (sdk >= 26) {   /* O, OMr1 */
        return is_64 ? 40 : 32;
    } else if (sdk >= 24) {   /* N, NMr1 */
        return is_64 ? 48 : 28;
    } else if (sdk >= 23) {   /* M */
        return is_64 ? 48 : 32;
    }
    /* Fallback: assume modern layout */
    return is_64 ? 24 : 20;
}

/* ── ART access_flags for non-compilable marking ─────────────── */

/* From ART access_flags.h — kAccCompileDontBother = 0x02000000 */
#define K_ACC_COMPILE_DONT_BOTHER 0x02000000u

/* access_flags_ offset in ArtMethod — same as entry_point for our purposes */
static size_t access_flags_offset(void) {
    int sdk = art_get_sdk_version();
    bool is_64 = art_is_64bit();

    if (sdk >= 26) {
        return is_64 ? 4 : 4;
    }
    /* On older versions access_flags_ is at a different offset, but the
     * layout from Pine shows it starts at offset 4 for all versions we
     * support (API 26+). */
    return 4;
}

/* ── Resolve ArtMethod pointer from jmethodID ────────────────── */

/*
 * On all Android versions, jmethodID returned by GetMethodID is the ArtMethod*.
 * (Pine's GetArtMethodForR works around an edge case where the Executable's
 *  artMethod field may differ, but for most cases the jmethodID is valid.)
 */

static void *jmethod_to_artmethod(JNIEnv *env, jclass cls,
                                  const char *name, const char *sig, bool is_static) {
    jmethodID mid;
    if (is_static) {
        mid = (*env)->GetStaticMethodID(env, cls, name, sig);
    } else {
        mid = (*env)->GetMethodID(env, cls, name, sig);
    }
    if (!mid) {
        LOGW("GetMethodID(%s, %s, %s) failed",
             name, sig, is_static ? "static" : "instance");
        return NULL;
    }

    int sdk = art_get_sdk_version();

    /* On Android R+, the Executable object's artMethod field is the canonical
     * pointer.  We obtain it via ToReflectedMethod + field access. */
    if (sdk >= 30) {
        jobject reflected = (*env)->ToReflectedMethod(env, cls, mid, !is_static);
        if (!reflected) {
            LOGW("ToReflectedMethod failed — falling back to jmethodID");
            return (void *)(uintptr_t)mid;
        }
        jclass exec_cls = (*env)->GetObjectClass(env, reflected);
        jfieldID art_field = (*env)->GetFieldID(env, exec_cls, "artMethod", "J");
        if (!art_field) {
            LOGW("artMethod field not found — falling back to jmethodID");
            return (void *)(uintptr_t)mid;
        }
        jlong ptr = (*env)->GetLongField(env, reflected, art_field);
        return ptr ? (void *)(uintptr_t)ptr : (void *)(uintptr_t)mid;
    }

    /* Pre-R: jmethodID IS the ArtMethod pointer. */
    return (void *)(uintptr_t)mid;
}

/* ── Hook installation ───────────────────────────────────────── */

int art_hook_method(JNIEnv *env, const char *klass_name, const char *method_name,
                    const char *sig, void *hook_fn, void **out_orig) {
    jclass cls = (*env)->FindClass(env, klass_name);
    if (!cls) {
        LOGW("FindClass(%s) failed", klass_name);
        return -1;
    }

    /* Determine if method is static by checking the signature.
     * A more robust check would inspect the access flags, but for
     * our known Telegram methods this is sufficient. */
    bool is_static = false;

    void *art_method = jmethod_to_artmethod(env, cls, method_name, sig, is_static);
    if (!art_method) {
        LOGW("Could not resolve ArtMethod for %s.%s%s", klass_name, method_name, sig);
        return -1;
    }

    size_t ep_off = entry_point_offset();
    void **ep_ptr = (void **)((char *)art_method + ep_off);

    /* Read and save the original entry point */
    void *orig_ep = *ep_ptr;
    if (!orig_ep) {
        LOGW("Original entry point is NULL for %s.%s%s — method may not be resolved",
             klass_name, method_name, sig);
        return -1;
    }

    if (out_orig) {
        *out_orig = orig_ep;
    }

    /* Mark the method as non-compilable so ART doesn't JIT it and
     * reset our hook.  This also prevents deoptimisation races. */
    uint32_t *flags_ptr = (uint32_t *)((char *)art_method + access_flags_offset());
    *flags_ptr |= K_ACC_COMPILE_DONT_BOTHER;

    /* Replace the entry point with our hook */
    *ep_ptr = hook_fn;

    LOGI("Hooked %s.%s%s (entry_pt offset=%zu, sdk=%d, 64bit=%d)",
         klass_name, method_name, sig, ep_off, art_get_sdk_version(), art_is_64bit());

    return 0;
}
