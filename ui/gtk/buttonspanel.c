#include "buttonspanel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t     button_id;
    GtkDropDown *func_drop;
} ButtonRow;

typedef struct {
    razerd_t  *r;
    char       idstr[64];
    ButtonRow *rows;
    size_t     nrows;
    uint32_t  *func_ids;
    size_t     nfunc_ids;
    GtkButton *apply_btn;
    GtkLabel  *status;
} ButtonsData;

static void buttons_data_free(gpointer p)
{
    ButtonsData *bd = p;
    free(bd->rows);
    free(bd->func_ids);
    g_free(bd);
}

/* ---------- async apply -------------------------------------------- */

typedef struct {
    razerd_t  *r;
    char       idstr[64];
    uint32_t  *button_ids;
    uint32_t  *func_ids_sel; /* selected func per button */
    size_t     nrows;
    gchar     *result;
} BtnsTask;

static void btns_task_free(gpointer p)
{
    BtnsTask *bt = p;
    free(bt->button_ids);
    free(bt->func_ids_sel);
    g_free(bt);
}

static void btns_worker(GTask *task, gpointer src, gpointer tdata, GCancellable *c)
{
    (void)src; (void)c;
    BtnsTask *bt = tdata;
    int errors = 0;
    for (size_t i = 0; i < bt->nrows; i++) {
        if (razerd_set_button_function(bt->r, bt->idstr,
                                        RAZERD_PROFILE_INVALID,
                                        bt->button_ids[i],
                                        bt->func_ids_sel[i]))
            errors++;
    }
    bt->result = errors ? g_strdup_printf("%zu button(s) failed to apply.", (size_t)errors)
                        : g_strdup("Buttons applied.");
    g_task_return_pointer(task, bt->result, NULL);
}

static void btns_done(GObject *src, GAsyncResult *res, gpointer data)
{
    (void)src;
    ButtonsData *bd     = data;
    gchar       *result = g_task_propagate_pointer(G_TASK(res), NULL);
    gtk_label_set_text(bd->status, result ? result : "");
    gtk_widget_set_sensitive(GTK_WIDGET(bd->apply_btn), TRUE);
    g_free(result);
}

static void on_apply(GtkButton *btn, gpointer data)
{
    (void)btn;
    ButtonsData *bd = data;

    /* Snapshot selections on main thread */
    BtnsTask *bt = g_new0(BtnsTask, 1);
    bt->r           = bd->r;
    g_strlcpy(bt->idstr, bd->idstr, sizeof(bt->idstr));
    bt->nrows       = bd->nrows;
    bt->button_ids  = malloc(bd->nrows * sizeof(uint32_t));
    bt->func_ids_sel = malloc(bd->nrows * sizeof(uint32_t));

    for (size_t i = 0; i < bd->nrows; i++) {
        bt->button_ids[i] = bd->rows[i].button_id;
        guint sel = gtk_drop_down_get_selected(bd->rows[i].func_drop);
        bt->func_ids_sel[i] = (sel < bd->nfunc_ids) ? bd->func_ids[sel] : 0;
    }

    gtk_widget_set_sensitive(GTK_WIDGET(bd->apply_btn), FALSE);
    gtk_label_set_text(bd->status, "Applying…");

    GTask *task = g_task_new(NULL, NULL, btns_done, bd);
    g_task_set_task_data(task, bt, btns_task_free);
    g_task_run_in_thread(task, btns_worker);
    g_object_unref(task);
}

GtkWidget *buttons_panel_new(razerd_t *r, const char *idstr)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(outer,    12);
    gtk_widget_set_margin_bottom(outer, 12);
    gtk_widget_set_margin_start(outer,  12);
    gtk_widget_set_margin_end(outer,    12);

    ButtonsData *bd = g_new0(ButtonsData, 1);
    bd->r = r;
    g_strlcpy(bd->idstr, idstr, sizeof(bd->idstr));

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid),    4);
    gtk_widget_set_margin_top(grid,   4);
    gtk_widget_set_margin_start(grid, 4);

    GtkWidget *h0 = gtk_label_new(NULL); gtk_label_set_markup(GTK_LABEL(h0), "<b>Button</b>");
    GtkWidget *h1 = gtk_label_new(NULL); gtk_label_set_markup(GTK_LABEL(h1), "<b>Function</b>");
    gtk_widget_set_halign(h0, GTK_ALIGN_START);
    gtk_widget_set_halign(h1, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), h0, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), h1, 1, 0, 1, 1);

    razerd_button_func_t *funcs = NULL;
    size_t fc = 0;
    GtkStringList *func_sl = gtk_string_list_new(NULL);
    if (razerd_get_button_functions(r, idstr, &funcs, &fc) == 0) {
        bd->func_ids  = malloc(fc * sizeof(uint32_t));
        bd->nfunc_ids = fc;
        for (size_t i = 0; i < fc; i++) {
            gtk_string_list_append(func_sl, funcs[i].name);
            bd->func_ids[i] = funcs[i].id;
        }
        razerd_free_button_functions(funcs);
    }

    razerd_button_t *btns = NULL;
    size_t bc = 0;
    if (razerd_get_buttons(r, idstr, &btns, &bc) == 0) {
        bd->rows  = calloc(bc, sizeof(ButtonRow));
        bd->nrows = bc;
        for (size_t i = 0; i < bc; i++) {
            ButtonRow *br = &bd->rows[i];
            br->button_id = btns[i].id;
            int gi = (int)i + 1;

            GtkWidget *name_lbl = gtk_label_new(btns[i].name);
            gtk_widget_set_halign(name_lbl, GTK_ALIGN_START);
            gtk_grid_attach(GTK_GRID(grid), name_lbl, 0, gi, 1, 1);

            GtkWidget *dd = gtk_drop_down_new(G_LIST_MODEL(func_sl), NULL);
            br->func_drop = GTK_DROP_DOWN(dd);
            gtk_widget_set_hexpand(dd, TRUE);

            razerd_button_func_t cur = {0};
            if (razerd_get_button_function(r, idstr, RAZERD_PROFILE_INVALID,
                                            btns[i].id, &cur) == 0) {
                for (size_t j = 0; j < bd->nfunc_ids; j++) {
                    if (bd->func_ids[j] == cur.id) {
                        gtk_drop_down_set_selected(br->func_drop, (guint)j);
                        break;
                    }
                }
            }
            gtk_grid_attach(GTK_GRID(grid), dd, 1, gi, 1, 1);
        }
        razerd_free_buttons(btns);
    }
    g_object_unref(func_sl);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), grid);
    gtk_box_append(GTK_BOX(outer), scroll);

    GtkWidget *apply = gtk_button_new_with_label("Apply");
    gtk_widget_set_halign(apply, GTK_ALIGN_START);
    bd->apply_btn = GTK_BUTTON(apply);
    gtk_box_append(GTK_BOX(outer), apply);

    GtkWidget *status = gtk_label_new("");
    gtk_widget_set_halign(status, GTK_ALIGN_START);
    bd->status = GTK_LABEL(status);
    gtk_box_append(GTK_BOX(outer), status);

    g_object_set_data_full(G_OBJECT(outer), "buttons-data", bd, buttons_data_free);
    g_signal_connect(apply, "clicked", G_CALLBACK(on_apply), bd);

    return outer;
}
