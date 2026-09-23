/* module.h stub for host-side syntax checking */
#ifndef MODULE_H
#define MODULE_H

#include <string.h>
#include <stdint.h>
#include <jni.h>
#include "logging.h"

#define REZYGISK_API_VERSION 5

enum rezygisk_options {
  FORCE_DENYLIST_UNMOUNT = 0,
  DLCLOSE_MODULE_LIBRARY = 1
};

struct rezygisk_abi {
  long api_version;
  void *impl;

  void (*pre_app_specialize)(void *, void *);
  void (*post_app_specialize)(void *, const void *);
  void (*pre_server_specialize)(void *, void *);
  void (*post_server_specialize)(void *, const void *);
};

struct rezygisk_api {
  void *impl;
  bool (*register_module)(struct rezygisk_api *, struct rezygisk_abi const *);

  void (*hook_jni_native_methods)(JNIEnv *, const char *, JNINativeMethod *, int);
  union {
    void (*plt_hook_register)(const char *, const char *, void *, void **);
    void (*plt_hook_register_v4)(unsigned long, unsigned long, const char *, void *, void **);
  };
  union {
    void (*plt_hook_exclude)(const char *, const char *);
    void (*exempt_fd)(int);
  };

  bool (*plt_hook_commit)(void);
  int (*connect_companion)(void *);
  void (*set_option)(void *, enum rezygisk_options opt);
  int (*get_module_dir)(void *);
  uint32_t (*get_flags)(void *);
};

struct rezygisk_module {
  struct rezygisk_abi abi;
  struct rezygisk_api api;
  void *lib;
  void (*zygisk_module_entry)(void *, void *);
  bool unload;
};

static inline void rz_module_call_on_load(struct rezygisk_module *m, void *env) {
  m->zygisk_module_entry((void *)&m->api, env);
}

static inline void rz_module_call_pre_app_specialize(struct rezygisk_module *m, void *args) {
  if (m->abi.pre_app_specialize) m->abi.pre_app_specialize(m->abi.impl, args);
}

static inline void rz_module_call_post_app_specialize(struct rezygisk_module *m, const void *args) {
  if (m->abi.post_app_specialize) m->abi.post_app_specialize(m->abi.impl, args);
}

static inline void rz_module_call_pre_server_specialize(struct rezygisk_module *m, void *args) {
  if (m->abi.pre_server_specialize) m->abi.pre_server_specialize(m->abi.impl, args);
}

static inline void rz_module_call_post_server_specialize(struct rezygisk_module *m, const void *args) {
  if (m->abi.post_server_specialize) m->abi.post_server_specialize(m->abi.impl, args);
}

#endif /* MODULE_H */
