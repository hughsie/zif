/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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

#ifndef __ZIF_UTILS_H
#define __ZIF_UTILS_H

#include <gio/gio.h>
#include <glib-object.h>

#include "zif-state.h"

G_BEGIN_DECLS

#define ZIF_UTILS_ERROR	(zif_utils_error_quark ())

typedef enum {
	ZIF_UTILS_ERROR_FAILED,
	ZIF_UTILS_ERROR_FAILED_TO_READ,
	ZIF_UTILS_ERROR_FAILED_TO_WRITE,
	ZIF_UTILS_ERROR_CANCELLED,
	ZIF_UTILS_ERROR_LAST
} ZifUtilsError;

#define ZIF_PACKAGE_ID_NAME	0
#define ZIF_PACKAGE_ID_VERSION	1
#define ZIF_PACKAGE_ID_ARCH	2
#define ZIF_PACKAGE_ID_DATA	3

gboolean	 zif_init			(void);
GQuark		 zif_utils_error_quark		(void);
void		 zif_list_print_array		(GPtrArray	*array);
gchar		*zif_package_id_from_nevra	(const gchar	*name,
						 guint		 epoch,
						 const gchar	*version,
						 const gchar	*release,
						 const gchar	*arch,
						 const gchar	*data);
gboolean	 zif_boolean_from_text		(const gchar	*text);
gint		 zif_compare_evr		(const gchar	*a,
						 const gchar	*b);
gboolean	 zif_file_untar			(const gchar	*filename,
						 const gchar	*directory,
						 GError		**error);
gboolean	 zif_file_decompress		(const gchar	*in,
						 const gchar	*out,
						 ZifState	*state,
						 GError		**error);
gchar		*zif_file_get_uncompressed_name	(const gchar	*filename);
gboolean	 zif_file_is_compressed_name	(const gchar	*filename);
gchar		**zif_package_id_split		(const gchar	*package_id);
gboolean	 zif_package_id_check		(const gchar	*package_id);
guint		 zif_time_string_to_seconds	(const gchar	*value);
gboolean	 zif_package_convert_evr	(gchar		*evr,
						 const gchar	**epoch,
						 const gchar	**version,
						 const gchar	**release);

G_END_DECLS

#endif /* __ZIF_UTILS_H */

