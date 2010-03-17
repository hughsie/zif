/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#ifndef __ZIF_MD_PRIMARY_H
#define __ZIF_MD_PRIMARY_H

#include <glib-object.h>
#include <packagekit-glib2/packagekit.h>

#include "zif-md.h"

G_BEGIN_DECLS

#define ZIF_TYPE_MD_PRIMARY		(zif_md_primary_get_type ())
#define ZIF_MD_PRIMARY(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_MD_PRIMARY, ZifMdPrimary))
#define ZIF_MD_PRIMARY_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_MD_PRIMARY, ZifMdPrimaryClass))
#define ZIF_IS_MD_PRIMARY(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_MD_PRIMARY))
#define ZIF_IS_MD_PRIMARY_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_MD_PRIMARY))
#define ZIF_MD_PRIMARY_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_MD_PRIMARY, ZifMdPrimaryClass))

typedef struct _ZifMdPrimary		ZifMdPrimary;
typedef struct _ZifMdPrimaryPrivate	ZifMdPrimaryPrivate;
typedef struct _ZifMdPrimaryClass	ZifMdPrimaryClass;

struct _ZifMdPrimary
{
	ZifMd				 parent;
	ZifMdPrimaryPrivate		*priv;
};

struct _ZifMdPrimaryClass
{
	ZifMdClass			 parent_class;
};

GType		 zif_md_primary_get_type		(void);
ZifMdPrimary	*zif_md_primary_new			(void);
GPtrArray	*zif_md_primary_search_file		(ZifMdPrimary		*md,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_md_primary_search_name		(ZifMdPrimary		*md,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_md_primary_search_details		(ZifMdPrimary		*md,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_md_primary_search_group		(ZifMdPrimary		*md,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_md_primary_search_pkgid		(ZifMdPrimary		*md,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_md_primary_search_pkgkey		(ZifMdPrimary		*md,
							 guint			 pkgkey,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_md_primary_what_provides		(ZifMdPrimary		*md,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_md_primary_resolve			(ZifMdPrimary		*md,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_md_primary_get_packages		(ZifMdPrimary		*md,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_md_primary_find_package		(ZifMdPrimary		*md,
							 const gchar		*package_id,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);

G_END_DECLS

#endif /* __ZIF_MD_PRIMARY_H */

