#include "mainwindow.h"
#include "dpipanel.h"
#include "ledpanel.h"
#include "freqpanel.h"
#include "profilepanel.h"
#include "buttonspanel.h"

#include <glib-unix.h>
#include <string.h>
#include <stdlib.h>

/* Per-window state attached via g_object_set_data_full */
typedef struct {
    razerd_t      *r;
    GtkDropDown   *devdrop;
    GtkStringList *devnames;
    GtkNotebook   *notebook;
    GPtrArray     *idstrs;   /* GPtrArray<gchar*> — parallel to devnames */
    guint          hotplug_id;
} MainWin;

static void main_win_free(gpointer p)
{
    MainWin *mw = p;
    if (mw->hotplug_id)
        g_source_remove(mw->hotplug_id);
    g_ptr_array_unref(mw->idstrs);
    g_free(mw);
}

/* Forward declarations */
static void refresh_tabs(MainWin *mw);
static void populate_device_list(MainWin *mw);

/* ---------- helpers ------------------------------------------------ */

static void refresh_tabs(MainWin *mw)
{
    while (gtk_notebook_get_n_pages(mw->notebook) > 0)
        gtk_notebook_remove_page(mw->notebook, -1);

    guint sel = gtk_drop_down_get_selected(mw->devdrop);
    if (sel == GTK_INVALID_LIST_POSITION || sel >= mw->idstrs->len)
        return;

    const char *idstr = mw->idstrs->pdata[sel];

    struct { const char *label; GtkWidget *(*fn)(razerd_t *, const char *); } tabs[] = {
        { "DPI",       dpi_panel_new     },
        { "LED",       led_panel_new     },
        { "Frequency", freq_panel_new    },
        { "Profiles",  profile_panel_new },
        { "Buttons",   buttons_panel_new },
    };

    for (size_t i = 0; i < sizeof(tabs) / sizeof(tabs[0]); i++) {
        GtkWidget *page  = tabs[i].fn(mw->r, idstr);
        GtkWidget *label = gtk_label_new(tabs[i].label);
        gtk_notebook_append_page(mw->notebook, page, label);
    }
}

static void populate_device_list(MainWin *mw)
{
    guint old = g_list_model_get_n_items(G_LIST_MODEL(mw->devnames));
    if (old)
        gtk_string_list_splice(mw->devnames, 0, old, NULL);
    g_ptr_array_set_size(mw->idstrs, 0);

    char **mice = NULL;
    size_t mc   = 0;
    if (razerd_get_mice(mw->r, &mice, &mc) != 0 || mc == 0) {
        razerd_free_mice(mice, mc);
        refresh_tabs(mw);
        return;
    }

    for (size_t i = 0; i < mc; i++) {
        gtk_string_list_append(mw->devnames, mice[i]);
        g_ptr_array_add(mw->idstrs, g_strdup(mice[i]));
    }
    razerd_free_mice(mice, mc);

    gtk_drop_down_set_selected(mw->devdrop, 0);
    refresh_tabs(mw);
}

/* ---------- signal handlers ---------------------------------------- */

static void on_device_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
    (void)obj; (void)pspec;
    refresh_tabs(data);
}

static gboolean on_hotplug(gint fd, GIOCondition cond, gpointer data)
{
    (void)fd; (void)cond;
    MainWin *mw = data;
    razerd_event_t ev;
    while (razerd_read_event(mw->r, &ev) == 0)
        ; /* drain */
    populate_device_list(mw);
    return G_SOURCE_CONTINUE;
}

/* ---------- constructor -------------------------------------------- */

GtkWidget *mainwindow_new(GtkApplication *app, razerd_t *r)
{
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Razer Device Configuration");
    gtk_window_set_default_size(GTK_WINDOW(window), 520, 420);

    MainWin *mw  = g_new0(MainWin, 1);
    mw->r        = r;
    mw->devnames = gtk_string_list_new(NULL);
    mw->idstrs   = g_ptr_array_new_with_free_func(g_free);

    g_object_set_data_full(G_OBJECT(window), "mainwin", mw, main_win_free);

    /* Outer vertical box */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(vbox,    8);
    gtk_widget_set_margin_bottom(vbox, 8);
    gtk_widget_set_margin_start(vbox,  8);
    gtk_widget_set_margin_end(vbox,    8);
    gtk_window_set_child(GTK_WINDOW(window), vbox);

    /* Device selection row */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *lbl  = gtk_label_new("Device:");
    GtkWidget *dd   = gtk_drop_down_new(G_LIST_MODEL(mw->devnames), NULL);
    mw->devdrop     = GTK_DROP_DOWN(dd);
    gtk_box_append(GTK_BOX(hbox), lbl);
    gtk_box_append(GTK_BOX(hbox), dd);
    gtk_box_append(GTK_BOX(vbox), hbox);

    /* Notebook */
    GtkWidget *nb = gtk_notebook_new();
    mw->notebook  = GTK_NOTEBOOK(nb);
    gtk_widget_set_vexpand(nb, TRUE);
    gtk_box_append(GTK_BOX(vbox), nb);

    g_signal_connect(dd, "notify::selected", G_CALLBACK(on_device_changed), mw);

    /* Hot-plug via razerd notify fd */
    int nfd = razerd_get_notify_fd(r);
    if (nfd >= 0)
        mw->hotplug_id = g_unix_fd_add(nfd, G_IO_IN, on_hotplug, mw);

    populate_device_list(mw);

    return window;
}
