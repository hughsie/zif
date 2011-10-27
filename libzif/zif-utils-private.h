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

#ifndef __ZIF_UTILS_PRIVATE_H
#define __ZIF_UTILS_PRIVATE_H

#include <gio/gio.h>
#include <glib-object.h>

#include "zif-utils.h"

G_BEGIN_DECLS

gboolean	 zif_init			(void);
GKeyFile	*zif_load_multiline_key_file	(const gchar	*filename,
						 GError		**error);

typedef gboolean (*ZifStrCompareFunc)		(const gchar	*a,
						 const gchar	*b);
gboolean	 zif_str_compare_regex		(const gchar	*a,
						 const gchar	*b);
gboolean	 zif_str_compare_glob		(const gchar	*a,
						 const gchar	*b);
gboolean	 zif_str_compare_equal		(const gchar	*a,
						 const gchar	*b);
guint		 zif_string_replace		(GString	*string,
						 const gchar	*search,
						 const gchar	*replace);
gboolean	 zif_ensure_parent_dir_exists	(const gchar	*filename,
						 GCancellable	*cancellable,
						 GError		**error);

G_END_DECLS

#endif /* __ZIF_UTILS_PRIVATE_H */

