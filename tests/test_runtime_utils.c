#include "runtime_utils.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void test_target_process(void) {
    assert(tch_is_target_process("org.telegram.messenger"));
    assert(!tch_is_target_process("org.telegram.messenger:push"));
    assert(!tch_is_target_process("org.telegram.messenger.webdebug"));
    assert(!tch_is_target_process("xorg.telegram.messenger"));
    assert(!tch_is_target_process(""));
    assert(!tch_is_target_process(NULL));
}

static void test_dialog_id_parser(void) {
    int64_t value = 0;
    assert(tch_parse_dialog_id("1", &value) && value == 1);
    assert(tch_parse_dialog_id("-1001234567890", &value) && value == INT64_C(-1001234567890));
    assert(tch_parse_dialog_id("9223372036854775807", &value) && value == INT64_MAX);
    assert(tch_parse_dialog_id("-9223372036854775808", &value) && value == INT64_MIN);

    assert(!tch_parse_dialog_id("", &value));
    assert(!tch_parse_dialog_id("0", &value));
    assert(!tch_parse_dialog_id(" 1", &value));
    assert(!tch_parse_dialog_id("1 ", &value));
    assert(!tch_parse_dialog_id("1x", &value));
    assert(!tch_parse_dialog_id("9223372036854775808", &value));
    assert(!tch_parse_dialog_id("-9223372036854775809", &value));
    assert(!tch_parse_dialog_id("1", NULL));
}

static void assert_close(float actual, float expected) {
    assert(fabsf(actual - expected) < 0.01f);
}

static void test_header_threshold(void) {
    assert_close(tch_header_threshold_px(2400, 3.0f), 288.0f);
    assert_close(tch_header_threshold_px(1080, 3.0f), 194.4f);
    assert_close(tch_header_threshold_px(1600, 2.0f), 192.0f);
    assert_close(tch_header_threshold_px(0, 0.0f), 220.0f);
}

static void test_send_all(void) {
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

    const char payload[] = "{\"dialogs\":[{\"id\":\"-1001\"}]}";
    assert(tch_send_all(sockets[0], payload, sizeof(payload) - 1) == 0);

    char received[sizeof(payload)] = {0};
    ssize_t count = read(sockets[1], received, sizeof(received));
    assert(count == (ssize_t)(sizeof(payload) - 1));
    assert(memcmp(received, payload, sizeof(payload) - 1) == 0);

    close(sockets[0]);
    close(sockets[1]);
}

int main(void) {
    test_target_process();
    test_dialog_id_parser();
    test_header_threshold();
    test_send_all();
    puts("runtime_utils: 4/4 passed");
    return 0;
}
