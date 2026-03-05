#pragma once
#include <gtk/gtk.h>
#include "librazerd.h"

GtkWidget *led_panel_new(razerd_t *r, const char *idstr);
