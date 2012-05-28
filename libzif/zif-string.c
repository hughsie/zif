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

/**
 * SECTION:zif-string
 * @short_description: Reference counted strings
 *
 * To avoid frequent malloc/free, we use reference counted strings to
 * optimise many of the zif internals.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

#include "zif-utils.h"
#include "zif-string.h"

/* private structure */
typedef struct {
	gchar		*value;
	guint		 count;
	gboolean	 is_static;
} ZifStringInternal;

/**
 * zif_string_new: (skip):
 * @value: string to copy
 *
 * Creates a new referenced counted string
 *
 * Return value: New allocated string
 *
 * Since: 0.1.0
 **/
ZifString *
zif_string_new (const gchar *value)
{
	ZifStringInternal *string;
	string = g_slice_new (ZifStringInternal);
	string->count = 1;
	string->value = g_strdup (value);
	string->is_static = FALSE;
	return (ZifString *) string;
}

/**
 * zif_string_new_value: (skip):
 * @value: string to use
 *
 * Creates a new referenced counted string, using the allocated memory.
 * Do not free this string as it is now owned by the #ZifString.
 *
 * Return value: New allocated string
 *
 * Since: 0.1.0
 **/
ZifString *
zif_string_new_value (gchar *value)
{
	ZifStringInternal *string;
	string = g_slice_new (ZifStringInternal);
	string->count = 1;
	string->value = value;
	string->is_static = TRUE;
	return (ZifString *) string;
}

/**
 * zif_string_new_static: (skip):
 * @value: string to use
 *
 * Creates a new referenced counted string, using the static memory.
 * You MUST not free the static string that backs this object. Use this
 * function with care.
 *
 * Return value: New allocated string
 *
 * Since: 0.1.3
 **/
ZifString *
zif_string_new_static (const gchar *value)
{
	ZifStringInternal *string;
	string = g_slice_new (ZifStringInternal);
	string->count = 1;
	string->value = (gchar*) value;
	string->is_static = TRUE;
	return (ZifString *) string;
}

/**
 * zif_string_ref: (skip):
 * @string: A #ZifString
 *
 * Increases the reference count on the object.
 *
 * Return value: A #ZifString
 *
 * Since: 0.1.0
 **/
ZifString *
zif_string_ref (ZifString *string)
{
	ZifStringInternal *internal = (ZifStringInternal *) string;
	g_return_val_if_fail (internal != NULL, NULL);
	internal->count++;
	return string;
}

/**
 * zif_string_unref: (skip):
 * @string: A #ZifString
 *
 * Decreses the reference count on the object, and frees the value if
 * it calls to zero.
 *
 * Return value: A #ZifString
 *
 * Since: 0.1.0
 **/
ZifString *
zif_string_unref (ZifString *string)
{
	ZifStringInternal *internal = (ZifStringInternal *) string;
	g_return_val_if_fail (internal != NULL, NULL);
	internal->count--;
	if (internal->count == 0) {
		if (!internal->is_static)
			g_free (internal->value);
		g_slice_free (ZifStringInternal, internal);
		internal = NULL;
	}
	return (ZifString *) internal;
}

