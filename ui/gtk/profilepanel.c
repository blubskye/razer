#include "profilepanel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    razerd_t   *r;
    char        idstr[64];
    uint32_t    active_id;
    GtkListBox *list;
    GtkEntry   *name_entry;
    GtkLabel   *status;
} ProfileData;

/* Forward declaration */
static void populate_list(ProfileData *d);

static void populate_list(ProfileData *d)
{
    gtk_list_box_remove_all(d->list);

    (void)razerd_get_active_profile(d->r, d->idstr, &d->active_id);

    uint32_t *ids = NULL;
    size_t    pc  = 0;
    if (razerd_get_profiles(d->r, d->idstr, &ids, &pc) != 0)
        return;

    for (size_t i = 0; i < pc; i++) {
        char  *name  = NULL;
        gchar *label = NULL;
        if (razerd_get_profile_name(d->r, d->idstr, ids[i], &name) == 0 && name) {
            if (ids[i] == d->active_id)
                label = g_strdup_printf("<b>%s  [active]</b>", name);
            else
                label = g_strdup(name);
            free(name);
        } else {
            if (ids[i] == d->active_id)
                label = g_strdup_printf("<b>Profile %u  [active]</b>", ids[i]);
            else
                label = g_strdup_printf("Profile %u", ids[i]);
        }

        GtkWidget *lbl = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(lbl), label);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
        g_free(label);

        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl);
        g_object_set_data(G_OBJECT(row), "profile-id", GUINT_TO_POINTER(ids[i]));
        gtk_list_box_append(d->list, row);
    }
    razerd_free_profiles(ids);
}

static void on_row_selected(GtkListBox *lb, GtkListBoxRow *row, gpointer data)
{
    (void)lb;
    ProfileData *d = data;
    if (!row) {
        gtk_editable_set_text(GTK_EDITABLE(d->name_entry), "");
        return;
    }
    uint32_t pid = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "profile-id"));
    char *name = NULL;
    if (razerd_get_profile_name(d->r, d->idstr, pid, &name) == 0 && name) {
        gtk_editable_set_text(GTK_EDITABLE(d->name_entry), name);
        free(name);
    } else {
        gtk_editable_set_text(GTK_EDITABLE(d->name_entry), "");
    }
}

static void on_set_active(GtkButton *btn, gpointer data)
{
    (void)btn;
    ProfileData   *d   = data;
    GtkListBoxRow *row = gtk_list_box_get_selected_row(d->list);
    if (!row) {
        gtk_label_set_text(d->status, "No profile selected.");
        return;
    }
    uint32_t pid = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "profile-id"));
    int err = razerd_set_active_profile(d->r, d->idstr, pid);
    char buf[64];
    if (err) {
        snprintf(buf, sizeof(buf), "Error setting active profile: %d", err);
        gtk_label_set_text(d->status, buf);
    } else {
        gtk_label_set_text(d->status, "Active profile changed.");
        populate_list(d);
    }
}

static void on_rename(GtkButton *btn, gpointer data)
{
    (void)btn;
    ProfileData   *d   = data;
    GtkListBoxRow *row = gtk_list_box_get_selected_row(d->list);
    if (!row) {
        gtk_label_set_text(d->status, "No profile selected.");
        return;
    }
    uint32_t    pid  = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "profile-id"));
    const char *name = gtk_editable_get_text(GTK_EDITABLE(d->name_entry));
    int err = razerd_set_profile_name(d->r, d->idstr, pid, name);
    char buf[64];
    if (err) {
        snprintf(buf, sizeof(buf), "Error renaming profile: %d", err);
        gtk_label_set_text(d->status, buf);
    } else {
        gtk_label_set_text(d->status, "Profile renamed.");
        populate_list(d);
    }
}

GtkWidget *profile_panel_new(razerd_t *r, const char *idstr)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(outer,    12);
    gtk_widget_set_margin_bottom(outer, 12);
    gtk_widget_set_margin_start(outer,  12);
    gtk_widget_set_margin_end(outer,    12);

    ProfileData *d = g_new0(ProfileData, 1);
    d->r = r;
    g_strlcpy(d->idstr, idstr, sizeof(d->idstr));

    /* Scrollable profile list */
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *lb = gtk_list_box_new();
    d->list = GTK_LIST_BOX(lb);
    gtk_list_box_set_selection_mode(d->list, GTK_SELECTION_SINGLE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), lb);
    gtk_box_append(GTK_BOX(outer), scroll);

    /* Rename row */
    GtkWidget *frame = gtk_frame_new("Rename selected profile");
    GtkWidget *rbox  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_top(rbox,    6);
    gtk_widget_set_margin_bottom(rbox, 6);
    gtk_widget_set_margin_start(rbox,  6);
    gtk_widget_set_margin_end(rbox,    6);

    GtkWidget *entry = gtk_entry_new();
    d->name_entry = GTK_ENTRY(entry);
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_box_append(GTK_BOX(rbox), entry);

    GtkWidget *ren_btn = gtk_button_new_with_label("Rename");
    gtk_box_append(GTK_BOX(rbox), ren_btn);
    gtk_frame_set_child(GTK_FRAME(frame), rbox);
    gtk_box_append(GTK_BOX(outer), frame);

    /* Set active button */
    GtkWidget *act_btn = gtk_button_new_with_label("Set Active");
    gtk_widget_set_halign(act_btn, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(outer), act_btn);

    GtkWidget *status = gtk_label_new("");
    gtk_widget_set_halign(status, GTK_ALIGN_START);
    d->status = GTK_LABEL(status);
    gtk_box_append(GTK_BOX(outer), status);

    g_signal_connect(lb,      "row-selected", G_CALLBACK(on_row_selected), d);
    g_signal_connect(act_btn, "clicked",      G_CALLBACK(on_set_active),   d);
    g_signal_connect(ren_btn, "clicked",      G_CALLBACK(on_rename),       d);

    g_object_set_data_full(G_OBJECT(outer), "profile-data", d, g_free);

    populate_list(d);
    return outer;
}
