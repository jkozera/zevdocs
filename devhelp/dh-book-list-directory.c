/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * This file is part of Devhelp.
 *
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

#include <cairo/cairo-gobject.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include "dh-book-list-directory.h"
#include "dh-book-manager.h"
#include "dh-util-lib.h"

/**
 * SECTION:dh-book-list-directory
 * @Title: DhBookListDirectory
 * @Short_description: Subclass of #DhBookList containing the #DhBook's in one
 *   directory
 *
 * #DhBookListDirectory is a subclass of #DhBookList containing the #DhBook's in
 * #DhBookListDirectory:directory. In that directory, each book must be in a
 * direct sub-directory, with the Devhelp index file as a direct child of that
 * sub-directory.
 *
 * For example if #DhBookListDirectory:directory is "/usr/share/gtk-doc/html/",
 * and if there is an index file at
 * "/usr/share/gtk-doc/html/glib/glib.devhelp2", #DhBookListDirectory will
 * contain a #DhBook for that index file.
 *
 * Additionally the name of (1) the sub-directory and (2) the index file minus
 * its extension, must match ("glib" in the example).
 *
 * See #DhBook for the list of allowed Devhelp index file extensions
 * ("*.devhelp2" in the example).
 *
 * #DhBookListDirectory listens to the #DhBook #DhBook::deleted and
 * #DhBook::updated signals, to remove the #DhBook or to re-create it. And
 * #DhBookListDirectory contains a #GFileMonitor on the
 * #DhBookListDirectory:directory to add new #DhBook's when they are installed.
 * But note that those #GFileMonitor's are not guaranteed to work perfectly,
 * recreating the #DhBookListDirectory (or restarting the application) may be
 * needed to see all the index files after filesystem changes in
 * #DhBookListDirectory:directory.
 */

#define NEW_POSSIBLE_BOOK_TIMEOUT_SECS 5

typedef struct {
        DhBookListDirectory *list_directory; /* unowned */
        GFile *book_directory;
        guint timeout_id;
} NewPossibleBookData;

typedef struct {
        GFile *directory;
        GFileMonitor *directory_monitor;

        /* List of NewPossibleBookData* */
        gint scale;
        GSList *new_possible_books_data;
} DhBookListDirectoryPrivate;

enum {
        PROP_0,
        PROP_SCALE,    // must be set before directory
        PROP_DIRECTORY,
        N_PROPERTIES
};

/* List of unowned DhBookListDirectory*. */
static GList *instances;

static GParamSpec *properties[N_PROPERTIES];

G_DEFINE_TYPE_WITH_PRIVATE (DhBookListDirectory, dh_book_list_directory, DH_TYPE_BOOK_LIST)

/* Prototypes */
static gboolean create_book_from_index_file (DhBookListDirectory *list_directory,
                                             GFile               *index_file);

static NewPossibleBookData *
new_possible_book_data_new (DhBookListDirectory *list_directory,
                            GFile               *book_directory)
{
        NewPossibleBookData *data;

        data = g_new0 (NewPossibleBookData, 1);
        data->list_directory = list_directory;
        data->book_directory = g_object_ref (book_directory);

        return data;
}

static void
new_possible_book_data_free (gpointer _data)
{
        NewPossibleBookData *data = _data;

        if (data == NULL)
                return;

        g_clear_object (&data->book_directory);

        if (data->timeout_id != 0)
                g_source_remove (data->timeout_id);

        g_free (data);
}

static void
book_deleted_cb (DhBook              *book,
                 DhBookListDirectory *list_directory)
{
        dh_book_list_remove_book (DH_BOOK_LIST (list_directory), book);
}

static void
book_updated_cb (DhBook              *book,
                 DhBookListDirectory *list_directory)
{
        GFile *index_file;

        /* Re-create the DhBook to parse again the index file. */

        index_file = dh_book_get_index_file (book);
        g_object_ref (index_file);

        dh_book_list_remove_book (DH_BOOK_LIST (list_directory), book);

        create_book_from_index_file (list_directory, index_file);
        g_object_unref (index_file);
}

/* Returns TRUE if "successful", FALSE if the next possible index file in the
 * book directory needs to be tried.
 */
static gboolean
create_book_from_index_file (DhBookListDirectory *list_directory,
                             GFile               *index_file)
{
        GList *books;
        GList *l;
        DhBook *book;

        books = dh_book_list_get_books (DH_BOOK_LIST (list_directory));

        /* Check if a DhBook at the same location has already been loaded. */
        for (l = books; l != NULL; l = l->next) {
                DhBook *cur_book = DH_BOOK (l->data);
                GFile *cur_index_file;

                cur_index_file = dh_book_get_index_file (cur_book);

                if (g_file_equal (index_file, cur_index_file))
                        return TRUE;
        }

        book = dh_book_new (index_file);
        if (book == NULL)
                return FALSE;

        /* Check if book with same ID was already loaded (we need to force
         * unique book IDs).
         */
        if (g_list_find_custom (books, book, (GCompareFunc)dh_book_cmp_by_id) != NULL) {
                g_object_unref (book);
                return TRUE;
        }

        g_signal_connect_object (book,
                                 "deleted",
                                 G_CALLBACK (book_deleted_cb),
                                 list_directory,
                                 0);

        g_signal_connect_object (book,
                                 "updated",
                                 G_CALLBACK (book_updated_cb),
                                 list_directory,
                                 0);

        dh_book_list_add_book (DH_BOOK_LIST (list_directory), book);
        g_object_unref (book);

        return TRUE;
}

/* @book_directory is a directory containing a single book, with the index file
 * as a direct child.
 */
static void
create_book_from_book_directory (DhBookListDirectory *list_directory,
                                 GFile               *book_directory)
{
        GSList *possible_index_files;
        GSList *l;

        possible_index_files = _dh_util_get_possible_index_files (book_directory);

        for (l = possible_index_files; l != NULL; l = l->next) {
                GFile *index_file = G_FILE (l->data);

                if (create_book_from_index_file (list_directory, index_file))
                        break;
        }

        g_slist_free_full (possible_index_files, g_object_unref);
}

static gboolean
new_possible_book_timeout_cb (gpointer user_data)
{
        NewPossibleBookData *data = user_data;
        DhBookListDirectoryPrivate *priv = dh_book_list_directory_get_instance_private (data->list_directory);

        data->timeout_id = 0;

        create_book_from_book_directory (data->list_directory, data->book_directory);

        priv->new_possible_books_data = g_slist_remove (priv->new_possible_books_data, data);
        new_possible_book_data_free (data);

        return G_SOURCE_REMOVE;
}

static void
books_directory_changed_cb (GFileMonitor        *directory_monitor,
                            GFile               *file,
                            GFile               *other_file,
                            GFileMonitorEvent    event_type,
                            DhBookListDirectory *list_directory)
{
        DhBookListDirectoryPrivate *priv = dh_book_list_directory_get_instance_private (list_directory);
        NewPossibleBookData *data;

        /* With the GFileMonitor here we only handle events for new directories
         * created. Book deletions and updates are handled by the GFileMonitor
         * in each DhBook object.
         */
        if (event_type != G_FILE_MONITOR_EVENT_CREATED)
                return;

        data = new_possible_book_data_new (list_directory, file);

        /* We add a timeout of several seconds so that we give time to the whole
         * documentation to get installed. If we don't do this, we may end up
         * trying to add the new book when even the *.devhelp2 index file is not
         * installed yet.
         */
        data->timeout_id = g_timeout_add_seconds (NEW_POSSIBLE_BOOK_TIMEOUT_SECS,
                                                  new_possible_book_timeout_cb,
                                                  data);

        priv->new_possible_books_data = g_slist_prepend (priv->new_possible_books_data, data);
}

static void
monitor_books_directory (DhBookListDirectory *list_directory)
{
        GError *error = NULL;
        DhBookListDirectoryPrivate *priv = dh_book_list_directory_get_instance_private (list_directory);

        g_assert (priv->directory_monitor == NULL);
        priv->directory_monitor = g_file_monitor_directory (priv->directory,
                                                            G_FILE_MONITOR_NONE,
                                                            NULL,
                                                            &error);

        if (error != NULL) {
                gchar *parse_name;

                parse_name = g_file_get_parse_name (priv->directory);

                g_warning ("Failed to create file monitor on directory “%s”: %s",
                           parse_name,
                           error->message);

                g_free (parse_name);
                g_clear_error (&error);
        }

        if (priv->directory_monitor != NULL) {
                g_signal_connect_object (priv->directory_monitor,
                                         "changed",
                                         G_CALLBACK (books_directory_changed_cb),
                                         list_directory,
                                         0);
        }
}

static gboolean
create_book_from_json_object (DhBookListDirectory *book_dir,
                              JsonObject    *object)
{
        DhBookListDirectoryPrivate *priv;
        DhBook *book;

        priv = dh_book_list_directory_get_instance_private (book_dir);

        book = dh_book_new_from_json (object, priv->scale);
        if (book == NULL)
                return FALSE;


        dh_book_list_add_book (DH_BOOK_LIST (book_dir), book);

        g_signal_connect_object (book,
                                 "deleted",
                                 G_CALLBACK (book_deleted_cb),
                                 book_dir,
                                 0);

        g_signal_connect_object (book,
                                 "updated",
                                 G_CALLBACK (book_updated_cb),
                                 book_dir,
                                 0);

        return TRUE;
}

static void
print_doc (JsonArray *array,
           guint index_,
           JsonNode *element_node,
           gpointer user_data)
{
        JsonObject *object = json_node_get_object(element_node);

        DhBookManager *manager = DH_BOOK_LIST_DIRECTORY (user_data);

        create_book_from_json_object (manager, object);
}

static void
find_books (DhBookListDirectory *list_directory)
{
        SoupSession *session;
        const char *uri;
        GHashTable *hash;
        SoupMessage *request;
        SoupMessageBody *body;
        SoupBuffer *buffer;
        const gchar *data;
        gsize length;
        JsonParser *parser;
        JsonNode *root;
        JsonArray *array;

        GList* books = dh_book_list_get_books (DH_BOOK_LIST (list_directory));

        g_list_free_full (books, g_object_unref);
        dh_book_list_set_books(DH_BOOK_LIST (list_directory), NULL);

        parser = json_parser_new();
        session = soup_session_new();
        uri = "http://localhost:12340/item";
        hash = g_hash_table_new(g_str_hash, g_str_equal);
        request = soup_form_request_new_from_hash ("GET", uri, hash);

        soup_session_send_message (session, request);
        g_object_get(request, "response-body", &body, NULL);

        buffer = soup_message_body_flatten(body);
        soup_buffer_get_data(buffer, (const guint8**)&data, &length);
        json_parser_load_from_data(parser, data, length, NULL);
        root = json_parser_get_root(parser);
        array = json_node_get_array(root);

        json_array_foreach_element(array, print_doc, list_directory);

        soup_buffer_free (buffer);
        g_object_unref(parser);
        soup_message_body_free (body);
        g_hash_table_unref (hash);
        g_object_unref (request);
        g_object_unref (session);

        /// https://cdn.sstatic.net/Sites/stackoverflow/img/favicon.ico?v=4f32ecc8f43d
        DhBookManager *manager = list_directory;
        const char *favicons[] = {
                "iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAMAAAAoLQ9TAAAABGdBTUEAALGPC/xhBQAAACBjSFJNAAB6JgAAgIQAAPoAAACA6AAAdTAAAOpgAAA6mAAAF3CculE8AAAAb1BMVEUAAADycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAnycAmeo6nycAn////KsYcjAAAAInRSTlMAHcJkJ+gjSWvIAzriibZ+Exg2oHdGUYIPcdbTPaXvL129TyOK4gAAAAFiS0dEJLQG+ZkAAAAHdElNRQfiCRAQLzjsaQZEAAAAbUlEQVQY022OyQ6AIAxEBxVXxH1fUPz/f/QgpIT4Tp2XaVrAwuARhJ6IeOykMAHSLCdRiDKHrJxKLQPW8NakuCuAng/jZMS8rFs67yJyVvbj7BS9cPQJqxcS6iqlOG8btbnT2knj+SDhdTWBH16iswcK9IKYxwAAACV0RVh0ZGF0ZTpjcmVhdGUAMjAxOC0wOS0xNlQxNjo0Nzo1OCswMjowMHenmG0AAAAldEVYdGRhdGU6bW9kaWZ5ADIwMTgtMDktMTZUMTY6NDc6NTYrMDI6MDBWxVuMAAAAAElFTkSuQmCC",
                "iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAMAAABEpIrGAAAABGdBTUEAALGPC/xhBQAAACBjSFJNAAB6JgAAgIQAAPoAAACA6AAAdTAAAOpgAAA6mAAAF3CculE8AAABQVBMVEX////0gCSeo6n0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCT0gCSeo6n///8Qpy7TAAAAaHRSTlMAAAA0yCwP79sQU/63AyqCiJb4ZwK0Vh/nogrY9S4YwNMmKPDeFATxVE27WkxQjgV+7txwJM/DHK7zZsLylAif6UYjMkli9+MVfOTZbD1KWylYx3i99MUxo+YNuPnNmmpXibrtHlGBEoEFhGAAAAABYktHRACIBR1IAAAAB3RJTUUH4gkQEC847GkGRAAAARdJREFUOMut0WdTwkAQBmA2SFCPIGCwF2LDSiwoNgRRREWx995f/v8f8ETHYZIlGRnfDzdzN8/s7e15PP8WYlMn8Db4HIHqR2OTY4VmgYBWC2hBubQAoVogHGkl0qMQbTxo7wA6u0jtRk8vX6GvH4gZNDCIoWG+yZGQQHyUxgTGNRuYmNTlOuWHSJjTQMIK1BnMzqlEyXlgIbW4tGwFwRUAq2mD1jJAdj3H9LCRiQAisKnntwop/hXbO7uyTHFv32efQ+ng8GvA5tGxJCfMJE+Bs/N0/sIk7+XVNQNu4qjk9u7+IWn5TUVR5E43Hp+eXyqoZAXl8u9nv769Rwsf9H3CgZ8wAFWpD7he4QLsUaqm5A7Y/AU45BMkqFCzuSQj1gAAACV0RVh0ZGF0ZTpjcmVhdGUAMjAxOC0wOS0xNlQxNjo0Nzo1OCswMjowMHenmG0AAAAldEVYdGRhdGU6bW9kaWZ5ADIwMTgtMDktMTZUMTY6NDc6NTYrMDI6MDBWxVuMAAAAAElFTkSuQmCC",
        };
        const char *rawjson = g_strdup_printf(
                "{"
                "\"Title\": \"Stack Overflow\","
                "\"Id\": \"stackoverflow\","
                "\"SourceId\": \"com.stackoverflow\","
                "\"Icon\": \"%s\","
                "\"Icon2x\": \"%s\","
                "\"Language\": \"\""
                "}",
                favicons[0],
                favicons[1],
                NULL
        );
        parser = json_parser_new();

        json_parser_load_from_data(parser, rawjson, strlen(rawjson), NULL);

        DhBook *book = dh_book_new_from_json(json_node_get_object(json_parser_get_root(parser)), 1);
        dh_book_list_add_book (DH_BOOK_LIST (list_directory), book);

        g_object_unref(parser);
        g_free(rawjson);
}

static void
set_directory (DhBookListDirectory *list_directory,
               GFile               *directory)
{
        DhBookListDirectoryPrivate *priv = dh_book_list_directory_get_instance_private (list_directory);
        g_assert (priv->directory == NULL);
        g_return_if_fail (G_IS_FILE (directory));

        priv->directory = g_object_ref (directory);
        find_books (list_directory);
}

void
dh_book_list_directory_refresh (DhBookList * object)
{
        DhBookListDirectory *list_directory = DH_BOOK_LIST_DIRECTORY (object);

        find_books (list_directory);

        g_signal_emit_by_name (list_directory, "refresh");
}


static void
dh_book_list_directory_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
        DhBookListDirectory *list_directory = DH_BOOK_LIST_DIRECTORY (object);
        DhBookListDirectoryPrivate *priv = dh_book_list_directory_get_instance_private (object);

        switch (prop_id) {
                case PROP_DIRECTORY:
                        g_value_set_object (value, dh_book_list_directory_get_directory (list_directory));
                        break;

                case PROP_SCALE:
                        g_value_set_object (value, priv->scale);
                        break;

                default:
                        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                        break;
        }
}

static void
dh_book_list_directory_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
        DhBookListDirectory *list_directory = DH_BOOK_LIST_DIRECTORY (object);
        DhBookListDirectoryPrivate *priv = dh_book_list_directory_get_instance_private (object);

        switch (prop_id) {
                case PROP_DIRECTORY:
                        set_directory (list_directory, g_value_get_object (value));
                        break;

                case PROP_SCALE:
                        priv->scale = g_value_get_int(value);
                        break;

                default:
                        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                        break;
        }
}

static void
dh_book_list_directory_dispose (GObject *object)
{
        DhBookListDirectory *list_directory = DH_BOOK_LIST_DIRECTORY (object);
        DhBookListDirectoryPrivate *priv = dh_book_list_directory_get_instance_private (list_directory);

        g_clear_object (&priv->directory);
        g_clear_object (&priv->directory_monitor);

        g_slist_free_full (priv->new_possible_books_data, new_possible_book_data_free);
        priv->new_possible_books_data = NULL;

        G_OBJECT_CLASS (dh_book_list_directory_parent_class)->dispose (object);
}

static void
dh_book_list_directory_finalize (GObject *object)
{
        DhBookListDirectory *list_directory = DH_BOOK_LIST_DIRECTORY (object);

        instances = g_list_remove (instances, list_directory);

        G_OBJECT_CLASS (dh_book_list_directory_parent_class)->finalize (object);
}

static void
dh_book_list_directory_class_init (DhBookListDirectoryClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = dh_book_list_directory_get_property;
        object_class->set_property = dh_book_list_directory_set_property;
        object_class->dispose = dh_book_list_directory_dispose;
        object_class->finalize = dh_book_list_directory_finalize;

        DhBookListClass *list_class = DH_BOOK_LIST_CLASS (klass);
        list_class->refresh = dh_book_list_directory_refresh;

        /**
         * DhBookListDirectory:directory:
         *
         * The directory, as a #GFile, containing a set of Devhelp books.
         *
         * Since: 3.30
         */
        properties[PROP_DIRECTORY] =
                g_param_spec_object ("directory",
                                     "Directory",
                                     "",
                                     G_TYPE_FILE,
                                     G_PARAM_READWRITE |
                                     G_PARAM_CONSTRUCT_ONLY |
                                     G_PARAM_STATIC_STRINGS);

        properties[PROP_SCALE] =
                g_param_spec_int("scale",
                                 "UI Scale",
                                 "",
                                 1,
                                 10,
                                 1,
                                 G_PARAM_READWRITE |
                                 G_PARAM_CONSTRUCT_ONLY |
                                 G_PARAM_STATIC_STRINGS);

        g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

static void
dh_book_list_directory_init (DhBookListDirectory *list_directory)
{
        instances = g_list_prepend (instances, list_directory);
}

/**
 * dh_book_list_directory_new:
 * @directory: the #DhBookListDirectory:directory.
 *
 * Returns a #DhBookListDirectory for @directory.
 *
 * If a #DhBookListDirectory instance is still alive for @directory (according
 * to g_file_equal()), the same instance is returned with the reference count
 * increased by one, to avoid data duplication. If no #DhBookListDirectory
 * instance already exists for @directory, this function returns a new instance
 * with a reference count of one (so it's the responsibility of the caller to
 * keep the object alive if wanted, to avoid destroying and re-creating the same
 * #DhBookListDirectory repeatedly).
 *
 * Returns: (transfer full): a #DhBookListDirectory for @directory.
 * Since: 3.30
 */
DhBookListDirectory *
dh_book_list_directory_new (GFile *directory, gint scale)
{
        GList *l;

        g_return_val_if_fail (G_IS_FILE (directory), NULL);

        for (l = instances; l != NULL; l = l->next) {
                DhBookListDirectory *cur_list_directory = DH_BOOK_LIST_DIRECTORY (l->data);
                DhBookListDirectoryPrivate *priv = dh_book_list_directory_get_instance_private (cur_list_directory);

                if (priv->directory != NULL &&
                    g_file_equal (priv->directory, directory))
                        return g_object_ref (cur_list_directory);
        }

        g_assert(scale >= 0);

        DhBookListDirectory* dir = g_object_new (DH_TYPE_BOOK_LIST_DIRECTORY,
                                                 "scale", scale,
                                                 "directory", directory,
                                                 NULL);
        return dir;
}

/**
 * dh_book_list_directory_get_directory:
 * @list_directory: a #DhBookListDirectory.
 *
 * Returns: (transfer none): the #DhBookListDirectory:directory.
 * Since: 3.30
 */
GFile *
dh_book_list_directory_get_directory (DhBookListDirectory *list_directory)
{
        DhBookListDirectoryPrivate *priv = dh_book_list_directory_get_instance_private (list_directory);

        g_return_val_if_fail (DH_IS_BOOK_LIST_DIRECTORY (list_directory), NULL);

        return priv->directory;
}
