/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
 *
 * Some ideas have been taken from:
 *
 * yum, Copyright (C) 2002 - 2010 Seth Vidal <skvidal@fedoraproject.org>
 * low, Copyright (C) 2008 - 2010 James Bowes <jbowes@repl.ca>
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

#ifndef __ZIF_TRANSACTION_H
#define __ZIF_TRANSACTION_H

#include <glib-object.h>

#include "zif-state.h"
#include "zif-package.h"

G_BEGIN_DECLS

#define ZIF_TYPE_TRANSACTION		(zif_transaction_get_type ())
#define ZIF_TRANSACTION(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_TRANSACTION, ZifTransaction))
#define ZIF_TRANSACTION_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_TRANSACTION, ZifTransactionClass))
#define ZIF_IS_TRANSACTION(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_TRANSACTION))
#define ZIF_IS_TRANSACTION_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_TRANSACTION))
#define ZIF_TRANSACTION_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_TRANSACTION, ZifTransactionClass))
#define ZIF_TRANSACTION_ERROR		(zif_transaction_error_quark ())

typedef struct _ZifTransaction		ZifTransaction;
typedef struct _ZifTransactionPrivate	ZifTransactionPrivate;
typedef struct _ZifTransactionClass	ZifTransactionClass;

struct _ZifTransaction
{
	GObject				 parent;
	ZifTransactionPrivate		*priv;
};

struct _ZifTransactionClass
{
	GObjectClass			 parent_class;
};

typedef enum {
	ZIF_TRANSACTION_ERROR_FAILED,
	ZIF_TRANSACTION_ERROR_LAST
} ZifTransactionError;

GQuark		 zif_transaction_error_quark		(void);
GType		 zif_transaction_get_type		(void);

ZifTransaction	*zif_transaction_new			(void);
gboolean	 zif_transaction_add_install		(ZifTransaction	*transaction,
							 ZifPackage	*package,
							 GError		**error);
gboolean	 zif_transaction_add_update		(ZifTransaction	*transaction,
							 ZifPackage	*package,
							 GError		**error);
gboolean	 zif_transaction_add_remove		(ZifTransaction	*transaction,
							 ZifPackage	*package,
							 GError		**error);
gboolean	 zif_transaction_resolve		(ZifTransaction	*transaction,
							 ZifState	*state,
							 GError		**error);

G_END_DECLS

#endif /* __ZIF_TRANSACTION_H */
