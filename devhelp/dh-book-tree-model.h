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

#ifndef DH_BOOK_TREE_MODEL_H
#define DH_BOOK_TREE_MODEL_H

#include <glib-object.h>
#include <devhelp/dh-link.h>

G_BEGIN_DECLS

#define DH_TYPE_BOOK_TREE_MODEL            (dh_book_tree_model_get_type ())
#define DH_BOOK_TREE_MODEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), DH_TYPE_BOOK_TREE_MODEL, DhBookTreeModel))
#define DH_BOOK_TREE_MODEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), DH_TYPE_BOOK_TREE_MODEL, DhBookTreeModelClass))
#define DH_IS_BOOK_TREE_MODEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DH_TYPE_BOOK_TREE_MODEL))
#define DH_IS_BOOK_TREE_MODEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), DH_TYPE_BOOK_TREE_MODEL))
#define DH_BOOK_TREE_MODEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), DH_TYPE_BOOK_TREE_MODEL, DhBookTreeModelClass))

typedef struct _DhBookTreeModel      DhBookTreeModel;
typedef struct _DhBookTreeModelClass DhBookTreeModelClass;

struct _DhBookTreeModel {
        GObject parent_instance;
};

struct _DhBookTreeModelClass {
        GObjectClass parent_class;

        /* Padding for future expansion */
        gpointer padding[12];
};

enum {
        DH_BOOK_TREE_MODEL_COL_TITLE,
        DH_BOOK_TREE_MODEL_COL_LINK,
        DH_BOOK_TREE_MODEL_COL_BOOK,
        DH_BOOK_TREE_MODEL_COL_WEIGHT,
        DH_BOOK_TREE_MODEL_COL_UNDERLINE,
        DH_BOOK_TREE_MODEL_COL_ICON,
        DH_BOOK_TREE_MODEL_NUM_COLS
};


GType           dh_book_tree_model_get_type  (void);

DhBookTreeModel *dh_book_tree_model_new       (gint scale, gboolean group_by_language);

G_END_DECLS

#endif /* DH_BOOK_TREE_MODEL_H */
