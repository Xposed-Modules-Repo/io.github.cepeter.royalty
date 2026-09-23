/* logging.h stub for host-side syntax checking */
#ifndef LOGGING_H
#define LOGGING_H
#include <stdio.h>
#define LOG_TAG "TelegramChatHider"
#define LOGD(...) do { fprintf(stderr, "[D] " LOG_TAG ": " __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#define LOGV(...) do { fprintf(stderr, "[V] " LOG_TAG ": " __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#define LOGI(...) do { fprintf(stderr, "[I] " LOG_TAG ": " __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#define LOGW(...) do { fprintf(stderr, "[W] " LOG_TAG ": " __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#define LOGE(...) do { fprintf(stderr, "[E] " LOG_TAG ": " __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#define PLOGE(fmt, args...) do { fprintf(stderr, "[E] " LOG_TAG ": " fmt " failed: " #args "\n", ##args); } while(0)
#endif
