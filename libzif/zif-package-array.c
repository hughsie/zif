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

/**
 * SECTION:zif-package-package_array
 * @short_description: A helper function to deal with arrays of packages
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

#include "zif-package-array.h"

/**
 * zif_package_array_new:
 *
 * Return value: A new #GPtrArray instance.
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_package_array_new (void)
{
	return g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

/**
 * zif_package_array_get_newest:
 * @array: array of %ZifPackage's
 * @error: a #GError which is used on failure, or %NULL
 *
 * Returns the newest package from a list.
 *
 * Return value: a single %ZifPackage, or %NULL in the case of an error. Use g_object_unref() when done.
 *
 * Since: 0.1.0
 **/
ZifPackage *
zif_package_array_get_newest (GPtrArray *array, GError **error)
{
	ZifPackage *package_newest;
	ZifPackage *package = NULL;
	guint i;
	gint retval;

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no results */
	if (array->len == 0) {
		g_set_error_literal (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED,
				     "nothing in array");
		goto out;
	}

	/* start with the first package being the newest */
	package_newest = g_ptr_array_index (array, 0);

	/* find newest in rest of the array */
	for (i=1; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		retval = zif_package_compare (package, package_newest);
		if (retval > 0)
			package_newest = package;
	}

	/* return reference so we can unref the list */
	package = g_object_ref (package_newest);
out:
	return package;
}


/**
 * zif_package_array_get_oldest:
 * @array: array of %ZifPackage's
 * @error: a #GError which is used on failure, or %NULL
 *
 * Returns the oldest package from a list.
 *
 * Return value: a single %ZifPackage, or %NULL in the case of an error. Use g_object_unref() when done.
 *
 * Since: 0.1.3
 **/
ZifPackage *
zif_package_array_get_oldest (GPtrArray *array, GError **error)
{
	ZifPackage *package_oldest;
	ZifPackage *package = NULL;
	guint i;
	gint retval;

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no results */
	if (array->len == 0) {
		g_set_error_literal (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED,
				     "nothing in array");
		goto out;
	}

	/* start with the first package being the oldest */
	package_oldest = g_ptr_array_index (array, 0);

	/* find oldest in rest of the array */
	for (i=1; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		retval = zif_package_compare (package, package_oldest);
		if (retval < 0)
			package_oldest = package;
	}

	/* return reference so we can unref the list */
	package = g_object_ref (package_oldest);
out:
	return package;
}

/**
 * zif_package_array_filter_newest:
 * @packages: array of %ZifPackage's
 *
 * Filters the list so that only the newest version of a package remains.
 *
 * Return value: %TRUE if the array was modified
 *
 * Since: 0.1.0
 **/
gboolean
zif_package_array_filter_newest (GPtrArray *packages)
{
	guint i;
	GHashTable *hash;
	ZifPackage *package;
	ZifPackage *package_tmp;
	const gchar *name;
	gboolean ret = FALSE;

	/* use a hash so it's O(n) not O(n^2) */
	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	for (i=0; i<packages->len; i++) {
		package = ZIF_PACKAGE (g_ptr_array_index (packages, i));
		name = zif_package_get_name (package);
		if (name == NULL)
			continue;
		package_tmp = g_hash_table_lookup (hash, name);

		/* does not already exist */
		if (package_tmp == NULL) {
			g_hash_table_insert (hash, g_strdup (name), g_object_ref (package));
			continue;
		}

		/* the new package is older */
		if (zif_package_compare (package, package_tmp) < 0) {
			g_debug ("%s is older than %s, so ignoring it",
				   zif_package_get_id (package), zif_package_get_id (package_tmp));
			g_ptr_array_remove_index_fast (packages, i);
			ret = TRUE;
			continue;
		}

		ret = TRUE;
		g_debug ("removing %s", zif_package_get_id (package_tmp));
		g_debug ("adding %s", zif_package_get_id (package));

		/* remove the old one */
		g_hash_table_remove (hash, zif_package_get_name (package_tmp));
		g_hash_table_insert (hash, g_strdup (name), g_object_ref (package));
		g_ptr_array_remove_fast (packages, package_tmp);
	}
	g_hash_table_unref (hash);
	return  ret;
}
