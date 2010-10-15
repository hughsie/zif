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

/**
 * SECTION:zif-string
 * @short_description: Create and manage reference counted strings
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
struct ZifString {
	gchar		*value;
	guint		 count;
};

/**
 * zif_string_new:
 * @value: string to copy
 *
 * Creates a new referenced counted string
 *
 * Return value: New allocated object
 *
 * Since: 0.1.0
 **/
ZifString *
zif_string_new (const gchar *value)
{
	ZifString *string;
	string = g_new0 (ZifString, 1);
	string->count = 1;
	string->value = g_strdup (value);
	return string;
}

/**
 * zif_string_new_value:
 * @value: string to use
 *
 * Creates a new referenced counted string, using the allocated memory.
 * Do not free this string as it is now owned by the #ZifString.
 *
 * Return value: New allocated object
 *
 * Since: 0.1.0
 **/
ZifString *
zif_string_new_value (gchar *value)
{
	ZifString *string;
	string = g_new0 (ZifString, 1);
	string->count = 1;
	string->value = value;
	return string;
}

/**
 * zif_string_get_value:
 * @string: the #ZifString object
 *
 * Returns the string stored in the #ZifString.
 * This value is only valid while the #ZifString's reference count > 1.
 *
 * Return value: string value
 *
 * Since: 0.1.0
 **/
const gchar *
zif_string_get_value (ZifString *string)
{
	g_return_val_if_fail (string != NULL, NULL);
	return string->value;
}

/**
 * zif_string_ref:
 * @string: the #ZifString object
 *
 * Increases the reference count on the object.
 *
 * Return value: the #ZifString object
 *
 * Since: 0.1.0
 **/
ZifString *
zif_string_ref (ZifString *string)
{
	g_return_val_if_fail (string != NULL, NULL);
	string->count++;
	return string;
}

/**
 * zif_string_unref:
 * @string: the #ZifString object
 *
 * Decreses the reference count on the object, and frees the value if
 * it calls to zero.
 *
 * Return value: the #ZifString object
 *
 * Since: 0.1.0
 **/
ZifString *
zif_string_unref (ZifString *string)
{
	g_return_val_if_fail (string != NULL, NULL);
	string->count--;
	if (string->count == 0) {
		g_free (string->value);
		g_free (string);
		string = NULL;
	}
	return string;
}

