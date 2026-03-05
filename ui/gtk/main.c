#include "mainwindow.h"
#include "crash.h"
#include "librazerd.h"

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>

static void on_activate(GtkApplication *app, gpointer data)
{
    razerd_t  *r   = data;
    GtkWidget *win = mainwindow_new(app, r);
    gtk_window_present(GTK_WINDOW(win));
}

int main(int argc __attribute__((unused)), char **argv)
{
    install_crash_handler(argv[0]);

    razerd_t *r = razerd_open();
    if (!r) {
        fprintf(stderr, "qrazercfg: cannot connect to razerd socket\n");
        return 1;
    }

    GtkApplication *app = gtk_application_new("org.razer.qrazercfg",
                                               G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), r);

    int status = g_application_run(G_APPLICATION(app), 0, NULL);

    g_object_unref(app);
    razerd_close(r);
    return status;
}
