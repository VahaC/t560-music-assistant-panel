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
    GtkWidget *shuffle;
    GtkWidget *repeat;
    GtkWidget *volume;
    GtkWidget *queue_list;
    GtkWidget *playlist_list;
    GtkWidget *room_buttons[PANEL_ROOM_COUNT];
    GtkWidget *room_states[PANEL_ROOM_COUNT];
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

static void queue_row_clicked(GtkButton *button, gpointer user_data)
{
    PanelUi *ui = user_data;
    gint index = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(button), "index"));
    panel_ui_select_queue_item(ui, index);
    emit_event(ui, PANEL_UI_SELECT_QUEUE_ITEM, NULL, index);
}

static void playlist_row_clicked(GtkButton *button, gpointer user_data)
{
    PanelUi *ui = user_data;
    gint index = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(button), "index"));
    panel_ui_select_playlist(ui, index);
    emit_event(ui, PANEL_UI_SELECT_PLAYLIST, NULL, index);
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
    GList *rows = gtk_container_get_children(GTK_CONTAINER(list));
    for (GList *item = rows; item != NULL; item = item->next) {
        GtkWidget *row = GTK_WIDGET(item->data);
        gint index = GPOINTER_TO_INT(
            g_object_get_data(G_OBJECT(row), "index"));
        toggle_css_class(row, "selected", index == selected);
    }
    g_list_free(rows);
}

static void clear_container(GtkWidget *container)
{
    GList *children = gtk_container_get_children(GTK_CONTAINER(container));
    for (GList *item = children; item != NULL; item = item->next)
        gtk_widget_destroy(GTK_WIDGET(item->data));
    g_list_free(children);
}

static gchar *format_time(gdouble seconds)
{
    gint total = MAX(0, (gint)seconds);
    return g_strdup_printf("%d:%02d", total / 60, total % 60);
}

static GtkWidget *navigation_button(PanelUi *ui, const gchar *text,
                                    const gchar *page, const gchar *title)
{
    GtkWidget *button = new_button(text, "nav-button", 150, 68);
    g_object_set_data(G_OBJECT(button), "page", (gpointer)page);
    g_object_set_data(G_OBJECT(button), "title", (gpointer)title);
    g_signal_connect(button, "clicked", G_CALLBACK(page_clicked), ui);
    return button;
}

static GtkWidget *navigation(PanelUi *ui)
{
    GtkWidget *navigation_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(navigation_box, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(navigation_box),
                       navigation_button(ui, "PLAYER", "player", "PLAYER"),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(navigation_box),
                       navigation_button(ui, "QUEUE", "queue", "QUEUE"),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(
        GTK_BOX(navigation_box),
        navigation_button(ui, "PLAYLISTS", "playlists", "PLAYLISTS"),
        FALSE, FALSE, 0);
    gtk_box_pack_start(
        GTK_BOX(navigation_box),
        navigation_button(ui, "ROOM", "room", "ROOM CONTROLS"),
        FALSE, FALSE, 0);
    return navigation_box;
}

static GtkWidget *player_page(PanelUi *ui)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 9);
    gtk_widget_set_margin_start(page, 18);
    gtk_widget_set_margin_end(page, 18);

    ui->album_art = gtk_image_new_from_icon_name(
        "audio-x-generic", GTK_ICON_SIZE_DIALOG);
    gtk_widget_set_size_request(ui->album_art, 510, 510);
    ui->track_title = new_label("Nothing playing", "track-title");
    ui->artist = new_label("", "artist");
    ui->progress = gtk_progress_bar_new();
    gtk_widget_set_size_request(ui->progress, -1, 20);
    ui->position = new_label("0:00  /  0:00", "position");
    gtk_box_pack_start(GTK_BOX(page), ui->album_art, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), ui->track_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), ui->artist, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), ui->progress, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), ui->position, FALSE, FALSE, 0);

    GtkWidget *modes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    ui->shuffle = new_button("SHUFFLE", "mode-button", 180, 60);
    ui->repeat = new_button("REPEAT OFF", "mode-button", 180, 60);
    g_signal_connect(ui->shuffle, "clicked", G_CALLBACK(shuffle_clicked), ui);
    g_signal_connect(ui->repeat, "clicked", G_CALLBACK(repeat_clicked), ui);
    gtk_box_pack_start(GTK_BOX(modes), ui->shuffle, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(modes), ui->repeat, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), modes, FALSE, FALSE, 0);

    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *previous = new_button("PREVIOUS", "control-button", 180, 86);
    ui->play = new_button("PLAY", "play-button", 210, 96);
    GtkWidget *next = new_button("NEXT", "control-button", 180, 86);
    g_object_set_data(G_OBJECT(previous), "service", "media_previous_track");
    g_object_set_data(G_OBJECT(ui->play), "service", "media_play_pause");
    g_object_set_data(G_OBJECT(next), "service", "media_next_track");
    g_signal_connect(previous, "clicked", G_CALLBACK(player_clicked), ui);
    g_signal_connect(ui->play, "clicked", G_CALLBACK(player_clicked), ui);
    g_signal_connect(next, "clicked", G_CALLBACK(player_clicked), ui);
    gtk_box_pack_start(GTK_BOX(controls), previous, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(controls), ui->play, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(controls), next, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), controls, FALSE, FALSE, 0);

    GtkWidget *volume_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *down = new_button("VOL -", "control-button", 180, 68);
    ui->volume = new_label("VOLUME --", "volume");
    GtkWidget *up = new_button("VOL +", "control-button", 180, 68);
    g_object_set_data(G_OBJECT(down), "service", "volume_down");
    g_object_set_data(G_OBJECT(up), "service", "volume_up");
    g_signal_connect(down, "clicked", G_CALLBACK(player_clicked), ui);
    g_signal_connect(up, "clicked", G_CALLBACK(player_clicked), ui);
    gtk_box_pack_start(GTK_BOX(volume_controls), down, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(volume_controls), ui->volume, TRUE, TRUE, 0);
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
    GtkWidget *list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
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
    return ui;
}

void panel_ui_free(PanelUi *ui)
{
    g_free(ui);
}

GtkWidget *panel_ui_build(PanelUi *ui)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    add_css_class(header, "header");
    gtk_widget_set_size_request(header, -1, 62);

    ui->page_title = new_label("PLAYER", "header-title");
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
        "*{font-family:Sans}window{background:#090b11;color:#f5f7ff}"
        ".header{background:#101521;border-bottom:1px solid #263248}"
        ".header-title{font-size:25px;font-weight:bold}.status{font-size:16px;color:#00cfff}.error{color:#ff6b72}"
        "button{background:#141927;color:#f4f4ff;border:2px solid #334466;border-radius:14px;box-shadow:none}"
        "button:active{background:#24344e}button.active,.list-row.selected{background:#123d45;border-color:#00cfff;color:#7deaff}"
        ".track-title{font-size:31px;font-weight:bold}.artist{font-size:23px;color:#6ca8df}.position{font-size:17px;color:#7f8ca5}"
        "progressbar trough{min-height:14px;background:#1a1a35;border-radius:8px}progressbar progress{min-height:14px;background:#00cfff;border-radius:8px}"
        ".control-button,.mode-button{font-size:18px;font-weight:bold}.play-button{font-size:24px;font-weight:bold;border-color:#00cfff}"
        ".volume{font-size:20px;font-weight:bold;color:#78a7d6}.nav-button{font-size:16px;font-weight:bold}"
        ".list-row{font-size:20px;border-color:#27344a}.play-selected{font-size:22px;font-weight:bold;border-color:#00cfff}"
        ".room-help{font-size:18px;color:#77779a}.room-card{background:#121521;border:2px solid #2a3044;border-radius:22px}"
        ".room-card.active{background:#163d3d;border-color:#00cfff}.room-name{font-size:29px;font-weight:bold}.room-state{font-size:22px;color:#7d89a4}"
        ".setup-title{font-size:38px;font-weight:bold;color:#00cfff}.config-error{font-size:21px}";
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
    gtk_button_set_label(GTK_BUTTON(ui->play), playing ? "PAUSE" : "PLAY");
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

    gchar *volume_text = g_strdup_printf("VOLUME  %.0f%%", volume * 100.0);
    gtk_label_set_text(GTK_LABEL(ui->volume), volume_text);
    g_free(volume_text);

    panel_ui_set_modes(ui, shuffle, repeat);
}

void panel_ui_set_modes(PanelUi *ui, gboolean shuffle, const gchar *repeat)
{
    toggle_css_class(ui->shuffle, "active", shuffle);
    gchar *upper_repeat = g_ascii_strup(repeat, -1);
    gchar *repeat_text = g_strdup_printf("REPEAT %s", upper_repeat);
    gtk_button_set_label(GTK_BUTTON(ui->repeat), repeat_text);
    toggle_css_class(ui->repeat, "active", !g_str_equal(repeat, "off"));
    g_free(upper_repeat);
    g_free(repeat_text);
}

void panel_ui_set_album_art(PanelUi *ui, GdkPixbuf *pixbuf)
{
    gtk_image_set_from_pixbuf(GTK_IMAGE(ui->album_art), pixbuf);
}

void panel_ui_set_queue(PanelUi *ui, GPtrArray *titles, GPtrArray *artists,
                        guint count, gint selected)
{
    clear_container(ui->queue_list);
    for (guint i = 0; i < count; i++) {
        const gchar *title = g_ptr_array_index(titles, i);
        const gchar *artist = g_ptr_array_index(artists, i);
        gchar *text = *artist != '\0' ? g_strdup_printf("%s\n%s", title, artist)
                                      : g_strdup(title);
        GtkWidget *row = new_button(text, "list-row", -1, 90);
        gtk_widget_set_hexpand(row, TRUE);
        g_object_set_data(G_OBJECT(row), "index", GINT_TO_POINTER((gint)i));
        toggle_css_class(row, "selected", (gint)i == selected);
        g_signal_connect(row, "clicked", G_CALLBACK(queue_row_clicked), ui);
        gtk_box_pack_start(GTK_BOX(ui->queue_list), row, FALSE, FALSE, 5);
        g_free(text);
    }
    gtk_widget_show_all(ui->queue_list);
}

void panel_ui_set_playlists(PanelUi *ui, GPtrArray *names, guint count,
                            gint selected)
{
    clear_container(ui->playlist_list);
    for (guint i = 0; i < count; i++) {
        GtkWidget *row = new_button(g_ptr_array_index(names, i), "list-row",
                                    -1, 82);
        gtk_widget_set_hexpand(row, TRUE);
        g_object_set_data(G_OBJECT(row), "index", GINT_TO_POINTER((gint)i));
        toggle_css_class(row, "selected", (gint)i == selected);
        g_signal_connect(row, "clicked", G_CALLBACK(playlist_row_clicked), ui);
        gtk_box_pack_start(GTK_BOX(ui->playlist_list), row, FALSE, FALSE, 5);
    }
    gtk_widget_show_all(ui->playlist_list);
}

void panel_ui_select_queue_item(PanelUi *ui, gint selected)
{
    select_row(ui->queue_list, selected);
}

void panel_ui_select_playlist(PanelUi *ui, gint selected)
{
    select_row(ui->playlist_list, selected);
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
}
