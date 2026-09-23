/* jni.h stub for host-side syntax checking */
#ifndef JNIDL_STUB_H
#define JNIDL_STUB_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

typedef intptr_t jint;
typedef int32_t  jsize;
typedef int64_t  jlong;
typedef uint8_t  jboolean;
typedef uint8_t  jbyte;
typedef int16_t  jshort;
typedef uint16_t jchar;
typedef float    jfloat;
typedef double   jdouble;
typedef void*    jobject;
typedef jobject  jclass;
typedef jobject  jthrowable;
typedef jobject  jstring;
typedef jobject  jbyteArray;
typedef jobject  jintArray;
typedef jobject  jcharArray;
typedef jobject  jbooleanArray;
typedef jobject  jobjectArray;
typedef jobject  jweak;
typedef jobject  jmethodID;
typedef jobject  jfieldID;
typedef jobject  jarray;

typedef int      boolean;

typedef struct {
    const char *name;
    const char *sig;
    void *fnPtr;
} JNINativeMethod;

typedef struct JNINativeInterface *JNIEnv;

#define JNICALL
#define JNIEXPORT

struct JNINativeInterface {
    jclass      (*DefineClass)(JNIEnv*, const char*, jobject, const jbyte*, jsize);
    void        (*DeleteLocalRef)(JNIEnv*, jobject);
    void        (*DeleteGlobalRef)(JNIEnv*, jweak);
    void        (*DeleteWeakGlobalRef)(JNIEnv*, jweak);
    jclass      (*GetObjectClass)(JNIEnv*, jobject);
    jmethodID   (*GetMethodID)(JNIEnv*, jclass, const char*, const char*);
    jfieldID    (*GetFieldID)(JNIEnv*, jclass, const char*, const char*);
    jobject     (*GetObjectField)(JNIEnv*, jobject, jfieldID);
    jlong       (*GetLongField)(JNIEnv*, jobject, jfieldID);
    void        (*SetLongField)(JNIEnv*, jobject, jfieldID, jlong);
    jobject     (*CallObjectMethod)(JNIEnv*, jobject, jmethodID, ...);
    void        (*CallVoidMethod)(JNIEnv*, jobject, jmethodID, ...);
    jint        (*CallIntMethod)(JNIEnv*, jobject, jmethodID, ...);
    jboolean    (*CallBooleanMethod)(JNIEnv*, jobject, jmethodID, ...);
    jfloat      (*CallFloatMethod)(JNIEnv*, jobject, jmethodID, ...);
    jlong       (*CallLongMethod)(JNIEnv*, jobject, jmethodID, ...);
    jobject     (*NewObject)(JNIEnv*, jclass, jmethodID, ...);
    jclass      (*FindClass)(JNIEnv*, const char*);
    jsize       (*GetArrayLength)(JNIEnv*, jarray);
    jstring     (*NewStringUTF)(JNIEnv*, const char*);
    const char* (*GetStringUTFChars)(JNIEnv*, jstring, jboolean*);
    void        (*ReleaseStringUTFChars)(JNIEnv*, jstring, const char*);
    jthrowable  (*ExceptionOccurred)(JNIEnv*);
    void        (*ExceptionClear)(JNIEnv*);
    jint        (*RegisterNatives)(JNIEnv*, jclass, const JNINativeMethod*, jint);
    jboolean    (*IsSameObject)(JNIEnv*, jobject, jobject);
};

union jvalue {
    jboolean    z;
    jbyte       b;
    jchar       c;
    jshort      s;
    jint        i;
    jlong       j;
    jfloat      f;
    jdouble     d;
    jobject     l;
};

typedef union jvalue jvalue;
#define JNI_FALSE 0
#define JNI_TRUE  1
#define JNI_VERSION_1_6 0x00010006

#endif /* JNIDL_STUB_H */
