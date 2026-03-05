#include "ledpanel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *mode_names[] = {
    "Static", "Spectrum", "Breathing", "Wave", "Reaction"
};

typedef struct {
    razerd_led_t    led;        /* working copy */
    GtkCheckButton *state_btn;
    GtkWidget      *color_btn;  /* GtkColorDialogButton or NULL */
    GtkDropDown    *mode_drop;
    uint32_t       *mode_ids;
    size_t          nmode_ids;
} LedRow;

typedef struct {
    razerd_t *r;
    char      idstr[64];
    LedRow   *rows;
    size_t    nrows;
    GtkLabel *status;
} LedPanel;

static void led_panel_free(gpointer p)
{
    LedPanel *lp = p;
    for (size_t i = 0; i < lp->nrows; i++)
        free(lp->rows[i].mode_ids);
    free(lp->rows);
    g_free(lp);
}

static void on_apply(GtkButton *btn, gpointer data)
{
    (void)btn;
    LedPanel *lp = data;
    int errors = 0;

    for (size_t i = 0; i < lp->nrows; i++) {
        LedRow *lr = &lp->rows[i];
        lr->led.state = gtk_check_button_get_active(lr->state_btn) ? 1u : 0u;

        if (lr->color_btn) {
            const GdkRGBA *rgba =
                gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(lr->color_btn));
            lr->led.r = (uint8_t)(rgba->red   * 255.0 + 0.5);
            lr->led.g = (uint8_t)(rgba->green * 255.0 + 0.5);
            lr->led.b = (uint8_t)(rgba->blue  * 255.0 + 0.5);
        }

        guint sel = gtk_drop_down_get_selected(lr->mode_drop);
        if (sel != GTK_INVALID_LIST_POSITION && sel < lr->nmode_ids)
            lr->led.mode = lr->mode_ids[sel];

        int err = razerd_set_led(lp->r, lp->idstr, RAZERD_PROFILE_INVALID, &lr->led);
        if (err) errors++;
    }

    char buf[64];
    if (errors)
        snprintf(buf, sizeof(buf), "%d LED(s) failed to apply.", errors);
    else
        g_strlcpy(buf, "LEDs applied.", sizeof(buf));
    gtk_label_set_text(lp->status, buf);
}

GtkWidget *led_panel_new(razerd_t *r, const char *idstr)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(outer,    12);
    gtk_widget_set_margin_bottom(outer, 12);
    gtk_widget_set_margin_start(outer,  12);
    gtk_widget_set_margin_end(outer,    12);

    LedPanel *lp = g_new0(LedPanel, 1);
    lp->r = r;
    g_strlcpy(lp->idstr, idstr, sizeof(lp->idstr));

    /* Scrollable grid */
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_widget_set_margin_top(grid,   4);
    gtk_widget_set_margin_start(grid, 4);

    /* Header row */
    GtkWidget *h0 = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(h0), "<b>LED</b>");
    gtk_widget_set_halign(h0, GTK_ALIGN_START);
    GtkWidget *h1 = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(h1), "<b>On</b>");
    GtkWidget *h2 = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(h2), "<b>Color</b>");
    GtkWidget *h3 = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(h3), "<b>Mode</b>");
    gtk_grid_attach(GTK_GRID(grid), h0, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), h1, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), h2, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), h3, 3, 0, 1, 1);

    /* Load LEDs */
    razerd_led_t *leds = NULL;
    size_t lc = 0;
    if (razerd_get_leds(r, idstr, RAZERD_PROFILE_INVALID, &leds, &lc) == 0) {
        lp->rows  = calloc(lc, sizeof(LedRow));
        lp->nrows = lc;

        for (size_t i = 0; i < lc; i++) {
            LedRow *lr = &lp->rows[i];
            lr->led = leds[i];
            int gi = (int)i + 1;

            /* Name */
            GtkWidget *name_lbl = gtk_label_new(leds[i].name);
            gtk_widget_set_halign(name_lbl, GTK_ALIGN_START);
            gtk_grid_attach(GTK_GRID(grid), name_lbl, 0, gi, 1, 1);

            /* State checkbox */
            lr->state_btn = GTK_CHECK_BUTTON(gtk_check_button_new());
            gtk_check_button_set_active(lr->state_btn, leds[i].state != 0);
            gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(lr->state_btn), 1, gi, 1, 1);

            /* Color button */
            if (leds[i].can_change_color) {
                GtkColorDialog *dlg = gtk_color_dialog_new();
                lr->color_btn = gtk_color_dialog_button_new(dlg);
                g_object_unref(dlg);
                GdkRGBA rgba = {
                    .red   = leds[i].r / 255.0,
                    .green = leds[i].g / 255.0,
                    .blue  = leds[i].b / 255.0,
                    .alpha = 1.0,
                };
                gtk_color_dialog_button_set_rgba(
                    GTK_COLOR_DIALOG_BUTTON(lr->color_btn), &rgba);
                gtk_grid_attach(GTK_GRID(grid), lr->color_btn, 2, gi, 1, 1);
            } else {
                GtkWidget *dash = gtk_label_new("—");
                gtk_grid_attach(GTK_GRID(grid), dash, 2, gi, 1, 1);
            }

            /* Mode dropdown */
            GtkStringList *sl = gtk_string_list_new(NULL);
            lr->mode_ids  = malloc(5 * sizeof(uint32_t));
            lr->nmode_ids = 0;
            guint cur_idx = 0;
            for (int m = 0; m < 5; m++) {
                if (leds[i].supported_modes & (1u << (unsigned)m)) {
                    gtk_string_list_append(sl, mode_names[m]);
                    lr->mode_ids[lr->nmode_ids] = (uint32_t)m;
                    if ((uint32_t)m == leds[i].mode)
                        cur_idx = (guint)lr->nmode_ids;
                    lr->nmode_ids++;
                }
            }
            GtkWidget *dd = gtk_drop_down_new(G_LIST_MODEL(sl), NULL);
            lr->mode_drop = GTK_DROP_DOWN(dd);
            gtk_drop_down_set_selected(lr->mode_drop, cur_idx);
            gtk_grid_attach(GTK_GRID(grid), dd, 3, gi, 1, 1);
        }
        razerd_free_leds(leds);
    }

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), grid);
    gtk_box_append(GTK_BOX(outer), scroll);

    /* Apply button */
    GtkWidget *btn = gtk_button_new_with_label("Apply");
    gtk_widget_set_halign(btn, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(outer), btn);

    GtkWidget *status = gtk_label_new("");
    gtk_widget_set_halign(status, GTK_ALIGN_START);
    lp->status = GTK_LABEL(status);
    gtk_box_append(GTK_BOX(outer), status);

    g_object_set_data_full(G_OBJECT(outer), "led-data", lp, led_panel_free);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_apply), lp);

    return outer;
}
