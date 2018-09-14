/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * This file is part of Devhelp.
 *
 * Copyright (C) 2004-2008 Imendio AB
 * Copyright (C) 2010 Lanedo GmbH
 * Copyright (C) 2012 Thomas Bechtold <toabctl@gnome.org>
 * Copyright (C) 2018 Sébastien Wilmet <swilmet@gnome.org>
 *
 * Devhelp is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * Devhelp is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Devhelp.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "dh-preferences.h"
#include <glib/gi18n.h>
#include <devhelp/devhelp.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include "devhelp/dh-settings.h"
#include "dh-settings-app.h"

enum {
        COLUMN_BOOK = 0,
        COLUMN_TITLE,
        COLUMN_ID_FOR_REMOVING,
        N_COLUMNS
};

enum {
        COLUMN_DL_TITLE = 0,
        COLUMN_DL_PROGRESS,
        COLUMN_DL_ID,
        N_COLUMNS_DL
};

typedef struct {
        /* Book Shelf tab */
        GtkCheckButton *bookshelf_group_by_language_checkbutton;
        GtkTreeView *bookshelf_view;
        GtkListStore *bookshelf_store;
        GtkListStore *bookshelf_store_downloads;
        GtkListStore *bookshelf_store_usercontrib_downloads;
        GtkTreeView *bookshelf_download_treeview;
        GtkTreeView *bookshelf_download_treeview_usercontrib;
        DhBookList *full_book_list;
        GtkButton *bookshelf_delete_button;

        /* Fonts tab */
        GtkCheckButton *use_system_fonts_checkbutton;
        GtkCheckButton *dark_mode_checkbutton;
        GtkGrid *custom_fonts_grid;
        GtkFontButton *variable_font_button;
        GtkFontButton *fixed_font_button;
        guint      use_system_fonts_id;
        guint      system_var_id;
        guint      system_fixed_id;
        guint      var_id;
        guint      fixed_id;

        SoupWebsocketConnection *dl_ws;
} DhPreferencesPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (DhPreferences, dh_preferences, GTK_TYPE_DIALOG)

static void
dh_preferences_dispose (GObject *object)
{
        DhPreferencesPrivate *priv = dh_preferences_get_instance_private (DH_PREFERENCES (object));

        g_clear_object (&priv->bookshelf_store);
        g_clear_object (&priv->full_book_list);

        G_OBJECT_CLASS (dh_preferences_parent_class)->dispose (object);
}

static void
dh_preferences_response (GtkDialog *dialog,
                         gint       response_id)
{
        gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
dh_preferences_class_init (DhPreferencesClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
        GtkDialogClass *dialog_class = GTK_DIALOG_CLASS (klass);

        object_class->dispose = dh_preferences_dispose;

        dialog_class->response = dh_preferences_response;

        /* Bind class to template */
        gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/devhelp/dh-preferences.ui");

        // Book Shelf tab
        gtk_widget_class_bind_template_child_private (widget_class, DhPreferences, bookshelf_group_by_language_checkbutton);
        gtk_widget_class_bind_template_child_private (widget_class, DhPreferences, bookshelf_view);
        gtk_widget_class_bind_template_child_private (widget_class, DhPreferences, bookshelf_store_downloads);
        gtk_widget_class_bind_template_child_private (widget_class, DhPreferences, bookshelf_store_usercontrib_downloads);
        gtk_widget_class_bind_template_child_private (widget_class, DhPreferences, bookshelf_download_treeview);
        gtk_widget_class_bind_template_child_private (widget_class, DhPreferences, bookshelf_download_treeview_usercontrib);
        gtk_widget_class_bind_template_child_private (widget_class, DhPreferences, bookshelf_delete_button);

        // Fonts tab
        gtk_widget_class_bind_template_child_private (widget_class, DhPreferences, use_system_fonts_checkbutton);
        gtk_widget_class_bind_template_child_private (widget_class, DhPreferences, dark_mode_checkbutton);
        gtk_widget_class_bind_template_child_private (widget_class, DhPreferences, custom_fonts_grid);
        gtk_widget_class_bind_template_child_private (widget_class, DhPreferences, variable_font_button);
        gtk_widget_class_bind_template_child_private (widget_class, DhPreferences, fixed_font_button);
}

static gboolean
is_language_group_active (DhPreferences *prefs,
                          const gchar   *language)
{
        DhPreferencesPrivate *priv = dh_preferences_get_instance_private (prefs);
        DhSettings *settings;
        GList *books;
        GList *l;

        g_return_val_if_fail (language != NULL, FALSE);

        settings = dh_settings_get_default ();
        books = dh_book_list_get_books (priv->full_book_list);

        for (l = books; l != NULL; l = l->next) {
                DhBook *cur_book = DH_BOOK (l->data);

                if (g_strcmp0 (language, dh_book_get_language (cur_book)) != 0)
                        continue;

                if (dh_settings_is_book_enabled (settings, cur_book))
                        return TRUE;
        }

        return FALSE;
}

static gboolean
is_language_group_inconsistent (DhPreferences *prefs,
                                const gchar   *language)
{
        DhPreferencesPrivate *priv = dh_preferences_get_instance_private (prefs);
        DhSettings *settings;
        GList *books;
        GList *l;
        gboolean is_first_book;
        gboolean is_first_book_enabled;

        g_return_val_if_fail (language != NULL, FALSE);

        settings = dh_settings_get_default ();
        books = dh_book_list_get_books (priv->full_book_list);

        is_first_book = TRUE;

        for (l = books; l != NULL; l = l->next) {
                DhBook *cur_book = DH_BOOK (l->data);
                gboolean is_cur_book_enabled;

                if (g_strcmp0 (language, dh_book_get_language (cur_book)) != 0)
                        continue;

                is_cur_book_enabled = dh_settings_is_book_enabled (settings, cur_book);

                if (is_first_book) {
                        is_first_book_enabled = is_cur_book_enabled;
                        is_first_book = FALSE;
                } else if (is_cur_book_enabled != is_first_book_enabled) {
                        /* Inconsistent */
                        return TRUE;
                }
        }

        /* Consistent */
        return FALSE;
}

static void
set_language_group_enabled (DhPreferences *prefs,
                            const gchar   *language,
                            gboolean       enabled)
{
        DhPreferencesPrivate *priv = dh_preferences_get_instance_private (prefs);
        DhSettings *settings;
        GList *books;
        GList *l;

        settings = dh_settings_get_default ();
        books = dh_book_list_get_books (priv->full_book_list);

        dh_settings_freeze_books_disabled_changed (settings);

        for (l = books; l != NULL; l = l->next) {
                DhBook *cur_book = DH_BOOK (l->data);

                if (g_strcmp0 (language, dh_book_get_language (cur_book)) == 0)
                        dh_settings_set_book_enabled (settings, cur_book, enabled);
        }

        dh_settings_thaw_books_disabled_changed (settings);
}

static gboolean
bookshelf_store_changed_foreach_func (GtkTreeModel *model,
                                      GtkTreePath  *path,
                                      GtkTreeIter  *iter,
                                      gpointer      data)
{
        /* Emit ::row-changed for every row. */
        gtk_tree_model_row_changed (model, path, iter);
        return FALSE;
}

/* Have a dumb implementation, normally the performance is not a problem with a
 * small GtkListStore.
 */
static void
bookshelf_store_changed (DhPreferences *prefs)
{
        DhPreferencesPrivate *priv = dh_preferences_get_instance_private (prefs);

        gtk_tree_model_foreach (GTK_TREE_MODEL (priv->bookshelf_store),
                                bookshelf_store_changed_foreach_func,
                                NULL);
}

static void
bookshelf_populate_store (DhPreferences *prefs)
{
        DhPreferencesPrivate *priv = dh_preferences_get_instance_private (prefs);
        DhSettings *settings;
        gboolean group_by_language;
        GList *books;
        GList *l;
        GSList *inserted_languages = NULL;

        gtk_list_store_clear (priv->bookshelf_store);

        settings = dh_settings_get_default ();
        group_by_language = dh_settings_get_group_books_by_language (settings);

        books = dh_book_list_get_books (priv->full_book_list);

        for (l = books; l != NULL; l = l->next) {
                DhBook *book = DH_BOOK (l->data);
                gchar *indented_title = NULL;
                const gchar *title;
                const gchar *language;
		        const gchar *id_for_removing;
                language = dh_book_get_language (book);

                /* Insert book */

                if (group_by_language && !g_str_equal(language, "")) {
                        indented_title = g_strdup_printf ("     %s", dh_book_get_title (book));
                        title = indented_title;
                } else {
                        title = dh_book_get_title (book);
                }
                id_for_removing = dh_book_get_id_for_removing(book);

                gtk_list_store_insert_with_values (priv->bookshelf_store, NULL, -1,
                                                   COLUMN_BOOK, book,
                                                   COLUMN_TITLE, title,
                                                   COLUMN_ID_FOR_REMOVING, id_for_removing,
                                                   -1);

                g_free (indented_title);

                /* Insert language if needed */

                if (!group_by_language)
                        continue;

                if (g_str_equal (language, "")) { // Dash docset
                        continue;
                }
                if (g_slist_find_custom (inserted_languages, language, (GCompareFunc)g_strcmp0) != NULL)
                        /* Already inserted. */
                        continue;

                gtk_list_store_insert_with_values (priv->bookshelf_store, NULL, -1,
                                                   COLUMN_BOOK, NULL,
                                                   COLUMN_TITLE, language,
                                                   COLUMN_ID_FOR_REMOVING, "",
                                                   -1);

                inserted_languages = g_slist_prepend (inserted_languages, g_strdup (language));
        }

        g_slist_free_full (inserted_languages, g_free);
}

static void
bookshelf_scroll_to_top (DhPreferences *prefs)
{
        DhPreferencesPrivate *priv = dh_preferences_get_instance_private (prefs);
        GtkTreePath *path;
        GtkTreeIter iter;

        path = gtk_tree_path_new_first ();

        /* Check if the path exists, if the GtkTreeModel is not empty. */
        if (gtk_tree_model_get_iter (GTK_TREE_MODEL (priv->bookshelf_store), &iter, path)) {
                GtkTreeViewColumn *first_column;

                first_column = gtk_tree_view_get_column (priv->bookshelf_view, 0);

                gtk_tree_view_scroll_to_cell (priv->bookshelf_view,
                                              path, first_column,
                                              TRUE, 0.0, 0.0);
        }
        gtk_tree_path_free (path);
}


static void
preferences_bookshelf_populate_store_downloads (DhPreferences *prefs, GtkListStore *store, char repo)
{
        DhPreferencesPrivate *priv = dh_preferences_get_instance_private (prefs);
        GtkTreeIter  iter;
        SoupSession *session;
        char uri[] = "http://localhost:12340/repo/_/items";
        int i;
        for (i = 0; uri[i] != '_'; ++i);
        uri[i] = repo;
        GHashTable *hash;
        GList *langlist;
        SoupMessage *request;
        SoupMessageBody *body;
        SoupBuffer *buffer;
        const gchar *data;
        gsize length;
        JsonParser *parser;
        JsonNode *root;
        JsonArray *array;
        JsonObject *object;

        parser = json_parser_new();
        session = soup_session_new_with_options(
                "use-thread-context", gtk_true(),
                NULL
        );
        hash = g_hash_table_new(g_str_hash, g_str_equal);
        request = soup_form_request_new_from_hash ("GET", uri, hash);

        soup_session_send_message (session, request);
        g_object_get(request, "response-body", &body, NULL);

        buffer = soup_message_body_flatten(body);
        soup_buffer_get_data(buffer, (const guint8**)&data, &length);
        json_parser_load_from_data(parser, data, length, NULL);
        root = json_parser_get_root(parser);
        array = json_node_get_array(root);

        for (guint i = 0; i < json_array_get_length (array); ++i) {
                object = json_array_get_object_element(array, i);
                gtk_list_store_append (store,
                                       &iter);

                gtk_list_store_set (store,
                                    &iter,
                                    COLUMN_DL_TITLE, json_object_get_string_member(object, "Title"),
                                    COLUMN_DL_PROGRESS, 0,
                                    COLUMN_DL_ID, json_object_get_string_member(object, "Id"),
                                    -1);

        }

        soup_buffer_free (buffer);
        soup_message_body_free (body);
        g_object_unref (parser);
        g_hash_table_unref (hash);
        g_object_unref (request);
        g_object_unref (session);
}

static void
preferences_bookshelf_group_by_language_cb (GObject       *object,
                                            GParamSpec    *pspec,
                                            DhPreferences *prefs)
{
        // TODO
}

static void
bookshelf_populate (DhPreferences *prefs)
{
        DhPreferencesPrivate *priv = dh_preferences_get_instance_private (prefs);

        /* Disconnect the model from the view, it has better performances
         * because the view doesn't listen to all the GtkTreeModel signals.
         */
        gtk_tree_view_set_model (priv->bookshelf_view, NULL);

        bookshelf_populate_store (prefs);

        gtk_tree_view_set_model (priv->bookshelf_view,
                                 GTK_TREE_MODEL (priv->bookshelf_store));

        /* It's maybe a bug in GtkTreeView, but if before calling this function
         * the GtkTreeView is scrolled down, then with the new content the first
         * row will not be completely visible (the GtkTreeView is still a bit
         * scrolled down), even though gtk_list_store_clear() has been called.
         *
         * So to fix this bug, scroll explicitly to the top.
         */
        bookshelf_scroll_to_top (prefs);
}

static void
bookshelf_group_books_by_language_notify_cb (DhSettings    *settings,
                                             GParamSpec    *pspec,
                                             DhPreferences *prefs)
{
        bookshelf_populate (prefs);
}

static void
bookshelf_books_disabled_changed_cb (DhSettings    *settings,
                                     DhPreferences *prefs)
{
        bookshelf_store_changed (prefs);
}

static void
bookshelf_add_book_cb (DhBookList    *full_book_list,
                       DhBook        *book,
                       DhPreferences *prefs)
{
        bookshelf_populate (prefs);
}

static void
bookshelf_remove_book_cb (DhBookList    *full_book_list,
                          DhBook        *book,
                          DhPreferences *prefs)
{
        bookshelf_populate (prefs);
}

static void
bookshelf_row_toggled_cb (GtkCellRendererToggle *cell_renderer,
                          gchar                 *path,
                          DhPreferences         *prefs)
{
        DhPreferencesPrivate *priv = dh_preferences_get_instance_private (prefs);
        GtkTreeIter iter;
        DhBook *book = NULL;
        gchar *title = NULL;

        if (!gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (priv->bookshelf_store),
                                                  &iter,
                                                  path)) {
                return;
        }

        gtk_tree_model_get (GTK_TREE_MODEL (priv->bookshelf_store),
                            &iter,
                            COLUMN_BOOK, &book,
                            COLUMN_TITLE, &title,
                            -1);

        if (book != NULL) {
                DhSettings *settings;
                gboolean enabled;

                settings = dh_settings_get_default ();
                enabled = dh_settings_is_book_enabled (settings, book);
                dh_settings_set_book_enabled (settings, book, !enabled);
        } else {
                const gchar *language = title;
                gboolean enable;

                if (is_language_group_inconsistent (prefs, language))
                        enable = TRUE;
                else
                        enable = !is_language_group_active (prefs, language);

                set_language_group_enabled (prefs, language, enable);
        }

        g_clear_object (&book);
        g_free (title);
}

/* The implementation is simpler with a sort function. Performance is normally
 * not a problem with a small GtkListStore. A previous implementation didn't use
 * a sort function and inserted the books and language groups at the right place
 * directly by walking through the GtkListStore, but it takes a lot of code to
 * do that.
 */
static gint
bookshelf_sort_func (GtkTreeModel *model,
                     GtkTreeIter  *iter_a,
                     GtkTreeIter  *iter_b,
                     gpointer      user_data)
{
        DhSettings *settings;
        DhBook *book_a;
        DhBook *book_b;
        gchar *title_a;
        gchar *title_b;
        const gchar *language_a;
        const gchar *language_b;
        gint ret;

        gtk_tree_model_get (model,
                            iter_a,
                            COLUMN_BOOK, &book_a,
                            COLUMN_TITLE, &title_a,
                            -1);

        gtk_tree_model_get (model,
                            iter_b,
                            COLUMN_BOOK, &book_b,
                            COLUMN_TITLE, &title_b,
                            -1);

        settings = dh_settings_get_default ();
        if (!dh_settings_get_group_books_by_language (settings)) {
                ret = dh_book_cmp_by_title (book_a, book_b);
                goto out;
        }

        if (book_a != NULL)
                language_a = dh_book_get_language (book_a);
        else
                language_a = title_a;

        if (book_b != NULL)
                language_b = dh_book_get_language (book_b);
        else
                language_b = title_b;

        ret = g_strcmp0 (language_a, language_b);
        if (ret != 0) {
                /* Different language. */
                goto out;
        }

        /* Same language. */

        if (book_a == NULL) {
                if (book_b == NULL) {
                        /* Duplicated language group, should not happen. */
                        g_warn_if_reached ();
                        ret = 0;
                } else {
                        /* @iter_a is the language group and @iter_b is a book
                         * inside that language group.
                         */
                        ret = -1;
                }
        } else if (book_b == NULL) {
                /* @iter_b is the language group and @iter_a is a book inside
                 * that language group.
                 */
                ret = 1;
        } else {
                ret = dh_book_cmp_by_title (book_a, book_b);
        }

out:
        g_clear_object (&book_a);
        g_clear_object (&book_b);
        g_free (title_a);
        g_free (title_b);
        return ret;
}

static void
preferences_bookshelf_remove_book (GObject* obj, GdkEvent *ev, gpointer user_data)
{
        DhPreferencesPrivate *priv = user_data;
        GtkTreeSelection *selection = gtk_tree_view_get_selection(priv->bookshelf_view);
        SoupSession *session;
        SoupMessage *request;
        GtkTreeModel *model;
        GtkTreeIter iter;
        gchar *uri, *id;

        gtk_tree_selection_get_selected(selection, &model, &iter);
        gtk_tree_model_get(GTK_TREE_MODEL (priv->bookshelf_store),
                           &iter,
                           COLUMN_ID_FOR_REMOVING, &id, -1);
        session = soup_session_new();
        uri = g_strjoin("/", "http://localhost:12340/item", id);
        request = soup_message_new ("DELETE", uri);
        soup_session_send_message (session, request);

        g_object_unref(request);
        g_object_unref(session);

        dh_book_list_refresh (dh_book_list_get_default(
                -1 // at this point it should be already created outside
        ));
}

static void
preferences_bookshelf_tree_selection_changed_cb (GtkTreeSelection *selection,
                                                 gpointer user_data)
{
        DhPreferencesPrivate *priv = user_data;
        GtkTreeModel *model;
        GtkTreeIter iter;
        gchar *id;

        if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
                gtk_tree_model_get(GTK_TREE_MODEL (priv->bookshelf_store),
                                   &iter,
                                   COLUMN_ID_FOR_REMOVING, &id, -1);
                if (g_str_equal(id, "")) {
                        gtk_widget_set_sensitive(priv->bookshelf_delete_button, FALSE);
                } else {
                        gtk_widget_set_sensitive(priv->bookshelf_delete_button, TRUE);
                }
        } else {
                gtk_widget_set_sensitive(priv->bookshelf_delete_button, FALSE);
        }

}


static void
websocket_message_cb (SoupWebsocketConnection *self,
                      gint                     type,
                      GBytes                  *message,
                      gpointer                 user_data) {
        DhPreferencesPrivate *priv = user_data;
        JsonParser *parser;
        JsonNode *root;
        JsonObject *object;
        GError *error = NULL;
        size_t len;
        gint64 received, total;
        gboolean next, first = true;
        GtkTreeIter iter;
        GtkListStore *store;
        const gchar *docset, *iter_docset, *repo_id;

        const gchar* msg = g_bytes_get_data(message, &len);
        if (len < 2) {
                return;
        }

        parser = json_parser_new();
        json_parser_load_from_data(parser, msg, len, &error);

        root = json_parser_get_root(parser);
        object = json_node_get_object(root);
        repo_id = json_object_get_string_member(object, "RepoId");
        docset = json_object_get_string_member(object, "Docset");
        received = json_object_get_int_member(object, "Received");
        total = json_object_get_int_member(object, "Total");

        if (g_str_equal(repo_id, "com.kapeli")){
                store = priv->bookshelf_store_downloads;
        }  else {
                store = priv->bookshelf_store_usercontrib_downloads;
        }

        next = gtk_tree_model_get_iter_first(store, &iter);
        while (next) {
                gtk_tree_model_get(GTK_TREE_MODEL (store),
                                   &iter,
                                   COLUMN_DL_TITLE, &iter_docset,
                                   -1);
                if (g_str_equal (iter_docset, docset)) {
                        gtk_list_store_set(store,
                                           &iter,
                                           COLUMN_DL_PROGRESS, (gint)(((guint64)100) * ((guint64) received) / ((guint64) total)),
                                           -1);
                        break;
                }
                g_free(iter_docset);
                next = gtk_tree_model_iter_next(store, &iter);
                if (first && !next) {
                        first = false;
                        next = gtk_tree_model_get_iter_first(store, &iter);
                }
        }
        if (g_str_equal(repo_id, "com.kapeli")) {
                gtk_widget_queue_draw(priv->bookshelf_download_treeview);
        } else {
                gtk_widget_queue_draw(priv->bookshelf_download_treeview_usercontrib);
        }

        if (received == total) {
            dh_book_list_refresh (dh_book_list_get_default(
                    -1  // at this point it should be already created outside
            ));
        }

        g_object_unref(parser);
}

static void
websocket_closed_cb (SoupWebsocketConnection *self,
                     gpointer                 user_data)
{
        DhPreferencesPrivate *priv = user_data;
        priv->dl_ws = NULL;
}

static void
websocket_connected_cb (GObject *source_object,
                        GAsyncResult *res,
                        gpointer user_data)
{
        DhPreferencesPrivate *priv = user_data;
        priv->dl_ws = soup_session_websocket_connect_finish(source_object, res, NULL);
        g_signal_connect(priv->dl_ws, "message", (GCallback*)websocket_message_cb, user_data);
        g_signal_connect(priv->dl_ws, "closed", (GCallback*)websocket_closed_cb, user_data);
}

static void
open_progress_ws(DhPreferences *prefs)
{
        DhPreferencesPrivate *priv = dh_preferences_get_instance_private (prefs);

        SoupSession *session;
        const char *uri;
        GHashTable *hash;
        SoupMessage *request;

        session = soup_session_new();
        uri = "ws://localhost:12340/download_progress";
        hash = g_hash_table_new(g_str_hash, g_str_equal);
        request = soup_form_request_new_from_hash ("GET", uri, hash);

        char **protocols = malloc(sizeof(char*));
        *protocols = NULL;
        soup_session_websocket_connect_async(session,
                                             request,
                                             "http://localhost/",
                                             protocols,
                                             NULL,
                                             websocket_connected_cb,
                                             priv);
        g_hash_table_unref(hash);
        free(protocols);
}

gboolean download_start(GtkTreeView *tv, GtkTreePath *path, GtkTreeViewColumn *column, DhPreferences *prefs)
{
        DhPreferencesPrivate *priv = dh_preferences_get_instance_private (prefs);
        GtkTreeSelection *selection = gtk_tree_view_get_selection(tv);
        GtkTreeModel *model;
        GtkTreeIter iter;
        gchar *id, *json;
        SoupSession *session;
        const char *uri;
        SoupMessage *request;

        if (priv->dl_ws != NULL) {
               return FALSE;
        }

        gtk_tree_selection_get_selected (selection, &model, &iter);
        gtk_tree_model_get(model, &iter, COLUMN_DL_ID, &id, -1);

        session = soup_session_new();
        uri = "http://localhost:12340/item";
        request = soup_message_new ("POST", uri);
        if (tv == priv->bookshelf_download_treeview) {
               json = g_strjoin("", "{\"id\":\"", id, "\", \"repo\": \"com.kapeli\"}", NULL);
        } else {
               json = g_strjoin("", "{\"id\":\"", id, "\", \"repo\": \"com.kapeli.contrib\"}", NULL);
        }

        soup_message_set_request(request,
                                 "application/json",
                                 SOUP_MEMORY_COPY,
                                 json,
                                 strlen(json));

        soup_session_send_message (session, request);

        char **protocols = malloc(sizeof(char*));
        *protocols = NULL;

        g_free(json);
        g_object_unref(request);
        g_object_unref(session);

        open_progress_ws (prefs);
        return FALSE;
}


preferences_bookshelf_refresh_cb (GObject* object, DhPreferences  *prefs)
{
        DhPreferencesPrivate *priv = dh_preferences_get_instance_private (prefs);
        gtk_list_store_clear (priv->bookshelf_store);
        gtk_list_store_clear (priv->bookshelf_store_downloads);
        gtk_list_store_clear (priv->bookshelf_store_usercontrib_downloads);
        bookshelf_populate (prefs);
        preferences_bookshelf_populate_store_downloads (
                prefs, priv->bookshelf_store_downloads, '1'
        );
        preferences_bookshelf_populate_store_downloads (
                prefs, priv->bookshelf_store_usercontrib_downloads, '2'
        );
}


init_bookshelf_store (DhPreferences *prefs)
{
        DhPreferencesPrivate *priv = dh_preferences_get_instance_private (prefs);

        g_assert (priv->bookshelf_store == NULL);
        priv->bookshelf_store = gtk_list_store_new (N_COLUMNS,
                                                    DH_TYPE_BOOK,
                                                    G_TYPE_STRING, /* Title */
                                                    G_TYPE_STRING, /* id_for_removing */
                                                    PANGO_TYPE_WEIGHT);

        gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE (priv->bookshelf_store),
                                                 bookshelf_sort_func,
                                                 NULL, NULL);
        gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (priv->bookshelf_store),
                                              GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,
                                              GTK_SORT_ASCENDING);
}

static void
bookshelf_cell_data_func_toggle (GtkTreeViewColumn *column,
                                 GtkCellRenderer   *cell,
                                 GtkTreeModel      *model,
                                 GtkTreeIter       *iter,
                                 gpointer           data)
{
        DhPreferences *prefs = DH_PREFERENCES (data);
        DhBook *book = NULL;
        gchar *title = NULL;
        gboolean active;
        gboolean inconsistent;

        gtk_tree_model_get (model,
                            iter,
                            COLUMN_BOOK, &book,
                            COLUMN_TITLE, &title,
                            -1);

        if (book != NULL) {
                DhSettings *settings = dh_settings_get_default ();

                active = dh_settings_is_book_enabled (settings, book);
                inconsistent = FALSE;
        } else {
                active = is_language_group_active (prefs, title);
                inconsistent = is_language_group_inconsistent (prefs, title);
        }

        g_object_set (cell,
                      "active", active,
                      "inconsistent", inconsistent,
                      NULL);

        g_clear_object (&book);
        g_free (title);
}

static void
bookshelf_cell_data_func_text (GtkTreeViewColumn *column,
                               GtkCellRenderer   *cell,
                               GtkTreeModel      *model,
                               GtkTreeIter       *iter,
                               gpointer           data)
{
        DhBook *book = NULL;
        gchar *title = NULL;
        PangoWeight weight;

        gtk_tree_model_get (model,
                            iter,
                            COLUMN_BOOK, &book,
                            COLUMN_TITLE, &title,
                            -1);

        if (book != NULL)
                weight = PANGO_WEIGHT_NORMAL;
        else
                weight = PANGO_WEIGHT_BOLD; /* For the language group. */

        g_object_set (cell,
                      "text", title,
                      "weight", weight,
                      NULL);

        g_clear_object (&book);
        g_free (title);
}


static void
init_bookshelf_view (DhPreferences *prefs)
{
        DhPreferencesPrivate *priv = dh_preferences_get_instance_private (prefs);
        GtkCellRenderer *cell_renderer_toggle;
        GtkCellRenderer *cell_renderer_text;
        GtkTreeViewColumn *column;

        /* Title column */
        cell_renderer_text = gtk_cell_renderer_text_new ();
        gtk_tree_view_insert_column_with_data_func(priv->bookshelf_view,
                                                   -1,
                                                   _("Title"),
                                                   cell_renderer_text,
                                                   bookshelf_cell_data_func_text,
                                                   NULL, NULL);
}

static void
init_bookshelf_tab (DhPreferences *prefs)
{
        DhPreferencesPrivate *priv = dh_preferences_get_instance_private (prefs);
        DhBookListBuilder *builder;
        DhSettings *settings;

        g_assert (priv->full_book_list == NULL);

        builder = dh_book_list_builder_new ();
        dh_book_list_builder_add_default_sub_book_lists (builder, gtk_widget_get_scale_factor(GTK_WIDGET(prefs)));
        /* Do not call dh_book_list_builder_read_books_disabled_setting(), we
         * need the full list.
         */
        priv->full_book_list = dh_book_list_builder_create_object (builder);
        g_object_unref (builder);

        init_bookshelf_store (prefs);
        init_bookshelf_view (prefs);

        settings = dh_settings_get_default ();

        g_object_bind_property (settings, "group-books-by-language",
                                priv->bookshelf_group_by_language_checkbutton, "active",
                                G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

        g_signal_connect_object (settings,
                                 "notify::group-books-by-language",
                                 G_CALLBACK (bookshelf_group_books_by_language_notify_cb),
                                 prefs,
                                 0);

        g_signal_connect_object (settings,
                                 "books-disabled-changed",
                                 G_CALLBACK (bookshelf_books_disabled_changed_cb),
                                 prefs,
                                 0);

	// !!!!!
        g_signal_connect_object (dh_book_list_get_default(gtk_widget_get_scale_factor(GTK_WIDGET(prefs))),
                                 "refresh",
                                 G_CALLBACK (preferences_bookshelf_refresh_cb),
                                 prefs,
                                 0);

        /* setup GSettings bindings */
        g_signal_connect (gtk_tree_view_get_selection(priv->bookshelf_view),
                          "changed",
                          G_CALLBACK (preferences_bookshelf_tree_selection_changed_cb),
                          priv);

        preferences_bookshelf_populate_store_downloads (
                prefs, priv->bookshelf_store_downloads, '1'
        );
        preferences_bookshelf_populate_store_downloads (
                prefs, priv->bookshelf_store_usercontrib_downloads, '2'
        );

        g_signal_connect (priv->bookshelf_download_treeview,
                          "row-activated",
                          G_CALLBACK (download_start),
                          prefs);

        g_signal_connect (priv->bookshelf_download_treeview_usercontrib,
                          "row-activated",
                          G_CALLBACK (download_start),
                          prefs);

        g_signal_connect (priv->bookshelf_delete_button,
                          "button-press-event",
                          G_CALLBACK (preferences_bookshelf_remove_book),
                          priv);
        g_signal_connect_object (priv->full_book_list,
                                 "add-book",
                                 G_CALLBACK (bookshelf_add_book_cb),
                                 prefs,
                                 G_CONNECT_AFTER);

        g_signal_connect_object (priv->full_book_list,
                                 "remove-book",
                                 G_CALLBACK (bookshelf_remove_book_cb),
                                 prefs,
                                 G_CONNECT_AFTER);

        bookshelf_populate (prefs);
}

static void
init_fonts_tab (DhPreferences *prefs)
{
        DhPreferencesPrivate *priv = dh_preferences_get_instance_private (prefs);
        DhSettings *settings;

        settings = dh_settings_get_default ();

        g_object_bind_property (settings, "use-system-fonts",
                                priv->use_system_fonts_checkbutton, "active",
                                G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

        g_object_bind_property (priv->use_system_fonts_checkbutton, "active",
                                priv->custom_fonts_grid, "sensitive",
                                G_BINDING_DEFAULT |
                                G_BINDING_SYNC_CREATE |
                                G_BINDING_INVERT_BOOLEAN);

        g_object_bind_property (settings, "dark-mode",
                                priv->dark_mode_checkbutton, "active",
                                G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

        g_object_bind_property (settings, "variable-font",
                                priv->variable_font_button, "font",
                                G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

        g_object_bind_property (settings, "fixed-font",
                                priv->fixed_font_button, "font",
                                G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
}

static void
dh_preferences_init (DhPreferences *prefs)
{
        DhPreferencesPrivate *priv;
	priv = dh_preferences_get_instance_private (prefs);
        priv->dl_ws = NULL;

        gtk_widget_init_template (GTK_WIDGET (prefs));

        gtk_window_set_destroy_with_parent (GTK_WINDOW (prefs), TRUE);

        init_bookshelf_tab (prefs);
        init_fonts_tab (prefs);
}

void
dh_preferences_show_dialog (GtkWindow *parent)
{
        static GtkWindow *prefs = NULL;

        g_return_if_fail (GTK_IS_WINDOW (parent));

        if (prefs == NULL) {
                prefs = g_object_new (DH_TYPE_PREFERENCES,
                                      "modal", TRUE,
                                      "use-header-bar", TRUE,
                                      NULL);

                g_signal_connect (prefs,
                                  "destroy",
                                  G_CALLBACK (gtk_widget_destroyed),
                                  &prefs);
        }

        if (parent != gtk_window_get_transient_for (prefs))
                gtk_window_set_transient_for (prefs, parent);

        gtk_window_present (prefs);
}
