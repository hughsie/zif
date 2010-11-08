/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
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

#ifndef __ZIF_RELEASE_H
#define __ZIF_RELEASE_H

#include <glib-object.h>

#include "zif-upgrade.h"

G_BEGIN_DECLS

#define ZIF_TYPE_RELEASE		(zif_release_get_type ())
#define ZIF_RELEASE(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_RELEASE, ZifRelease))
#define ZIF_RELEASE_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_RELEASE, ZifReleaseClass))
#define ZIF_IS_RELEASE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_RELEASE))
#define ZIF_IS_RELEASE_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_RELEASE))
#define ZIF_RELEASE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_RELEASE, ZifReleaseClass))
#define ZIF_RELEASE_ERROR		(zif_release_error_quark ())

typedef struct _ZifRelease		ZifRelease;
typedef struct _ZifReleasePrivate	ZifReleasePrivate;
typedef struct _ZifReleaseClass		ZifReleaseClass;

struct _ZifRelease
{
	GObject				 parent;
	ZifReleasePrivate		*priv;
};

struct _ZifReleaseClass
{
	GObjectClass			 parent_class;
};

typedef enum {
	ZIF_RELEASE_UPGRADE_KIND_MINIMAL,
	ZIF_RELEASE_UPGRADE_KIND_DEFAULT,
	ZIF_RELEASE_UPGRADE_KIND_COMPLETE,
	ZIF_RELEASE_UPGRADE_KIND_LAST
} ZifReleaseUpgradeKind;

typedef enum {
	ZIF_RELEASE_ERROR_DOWNLOAD_FAILED,
	ZIF_RELEASE_ERROR_FILE_INVALID,
	ZIF_RELEASE_ERROR_LOW_DISKSPACE,
	ZIF_RELEASE_ERROR_NOT_FOUND,
	ZIF_RELEASE_ERROR_NOT_SUPPORTED,
	ZIF_RELEASE_ERROR_NO_UUID_FOR_ROOT,
	ZIF_RELEASE_ERROR_SETUP_INVALID,
	ZIF_RELEASE_ERROR_SPAWN_FAILED,
	ZIF_RELEASE_ERROR_WRITE_FAILED,
	ZIF_RELEASE_ERROR_LAST
} ZifReleaseError;

GQuark		 zif_release_error_quark		(void);
GType		 zif_release_get_type			(void);
ZifRelease	*zif_release_new			(void);
void		 zif_release_set_cache_dir		(ZifRelease	*release,
							 const gchar	*cache_dir);
void		 zif_release_set_boot_dir		(ZifRelease	*release,
							 const gchar	*boot_dir);
void		 zif_release_set_repo_dir		(ZifRelease	*release,
							 const gchar	*boot_dir);
void		 zif_release_set_uri			(ZifRelease	*release,
							 const gchar	*uri);
GPtrArray	*zif_release_get_upgrades		(ZifRelease	*release,
							 ZifState	*state,
							 GError		**error);
GPtrArray	*zif_release_get_upgrades_new		(ZifRelease	*release,
							 guint		 version,
							 ZifState	*state,
							 GError		**error);
ZifUpgrade	*zif_release_get_upgrade_for_version	(ZifRelease	*release,
							 guint		 version,
							 ZifState	*state,
							 GError		**error);
gboolean	 zif_release_upgrade_version		(ZifRelease	*release,
							 guint		 version,
							 ZifReleaseUpgradeKind	 upgrade_kind,
							 ZifState	*state,
							 GError		**error);

G_END_DECLS

#endif /* __ZIF_RELEASE_H */
