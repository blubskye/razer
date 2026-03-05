#include "freqpanel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    razerd_t    *r;
    char         idstr[64];
    uint32_t    *freqs;
    size_t       nfreqs;
    GtkDropDown *combo;
    GtkLabel    *status;
} FreqData;

static void freq_data_free(gpointer p)
{
    FreqData *d = p;
    free(d->freqs);
    g_free(d);
}

static void on_apply(GtkButton *btn, gpointer data)
{
    (void)btn;
    FreqData *d = data;
    guint sel = gtk_drop_down_get_selected(d->combo);
    if (sel == GTK_INVALID_LIST_POSITION || sel >= d->nfreqs) {
        gtk_label_set_text(d->status, "No frequency selected.");
        return;
    }
    uint32_t freq = d->freqs[sel];
    int err = razerd_set_freq(d->r, d->idstr, RAZERD_PROFILE_INVALID, freq);
    char buf[64];
    if (err) {
        snprintf(buf, sizeof(buf), "Error setting frequency: %d", err);
        gtk_label_set_text(d->status, buf);
    } else {
        gtk_label_set_text(d->status, "Frequency applied.");
    }
}

GtkWidget *freq_panel_new(razerd_t *r, const char *idstr)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(box,    12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_widget_set_margin_start(box,  12);
    gtk_widget_set_margin_end(box,    12);

    FreqData *d = g_new0(FreqData, 1);
    d->r = r;
    g_strlcpy(d->idstr, idstr, sizeof(d->idstr));

    /* Polling rate row */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(hbox), gtk_label_new("Polling rate:"));

    GtkStringList *sl = gtk_string_list_new(NULL);
    GtkWidget     *dd = gtk_drop_down_new(G_LIST_MODEL(sl), NULL);
    d->combo = GTK_DROP_DOWN(dd);
    gtk_widget_set_hexpand(dd, TRUE);
    gtk_box_append(GTK_BOX(hbox), dd);
    gtk_box_append(GTK_BOX(box), hbox);

    /* Populate */
    uint32_t cur     = 0;
    guint    cur_idx = 0;
    (void)razerd_get_freq(r, idstr, RAZERD_PROFILE_INVALID, &cur);

    uint32_t *freqs = NULL;
    size_t fc = 0;
    if (razerd_get_supported_freqs(r, idstr, &freqs, &fc) == 0) {
        d->freqs  = malloc(fc * sizeof(uint32_t));
        d->nfreqs = fc;
        for (size_t i = 0; i < fc; i++) {
            gchar *label = g_strdup_printf("%u Hz", freqs[i]);
            gtk_string_list_append(sl, label);
            g_free(label);
            d->freqs[i] = freqs[i];
            if (freqs[i] == cur)
                cur_idx = (guint)i;
        }
        razerd_free_freqs(freqs);
    }
    gtk_drop_down_set_selected(d->combo, cur_idx);

    /* Apply button */
    GtkWidget *btn = gtk_button_new_with_label("Apply");
    gtk_widget_set_halign(btn, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), btn);

    GtkWidget *status = gtk_label_new("");
    gtk_widget_set_halign(status, GTK_ALIGN_START);
    d->status = GTK_LABEL(status);
    gtk_box_append(GTK_BOX(box), status);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(box), spacer);

    g_object_set_data_full(G_OBJECT(box), "freq-data", d, freq_data_free);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_apply), d);

    return box;
}
