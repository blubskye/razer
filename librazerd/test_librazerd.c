#include <stdio.h>
#include <stdlib.h>
#include "librazerd.h"
#ifdef LIBRAZERD_NOTIFICATIONS
#include <poll.h>
#endif

static int tests_run = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { \
        printf("  %-40s ", name); \
        tests_run++; \
    } while(0)

#define PASS() do { printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)
#define ASSERT(cond, msg) \
    do { if (!(cond)) { FAIL(msg); return; } } while(0)

static void test_open_close(void)
{
    TEST("razerd_open/close");
    razerd_t *r = razerd_open();
    if (!r) { FAIL("razerd_open failed (razerd running?)"); return; }
    razerd_close(r);
    PASS();
}

static void test_get_mice(void)
{
    TEST("razerd_get_mice");
    razerd_t *r = razerd_open();
    if (!r) { FAIL("razerd_open failed (razerd running?)"); return; }

    char **mice = NULL;
    size_t count = 0;
    int err = razerd_get_mice(r, &mice, &count);
    if (err != 0) { razerd_close(r); FAIL("razerd_get_mice returned error"); return; }
    printf("(%zu mice) ", count);
    razerd_free_mice(mice, count);
    razerd_close(r);
    PASS();
}

static void test_mouse_info(void)
{
    TEST("razerd_get_mouse_info");
    razerd_t *r = razerd_open();
    if (!r) { FAIL("razerd_open failed (razerd running?)"); return; }

    char **mice = NULL; size_t count = 0;
    int err = razerd_get_mice(r, &mice, &count);
    if (err != 0) { razerd_close(r); FAIL("get_mice failed"); return; }

    if (count == 0) {
        printf("(no mice, skipping) ");
        razerd_free_mice(mice, count);
        razerd_close(r);
        PASS();
        return;
    }

    uint32_t flags = 0;
    err = razerd_get_mouse_info(r, mice[0], &flags);
    if (err != 0) { razerd_free_mice(mice, count); razerd_close(r); FAIL("get_mouse_info failed"); return; }
    printf("(flags=0x%x) ", flags);

    razerd_free_mice(mice, count);
    razerd_close(r);
    PASS();
}

static void test_get_leds(void)
{
    TEST("razerd_get_leds");
    razerd_t *r = razerd_open();
    if (!r) { FAIL("razerd_open failed"); return; }

    char **mice = NULL; size_t mc = 0;
    int err = razerd_get_mice(r, &mice, &mc);
    if (err != 0) { razerd_close(r); FAIL("get_mice failed"); return; }
    if (mc == 0) {
        printf("(no mice, skipping) ");
        razerd_free_mice(mice, mc);
        razerd_close(r);
        PASS();
        return;
    }

    razerd_led_t *leds = NULL;
    size_t lc = 0;
    err = razerd_get_leds(r, mice[0], 0xFFFFFFFFu, &leds, &lc);
    if (err != 0) { razerd_free_mice(mice, mc); razerd_close(r); FAIL("get_leds failed"); return; }
    printf("(%zu leds) ", lc);
    razerd_free_leds(leds);
    razerd_free_mice(mice, mc);
    razerd_close(r);
    PASS();
}

#ifdef LIBRAZERD_NOTIFICATIONS
static void test_notifications(void)
{
    TEST("razerd notifications (poll fd)");
    razerd_t *r = razerd_open();
    if (!r) { FAIL("razerd_open failed (razerd running?)"); return; }

    int fd = razerd_get_notify_fd(r);
    ASSERT(fd >= 0, "notify fd invalid");

    /* Poll with 0 timeout — should have no events right after connect */
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    int ret = poll(&pfd, 1, 0);
    ASSERT(ret == 0, "unexpected event on notify fd");

    printf("(fd=%d, no spurious events) ", fd);
    razerd_close(r);
    PASS();
}
#endif

int main(void)
{
    printf("librazerd tests (requires running razerd)\n");
    printf("==========================================\n");

    test_open_close();
    test_get_mice();
    test_mouse_info();
    test_get_leds();
#ifdef LIBRAZERD_NOTIFICATIONS
    test_notifications();
#endif

    printf("\n%d/%d tests passed\n", tests_run - tests_failed, tests_run);
    return tests_failed ? 1 : 0;
}
