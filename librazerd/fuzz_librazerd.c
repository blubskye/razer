/*
 * fuzz_librazerd.c — AFL++ persistent-mode fuzzer for librazerd.
 *
 * Exercises two attack surfaces per iteration:
 *   1. razerd_set_profile_name — UTF-8 → UTF-16-BE encoder (pure computation)
 *   2. Full parse chain (get_leds, get_dpi_mappings, …) against live razerd
 *
 * Compile (link against a Release build of librazerd — no ASAN in the .so):
 *   AFL_HARDEN=1 afl-gcc-fast -std=c2x -Wall -O2 -g \
 *     -I. fuzz_librazerd.c \
 *     -L../build_fuzz/librazerd -lrazerd \
 *     -Wl,-rpath,$(realpath ../build_fuzz/librazerd) \
 *     -o fuzz_librazerd
 *
 * Run:
 *   AFL_SKIP_CPUFREQ=1 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 \
 *     LD_LIBRARY_PATH=../build_fuzz/librazerd \
 *     afl-fuzz -i fuzz/in -o fuzz/out -m none -t 5000 \
 *     -- ./fuzz_librazerd
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include "librazerd.h"

#ifdef __AFL_FUZZ_TESTCASE_BUF
__AFL_FUZZ_INIT();
#endif

static void fuzz_one(const uint8_t *data, size_t size)
{
    razerd_t *r = razerd_open();
    if (!r) return;   /* razerd not running — skip gracefully */

    /* Build a null-terminated string from fuzz input */
    char *name = malloc(size + 1);
    if (!name) { razerd_close(r); return; }
    memcpy(name, data, size);
    name[size] = '\0';

    /* Resolve an idstr from the live device list, or fall back to a dummy */
    char **mice = NULL;
    size_t mc = 0;
    const char *idstr = "fuzz:0000:0000:0000:00";
    if (razerd_get_mice(r, &mice, &mc) == 0 && mc > 0)
        idstr = mice[0];

    /* Target 1: UTF-8 → UTF-16-BE encoder */
    (void)razerd_set_profile_name(r, idstr, 0, name);

    /* Target 2: reply parsers — exercise various get paths */
    if (mc > 0) {
        razerd_led_t *leds = NULL; size_t lc = 0;
        (void)razerd_get_leds(r, idstr, 0xFFFFFFFFu, &leds, &lc);
        razerd_free_leds(leds);

        razerd_dpi_mapping_t *dpi = NULL; size_t dc = 0;
        (void)razerd_get_dpi_mappings(r, idstr, &dpi, &dc);
        razerd_free_dpi_mappings(dpi);

        uint32_t *freqs = NULL; size_t fc = 0;
        (void)razerd_get_supported_freqs(r, idstr, &freqs, &fc);
        razerd_free_freqs(freqs);

        razerd_button_t *btns = NULL; size_t bc = 0;
        (void)razerd_get_buttons(r, idstr, &btns, &bc);
        razerd_free_buttons(btns);

        razerd_axis_t *axes = NULL; size_t ac = 0;
        (void)razerd_get_axes(r, idstr, &axes, &ac);
        razerd_free_axes(axes);
    }

    free(name);
    if (mice) razerd_free_mice(mice, mc);
    razerd_close(r);
}

int main(void)
{
#ifdef __AFL_FUZZ_TESTCASE_BUF
    __AFL_INIT();
    uint8_t *buf = __AFL_FUZZ_TESTCASE_BUF;
    while (__AFL_LOOP(10000)) {
        size_t len = __AFL_FUZZ_TESTCASE_LEN;
        fuzz_one(buf, len);
    }
#else
    /* Non-AFL mode: read from stdin for manual testing */
    uint8_t buf[65536];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n > 0)
        fuzz_one(buf, (size_t)n);
#endif
    return 0;
}
