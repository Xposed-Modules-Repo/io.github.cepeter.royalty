/*
 * art_hook.h — Minimal ART method entry-point hooking for Zygisk modules
 *
 * Replaces the ArtMethod's entry_point_from_compiled_code_ with a hook
 * function, so we can intercept Java method calls from native code.
 *
 * Unlike hook_jni_native_methods (which only finds RegisterNatives entries),
 * this resolves methods via JNI's GetMethodID — which works for ALL Java
 * methods, native-registered or not.
 *
 * The hook callback receives the same calling convention as the original
 * compiled method (this/object + args per the quick ABI), and calls back
 * to the saved original entry point via a function pointer.
 *
 * Limitations:
 *   - On Android 12+ ART may re-compile and reset the entry point.
 *     We handle this by also marking the method non-compilable via
 *     SetNonCompilable when needed.
 *   - Only supports arm64-v8a and armeabi-v7a.
 */
#ifndef ART_HOOK_H
#define ART_HOOK_H

#include <jni.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Hook a Java instance or static method by class + name + signature.
 *
 *   env       — valid JNIEnv (must be attached to the target process)
 *   klass     — class name in JNI format, e.g. "org/telegram/messenger/MessagesController"
 *   name      — method name, e.g. "getDialogs"
 * Returns:     0 on success, -1 on failure (class/method not found,
 *              entry-point field unreachable).
 */
int art_hook_method(JNIEnv *env, const char *klass, const char *name,
                    const char *sig, void *hook_fn, void **out_orig);

/*
 * Convenience: get the current SDK version (reads ro.build.version.sdk
 * property without external tools — safe inside a Zygisk native module).
 */
int art_get_sdk_version(void);

/*
 * Returns true if the CPU is 64-bit.
 */
bool art_is_64bit(void);

#ifdef __cplusplus
}
#endif

#endif /* ART_HOOK_H */
