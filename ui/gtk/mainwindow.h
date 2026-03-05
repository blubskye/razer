#pragma once
#include <gtk/gtk.h>
#include "librazerd.h"

GtkWidget *mainwindow_new(GtkApplication *app, razerd_t *r);
