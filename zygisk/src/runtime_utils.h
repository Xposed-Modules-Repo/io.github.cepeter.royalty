#ifndef TCH_RUNTIME_UTILS_H
#define TCH_RUNTIME_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool tch_is_target_process(const char *process_name);
bool tch_parse_dialog_id(const char *text, int64_t *value);
float tch_header_threshold_px(int32_t screen_height_px, float density);
int tch_send_all(int fd, const void *buffer, size_t length);

#endif
