#include "runtime_utils.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define TARGET_PROCESS "org.telegram.messenger"
#define FALLBACK_HEADER_PX 220.0f
#define HEADER_HEIGHT_DP 96.0f
#define HEADER_SCREEN_RATIO 0.18f

bool tch_is_target_process(const char *process_name) {
    return process_name != NULL && strcmp(process_name, TARGET_PROCESS) == 0;
}

bool tch_parse_dialog_id(const char *text, int64_t *value) {
    if (text == NULL || value == NULL || text[0] == '\0') {
        return false;
    }

    if (text[0] == '-') {
        if (text[1] < '0' || text[1] > '9') {
            return false;
        }
    } else if (text[0] < '0' || text[0] > '9') {
        return false;
    }

    errno = 0;
    char *end = NULL;
    long long parsed = strtoll(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || parsed == 0) {
        return false;
    }

    *value = (int64_t)parsed;
    return true;
}

float tch_header_threshold_px(int32_t screen_height_px, float density) {
    if (screen_height_px <= 0 || density <= 0.0f) {
        return FALLBACK_HEADER_PX;
    }

    float dp_threshold = HEADER_HEIGHT_DP * density;
    float ratio_threshold = (float)screen_height_px * HEADER_SCREEN_RATIO;
    return dp_threshold < ratio_threshold ? dp_threshold : ratio_threshold;
}

int tch_send_all(int fd, const void *buffer, size_t length) {
    const char *cursor = buffer;
    size_t remaining = length;

    while (remaining > 0) {
        ssize_t sent = send(fd, cursor, remaining, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (sent == 0) {
            errno = EPIPE;
            return -1;
        }
        cursor += (size_t)sent;
        remaining -= (size_t)sent;
    }

    return 0;
}
