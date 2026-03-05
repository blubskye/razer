#include "dpipanel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    razerd_t    *r;
    char         idstr[64];
    uint32_t    *ids;
    size_t       nids;
    GtkDropDown *combo;
    GtkLabel    *status;
} DpiData;

static void dpi_data_free(gpointer p)
{
    DpiData *d = p;
    free(d->ids);
    g_free(d);
}

static void on_apply(GtkButton *btn, gpointer data)
{
    (void)btn;
    DpiData *d = data;
    guint sel = gtk_drop_down_get_selected(d->combo);
    if (sel == GTK_INVALID_LIST_POSITION || sel >= d->nids) {
        gtk_label_set_text(d->status, "No mapping selected.");
        return;
    }
    uint32_t mapping_id = d->ids[sel];
    int err = razerd_set_dpi_mapping(d->r, d->idstr, 0, mapping_id, 0xFFFFFFFFu);
    char buf[64];
    if (err) {
        snprintf(buf, sizeof(buf), "Error setting DPI mapping: %d", err);
        gtk_label_set_text(d->status, buf);
    } else {
        gtk_label_set_text(d->status, "DPI mapping applied.");
    }
}

GtkWidget *dpi_panel_new(razerd_t *r, const char *idstr)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(box,    12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_widget_set_margin_start(box,  12);
    gtk_widget_set_margin_end(box,    12);

    DpiData *d = g_new0(DpiData, 1);
    d->r = r;
    g_strlcpy(d->idstr, idstr, sizeof(d->idstr));

    /* Device row */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(hbox), gtk_label_new("DPI mapping:"));

    GtkStringList *sl = gtk_string_list_new(NULL);
    GtkWidget     *dd = gtk_drop_down_new(G_LIST_MODEL(sl), NULL);
    d->combo = GTK_DROP_DOWN(dd);
    gtk_widget_set_hexpand(dd, TRUE);
    gtk_box_append(GTK_BOX(hbox), dd);
    gtk_box_append(GTK_BOX(box), hbox);

    /* Populate */
    uint32_t active     = 0;
    guint    active_idx = 0;
    (void)razerd_get_dpi_mapping(r, idstr, 0, 0xFFFFFFFFu, &active);

    razerd_dpi_mapping_t *maps = NULL;
    size_t mc = 0;
    if (razerd_get_dpi_mappings(r, idstr, &maps, &mc) == 0) {
        d->ids  = malloc(mc * sizeof(uint32_t));
        d->nids = mc;
        for (size_t i = 0; i < mc; i++) {
            gchar *label = g_strdup_printf("%u DPI", maps[i].res[0]);
            if (maps[i].dim_mask & 2u) {
                gchar *tmp = g_strdup_printf("%s x %u DPI", label, maps[i].res[1]);
                g_free(label);
                label = tmp;
            }
            gtk_string_list_append(sl, label);
            g_free(label);
            d->ids[i] = maps[i].id;
            if (maps[i].id == active)
                active_idx = (guint)i;
        }
        razerd_free_dpi_mappings(maps);
    }
    gtk_drop_down_set_selected(d->combo, active_idx);

    /* Apply button */
    GtkWidget *btn = gtk_button_new_with_label("Apply");
    gtk_widget_set_halign(btn, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), btn);

    GtkWidget *status = gtk_label_new("");
    gtk_widget_set_halign(status, GTK_ALIGN_START);
    d->status = GTK_LABEL(status);
    gtk_box_append(GTK_BOX(box), status);

    /* Filler */
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(box), spacer);

    g_object_set_data_full(G_OBJECT(box), "dpi-data", d, dpi_data_free);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_apply), d);

    return box;
}
