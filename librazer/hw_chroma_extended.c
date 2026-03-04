/*
 * Shared helper for Razer mice using the Chroma extended matrix protocol.
 * Protocol reference: OpenRazer driver/razerchromacommon.c
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

#include "hw_chroma_extended.h"

#include <string.h>
#include <errno.h>

static uint8_t razer_chroma_ext_checksum(struct razer_chroma_ext_cmd *cmd)
{
	size_t ctrl_size = sizeof(cmd->size) + sizeof(cmd->request);

	return razer_xor8_checksum((uint8_t *)&cmd->size,
				   ctrl_size + cmd->size);
}

int razer_chroma_ext_send(struct razer_mouse *m,
			  struct razer_event_spacing *spacing,
			  struct razer_chroma_ext_cmd *cmd)
{
	int err;
	uint8_t checksum;

	BUILD_BUG_ON(sizeof(struct razer_chroma_ext_cmd) != 90);

	cmd->checksum = razer_chroma_ext_checksum(cmd);

	if (spacing)
		razer_event_spacing_enter(spacing);

	err = libusb_control_transfer(
		m->usb_ctx->h,
		LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS |
			LIBUSB_RECIPIENT_INTERFACE,
		LIBUSB_REQUEST_SET_CONFIGURATION,
		RAZER_CHROMA_EXT_SETUP_VALUE, 0,
		(unsigned char *)cmd, sizeof(*cmd), RAZER_USB_TIMEOUT);

	if (err != (int)sizeof(*cmd)) {
		if (spacing)
			razer_event_spacing_leave(spacing);
		razer_error("razer-chroma-ext: USB write failed: %d\n", err);
		return -EIO;
	}

	err = libusb_control_transfer(
		m->usb_ctx->h,
		LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS |
			LIBUSB_RECIPIENT_INTERFACE,
		LIBUSB_REQUEST_CLEAR_FEATURE,
		RAZER_CHROMA_EXT_SETUP_VALUE, 0,
		(unsigned char *)cmd, sizeof(*cmd), RAZER_USB_TIMEOUT);

	if (spacing)
		razer_event_spacing_leave(spacing);

	if (err != (int)sizeof(*cmd)) {
		razer_error("razer-chroma-ext: USB read failed: %d\n", err);
		return -EIO;
	}

	checksum = razer_chroma_ext_checksum(cmd);
	if (checksum != cmd->checksum) {
		razer_error("razer-chroma-ext: Bad response checksum "
			    "%02X (expected %02X)\n",
			    checksum, cmd->checksum);
		return -EBADMSG;
	}

	if (cmd->status != RAZER_CHROMA_EXT_SUCCESS_STATUS) {
		razer_error("razer-chroma-ext: Command status %02X\n",
			    cmd->status);
		return -EPROTO;
	}

	return 0;
}

int razer_chroma_ext_set_dpi(struct razer_mouse *m,
			     struct razer_event_spacing *spacing,
			     uint16_t x_dpi, uint16_t y_dpi)
{
	struct razer_chroma_ext_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.tx_id     = RAZER_CHROMA_EXT_TX_DEFAULT;
	cmd.size      = 0x07;
	cmd.request   = cpu_to_be16(0x0405);
	cmd.bvalue[0] = RAZER_CHROMA_EXT_VARSTORE;
	/*
	 * The union's inner struct has padding1 (1 byte) before value[],
	 * so value[0] aliases bvalue[1..2] and value[1] aliases bvalue[3..4].
	 * Layout sent: [VARSTORE, x_hi, x_lo, y_hi, y_lo, 0, 0]
	 */
	cmd.value[0]  = cpu_to_be16(x_dpi);
	cmd.value[1]  = cpu_to_be16(y_dpi);
	return razer_chroma_ext_send(m, spacing, &cmd);
}

int razer_chroma_ext_set_freq(struct razer_mouse *m,
			      struct razer_event_spacing *spacing,
			      uint8_t freq_arg)
{
	struct razer_chroma_ext_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.tx_id     = RAZER_CHROMA_EXT_TX_DEFAULT;
	cmd.size      = 0x01;
	cmd.request   = cpu_to_be16(0x0005);
	cmd.bvalue[0] = freq_arg;
	return razer_chroma_ext_send(m, spacing, &cmd);
}

int razer_chroma_ext_set_brightness(struct razer_mouse *m,
				    struct razer_event_spacing *spacing,
				    uint8_t led_id, uint8_t brightness)
{
	struct razer_chroma_ext_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.tx_id     = RAZER_CHROMA_EXT_TX_LED;
	cmd.size      = 0x03;
	cmd.request   = cpu_to_be16(0x0F04);
	cmd.bvalue[0] = RAZER_CHROMA_EXT_VARSTORE;
	cmd.bvalue[1] = led_id;
	cmd.bvalue[2] = brightness;
	return razer_chroma_ext_send(m, spacing, &cmd);
}

int razer_chroma_ext_set_static_color(struct razer_mouse *m,
				      struct razer_event_spacing *spacing,
				      uint8_t led_id,
				      uint8_t r, uint8_t g, uint8_t b)
{
	struct razer_chroma_ext_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.tx_id     = RAZER_CHROMA_EXT_TX_LED;
	cmd.size      = 0x09;
	cmd.request   = cpu_to_be16(0x0F02);
	cmd.bvalue[0] = RAZER_CHROMA_EXT_VARSTORE;
	cmd.bvalue[1] = led_id;
	cmd.bvalue[2] = 0x01; /* effect_id: static */
	/* bvalue[3..4] = 0x00 padding */
	cmd.bvalue[5] = 0x01; /* num_colors = 1 */
	cmd.bvalue[6] = r;
	cmd.bvalue[7] = g;
	cmd.bvalue[8] = b;
	return razer_chroma_ext_send(m, spacing, &cmd);
}

int razer_chroma_ext_set_spectrum(struct razer_mouse *m,
				  struct razer_event_spacing *spacing,
				  uint8_t led_id)
{
	struct razer_chroma_ext_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.tx_id     = RAZER_CHROMA_EXT_TX_LED;
	cmd.size      = 0x06;
	cmd.request   = cpu_to_be16(0x0F02);
	cmd.bvalue[0] = RAZER_CHROMA_EXT_VARSTORE;
	cmd.bvalue[1] = led_id;
	cmd.bvalue[2] = 0x03; /* effect_id: spectrum */
	return razer_chroma_ext_send(m, spacing, &cmd);
}

int razer_chroma_ext_set_breathing(struct razer_mouse *m,
				   struct razer_event_spacing *spacing,
				   uint8_t led_id,
				   uint8_t r, uint8_t g, uint8_t b)
{
	struct razer_chroma_ext_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.tx_id     = RAZER_CHROMA_EXT_TX_LED;
	cmd.size      = 0x09;
	cmd.request   = cpu_to_be16(0x0F02);
	cmd.bvalue[0] = RAZER_CHROMA_EXT_VARSTORE;
	cmd.bvalue[1] = led_id;
	cmd.bvalue[2] = 0x02; /* effect_id: breathing single */
	cmd.bvalue[3] = 0x01; /* num_colors = 1 */
	/* bvalue[4] = 0x00 padding */
	cmd.bvalue[5] = 0x01; /* OpenRazer sets this too */
	cmd.bvalue[6] = r;
	cmd.bvalue[7] = g;
	cmd.bvalue[8] = b;
	return razer_chroma_ext_send(m, spacing, &cmd);
}
