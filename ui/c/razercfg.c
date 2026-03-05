/*
 * razercfg.c — C CLI for Razer device configuration via librazerd.
 *
 * Copyright (C) 2026 Contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Usage: razercfg [OPTIONS] [-d DEV DEVOPS] [-d DEV DEVOPS]...
 *
 * Mirrors the Python razercfg flag set. Device ops apply to the device
 * selected with -d (or the first mouse found if -d is omitted).
 *
 * Profile prefix: most per-device ops accept an optional "PROF:" prefix
 * (e.g. "1:500" or "0:logo:on"). PROF=0 means the current/first profile;
 * omitting the prefix uses the global setting (RAZERD_PROFILE_INVALID).
 */

#include "librazerd.h"
#include "crash.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define VERSION "0.44-c"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static razerd_t *r;

static void die(const char *msg, int err)
{
	if (err)
		fprintf(stderr, "razercfg: %s: %s\n", msg, strerror(-err));
	else
		fprintf(stderr, "razercfg: %s\n", msg);
	exit(1);
}

/* Parse optional "PROF:" prefix from a string like "1:logo:on".
 * Returns pointer past the prefix. Sets *profile_id to the parsed
 * profile number, or RAZERD_PROFILE_INVALID if no prefix. */
static const char *parse_profile_prefix(const char *s, uint32_t *profile_id)
{
	*profile_id = RAZERD_PROFILE_INVALID;
	/* A leading digit followed by ':' that is not part of "123x456" */
	if (isdigit((unsigned char)s[0])) {
		char *end;
		unsigned long v = strtoul(s, &end, 10);
		if (*end == ':') {
			*profile_id = (uint32_t)v;
			return end + 1;
		}
	}
	return s;
}

/* Return the first mouse idstr, or die. Caller must razerd_free_mice(). */
static const char *first_mouse(char ***mice_out, size_t *mc_out)
{
	int err = razerd_get_mice(r, mice_out, mc_out);
	if (err) die("get_mice failed", err);
	if (*mc_out == 0) die("no Razer mouse found", 0);
	return (*mice_out)[0];
}

/* ------------------------------------------------------------------ */
/* Device operations                                                   */
/* ------------------------------------------------------------------ */

static void op_scan(void)
{
	if (razerd_rescan(r) != 0)
		fprintf(stderr, "razercfg: rescan failed\n");
	char **mice = NULL; size_t mc = 0;
	int err = razerd_get_mice(r, &mice, &mc);
	if (err) die("get_mice", err);
	for (size_t i = 0; i < mc; i++)
		printf("%s\n", mice[i]);
	razerd_free_mice(mice, mc);
}

static void op_reconfigure(void)
{
	int err = razerd_reconfigure(r);
	if (err) die("reconfigure failed", err);
	printf("Reconfigured.\n");
}

static void op_fwver(const char *idstr)
{
	uint8_t major = 0, minor = 0;
	int err = razerd_get_fw_version(r, idstr, &major, &minor);
	if (err) die("get_fw_version", err);
	printf("Firmware version: %u.%u\n", major, minor);
}

static void op_getprofile(const char *idstr)
{
	uint32_t id = 0;
	int err = razerd_get_active_profile(r, idstr, &id);
	if (err) die("get_active_profile", err);
	printf("Active profile: %u\n", id);
}

static void op_setprofile(const char *idstr, const char *arg)
{
	uint32_t id = (uint32_t)atoi(arg);
	int err = razerd_set_active_profile(r, idstr, id);
	if (err) die("set_active_profile", err);
	printf("Active profile set to %u\n", id);
}

static void op_getres(const char *idstr)
{
	razerd_dpi_mapping_t *maps = NULL; size_t mc = 0;
	int err = razerd_get_dpi_mappings(r, idstr, &maps, &mc);
	if (err) die("get_dpi_mappings", err);

	uint32_t active = 0;
	(void)razerd_get_dpi_mapping(r, idstr, 0, 0xFFFFFFFFu, &active);

	for (size_t i = 0; i < mc; i++) {
		printf("mapping %u: %u", maps[i].id, maps[i].res[0]);
		if (maps[i].dim_mask & 2u)
			printf("x%u", maps[i].res[1]);
		printf(" DPI%s\n", maps[i].id == active ? " (active)" : "");
	}
	razerd_free_dpi_mappings(maps);
}

/* -r [PROF:]RES[xRES2]  — change the active mapping's resolution */
static void op_setres(const char *idstr, const char *arg)
{
	uint32_t profile_id;
	const char *s = parse_profile_prefix(arg, &profile_id);
	if (profile_id == RAZERD_PROFILE_INVALID) profile_id = 0;

	/* Parse RES or RESxRES2 */
	char *x;
	uint32_t res_x = (uint32_t)strtoul(s, &x, 10);
	uint32_t res_y = res_x;
	if (*x == 'x' || *x == 'X')
		res_y = (uint32_t)strtoul(x + 1, NULL, 10);

	/* Get the active mapping id */
	uint32_t mapping_id = 0;
	int err = razerd_get_dpi_mapping(r, idstr, profile_id, 0xFFFFFFFFu, &mapping_id);
	if (err) die("get_dpi_mapping", err);

	/* Change X dimension */
	err = razerd_change_dpi_mapping(r, idstr, mapping_id, 0, res_x);
	if (err) die("change_dpi_mapping (X)", err);

	/* Change Y dimension if device supports independent axes (best effort) */
	err = razerd_change_dpi_mapping(r, idstr, mapping_id, 1, res_y);
	(void)err;

	printf("Resolution set to %ux%u DPI (mapping %u)\n", res_x, res_y, mapping_id);
}

static void op_getfreq(const char *idstr)
{
	uint32_t *freqs = NULL; size_t fc = 0;
	int err = razerd_get_supported_freqs(r, idstr, &freqs, &fc);
	if (err) die("get_supported_freqs", err);

	uint32_t cur = 0;
	(void)razerd_get_freq(r, idstr, 0, &cur);

	for (size_t i = 0; i < fc; i++)
		printf("%u Hz%s\n", freqs[i], freqs[i] == cur ? " (active)" : "");
	razerd_free_freqs(freqs);
}

/* -f [PROF:]FREQ */
static void op_setfreq(const char *idstr, const char *arg)
{
	uint32_t profile_id;
	const char *s = parse_profile_prefix(arg, &profile_id);
	if (profile_id == RAZERD_PROFILE_INVALID) profile_id = 0;
	uint32_t hz = (uint32_t)atoi(s);
	int err = razerd_set_freq(r, idstr, profile_id, hz);
	if (err) die("set_freq", err);
	printf("Frequency set to %u Hz\n", hz);
}

static void op_listleds(const char *idstr)
{
	razerd_led_t *leds = NULL; size_t lc = 0;
	int err = razerd_get_leds(r, idstr, RAZERD_PROFILE_INVALID, &leds, &lc);
	if (err) die("get_leds", err);
	for (size_t i = 0; i < lc; i++)
		printf("%s (%s)\n", leds[i].name, leds[i].state ? "on" : "off");
	razerd_free_leds(leds);
}

/* -l [PROF:]LED:(on|off)  — toggle LED state */
static void op_setled(const char *idstr, const char *arg)
{
	uint32_t profile_id;
	const char *s = parse_profile_prefix(arg, &profile_id);

	/* Parse LED:state */
	const char *colon = strrchr(s, ':');
	if (!colon) die("--setled format: [PROF:]LED:(on|off)", 0);

	char ledname[64];
	size_t namelen = (size_t)(colon - s);
	if (namelen >= sizeof(ledname)) die("LED name too long", 0);
	memcpy(ledname, s, namelen);
	ledname[namelen] = '\0';

	bool on = (strcmp(colon + 1, "on") == 0);
	bool all = (strcmp(ledname, "all") == 0);

	razerd_led_t *leds = NULL; size_t lc = 0;
	int err = razerd_get_leds(r, idstr, profile_id, &leds, &lc);
	if (err) die("get_leds", err);

	bool found = false;
	for (size_t i = 0; i < lc; i++) {
		if (all || strcmp(leds[i].name, ledname) == 0) {
			leds[i].state = on ? 1u : 0u;
			(void)razerd_set_led(r, idstr, profile_id, &leds[i]);
			printf("LED '%s' set to %s\n", leds[i].name, on ? "on" : "off");
			found = true;
			if (!all) break;
		}
	}
	razerd_free_leds(leds);
	if (!found) die("LED not found", 0);
}

/* -c [PROF:]LED:rrggbb  — set LED color */
static void op_setledcolor(const char *idstr, const char *arg)
{
	uint32_t profile_id;
	const char *s = parse_profile_prefix(arg, &profile_id);

	const char *colon = strrchr(s, ':');
	if (!colon) die("--setledcolor format: [PROF:]LED:rrggbb", 0);

	char ledname[64];
	size_t namelen = (size_t)(colon - s);
	if (namelen >= sizeof(ledname)) die("LED name too long", 0);
	memcpy(ledname, s, namelen);
	ledname[namelen] = '\0';

	unsigned int rgb = 0;
	if (sscanf(colon + 1, "%06x", &rgb) != 1)
		die("color must be rrggbb hex (e.g. ff0000 for red)", 0);

	razerd_led_t *leds = NULL; size_t lc = 0;
	int err = razerd_get_leds(r, idstr, profile_id, &leds, &lc);
	if (err) die("get_leds", err);

	bool found = false;
	for (size_t i = 0; i < lc; i++) {
		if (strcmp(leds[i].name, ledname) == 0) {
			leds[i].r = (uint8_t)((rgb >> 16) & 0xFF);
			leds[i].g = (uint8_t)((rgb >>  8) & 0xFF);
			leds[i].b = (uint8_t)( rgb        & 0xFF);
			(void)razerd_set_led(r, idstr, profile_id, &leds[i]);
			printf("LED '%s' color set to #%06x\n", ledname, rgb);
			found = true;
			break;
		}
	}
	razerd_free_leds(leds);
	if (!found) die("LED not found", 0);
}

/* -m [PROF:]LED:MODE  — set LED mode */
static void op_setledmode(const char *idstr, const char *arg)
{
	uint32_t profile_id;
	const char *s = parse_profile_prefix(arg, &profile_id);

	const char *colon = strrchr(s, ':');
	if (!colon) die("--setledmode format: [PROF:]LED:MODE", 0);

	char ledname[64];
	size_t namelen = (size_t)(colon - s);
	if (namelen >= sizeof(ledname)) die("LED name too long", 0);
	memcpy(ledname, s, namelen);
	ledname[namelen] = '\0';

	const char *modestr = colon + 1;
	uint32_t mode;
	if      (strcmp(modestr, "static")   == 0) mode = RAZERD_LED_MODE_STATIC;
	else if (strcmp(modestr, "spectrum") == 0) mode = RAZERD_LED_MODE_SPECTRUM;
	else if (strcmp(modestr, "breathing")== 0) mode = RAZERD_LED_MODE_BREATHING;
	else if (strcmp(modestr, "wave")     == 0) mode = RAZERD_LED_MODE_WAVE;
	else if (strcmp(modestr, "reaction") == 0) mode = RAZERD_LED_MODE_REACTION;
	else die("unknown mode (static|spectrum|breathing|wave|reaction)", 0);

	razerd_led_t *leds = NULL; size_t lc = 0;
	int err = razerd_get_leds(r, idstr, profile_id, &leds, &lc);
	if (err) die("get_leds", err);

	bool found = false;
	for (size_t i = 0; i < lc; i++) {
		if (strcmp(leds[i].name, ledname) == 0) {
			leds[i].mode = mode;
			(void)razerd_set_led(r, idstr, profile_id, &leds[i]);
			printf("LED '%s' mode set to %s\n", ledname, modestr);
			found = true;
			break;
		}
	}
	razerd_free_leds(leds);
	if (!found) die("LED not found", 0);
}

/* -X FILE  — flash firmware (requires privileged socket) */
static void op_flashfw(const char *idstr, const char *path)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) die("cannot open firmware file", -errno);

	struct stat st;
	if (fstat(fd, &st) < 0) { close(fd); die("fstat firmware", -errno); }

	void *image = malloc((size_t)st.st_size);
	if (!image) { close(fd); die("out of memory", 0); }

	ssize_t n = read(fd, image, (size_t)st.st_size);
	close(fd);
	if (n != st.st_size) { free(image); die("short read on firmware file", 0); }

	printf("Flashing %zu bytes to %s ...\n", (size_t)st.st_size, idstr);
	int err = razerd_flash_firmware(r, idstr, image, (size_t)st.st_size);
	free(image);
	if (err) die("flash_firmware", err);
	printf("Firmware flashed successfully.\n");
}

/* ------------------------------------------------------------------ */
/* Usage / main                                                        */
/* ------------------------------------------------------------------ */

static void usage(void)
{
	printf("razercfg (C) version %s\n\n", VERSION);
	printf("Usage: razercfg [OPTIONS] [-d DEV DEVOPS] [-d DEV DEVOPS]...\n\n");
	printf("-h|--help            Print this help text\n");
	printf("-v|--version         Print the program version\n");
	printf("-B|--background      Fork into the background\n");
	printf("-s|--scan            Scan for devices and print bus IDs\n");
	printf("-K|--reconfigure     Force-reconfigure all detected devices\n");
	printf("\n");
	printf("-d|--device DEV      Select device by bus ID (or \"mouse\" for first mouse)\n");
	printf("-S|--sleep SECS      Sleep SECS seconds\n");
	printf("\n");
	printf("Device operations (apply to the device selected with -d):\n");
	printf("-V|--fwver                          Print firmware version\n");
	printf("-p|--profile PROF                   Set active profile\n");
	printf("-P|--getprofile                     Print active profile\n");
	printf("-r|--res [PROF:]RES[xRES]           Set scan resolution (DPI)\n");
	printf("-R|--getres                         Print DPI mappings\n");
	printf("-f|--freq [PROF:]FREQ               Set scan frequency (Hz)\n");
	printf("-F|--getfreq                        Print supported frequencies\n");
	printf("-L|--leds                           List LED identifiers\n");
	printf("-l|--setled [PROF:]LED:(on|off)     Toggle LED (use \"all\" for all LEDs)\n");
	printf("-c|--setledcolor [PROF:]LED:rrggbb  Set LED color\n");
	printf("-m|--setledmode [PROF:]LED:MODE     Set LED mode\n");
	printf("                                    (static|spectrum|breathing|wave|reaction)\n");
	printf("-X|--flashfw FILE                   Flash firmware image\n");
}

int main(int argc, char **argv)
{
	install_crash_handler(argv[0]);

	if (argc < 2) { usage(); return 1; }

	r = razerd_open();
	if (!r) die("razerd is not running", 0);

	/* Current device idstr — NULL means "use first mouse" */
	char **cur_mice = NULL;
	size_t cur_mc   = 0;
	const char *idstr = NULL;

	static const struct option long_opts[] = {
		{ "help",        no_argument,       NULL, 'h' },
		{ "version",     no_argument,       NULL, 'v' },
		{ "background",  no_argument,       NULL, 'B' },
		{ "scan",        no_argument,       NULL, 's' },
		{ "reconfigure", no_argument,       NULL, 'K' },
		{ "device",      required_argument, NULL, 'd' },
		{ "profile",     required_argument, NULL, 'p' },
		{ "getprofile",  no_argument,       NULL, 'P' },
		{ "res",         required_argument, NULL, 'r' },
		{ "getres",      no_argument,       NULL, 'R' },
		{ "freq",        required_argument, NULL, 'f' },
		{ "getfreq",     no_argument,       NULL, 'F' },
		{ "leds",        no_argument,       NULL, 'L' },
		{ "setled",      required_argument, NULL, 'l' },
		{ "setledcolor", required_argument, NULL, 'c' },
		{ "setledmode",  required_argument, NULL, 'm' },
		{ "fwver",       no_argument,       NULL, 'V' },
		{ "sleep",       required_argument, NULL, 'S' },
		{ "flashfw",     required_argument, NULL, 'X' },
		{ NULL, 0, NULL, 0 }
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "hvBsKd:p:Pr:Rf:FLl:c:m:VS:X:",
	                          long_opts, NULL)) != -1) {
		/* Resolve idstr lazily on first device op */
		bool need_dev = (opt != 'h' && opt != 'v' && opt != 'B' &&
		                 opt != 's' && opt != 'K' && opt != 'd' &&
		                 opt != 'S');
		if (need_dev && !idstr) {
			size_t mc = 0; char **mice = NULL;
			idstr = first_mouse(&mice, &mc);
			cur_mice = mice;
			cur_mc   = mc;
		}

		switch (opt) {
		case 'h': usage(); goto done;
		case 'v': printf("razercfg (C) version %s\n", VERSION); goto done;
		case 'B':
			if (fork() != 0) goto done; /* parent exits */
			break;
		case 's': op_scan();            break;
		case 'K': op_reconfigure();     break;
		case 'd':
			/* Switch device — free previous lazy-resolved mice */
			if (cur_mice) { razerd_free_mice(cur_mice, cur_mc); cur_mice = NULL; }
			if (strcmp(optarg, "mouse") == 0) {
				size_t mc = 0; char **mice = NULL;
				idstr = first_mouse(&mice, &mc);
				cur_mice = mice; cur_mc = mc;
			} else {
				idstr = optarg;
			}
			break;
		case 'p': op_setprofile(idstr, optarg);  break;
		case 'P': op_getprofile(idstr);           break;
		case 'r': op_setres(idstr, optarg);       break;
		case 'R': op_getres(idstr);               break;
		case 'f': op_setfreq(idstr, optarg);      break;
		case 'F': op_getfreq(idstr);              break;
		case 'L': op_listleds(idstr);             break;
		case 'l': op_setled(idstr, optarg);       break;
		case 'c': op_setledcolor(idstr, optarg);  break;
		case 'm': op_setledmode(idstr, optarg);   break;
		case 'V': op_fwver(idstr);                break;
		case 'S': {
			double secs = atof(optarg);
			usleep((useconds_t)(secs * 1e6));
			break;
		}
		case 'X': op_flashfw(idstr, optarg); break;
		default:  usage(); razerd_close(r); return 1;
		}
	}

done:
	if (cur_mice) razerd_free_mice(cur_mice, cur_mc);
	razerd_close(r);
	return 0;
}
