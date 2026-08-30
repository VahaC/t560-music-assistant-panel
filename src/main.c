#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <math.h>
#include <string.h>

#define APP_ID "com.vahac.T560MusicPanel"
#define DEFAULT_POLL_MS 1000

typedef struct _Panel Panel;

typedef struct {
    Panel *panel;
    SoupMessage *message;
} Request;

typedef struct {
    Panel *panel;
    SoupMessage *message;
    gchar *url;
} ArtRequest;

struct _Panel {
    GtkApplication *application;
    GtkWidget *window, *stack, *status, *page_title;
    GtkWidget *album_art, *track_title, *artist, *progress, *position;
    GtkWidget *play, *shuffle, *repeat, *volume;
    GtkWidget *queue_list, *playlist_list;
    GtkWidget *room_buttons[4], *room_states[4];
    SoupSession *session;
    GPtrArray *queue_titles, *queue_artists, *queue_ids;
    GPtrArray *playlist_names, *playlist_uris;
    gchar *url, *token, *player_entity, *queue_entity, *playlists_entity;
    gchar *room_entities[4], *room_labels[4], *art_url, *repeat_state, *queue_data;
    guint poll_ms, poll_source;
    gint queue_selected, playlist_selected;
    gboolean poll_in_flight, player_playing, shuffle_state;
};

static gchar *config_path(const gchar *name)
{
    gchar *dir = g_build_filename(g_get_user_config_dir(), "t560-music-panel", NULL);
    gchar *path = g_build_filename(dir, name, NULL);
    g_free(dir);
    return path;
}

static gchar *key_string(GKeyFile *file, const gchar *group, const gchar *key,
                         const gchar *fallback)
{
    GError *error = NULL;
    gchar *value = g_key_file_get_string(file, group, key, &error);
    if (error != NULL) {
        g_clear_error(&error);
        return fallback == NULL ? NULL : g_strdup(fallback);
    }
    g_strstrip(value);
    return value;
}

static gint key_integer(GKeyFile *file, const gchar *group, const gchar *key,
                        gint fallback)
{
    GError *error = NULL;
    gint value = g_key_file_get_integer(file, group, key, &error);
    if (error != NULL) {
        g_clear_error(&error);
        return fallback;
    }
    return value;
}

static void css_add(GtkWidget *widget, const gchar *name)
{
    gtk_style_context_add_class(gtk_widget_get_style_context(widget), name);
}

static void css_toggle(GtkWidget *widget, const gchar *name, gboolean enabled)
{
    GtkStyleContext *style = gtk_widget_get_style_context(widget);
    if (enabled)
        gtk_style_context_add_class(style, name);
    else
        gtk_style_context_remove_class(style, name);
}

static GtkWidget *new_label(const gchar *text, const gchar *css)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    if (css != NULL)
        css_add(label, css);
    return label;
}

static GtkWidget *new_button(const gchar *text, const gchar *css, gint width,
                             gint height)
{
    GtkWidget *button = gtk_button_new_with_label(text);
    gtk_widget_set_size_request(button, width, height);
    if (css != NULL)
        css_add(button, css);
    return button;
}

static void set_status(Panel *panel, const gchar *text, gboolean error)
{
    gtk_label_set_text(GTK_LABEL(panel->status), text);
    css_toggle(panel->status, "error", error);
}

static const gchar *jstring(JsonObject *object, const gchar *member,
                            const gchar *fallback)
{
    if (object == NULL || !json_object_has_member(object, member))
        return fallback;
    JsonNode *node = json_object_get_member(object, member);
    return node != NULL && json_node_get_value_type(node) == G_TYPE_STRING
               ? json_node_get_string(node) : fallback;
}

static gboolean jnumber(JsonObject *object, const gchar *member, gdouble *value)
{
    if (object == NULL || !json_object_has_member(object, member))
        return FALSE;
    JsonNode *node = json_object_get_member(object, member);
    if (node == NULL || !JSON_NODE_HOLDS_VALUE(node))
        return FALSE;
    GType type = json_node_get_value_type(node);
    if (type != G_TYPE_DOUBLE && type != G_TYPE_INT64 && type != G_TYPE_INT)
        return FALSE;
    *value = json_node_get_double(node);
    return TRUE;
}

static gboolean jboolean(JsonObject *object, const gchar *member,
                         gboolean fallback)
{
    if (object == NULL || !json_object_has_member(object, member))
        return fallback;
    JsonNode *node = json_object_get_member(object, member);
    return node != NULL && json_node_get_value_type(node) == G_TYPE_BOOLEAN
               ? json_node_get_boolean(node) : fallback;
}

static JsonObject *attributes(JsonObject *state)
{
    return state != NULL && json_object_has_member(state, "attributes")
               ? json_object_get_object_member(state, "attributes") : NULL;
}

static void request_free(Request *request)
{
    g_object_unref(request->message);
    g_free(request);
}

static void add_auth(Panel *panel, SoupMessage *message)
{
    gchar *auth = g_strdup_printf("Bearer %s", panel->token);
    SoupMessageHeaders *headers = soup_message_get_request_headers(message);
    soup_message_headers_append(headers, "Authorization", auth);
    soup_message_headers_append(headers, "Accept", "application/json");
    g_free(auth);
}

static gchar *service_json(const gchar *entity, const gchar *key,
                           const gchar *string_value, gint boolean_value)
{
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "entity_id");
    json_builder_add_string_value(builder, entity);
    if (key != NULL) {
        json_builder_set_member_name(builder, key);
        if (boolean_value >= 0)
            json_builder_add_boolean_value(builder, boolean_value != 0);
        else
            json_builder_add_string_value(builder, string_value != NULL ? string_value : "");
    }
    json_builder_end_object(builder);
    JsonNode *root = json_builder_get_root(builder);
    JsonGenerator *generator = json_generator_new();
    json_generator_set_root(generator, root);
    gchar *json = json_generator_to_data(generator, NULL);
    json_node_free(root);
    g_object_unref(generator);
    g_object_unref(builder);
    return json;
}

static gboolean poll_states(gpointer user_data);

static gboolean poll_soon(gpointer user_data)
{
    poll_states(user_data);
    return G_SOURCE_REMOVE;
}

static void service_finished(GObject *source, GAsyncResult *result,
                             gpointer user_data)
{
    Request *request = user_data;
    Panel *panel = request->panel;
    GError *error = NULL;
    GBytes *body = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);
    if (error != NULL) {
        gchar *text = g_strdup_printf("Command failed: %s", error->message);
        set_status(panel, text, TRUE);
        g_free(text);
        g_clear_error(&error);
    } else {
        guint status = soup_message_get_status(request->message);
        if (status < 200 || status >= 300) {
            gchar *text = g_strdup_printf("Command HTTP %u", status);
            set_status(panel, text, TRUE);
            g_free(text);
        } else {
            set_status(panel, "Connected", FALSE);
            g_timeout_add(120, poll_soon, panel);
        }
    }
    if (body != NULL)
        g_bytes_unref(body);
    request_free(request);
}

static void call_service(Panel *panel, const gchar *domain, const gchar *service,
                         const gchar *json)
{
    gchar *uri = g_strdup_printf("%s/api/services/%s/%s", panel->url, domain, service);
    SoupMessage *message = soup_message_new("POST", uri);
    g_free(uri);
    if (message == NULL) {
        set_status(panel, "Invalid Home Assistant URL", TRUE);
        return;
    }
    add_auth(panel, message);
    GBytes *bytes = g_bytes_new(json, strlen(json));
    soup_message_set_request_body_from_bytes(message, "application/json", bytes);
    g_bytes_unref(bytes);
    Request *request = g_new0(Request, 1);
    request->panel = panel;
    request->message = g_object_ref(message);
    soup_session_send_and_read_async(panel->session, message, G_PRIORITY_DEFAULT,
                                     NULL, service_finished, request);
    g_object_unref(message);
}

static void simple_player_service(Panel *panel, const gchar *service)
{
    gchar *json = service_json(panel->player_entity, NULL, NULL, -1);
    call_service(panel, "media_player", service, json);
    g_free(json);
}

static void player_clicked(GtkButton *button, gpointer user_data)
{
    Panel *panel = user_data;
    simple_player_service(panel, g_object_get_data(G_OBJECT(button), "service"));
}

static void shuffle_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    Panel *panel = user_data;
    panel->shuffle_state = !panel->shuffle_state;
    css_toggle(panel->shuffle, "active", panel->shuffle_state);
    gchar *json = service_json(panel->player_entity, "shuffle", NULL,
                               panel->shuffle_state);
    call_service(panel, "media_player", "shuffle_set", json);
    g_free(json);
}

static void repeat_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    Panel *panel = user_data;
    const gchar *next = g_str_equal(panel->repeat_state, "off") ? "all" :
                        g_str_equal(panel->repeat_state, "all") ? "one" : "off";
    g_free(panel->repeat_state);
    panel->repeat_state = g_strdup(next);
    gchar *upper = g_ascii_strup(next, -1);
    gchar *text = g_strdup_printf("REPEAT %s", upper);
    gtk_button_set_label(GTK_BUTTON(panel->repeat), text);
    css_toggle(panel->repeat, "active", !g_str_equal(next, "off"));
    gchar *json = service_json(panel->player_entity, "repeat", next, -1);
    call_service(panel, "media_player", "repeat_set", json);
    g_free(json);
    g_free(text);
    g_free(upper);
}

static void room_clicked(GtkButton *button, gpointer user_data)
{
    Panel *panel = user_data;
    gint index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "room-index"));
    if (index < 0 || index > 3 || panel->room_entities[index] == NULL)
        return;
    gchar *json = service_json(panel->room_entities[index], NULL, NULL, -1);
    call_service(panel, "homeassistant", "toggle", json);
    g_free(json);
}

static void page_clicked(GtkButton *button, gpointer user_data)
{
    Panel *panel = user_data;
    const gchar *page = g_object_get_data(G_OBJECT(button), "page");
    gtk_stack_set_visible_child_name(GTK_STACK(panel->stack), page);
    gtk_label_set_text(GTK_LABEL(panel->page_title),
                       g_object_get_data(G_OBJECT(button), "title"));
    if (g_str_equal(page, "queue"))
        call_service(panel, "media_controller", "refresh", "{}");
}

static void select_row(GtkWidget *list, gint selected)
{
    GList *rows = gtk_container_get_children(GTK_CONTAINER(list));
    for (GList *item = rows; item != NULL; item = item->next) {
        GtkWidget *row = GTK_WIDGET(item->data);
        gint index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "index"));
        css_toggle(row, "selected", index == selected);
    }
    g_list_free(rows);
}

static void queue_row_clicked(GtkButton *button, gpointer user_data)
{
    Panel *panel = user_data;
    panel->queue_selected = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "index"));
    select_row(panel->queue_list, panel->queue_selected);
}

static void playlist_row_clicked(GtkButton *button, gpointer user_data)
{
    Panel *panel = user_data;
    panel->playlist_selected = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "index"));
    select_row(panel->playlist_list, panel->playlist_selected);
}

static gchar *builder_to_json(JsonBuilder *builder)
{
    JsonNode *root = json_builder_get_root(builder);
    JsonGenerator *generator = json_generator_new();
    json_generator_set_root(generator, root);
    gchar *json = json_generator_to_data(generator, NULL);
    json_node_free(root);
    g_object_unref(generator);
    return json;
}

static void queue_play_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    Panel *panel = user_data;
    if (panel->queue_selected < 0 ||
        (guint)panel->queue_selected >= panel->queue_ids->len)
        return;
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "entity_id");
    json_builder_add_string_value(builder, panel->player_entity);
    json_builder_set_member_name(builder, "queue_item_id");
    json_builder_add_string_value(builder,
                                  g_ptr_array_index(panel->queue_ids, panel->queue_selected));
    json_builder_end_object(builder);
    gchar *json = builder_to_json(builder);
    call_service(panel, "media_controller", "play_queue_item", json);
    g_free(json);
    g_object_unref(builder);
}

static void playlist_play_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    Panel *panel = user_data;
    if (panel->playlist_selected < 0 ||
        (guint)panel->playlist_selected >= panel->playlist_uris->len)
        return;
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "entity_id");
    json_builder_add_string_value(builder, panel->player_entity);
    json_builder_set_member_name(builder, "media_id");
    json_builder_add_string_value(builder,
                                  g_ptr_array_index(panel->playlist_uris,
                                                    panel->playlist_selected));
    json_builder_set_member_name(builder, "media_type");
    json_builder_add_string_value(builder, "playlist");
    json_builder_end_object(builder);
    gchar *json = builder_to_json(builder);
    call_service(panel, "music_assistant", "play_media", json);
    g_free(json);
    g_object_unref(builder);
    gtk_stack_set_visible_child_name(GTK_STACK(panel->stack), "player");
    gtk_label_set_text(GTK_LABEL(panel->page_title), "PLAYER");
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

static void art_finished(GObject *source, GAsyncResult *result, gpointer user_data)
{
    ArtRequest *request = user_data;
    Panel *panel = request->panel;
    GError *error = NULL;
    GBytes *bytes = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);
    if (error == NULL && bytes != NULL && g_strcmp0(request->url, panel->art_url) == 0 &&
        soup_message_get_status(request->message) == SOUP_STATUS_OK) {
        GInputStream *stream = g_memory_input_stream_new_from_bytes(bytes);
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream_at_scale(stream, 510, 510, TRUE,
                                                                NULL, &error);
        if (pixbuf != NULL) {
            gtk_image_set_from_pixbuf(GTK_IMAGE(panel->album_art), pixbuf);
            g_object_unref(pixbuf);
        }
        g_object_unref(stream);
    }
    g_clear_error(&error);
    if (bytes != NULL)
        g_bytes_unref(bytes);
    g_object_unref(request->message);
    g_free(request->url);
    g_free(request);
}

static void load_album_art(Panel *panel, const gchar *picture)
{
    if (picture == NULL || *picture == '\0')
        return;
    gchar *url = (g_str_has_prefix(picture, "http://") ||
                  g_str_has_prefix(picture, "https://"))
                     ? g_strdup(picture) : g_strdup_printf("%s%s", panel->url, picture);
    if (g_strcmp0(url, panel->art_url) == 0) {
        g_free(url);
        return;
    }
    g_free(panel->art_url);
    panel->art_url = url;
    SoupMessage *message = soup_message_new("GET", panel->art_url);
    if (message == NULL)
        return;
    add_auth(panel, message);
    ArtRequest *request = g_new0(ArtRequest, 1);
    request->panel = panel;
    request->message = g_object_ref(message);
    request->url = g_strdup(panel->art_url);
    soup_session_send_and_read_async(panel->session, message, G_PRIORITY_LOW,
                                     NULL, art_finished, request);
    g_object_unref(message);
}

static void update_player(Panel *panel, JsonObject *state)
{
    if (state == NULL)
        return;
    JsonObject *attrs = attributes(state);
    panel->player_playing = g_str_equal(jstring(state, "state", ""), "playing");
    gtk_button_set_label(GTK_BUTTON(panel->play), panel->player_playing ? "PAUSE" : "PLAY");
    gtk_label_set_text(GTK_LABEL(panel->track_title),
                       jstring(attrs, "media_title", "Nothing playing"));
    gtk_label_set_text(GTK_LABEL(panel->artist), jstring(attrs, "media_artist", ""));
    gdouble position = 0.0, duration = 0.0, volume = 0.0;
    jnumber(attrs, "media_position", &position);
    jnumber(attrs, "media_duration", &duration);
    jnumber(attrs, "volume_level", &volume);
    const gchar *updated_at = jstring(attrs, "media_position_updated_at", NULL);
    if (panel->player_playing && updated_at != NULL) {
        GDateTime *updated = g_date_time_new_from_iso8601(updated_at, NULL);
        GDateTime *now = g_date_time_new_now_utc();
        if (updated != NULL) {
            gint64 elapsed = g_date_time_difference(now, updated);
            if (elapsed > 0)
                position += (gdouble)elapsed / G_TIME_SPAN_SECOND;
            g_date_time_unref(updated);
        }
        g_date_time_unref(now);
    }
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(panel->progress),
                                  duration > 0.0 ? CLAMP(position / duration, 0.0, 1.0) : 0.0);
    gchar *pos = format_time(position), *dur = format_time(duration);
    gchar *timeline = g_strdup_printf("%s  /  %s", pos, dur);
    gtk_label_set_text(GTK_LABEL(panel->position), timeline);
    g_free(pos); g_free(dur); g_free(timeline);
    gchar *volume_text = g_strdup_printf("VOLUME  %.0f%%", volume * 100.0);
    gtk_label_set_text(GTK_LABEL(panel->volume), volume_text);
    g_free(volume_text);
    panel->shuffle_state = jboolean(attrs, "shuffle", FALSE);
    css_toggle(panel->shuffle, "active", panel->shuffle_state);
    const gchar *repeat = jstring(attrs, "repeat", "off");
    g_free(panel->repeat_state);
    panel->repeat_state = g_strdup(repeat);
    gchar *upper = g_ascii_strup(repeat, -1);
    gchar *repeat_text = g_strdup_printf("REPEAT %s", upper);
    gtk_button_set_label(GTK_BUTTON(panel->repeat), repeat_text);
    css_toggle(panel->repeat, "active", !g_str_equal(repeat, "off"));
    g_free(upper); g_free(repeat_text);
    load_album_art(panel, jstring(attrs, "entity_picture", ""));
}

static JsonArray *optional_array(JsonObject *object, const gchar *member)
{
    return object != NULL && json_object_has_member(object, member)
               ? json_object_get_array_member(object, member) : NULL;
}

static void copy_array(GPtrArray *target, JsonArray *source)
{
    g_ptr_array_set_size(target, 0);
    if (source == NULL)
        return;
    for (guint i = 0; i < json_array_get_length(source); i++) {
        const gchar *value = json_array_get_string_element(source, i);
        g_ptr_array_add(target, g_strdup(value != NULL ? value : ""));
    }
}

static gboolean array_matches(GPtrArray *current, JsonArray *incoming)
{
    if (incoming == NULL)
        return current->len == 0;
    if (current->len != json_array_get_length(incoming))
        return FALSE;
    for (guint i = 0; i < current->len; i++) {
        const gchar *left = g_ptr_array_index(current, i);
        const gchar *right = json_array_get_string_element(incoming, i);
        if (g_strcmp0(left, right) != 0)
            return FALSE;
    }
    return TRUE;
}

static void update_queue(Panel *panel, JsonObject *state)
{
    const gchar *data = jstring(attributes(state), "data", NULL);
    if (data == NULL)
        return;
    if (g_strcmp0(data, panel->queue_data) == 0)
        return;
    g_free(panel->queue_data);
    panel->queue_data = g_strdup(data);
    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, data, -1, NULL) ||
        !JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) {
        g_object_unref(parser);
        return;
    }
    JsonObject *payload = json_node_get_object(json_parser_get_root(parser));
    copy_array(panel->queue_titles, optional_array(payload, "titles"));
    copy_array(panel->queue_artists, optional_array(payload, "artists"));
    copy_array(panel->queue_ids, optional_array(payload, "queue_ids"));
    panel->queue_selected = json_object_has_member(payload, "current_index")
                                ? (gint)json_object_get_int_member(payload, "current_index") : 0;
    clear_container(panel->queue_list);
    guint count = MIN(panel->queue_titles->len,
                      MIN(panel->queue_artists->len, panel->queue_ids->len));
    for (guint i = 0; i < count; i++) {
        const gchar *title = g_ptr_array_index(panel->queue_titles, i);
        const gchar *artist = g_ptr_array_index(panel->queue_artists, i);
        gchar *text = *artist ? g_strdup_printf("%s\n%s", title, artist) : g_strdup(title);
        GtkWidget *row = new_button(text, "list-row", -1, 90);
        gtk_widget_set_hexpand(row, TRUE);
        g_object_set_data(G_OBJECT(row), "index", GINT_TO_POINTER((gint)i));
        css_toggle(row, "selected", (gint)i == panel->queue_selected);
        g_signal_connect(row, "clicked", G_CALLBACK(queue_row_clicked), panel);
        gtk_box_pack_start(GTK_BOX(panel->queue_list), row, FALSE, FALSE, 5);
        g_free(text);
    }
    gtk_widget_show_all(panel->queue_list);
    g_object_unref(parser);
}

static void update_playlists(Panel *panel, JsonObject *state)
{
    JsonObject *attrs = attributes(state);
    JsonArray *names = optional_array(attrs, "names");
    JsonArray *uris = optional_array(attrs, "uris");
    if (array_matches(panel->playlist_names, names) &&
        array_matches(panel->playlist_uris, uris))
        return;
    copy_array(panel->playlist_names, names);
    copy_array(panel->playlist_uris, uris);
    if (panel->playlist_selected < 0 ||
        (guint)panel->playlist_selected >= panel->playlist_names->len)
        panel->playlist_selected = panel->playlist_names->len > 0 ? 0 : -1;
    clear_container(panel->playlist_list);
    guint count = MIN(panel->playlist_names->len, panel->playlist_uris->len);
    for (guint i = 0; i < count; i++) {
        GtkWidget *row = new_button(g_ptr_array_index(panel->playlist_names, i),
                                    "list-row", -1, 82);
        gtk_widget_set_hexpand(row, TRUE);
        g_object_set_data(G_OBJECT(row), "index", GINT_TO_POINTER((gint)i));
        css_toggle(row, "selected", (gint)i == panel->playlist_selected);
        g_signal_connect(row, "clicked", G_CALLBACK(playlist_row_clicked), panel);
        gtk_box_pack_start(GTK_BOX(panel->playlist_list), row, FALSE, FALSE, 5);
    }
    gtk_widget_show_all(panel->playlist_list);
}

static void update_room(Panel *panel, gint index, JsonObject *state)
{
    if (state == NULL)
        return;
    gboolean active = g_str_equal(jstring(state, "state", ""), "on");
    gtk_label_set_text(GTK_LABEL(panel->room_states[index]), active ? "ON" : "OFF");
    css_toggle(panel->room_buttons[index], "active", active);
}

static void poll_finished(GObject *source, GAsyncResult *result, gpointer user_data)
{
    Request *request = user_data;
    Panel *panel = request->panel;
    GError *error = NULL;
    GBytes *body = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);
    panel->poll_in_flight = FALSE;
    if (error != NULL) {
        gchar *text = g_strdup_printf("Offline: %s", error->message);
        set_status(panel, text, TRUE);
        g_free(text); g_clear_error(&error); request_free(request);
        return;
    }
    guint status = soup_message_get_status(request->message);
    if (status != SOUP_STATUS_OK) {
        gchar *text = g_strdup_printf("Home Assistant HTTP %u", status);
        set_status(panel, text, TRUE);
        g_free(text); g_bytes_unref(body); request_free(request);
        return;
    }
    gsize length = 0;
    const gchar *data = g_bytes_get_data(body, &length);
    JsonParser *parser = json_parser_new();
    if (json_parser_load_from_data(parser, data, (gssize)length, &error) &&
        JSON_NODE_HOLDS_ARRAY(json_parser_get_root(parser))) {
        GHashTable *states = g_hash_table_new(g_str_hash, g_str_equal);
        JsonArray *array = json_node_get_array(json_parser_get_root(parser));
        for (guint i = 0; i < json_array_get_length(array); i++) {
            JsonObject *state = json_array_get_object_element(array, i);
            const gchar *entity = jstring(state, "entity_id", NULL);
            if (entity != NULL)
                g_hash_table_insert(states, (gpointer)entity, state);
        }
        update_player(panel, g_hash_table_lookup(states, panel->player_entity));
        update_queue(panel, g_hash_table_lookup(states, panel->queue_entity));
        update_playlists(panel, g_hash_table_lookup(states, panel->playlists_entity));
        for (gint i = 0; i < 4; i++)
            if (panel->room_entities[i] != NULL)
                update_room(panel, i, g_hash_table_lookup(states, panel->room_entities[i]));
        g_hash_table_unref(states);
        set_status(panel, "Connected", FALSE);
    } else {
        set_status(panel, "Invalid Home Assistant response", TRUE);
        g_clear_error(&error);
    }
    g_object_unref(parser); g_bytes_unref(body); request_free(request);
}

static gboolean poll_states(gpointer user_data)
{
    Panel *panel = user_data;
    if (panel->poll_in_flight)
        return G_SOURCE_CONTINUE;
    gchar *uri = g_strdup_printf("%s/api/states", panel->url);
    SoupMessage *message = soup_message_new("GET", uri);
    g_free(uri);
    if (message == NULL)
        return G_SOURCE_CONTINUE;
    add_auth(panel, message);
    Request *request = g_new0(Request, 1);
    request->panel = panel;
    request->message = g_object_ref(message);
    panel->poll_in_flight = TRUE;
    soup_session_send_and_read_async(panel->session, message, G_PRIORITY_DEFAULT,
                                     NULL, poll_finished, request);
    g_object_unref(message);
    return G_SOURCE_CONTINUE;
}

static GtkWidget *nav_button(Panel *panel, const gchar *text, const gchar *page,
                             const gchar *title)
{
    GtkWidget *button = new_button(text, "nav-button", 150, 68);
    g_object_set_data(G_OBJECT(button), "page", (gpointer)page);
    g_object_set_data(G_OBJECT(button), "title", (gpointer)title);
    g_signal_connect(button, "clicked", G_CALLBACK(page_clicked), panel);
    return button;
}

static GtkWidget *navigation(Panel *panel)
{
    GtkWidget *nav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(nav, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(nav), nav_button(panel, "PLAYER", "player", "PLAYER"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(nav), nav_button(panel, "QUEUE", "queue", "QUEUE"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(nav), nav_button(panel, "PLAYLISTS", "playlists", "PLAYLISTS"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(nav), nav_button(panel, "ROOM", "room", "ROOM CONTROLS"), FALSE, FALSE, 0);
    return nav;
}

static GtkWidget *player_page(Panel *panel)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 9);
    gtk_widget_set_margin_start(page, 18); gtk_widget_set_margin_end(page, 18);
    panel->album_art = gtk_image_new_from_icon_name("audio-x-generic", GTK_ICON_SIZE_DIALOG);
    gtk_widget_set_size_request(panel->album_art, 510, 510);
    panel->track_title = new_label("Nothing playing", "track-title");
    panel->artist = new_label("", "artist");
    panel->progress = gtk_progress_bar_new();
    gtk_widget_set_size_request(panel->progress, -1, 20);
    panel->position = new_label("0:00  /  0:00", "position");
    gtk_box_pack_start(GTK_BOX(page), panel->album_art, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), panel->track_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), panel->artist, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), panel->progress, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), panel->position, FALSE, FALSE, 0);

    GtkWidget *modes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    panel->shuffle = new_button("SHUFFLE", "mode-button", 180, 60);
    panel->repeat = new_button("REPEAT OFF", "mode-button", 180, 60);
    g_signal_connect(panel->shuffle, "clicked", G_CALLBACK(shuffle_clicked), panel);
    g_signal_connect(panel->repeat, "clicked", G_CALLBACK(repeat_clicked), panel);
    gtk_box_pack_start(GTK_BOX(modes), panel->shuffle, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(modes), panel->repeat, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), modes, FALSE, FALSE, 0);

    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *previous = new_button("PREVIOUS", "control-button", 180, 86);
    panel->play = new_button("PLAY", "play-button", 210, 96);
    GtkWidget *next = new_button("NEXT", "control-button", 180, 86);
    g_object_set_data(G_OBJECT(previous), "service", "media_previous_track");
    g_object_set_data(G_OBJECT(panel->play), "service", "media_play_pause");
    g_object_set_data(G_OBJECT(next), "service", "media_next_track");
    g_signal_connect(previous, "clicked", G_CALLBACK(player_clicked), panel);
    g_signal_connect(panel->play, "clicked", G_CALLBACK(player_clicked), panel);
    g_signal_connect(next, "clicked", G_CALLBACK(player_clicked), panel);
    gtk_box_pack_start(GTK_BOX(controls), previous, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(controls), panel->play, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(controls), next, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), controls, FALSE, FALSE, 0);

    GtkWidget *volume = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *down = new_button("VOL -", "control-button", 180, 68);
    panel->volume = new_label("VOLUME --", "volume");
    GtkWidget *up = new_button("VOL +", "control-button", 180, 68);
    g_object_set_data(G_OBJECT(down), "service", "volume_down");
    g_object_set_data(G_OBJECT(up), "service", "volume_up");
    g_signal_connect(down, "clicked", G_CALLBACK(player_clicked), panel);
    g_signal_connect(up, "clicked", G_CALLBACK(player_clicked), panel);
    gtk_box_pack_start(GTK_BOX(volume), down, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(volume), panel->volume, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(volume), up, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), volume, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(page), navigation(panel), FALSE, FALSE, 4);
    return page;
}

static GtkWidget *list_page(Panel *panel, gboolean queue)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(page, 16); gtk_widget_set_margin_end(page, 16);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_kinetic_scrolling(GTK_SCROLLED_WINDOW(scroll), TRUE);
    GtkWidget *list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(scroll), list);
    GtkWidget *play = new_button(queue ? "PLAY SELECTED TRACK" : "PLAY SELECTED PLAYLIST",
                                 "play-selected", -1, 80);
    if (queue) {
        panel->queue_list = list;
        g_signal_connect(play, "clicked", G_CALLBACK(queue_play_clicked), panel);
    } else {
        panel->playlist_list = list;
        g_signal_connect(play, "clicked", G_CALLBACK(playlist_play_clicked), panel);
    }
    gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), play, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), navigation(panel), FALSE, FALSE, 4);
    return page;
}

static GtkWidget *room_page(Panel *panel)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(page, 20); gtk_widget_set_margin_end(page, 20);
    gtk_box_pack_start(GTK_BOX(page), new_label("Tap a control to toggle", "room-help"),
                       FALSE, FALSE, 0);
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 18);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 18);
    gtk_widget_set_vexpand(grid, TRUE);
    for (gint i = 0; i < 4; i++) {
        GtkWidget *button = gtk_button_new();
        gtk_widget_set_hexpand(button, TRUE); gtk_widget_set_vexpand(button, TRUE);
        gtk_widget_set_size_request(button, 350, 350); css_add(button, "room-card");
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
        panel->room_states[i] = new_label(panel->room_entities[i] ? "--" : "NOT CONFIGURED",
                                          "room-state");
        gtk_box_pack_start(GTK_BOX(box), new_label(panel->room_labels[i], "room-name"),
                           TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(box), panel->room_states[i], TRUE, TRUE, 0);
        gtk_container_add(GTK_CONTAINER(button), box);
        g_object_set_data(G_OBJECT(button), "room-index", GINT_TO_POINTER(i));
        g_signal_connect(button, "clicked", G_CALLBACK(room_clicked), panel);
        gtk_widget_set_sensitive(button, panel->room_entities[i] != NULL);
        panel->room_buttons[i] = button;
        gtk_grid_attach(GTK_GRID(grid), button, i % 2, i / 2, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(page), grid, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), navigation(panel), FALSE, FALSE, 4);
    return page;
}

static GtkWidget *build_shell(Panel *panel)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    css_add(header, "header"); gtk_widget_set_size_request(header, -1, 62);
    panel->page_title = new_label("PLAYER", "header-title");
    gtk_widget_set_halign(panel->page_title, GTK_ALIGN_START);
    gtk_widget_set_hexpand(panel->page_title, TRUE);
    panel->status = new_label("Connecting", "status");
    gtk_box_pack_start(GTK_BOX(header), panel->page_title, TRUE, TRUE, 18);
    gtk_box_pack_start(GTK_BOX(header), panel->status, FALSE, FALSE, 18);
    panel->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(panel->stack), GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_stack_add_named(GTK_STACK(panel->stack), player_page(panel), "player");
    gtk_stack_add_named(GTK_STACK(panel->stack), list_page(panel, TRUE), "queue");
    gtk_stack_add_named(GTK_STACK(panel->stack), list_page(panel, FALSE), "playlists");
    gtk_stack_add_named(GTK_STACK(panel->stack), room_page(panel), "room");
    gtk_box_pack_start(GTK_BOX(outer), header, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), panel->stack, TRUE, TRUE, 0);
    return outer;
}

static GtkWidget *config_error_page(const gchar *message)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(box, 60); gtk_widget_set_margin_end(box, 60);
    GtkWidget *error = new_label(message, "config-error");
    gtk_label_set_line_wrap(GTK_LABEL(error), TRUE);
    gtk_box_pack_start(GTK_BOX(box), new_label("T560 Music Panel", "setup-title"),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), error, FALSE, FALSE, 0);
    return box;
}

static gboolean load_config(Panel *panel, gchar **failure)
{
    gchar *path = config_path("config.ini");
    GKeyFile *file = g_key_file_new();
    if (!g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, NULL)) {
        *failure = g_strdup_printf("Configuration is missing.\n\nCreate:\n%s\n\nCopy config.ini.example and edit it over SSH. This application has no keyboard or text input.", path);
        g_key_file_unref(file); g_free(path); return FALSE;
    }
    panel->url = key_string(file, "home_assistant", "url", NULL);
    panel->player_entity = key_string(file, "entities", "player", NULL);
    panel->queue_entity = key_string(file, "entities", "queue", NULL);
    panel->playlists_entity = key_string(file, "entities", "playlists", NULL);
    const gchar *keys[] = { "light_1", "light_2", "fan", "ac" };
    const gchar *labels[] = { "LIGHT 1", "LIGHT 2", "FAN", "AC" };
    for (gint i = 0; i < 4; i++) {
        panel->room_entities[i] = key_string(file, "entities", keys[i], NULL);
        panel->room_labels[i] = key_string(file, "labels", keys[i], labels[i]);
    }
    panel->poll_ms = (guint)CLAMP(key_integer(file, "panel", "poll_interval_ms",
                                              DEFAULT_POLL_MS), 500, 30000);
    g_key_file_unref(file); g_free(path);
    if (!panel->url || !panel->player_entity || !panel->queue_entity ||
        !panel->playlists_entity) {
        *failure = g_strdup("config.ini must define url, player, queue, and playlists.");
        return FALSE;
    }
    while (*panel->url && panel->url[strlen(panel->url) - 1] == '/')
        panel->url[strlen(panel->url) - 1] = '\0';
    gchar *token_path = config_path("token");
    if (!g_file_get_contents(token_path, &panel->token, NULL, NULL)) {
        *failure = g_strdup_printf("Home Assistant token is missing.\n\nCreate this file over SSH and set mode 600:\n%s", token_path);
        g_free(token_path); return FALSE;
    }
    g_free(token_path); g_strstrip(panel->token);
    if (!*panel->token) {
        *failure = g_strdup("The token file is empty."); return FALSE;
    }
    return TRUE;
}

static void install_css(void)
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
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void activate(GtkApplication *application, gpointer user_data)
{
    Panel *panel = user_data;
    panel->application = application; install_css();
    panel->window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(panel->window), "T560 Music Panel");
    gtk_window_set_wmclass(GTK_WINDOW(panel->window), "t560-music-panel", "T560MusicPanel");
    gtk_window_set_default_size(GTK_WINDOW(panel->window), 800, 1219);
    gtk_window_maximize(GTK_WINDOW(panel->window));
    gchar *failure = NULL;
    GtkWidget *content;
    if (!load_config(panel, &failure)) {
        content = config_error_page(failure); g_free(failure);
    } else {
        content = build_shell(panel);
        poll_states(panel);
        panel->poll_source = g_timeout_add(panel->poll_ms, poll_states, panel);
    }
    gtk_container_add(GTK_CONTAINER(panel->window), content);
    gtk_widget_show_all(panel->window);
}

static void panel_free(Panel *panel)
{
    if (panel->poll_source) g_source_remove(panel->poll_source);
    soup_session_abort(panel->session); g_object_unref(panel->session);
    g_ptr_array_unref(panel->queue_titles); g_ptr_array_unref(panel->queue_artists);
    g_ptr_array_unref(panel->queue_ids); g_ptr_array_unref(panel->playlist_names);
    g_ptr_array_unref(panel->playlist_uris);
    g_free(panel->url); g_free(panel->token); g_free(panel->player_entity);
    g_free(panel->queue_entity); g_free(panel->playlists_entity); g_free(panel->art_url);
    g_free(panel->repeat_state); g_free(panel->queue_data);
    for (gint i = 0; i < 4; i++) { g_free(panel->room_entities[i]); g_free(panel->room_labels[i]); }
}

int main(int argc, char **argv)
{
    g_set_prgname("t560-music-panel");
    g_set_application_name("T560 Music Panel");
    Panel panel; memset(&panel, 0, sizeof(panel));
    panel.poll_ms = DEFAULT_POLL_MS; panel.queue_selected = -1; panel.playlist_selected = -1;
    panel.repeat_state = g_strdup("off");
    panel.queue_titles = g_ptr_array_new_with_free_func(g_free);
    panel.queue_artists = g_ptr_array_new_with_free_func(g_free);
    panel.queue_ids = g_ptr_array_new_with_free_func(g_free);
    panel.playlist_names = g_ptr_array_new_with_free_func(g_free);
    panel.playlist_uris = g_ptr_array_new_with_free_func(g_free);
    panel.session = soup_session_new_with_options("timeout", 8,
                                                  "user-agent", "t560-music-panel/0.1", NULL);
    GtkApplication *application = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(activate), &panel);
    int result = g_application_run(G_APPLICATION(application), argc, argv);
    panel_free(&panel); g_object_unref(application);
    return result;
}
