/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * This file is part of Devhelp.
 *
 * Copyright (C) 2002 CodeFactory AB
 * Copyright (C) 2002 Mikael Hallendal <micke@imendio.com>
 * Copyright (C) 2008 Imendio AB
 * Copyright (C) 2010 Lanedo GmbH
 * Copyright (C) 2015-2018 Sébastien Wilmet <swilmet@gnome.org>
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
#include <glib/gi18n.h>
#include "dh-book.h"
#include "dh-book-list.h"
#include "dh-keyword-model.h"
#include "dh-search-context.h"
#include "dh-util-lib.h"

/**
 * SECTION:dh-keyword-model
 * @Title: DhKeywordModel
 * @Short_description: A custom #GtkTreeModel implementation for searching
 * #DhLink's
 *
 * #DhKeywordModel is a custom #GtkTreeModel implementation (as a list, not a
 * tree) for searching #DhLink's.
 *
 * The dh_keyword_model_filter() function is used to set the search criteria. It
 * fills the #GtkTreeModel with the list of #DhLink's that match the search
 * criteria (up to a certain maximum number of matches).
 *
 * How the search works (for end users) is explained in the user documentation
 * of the Devhelp application.
 *
 * # Filter by book and page
 *
 * As a kind of API for integrating Devhelp with other applications, the search
 * string supports additional features. Those features are not intended to be
 * used directly by end users when typing the search string in the GUI, because
 * it's not really convenient. It is intended to be used with the
 * `devhelp --search "search-string"` command line, so that another application
 * can launch Devhelp and set a specific search string.
 *
 * It is possible to filter by book by prefixing the search string with
 * “book:the-book-ID”. For example “book:gtk3”. If there are no other search
 * terms, it shows the top-level page of that book. If there are other search
 * terms, it limits the search to the specified book. See also the
 * dh_book_get_id() function (in the `*.devhelp2` index file format it's called
 * the book “name”, not ID, but ID is clearer).
 *
 * Similarly, it is possible to filter by page, by prefixing the search string
 * with “page:the-page-ID”. For example “page:GtkWindow”. If there are no other
 * search terms, the top of the page is shown and the search matches all the
 * symbols part of that page. If there are other search terms, it limits the
 * search to the specified page. To know what is the “page ID”, see the
 * dh_link_belongs_to_page() function.
 *
 * “book:” and “page:” can be combined. Normal search terms must be
 * <emphasis>after</emphasis> “book:” and “page:”.
 *
 * The book and page IDs – even if they contain an uppercase letter – don't
 * affect the case sensitivity for the other search terms.
 */


typedef struct {
        gchar *current_book_id;

        /* List of owned DhLink*.
         *
         * Note: GQueue, not GQueue* so we are sure that it always exists, we
         * don't need to check if priv->links == NULL.
         */
        GQueue links;

        gint stamp;
        GtkTreeModel *filter_store;
        GString *group_id;
} DhKeywordModelPrivate;

typedef struct {
        DhBookList *book_list;
        DhSearchContext *search_context;
        const gchar *book_id;
        const gchar *skip_book_id;
        guint prefix : 1;
} SearchSettings;

#define MAX_HITS 1000

enum {
        SIGNAL_FILTER_COMPLETE,
        N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

static void dh_keyword_model_tree_model_init (GtkTreeModelIface *iface);

G_DEFINE_TYPE_WITH_CODE (DhKeywordModel, dh_keyword_model, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (DhKeywordModel)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL,
                                                dh_keyword_model_tree_model_init));

static void
clear_links (DhKeywordModel *model)
{
        DhKeywordModelPrivate *priv = dh_keyword_model_get_instance_private (model);
        GList *l;

        for (l = priv->links.head; l != NULL; l = l->next) {
                DhLink *cur_link = l->data;
                dh_link_unref (cur_link);
        }

        g_queue_clear (&priv->links);
}

static void
dh_keyword_model_finalize (GObject *object)
{
        DhKeywordModel *model = DH_KEYWORD_MODEL (object);
        DhKeywordModelPrivate *priv = dh_keyword_model_get_instance_private (model);

        g_free (priv->current_book_id);
        clear_links (model);

        G_OBJECT_CLASS (dh_keyword_model_parent_class)->finalize (object);
}

static void
dh_keyword_model_class_init (DhKeywordModelClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = dh_keyword_model_finalize;
        signals[SIGNAL_FILTER_COMPLETE] =
                g_signal_new ("filter-complete",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (DhKeywordModelClass, filter_complete),
                              NULL, NULL, NULL,
                              G_TYPE_NONE,
                              0);
}

static void
dh_keyword_model_init (DhKeywordModel *model)
{
        DhKeywordModelPrivate *priv = dh_keyword_model_get_instance_private (model);

        priv->stamp = g_random_int_range (1, G_MAXINT32);
}

static GtkTreeModelFlags
dh_keyword_model_get_flags (GtkTreeModel *tree_model)
{
        return GTK_TREE_MODEL_LIST_ONLY;
}

static gint
dh_keyword_model_get_n_columns (GtkTreeModel *tree_model)
{
        return DH_KEYWORD_MODEL_NUM_COLS;
}

static GType
dh_keyword_model_get_column_type (GtkTreeModel *tree_model,
                                  gint          column)
{
        switch (column) {
        case DH_KEYWORD_MODEL_COL_NAME:
                return G_TYPE_STRING;

        case DH_KEYWORD_MODEL_COL_LINK:
                return DH_TYPE_LINK;

        case DH_KEYWORD_MODEL_COL_CURRENT_BOOK_FLAG:
                return G_TYPE_BOOLEAN;

        default:
                return G_TYPE_INVALID;
        }
}

static gboolean
dh_keyword_model_get_iter (GtkTreeModel *tree_model,
                           GtkTreeIter  *iter,
                           GtkTreePath  *path)
{
        DhKeywordModelPrivate *priv;
        const gint *indices;
        GList *node;

        priv = dh_keyword_model_get_instance_private (DH_KEYWORD_MODEL (tree_model));

        if (gtk_tree_path_get_depth (path) > 1) {
                return FALSE;
        }

        indices = gtk_tree_path_get_indices (path);

        if (indices == NULL) {
                return FALSE;
        }

        node = g_queue_peek_nth_link (&priv->links, indices[0]);

        if (node != NULL) {
                iter->stamp = priv->stamp;
                iter->user_data = node;
                return TRUE;
        }

        return FALSE;
}

static GtkTreePath *
dh_keyword_model_get_path (GtkTreeModel *tree_model,
                           GtkTreeIter  *iter)
{
        DhKeywordModelPrivate *priv;
        GList *node;
        GtkTreePath *path;
        gint pos;

        priv = dh_keyword_model_get_instance_private (DH_KEYWORD_MODEL (tree_model));

        g_return_val_if_fail (iter->stamp == priv->stamp, NULL);

        node = iter->user_data;
        pos = g_queue_link_index (&priv->links, node);

        if (pos < 0) {
                return NULL;
        }

        path = gtk_tree_path_new ();
        gtk_tree_path_append_index (path, pos);

        return path;
}

static void
dh_keyword_model_get_value (GtkTreeModel *tree_model,
                            GtkTreeIter  *iter,
                            gint          column,
                            GValue       *value)
{
        DhKeywordModelPrivate *priv;
        GList *node;
        DhLink *link;
        gboolean in_current_book;

        priv = dh_keyword_model_get_instance_private (DH_KEYWORD_MODEL (tree_model));

        g_return_if_fail (iter->stamp == priv->stamp);

        node = iter->user_data;
        link = node->data;

        switch (column) {
        case DH_KEYWORD_MODEL_COL_NAME:
                g_value_init (value, G_TYPE_STRING);
                g_value_set_string (value, dh_link_get_name (link));
                break;

        case DH_KEYWORD_MODEL_COL_LINK:
                g_value_init (value, DH_TYPE_LINK);
                g_value_set_boxed (value, link);
                break;

        case DH_KEYWORD_MODEL_COL_CURRENT_BOOK_FLAG:
                in_current_book = g_strcmp0 (dh_link_get_book_id (link), priv->current_book_id) == 0;
                g_value_init (value, G_TYPE_BOOLEAN);
                g_value_set_boolean (value, in_current_book);
                break;

        default:
                g_warning ("Bad column %d requested", column);
        }
}

static gboolean
dh_keyword_model_iter_next (GtkTreeModel *tree_model,
                            GtkTreeIter  *iter)
{
        DhKeywordModelPrivate *priv;
        GList *node;

        priv = dh_keyword_model_get_instance_private (DH_KEYWORD_MODEL (tree_model));

        g_return_val_if_fail (priv->stamp == iter->stamp, FALSE);

        node = iter->user_data;
        iter->user_data = node->next;

        return iter->user_data != NULL;
}

static gboolean
dh_keyword_model_iter_children (GtkTreeModel *tree_model,
                                GtkTreeIter  *iter,
                                GtkTreeIter  *parent)
{
        DhKeywordModelPrivate *priv;

        priv = dh_keyword_model_get_instance_private (DH_KEYWORD_MODEL (tree_model));

        /* This is a list, nodes have no children. */
        if (parent != NULL) {
                return FALSE;
        }

        /* But if parent == NULL we return the list itself as children of
         * the "root".
         */
        if (priv->links.head != NULL) {
                iter->stamp = priv->stamp;
                iter->user_data = priv->links.head;
                return TRUE;
        }

        return FALSE;
}

static gboolean
dh_keyword_model_iter_has_child (GtkTreeModel *tree_model,
                                 GtkTreeIter  *iter)
{
        return FALSE;
}

static gint
dh_keyword_model_iter_n_children (GtkTreeModel *tree_model,
                                  GtkTreeIter  *iter)
{
        DhKeywordModelPrivate *priv;

        priv = dh_keyword_model_get_instance_private (DH_KEYWORD_MODEL (tree_model));

        if (iter == NULL) {
                return priv->links.length;
        }

        g_return_val_if_fail (priv->stamp == iter->stamp, -1);

        return 0;
}

static gboolean
dh_keyword_model_iter_nth_child (GtkTreeModel *tree_model,
                                 GtkTreeIter  *iter,
                                 GtkTreeIter  *parent,
                                 gint          n)
{
        DhKeywordModelPrivate *priv;
        GList *child;

        priv = dh_keyword_model_get_instance_private (DH_KEYWORD_MODEL (tree_model));

        if (parent != NULL) {
                return FALSE;
        }

        child = g_queue_peek_nth_link (&priv->links, n);

        if (child != NULL) {
                iter->stamp = priv->stamp;
                iter->user_data = child;
                return TRUE;
        }

        return FALSE;
}

static gboolean
dh_keyword_model_iter_parent (GtkTreeModel *tree_model,
                              GtkTreeIter  *iter,
                              GtkTreeIter  *child)
{
        return FALSE;
}

static void
dh_keyword_model_tree_model_init (GtkTreeModelIface *iface)
{
        iface->get_flags = dh_keyword_model_get_flags;
        iface->get_n_columns = dh_keyword_model_get_n_columns;
        iface->get_column_type = dh_keyword_model_get_column_type;
        iface->get_iter = dh_keyword_model_get_iter;
        iface->get_path = dh_keyword_model_get_path;
        iface->get_value = dh_keyword_model_get_value;
        iface->iter_next = dh_keyword_model_iter_next;
        iface->iter_children = dh_keyword_model_iter_children;
        iface->iter_has_child = dh_keyword_model_iter_has_child;
        iface->iter_n_children = dh_keyword_model_iter_n_children;
        iface->iter_nth_child = dh_keyword_model_iter_nth_child;
        iface->iter_parent = dh_keyword_model_iter_parent;
}

/**
 * dh_keyword_model_new:
 *
 * Returns: a new #DhKeywordModel object.
 */
DhKeywordModel *
dh_keyword_model_new (void)
{
        return g_object_new (DH_TYPE_KEYWORD_MODEL, NULL);
}

static GQueue *
search_single_book (DhBook          *book,
                    SearchSettings  *settings,
                    guint            max_hits,
                    DhLink         **exact_link)
{
        GQueue *ret;
        GList *l;

        ret = g_queue_new ();

        for (l = dh_book_get_links (book);
             l != NULL && ret->length < max_hits;
             l = l->next) {
                DhLink *link = l->data;

                if (!_dh_search_context_match_link (settings->search_context,
                                                    link,
                                                    settings->prefix)) {
                        continue;
                }

                g_queue_push_tail (ret, dh_link_ref (link));

                if (exact_link == NULL || !settings->prefix)
                        continue;

                /* Look for an exact link match. If the link is a PAGE, we can
                 * overwrite any previous exact link set. For example, when
                 * looking for GFile, we want the page, not the struct.
                 */
                if ((*exact_link == NULL || dh_link_get_link_type (link) == DH_LINK_TYPE_PAGE) &&
                    _dh_search_context_is_exact_link (settings->search_context, link)) {
                        *exact_link = link;
                }
        }

        return ret;
}

typedef struct {
        SearchSettings settings;
        DhKeywordModel *model;
        DhKeywordModelPrivate *priv;
} SearchContext;

static void
websocket_message_cb (SoupWebsocketConnection *self,
                      gint                     type,
                      GBytes                  *message,
                      gpointer                 user_data)
{
        JsonParser *parser;
        JsonNode *root;
        JsonObject *object;
        GError *error = NULL;
        size_t len;
        DhLink *link;
        SearchContext *ctx = user_data;
        DhLink *book_link;
        gchar* msg = g_bytes_get_data(message, &len), *uri;

        if (len < 2) {
                return;
        }

        parser = json_parser_new();
        json_parser_load_from_data(parser, msg, len, &error);

        if (error != NULL) {
                const char *data = "";
                soup_websocket_connection_close(self, 0, data);

                if (g_queue_get_length(&ctx->priv->links) == 0) {
                        book_link = dh_link_new_book ("", "stackoverflow", "Stack Overflow", "");

                        uri = g_strjoin("",
                                        "https://stackoverflow.com/search?",
                                        soup_form_encode("q", ctx->settings.search_context->joined_keywords, NULL),
                                        NULL);
                        link = dh_link_new (DH_LINK_TYPE_KEYWORD,
                                            book_link,
                                            _("Search on Stack Overflow"),
                                            uri);
                        g_queue_push_tail(&ctx->priv->links, link);
                        g_free (uri);
                        dh_link_unref (book_link);
                }

                g_signal_emit(ctx->model, signals[SIGNAL_FILTER_COMPLETE], 0);
                return;
        }

        root = json_parser_get_root(parser);
        object = json_node_get_object(root);

        book_link = dh_link_new_book ("",
                                      json_object_get_string_member(object, "DocsetId"),
                                      json_object_get_string_member(object, "DocsetName"),
                                      "");
        uri = g_strjoin("/",
                        "http://localhost:12340",
                        json_object_get_string_member(object, "Path"),
                        NULL);
        link = dh_link_new (DH_LINK_TYPE_KEYWORD,
                            book_link,
                            json_object_get_string_member(object, "Res"),
                            uri);
        g_free(uri);
        dh_link_unref (book_link);
        g_queue_push_tail(&ctx->priv->links, link);
        g_object_unref (parser);
}

void
websocket_connected_cb (GObject *source_object,
                        GAsyncResult *res,
                        gpointer user_data)
{
        SearchContext *ctx = user_data;
        SoupWebsocketConnection *conn = soup_session_websocket_connect_finish(source_object, res, NULL);
        g_signal_connect(conn, "message", (GCallback*)websocket_message_cb, user_data);
        gchar *kws = ctx->settings.search_context->joined_keywords;
        soup_websocket_connection_send_text(conn, kws);
}

static gint
link_compare (gconstpointer a,
              gconstpointer b,
              gpointer      user_data)
{
        return dh_link_compare (a, b);
}

void dh_keyword_model_set_group_id(DhKeywordModel *model, gchar *id)
{
        DhKeywordModelPrivate *priv = dh_keyword_model_get_instance_private (model);
        if (priv->group_id != NULL) {
                g_string_free(priv->group_id, TRUE);
        }
        priv->group_id = g_string_new(id);
}

static void
search_books (DhKeywordModel *model,
              SearchSettings  *settings,
              guint            max_hits,
              DhLink         **exact_link)
{
        GList *l;
        GQueue *ret;
        clear_links(model);
        DhKeywordModelPrivate *priv = dh_keyword_model_get_instance_private (model);

        if (settings->search_context->keywords == NULL) {
                return;
        }

        SoupSession *session;
        char *uri;
        gboolean uri_needs_free = FALSE;
        GHashTable *hash;
        SearchContext *ctx;
        SoupMessage *request;

        ctx = malloc(sizeof(SearchContext));
        ctx->settings = *settings;
        ctx->model = model;
        ctx->priv = priv;

        session = soup_session_new();
        if (priv->group_id == NULL || g_str_equal("*", priv->group_id->str)) {
                uri = "ws://localhost:12340/search";
        } else {
                uri = g_strdup_printf("ws://localhost:12340/search/group/%s", priv->group_id->str);
                uri_needs_free = TRUE;
        }
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
                                             ctx);
        free(protocols);
        g_hash_table_unref(hash);
        g_object_unref(request);
        if (uri_needs_free) {
                g_free(uri);
        }
}

static GQueue *
handle_book_id_only (DhBookList       *book_list,
                     DhSearchContext  *search_context,
                     DhLink          **exact_link)
{
        GList *books;
        GList *l;
        GQueue *ret;

        if (_dh_search_context_get_book_id (search_context) == NULL ||
            _dh_search_context_get_page_id (search_context) != NULL ||
            _dh_search_context_get_keywords (search_context) != NULL) {
                return NULL;
        }

        ret = g_queue_new ();

        books = dh_book_list_get_books (book_list);

        for (l = books; l != NULL; l = l->next) {
                DhBook *book = DH_BOOK (l->data);
                GNode *node;

                if (!_dh_search_context_match_book (search_context, book))
                        continue;

                /* Return only the top-level book page. */
                node = dh_book_get_tree (book);
                if (node != NULL) {
                        DhLink *link;

                        link = node->data;
                        g_queue_push_tail (ret, dh_link_ref (link));

                        if (exact_link != NULL)
                                *exact_link = link;
                }

                break;
        }

        return ret;
}

/* The Search rationale is as follows:
 *
 * - If 'book_id' is given, but no 'page_id' or 'keywords', the main page of
 *   the book will only be shown, giving as exact match this book link.
 * - If 'book_id' and 'page_id' are given, but no 'keywords', all the items
 *   in the given page of the given book will be shown.
 * - If 'book_id' and 'keywords' are given, but no 'page_id', up to MAX_HITS
 *   items matching the keywords in the given book will be shown.
 * - If 'book_id' and 'page_id' and 'keywords' are given, all the items
 *   matching the keywords in the given page of the given book will be shown.
 *
 * - If 'page_id' is given, but no 'book_id' or 'keywords', all the items
 *   in the given page will be shown, giving as exact match the page link.
 * - If 'page_id' and 'keywords' are given but no 'book_id', all the items
 *   matching the keywords in the given page will be shown.
 *
 * - If 'keywords' only are given, up to max_hits items matching the keywords
 *   will be shown. If keyword matches both a page link and a non-page one,
 *   the page link is the one given as exact match.
 */
static void
keyword_model_search (DhKeywordModel   *model,
                      DhBookList       *book_list,
                      DhSearchContext  *search_context,
                      DhLink          **exact_link)
{
        DhKeywordModelPrivate *priv = dh_keyword_model_get_instance_private (model);
        SearchSettings settings;
        guint max_hits = MAX_HITS;
        GQueue *in_book = NULL;
        GQueue *other_books = NULL;
        DhLink *in_book_exact_link = NULL;
        DhLink *other_books_exact_link = NULL;

        settings.book_list = book_list;
        settings.search_context = search_context;
        settings.book_id = priv->current_book_id;
        settings.skip_book_id = NULL;
        settings.prefix = TRUE;

        if (_dh_search_context_get_page_id (search_context) != NULL) {
                /* If filtering per page, increase the maximum number of
                 * hits. This is due to the fact that a page may have
                 * more than MAX_HITS keywords, and the page link may be
                 * the last one in the list, but we always want to get it.
                 */
                max_hits = G_MAXUINT;
        }

        /* First look for prefixed items in the given book id. */
        if (priv->current_book_id != NULL) {
                /*in_book = search_books (model, &settings,
                                        max_hits,
                                        &in_book_exact_link);*/
        }

        /* Next, always check other books as well, as the exact match may be in
         * there.
         */
        settings.book_id = NULL;
        settings.skip_book_id = priv->current_book_id;
        search_books (model, &settings,
                      max_hits,
                      &other_books_exact_link);
        return;

}

/**
 * dh_keyword_model_filter:
 * @model: a #DhKeywordModel.
 * @search_string: a search query.
 * @current_book_id: (nullable): the ID of the book currently shown, or %NULL.
 * @profile: (nullable): a #DhProfile, or %NULL for the default profile.
 *
 * Searches in the #DhBookList of @profile the list of #DhLink's that correspond
 * to @search_string, and fills the @model with that list (erasing the previous
 * content).
 *
 * Attention, when calling this function the @model needs to be disconnected
 * from the #GtkTreeView, because the #GtkTreeModel signals are not emitted, to
 * improve the performances (sending a lot of signals is slow) and have a
 * simpler implementation. The previous row selection is anyway no longer
 * relevant.
 *
 * Note that there is a maximum number of matches (configured internally). When
 * the maximum is reached the search is stopped, to avoid blocking the GUI
 * (since this function runs synchronously) if the @search_string contains for
 * example only one character. (And it is anyway not very useful to show to the
 * user tens of thousands search results).
 *
 * Returns: (nullable) (transfer none): the #DhLink that matches exactly
 * @search_string, or %NULL if no such #DhLink was found within the maximum
 * number of matches.
 */
DhLink *
dh_keyword_model_filter (DhKeywordModel *model,
                         const gchar    *search_string,
                         const gchar    *current_book_id,
                         DhProfile      *profile)
{
        DhKeywordModelPrivate *priv;
        DhBookList *book_list;
        DhSearchContext *search_context;
        DhLink *exact_link = NULL;

        g_return_val_if_fail (DH_IS_KEYWORD_MODEL (model), NULL);
        g_return_val_if_fail (search_string != NULL, NULL);
        g_return_val_if_fail (profile == NULL || DH_IS_PROFILE (profile), NULL);

        priv = dh_keyword_model_get_instance_private (model);

        if (profile == NULL)
                profile = dh_profile_get_default (-1);  // should be already created

        book_list = dh_profile_get_book_list (profile);

        g_free (priv->current_book_id);
        priv->current_book_id = NULL;

        search_context = _dh_search_context_new (search_string);

        if (search_context != NULL) {
                const gchar *book_id_in_search_string;

                book_id_in_search_string = _dh_search_context_get_book_id (search_context);

                if (book_id_in_search_string != NULL)
                        priv->current_book_id = g_strdup (book_id_in_search_string);
                else
                        priv->current_book_id = g_strdup (current_book_id);

                keyword_model_search (model, book_list, search_context, &exact_link);
        }

        //clear_links (model);
        //_dh_util_queue_concat (&priv->links, new_links);
        //new_links = NULL;

        /* The content has been modified, change the stamp so that older
         * GtkTreeIter's become invalid.
         */
        priv->stamp++;

        // _dh_search_context_free (search_context);

        /* One hit */
        if (priv->links.length == 1)
                return g_queue_peek_head (&priv->links);

        return exact_link;
}
