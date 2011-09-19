/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2010 Richard Hughes <richard@hughsie.com>
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

#ifndef __ZIF_PACKAGE_H
#define __ZIF_PACKAGE_H

#include <glib-object.h>
#include <gio/gio.h>

#include "zif-depend.h"
#include "zif-state.h"
#include "zif-string.h"

G_BEGIN_DECLS

#define ZIF_TYPE_PACKAGE		(zif_package_get_type ())
#define ZIF_PACKAGE(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_PACKAGE, ZifPackage))
#define ZIF_PACKAGE_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_PACKAGE, ZifPackageClass))
#define ZIF_IS_PACKAGE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_PACKAGE))
#define ZIF_IS_PACKAGE_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_PACKAGE))
#define ZIF_PACKAGE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_PACKAGE, ZifPackageClass))
#define ZIF_PACKAGE_ERROR		(zif_package_error_quark ())

typedef struct _ZifPackage		ZifPackage;
typedef struct _ZifPackagePrivate	ZifPackagePrivate;
typedef struct _ZifPackageClass		ZifPackageClass;

#include "zif-update.h"

typedef enum {
	ZIF_PACKAGE_ENSURE_TYPE_FILES,
	ZIF_PACKAGE_ENSURE_TYPE_SUMMARY,
	ZIF_PACKAGE_ENSURE_TYPE_LICENCE,
	ZIF_PACKAGE_ENSURE_TYPE_DESCRIPTION,
	ZIF_PACKAGE_ENSURE_TYPE_URL,
	ZIF_PACKAGE_ENSURE_TYPE_SIZE,
	ZIF_PACKAGE_ENSURE_TYPE_GROUP,
	ZIF_PACKAGE_ENSURE_TYPE_CATEGORY,
	ZIF_PACKAGE_ENSURE_TYPE_REQUIRES,
	ZIF_PACKAGE_ENSURE_TYPE_PROVIDES,
	ZIF_PACKAGE_ENSURE_TYPE_CONFLICTS,
	ZIF_PACKAGE_ENSURE_TYPE_OBSOLETES,
	ZIF_PACKAGE_ENSURE_TYPE_CACHE_FILENAME,
	ZIF_PACKAGE_ENSURE_TYPE_LAST
} ZifPackageEnsureType;

typedef enum {
	ZIF_PACKAGE_TRUST_KIND_UNKNOWN, /* must be first */
	ZIF_PACKAGE_TRUST_KIND_NONE,
	ZIF_PACKAGE_TRUST_KIND_PUBKEY,
	ZIF_PACKAGE_TRUST_KIND_LAST
} ZifPackageTrustKind;

typedef enum {
	ZIF_PACKAGE_COMPARE_MODE_VERSION,
	ZIF_PACKAGE_COMPARE_MODE_DISTRO,
	ZIF_PACKAGE_COMPARE_MODE_UNKNOWN
} ZifPackageCompareMode;

typedef enum {
	ZIF_PACKAGE_COMPARE_FLAG_CHECK_NAME = 1,
	ZIF_PACKAGE_COMPARE_FLAG_CHECK_ARCH = 2
} ZifPackageCompareFlags;

struct _ZifPackage
{
	GObject			 parent;
	ZifPackagePrivate	*priv;
};

struct _ZifPackageClass
{
	GObjectClass	parent_class;

	/* vtable */
	gboolean	 (*ensure_data)			(ZifPackage	*package,
							 ZifPackageEnsureType type,
							 ZifState	*state,
							 GError		**error);
};

typedef enum {
	ZIF_PACKAGE_ERROR_FAILED,
	ZIF_PACKAGE_ERROR_NO_SUPPORT,
	ZIF_PACKAGE_ERROR_LAST
} ZifPackageError;

GType			 zif_package_get_type		(void);
GQuark			 zif_package_error_quark	(void);
ZifPackage		*zif_package_new		(void);

/* public getters */
const gchar		*zif_package_get_id		(ZifPackage	*package);
const gchar		*zif_package_get_printable	(ZifPackage	*package);
const gchar		*zif_package_get_name_arch	(ZifPackage	*package);
const gchar		*zif_package_get_name_version	(ZifPackage	*package);
const gchar		*zif_package_get_name_version_arch (ZifPackage	*package);
const gchar		*zif_package_get_name		(ZifPackage	*package);
const gchar		*zif_package_get_version	(ZifPackage	*package);
const gchar		*zif_package_get_arch		(ZifPackage	*package);
const gchar		*zif_package_get_data		(ZifPackage	*package);
ZifPackageTrustKind	 zif_package_get_trust_kind	(ZifPackage	*package);
const gchar		*zif_package_get_summary	(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
const gchar		*zif_package_get_description	(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
const gchar		*zif_package_get_license	(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
const gchar		*zif_package_get_url		(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
const gchar		*zif_package_get_filename	(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
const gchar		*zif_package_get_category	(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
const gchar		*zif_package_get_group		(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
const gchar		*zif_package_get_pkgid		(ZifPackage	*package);
const gchar		*zif_package_get_cache_filename	(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
GFile			*zif_package_get_cache_file	(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
guint64			 zif_package_get_size		(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
GPtrArray		*zif_package_get_files		(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
GPtrArray		*zif_package_get_requires	(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
GPtrArray		*zif_package_get_provides	(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
GPtrArray		*zif_package_get_obsoletes	(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
GPtrArray		*zif_package_get_conflicts	(ZifPackage	*package,
							 ZifState	*state,
							 GError		**error);
guint64			 zif_package_get_time_file	(ZifPackage	*package);

gboolean		 zif_package_provides		(ZifPackage	*package,
							 ZifDepend	*depend,
							 ZifDepend	**satisfies,
							 ZifState	*state,
							 GError		**error);
gboolean		 zif_package_conflicts		(ZifPackage	*package,
							 ZifDepend	*depend,
							 ZifDepend	**satisfies,
							 ZifState	*state,
							 GError		**error);
gboolean		 zif_package_requires		(ZifPackage	*package,
							 ZifDepend	*depend,
							 ZifDepend	**satisfies,
							 ZifState	*state,
							 GError		**error);
gboolean		 zif_package_obsoletes		(ZifPackage	*package,
							 ZifDepend	*depend,
							 ZifDepend	**satisfies,
							 ZifState	*state,
							 GError		**error);

/* internal setters */
gboolean		 zif_package_set_id		(ZifPackage	*package,
							 const gchar	*package_id,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
void			 zif_package_set_repo_id	(ZifPackage	*package,
							 const gchar	*repo_id);
void			 zif_package_set_installed	(ZifPackage	*package,
							 gboolean	 installed);
void			 zif_package_set_trust_kind	(ZifPackage	*package,
							 ZifPackageTrustKind trust_kind);
void			 zif_package_set_summary	(ZifPackage	*package,
							 ZifString	*summary);
void			 zif_package_set_description	(ZifPackage	*package,
							 ZifString	*description);
void			 zif_package_set_license	(ZifPackage	*package,
							 ZifString	*license);
void			 zif_package_set_url		(ZifPackage	*package,
							 ZifString	*url);
void			 zif_package_set_location_href	(ZifPackage	*package,
							 ZifString	*location_href);
void			 zif_package_set_category	(ZifPackage	*package,
							 ZifString	*category);
void			 zif_package_set_group		(ZifPackage	*package,
							 ZifString	*group);
void			 zif_package_set_pkgid		(ZifPackage	*package,
							 ZifString	*pkgid);
void			 zif_package_set_cache_filename	(ZifPackage	*package,
							 const gchar	*cache_filename);
void			 zif_package_set_size		(ZifPackage	*package,
							 guint64	 size);
void			 zif_package_add_file		(ZifPackage	*package,
							 const gchar	*filename);
void			 zif_package_set_files		(ZifPackage	*package,
							 GPtrArray	*files);
void			 zif_package_add_require	(ZifPackage	*package,
							 ZifDepend	*depend);
void			 zif_package_add_provide	(ZifPackage	*package,
							 ZifDepend	*depend);
void			 zif_package_add_obsolete	(ZifPackage	*package,
							 ZifDepend	*depend);
void			 zif_package_add_conflict	(ZifPackage	*package,
							 ZifDepend	*depend);
void			 zif_package_set_requires	(ZifPackage	*package,
							 GPtrArray	*requires);
void			 zif_package_set_provides	(ZifPackage	*package,
							 GPtrArray	*provides);
void			 zif_package_set_obsoletes	(ZifPackage	*package,
							 GPtrArray	*obsoletes);
void			 zif_package_set_conflicts	(ZifPackage	*package,
							 GPtrArray	*conflicts);
void			 zif_package_set_time_file	(ZifPackage	*package,
							 guint64	 time_file);
const gchar		*zif_package_get_package_id	(ZifPackage	*package);
void			 zif_package_print		(ZifPackage	*package);
gboolean		 zif_package_is_devel		(ZifPackage	*package);
gboolean		 zif_package_is_gui		(ZifPackage	*package);
gboolean		 zif_package_is_installed	(ZifPackage	*package);
gboolean		 zif_package_is_free		(ZifPackage	*package);
gboolean		 zif_package_is_native		(ZifPackage	*package);
gint			 zif_package_compare		(ZifPackage	*a,
							 ZifPackage	*b);
gint			 zif_package_compare_full	(ZifPackage	*a,
							 ZifPackage	*b,
							 ZifPackageCompareFlags flags);
gboolean		 zif_package_is_compatible_arch	(ZifPackage	*a,
							 ZifPackage	*b);
const gchar		*zif_package_ensure_type_to_string (ZifPackageEnsureType type);
const gchar		*zif_package_trust_kind_to_string (ZifPackageTrustKind trust_kind);
void			 zif_package_set_compare_mode	(ZifPackage	*package,
							 ZifPackageCompareMode compare_mode);
ZifPackageCompareMode	 zif_package_compare_mode_from_string (const gchar *value);
const gchar		*zif_package_compare_mode_to_string (ZifPackageCompareMode value);

G_END_DECLS

#endif /* __ZIF_PACKAGE_H */

