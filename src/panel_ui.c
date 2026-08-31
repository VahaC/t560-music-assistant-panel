#include "panel_ui.h"

struct _PanelUi {
    const AppConfig *config;
    PanelUiEventHandler event_handler;
    gpointer event_user_data;
    GtkWidget *stack;
    GtkWidget *status;
    GtkWidget *page_title;
    GtkWidget *album_art;
    GtkWidget *track_title;
    GtkWidget *artist;
    GtkWidget *progress;
    GtkWidget *position;
    GtkWidget *play;
    GtkWidget *play_icon;
    GtkWidget *shuffle;
    GtkWidget *repeat;
    GtkWidget *repeat_label;
    GtkWidget *volume;
    GtkWidget *queue_list;
    GtkWidget *playlist_list;
    gboolean changing_list_selection;
    GPtrArray *navigation_buttons;
    GtkWidget *room_buttons[PANEL_ROOM_COUNT];
    GtkWidget *room_states[PANEL_ROOM_COUNT];
};

enum {
    LIST_COLUMN_INDEX,
    LIST_COLUMN_TEXT,
    LIST_COLUMN_COUNT
};

static void add_css_class(GtkWidget *widget, const gchar *name)
{
    gtk_style_context_add_class(gtk_widget_get_style_context(widget), name);
}

static void toggle_css_class(GtkWidget *widget, const gchar *name,
                             gboolean enabled)
{
    GtkStyleContext *style = gtk_widget_get_style_context(widget);
    if (enabled)
        gtk_style_context_add_class(style, name);
    else
        gtk_style_context_remove_class(style, name);
}

static GtkWidget *new_label(const gchar *text, const gchar *css_class)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    if (css_class != NULL)
        add_css_class(label, css_class);
    return label;
}

static GtkWidget *new_button(const gchar *text, const gchar *css_class,
                             gint width, gint height)
{
    GtkWidget *button = gtk_button_new_with_label(text);
    gtk_widget_set_size_request(button, width, height);
    if (css_class != NULL)
        add_css_class(button, css_class);
    return button;
}

static GtkWidget *new_icon(const gchar *name, gint pixel_size)
{
    GtkWidget *icon = gtk_image_new_from_icon_name(name, GTK_ICON_SIZE_BUTTON);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), pixel_size);
    return icon;
}

static GtkWidget *new_icon_button(const gchar *icon_name, const gchar *text,
                                  const gchar *css_class, gint width,
                                  gint height, gint icon_size,
                                  GtkOrientation orientation,
                                  GtkWidget **icon_out,
                                  GtkWidget **label_out)
{
    GtkWidget *button = gtk_button_new();
    GtkWidget *content = gtk_box_new(orientation, orientation ==
                                                      GTK_ORIENTATION_HORIZONTAL
                                                  ? 9
                                                  : 3);
    GtkWidget *icon = new_icon(icon_name, icon_size);
    GtkWidget *label = text != NULL ? new_label(text, "button-label") : NULL;

    gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(content, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(content), icon, FALSE, FALSE, 0);
    if (label != NULL)
        gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(button), content);
    gtk_widget_set_size_request(button, width, height);
    add_css_class(button, "icon-button");
    if (css_class != NULL)
        add_css_class(button, css_class);
    if (icon_out != NULL)
        *icon_out = icon;
    if (label_out != NULL)
        *label_out = label;
    return button;
}

static void emit_event(PanelUi *ui, PanelUiEvent event, const gchar *value,
                       gint index)
{
    ui->event_handler(event, value, index, ui->event_user_data);
}

static void player_clicked(GtkButton *button, gpointer user_data)
{
    PanelUi *ui = user_data;
    emit_event(ui, PANEL_UI_PLAYER_SERVICE,
               g_object_get_data(G_OBJECT(button), "service"), -1);
}

static void shuffle_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    emit_event(user_data, PANEL_UI_TOGGLE_SHUFFLE, NULL, -1);
}

static void repeat_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    emit_event(user_data, PANEL_UI_CYCLE_REPEAT, NULL, -1);
}

static void room_clicked(GtkButton *button, gpointer user_data)
{
    emit_event(user_data, PANEL_UI_TOGGLE_ROOM, NULL,
               GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),
                                                 "room-index")));
}

static void page_clicked(GtkButton *button, gpointer user_data)
{
    PanelUi *ui = user_data;
    const gchar *page = g_object_get_data(G_OBJECT(button), "page");
    const gchar *title = g_object_get_data(G_OBJECT(button), "title");

    panel_ui_show_page(ui, page, title);
    emit_event(ui, PANEL_UI_SHOW_PAGE, page, -1);
}

static gint selected_list_index(GtkTreeSelection *selection)
{
    GtkTreeModel *model = NULL;
    GtkTreeIter iter;
    gint index = -1;

    if (gtk_tree_selection_get_selected(selection, &model, &iter))
        gtk_tree_model_get(model, &iter, LIST_COLUMN_INDEX, &index, -1);
    return index;
}

static void queue_selection_changed(GtkTreeSelection *selection,
                                    gpointer user_data)
{
    PanelUi *ui = user_data;
    if (!ui->changing_list_selection)
        emit_event(ui, PANEL_UI_SELECT_QUEUE_ITEM, NULL,
                   selected_list_index(selection));
}

static void playlist_selection_changed(GtkTreeSelection *selection,
                                       gpointer user_data)
{
    PanelUi *ui = user_data;
    if (!ui->changing_list_selection)
        emit_event(ui, PANEL_UI_SELECT_PLAYLIST, NULL,
                   selected_list_index(selection));
}

static void play_queue_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    emit_event(user_data, PANEL_UI_PLAY_QUEUE_ITEM, NULL, -1);
}

static void play_playlist_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    emit_event(user_data, PANEL_UI_PLAY_PLAYLIST, NULL, -1);
}

static void select_row(GtkWidget *list, gint selected)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(
        GTK_TREE_VIEW(list));
    gtk_tree_selection_unselect_all(selection);
    if (selected >= 0) {
        GtkTreePath *path = gtk_tree_path_new_from_indices(selected, -1);
        gtk_tree_selection_select_path(selection, path);
        gtk_tree_path_free(path);
    }
}

static GtkWidget *new_list(PanelUi *ui, gboolean queue)
{
    GtkListStore *store = gtk_list_store_new(
        LIST_COLUMN_COUNT, G_TYPE_INT, G_TYPE_STRING);
    GtkWidget *list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(list), FALSE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(list), FALSE);
    gtk_tree_view_set_fixed_height_mode(GTK_TREE_VIEW(list), TRUE);
    add_css_class(list, "list-view");

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    gtk_cell_renderer_set_fixed_size(renderer, -1, queue ? 90 : 82);
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
        "", renderer, "text", LIST_COLUMN_TEXT, NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);

    GtkTreeSelection *selection = gtk_tree_view_get_selection(
        GTK_TREE_VIEW(list));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(selection, "changed",
                     G_CALLBACK(queue ? queue_selection_changed
                                      : playlist_selection_changed),
                     ui);
    return list;
}

static gchar *format_time(gdouble seconds)
{
    gint total = MAX(0, (gint)seconds);
    return g_strdup_printf("%d:%02d", total / 60, total % 60);
}

static GtkWidget *navigation_button(PanelUi *ui, const gchar *icon,
                                    const gchar *text, const gchar *page,
                                    const gchar *title)
{
    GtkWidget *button = new_icon_button(
        icon, text, "nav-button", 150, 72, 23, GTK_ORIENTATION_VERTICAL,
        NULL, NULL);
    g_object_set_data(G_OBJECT(button), "page", (gpointer)page);
    g_object_set_data(G_OBJECT(button), "title", (gpointer)title);
    g_signal_connect(button, "clicked", G_CALLBACK(page_clicked), ui);
    g_ptr_array_add(ui->navigation_buttons, button);
    toggle_css_class(button, "active", g_str_equal(page, "player"));
    return button;
}

static GtkWidget *navigation(PanelUi *ui)
{
    GtkWidget *navigation_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(navigation_box, GTK_ALIGN_CENTER);
    add_css_class(navigation_box, "navigation-bar");
    gtk_box_pack_start(GTK_BOX(navigation_box),
                       navigation_button(ui, "audio-x-generic-symbolic",
                                         "Player", "player", "NOW PLAYING"),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(navigation_box),
                       navigation_button(ui, "view-list-details-symbolic",
                                         "Queue", "queue", "QUEUE"),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(
        GTK_BOX(navigation_box),
        navigation_button(ui, "view-list-icons-symbolic", "Playlists",
                          "playlists", "PLAYLISTS"),
        FALSE, FALSE, 0);
    gtk_box_pack_start(
        GTK_BOX(navigation_box),
        navigation_button(ui, "computer-symbolic", "Room", "room",
                          "ROOM CONTROLS"),
        FALSE, FALSE, 0);
    return navigation_box;
}

static GtkWidget *player_page(PanelUi *ui)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 11);
    gtk_widget_set_margin_start(page, 24);
    gtk_widget_set_margin_end(page, 24);
    gtk_widget_set_margin_top(page, 14);
    gtk_widget_set_margin_bottom(page, 10);
    add_css_class(page, "player-page");

    ui->album_art = gtk_image_new_from_icon_name(
        "audio-x-generic-symbolic", GTK_ICON_SIZE_DIALOG);
    gtk_widget_set_size_request(ui->album_art, 510, 510);
    GtkWidget *artwork = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(artwork), GTK_SHADOW_NONE);
    gtk_widget_set_halign(artwork, GTK_ALIGN_CENTER);
    add_css_class(artwork, "artwork-card");
    gtk_container_add(GTK_CONTAINER(artwork), ui->album_art);

    GtkWidget *track_details = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    add_css_class(track_details, "track-details");
    ui->track_title = new_label("Nothing playing", "track-title");
    ui->artist = new_label("", "artist");
    gtk_box_pack_start(GTK_BOX(track_details), ui->track_title,
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(track_details), ui->artist,
                       FALSE, FALSE, 0);

    GtkWidget *timeline = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(timeline, 34);
    gtk_widget_set_margin_end(timeline, 34);
    ui->progress = gtk_progress_bar_new();
    gtk_widget_set_size_request(ui->progress, -1, 12);
    ui->position = new_label("0:00  /  0:00", "position");
    gtk_box_pack_start(GTK_BOX(timeline), ui->progress, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(timeline), ui->position, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(page), artwork, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), track_details, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), timeline, FALSE, FALSE, 0);

    GtkWidget *modes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(modes, GTK_ALIGN_CENTER);
    ui->shuffle = new_icon_button(
        "media-playlist-shuffle-symbolic", "Shuffle", "mode-button",
        190, 58, 23, GTK_ORIENTATION_HORIZONTAL, NULL, NULL);
    ui->repeat = new_icon_button(
        "media-playlist-repeat-symbolic", "Repeat off", "mode-button",
        190, 58, 23, GTK_ORIENTATION_HORIZONTAL, NULL, &ui->repeat_label);
    g_signal_connect(ui->shuffle, "clicked", G_CALLBACK(shuffle_clicked), ui);
    g_signal_connect(ui->repeat, "clicked", G_CALLBACK(repeat_clicked), ui);
    gtk_box_pack_start(GTK_BOX(modes), ui->shuffle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(modes), ui->repeat, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), modes, FALSE, FALSE, 0);

    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_widget_set_halign(controls, GTK_ALIGN_CENTER);
    GtkWidget *previous = new_icon_button(
        "media-skip-backward-symbolic", NULL, "transport-button",
        108, 86, 38, GTK_ORIENTATION_VERTICAL, NULL, NULL);
    ui->play = new_icon_button(
        "media-playback-start-symbolic", NULL, "play-button", 120, 104,
        48, GTK_ORIENTATION_VERTICAL, &ui->play_icon, NULL);
    GtkWidget *next = new_icon_button(
        "media-skip-forward-symbolic", NULL, "transport-button",
        108, 86, 38, GTK_ORIENTATION_VERTICAL, NULL, NULL);
    gtk_widget_set_tooltip_text(previous, "Previous track");
    gtk_widget_set_tooltip_text(ui->play, "Play or pause");
    gtk_widget_set_tooltip_text(next, "Next track");
    g_object_set_data(G_OBJECT(previous), "service", "media_previous_track");
    g_object_set_data(G_OBJECT(ui->play), "service", "media_play_pause");
    g_object_set_data(G_OBJECT(next), "service", "media_next_track");
    g_signal_connect(previous, "clicked", G_CALLBACK(player_clicked), ui);
    g_signal_connect(ui->play, "clicked", G_CALLBACK(player_clicked), ui);
    g_signal_connect(next, "clicked", G_CALLBACK(player_clicked), ui);
    gtk_box_pack_start(GTK_BOX(controls), previous, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), ui->play, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), next, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), controls, FALSE, FALSE, 0);

    GtkWidget *volume_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(volume_controls, GTK_ALIGN_CENTER);
    add_css_class(volume_controls, "volume-card");
    GtkWidget *down = new_icon_button(
        "audio-volume-low-symbolic", NULL, "volume-button", 86, 62, 28,
        GTK_ORIENTATION_VERTICAL, NULL, NULL);
    GtkWidget *volume_readout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(volume_readout, 180, -1);
    gtk_box_pack_start(GTK_BOX(volume_readout),
                       new_label("VOLUME", "volume-caption"),
                       FALSE, FALSE, 0);
    ui->volume = new_label("--", "volume");
    gtk_box_pack_start(GTK_BOX(volume_readout), ui->volume,
                       FALSE, FALSE, 0);
    GtkWidget *up = new_icon_button(
        "audio-volume-high-symbolic", NULL, "volume-button", 86, 62, 28,
        GTK_ORIENTATION_VERTICAL, NULL, NULL);
    gtk_widget_set_tooltip_text(down, "Volume down");
    gtk_widget_set_tooltip_text(up, "Volume up");
    g_object_set_data(G_OBJECT(down), "service", "volume_down");
    g_object_set_data(G_OBJECT(up), "service", "volume_up");
    g_signal_connect(down, "clicked", G_CALLBACK(player_clicked), ui);
    g_signal_connect(up, "clicked", G_CALLBACK(player_clicked), ui);
    gtk_box_pack_start(GTK_BOX(volume_controls), down, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(volume_controls), volume_readout,
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(volume_controls), up, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), volume_controls, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(page), navigation(ui), FALSE, FALSE, 4);
    return page;
}

static GtkWidget *list_page(PanelUi *ui, gboolean queue)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(page, 16);
    gtk_widget_set_margin_end(page, 16);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_kinetic_scrolling(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_capture_button_press(GTK_SCROLLED_WINDOW(scroll),
                                                  TRUE);
    GtkWidget *list = new_list(ui, queue);
    gtk_container_add(GTK_CONTAINER(scroll), list);
    GtkWidget *play = new_button(queue ? "PLAY SELECTED TRACK"
                                       : "PLAY SELECTED PLAYLIST",
                                 "play-selected", -1, 80);
    if (queue) {
        ui->queue_list = list;
        g_signal_connect(play, "clicked", G_CALLBACK(play_queue_clicked), ui);
    } else {
        ui->playlist_list = list;
        g_signal_connect(play, "clicked", G_CALLBACK(play_playlist_clicked), ui);
    }
    gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), play, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), navigation(ui), FALSE, FALSE, 4);
    return page;
}

static GtkWidget *room_page(PanelUi *ui)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(page, 20);
    gtk_widget_set_margin_end(page, 20);
    gtk_box_pack_start(GTK_BOX(page),
                       new_label("Tap a control to toggle", "room-help"),
                       FALSE, FALSE, 0);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 18);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 18);
    gtk_widget_set_vexpand(grid, TRUE);
    for (guint i = 0; i < PANEL_ROOM_COUNT; i++) {
        GtkWidget *button = gtk_button_new();
        gtk_widget_set_hexpand(button, TRUE);
        gtk_widget_set_vexpand(button, TRUE);
        gtk_widget_set_size_request(button, 350, 350);
        add_css_class(button, "room-card");

        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
        ui->room_states[i] = new_label(
            ui->config->room_entities[i] != NULL ? "--" : "NOT CONFIGURED",
            "room-state");
        gtk_box_pack_start(GTK_BOX(box),
                           new_label(ui->config->room_labels[i], "room-name"),
                           TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(box), ui->room_states[i], TRUE, TRUE, 0);
        gtk_container_add(GTK_CONTAINER(button), box);
        g_object_set_data(G_OBJECT(button), "room-index", GINT_TO_POINTER(i));
        g_signal_connect(button, "clicked", G_CALLBACK(room_clicked), ui);
        gtk_widget_set_sensitive(button, ui->config->room_entities[i] != NULL);
        ui->room_buttons[i] = button;
        gtk_grid_attach(GTK_GRID(grid), button, i % 2, i / 2, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(page), grid, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), navigation(ui), FALSE, FALSE, 4);
    return page;
}

PanelUi *panel_ui_new(const AppConfig *config, PanelUiEventHandler handler,
                      gpointer user_data)
{
    g_return_val_if_fail(config != NULL, NULL);
    g_return_val_if_fail(handler != NULL, NULL);

    PanelUi *ui = g_new0(PanelUi, 1);
    ui->config = config;
    ui->event_handler = handler;
    ui->event_user_data = user_data;
    ui->navigation_buttons = g_ptr_array_new();
    return ui;
}

void panel_ui_free(PanelUi *ui)
{
    g_ptr_array_unref(ui->navigation_buttons);
    g_free(ui);
}

GtkWidget *panel_ui_build(PanelUi *ui)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    add_css_class(header, "header");
    gtk_widget_set_size_request(header, -1, 70);

    ui->page_title = new_label("NOW PLAYING", "header-title");
    gtk_widget_set_halign(ui->page_title, GTK_ALIGN_START);
    gtk_widget_set_hexpand(ui->page_title, TRUE);
    ui->status = new_label("Connecting", "status");
    gtk_box_pack_start(GTK_BOX(header), ui->page_title, TRUE, TRUE, 18);
    gtk_box_pack_start(GTK_BOX(header), ui->status, FALSE, FALSE, 18);

    ui->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(ui->stack),
                                  GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_stack_add_named(GTK_STACK(ui->stack), player_page(ui), "player");
    gtk_stack_add_named(GTK_STACK(ui->stack), list_page(ui, TRUE), "queue");
    gtk_stack_add_named(GTK_STACK(ui->stack), list_page(ui, FALSE), "playlists");
    gtk_stack_add_named(GTK_STACK(ui->stack), room_page(ui), "room");
    gtk_box_pack_start(GTK_BOX(outer), header, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), ui->stack, TRUE, TRUE, 0);
    return outer;
}

GtkWidget *panel_ui_build_config_error(const gchar *message)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(box, 60);
    gtk_widget_set_margin_end(box, 60);
    GtkWidget *error = new_label(message, "config-error");
    gtk_label_set_line_wrap(GTK_LABEL(error), TRUE);
    gtk_box_pack_start(GTK_BOX(box),
                       new_label("T560 Music Panel", "setup-title"),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), error, FALSE, FALSE, 0);
    return box;
}

void panel_ui_install_styles(void)
{
    static const gchar css[] =
        "*{font-family:Sans;color:#edf4ff}"
        "window{background:#070c14;color:#edf4ff}"
        ".header{background:#0c1420;border-bottom:1px solid #1d2b3f;box-shadow:0 5px 18px rgba(0,0,0,.28)}"
        ".header-title{font-size:24px;font-weight:700;color:#f7faff}"
        ".status{font-size:14px;font-weight:700;color:#56e5dc;background:#102b31;border:1px solid #1d5255;border-radius:18px;padding:7px 13px}"
        ".status.error,.error{color:#ff8a94;background:#32171e;border-color:#68303b}"
        "button{background-image:linear-gradient(to bottom,#182438,#111a29);color:#e8f0fb;border:1px solid #2b3c55;border-radius:18px;box-shadow:0 5px 14px rgba(0,0,0,.24)}"
        "button:hover{background-image:linear-gradient(to bottom,#1d2c43,#152138);border-color:#3b526f}"
        "button:active{background:#20334d;box-shadow:none}"
        "button.active{background:#12373c;border-color:#43d8d0;color:#75f1e9;box-shadow:0 0 0 1px rgba(67,216,208,.18)}"
        ".icon-button image{-gtk-icon-shadow:none}"
        ".player-page{background-image:linear-gradient(to bottom,#0a111c,#070c14)}"
        ".artwork-card{background:#101a29;border:1px solid #25364e;border-radius:28px;padding:10px;box-shadow:0 14px 34px rgba(0,0,0,.42)}"
        ".track-details{padding:2px 12px}"
        ".track-title{font-size:30px;font-weight:700;color:#f8fbff}"
        ".artist{font-size:20px;color:#8fa9c7}"
        ".position{font-size:14px;font-weight:600;color:#778ba5}"
        "progressbar trough{min-height:8px;background:#172235;border-radius:6px}"
        "progressbar progress{min-height:8px;background-image:linear-gradient(to right,#31c8da,#55e4c5);border-radius:6px;box-shadow:0 0 8px rgba(49,200,218,.35)}"
        ".mode-button{font-size:16px;font-weight:700;border-radius:20px;box-shadow:none}"
        ".transport-button{background:#121d2d;border-color:#2a3c55;border-radius:28px;box-shadow:0 7px 18px rgba(0,0,0,.28)}"
        ".play-button{background-image:linear-gradient(to bottom,#5ce4d7,#2fc8d6);color:#061418;border:0;border-radius:36px;box-shadow:0 10px 28px rgba(47,200,214,.32)}"
        ".play-button:hover{background-image:linear-gradient(to bottom,#72eee3,#43d7e2)}"
        ".play-button:active{background:#27b3c1;box-shadow:0 4px 10px rgba(47,200,214,.24)}"
        ".volume-card{background:#0e1826;border:1px solid #22334a;border-radius:25px;padding:6px 12px}"
        ".volume-button{background:transparent;border:0;box-shadow:none;color:#91aac8}"
        ".volume-button:active{background:#192b41;color:#56e5dc}"
        ".volume-caption{font-size:11px;font-weight:700;color:#647a96}"
        ".volume{font-size:22px;font-weight:700;color:#dceaff}"
        ".navigation-bar{background:#0d1623;border:1px solid #1e2d42;border-radius:25px;padding:5px;box-shadow:0 8px 22px rgba(0,0,0,.3)}"
        ".nav-button{font-size:13px;font-weight:700;background:transparent;border:0;border-radius:19px;box-shadow:none;color:#7188a5}"
        ".nav-button.active{background:#152e3a;border:0;color:#5ee1d8;box-shadow:none}"
        ".list-view{font-size:20px;background:#070c14;color:#edf4ff}"
        ".list-view:selected{background:#12373c;color:#75f1e9}"
        ".play-selected{font-size:20px;font-weight:700;border-color:#43d8d0}"
        ".room-help{font-size:18px;color:#778ba5}"
        ".room-card{background:#101a29;border:1px solid #2a3b54;border-radius:24px;box-shadow:0 8px 22px rgba(0,0,0,.28)}"
        ".room-card.active{background:#12373c;border-color:#43d8d0}"
        ".room-name{font-size:28px;font-weight:700}"
        ".room-state{font-size:21px;color:#8fa9c7}"
        ".setup-title{font-size:38px;font-weight:700;color:#56e5dc}"
        ".config-error{font-size:21px}";
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

void panel_ui_set_status(PanelUi *ui, const gchar *text, gboolean is_error)
{
    gtk_label_set_text(GTK_LABEL(ui->status), text);
    toggle_css_class(ui->status, "error", is_error);
}

void panel_ui_set_player(PanelUi *ui, gboolean playing, const gchar *title,
                         const gchar *artist, gdouble position,
                         gdouble duration, gdouble volume, gboolean shuffle,
                         const gchar *repeat)
{
    gtk_image_set_from_icon_name(
        GTK_IMAGE(ui->play_icon),
        playing ? "media-playback-pause-symbolic"
                : "media-playback-start-symbolic",
        GTK_ICON_SIZE_BUTTON);
    gtk_image_set_pixel_size(GTK_IMAGE(ui->play_icon), 48);
    gtk_label_set_text(GTK_LABEL(ui->track_title), title);
    gtk_label_set_text(GTK_LABEL(ui->artist), artist);
    gtk_progress_bar_set_fraction(
        GTK_PROGRESS_BAR(ui->progress),
        duration > 0.0 ? CLAMP(position / duration, 0.0, 1.0) : 0.0);

    gchar *position_text = format_time(position);
    gchar *duration_text = format_time(duration);
    gchar *timeline = g_strdup_printf("%s  /  %s", position_text, duration_text);
    gtk_label_set_text(GTK_LABEL(ui->position), timeline);
    g_free(position_text);
    g_free(duration_text);
    g_free(timeline);

    gchar *volume_text = g_strdup_printf("%.0f%%", volume * 100.0);
    gtk_label_set_text(GTK_LABEL(ui->volume), volume_text);
    g_free(volume_text);

    panel_ui_set_modes(ui, shuffle, repeat);
}

void panel_ui_set_modes(PanelUi *ui, gboolean shuffle, const gchar *repeat)
{
    toggle_css_class(ui->shuffle, "active", shuffle);
    const gchar *repeat_state = g_str_equal(repeat, "all") ? "all"
                                : g_str_equal(repeat, "one") ? "one"
                                                               : "off";
    gchar *repeat_text = g_strdup_printf("Repeat %s", repeat_state);
    gtk_label_set_text(GTK_LABEL(ui->repeat_label), repeat_text);
    toggle_css_class(ui->repeat, "active", !g_str_equal(repeat, "off"));
    g_free(repeat_text);
}

void panel_ui_set_album_art(PanelUi *ui, GdkPixbuf *pixbuf)
{
    gtk_image_set_from_pixbuf(GTK_IMAGE(ui->album_art), pixbuf);
}

void panel_ui_set_queue(PanelUi *ui, GPtrArray *titles, GPtrArray *artists,
                        guint count, gint selected)
{
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(
        GTK_TREE_VIEW(ui->queue_list)));
    ui->changing_list_selection = TRUE;
    gtk_list_store_clear(store);
    for (guint i = 0; i < count; i++) {
        const gchar *title = g_ptr_array_index(titles, i);
        const gchar *artist = g_ptr_array_index(artists, i);
        gchar *text = *artist != '\0' ? g_strdup_printf("%s\n%s", title, artist)
                                      : g_strdup(title);
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
                           LIST_COLUMN_INDEX, (gint)i,
                           LIST_COLUMN_TEXT, text, -1);
        g_free(text);
    }
    select_row(ui->queue_list, selected);
    ui->changing_list_selection = FALSE;
}

void panel_ui_set_playlists(PanelUi *ui, GPtrArray *names, guint count,
                            gint selected)
{
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(
        GTK_TREE_VIEW(ui->playlist_list)));
    ui->changing_list_selection = TRUE;
    gtk_list_store_clear(store);
    for (guint i = 0; i < count; i++) {
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
                           LIST_COLUMN_INDEX, (gint)i,
                           LIST_COLUMN_TEXT, g_ptr_array_index(names, i), -1);
    }
    select_row(ui->playlist_list, selected);
    ui->changing_list_selection = FALSE;
}

void panel_ui_select_queue_item(PanelUi *ui, gint selected)
{
    ui->changing_list_selection = TRUE;
    select_row(ui->queue_list, selected);
    ui->changing_list_selection = FALSE;
}

void panel_ui_select_playlist(PanelUi *ui, gint selected)
{
    ui->changing_list_selection = TRUE;
    select_row(ui->playlist_list, selected);
    ui->changing_list_selection = FALSE;
}

void panel_ui_set_room(PanelUi *ui, guint index, gboolean active)
{
    g_return_if_fail(index < PANEL_ROOM_COUNT);
    gtk_label_set_text(GTK_LABEL(ui->room_states[index]), active ? "ON" : "OFF");
    toggle_css_class(ui->room_buttons[index], "active", active);
}

void panel_ui_show_page(PanelUi *ui, const gchar *page, const gchar *title)
{
    gtk_stack_set_visible_child_name(GTK_STACK(ui->stack), page);
    gtk_label_set_text(GTK_LABEL(ui->page_title), title);
    for (guint i = 0; i < ui->navigation_buttons->len; i++) {
        GtkWidget *button = g_ptr_array_index(ui->navigation_buttons, i);
        const gchar *button_page = g_object_get_data(
            G_OBJECT(button), "page");
        toggle_css_class(button, "active", g_str_equal(button_page, page));
    }
}
