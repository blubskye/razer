#pragma once
#include <gtk/gtk.h>
#include "librazerd.h"

GtkWidget *profile_panel_new(razerd_t *r, const char *idstr);
