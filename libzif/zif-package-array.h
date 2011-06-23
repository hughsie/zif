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

#ifndef __ZIF_PACKAGE_ARRAY_H
#define __ZIF_PACKAGE_ARRAY_H

#include <glib.h>

#include "zif-package.h"
#include "zif-depend.h"

G_BEGIN_DECLS

GPtrArray	*zif_package_array_new			(void);
ZifPackage	*zif_package_array_get_newest		(GPtrArray	*array,
							 GError		**error);
ZifPackage	*zif_package_array_get_oldest		(GPtrArray	*array,
							 GError		**error);
gboolean	 zif_package_array_filter_newest	(GPtrArray	*packages);
void		 zif_package_array_filter_best_arch	(GPtrArray	*array,
							 const gchar	*arch);
void		 zif_package_array_filter_arch		(GPtrArray	*array,
							 const gchar	*arch);
void		 zif_package_array_filter_smallest_name	(GPtrArray	*array);
gboolean	 zif_package_array_filter_provide	(GPtrArray	*array,
							 GPtrArray	*depends,
							 ZifState	*state,
							 GError		**error);
gboolean	 zif_package_array_filter_require	(GPtrArray	*array,
							 GPtrArray	*depends,
							 ZifState	*state,
							 GError		**error);
gboolean	 zif_package_array_filter_obsolete	(GPtrArray	*array,
							 GPtrArray	*depends,
							 ZifState	*state,
							 GError		**error);
gboolean	 zif_package_array_filter_conflict	(GPtrArray	*array,
							 GPtrArray	*depends,
							 ZifState	*state,
							 GError		**error);
gboolean	 zif_package_array_provide		(GPtrArray	*array,
							 ZifDepend	*depend,
							 ZifDepend	**best_depend,
							 GPtrArray	**results,
							 ZifState	*state,
							 GError		**error);
gboolean	 zif_package_array_require		(GPtrArray	*array,
							 ZifDepend	*depend,
							 ZifDepend	**best_depend,
							 GPtrArray	**results,
							 ZifState	*state,
							 GError		**error);
gboolean	 zif_package_array_conflict		(GPtrArray	*array,
							 ZifDepend	*depend,
							 ZifDepend	**best_depend,
							 GPtrArray	**results,
							 ZifState	*state,
							 GError		**error);
gboolean	 zif_package_array_obsolete		(GPtrArray	*array,
							 ZifDepend	*depend,
							 ZifDepend	**best_depend,
							 GPtrArray	**results,
							 ZifState	*state,
							 GError		**error);

G_END_DECLS

#endif /* __ZIF_PACKAGE_ARRAY_H */

