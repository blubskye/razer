/*
 * Shared helper for Razer mice using the Chroma extended matrix protocol.
 *
 * Important notice:
 * This hardware driver is based on reverse engineering, only.
 *
 * Copyright (C) 2026
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef RAZER_HW_CHROMA_EXTENDED_H_
#define RAZER_HW_CHROMA_EXTENDED_H_

#include "razer_private.h"

#include <stdint.h>

/* Transaction IDs */
#define RAZER_CHROMA_EXT_TX_DEFAULT     0xFF  /* DPI, freq, init */
#define RAZER_CHROMA_EXT_TX_LED         0x3F  /* All LED commands */

/* USB control transfer wValue */
#define RAZER_CHROMA_EXT_SETUP_VALUE    0x300

/* Response status indicating success */
#define RAZER_CHROMA_EXT_SUCCESS_STATUS 0x02

/* VARSTORE: persist setting to device flash */
#define RAZER_CHROMA_EXT_VARSTORE       0x01

/* Standard LED IDs used by most Razer mice */
#define RAZER_CHROMA_EXT_LED_SCROLL     0x01
#define RAZER_CHROMA_EXT_LED_LOGO       0x04

/* 90-byte HID command packet — identical layout to deathadder_essential_command */
struct razer_chroma_ext_cmd {
	uint8_t  status;
	uint8_t  tx_id;
	uint8_t  padding0[3];
	uint8_t  size;
	be16_t   request;

	union {
		uint8_t bvalue[80];
		struct {
			uint8_t padding1;
			be16_t  value[38];
			uint8_t padding2;
		} _packed;
	} _packed;

	uint8_t checksum;
	uint8_t padding3;
} _packed;

/**
 * razer_chroma_ext_send - Send a command and receive response.
 * @spacing: packet event spacing; may be NULL to skip timing enforcement.
 * Returns 0 on success, negative errno on failure.
 */
int razer_chroma_ext_send(struct razer_mouse *m,
			  struct razer_event_spacing *spacing,
			  struct razer_chroma_ext_cmd *cmd);

/* Set DPI (X and Y independently). Uses VARSTORE so setting persists. */
int razer_chroma_ext_set_dpi(struct razer_mouse *m,
			     struct razer_event_spacing *spacing,
			     uint16_t x_dpi, uint16_t y_dpi);

/* Set polling frequency. freq_arg = 1000/freq (e.g. 1=1000Hz, 4=250Hz, 8=125Hz). */
int razer_chroma_ext_set_freq(struct razer_mouse *m,
			      struct razer_event_spacing *spacing,
			      uint8_t freq_arg);

/* Set LED brightness: 0x00=off, 0xFF=full. Uses tx_id=0x3F. */
int razer_chroma_ext_set_brightness(struct razer_mouse *m,
				    struct razer_event_spacing *spacing,
				    uint8_t led_id, uint8_t brightness);

/* Set LED to static RGB color. Uses tx_id=0x3F. */
int razer_chroma_ext_set_static_color(struct razer_mouse *m,
				      struct razer_event_spacing *spacing,
				      uint8_t led_id,
				      uint8_t r, uint8_t g, uint8_t b);

/* Set LED to spectrum cycling. Uses tx_id=0x3F. */
int razer_chroma_ext_set_spectrum(struct razer_mouse *m,
				  struct razer_event_spacing *spacing,
				  uint8_t led_id);

/* Set LED to breathing (pulsing) with a given color. Uses tx_id=0x3F. */
int razer_chroma_ext_set_breathing(struct razer_mouse *m,
				   struct razer_event_spacing *spacing,
				   uint8_t led_id,
				   uint8_t r, uint8_t g, uint8_t b);

#endif /* RAZER_HW_CHROMA_EXTENDED_H_ */
