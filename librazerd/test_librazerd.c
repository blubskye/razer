#include <stdio.h>
#include <stdlib.h>
#include "librazerd.h"

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
    ASSERT(r != NULL, "razerd_open returned NULL");
    razerd_close(r);
    PASS();
}

static void test_get_mice(void)
{
    TEST("razerd_get_mice");
    razerd_t *r = razerd_open();
    ASSERT(r != NULL, "razerd_open failed");

    char **mice = NULL;
    size_t count = 0;
    int err = razerd_get_mice(r, &mice, &count);
    ASSERT(err == 0, "razerd_get_mice returned error");
    printf("(%zu mice) ", count);
    razerd_free_mice(mice, count);
    razerd_close(r);
    PASS();
}

int main(void)
{
    printf("librazerd tests (requires running razerd)\n");
    printf("==========================================\n");

    test_open_close();
    test_get_mice();

    printf("\n%d/%d tests passed\n", tests_run - tests_failed, tests_run);
    return tests_failed ? 1 : 0;
}
