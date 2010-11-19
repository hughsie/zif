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
#include <string.h>

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

/**
 * zif_package_array_filter_best_arch:
 * @array: array of %ZifPackage's
 *
 * Filters the array so that only the best version of a package remains.
 *
 * If we have the following packages:
 *  - glibc.i386
 *  - hal.i386
 *  - glibc.i686
 *
 * Then we output:
 *  - glibc.i686
 *
 * Since: 0.1.3
 **/
void
zif_package_array_filter_best_arch (GPtrArray *array)
{
	ZifPackage *package;
	guint i;
	const gchar *arch;
	const gchar *best_arch = NULL;

	/* find the best arch */
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		arch = zif_package_get_arch (package);
		if (g_strcmp0 (arch, "x86_64") == 0)
			break;
		if (g_strcmp0 (arch, best_arch) > 0) {
			best_arch = arch;
		}
	}

	/* if no obvious best, skip */
	g_debug ("bestarch=%s", best_arch);
	if (best_arch == NULL)
		return;

	/* remove any that are not best */
	for (i=0; i<array->len;) {
		package = g_ptr_array_index (array, i);
		arch = zif_package_get_arch (package);
		if (g_strcmp0 (arch, best_arch) != 0) {
			g_ptr_array_remove_index_fast (array, i);
			continue;
		}
		i++;
	}
}

/**
 * zif_package_array_filter_smallest_name:
 * @array: array of %ZifPackage's
 *
 * Filters the array so that only the smallest name of a package remains.
 *
 * If we have the following packages:
 *  - glibc.i386
 *  - hal.i386
 *
 * Then we output:
 *  - hal.i686
 *
 * As it has the smallest name. I know it's insane, but it's what yum does.
 *
 * Since: 0.1.3
 **/
void
zif_package_array_filter_smallest_name (GPtrArray *array)
{
	ZifPackage *package;
	guint i;
	guint length;
	guint shortest = G_MAXUINT;

	/* find the smallest name */
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		length = strlen (zif_package_get_name (package));
		if (length < shortest)
			shortest = length;
	}

	/* remove entries that are longer than the shortest name */
	for (i=0; i<array->len;) {
		package = g_ptr_array_index (array, i);
		length = strlen (zif_package_get_name (package));
		if (length != shortest) {
			g_ptr_array_remove_index_fast (array, i);
			continue;
		}
		i++;
	}
}

/**
 * zif_package_array_filter_provide:
 * @packages: array of %ZifPackage's
 * @depends: an array of #ZifDepend's
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Filters the list by the dependency satisfiability.
 *
 * Return value: %TRUE if the array was searched successfully
 *
 * Since: 0.1.3
 **/
gboolean
zif_package_array_filter_provide (GPtrArray *array,
				  GPtrArray *depends,
				  ZifState *state,
				  GError **error)
{
	guint i, j;
	gboolean ret = TRUE;
	ZifPackage *package;
	ZifDepend *satisfies = NULL;
	ZifDepend *depend_tmp;
	ZifState *state_local;

	/* shortcut */
	if (array->len == 0)
		goto out;

	/* remove entries that do not satisfy the dep */
	zif_state_set_number_steps (state, array->len);
	for (i=0; i<array->len;) {
		package = g_ptr_array_index (array, i);
		state_local = zif_state_get_child (state);

		/* try each depend as 'OR' */
		for (j=0; j<depends->len; j++) {
			depend_tmp = g_ptr_array_index (depends, j);
			zif_state_reset (state_local); //FIXME
			ret = zif_package_provides (package,
						    depend_tmp,
						    &satisfies,
						    state_local,
						    error);
			if (!ret)
				goto out;
			if (satisfies != NULL)
				break;
		}
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
		if (satisfies == NULL) {
			g_ptr_array_remove_index_fast (array, i);
			continue;
		}
		g_object_unref (satisfies);
		i++;
	}
out:
	return ret;
}

/**
 * zif_package_array_filter_require:
 * @packages: array of %ZifPackage's
 * @depends: an array of #ZifDepend's
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Filters the list by the dependency satisfiability.
 *
 * Return value: %TRUE if the array was searched successfully
 *
 * Since: 0.1.3
 **/
gboolean
zif_package_array_filter_require (GPtrArray *array,
				  GPtrArray *depends,
				  ZifState *state,
				  GError **error)
{
	guint i, j;
	gboolean ret = TRUE;
	ZifPackage *package;
	ZifDepend *satisfies = NULL;
	ZifDepend *depend_tmp;
	ZifState *state_local;

	/* shortcut */
	if (array->len == 0)
		goto out;

	/* remove entries that do not satisfy the dep */
	zif_state_set_number_steps (state, array->len);
	for (i=0; i<array->len;) {
		package = g_ptr_array_index (array, i);
		state_local = zif_state_get_child (state);

		/* try each depend as 'OR' */
		for (j=0; j<depends->len; j++) {
			depend_tmp = g_ptr_array_index (depends, j);
			zif_state_reset (state_local); //FIXME
			ret = zif_package_requires (package,
						    depend_tmp,
						    &satisfies,
						    state_local,
						    error);
			if (!ret)
				goto out;
			if (satisfies != NULL)
				break;
		}
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
		if (satisfies == NULL) {
			g_ptr_array_remove_index_fast (array, i);
			continue;
		}
		g_object_unref (satisfies);
		i++;
	}
out:
	return ret;
}

/**
 * zif_package_array_filter_conflict:
 * @packages: array of %ZifPackage's
 * @depends: an array of #ZifDepend's
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Filters the list by the dependency satisfiability.
 *
 * Return value: %TRUE if the array was searched successfully
 *
 * Since: 0.1.3
 **/
gboolean
zif_package_array_filter_conflict (GPtrArray *array,
				   GPtrArray *depends,
				   ZifState *state,
				   GError **error)
{
	guint i, j;
	gboolean ret = TRUE;
	ZifPackage *package;
	ZifDepend *satisfies = NULL;
	ZifDepend *depend_tmp;
	ZifState *state_local;

	/* shortcut */
	if (array->len == 0)
		goto out;

	/* remove entries that do not satisfy the dep */
	zif_state_set_number_steps (state, array->len);
	for (i=0; i<array->len;) {
		package = g_ptr_array_index (array, i);
		state_local = zif_state_get_child (state);

		/* try each depend as 'OR' */
		for (j=0; j<depends->len; j++) {
			depend_tmp = g_ptr_array_index (depends, j);
			zif_state_reset (state_local); //FIXME
			ret = zif_package_conflicts (package,
						     depend_tmp,
						     &satisfies,
						     state_local,
						     error);
			if (!ret)
				goto out;
			if (satisfies != NULL)
				break;
		}
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
		if (satisfies == NULL) {
			g_ptr_array_remove_index_fast (array, i);
			continue;
		}
		g_object_unref (satisfies);
		i++;
	}
out:
	return ret;
}

/**
 * zif_package_array_filter_obsolete:
 * @packages: array of %ZifPackage's
 * @depends: an array of #ZifDepend's
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Filters the list by the dependency satisfiability.
 *
 * Return value: %TRUE if the array was searched successfully
 *
 * Since: 0.1.3
 **/
gboolean
zif_package_array_filter_obsolete (GPtrArray *array,
				   GPtrArray *depends,
				   ZifState *state,
				   GError **error)
{
	guint i, j;
	gboolean ret = TRUE;
	ZifPackage *package;
	ZifDepend *satisfies = NULL;
	ZifDepend *depend_tmp;
	ZifState *state_local;

	/* shortcut */
	if (array->len == 0)
		goto out;

	/* remove entries that do not satisfy the dep */
	zif_state_set_number_steps (state, array->len);
	for (i=0; i<array->len;) {
		package = g_ptr_array_index (array, i);
		state_local = zif_state_get_child (state);

		/* try each depend as 'OR' */
		for (j=0; j<depends->len; j++) {
			depend_tmp = g_ptr_array_index (depends, j);
			zif_state_reset (state_local); //FIXME
			ret = zif_package_obsoletes (package,
						     depend_tmp,
						     &satisfies,
						     state_local,
						     error);
			if (!ret)
				goto out;
			if (satisfies != NULL)
				break;
		}
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
		if (satisfies == NULL) {
			g_ptr_array_remove_index_fast (array, i);
			continue;
		}
		g_object_unref (satisfies);
		i++;
	}
out:
	return ret;
}

/**
 * zif_package_array_depend:
 **/
static gboolean
zif_package_array_depend (GPtrArray *array,
			   ZifDepend *depend,
			   ZifDepend **best_depend,
			   GPtrArray **provides,
			   ZifPackageEnsureType type,
			   ZifState *state,
			   GError **error)
{
	gboolean ret = TRUE;
	guint i;
	ZifPackage *package_tmp;
	ZifDepend *satisfies = NULL;
	ZifDepend *best_depend_tmp = NULL;

	/* create results array */
	if (provides != NULL)
		*provides = zif_package_array_new ();

	/* interate through the array */
	for (i=0; i<array->len; i++) {
		package_tmp = g_ptr_array_index (array, i);

		/* does this match */
		switch (type) {
		case ZIF_PACKAGE_ENSURE_TYPE_PROVIDES:
			ret = zif_package_provides (package_tmp,
						    depend,
						    &satisfies,
						    state,
						    error);
			break;
		case ZIF_PACKAGE_ENSURE_TYPE_REQUIRES:
			ret = zif_package_requires (package_tmp,
						    depend,
						    &satisfies,
						    state,
						    error);
			break;
		case ZIF_PACKAGE_ENSURE_TYPE_CONFLICTS:
			ret = zif_package_conflicts (package_tmp,
						    depend,
						    &satisfies,
						    state,
						    error);
			break;
		case ZIF_PACKAGE_ENSURE_TYPE_OBSOLETES:
			ret = zif_package_obsoletes (package_tmp,
						    depend,
						    &satisfies,
						    state,
						    error);
			break;
		default:
			g_assert_not_reached ();
		}
		if (!ret)
			goto out;

		/* gotcha, but keep looking */
		if (satisfies != NULL) {
			if (provides != NULL)
				g_ptr_array_add (*provides, g_object_ref (package_tmp));

			/* ensure we track the best depend */
			if (best_depend != NULL &&
			    (best_depend_tmp == NULL ||
			     zif_depend_compare (satisfies, best_depend_tmp) > 0)) {
				if (best_depend_tmp != NULL)
					g_object_unref (best_depend_tmp);
				best_depend_tmp = g_object_ref (satisfies);
			}

			g_object_unref (satisfies);
		}
	}

	/* if we supplied an address for it, return it */
	if (best_depend != NULL) {
		*best_depend = NULL;
		if (best_depend_tmp != NULL)
			*best_depend = g_object_ref (best_depend_tmp);
	}
out:
	if (best_depend_tmp != NULL)
		g_object_unref (best_depend_tmp);
	return ret;
}

/**
 * zif_package_array_provide:
 * @array: array of %ZifPackage's
 * @depend: the dependency to try and satisfy
 * @best_depend: the best matched dependency, free with g_object_unref() if not %NULL
 * @results: the matched dependencies, free with g_ptr_array_unref() if not %NULL
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the package dependencies that satisfy the supplied dependency.
 *
 * Return value: %TRUE if the array was searched.
 * Use @results->len == 0 to detect a missing dependency.
 *
 * Since: 0.1.3
 **/
gboolean
zif_package_array_provide (GPtrArray *array,
			   ZifDepend *depend,
			   ZifDepend **best_depend,
			   GPtrArray **results,
			   ZifState *state,
			   GError **error)
{
	return zif_package_array_depend (array,
					 depend,
					 best_depend,
					 results,
					 ZIF_PACKAGE_ENSURE_TYPE_PROVIDES,
					 state,
					 error);
}

/**
 * zif_package_array_require:
 * @array: array of %ZifPackage's
 * @depend: the dependency to try and satisfy
 * @best_depend: the best matched dependency, free with g_object_unref() if not %NULL
 * @results: the matched dependencies, free with g_ptr_array_unref() if not %NULL
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the package dependencies that satisfy the supplied dependency.
 *
 * Return value: %TRUE if the array was searched.
 * Use @results->len == 0 to detect a missing dependency.
 *
 * Since: 0.1.3
 **/
gboolean
zif_package_array_require (GPtrArray *array,
			   ZifDepend *depend,
			   ZifDepend **best_depend,
			   GPtrArray **results,
			   ZifState *state,
			   GError **error)
{
	return zif_package_array_depend (array,
					 depend,
					 best_depend,
					 results,
					 ZIF_PACKAGE_ENSURE_TYPE_REQUIRES,
					 state,
					 error);
}

/**
 * zif_package_array_conflict:
 * @array: array of %ZifPackage's
 * @depend: the dependency to try and satisfy
 * @best_depend: the best matched dependency, free with g_object_unref() if not %NULL
 * @results: the matched dependencies, free with g_ptr_array_unref() if not %NULL
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the package dependencies that satisfy the supplied dependency.
 *
 * Return value: %TRUE if the array was searched.
 * Use @results->len == 0 to detect a missing dependency.
 *
 * Since: 0.1.3
 **/
gboolean
zif_package_array_conflict (GPtrArray *array,
			    ZifDepend *depend,
			    ZifDepend **best_depend,
			    GPtrArray **results,
			    ZifState *state,
			    GError **error)
{
	return zif_package_array_depend (array,
					 depend,
					 best_depend,
					 results,
					 ZIF_PACKAGE_ENSURE_TYPE_CONFLICTS,
					 state,
					 error);
}

/**
 * zif_package_array_obsolete:
 * @array: array of %ZifPackage's
 * @depend: the dependency to try and satisfy
 * @best_depend: the best matched dependency, free with g_object_unref() if not %NULL
 * @results: the matched dependencies, free with g_ptr_array_unref() if not %NULL
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the package dependencies that satisfy the supplied dependency.
 *
 * Return value: %TRUE if the array was searched.
 * Use @results->len == 0 to detect a missing dependency.
 *
 * Since: 0.1.3
 **/
gboolean
zif_package_array_obsolete (GPtrArray *array,
			    ZifDepend *depend,
			    ZifDepend **best_depend,
			    GPtrArray **results,
			    ZifState *state,
			    GError **error)
{
	return zif_package_array_depend (array,
					 depend,
					 best_depend,
					 results,
					 ZIF_PACKAGE_ENSURE_TYPE_OBSOLETES,
					 state,
					 error);
}
