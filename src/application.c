#include "application.h"

#include "app_config.h"
#include "home_assistant_client.h"
#include "json_helpers.h"
#include "panel_ui.h"

#include <json-glib/json-glib.h>

#define APPLICATION_ID "com.vahac.T560MusicPanel"

typedef struct {
    GtkWidget *window;
    AppConfig *config;
    PanelUi *ui;
    HomeAssistantClient *client;
    GPtrArray *queue_titles;
    GPtrArray *queue_artists;
    GPtrArray *queue_ids;
    GPtrArray *playlist_names;
    GPtrArray *playlist_uris;
    gchar *album_art_url;
    gchar *repeat_state;
    gchar *queue_data;
    guint poll_source;
    gint queue_selected;
    gint playlist_selected;
    gboolean poll_in_flight;
    gboolean player_playing;
    gboolean shuffle_state;
} PanelApplication;

typedef struct {
    PanelApplication *application;
    gchar *url;
} AlbumArtRequest;

static gboolean poll_states(gpointer user_data);

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
            json_builder_add_string_value(
                builder, string_value != NULL ? string_value : "");
    }
    json_builder_end_object(builder);
    gchar *json = json_builder_to_string(builder);
    g_object_unref(builder);
    return json;
}

static void service_finished(guint status_code, GBytes *body,
                             const GError *error, gpointer user_data)
{
    (void)body;
    PanelApplication *application = user_data;

    if (error != NULL) {
        gchar *message = g_strdup_printf("Command failed: %s", error->message);
        panel_ui_set_status(application->ui, message, TRUE);
        g_free(message);
    } else if (status_code < 200 || status_code >= 300) {
        gchar *message = g_strdup_printf("Command HTTP %u", status_code);
        panel_ui_set_status(application->ui, message, TRUE);
        g_free(message);
    } else {
        panel_ui_set_status(application->ui, "Connected", FALSE);
        poll_states(application);
    }
}

static void call_service(PanelApplication *application, const gchar *domain,
                         const gchar *service, const gchar *json)
{
    if (!home_assistant_client_call_service(
            application->client, domain, service, json, service_finished,
            application)) {
        panel_ui_set_status(application->ui,
                            "Invalid Home Assistant URL", TRUE);
    }
}

static void call_entity_service(PanelApplication *application,
                                const gchar *domain, const gchar *service,
                                const gchar *entity)
{
    gchar *json = service_json(entity, NULL, NULL, -1);
    call_service(application, domain, service, json);
    g_free(json);
}

static void play_selected_queue_item(PanelApplication *application)
{
    if (application->queue_selected < 0 ||
        (guint)application->queue_selected >= application->queue_ids->len)
        return;

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "entity_id");
    json_builder_add_string_value(builder, application->config->player_entity);
    json_builder_set_member_name(builder, "queue_item_id");
    json_builder_add_string_value(
        builder, g_ptr_array_index(application->queue_ids,
                                   application->queue_selected));
    json_builder_end_object(builder);
    gchar *json = json_builder_to_string(builder);
    call_service(application, "media_controller", "play_queue_item", json);
    g_free(json);
    g_object_unref(builder);
}

static void play_selected_playlist(PanelApplication *application)
{
    if (application->playlist_selected < 0 ||
        (guint)application->playlist_selected >= application->playlist_uris->len)
        return;

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "entity_id");
    json_builder_add_string_value(builder, application->config->player_entity);
    json_builder_set_member_name(builder, "media_id");
    json_builder_add_string_value(
        builder, g_ptr_array_index(application->playlist_uris,
                                   application->playlist_selected));
    json_builder_set_member_name(builder, "media_type");
    json_builder_add_string_value(builder, "playlist");
    json_builder_end_object(builder);
    gchar *json = json_builder_to_string(builder);
    call_service(application, "music_assistant", "play_media", json);
    g_free(json);
    g_object_unref(builder);
    panel_ui_show_page(application->ui, "player", "PLAYER");
}

static void handle_ui_event(PanelUiEvent event, const gchar *value, gint index,
                            gpointer user_data)
{
    PanelApplication *application = user_data;

    switch (event) {
    case PANEL_UI_PLAYER_SERVICE:
        call_entity_service(application, "media_player", value,
                            application->config->player_entity);
        break;
    case PANEL_UI_TOGGLE_SHUFFLE: {
        application->shuffle_state = !application->shuffle_state;
        panel_ui_set_modes(application->ui, application->shuffle_state,
                           application->repeat_state);
        gchar *json = service_json(application->config->player_entity,
                                   "shuffle", NULL,
                                   application->shuffle_state);
        call_service(application, "media_player", "shuffle_set", json);
        g_free(json);
        break;
    }
    case PANEL_UI_CYCLE_REPEAT: {
        const gchar *next = g_str_equal(application->repeat_state, "off")
                                ? "all"
                            : g_str_equal(application->repeat_state, "all")
                                ? "one"
                                : "off";
        g_free(application->repeat_state);
        application->repeat_state = g_strdup(next);
        panel_ui_set_modes(application->ui, application->shuffle_state, next);
        gchar *json = service_json(application->config->player_entity,
                                   "repeat", next, -1);
        call_service(application, "media_player", "repeat_set", json);
        g_free(json);
        break;
    }
    case PANEL_UI_TOGGLE_ROOM:
        if (index >= 0 && index < PANEL_ROOM_COUNT &&
            application->config->room_entities[index] != NULL) {
            call_entity_service(application, "homeassistant", "toggle",
                                application->config->room_entities[index]);
        }
        break;
    case PANEL_UI_SHOW_PAGE:
        if (g_str_equal(value, "queue"))
            call_service(application, "media_controller", "refresh", "{}");
        break;
    case PANEL_UI_SELECT_QUEUE_ITEM:
        application->queue_selected = index;
        break;
    case PANEL_UI_SELECT_PLAYLIST:
        application->playlist_selected = index;
        break;
    case PANEL_UI_PLAY_QUEUE_ITEM:
        play_selected_queue_item(application);
        break;
    case PANEL_UI_PLAY_PLAYLIST:
        play_selected_playlist(application);
        break;
    }
}

static void album_art_finished(guint status_code, GBytes *body,
                               const GError *error, gpointer user_data)
{
    AlbumArtRequest *request = user_data;
    PanelApplication *application = request->application;

    if (error == NULL && body != NULL && status_code == 200 &&
        g_strcmp0(request->url, application->album_art_url) == 0) {
        GError *decode_error = NULL;
        GInputStream *stream = g_memory_input_stream_new_from_bytes(body);
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream_at_scale(
            stream, 510, 510, TRUE, NULL, &decode_error);
        if (pixbuf != NULL) {
            panel_ui_set_album_art(application->ui, pixbuf);
            g_object_unref(pixbuf);
        }
        g_object_unref(stream);
        g_clear_error(&decode_error);
    }
}

static void album_art_request_free(gpointer user_data)
{
    AlbumArtRequest *request = user_data;
    g_free(request->url);
    g_free(request);
}

static void load_album_art(PanelApplication *application, const gchar *picture)
{
    if (picture == NULL || *picture == '\0')
        return;

    gchar *url = home_assistant_client_resolve_url(application->client, picture);
    if (g_strcmp0(url, application->album_art_url) == 0) {
        g_free(url);
        return;
    }
    g_free(application->album_art_url);
    application->album_art_url = url;

    AlbumArtRequest *request = g_new0(AlbumArtRequest, 1);
    request->application = application;
    request->url = g_strdup(url);
    if (!home_assistant_client_get_url(
            application->client, url, G_PRIORITY_LOW, album_art_finished,
            request, album_art_request_free))
        album_art_request_free(request);
}

static void update_player(PanelApplication *application, JsonObject *state)
{
    if (state == NULL)
        return;

    JsonObject *attributes = json_state_attributes(state);
    application->player_playing = g_str_equal(
        json_object_string(state, "state", ""), "playing");
    gdouble position = 0.0;
    gdouble duration = 0.0;
    gdouble volume = 0.0;
    json_object_number(attributes, "media_position", &position);
    json_object_number(attributes, "media_duration", &duration);
    json_object_number(attributes, "volume_level", &volume);

    const gchar *updated_at = json_object_string(
        attributes, "media_position_updated_at", NULL);
    if (application->player_playing && updated_at != NULL) {
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

    application->shuffle_state = json_object_boolean(
        attributes, "shuffle", FALSE);
    const gchar *repeat = json_object_string(attributes, "repeat", "off");
    g_free(application->repeat_state);
    application->repeat_state = g_strdup(repeat);
    panel_ui_set_player(
        application->ui, application->player_playing,
        json_object_string(attributes, "media_title", "Nothing playing"),
        json_object_string(attributes, "media_artist", ""), position, duration,
        volume, application->shuffle_state, repeat);
    load_album_art(application,
                   json_object_string(attributes, "entity_picture", ""));
}

static void update_queue(PanelApplication *application, JsonObject *state)
{
    const gchar *data = json_object_string(
        json_state_attributes(state), "data", NULL);
    if (data == NULL || g_strcmp0(data, application->queue_data) == 0)
        return;

    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, data, -1, NULL) ||
        !JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) {
        g_object_unref(parser);
        return;
    }

    g_free(application->queue_data);
    application->queue_data = g_strdup(data);
    JsonObject *payload = json_node_get_object(json_parser_get_root(parser));
    json_copy_string_array(application->queue_titles,
                           json_optional_array(payload, "titles"));
    json_copy_string_array(application->queue_artists,
                           json_optional_array(payload, "artists"));
    json_copy_string_array(application->queue_ids,
                           json_optional_array(payload, "queue_ids"));
    guint count = MIN(application->queue_titles->len,
                      MIN(application->queue_artists->len,
                          application->queue_ids->len));
    application->queue_selected = json_object_has_member(payload, "current_index")
                                      ? (gint)json_object_get_int_member(
                                            payload, "current_index")
                                      : 0;
    panel_ui_set_queue(application->ui, application->queue_titles,
                       application->queue_artists, count,
                       application->queue_selected);
    g_object_unref(parser);
}

static void update_playlists(PanelApplication *application, JsonObject *state)
{
    JsonObject *attributes = json_state_attributes(state);
    JsonArray *names = json_optional_array(attributes, "names");
    JsonArray *uris = json_optional_array(attributes, "uris");
    if (json_string_array_matches(application->playlist_names, names) &&
        json_string_array_matches(application->playlist_uris, uris))
        return;

    json_copy_string_array(application->playlist_names, names);
    json_copy_string_array(application->playlist_uris, uris);
    guint count = MIN(application->playlist_names->len,
                      application->playlist_uris->len);
    if (application->playlist_selected < 0 ||
        (guint)application->playlist_selected >= count) {
        application->playlist_selected = count > 0 ? 0 : -1;
    }
    panel_ui_set_playlists(application->ui, application->playlist_names, count,
                           application->playlist_selected);
}

static void update_room(PanelApplication *application, guint index,
                        JsonObject *state)
{
    if (state == NULL)
        return;
    gboolean active = g_str_equal(
        json_object_string(state, "state", ""), "on");
    panel_ui_set_room(application->ui, index, active);
}

static void states_finished(guint status_code, GBytes *body,
                            const GError *error, gpointer user_data)
{
    PanelApplication *application = user_data;
    application->poll_in_flight = FALSE;

    if (error != NULL) {
        gchar *message = g_strdup_printf("Offline: %s", error->message);
        panel_ui_set_status(application->ui, message, TRUE);
        g_free(message);
        return;
    }
    if (status_code != 200 || body == NULL) {
        gchar *message = g_strdup_printf("Home Assistant HTTP %u", status_code);
        panel_ui_set_status(application->ui, message, TRUE);
        g_free(message);
        return;
    }

    gsize length = 0;
    const gchar *data = g_bytes_get_data(body, &length);
    GError *parse_error = NULL;
    JsonParser *parser = json_parser_new();
    if (json_parser_load_from_data(parser, data, (gssize)length, &parse_error) &&
        JSON_NODE_HOLDS_ARRAY(json_parser_get_root(parser))) {
        GHashTable *states = g_hash_table_new(g_str_hash, g_str_equal);
        JsonArray *array = json_node_get_array(json_parser_get_root(parser));
        for (guint i = 0; i < json_array_get_length(array); i++) {
            JsonObject *state = json_array_get_object_element(array, i);
            const gchar *entity = json_object_string(state, "entity_id", NULL);
            if (entity != NULL)
                g_hash_table_insert(states, (gpointer)entity, state);
        }
        update_player(application, g_hash_table_lookup(
                                       states,
                                       application->config->player_entity));
        update_queue(application, g_hash_table_lookup(
                                      states,
                                      application->config->queue_entity));
        update_playlists(application, g_hash_table_lookup(
                                          states,
                                          application->config->playlists_entity));
        for (guint i = 0; i < PANEL_ROOM_COUNT; i++) {
            if (application->config->room_entities[i] != NULL) {
                update_room(application, i,
                            g_hash_table_lookup(
                                states,
                                application->config->room_entities[i]));
            }
        }
        g_hash_table_unref(states);
        panel_ui_set_status(application->ui, "Connected", FALSE);
    } else {
        panel_ui_set_status(application->ui,
                            "Invalid Home Assistant response", TRUE);
    }
    g_clear_error(&parse_error);
    g_object_unref(parser);
}

static gboolean poll_states(gpointer user_data)
{
    PanelApplication *application = user_data;
    if (application->poll_in_flight)
        return G_SOURCE_CONTINUE;

    application->poll_in_flight = TRUE;
    if (!home_assistant_client_get_states(
            application->client, states_finished, application)) {
        application->poll_in_flight = FALSE;
        panel_ui_set_status(application->ui,
                            "Invalid Home Assistant URL", TRUE);
    }
    return G_SOURCE_CONTINUE;
}

static PanelApplication *panel_application_new(void)
{
    PanelApplication *application = g_new0(PanelApplication, 1);
    application->queue_selected = -1;
    application->playlist_selected = -1;
    application->repeat_state = g_strdup("off");
    application->queue_titles = g_ptr_array_new_with_free_func(g_free);
    application->queue_artists = g_ptr_array_new_with_free_func(g_free);
    application->queue_ids = g_ptr_array_new_with_free_func(g_free);
    application->playlist_names = g_ptr_array_new_with_free_func(g_free);
    application->playlist_uris = g_ptr_array_new_with_free_func(g_free);
    return application;
}

static void panel_application_free(PanelApplication *application)
{
    if (application->poll_source != 0)
        g_source_remove(application->poll_source);
    home_assistant_client_free(application->client);
    panel_ui_free(application->ui);
    app_config_free(application->config);
    g_ptr_array_unref(application->queue_titles);
    g_ptr_array_unref(application->queue_artists);
    g_ptr_array_unref(application->queue_ids);
    g_ptr_array_unref(application->playlist_names);
    g_ptr_array_unref(application->playlist_uris);
    g_free(application->album_art_url);
    g_free(application->repeat_state);
    g_free(application->queue_data);
    g_free(application);
}

static void activate(GtkApplication *gtk_application, gpointer user_data)
{
    PanelApplication *application = user_data;
    if (application->window != NULL) {
        gtk_window_present(GTK_WINDOW(application->window));
        return;
    }

    panel_ui_install_styles();
    application->window = gtk_application_window_new(gtk_application);
    gtk_window_set_title(GTK_WINDOW(application->window), "T560 Music Panel");
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gtk_window_set_wmclass(GTK_WINDOW(application->window),
                           "t560-music-panel", "T560MusicPanel");
    G_GNUC_END_IGNORE_DEPRECATIONS
    gtk_window_set_default_size(GTK_WINDOW(application->window), 800, 1219);
    gtk_window_maximize(GTK_WINDOW(application->window));

    gchar *failure = NULL;
    application->config = app_config_load(&failure);
    GtkWidget *content = NULL;
    if (application->config == NULL) {
        content = panel_ui_build_config_error(failure);
        g_free(failure);
    } else {
        application->client = home_assistant_client_new(
            application->config->base_url, application->config->token);
        application->ui = panel_ui_new(application->config, handle_ui_event,
                                       application);
        content = panel_ui_build(application->ui);
        poll_states(application);
        application->poll_source = g_timeout_add(
            application->config->poll_interval_ms, poll_states, application);
    }
    gtk_container_add(GTK_CONTAINER(application->window), content);
    gtk_widget_show_all(application->window);
}

int application_run(int argc, char **argv)
{
    g_set_prgname("t560-music-panel");
    g_set_application_name("T560 Music Panel");

    PanelApplication *application = panel_application_new();
    GtkApplication *gtk_application = gtk_application_new(
        APPLICATION_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gtk_application, "activate", G_CALLBACK(activate),
                     application);
    int result = g_application_run(G_APPLICATION(gtk_application), argc, argv);
    panel_application_free(application);
    g_object_unref(gtk_application);
    return result;
}
