/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2010 Richard Hughes <richard@hughsie.com>
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

#ifndef __ZIF_MD_H
#define __ZIF_MD_H

#include <glib-object.h>
#include <gio/gio.h>

#include "zif-depend.h"
#include "zif-md.h"
#include "zif-state.h"
#include "zif-store.h"

G_BEGIN_DECLS

#define ZIF_TYPE_MD		(zif_md_get_type ())
#define ZIF_MD(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_MD, ZifMd))
#define ZIF_MD_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_MD, ZifMdClass))
#define ZIF_IS_MD(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_MD))
#define ZIF_IS_MD_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_MD))
#define ZIF_MD_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_MD, ZifMdClass))
#define ZIF_MD_ERROR		(zif_md_error_quark ())

typedef struct _ZifMd		ZifMd;
typedef struct _ZifMdPrivate	ZifMdPrivate;
typedef struct _ZifMdClass	ZifMdClass;

struct _ZifMd
{
	GObject			 parent;
	ZifMdPrivate		*priv;
	/* Padding for future expansion */
	void (*_zif_reserved1) (void);
	void (*_zif_reserved2) (void);
	void (*_zif_reserved3) (void);
	void (*_zif_reserved4) (void);
};

struct _ZifMdClass
{
	GObjectClass				 parent_class;
	/* vtable */
	gboolean	 (*load)		(ZifMd			*md,
						 ZifState		*state,
						 GError			**error);
	gboolean	 (*unload)		(ZifMd			*md,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*search_file)		(ZifMd			*md,
						 gchar			**search,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*search_name)		(ZifMd			*md,
						 gchar			**search,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*search_details)	(ZifMd			*md,
						 gchar			**search,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*search_group)	(ZifMd			*md,
						 gchar			**search,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*search_pkgid)	(ZifMd			*md,
						 gchar			**search,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*what_provides)	(ZifMd			*md,
						 GPtrArray		*depends,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*what_requires)	(ZifMd			*md,
						 GPtrArray		*depends,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*what_obsoletes)	(ZifMd			*md,
						 GPtrArray		*depends,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*what_conflicts)	(ZifMd			*md,
						 GPtrArray		*depends,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*resolve)		(ZifMd			*md,
						 gchar			**search,
						 ZifStoreResolveFlags	 flags,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*get_packages)	(ZifMd			*md,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*find_package)	(ZifMd			*md,
						 const gchar		*package_id,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*get_changelog)	(ZifMd			*md,
						 const gchar		*pkgid,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*get_files)		(ZifMd			*md,
						 ZifPackage		*package,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*get_provides)	(ZifMd			*md,
						 ZifPackage		*package,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*get_requires)	(ZifMd			*md,
						 ZifPackage		*package,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*get_obsoletes)	(ZifMd			*md,
						 ZifPackage		*package,
						 ZifState		*state,
						 GError			**error);
	GPtrArray	*(*get_conflicts)	(ZifMd			*md,
						 ZifPackage		*package,
						 ZifState		*state,
						 GError			**error);
	/* TODO: next time we break API, add padding! */
};

/* types of metadata */
typedef enum {
	ZIF_MD_KIND_UNKNOWN,
	ZIF_MD_KIND_PRIMARY_SQL,
	ZIF_MD_KIND_PRIMARY_XML,
	ZIF_MD_KIND_FILELISTS_SQL,
	ZIF_MD_KIND_FILELISTS_XML,
	ZIF_MD_KIND_OTHER_SQL,
	ZIF_MD_KIND_OTHER_XML,
	ZIF_MD_KIND_COMPS_GZ,
	ZIF_MD_KIND_COMPS,
	ZIF_MD_KIND_METALINK,
	ZIF_MD_KIND_MIRRORLIST,
	ZIF_MD_KIND_PRESTODELTA,
	ZIF_MD_KIND_UPDATEINFO,
	ZIF_MD_KIND_PKGTAGS,
	ZIF_MD_KIND_LAST
} ZifMdKind;

typedef enum {
	ZIF_MD_ERROR_FAILED,
	ZIF_MD_ERROR_NO_SUPPORT,
	ZIF_MD_ERROR_FAILED_TO_LOAD,
	ZIF_MD_ERROR_FAILED_AS_OFFLINE,
	ZIF_MD_ERROR_FAILED_DOWNLOAD,
	ZIF_MD_ERROR_BAD_SQL,
	ZIF_MD_ERROR_FILE_TOO_OLD,
	ZIF_MD_ERROR_NO_FILENAME,
	ZIF_MD_ERROR_FILE_NOT_EXISTS,
	ZIF_MD_ERROR_CHECKSUM_INVALID,
	ZIF_MD_ERROR_LAST
} ZifMdError;

GType		 zif_md_get_type			(void);
GQuark		 zif_md_error_quark			(void);
ZifMd		*zif_md_new				(void);

/* setters */
void		 zif_md_set_store			(ZifMd		*md,
							 ZifStore	*remote);
void		 zif_md_set_id				(ZifMd		*md,
							 const gchar	*id);
void		 zif_md_set_filename			(ZifMd		*md,
							 const gchar	*filename);
void		 zif_md_set_timestamp			(ZifMd		*md,
							 guint		 timestamp);
void		 zif_md_set_location			(ZifMd		*md,
							 const gchar	*location);
void		 zif_md_set_checksum			(ZifMd		*md,
							 const gchar	*checksum);
void		 zif_md_set_checksum_uncompressed	(ZifMd		*md,
							 const gchar	*checksum_uncompressed);
void		 zif_md_set_checksum_type		(ZifMd		*md,
							 GChecksumType	 checksum_type);
void		 zif_md_set_max_age			(ZifMd		*md,
							 guint64	 max_age);
const gchar	*zif_md_kind_to_text			(ZifMdKind	 type);

/* getters */
gboolean	 zif_md_get_is_loaded			(ZifMd		*md);
ZifStore	*zif_md_get_store			(ZifMd		*md);
const gchar	*zif_md_get_id				(ZifMd		*md);
ZifMdKind	 zif_md_get_kind			(ZifMd		*md);
const gchar	*zif_md_get_filename			(ZifMd		*md);
const gchar	*zif_md_get_filename_uncompressed	(ZifMd		*md);
const gchar	*zif_md_get_location			(ZifMd		*md);

/* actions */
gboolean	 zif_md_load				(ZifMd		*md,
							 ZifState	*state,
							 GError		**error);
gboolean	 zif_md_unload				(ZifMd		*md,
							 ZifState	*state,
							 GError		**error);
gboolean	 zif_md_clean				(ZifMd		*md,
							 GError		**error);
gboolean	 zif_md_file_check			(ZifMd		*md,
							 gboolean	 use_uncompressed,
							 gboolean	*valid,
							 ZifState	*state,
							 GError		**error);
gboolean	 zif_md_check_compressed		(ZifMd		*md,
							 ZifState	*state,
							 GError		**error);
gboolean	 zif_md_check_uncompressed		(ZifMd		*md,
							 ZifState	*state,
							 GError		**error);
GPtrArray	*zif_md_search_file			(ZifMd		*md,
							 gchar		**search,
							 ZifState	*state,
							 GError		**error);
GPtrArray	*zif_md_search_name			(ZifMd		*md,
							 gchar		**search,
							 ZifState	*state,
							 GError		**error);
GPtrArray	*zif_md_search_details			(ZifMd		*md,
							 gchar		**search,
							 ZifState	*state,
							 GError		**error);
GPtrArray	*zif_md_search_group			(ZifMd		*md,
							 gchar		**search,
							 ZifState	*state,
							 GError		**error);
GPtrArray	*zif_md_search_pkgid			(ZifMd		*md,
							 gchar		**search,
							 ZifState	*state,
							 GError		**error);
GPtrArray	*zif_md_what_provides			(ZifMd		*md,
							 GPtrArray	*depends,
							 ZifState	*state,
							 GError		**error);
GPtrArray	*zif_md_what_requires			(ZifMd		*md,
							 GPtrArray	*depends,
							 ZifState	*state,
							 GError		**error);
GPtrArray	*zif_md_what_obsoletes			(ZifMd		*md,
							 GPtrArray	*depends,
							 ZifState	*state,
							 GError		**error);
GPtrArray	*zif_md_what_conflicts			(ZifMd		*md,
							 GPtrArray	*depends,
							 ZifState	*state,
							 GError		**error);
GPtrArray	*zif_md_resolve				(ZifMd		*md,
							 gchar		**search,
							 ZifState	*state,
							 GError		**error);
GPtrArray	*zif_md_resolve_full			(ZifMd		*md,
							 gchar		**search,
							 ZifStoreResolveFlags flags,
							 ZifState	*state,
							 GError		**error);
GPtrArray	*zif_md_get_packages			(ZifMd		*md,
							 ZifState	*state,
							 GError		**error);
GPtrArray	*zif_md_find_package			(ZifMd		*md,
							 const gchar	*package_id,
							 ZifState	*state,
							 GError		**error);
GPtrArray	*zif_md_get_changelog			(ZifMd		*md,
							 const gchar	*pkgid,
							 ZifState	*state,
							 GError		**error);
GPtrArray	*zif_md_get_files			(ZifMd		*md,
							 ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
GPtrArray	*zif_md_get_requires			(ZifMd		*md,
							 ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
GPtrArray	*zif_md_get_provides			(ZifMd		*md,
							 ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
GPtrArray	*zif_md_get_obsoletes			(ZifMd		*md,
							 ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
GPtrArray	*zif_md_get_conflicts			(ZifMd		*md,
							 ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);

G_END_DECLS

#endif /* __ZIF_MD_H */

