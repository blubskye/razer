/*
 * Lowlevel hardware access for the Razer Orochi mouse family.
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

#include "hw_orochi.h"
#include "hw_chroma_extended.h"
#include "razer_private.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define OROCHI_LOGO_NAME	"Logo LED"

#define OROCHI_MAX_RES		RAZER_MOUSE_RES_8200DPI
#define OROCHI_RES_STEP		RAZER_MOUSE_RES_100DPI
#define OROCHI_PACKET_MS	35
#define OROCHI_LED_NUM		1

static enum razer_mouse_freq orochi_freqs[] = {
	RAZER_MOUSE_FREQ_125HZ, RAZER_MOUSE_FREQ_500HZ, RAZER_MOUSE_FREQ_1000HZ,
};

static enum razer_mouse_res orochi_res_stages[] = {
	RAZER_MOUSE_RES_450DPI,
	RAZER_MOUSE_RES_900DPI,
	RAZER_MOUSE_RES_1800DPI,
	RAZER_MOUSE_RES_3500DPI,
	RAZER_MOUSE_RES_8200DPI,
};

#define OROCHI_DPIMAPPINGS_NUM	ARRAY_SIZE(orochi_res_stages)
#define OROCHI_FREQS_NUM	ARRAY_SIZE(orochi_freqs)

struct orochi_led
{
	uint8_t id;
	uint8_t brightness;
	struct razer_rgb_color color;
	enum razer_led_mode mode;
};

struct orochi_drv_data
{
	struct razer_event_spacing	packet_spacing;
	struct razer_mouse_profile	profile;
	struct razer_mouse_dpimapping	*current_dpimapping;
	enum razer_mouse_freq		current_freq;
	struct orochi_led		logo_led;
	struct razer_mouse_dpimapping	dpimappings[OROCHI_DPIMAPPINGS_NUM];
	struct razer_axis		axes[3];
	uint16_t			fw_version;
	const char			*device_name;
};

static struct orochi_led *orochi_get_led(struct orochi_drv_data *d,
					 uint8_t id)
{
	if (id == d->logo_led.id)
		return &d->logo_led;
	return NULL;
}

static int orochi_send_init(struct razer_mouse *m)
{
	struct orochi_drv_data *d = m->drv_data;
	struct razer_chroma_ext_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.tx_id     = RAZER_CHROMA_EXT_TX_DEFAULT;
	cmd.size      = 0x02;                    /* 2-byte payload */
	cmd.request   = cpu_to_be16(0x0004);     /* init/handshake request code */
	cmd.bvalue[0] = 0x03;                    /* init payload byte */
	return razer_chroma_ext_send(m, &d->packet_spacing, &cmd);
}

static int orochi_send_set_resolution(struct razer_mouse *m)
{
	struct orochi_drv_data *d = m->drv_data;
	uint16_t rx = d->current_dpimapping->res[RAZER_DIM_X];
	uint16_t ry = d->current_dpimapping->res[RAZER_DIM_Y];

	return razer_chroma_ext_set_dpi(m, &d->packet_spacing, rx, ry);
}

static int orochi_send_set_frequency(struct razer_mouse *m)
{
	struct orochi_drv_data *d = m->drv_data;
	int tfreq;

	switch (d->current_freq) {
	case RAZER_MOUSE_FREQ_UNKNOWN:
		return -EINVAL;
	case RAZER_MOUSE_FREQ_125HZ:
	case RAZER_MOUSE_FREQ_500HZ:
	case RAZER_MOUSE_FREQ_1000HZ:
		tfreq = RAZER_MOUSE_FREQ_1000HZ / d->current_freq;
		break;
	default:
		return -EINVAL;
	}
	return razer_chroma_ext_set_freq(m, &d->packet_spacing, tfreq);
}

static int orochi_send_set_led(struct razer_mouse *m,
			       struct orochi_led *led)
{
	struct orochi_drv_data *d = m->drv_data;

	return razer_chroma_ext_set_brightness(m, &d->packet_spacing,
					       led->id, led->brightness);
}

static int orochi_get_fw_version(struct razer_mouse *m)
{
	return -ENOSYS;
}

static struct razer_mouse_profile *orochi_get_profiles(struct razer_mouse *m)
{
	return &((struct orochi_drv_data *)m->drv_data)->profile;
}

static int orochi_supported_axes(struct razer_mouse *m,
				 struct razer_axis **res_ptr)
{
	struct orochi_drv_data *d = m->drv_data;

	*res_ptr = d->axes;
	return ARRAY_SIZE(d->axes);
}

static int orochi_supported_dpimappings(struct razer_mouse *m,
					struct razer_mouse_dpimapping **res_ptr)
{
	struct orochi_drv_data *d = m->drv_data;

	*res_ptr = d->dpimappings;
	return ARRAY_SIZE(d->dpimappings);
}

static int orochi_supported_resolutions(struct razer_mouse *m,
					enum razer_mouse_res **res_ptr)
{
	size_t i;
	size_t n = OROCHI_MAX_RES / OROCHI_RES_STEP;

	*res_ptr = calloc(n, sizeof(enum razer_mouse_res));
	if (!*res_ptr)
		return -ENOMEM;
	for (i = 0; i < n; i++)
		(*res_ptr)[i] = (i + 1) * OROCHI_RES_STEP;
	return n;
}

static int orochi_supported_freqs(struct razer_mouse *m,
				  enum razer_mouse_freq **res_ptr)
{
	*res_ptr = malloc(sizeof(orochi_freqs));
	if (!*res_ptr)
		return -ENOMEM;
	memcpy(*res_ptr, orochi_freqs, sizeof(orochi_freqs));
	return OROCHI_FREQS_NUM;
}

static enum razer_mouse_freq orochi_get_freq(struct razer_mouse_profile *p)
{
	return ((struct orochi_drv_data *)p->mouse->drv_data)->current_freq;
}

static struct razer_mouse_dpimapping *
orochi_get_dpimapping(struct razer_mouse_profile *p, struct razer_axis *ax)
{
	return ((struct orochi_drv_data *)p->mouse->drv_data)->current_dpimapping;
}

static int orochi_change_dpimapping(struct razer_mouse_dpimapping *d,
				    enum razer_dimension dim,
				    enum razer_mouse_res res)
{
	struct orochi_drv_data *priv = d->mouse->drv_data;

	if (!(d->dimension_mask & (1 << dim)))
		return -EINVAL;
	if (res == RAZER_MOUSE_RES_UNKNOWN)
		res = RAZER_MOUSE_RES_1800DPI;
	if (res < OROCHI_RES_STEP || res > OROCHI_MAX_RES)
		return -EINVAL;
	d->res[dim] = res;
	if (d == priv->current_dpimapping)
		return orochi_send_set_resolution(d->mouse);
	return 0;
}

static int orochi_set_freq(struct razer_mouse_profile *p,
			   enum razer_mouse_freq freq)
{
	struct orochi_drv_data *d = p->mouse->drv_data;

	if (freq == RAZER_MOUSE_FREQ_UNKNOWN)
		freq = RAZER_MOUSE_FREQ_500HZ;
	if (freq != RAZER_MOUSE_FREQ_125HZ &&
	    freq != RAZER_MOUSE_FREQ_500HZ &&
	    freq != RAZER_MOUSE_FREQ_1000HZ)
		return -EINVAL;
	d->current_freq = freq;
	return orochi_send_set_frequency(p->mouse);
}

static int orochi_set_dpimapping(struct razer_mouse_profile *p,
				 struct razer_axis *ax,
				 struct razer_mouse_dpimapping *dm)
{
	struct orochi_drv_data *d = p->mouse->drv_data;

	if (ax && ax->id > 0)
		return -EINVAL;
	d->current_dpimapping = &d->dpimappings[dm->nr];
	return orochi_send_set_resolution(p->mouse);
}

static int orochi_led_toggle_state(struct razer_led *led,
				   enum razer_led_state new_state)
{
	int err;
	struct orochi_drv_data *d = led->u.mouse->drv_data;
	struct orochi_led *priv_led = orochi_get_led(d, led->id);

	if (!priv_led)
		return -EINVAL;

	if (new_state == RAZER_LED_OFF) {
		priv_led->brightness = 0x00;
		return razer_chroma_ext_set_brightness(led->u.mouse,
						       &d->packet_spacing,
						       priv_led->id, 0x00);
	}

	/* Turning ON: restore brightness then re-apply stored mode/color */
	priv_led->brightness = 0xFF;
	err = razer_chroma_ext_set_brightness(led->u.mouse, &d->packet_spacing,
					      priv_led->id, 0xFF);
	if (err)
		return err;

	/* Re-apply mode so device restores the effect */
	switch (priv_led->mode) {
	case RAZER_LED_MODE_SPECTRUM:
		return razer_chroma_ext_set_spectrum(led->u.mouse,
						     &d->packet_spacing,
						     priv_led->id);
	case RAZER_LED_MODE_BREATHING:
		return razer_chroma_ext_set_breathing(led->u.mouse,
						      &d->packet_spacing,
						      priv_led->id,
						      priv_led->color.r,
						      priv_led->color.g,
						      priv_led->color.b);
	default:
		return razer_chroma_ext_set_static_color(led->u.mouse,
							 &d->packet_spacing,
							 priv_led->id,
							 priv_led->color.r,
							 priv_led->color.g,
							 priv_led->color.b);
	}
}

static int orochi_led_change_color(struct razer_led *led,
				   const struct razer_rgb_color *color)
{
	struct orochi_drv_data *d = led->u.mouse->drv_data;
	struct orochi_led *priv_led = orochi_get_led(d, led->id);

	if (!priv_led)
		return -EINVAL;
	if (!color || !color->valid)
		return 0;
	priv_led->color = *color;
	return razer_chroma_ext_set_static_color(led->u.mouse, &d->packet_spacing,
						 priv_led->id,
						 color->r, color->g, color->b);
}

static int orochi_led_set_mode(struct razer_led *led,
			       enum razer_led_mode new_mode)
{
	struct orochi_drv_data *d = led->u.mouse->drv_data;
	struct orochi_led *priv_led = orochi_get_led(d, led->id);

	if (!priv_led)
		return -EINVAL;
	priv_led->mode = new_mode;
	if (!priv_led->color.valid) {
		priv_led->color.r = 0xFF;
		priv_led->color.g = 0xFF;
		priv_led->color.b = 0xFF;
		priv_led->color.valid = 1;
	}
	switch (new_mode) {
	case RAZER_LED_MODE_SPECTRUM:
		return razer_chroma_ext_set_spectrum(led->u.mouse,
						     &d->packet_spacing,
						     priv_led->id);
	case RAZER_LED_MODE_BREATHING:
		return razer_chroma_ext_set_breathing(led->u.mouse,
						      &d->packet_spacing,
						      priv_led->id,
						      priv_led->color.r,
						      priv_led->color.g,
						      priv_led->color.b);
	default:
		return razer_chroma_ext_set_static_color(led->u.mouse,
							 &d->packet_spacing,
							 priv_led->id,
							 priv_led->color.r,
							 priv_led->color.g,
							 priv_led->color.b);
	}
}

static int orochi_get_leds(struct razer_mouse *m,
			   struct razer_led **leds_list)
{
	struct orochi_drv_data *d = m->drv_data;
	unsigned int modes = (1 << RAZER_LED_MODE_STATIC) |
			     (1 << RAZER_LED_MODE_SPECTRUM) |
			     (1 << RAZER_LED_MODE_BREATHING);
	struct razer_led *logo;

	logo = zalloc(sizeof(*logo));
	if (!logo)
		return -ENOMEM;

	*logo = (struct razer_led){
		.id	= d->logo_led.id,
		.name	= OROCHI_LOGO_NAME,
		.state	= d->logo_led.brightness ? RAZER_LED_ON : RAZER_LED_OFF,
		.color	= d->logo_led.color,
		.mode	= d->logo_led.mode,
		.supported_modes_mask = modes,
		.u.mouse      = m,
		.next         = NULL,
		.toggle_state = orochi_led_toggle_state,
		.change_color = orochi_led_change_color,
		.set_mode     = orochi_led_set_mode,
	};

	*leds_list = logo;
	return OROCHI_LED_NUM;
}

int razer_orochi_init(struct razer_mouse *m,
		      struct libusb_device *usbdev)
{
	int err;
	size_t i;
	struct orochi_drv_data *d;
	const char *devname;
	struct libusb_device_descriptor desc;

	/* Determine per-PID device name */
	if (libusb_get_device_descriptor(usbdev, &desc) == 0) {
		switch (desc.idProduct) {
		case 0x0048: devname = "Orochi (wired)";   break;
		case 0x0032: devname = "Orochi (wireless)"; break;
		case 0x0113: devname = "Orochi V2";         break;
		default:     devname = "Orochi";             break;
		}
	} else {
		devname = "Orochi";
	}

	d = zalloc(sizeof(*d));
	if (!d)
		return -ENOMEM;

	d->device_name = devname;

	razer_event_spacing_init(&d->packet_spacing, OROCHI_PACKET_MS);

	for (i = 0; i < OROCHI_DPIMAPPINGS_NUM; i++) {
		d->dpimappings[i] = (struct razer_mouse_dpimapping){
			.nr		= i,
			.change		= orochi_change_dpimapping,
			.dimension_mask	= (1 << RAZER_DIM_X) | (1 << RAZER_DIM_Y),
			.mouse		= m,
		};
		d->dpimappings[i].res[RAZER_DIM_X] =
		d->dpimappings[i].res[RAZER_DIM_Y] = orochi_res_stages[i];
	}

	d->current_dpimapping = &d->dpimappings[2]; /* default: 1800 DPI */
	d->current_freq = RAZER_MOUSE_FREQ_500HZ;

	d->logo_led = (struct orochi_led){
		.id         = RAZER_CHROMA_EXT_LED_LOGO,
		.brightness = 0xFF,
		.color      = { .r = 0xFF, .g = 0xFF, .b = 0xFF, .valid = 1 },
	};

	razer_init_axes(d->axes, "X/Y", RAZER_AXIS_INDEPENDENT_DPIMAPPING,
			"Scroll", 0, NULL, 0);

	m->drv_data = d;

	if ((err = razer_usb_add_used_interface(m->usb_ctx, 0, 0)) ||
	    (err = m->claim(m))) {
		free(d);
		return err;
	}

	if ((err = orochi_send_init(m)) ||
	    (err = orochi_send_set_resolution(m)) ||
	    (err = orochi_send_set_frequency(m)) ||
	    (err = orochi_send_set_led(m, &d->logo_led)) ||
	    (err = razer_chroma_ext_set_static_color(m, &d->packet_spacing,
						     d->logo_led.id,
						     d->logo_led.color.r,
						     d->logo_led.color.g,
						     d->logo_led.color.b))) {
		m->release(m);
		free(d);
		return err;
	}

	m->release(m);

	d->profile = (struct razer_mouse_profile){
		.mouse          = m,
		.get_freq       = orochi_get_freq,
		.set_freq       = orochi_set_freq,
		.get_dpimapping = orochi_get_dpimapping,
		.set_dpimapping = orochi_set_dpimapping,
	};

	razer_generic_usb_gen_idstr(usbdev, m->usb_ctx->h,
				    d->device_name, false,
				    NULL, m->idstr);

	m->type			= RAZER_MOUSETYPE_DEATHADDER;
	m->get_fw_version	= orochi_get_fw_version;
	m->global_get_leds	= orochi_get_leds;
	m->get_profiles		= orochi_get_profiles;
	m->supported_axes	= orochi_supported_axes;
	m->supported_resolutions = orochi_supported_resolutions;
	m->supported_freqs	= orochi_supported_freqs;
	m->supported_dpimappings = orochi_supported_dpimappings;

	return 0;
}

void razer_orochi_release(struct razer_mouse *m)
{
	free(m->drv_data);
	m->drv_data = NULL;
}
