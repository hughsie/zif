/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#if !defined (__ZIF_H_INSIDE__) && !defined (ZIF_COMPILATION)
#error "Only <zif.h> can be included directly."
#endif

#ifndef __ZIF_HISTORY_H
#define __ZIF_HISTORY_H

#include <glib-object.h>

#include "zif-db.h"
#include "zif-package.h"
#include "zif-transaction.h"

G_BEGIN_DECLS

#define ZIF_TYPE_HISTORY		(zif_history_get_type ())
#define ZIF_HISTORY(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_HISTORY, ZifHistory))
#define ZIF_HISTORY_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_HISTORY, ZifHistoryClass))
#define ZIF_IS_HISTORY(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_HISTORY))
#define ZIF_IS_HISTORY_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_HISTORY))
#define ZIF_HISTORY_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_HISTORY, ZifHistoryClass))
#define ZIF_HISTORY_ERROR		(zif_history_error_quark ())

typedef struct _ZifHistory		ZifHistory;
typedef struct _ZifHistoryPrivate	ZifHistoryPrivate;
typedef struct _ZifHistoryClass		ZifHistoryClass;

struct _ZifHistory
{
	GObject				 parent;
	ZifHistoryPrivate		*priv;
};

struct _ZifHistoryClass
{
	GObjectClass			 parent_class;
};

typedef enum {
	ZIF_HISTORY_ERROR_FAILED,
	ZIF_HISTORY_ERROR_FAILED_TO_OPEN,
	ZIF_HISTORY_ERROR_LAST
} ZifHistoryError;

GQuark		 zif_history_error_quark		(void);
GType		 zif_history_get_type			(void);
ZifHistory	*zif_history_new			(void);

gboolean	 zif_history_add_entry			(ZifHistory	*history,
							 ZifPackage	*package,
							 guint		 timestamp,
							 ZifTransactionReason reason,
							 guint		 uid,
							 const gchar	*command_line,
							 GError		**error);
GArray		*zif_history_list_transactions		(ZifHistory	*history,
							 GError		**error);
GPtrArray	*zif_history_get_packages		(ZifHistory	*history,
							 guint		 timestamp,
							 GError		**error);
guint		 zif_history_get_uid			(ZifHistory	*history,
							 ZifPackage	*package,
							 guint		 timestamp,
							 GError		**error);
gchar		*zif_history_get_cmdline		(ZifHistory	*history,
							 ZifPackage	*package,
							 guint		 timestamp,
							 GError		**error);
gchar		*zif_history_get_repo			(ZifHistory	*history,
							 ZifPackage	*package,
							 guint		 timestamp,
							 GError		**error);
ZifTransactionReason zif_history_get_reason		(ZifHistory	*history,
							 ZifPackage	*package,
							 guint		 timestamp,
							 GError		**error);
gchar		*zif_history_get_repo_newest		(ZifHistory	*history,
							 ZifPackage	*package,
							 GError		**error);
gboolean	 zif_history_import			(ZifHistory	*history,
							 ZifDb		*db,
							 GError		**error);
gboolean	 zif_history_set_repo_for_store		(ZifHistory	*history,
							 ZifStore	*store,
							 GError		**error);

G_END_DECLS

#endif /* __ZIF_HISTORY_H */
