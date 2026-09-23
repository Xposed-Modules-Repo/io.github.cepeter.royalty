/* logging.h stub for host-side syntax checking */
#ifndef LOGGING_H
#define LOGGING_H
#include <stdio.h>
#define LOG_TAG "TelegramChatHider"
#define LOGD(...) fprintf(stderr, "[D] " LOG_TAG ": " __VA_ARGS__); fprintf(stderr, "\n")
#define LOGV(...) fprintf(stderr, "[V] " LOG_TAG ": " __VA_ARGS__); fprintf(stderr, "\n")
#define LOGI(...) fprintf(stderr, "[I] " LOG_TAG ": " __VA_ARGS__); fprintf(stderr, "\n")
#define LOGW(...) fprintf(stderr, "[W] " LOG_TAG ": " __VA_ARGS__); fprintf(stderr, "\n")
#define LOGE(...) fprintf(stderr, "[E] " LOG_TAG ": " __VA_ARGS__); fprintf(stderr, "\n")
#define PLOGE(fmt, args...) LOGE(fmt " failed: " #args, ##args)
#endif
