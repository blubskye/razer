#include "dpipanel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    razerd_t    *r;
    char         idstr[64];

    /* slot selector */
    GtkDropDown *slot_combo;
    uint32_t    *slot_ids;    /* mapping IDs parallel to slot_combo */
    uint32_t    *slot_dims;   /* dim_mask parallel to slot_combo */
    uint32_t    *slot_res_x;  /* current X res per slot */
    uint32_t    *slot_res_y;  /* current Y res per slot */
    gboolean    *slot_mut;    /* is_mutable per slot */
    size_t       nslots;

    /* axis combos */
    GtkDropDown  *x_combo;
    GtkDropDown  *y_combo;
    GtkCheckButton *lock_btn;
    GtkWidget    *y_row;    /* hidden for single-axis devices */

    /* supported res values (parallel to x/y combo items) */
    uint32_t    *supp_res;
    size_t       nsupp_res;

    GtkButton   *apply_btn;
    GtkLabel    *status;

    gboolean     updating;  /* guard against signal loops */
} DpiData;

static void dpi_data_free(gpointer p)
{
    DpiData *d = p;
    free(d->slot_ids);
    free(d->slot_dims);
    free(d->slot_res_x);
    free(d->slot_res_y);
    free(d->slot_mut);
    free(d->supp_res);
    g_free(d);
}

/* ---------- axis combo sync ---------------------------------------- */

static void update_axis_combos(DpiData *d, guint slot_idx)
{
    if (slot_idx >= d->nslots) return;

    gboolean has_y   = (d->slot_dims[slot_idx] & 2u) != 0;
    gboolean mutable_ = d->slot_mut[slot_idx];
    uint32_t cur_x   = d->slot_res_x[slot_idx];
    uint32_t cur_y   = d->slot_res_y[slot_idx];

    gtk_widget_set_visible(d->y_row, has_y);
    gtk_widget_set_sensitive(GTK_WIDGET(d->x_combo), mutable_);
    gtk_widget_set_sensitive(GTK_WIDGET(d->y_combo), mutable_ && has_y);
    gtk_widget_set_sensitive(GTK_WIDGET(d->lock_btn), mutable_ && has_y);

    d->updating = TRUE;
    for (size_t i = 0; i < d->nsupp_res; i++) {
        if (d->supp_res[i] == cur_x) {
            gtk_drop_down_set_selected(d->x_combo, (guint)i);
            break;
        }
    }
    if (has_y) {
        for (size_t i = 0; i < d->nsupp_res; i++) {
            if (d->supp_res[i] == cur_y) {
                gtk_drop_down_set_selected(d->y_combo, (guint)i);
                break;
            }
        }
    }
    d->updating = FALSE;
}

static void on_slot_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
    (void)pspec;
    DpiData *d = data;
    guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(obj));
    update_axis_combos(d, sel);
}

static void on_x_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
    (void)pspec;
    DpiData *d = data;
    if (d->updating) return;
    if (gtk_check_button_get_active(d->lock_btn) && gtk_widget_get_visible(d->y_row)) {
        guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(obj));
        d->updating = TRUE;
        gtk_drop_down_set_selected(d->y_combo, sel);
        d->updating = FALSE;
    }
}

static void on_y_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
    (void)pspec;
    DpiData *d = data;
    if (d->updating) return;
    if (gtk_check_button_get_active(d->lock_btn) && gtk_widget_get_visible(d->y_row)) {
        guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(obj));
        d->updating = TRUE;
        gtk_drop_down_set_selected(d->x_combo, sel);
        d->updating = FALSE;
    }
}

/* ---------- async apply -------------------------------------------- */

typedef struct {
    razerd_t *r;
    char      idstr[64];
    uint32_t  mapping_id;
    uint32_t  x_dpi;
    uint32_t  y_dpi;
    gboolean  has_y;
    gboolean  mutable_;
    gchar    *result;   /* filled by worker, freed after callback */
} ApplyTask;

static void apply_worker(GTask *task, gpointer src, gpointer tdata,
                          GCancellable *cancel)
{
    (void)src; (void)cancel;
    ApplyTask *at = tdata;

    if (at->mutable_) {
        int err = razerd_change_dpi_mapping(at->r, at->idstr, at->mapping_id, 0, at->x_dpi);
        if (err) {
            at->result = g_strdup_printf("Error setting X DPI: %d", err);
            g_task_return_pointer(task, at->result, NULL);
            return;
        }
        if (at->has_y) {
            err = razerd_change_dpi_mapping(at->r, at->idstr, at->mapping_id, 1, at->y_dpi);
            if (err) {
                at->result = g_strdup_printf("Error setting Y DPI: %d", err);
                g_task_return_pointer(task, at->result, NULL);
                return;
            }
        }
    }
    int err = razerd_set_dpi_mapping(at->r, at->idstr, 0, at->mapping_id, 0xFFFFFFFFu);
    at->result = err ? g_strdup_printf("Error activating slot: %d", err)
                     : g_strdup("DPI applied.");
    g_task_return_pointer(task, at->result, NULL);
}

static void apply_done(GObject *src, GAsyncResult *res, gpointer data)
{
    (void)src;
    DpiData  *d      = data;
    GTask    *task   = G_TASK(res);
    gchar    *result = g_task_propagate_pointer(task, NULL);
    gtk_label_set_text(d->status, result ? result : "");
    gtk_widget_set_sensitive(GTK_WIDGET(d->apply_btn), TRUE);
    g_free(result);
}

static void on_apply(GtkButton *btn, gpointer data)
{
    (void)btn;
    DpiData *d = data;
    guint slot_sel = gtk_drop_down_get_selected(d->slot_combo);
    if (slot_sel == GTK_INVALID_LIST_POSITION || slot_sel >= d->nslots) {
        gtk_label_set_text(d->status, "No slot selected.");
        return;
    }

    gboolean locked = gtk_check_button_get_active(d->lock_btn);
    guint x_sel = gtk_drop_down_get_selected(d->x_combo);
    guint y_sel = locked ? x_sel : gtk_drop_down_get_selected(d->y_combo);

    ApplyTask *at = g_new0(ApplyTask, 1);
    at->r          = d->r;
    g_strlcpy(at->idstr, d->idstr, sizeof(at->idstr));
    at->mapping_id = d->slot_ids[slot_sel];
    at->x_dpi      = (x_sel < d->nsupp_res) ? d->supp_res[x_sel] : 0;
    at->y_dpi      = (y_sel < d->nsupp_res) ? d->supp_res[y_sel] : 0;
    at->has_y      = (d->slot_dims[slot_sel] & 2u) != 0;
    at->mutable_   = d->slot_mut[slot_sel];

    gtk_widget_set_sensitive(GTK_WIDGET(d->apply_btn), FALSE);
    gtk_label_set_text(d->status, "Applying…");

    GTask *task = g_task_new(NULL, NULL, apply_done, d);
    g_task_set_task_data(task, at, g_free);
    g_task_run_in_thread(task, apply_worker);
    g_object_unref(task);
}

/* ---------- constructor -------------------------------------------- */

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

    /* ----- Load supported resolutions ------------------------------ */
    uint32_t *sres = NULL;
    size_t    src  = 0;
    GtkStringList *res_sl = gtk_string_list_new(NULL);
    if (razerd_get_supported_res(r, idstr, &sres, &src) == 0 && src > 0) {
        d->supp_res  = malloc(src * sizeof(uint32_t));
        d->nsupp_res = src;
        for (size_t i = 0; i < src; i++) {
            d->supp_res[i] = sres[i];
            gchar *lbl = g_strdup_printf("%u DPI", sres[i]);
            gtk_string_list_append(res_sl, lbl);
            g_free(lbl);
        }
        razerd_free_supported_res(sres);
    }

    /* ----- Load DPI mapping slots ---------------------------------- */
    uint32_t active = 0;
    guint    active_idx = 0;
    (void)razerd_get_dpi_mapping(r, idstr, 0, 0xFFFFFFFFu, &active);

    GtkStringList *slot_sl = gtk_string_list_new(NULL);
    razerd_dpi_mapping_t *maps = NULL;
    size_t mc = 0;
    if (razerd_get_dpi_mappings(r, idstr, &maps, &mc) == 0) {
        d->slot_ids   = malloc(mc * sizeof(uint32_t));
        d->slot_dims  = malloc(mc * sizeof(uint32_t));
        d->slot_res_x = malloc(mc * sizeof(uint32_t));
        d->slot_res_y = malloc(mc * sizeof(uint32_t));
        d->slot_mut   = malloc(mc * sizeof(gboolean));
        d->nslots     = mc;
        for (size_t i = 0; i < mc; i++) {
            d->slot_ids[i]   = maps[i].id;
            d->slot_dims[i]  = maps[i].dim_mask;
            d->slot_res_x[i] = maps[i].res[0];
            d->slot_res_y[i] = maps[i].res[1];
            d->slot_mut[i]   = maps[i].is_mutable;
            gchar *lbl = g_strdup_printf("%u DPI", maps[i].res[0]);
            if (maps[i].dim_mask & 2u) {
                gchar *tmp = g_strdup_printf("%s x %u DPI", lbl, maps[i].res[1]);
                g_free(lbl); lbl = tmp;
            }
            gtk_string_list_append(slot_sl, lbl);
            g_free(lbl);
            if (maps[i].id == active)
                active_idx = (guint)i;
        }
        razerd_free_dpi_mappings(maps);
    }

    /* ----- Slot row ------------------------------------------------- */
    GtkWidget *hbox0 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(hbox0), gtk_label_new("DPI slot:"));
    GtkWidget *slot_dd = gtk_drop_down_new(G_LIST_MODEL(slot_sl), NULL);
    d->slot_combo = GTK_DROP_DOWN(slot_dd);
    gtk_widget_set_hexpand(slot_dd, TRUE);
    gtk_box_append(GTK_BOX(hbox0), slot_dd);
    gtk_box_append(GTK_BOX(box), hbox0);
    g_object_unref(slot_sl);

    /* ----- X DPI row ------------------------------------------------ */
    GtkWidget *hbox1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(hbox1), gtk_label_new("X DPI:"));
    GtkWidget *x_dd = gtk_drop_down_new(G_LIST_MODEL(res_sl), NULL);
    d->x_combo = GTK_DROP_DOWN(x_dd);
    gtk_widget_set_hexpand(x_dd, TRUE);
    gtk_box_append(GTK_BOX(hbox1), x_dd);
    gtk_box_append(GTK_BOX(box), hbox1);

    /* ----- Y DPI row (hidden for single-axis) ----------------------- */
    GtkWidget *hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(hbox2), gtk_label_new("Y DPI:"));
    GtkWidget *y_dd = gtk_drop_down_new(G_LIST_MODEL(res_sl), NULL);
    d->y_combo = GTK_DROP_DOWN(y_dd);
    gtk_widget_set_hexpand(y_dd, TRUE);
    gtk_box_append(GTK_BOX(hbox2), y_dd);
    GtkWidget *lock_chk = gtk_check_button_new_with_label("Lock X = Y");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(lock_chk), TRUE);
    d->lock_btn = GTK_CHECK_BUTTON(lock_chk);
    gtk_box_append(GTK_BOX(hbox2), lock_chk);
    d->y_row = hbox2;
    gtk_box_append(GTK_BOX(box), hbox2);
    g_object_unref(res_sl);

    /* ----- Apply ---------------------------------------------------- */
    GtkWidget *apply = gtk_button_new_with_label("Apply");
    gtk_widget_set_halign(apply, GTK_ALIGN_START);
    d->apply_btn = GTK_BUTTON(apply);
    gtk_box_append(GTK_BOX(box), apply);

    GtkWidget *status = gtk_label_new("");
    gtk_widget_set_halign(status, GTK_ALIGN_START);
    d->status = GTK_LABEL(status);
    gtk_box_append(GTK_BOX(box), status);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(box), spacer);

    /* Initial axis state */
    gtk_drop_down_set_selected(d->slot_combo, active_idx);
    update_axis_combos(d, active_idx);

    g_signal_connect(slot_dd, "notify::selected", G_CALLBACK(on_slot_changed), d);
    g_signal_connect(x_dd,    "notify::selected", G_CALLBACK(on_x_changed),    d);
    g_signal_connect(y_dd,    "notify::selected", G_CALLBACK(on_y_changed),    d);
    g_signal_connect(apply,   "clicked",          G_CALLBACK(on_apply),        d);

    g_object_set_data_full(G_OBJECT(box), "dpi-data", d, dpi_data_free);
    return box;
}
