#pragma once
#include <gtk/gtk.h>
#include "librazerd.h"

GtkWidget *freq_panel_new(razerd_t *r, const char *idstr);
