/*
 * Lowlevel hardware access for the Razer DeathAdder Elite mouse.
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

#include "hw_deathadder_elite.h"
#include "hw_chroma_extended.h"
#include "razer_private.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define ELITE_DEVICE_NAME	"DeathAdder Elite"
#define ELITE_SCROLL_NAME	"Scrollwheel"
#define ELITE_LOGO_NAME		"GlowingLogo"

#define ELITE_MAX_RES		RAZER_MOUSE_RES_16000DPI
#define ELITE_RES_STEP		RAZER_MOUSE_RES_100DPI
#define ELITE_PACKET_MS		35
#define ELITE_LED_NUM		2

static enum razer_mouse_freq elite_freqs[] = {
	RAZER_MOUSE_FREQ_125HZ, RAZER_MOUSE_FREQ_500HZ, RAZER_MOUSE_FREQ_1000HZ,
};

static enum razer_mouse_res elite_res_stages[] = {
	RAZER_MOUSE_RES_400DPI,  RAZER_MOUSE_RES_800DPI,
	RAZER_MOUSE_RES_1800DPI, RAZER_MOUSE_RES_3500DPI,
	RAZER_MOUSE_RES_5600DPI, RAZER_MOUSE_RES_8200DPI,
	RAZER_MOUSE_RES_10000DPI, RAZER_MOUSE_RES_16000DPI,
};

#define ELITE_DPIMAPPINGS_NUM	ARRAY_SIZE(elite_res_stages)
#define ELITE_FREQS_NUM		ARRAY_SIZE(elite_freqs)

struct elite_led
{
	uint8_t id;
	uint8_t brightness;
	struct razer_rgb_color color;
	enum razer_led_mode mode;
};

struct elite_drv_data
{
	struct razer_event_spacing	packet_spacing;
	struct razer_mouse_profile	profile;
	struct razer_mouse_dpimapping	*current_dpimapping;
	enum razer_mouse_freq		current_freq;
	struct elite_led		scroll_led;
	struct elite_led		logo_led;
	struct razer_mouse_dpimapping	dpimappings[ELITE_DPIMAPPINGS_NUM];
	struct razer_axis		axes[2];
	uint16_t			fw_version;
};

static struct elite_led *elite_get_led(struct elite_drv_data *d, uint8_t id)
{
	if (id == d->scroll_led.id)
		return &d->scroll_led;
	if (id == d->logo_led.id)
		return &d->logo_led;
	return NULL;
}

static int elite_send_init(struct razer_mouse *m)
{
	struct elite_drv_data *d = m->drv_data;
	struct razer_chroma_ext_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.tx_id     = RAZER_CHROMA_EXT_TX_DEFAULT;
	cmd.size      = 0x02;
	cmd.request   = cpu_to_be16(0x0004);
	cmd.bvalue[0] = 0x03;
	return razer_chroma_ext_send(m, &d->packet_spacing, &cmd);
}

static int elite_send_set_resolution(struct razer_mouse *m)
{
	struct elite_drv_data *d = m->drv_data;
	uint16_t rx = d->current_dpimapping->res[RAZER_DIM_X];
	uint16_t ry = d->current_dpimapping->res[RAZER_DIM_Y];

	return razer_chroma_ext_set_dpi(m, &d->packet_spacing, rx, ry);
}

static int elite_send_set_frequency(struct razer_mouse *m)
{
	struct elite_drv_data *d = m->drv_data;
	int tfreq;

	switch (d->current_freq) {
	case RAZER_MOUSE_FREQ_UNKNOWN:
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

static int elite_send_set_led(struct razer_mouse *m, struct elite_led *led)
{
	struct elite_drv_data *d = m->drv_data;

	return razer_chroma_ext_set_brightness(m, &d->packet_spacing,
					       led->id, led->brightness);
}

static int elite_get_fw_version(struct razer_mouse *m)
{
	return ((struct elite_drv_data *)m->drv_data)->fw_version;
}

static struct razer_mouse_profile *elite_get_profiles(struct razer_mouse *m)
{
	return &((struct elite_drv_data *)m->drv_data)->profile;
}

static int elite_supported_axes(struct razer_mouse *m,
				struct razer_axis **res_ptr)
{
	struct elite_drv_data *d = m->drv_data;

	*res_ptr = d->axes;
	return ARRAY_SIZE(d->axes);
}

static int elite_supported_dpimappings(struct razer_mouse *m,
				       struct razer_mouse_dpimapping **res_ptr)
{
	struct elite_drv_data *d = m->drv_data;

	*res_ptr = d->dpimappings;
	return ARRAY_SIZE(d->dpimappings);
}

static int elite_supported_resolutions(struct razer_mouse *m,
				       enum razer_mouse_res **res_ptr)
{
	size_t i;
	size_t n = ELITE_MAX_RES / ELITE_RES_STEP;

	*res_ptr = calloc(n, sizeof(enum razer_mouse_res));
	if (!*res_ptr)
		return -ENOMEM;
	for (i = 0; i < n; i++)
		(*res_ptr)[i] = (i + 1) * ELITE_RES_STEP;
	return n;
}

static int elite_supported_freqs(struct razer_mouse *m,
				 enum razer_mouse_freq **res_ptr)
{
	*res_ptr = malloc(sizeof(elite_freqs));
	if (!*res_ptr)
		return -ENOMEM;
	memcpy(*res_ptr, elite_freqs, sizeof(elite_freqs));
	return ELITE_FREQS_NUM;
}

static enum razer_mouse_freq elite_get_freq(struct razer_mouse_profile *p)
{
	return ((struct elite_drv_data *)p->mouse->drv_data)->current_freq;
}

static struct razer_mouse_dpimapping *
elite_get_dpimapping(struct razer_mouse_profile *p, struct razer_axis *ax)
{
	return ((struct elite_drv_data *)p->mouse->drv_data)->current_dpimapping;
}

static int elite_change_dpimapping(struct razer_mouse_dpimapping *d,
				   enum razer_dimension dim,
				   enum razer_mouse_res res)
{
	struct elite_drv_data *priv = d->mouse->drv_data;

	if (!(d->dimension_mask & (1 << dim)))
		return -EINVAL;
	if (res == RAZER_MOUSE_RES_UNKNOWN)
		res = RAZER_MOUSE_RES_1800DPI;
	if (res < ELITE_RES_STEP || res > ELITE_MAX_RES)
		return -EINVAL;
	d->res[dim] = res;
	if (d == priv->current_dpimapping)
		return elite_send_set_resolution(d->mouse);
	return 0;
}

static int elite_set_freq(struct razer_mouse_profile *p,
			  enum razer_mouse_freq freq)
{
	struct elite_drv_data *d = p->mouse->drv_data;

	if (freq == RAZER_MOUSE_FREQ_UNKNOWN)
		freq = RAZER_MOUSE_FREQ_500HZ;
	if (freq != RAZER_MOUSE_FREQ_125HZ &&
	    freq != RAZER_MOUSE_FREQ_500HZ &&
	    freq != RAZER_MOUSE_FREQ_1000HZ)
		return -EINVAL;
	d->current_freq = freq;
	return elite_send_set_frequency(p->mouse);
}

static int elite_set_dpimapping(struct razer_mouse_profile *p,
				struct razer_axis *ax,
				struct razer_mouse_dpimapping *dm)
{
	struct elite_drv_data *d = p->mouse->drv_data;

	if (ax && ax->id > 0)
		return -EINVAL;
	d->current_dpimapping = &d->dpimappings[dm->nr];
	return elite_send_set_resolution(p->mouse);
}

static int elite_led_toggle_state(struct razer_led *led,
				  enum razer_led_state new_state)
{
	struct elite_drv_data *d = led->u.mouse->drv_data;
	struct elite_led *priv_led = elite_get_led(d, led->id);

	if (!priv_led)
		return -EINVAL;
	priv_led->brightness = (new_state == RAZER_LED_OFF) ? 0x00 : 0xFF;
	return elite_send_set_led(led->u.mouse, priv_led);
}

static int elite_led_change_color(struct razer_led *led,
				  const struct razer_rgb_color *color)
{
	struct elite_drv_data *d = led->u.mouse->drv_data;
	struct elite_led *priv_led = elite_get_led(d, led->id);

	if (!priv_led)
		return -EINVAL;
	priv_led->color = *color;
	return razer_chroma_ext_set_static_color(led->u.mouse, &d->packet_spacing,
						 priv_led->id,
						 color->r, color->g, color->b);
}

static int elite_led_set_mode(struct razer_led *led,
			      enum razer_led_mode new_mode)
{
	struct elite_drv_data *d = led->u.mouse->drv_data;
	struct elite_led *priv_led = elite_get_led(d, led->id);

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

static int elite_get_leds(struct razer_mouse *m, struct razer_led **leds_list)
{
	struct elite_drv_data *d = m->drv_data;
	unsigned int modes = (1 << RAZER_LED_MODE_STATIC) |
			     (1 << RAZER_LED_MODE_SPECTRUM) |
			     (1 << RAZER_LED_MODE_BREATHING);
	struct razer_led *scroll, *logo;

	scroll = zalloc(sizeof(*scroll));
	if (!scroll)
		return -ENOMEM;
	logo = zalloc(sizeof(*logo));
	if (!logo) {
		free(scroll);
		return -ENOMEM;
	}

	*scroll = (struct razer_led){
		.id	= d->scroll_led.id,
		.name	= ELITE_SCROLL_NAME,
		.state	= d->scroll_led.brightness ? RAZER_LED_ON : RAZER_LED_OFF,
		.color	= d->scroll_led.color,
		.mode	= d->scroll_led.mode,
		.supported_modes_mask = modes,
		.u.mouse      = m,
		.next         = logo,
		.toggle_state = elite_led_toggle_state,
		.change_color = elite_led_change_color,
		.set_mode     = elite_led_set_mode,
	};

	*logo = (struct razer_led){
		.id	= d->logo_led.id,
		.name	= ELITE_LOGO_NAME,
		.state	= d->logo_led.brightness ? RAZER_LED_ON : RAZER_LED_OFF,
		.color	= d->logo_led.color,
		.mode	= d->logo_led.mode,
		.supported_modes_mask = modes,
		.u.mouse      = m,
		.toggle_state = elite_led_toggle_state,
		.change_color = elite_led_change_color,
		.set_mode     = elite_led_set_mode,
	};

	*leds_list = scroll;
	return ELITE_LED_NUM;
}

int razer_deathadder_elite_init(struct razer_mouse *m,
				struct libusb_device *usbdev)
{
	int err;
	size_t i;
	struct elite_drv_data *d;

	d = zalloc(sizeof(*d));
	if (!d)
		return -ENOMEM;

	razer_event_spacing_init(&d->packet_spacing, ELITE_PACKET_MS);

	for (i = 0; i < ELITE_DPIMAPPINGS_NUM; i++) {
		d->dpimappings[i] = (struct razer_mouse_dpimapping){
			.nr		= i,
			.change		= elite_change_dpimapping,
			.dimension_mask	= (1 << RAZER_DIM_X) | (1 << RAZER_DIM_Y),
			.mouse		= m,
		};
		d->dpimappings[i].res[RAZER_DIM_X] =
		d->dpimappings[i].res[RAZER_DIM_Y] = elite_res_stages[i];
	}

	d->current_dpimapping = &d->dpimappings[2]; /* default: 1800 DPI */
	d->current_freq = RAZER_MOUSE_FREQ_500HZ;

	d->scroll_led = (struct elite_led){
		.id         = RAZER_CHROMA_EXT_LED_SCROLL,
		.brightness = 0xFF,
		.color      = { .r = 0xFF, .g = 0xFF, .b = 0xFF, .valid = 1 },
	};
	d->logo_led = (struct elite_led){
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

	if ((err = elite_send_init(m)) ||
	    (err = elite_send_set_resolution(m)) ||
	    (err = elite_send_set_frequency(m)) ||
	    (err = elite_send_set_led(m, &d->scroll_led)) ||
	    (err = elite_send_set_led(m, &d->logo_led))) {
		m->release(m);
		free(d);
		return err;
	}

	m->release(m);

	d->profile = (struct razer_mouse_profile){
		.mouse          = m,
		.get_freq       = elite_get_freq,
		.set_freq       = elite_set_freq,
		.get_dpimapping = elite_get_dpimapping,
		.set_dpimapping = elite_set_dpimapping,
	};

	razer_generic_usb_gen_idstr(usbdev, m->usb_ctx->h,
				    ELITE_DEVICE_NAME, false,
				    NULL, m->idstr);

	m->type			= RAZER_MOUSETYPE_DEATHADDER;
	m->get_fw_version	= elite_get_fw_version;
	m->global_get_leds	= elite_get_leds;
	m->get_profiles		= elite_get_profiles;
	m->supported_axes	= elite_supported_axes;
	m->supported_resolutions = elite_supported_resolutions;
	m->supported_freqs	= elite_supported_freqs;
	m->supported_dpimappings = elite_supported_dpimappings;

	return 0;
}

void razer_deathadder_elite_release(struct razer_mouse *m)
{
	free(m->drv_data);
	m->drv_data = NULL;
}
