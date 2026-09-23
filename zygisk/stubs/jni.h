/* jni.h stub for host-side syntax checking only.
 * The real NDK provides jni.h during compilation.
 * This stub provides just enough types for syntax verification. */
#ifndef JNI_H_STUB
#define JNI_H_STUB

#include <stdint.h>
#include <stddef.h>

typedef void *jobject;
typedef void *jclass;
typedef void *jthrowable;
typedef void *jstring;
typedef int32_t jint;
typedef int32_t jsize;
typedef uint8_t jboolean;
typedef int64_t jlong;
typedef float jfloat;
typedef double jdouble;
typedef int8_t jbyte;
typedef uint16_t jchar;
typedef int16_t jshort;

typedef void *jobjectArray;
typedef void *jintArray;
typedef void *jbyteArray;
typedef void *jcharArray;
typedef void *jshortArray;
typedef void *jlongArray;
typedef void *jfloatArray;
typedef void *jdoubleArray;
typedef void *jbooleanArray;

typedef void *jfieldID;
typedef void *jmethodID;

#define JNI_FALSE 0
#define JNI_TRUE  1
#define JNICALL

typedef struct {
    jint i;
    jboolean z;
    jbyte b;
    jchar c;
    jshort s;
    jlong j;
    jfloat f;
    jdouble d;
    jobject l;
} jvalue;

struct JNINativeMethod {
    const char *name;
    const char *signature;
    void *fnPtr;
};
typedef struct JNINativeMethod JNINativeMethod;

/* Forward-declare JavaVM (it's a pointer to this interface table) */
struct JNIInvokeInterface;
typedef const struct JNIInvokeInterface *JavaVM;

struct JNINativeInterface {
    void *reserved0[4];
    jint (*GetVersion)(void *env);
    jclass (*DefineClass)(void *env, const char *name, jobject loader, const void *buf, jsize len);
    jclass (*FindClass)(void *env, const char *name);
    jthrowable (*ExceptionOccurred)(void *env);
    jboolean (*ExceptionCheck)(void *env);
    void (*ExceptionClear)(void *env);
    void (*ExceptionDescribe)(void *env);
    void (*ExceptionThrow)(void *env, jclass clazz);
    jint (*RegisterNatives)(void *env, jclass clazz, const struct JNINativeMethod *methods, jint nMethods);
    jint (*UnregisterNatives)(void *env, jclass clazz);
    jboolean (*IsSameObject)(void *env, jobject ref1, jobject ref2);
    jobject (*GetObjectClass)(void *env, jobject obj);
    jfieldID (*GetFieldID)(void *env, jclass clazz, const char *name, const char *sig);
    jint (*GetIntField)(void *env, jobject obj, jfieldID fieldID);
    jlong (*GetLongField)(void *env, jobject obj, jfieldID fieldID);
    jobject (*GetObjectField)(void *env, jobject obj, jfieldID fieldID);
    jfloat (*GetFloatField)(void *env, jobject obj, jfieldID fieldID);
    void (*SetIntField)(void *env, jobject obj, jfieldID fieldID, jint val);
    jfieldID (*GetStaticFieldID)(void *env, jclass clazz, const char *name, const char *sig);
    jint (*GetStaticIntField)(void *env, jclass clazz, jfieldID fieldID);
    jmethodID (*GetStaticMethodID)(void *env, jclass clazz, const char *name, const char *sig);
    jint (*CallStaticIntMethod)(void *env, jclass clazz, jmethodID methodID, ...);
    jint (*GetJavaVM)(void *env, JavaVM **vm);
    jmethodID (*GetMethodID)(void *env, jclass clazz, const char *name, const char *sig);
    jobject (*NewObject)(void *env, jclass clazz, jmethodID methodID, ...);
    jobject (*CallObjectMethod)(void *env, jobject obj, jmethodID methodID, ...);
    jint (*CallIntMethod)(void *env, jobject obj, jmethodID methodID, ...);
    jlong (*CallLongMethod)(void *env, jobject obj, jmethodID methodID, ...);
    jboolean (*CallBooleanMethod)(void *env, jobject obj, jmethodID methodID, ...);
    jfloat (*CallFloatMethod)(void *env, jobject obj, jmethodID methodID, ...);
    void (*CallVoidMethod)(void *env, jobject obj, jmethodID methodID, ...);
    jobject (*NewObjectV)(void *env, jclass clazz, jmethodID methodID, const void *args);
    void (*DeleteLocalRef)(void *env, jobject localRef);
    jsize (*GetArrayLength)(void *env, jobjectArray array);
    jobject (*GetObjectArrayElement)(void *env, jobjectArray array, jsize index);
    void (*GetByteArrayRegion)(void *env, jbyteArray array, jsize start, jsize len, jbyte *buf);
    char *(*GetStringUTFChars)(void *env, jstring str, jboolean *isCopy);
    void (*ReleaseStringUTFChars)(void *env, jstring str, const char *utf);
    jstring (*NewStringUTF)(void *env, const char *utf);
    jbyteArray (*NewByteArray)(void *env, jsize length);
    void (*SetByteArrayRegion)(void *env, jbyteArray array, jsize start, jsize len, const jbyte *buf);
    jobject (*ToReflectedMethod)(void *env, jclass clazz, jmethodID methodID, jboolean isStatic);
    void (*DeleteGlobalRef)(void *env, jobject globalRef);
    jobject (*NewGlobalRef)(void *env, jobject localRef);
    jint (*EnsureLocalCapacity)(void *env, jsize capacity);
    void *reserved[16];
};

typedef const struct JNINativeInterface *JNIEnv;

/* JavaVM function table (matches JNIInvokeInterface) */
struct JNIInvokeInterface {
    jint (*AttachCurrentThread)(JavaVM *vm, JNIEnv **penv, void *args);
    jint (*DetachCurrentThread)(JavaVM *vm);
};

/* JNI_GetCreatedJavaVMs */
jint JNI_GetCreatedJavaVMs(JavaVM *vm, jsize capacity, jsize *count);

#endif /* JNI_H_STUB */
