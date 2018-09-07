/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2018 Jerzy Kozera <jerzy.kozera@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */


#include "dh-book-tree-model.h"
#include <gmodule.h>
#include <cairo/cairo-gobject.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include "dh-book.h"
#include "dh-book-list.h"
#include "dh-search-context.h"
#include "dh-util-lib.h"
#include "dh-link.h"


typedef struct {
        gchar *title;
        gchar *symbolchapterpath;
        JsonObject* object;
        GtkTreePath *path;
        GList *children;
        gboolean lazy_has_children;
        gchar *lazy_children_url;
        gchar *symbol_tp;
        DhLink *link;
        DhBook *book;
} DhBookTreeModelNode;

static gint
compare_strs (gconstpointer a,
              gconstpointer b)
{
        return g_strcmp0(a, b);
}

static DhBookTreeModelNode* new_empty_node ()
{
        DhBookTreeModelNode *node = malloc (sizeof(DhBookTreeModelNode));
        memset(node, 0, sizeof(DhBookTreeModelNode));
        return node;
}

static DhBookTreeModelNode*
new_dynamic_symbols_node(JsonObject  *object,
                         const gchar *symbol_type,
                         gint         count,
                         GtkTreePath *parent,
                         gint         num,
                         const gchar *title,
                         const gchar *tp,
                         const char  *URL,
                         const char  *book_title)
{
        DhBookTreeModelNode *node = new_empty_node();
        JsonObject *counts;
        JsonObjectIter iter;
        GList *symbols;
        const gchar *member_name;
        JsonNode *member_node;
        size_t len;
        GList *books = dh_book_list_get_books (dh_book_list_get_default (
                -1  // at this point it should be created outside
        ));
        DhBook *book;

        if (title == NULL) {
                len = strlen (symbol_type);
                node->title = malloc (len + 1);
                memcpy(node->title, symbol_type, len + 1);
        } else {
                len = strlen (title);
                node->title = malloc (len + 1);
                memcpy(node->title, title, len + 1);
        }
        node->children = NULL;
        node->path = gtk_tree_path_copy (parent);

        node->book = NULL;
        while (books) {
                book = books->data;
                if (g_str_equal(book_title, dh_book_get_title(book))) {
                        node->book = book;
                }
                books = books->next;
        }
        g_assert(node->book != NULL);

        node->link = dh_link_new (DH_LINK_TYPE_KEYWORD, NULL, node->title, URL);
        gtk_tree_path_append_index(node->path, num);

        if (title == NULL || g_str_equal(tp, "chapters")) {
                // only 1st level of symbols
                // (chapters need querying on all levels because we don't know in advance
                //  if they have children)
                node->lazy_children_url = g_strjoin("",
                                                    "http://localhost:12340/item/",
                                                    json_object_get_string_member(object, "Id"),
                                                    "/", tp, "/", symbol_type, NULL);
        } else {
                node->lazy_children_url = NULL;
        }
        node->symbol_tp = g_strdup(tp);
        node->symbolchapterpath = malloc(strlen(symbol_type)+1);
        node->object = json_object_ref(object);
        strcpy(node->symbolchapterpath, symbol_type);
        node->lazy_has_children = count > 0;

        return node;
}


static DhBookTreeModelNode*
new_symbols_node(JsonObject* object, GtkTreePath *parent, gint num, const gchar* title, const gchar* book_title)
{
        DhBookTreeModelNode *node = new_empty_node();
        node->lazy_children_url = NULL;
        JsonObject *counts;
        JsonObjectIter iter;
        GList *symbols, *symbol;
        const gchar *member_name;
        JsonNode *member_node;
        size_t len;
        GList *books = dh_book_list_get_books (dh_book_list_get_default (
                -1  // at this point it should be already created outside
        ));
        DhBook *book;
        gint i;

        if (title == NULL) {
                title = "Symbols";
        }
        len = strlen (title);
        node->title = malloc (len + 1);
        memcpy(node->title, title, len + 1);
        node->children = NULL;
        if (parent != NULL) {
                node->path = gtk_tree_path_copy (parent);
        } else {
                node->path = gtk_tree_path_new ();
        }
        node->link = NULL;
        node->book = NULL;
        while (books) {
                book = books->data;
                if (g_str_equal(book_title, dh_book_get_title(book))) {
                        node->book = book;
                }
                books = books->next;
        }
        g_assert(node->book != NULL);
        gtk_tree_path_append_index(node->path, num);


        counts = json_object_get_object_member (object, "SymbolCounts");
        json_object_iter_init (&iter, counts);
        symbols = NULL;
        while(json_object_iter_next(&iter, &member_name, &member_node)) {
                symbols = g_list_append (symbols, member_name);
        }
        symbol = symbols = g_list_sort (symbols, compare_strs);
        i = 0;
        while(symbol) {
                node->children = g_list_append(node->children,
                                               new_dynamic_symbols_node (object,
                                                                         symbol->data,
                                                                         json_object_get_int_member(counts, symbol->data),
                                                                         node->path,
                                                                         i++, NULL, "symbols", NULL, book_title));
                symbol = symbol->next;
        }
        g_list_free(symbols);
        return node;
}

static DhBookTreeModelNode*
new_node (JsonObject* object, GtkTreePath *parent, gint num)
{
        DhBookTreeModelNode *node = new_empty_node();
        node->lazy_children_url = NULL;
        const gchar* title;
        size_t len;
        GList *books = dh_book_list_get_books (dh_book_list_get_default (
                -1  // at this point it should be already created outside
        ));
        DhBook *book;

        title = json_object_get_string_member(object, "Title");

        if (g_str_equal (json_object_get_string_member(object, "SourceId"), "com.kapeli")) {
                return new_symbols_node (object, parent, num, title, title);
        } else {
                len = strlen (title);
                node->title = malloc (len + 1);
                memcpy(node->title, title, len + 1);
                node->children = NULL;


                if (parent != NULL) {
                        node->path = gtk_tree_path_copy (parent);
                } else {
                        node->path = gtk_tree_path_new ();
                }

                node->link = NULL;
                node->book = NULL;
                while (books) {
                        book = books->data;
                        if (g_str_equal(title, dh_book_get_title(book))) {
                                node->book = book;
                        }
                        books = books->next;
                }
                g_assert(node->book != NULL);
                gtk_tree_path_append_index(node->path, num);
                node->children = g_list_append(node->children,
                                               new_dynamic_symbols_node (object,
                                                                         "",
                                                                         0,
                                                                         node->path,
                                                                         0,
                                                                         "Chapters",
                                                                         "chapters",
                                                                         NULL,
                                                                         title));
                g_list_append(node->children, new_symbols_node (object, node->path, 1, NULL, title));
        }

        return node;
}

static DhBookTreeModelNode*
new_lang_node (const gchar* title, GtkTreePath *parent, gint num) {
        DhBookTreeModelNode *node = new_empty_node();
        node->lazy_children_url = NULL;
        size_t len;
        len = strlen (title);
        node->title = malloc (len + 1);
        memcpy(node->title, title, len + 1);
        node->children = NULL;
        if (parent != NULL) {
                node->path = gtk_tree_path_copy (parent);
        } else {
                node->path = gtk_tree_path_new ();
        }

        node->link = NULL;

        gtk_tree_path_append_index(node->path, num);
        return node;
}

typedef struct {
        gchar *current_book_id;

        /* List of owned DhLink*.
         *
         * Note: GQueue, not GQueue* so we are sure that it always exists, we
         * don't need to check if priv->links == NULL.
         */
        GQueue links;
        GList *root_nodes;

        gboolean group_by_language;
        gint langcount;
        gint stamp;
        const gchar *currently_adding_language;
        JsonArray *array;
} DhBookTreeModelPrivate;

static void dh_book_tree_model_tree_model_init (GtkTreeModelIface *iface);

G_DEFINE_TYPE_WITH_CODE (DhBookTreeModel, dh_book_tree_model, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (DhBookTreeModel)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL,
                                                dh_book_tree_model_tree_model_init));

static void
free_node(DhBookTreeModelNode *node) {
        free(node->title);
        if (node->symbolchapterpath != NULL) {
                free(node->symbolchapterpath);
        }
        if (node->object != NULL) {
                json_object_unref (node->object);
        }
        if (node->path != NULL) {
                gtk_tree_path_free (node->path);
        }
        if (node->lazy_children_url != NULL) {
                free(node->lazy_children_url);
        }
        if (node->symbol_tp != NULL) {
                free(node->symbol_tp);
        }
        if (node->link != NULL) {
                dh_link_unref (node->link);
        }
        g_list_free_full(node->children, (GDestroyNotify)free_node);
        free(node);
}


static void
dh_book_tree_model_finalize (GObject *object)
{
        DhBookTreeModel *model = DH_BOOK_TREE_MODEL (object);
        DhBookTreeModelPrivate *priv = dh_book_tree_model_get_instance_private (model);
        g_list_free_full (priv->root_nodes, (GDestroyNotify)free_node);
        G_OBJECT_CLASS (dh_book_tree_model_parent_class)->finalize (object);
}

static void
dh_book_tree_model_class_init (DhBookTreeModelClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = dh_book_tree_model_finalize;
}

static void
print_doc (JsonArray *array,
           guint index_,
           JsonNode *element_node,
           gpointer user_data)
{
        JsonObject *object;
        DhBookTreeModelPrivate *priv;
        DhBookTreeModelNode *node;

        priv = dh_book_tree_model_get_instance_private (DH_BOOK_TREE_MODEL (user_data));

        object = json_node_get_object(element_node);
        node = new_node (object, NULL, index_);
        priv->root_nodes = g_list_append (priv->root_nodes, node);
}

static void
extract_language (JsonArray *array,
                  guint index_,
                  JsonNode *element_node,
                  gpointer user_data)
{
        GHashTable *table = user_data;
        JsonObject *object;
        object = json_node_get_object(element_node);
        g_hash_table_add(table, json_object_get_string_member(object, "Language"));
}

static void
add_language (gpointer data,
              gpointer user_data)
{
        DhBookTreeModelPrivate *priv;
        DhBookTreeModelNode *node;

        priv = dh_book_tree_model_get_instance_private (DH_BOOK_TREE_MODEL (user_data));

        if (!g_str_equal(data, "")) {
                node = new_lang_node (data, NULL, priv->langcount++);
                priv->root_nodes = g_list_append (priv->root_nodes, node);
        }
}

static void
add_docs_for_cur_lang (JsonArray *array,
                       guint index_,
                       JsonNode *element_node,
                       gpointer user_data)
{
        JsonObject *object;
        int idx;
        DhBookTreeModelPrivate *priv;
        GList *l;
        GtkTreePath *path;
        DhBookTreeModelNode *node, *newnode;
        const gchar *title;
        const gchar *language;

        priv = dh_book_tree_model_get_instance_private (DH_BOOK_TREE_MODEL (user_data));

        object = json_node_get_object(element_node);
        title = json_object_get_string_member(object, "Title");
        language = json_object_get_string_member(object, "Language");
        if (g_strcmp0(language, priv->currently_adding_language) != 0) {
                return;
        }

        if (g_str_equal(language, "")) {
                priv->root_nodes = g_list_append (priv->root_nodes,
                                                  new_node (object,
                                                            NULL,
                                                            g_list_length(priv->root_nodes)));
        } else {
                l = priv->root_nodes;
                idx = 0;
                while (l) {
                        node = l->data;
                        if (node->link == NULL && g_strcmp0(language, node->title) == 0) {
                                break;
                        }
                        if (node->link == NULL) {
                                idx += 1;
                        }
                        l = l->next;
                }
                path = gtk_tree_path_new_from_indices(idx, -1),
                newnode = new_node (object, path, priv->langcount++);
                gtk_tree_path_free(path);
                node->book = newnode->book;  // needed for icons
                node->children = g_list_append (node->children, newnode);
        }
}

static void
add_language_docs (gpointer data,
                   gpointer user_data)
{
        DhBookTreeModelPrivate *priv;

        priv = dh_book_tree_model_get_instance_private (DH_BOOK_TREE_MODEL (user_data));

        priv->currently_adding_language = data;
        priv->langcount = 0;
        json_array_foreach_element(priv->array, add_docs_for_cur_lang, user_data);
}

static void
dh_book_tree_model_init (DhBookTreeModel *model)
{
        DhBookTreeModelPrivate *priv = dh_book_tree_model_get_instance_private (model);



        priv->stamp = g_random_int_range (1, G_MAXINT32);
}

/**
 * dh_book_tree_model_new:
 *
 * Returns: a new #DhBookTreeModel object.
 */
DhBookTreeModel *
dh_book_tree_model_new (gboolean group_by_language, gint scale)
{
        DhBookTreeModel *model = DH_BOOK_TREE_MODEL (g_object_new (DH_TYPE_BOOK_TREE_MODEL, NULL));
        DhBookTreeModelPrivate *priv = dh_book_tree_model_get_instance_private (model);

        SoupSession *session;
        const char *uri;
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

        priv->root_nodes = NULL;
        priv->group_by_language = group_by_language;

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

        if (priv->group_by_language) {
                json_array_foreach_element(array, extract_language, hash);
                priv->langcount = 0;
                langlist = NULL;
                langlist = g_hash_table_get_keys(hash);
                langlist = g_list_sort (langlist, compare_strs);
                g_list_foreach(langlist, add_language, model);
                priv->array = array;
                g_list_foreach(langlist, add_language_docs, model);
                g_list_free(langlist);
        } else {
                json_array_foreach_element(array, print_doc, model);
        }

        soup_buffer_free (buffer);
        soup_message_body_free (body);
        g_object_unref (parser);
        g_hash_table_unref (hash);
        g_object_unref (request);
        g_object_unref (session);
        return model;
}

static gboolean
dh_book_tree_model_get_iter (GtkTreeModel *tree_model,
                             GtkTreeIter  *iter,
                             GtkTreePath  *path)
{
        DhBookTreeModelPrivate *priv;
        const gint *indices;
        DhBookTreeModelNode *node;
        int depth;
        GList *nodes, *children;

        priv = dh_book_tree_model_get_instance_private (DH_BOOK_TREE_MODEL (tree_model));

        depth = gtk_tree_path_get_depth (path);
        indices = gtk_tree_path_get_indices (path);

        if (indices == NULL) {
                return FALSE;
        }

        nodes = priv->root_nodes;
        children = priv->root_nodes;
        for (int i = 0; i < depth; ++i)  {
                nodes = g_list_nth (children, indices[i]);
                if (nodes == NULL) {
                        break;
                }
                node = nodes->data;
                children = node->children;
        }

        if (nodes != NULL) {
                iter->stamp = priv->stamp;
                iter->user_data = nodes;
                return TRUE;
        }

        return FALSE;
}

static gint
dh_book_tree_model_get_n_columns (GtkTreeModel *tree_model)
{
        return DH_BOOK_TREE_MODEL_NUM_COLS;
}


static GType
dh_book_tree_model_get_column_type (GtkTreeModel *tree_model,
                                  gint          column)
{
        switch (column) {
        case DH_BOOK_TREE_MODEL_COL_TITLE:
                return G_TYPE_STRING;

        case DH_BOOK_TREE_MODEL_COL_LINK:
                return DH_TYPE_LINK;

        case DH_BOOK_TREE_MODEL_COL_BOOK:
                return DH_TYPE_BOOK;

        case DH_BOOK_TREE_MODEL_COL_WEIGHT:
                return PANGO_TYPE_WEIGHT;

        case DH_BOOK_TREE_MODEL_COL_UNDERLINE:
                return PANGO_TYPE_UNDERLINE;

        case DH_BOOK_TREE_MODEL_COL_ICON:
                return CAIRO_GOBJECT_TYPE_SURFACE;

        default:
                return G_TYPE_INVALID;
        }
}

static void
lazy_fetch_children(DhBookTreeModelNode *node)
{
        SoupSession *session;
        const char *uri;
        GHashTable *hash;
        GList *langlist;
        SoupMessage *request;
        SoupMessageBody *body;
        SoupBuffer *buffer;
        const gchar *data, *symbol;
        gsize length;
        JsonParser *parser;
        JsonNode *root;
        JsonArray *array, *subarray;

        parser = json_parser_new();
        session = soup_session_new();
        hash = g_hash_table_new(g_str_hash, g_str_equal);
        request = soup_form_request_new_from_hash ("GET", node->lazy_children_url, hash);

        soup_session_send_message (session, request);
        g_object_get(request, "response-body", &body, NULL);

        buffer = soup_message_body_flatten(body);
        soup_buffer_get_data(buffer, (const guint8**)&data, &length);
        json_parser_load_from_data(parser, data, length, NULL);
        root = json_parser_get_root(parser);
        array = json_node_get_array(root);

        for (guint i = 0; i < json_array_get_length(array); ++i) {
                subarray = json_array_get_array_element(array, i);
                symbol = json_array_get_string_element(subarray, 0);
                node->children = g_list_append(node->children,
                                               new_dynamic_symbols_node (node->object,
                                                                         g_strjoin("", node->symbolchapterpath, "/", symbol, NULL),
                                                                         0,
                                                                         node->path,
                                                                         i,
                                                                         symbol, node->symbol_tp,
                                                                         g_strjoin("/",
                                                                                   "http://localhost:12340",
                                                                                   json_array_get_string_element(subarray, 1),
                                                                                   NULL),
                                                                         dh_book_get_title(node->book)));
        }


        soup_buffer_free (buffer);
        soup_message_body_free (body);
        g_object_unref (parser);
        g_hash_table_unref (hash);
        g_object_unref (request);
        g_object_unref (session);
}

static gboolean
dh_book_tree_model_iter_has_child (GtkTreeModel *tree_model,
                                   GtkTreeIter *iter)
{
        GList *list;
        DhBookTreeModelNode *node;
        DhBookTreeModelPrivate *priv;

        priv = dh_book_tree_model_get_instance_private (DH_BOOK_TREE_MODEL (tree_model));
        list = iter->user_data;
        if (list == NULL) {
                return FALSE;
        }
        node = list->data;
        if (node->lazy_children_url && !node->lazy_has_children && !node->children) {
                lazy_fetch_children(node);
        }
        return g_list_length (node->children) > 0 || (node->lazy_children_url && node->lazy_has_children);
}

static gboolean
dh_book_tree_model_iter_next (GtkTreeModel *tree_model,
                              GtkTreeIter *iter)
{
        GList *node;
        DhBookTreeModelPrivate *priv;
        priv = dh_book_tree_model_get_instance_private (DH_BOOK_TREE_MODEL (tree_model));
        node = iter->user_data;
        if (node == NULL && g_list_length(priv->root_nodes) > 0) {
                iter->stamp = priv->stamp;
                iter->user_data = priv->root_nodes;
                return TRUE;
        }
        if (node == NULL || node->next == NULL) {
                return FALSE;
        }
        iter->stamp = priv->stamp;
        iter->user_data = node->next;
        return TRUE;
}

static GtkTreePath *
dh_book_tree_model_get_path (GtkTreeModel *tree_model,
                             GtkTreeIter *iter)
{
        GList *list;
        DhBookTreeModelNode *node;

        list = iter->user_data;
        if (list != NULL) {
                node = list->data;
                return gtk_tree_path_copy (node->path);
        }
        return NULL;

}

static void
dh_book_tree_model_get_value (GtkTreeModel *tree_model,
                              GtkTreeIter *iter,
                              gint column,
                              GValue *value)
{

        GList *list;
        GList *books = dh_book_list_get_books (dh_book_list_get_default (
                -1 // at this point it should be already created outside
        ));
        DhBook *book;
        DhBookTreeModelNode *node;
        list = iter->user_data;
        node = list->data;

        switch (column) {
        case DH_BOOK_TREE_MODEL_COL_TITLE:
                g_value_init(value, G_TYPE_STRING);
                g_value_set_string(value, node->title);
                return;

        case DH_BOOK_TREE_MODEL_COL_LINK:
                g_value_init(value, DH_TYPE_LINK);
                g_value_set_boxed(value, node->link);
                return;

        case DH_BOOK_TREE_MODEL_COL_BOOK:
                g_value_init(value, DH_TYPE_BOOK);
                g_value_set_boxed(value, node->book);
                return;

        case DH_BOOK_TREE_MODEL_COL_WEIGHT:
                g_value_init(value, PANGO_TYPE_WEIGHT);
                g_value_set_enum(value, PANGO_WEIGHT_NORMAL);
                return;

        case DH_BOOK_TREE_MODEL_COL_UNDERLINE:
                g_value_init(value, PANGO_TYPE_UNDERLINE);
                g_value_set_enum(value, PANGO_UNDERLINE_NONE);
                return;

        case DH_BOOK_TREE_MODEL_COL_ICON:
                while (books) {
                        book = books->data;
                        if (g_str_equal(dh_book_get_title(node->book), dh_book_get_title(book))) {
                                g_value_init(value, CAIRO_GOBJECT_TYPE_SURFACE);
                                g_value_set_boxed(value, dh_book_get_icon_surface(book));
                                return;
                        }
                        books = books->next;
                }
                return;

        default:
                return;
        }
}

static gboolean
dh_book_tree_model_iter_nth_child (GtkTreeModel *tree_model,
                                   GtkTreeIter *iter,
                                   GtkTreeIter *parent,
                                   gint n)
{
        GList *list;
        DhBookTreeModelNode *node;
        DhBookTreeModelPrivate *priv;

        priv = dh_book_tree_model_get_instance_private (DH_BOOK_TREE_MODEL (tree_model));
        if (parent == NULL) {
                return dh_book_tree_model_get_iter (tree_model,
                                                    iter,
                                                    gtk_tree_path_new_from_indices(n, -1));
        }
        list = parent->user_data;

        node = list->data;
        if (node->lazy_children_url && !node->children) {
                lazy_fetch_children(node);
        }
        list = g_list_nth (node->children, n);
        if (list == NULL) {
                return FALSE;
        }
        iter->user_data = list;
        iter->stamp = priv->stamp;
        return TRUE;

}

static gboolean
dh_book_tree_model_iter_children (GtkTreeModel *tree_model,
                              GtkTreeIter *iter,
                              GtkTreeIter *parent)
{
        return dh_book_tree_model_iter_nth_child (tree_model, iter, parent, 0);
}

static gboolean
dh_book_tree_model_iter_parent (GtkTreeModel *tree_model,
                                GtkTreeIter *iter,
                                GtkTreeIter *child)
{
        DhBookTreeModelPrivate *priv;
        GList *list;
        DhBookTreeModelNode *node;
        gint *indices, depth;

        priv = dh_book_tree_model_get_instance_private (DH_BOOK_TREE_MODEL (tree_model));
        list = child->user_data;

        if (list != NULL) {
                node = list->data;
                indices = gtk_tree_path_get_indices_with_depth (node->path, &depth);
                if (depth < 2) {
                        return FALSE;
                }
                list = priv->root_nodes;
                for (int i = 0; i < depth - 1; ++i) {
                        list = iter->user_data = g_list_nth(list, indices[i]);
                        node = list->data;
                        list = node->children;
                }
                iter->stamp = priv->stamp;
                return TRUE;
        } else {
                return FALSE;
        }
}

static void
dh_book_tree_model_tree_model_init (GtkTreeModelIface *iface)
{
        iface->get_iter = dh_book_tree_model_get_iter;
        iface->get_n_columns = dh_book_tree_model_get_n_columns;
        iface->get_column_type = dh_book_tree_model_get_column_type;
        iface->iter_has_child = dh_book_tree_model_iter_has_child;
        iface->iter_nth_child = dh_book_tree_model_iter_nth_child;
        iface->iter_children = dh_book_tree_model_iter_children;
        iface->iter_next = dh_book_tree_model_iter_next;
        iface->iter_parent = dh_book_tree_model_iter_parent;
        iface->get_path = dh_book_tree_model_get_path;
        iface->get_value = dh_book_tree_model_get_value;
}
