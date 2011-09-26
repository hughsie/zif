/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2011 Richard Hughes <richard@hughsie.com>
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

#ifndef __ZIF_PACKAGE_PRIVATE_H
#define __ZIF_PACKAGE_PRIVATE_H

#include "zif-package.h"
#include "zif-string.h"

G_BEGIN_DECLS

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
void			 zif_package_set_source_filename (ZifPackage	*package,
							 ZifString	*source_filename);
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

G_END_DECLS

#endif /* __ZIF_PACKAGE_PRIVATE_H */

