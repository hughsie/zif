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

/**
 * SECTION:zif-package
 * @short_description: Generic object to represent an installed or remote package.
 *
 * This object is subclassed by #ZifPackageLocal and #ZifPackageRemote.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>

#include "zif-depend.h"
#include "zif-utils.h"
#include "zif-config.h"
#include "zif-package.h"
#include "zif-repos.h"
#include "zif-string.h"
#include "zif-legal.h"

#define ZIF_PACKAGE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_PACKAGE, ZifPackagePrivate))

struct _ZifPackagePrivate
{
	gchar			**package_id_split;
	gchar			*package_id;
	ZifString		*summary;
	ZifString		*description;
	ZifString		*license;
	ZifString		*url;
	ZifString		*category;
	ZifString		*location_href;
	ZifString		*group;
	guint64			 size;
	guint64			 time_file;
	GPtrArray		*files;
	GPtrArray		*requires;
	GPtrArray		*provides;
	gboolean		 installed;
};

G_DEFINE_TYPE (ZifPackage, zif_package, G_TYPE_OBJECT)

/**
 * zif_package_error_quark:
 *
 * Return value: Our personal error quark.
 *
 * Since: 0.1.0
 **/
GQuark
zif_package_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("zif_package_error");
	return quark;
}

/**
 * zif_package_compare:
 * @a: the first package to compare
 * @b: the second package to compare
 *
 * Compares one package versions against each other.
 *
 * Return value: 1 for a>b, 0 for a==b, -1 for b>a, or G_MAXINT for error
 *
 * Since: 0.1.0
 **/
gint
zif_package_compare (ZifPackage *a, ZifPackage *b)
{
	gchar **splita;
	gchar **splitb;
	gint val = G_MAXINT;

	g_return_val_if_fail (ZIF_IS_PACKAGE (a), G_MAXINT);
	g_return_val_if_fail (ZIF_IS_PACKAGE (b), G_MAXINT);

	/* no-copy */
	splita = a->priv->package_id_split;
	splitb = b->priv->package_id_split;

	g_return_val_if_fail (splita != NULL, G_MAXINT);
	g_return_val_if_fail (splitb != NULL, G_MAXINT);

	/* check name the same */
	if (g_strcmp0 (splita[ZIF_PACKAGE_ID_NAME], splitb[ZIF_PACKAGE_ID_NAME]) != 0)
		goto out;

	/* do a version compare */
	val = zif_compare_evr (splita[ZIF_PACKAGE_ID_VERSION], splitb[ZIF_PACKAGE_ID_VERSION]);

	/* if the packages are equal, prefer the same architecture */
	if (val == 0)
		val = g_strcmp0 (splitb[ZIF_PACKAGE_ID_ARCH], splita[ZIF_PACKAGE_ID_ARCH]);
out:
	return val;
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
 * zif_package_get_store_for_package:
 **/
static ZifStoreRemote *
zif_package_get_store_for_package (ZifPackage *package, ZifState *state, GError **error)
{
	ZifStoreRemote *store_remote;
	ZifRepos *repos;

	g_return_val_if_fail (zif_state_valid (state), NULL);

	repos = zif_repos_new ();
	store_remote = zif_repos_get_store (repos, package->priv->package_id_split[ZIF_PACKAGE_ID_DATA],
					    state, error);
	g_object_unref (repos);
	return store_remote;
}

/**
 * zif_package_download:
 * @package: the #ZifPackage object
 * @directory: the local directory to save to
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Downloads a package.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_package_download (ZifPackage *package, const gchar *directory, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	ZifStoreRemote *store_remote = NULL;
	GError *error_local = NULL;
	ZifState *state_local = NULL;

	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (directory != NULL, FALSE);
	g_return_val_if_fail (package->priv->package_id_split != NULL, FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check we are not installed */
	if (package->priv->installed) {
		g_set_error_literal (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED,
				     "cannot download installed packages");
		goto out;
	}

	/* two steps, TODO: the second will take longer than the first */
	zif_state_set_number_steps (state, 2);

	/* find correct repo */
	state_local = zif_state_get_child (state);
	store_remote = zif_package_get_store_for_package (package, state_local, &error_local);
	if (store_remote == NULL) {
		g_set_error (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED,
			     "cannot find remote store: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* create a chain of states */
	state_local = zif_state_get_child (state);

	/* download from the store */
	ret = zif_store_remote_download (store_remote, zif_string_get_value (package->priv->location_href), directory, state_local, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED,
			     "cannot download from store: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	if (store_remote != NULL)
		g_object_unref (store_remote);
	return ret;
}

/**
 * zif_package_get_update_detail:
 * @package: the #ZifPackage object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the update detail for a package.
 *
 * Return value: a %ZifUpdate, or %NULL for failure
 *
 * Since: 0.1.0
 **/
ZifUpdate *
zif_package_get_update_detail (ZifPackage *package, ZifState *state, GError **error)
{
	gboolean ret;
	ZifUpdate *update = NULL;
	ZifUpdate *update_tmp = NULL;
	ZifStoreRemote *store_remote = NULL;
	GError *error_local = NULL;
	ZifState *state_local = NULL;

	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->package_id_split != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* check we are not installed */
	if (package->priv->installed) {
		g_set_error_literal (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED,
				     "cannot get details for installed packages");
		goto out;
	}

	/* two steps */
	zif_state_set_number_steps (state, 2);

	/* find correct repo */
	state_local = zif_state_get_child (state);
	store_remote = zif_package_get_store_for_package (package, state_local, &error_local);
	if (store_remote == NULL) {
		g_set_error (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED,
			     "cannot find remote store: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* download from the store */
	state_local = zif_state_get_child (state);
	update_tmp = zif_store_remote_get_update_detail (store_remote, package->priv->package_id, state_local, &error_local);
	if (update_tmp == NULL) {
		g_set_error (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED,
			     "cannot get update detail from store: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	update = g_object_ref (update_tmp);
out:
	if (store_remote != NULL)
		g_object_unref (store_remote);
	if (update_tmp != NULL)
		g_object_unref (update_tmp);
	return update;
}

/**
 * zif_package_print:
 * @package: the #ZifPackage object
 *
 * Prints details about a package to %STDOUT.
 *
 * Since: 0.1.0
 **/
void
zif_package_print (ZifPackage *package)
{
	guint i;
	gchar *text;
	const ZifDepend *depend;
	GPtrArray *array;

	g_return_if_fail (ZIF_IS_PACKAGE (package));
	g_return_if_fail (package->priv->package_id_split != NULL);

	g_print ("id=%s\n", package->priv->package_id);
	g_print ("summary=%s\n", zif_string_get_value (package->priv->summary));
	g_print ("description=%s\n", zif_string_get_value (package->priv->description));
	g_print ("license=%s\n", zif_string_get_value (package->priv->license));
	g_print ("group=%s\n", zif_string_get_value (package->priv->group));
	g_print ("category=%s\n", zif_string_get_value (package->priv->category));
	if (package->priv->url != NULL)
		g_print ("url=%s\n", zif_string_get_value (package->priv->url));
	g_print ("size=%"G_GUINT64_FORMAT"\n", package->priv->size);

	if (package->priv->files != NULL) {
		g_print ("files:\n");
		array = package->priv->files;
		for (i=0; i<array->len; i++)
			g_print ("\t%s\n", (const gchar *) g_ptr_array_index (array, i));
	}
	if (package->priv->requires != NULL) {
		g_print ("requires:\n");
		array = package->priv->requires;
		for (i=0; i<array->len; i++) {
			depend = g_ptr_array_index (array, i);
			text = zif_depend_to_string (depend);
			g_print ("\t%s\n", text);
			g_free (text);
		}
	}
	if (package->priv->provides != NULL) {
		g_print ("provides:\n");
		array = package->priv->provides;
		for (i=0; i<array->len; i++) {
			depend = g_ptr_array_index (array, i);
			text = zif_depend_to_string (depend);
			g_print ("\t%s\n", text);
			g_free (text);
		}
	}
}

/**
 * zif_package_is_devel:
 * @package: the #ZifPackage object
 *
 * Finds out if a package is a development package.
 *
 * Return value: %TRUE or %FALSE
 *
 * Since: 0.1.0
 **/
gboolean
zif_package_is_devel (ZifPackage *package)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (package->priv->package_id_split != NULL, FALSE);

	if (g_str_has_suffix (package->priv->package_id_split[ZIF_PACKAGE_ID_NAME], "-debuginfo"))
		return TRUE;
	if (g_str_has_suffix (package->priv->package_id_split[ZIF_PACKAGE_ID_NAME], "-devel"))
		return TRUE;
	if (g_str_has_suffix (package->priv->package_id_split[ZIF_PACKAGE_ID_NAME], "-static"))
		return TRUE;
	if (g_str_has_suffix (package->priv->package_id_split[ZIF_PACKAGE_ID_NAME], "-libs"))
		return TRUE;
	return FALSE;
}

/**
 * zif_package_is_gui:
 * @package: the #ZifPackage object
 *
 * Finds out if a package is a GUI package.
 *
 * Return value: %TRUE or %FALSE
 *
 * Since: 0.1.0
 **/
gboolean
zif_package_is_gui (ZifPackage *package)
{
	gboolean ret = FALSE;
	guint i;
	const ZifDepend *depend;
	GPtrArray *array;
	ZifState *state_tmp;

	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (package->priv->package_id_split != NULL, FALSE);

	/* get list of requires */
	state_tmp = zif_state_new ();
	array = zif_package_get_requires (package, state_tmp, NULL);
	if (array == NULL)
		goto out;
	for (i=0; i<array->len; i++) {
		depend = g_ptr_array_index (array, i);
		if (g_strstr_len (depend->name, -1, "gtk") != NULL ||
		    g_strstr_len (depend->name, -1, "kde") != NULL) {
			ret = TRUE;
			break;
		}
	}
	g_ptr_array_unref (array);
out:
	g_object_unref (state_tmp);
	return ret;
}

/**
 * zif_package_is_installed:
 * @package: the #ZifPackage object
 *
 * Finds out if a package is installed.
 *
 * Return value: %TRUE or %FALSE
 *
 * Since: 0.1.0
 **/
gboolean
zif_package_is_installed (ZifPackage *package)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (package->priv->package_id_split != NULL, FALSE);
	return package->priv->installed;
}

/**
 * zif_package_is_native:
 * @package: the #ZifPackage object
 *
 * Finds out if a package is the native architecture for the system.
 *
 * Return value: %TRUE or %FALSE
 *
 * Since: 0.1.0
 **/
gboolean
zif_package_is_native (ZifPackage *package)
{
	gchar **array;
	guint i;
	const gchar *arch;
	gboolean ret = FALSE;
	ZifConfig *config;

	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (package->priv->package_id_split != NULL, FALSE);

	/* is package in arch array */
	arch = package->priv->package_id_split[ZIF_PACKAGE_ID_ARCH];
	config = zif_config_new ();
	array = zif_config_get_basearch_array (config);
	for (i=0; array[i] != NULL; i++) {
		if (g_strcmp0 (array[i], arch) == 0) {
			ret = TRUE;
			break;
		}
	}
	g_object_unref (config);
	return ret;
}

/**
 * zif_package_is_free:
 * @package: the #ZifPackage object
 *
 * Check the string license_text for free licenses, indicated by
 * their short names as documented at
 * http://fedoraproject.org/wiki/Licensing
 *
 * Licenses can be grouped by " or " to indicate that the package
 * can be redistributed under any of the licenses in the group.
 * For instance: GPLv2+ or Artistic or FooLicense.
 *
 * Also, if a license ends with "+", the "+" is removed before
 * comparing it to the list of valid licenses.  So if license
 * "FooLicense" is free, then "FooLicense+" is considered free.
 *
 * Groups of licenses can be grouped with " and " to indicate
 * that parts of the package are distributed under one group of
 * licenses, while other parts of the package are distributed
 * under another group.  Groups may be wrapped in parenthesis.
 * For instance:
 * (GPLv2+ or Artistic) and (GPL+ or Artistic) and FooLicense.
 *
 * At least one license in each group must be free for the
 * package to be considered Free Software.  If the license_text
 * is empty, the package is considered non-free.
 *
 * Return value: %TRUE or %FALSE
 *
 * Since: 0.1.0
 **/
gboolean
zif_package_is_free (ZifPackage *package)
{
	GError *error = NULL;
	gboolean ret;
	gboolean is_free = FALSE;
	ZifLegal *legal;

	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (package->priv->package_id_split != NULL, FALSE);

	/* see if license is free */
	legal = zif_legal_new ();
	ret = zif_legal_is_free (legal,
				 zif_string_get_value (package->priv->license),
				 &is_free, &error);
	if (!ret) {
		g_warning ("failed to get free status: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_object_unref (legal);
	return is_free;
}

/**
 * zif_package_get_id:
 * @package: the #ZifPackage object
 *
 * Gets the id uniquely identifying the package in all repos.
 *
 * Return value: the PackageId representing the package.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_package_get_id (ZifPackage *package)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->package_id != NULL, NULL);
	return package->priv->package_id;
}

/**
 * zif_package_get_name:
 * @package: the #ZifPackage object
 *
 * Gets the package name.
 *
 * Return value: the package name.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_package_get_name (ZifPackage *package)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->package_id_split != NULL, NULL);
	return package->priv->package_id_split[ZIF_PACKAGE_ID_NAME];
}

/**
 * zif_package_get_version:
 * @package: the #ZifPackage object
 *
 * Gets the package version.
 *
 * Return value: the package version, e.g. "0.1.2".
 *
 * Since: 0.1.1
 **/
const gchar *
zif_package_get_version (ZifPackage *package)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->package_id_split != NULL, NULL);
	return package->priv->package_id_split[ZIF_PACKAGE_ID_VERSION];
}

/**
 * zif_package_get_arch:
 * @package: the #ZifPackage object
 *
 * Gets the package architecture, e.g. "i386".
 *
 * Return value: the package architecture.
 *
 * Since: 0.1.1
 **/
const gchar *
zif_package_get_arch (ZifPackage *package)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->package_id_split != NULL, NULL);
	return package->priv->package_id_split[ZIF_PACKAGE_ID_ARCH];
}

/**
 * zif_package_get_data:
 * @package: the #ZifPackage object
 *
 * Gets the package source data, e.g. "fedora".
 *
 * Return value: the package data.
 *
 * Since: 0.1.1
 **/
const gchar *
zif_package_get_data (ZifPackage *package)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->package_id_split != NULL, NULL);
	return package->priv->package_id_split[ZIF_PACKAGE_ID_DATA];
}

/**
 * zif_package_get_package_id:
 * @package: the #ZifPackage object
 *
 * Gets the id (as text) uniquely identifying the package in all repos.
 *
 * Return value: The package-id representing the package.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_package_get_package_id (ZifPackage *package)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->package_id_split != NULL, NULL);
	return package->priv->package_id;
}

/**
 * zif_package_ensure_type_to_string:
 * @type: the #ZifPackageEnsureType enumerated value
 *
 * Gets the string representation of a #ZifPackageEnsureType
 *
 * Return value: The #ZifPackageEnsureType represented as a string
 **/
const gchar *
zif_package_ensure_type_to_string (ZifPackageEnsureType type)
{
	if (type == ZIF_PACKAGE_ENSURE_TYPE_FILES)
		return "files";
	if (type == ZIF_PACKAGE_ENSURE_TYPE_SUMMARY)
		return "summary";
	if (type == ZIF_PACKAGE_ENSURE_TYPE_LICENCE)
		return "licence";
	if (type == ZIF_PACKAGE_ENSURE_TYPE_DESCRIPTION)
		return "description";
	if (type == ZIF_PACKAGE_ENSURE_TYPE_URL)
		return "url";
	if (type == ZIF_PACKAGE_ENSURE_TYPE_SIZE)
		return "size";
	if (type == ZIF_PACKAGE_ENSURE_TYPE_GROUP)
		return "group";
	if (type == ZIF_PACKAGE_ENSURE_TYPE_REQUIRES)
		return "requires";
	if (type == ZIF_PACKAGE_ENSURE_TYPE_PROVIDES)
		return "provides";
	if (type == ZIF_PACKAGE_ENSURE_TYPE_CONFLICTS)
		return "conflicts";
	if (type == ZIF_PACKAGE_ENSURE_TYPE_OBSOLETES)
		return "obsoletes";
	return "unknown";
}

/**
 * zif_package_ensure_data:
 **/
static gboolean
zif_package_ensure_data (ZifPackage *package, ZifPackageEnsureType type,
			 ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	ZifPackageClass *klass = ZIF_PACKAGE_GET_CLASS (package);

	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no support */
	if (klass->ensure_data == NULL) {
		g_set_error (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED,
			     "cannot ensure data for %s data", zif_package_ensure_type_to_string (type));
		goto out;
	}

	ret = klass->ensure_data (package, type, state, error);
out:
	return ret;
}

/**
 * zif_package_get_summary:
 * @package: the #ZifPackage object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the package summary.
 *
 * Return value: the const string or %NULL
 *
 * Since: 0.1.0
 **/
const gchar *
zif_package_get_summary (ZifPackage *package, ZifState *state, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->package_id_split != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* not exists */
	if (package->priv->summary == NULL) {
		ret = zif_package_ensure_data (package, ZIF_PACKAGE_ENSURE_TYPE_SUMMARY, state, error);
		if (!ret)
			return NULL;
	}

	/* return const string */
	return zif_string_get_value (package->priv->summary);
}

/**
 * zif_package_get_description:
 * @package: the #ZifPackage object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the package description.
 *
 * Return value: the const string or %NULL
 *
 * Since: 0.1.0
 **/
const gchar *
zif_package_get_description (ZifPackage *package, ZifState *state, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->package_id_split != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* not exists */
	if (package->priv->description == NULL) {
		ret = zif_package_ensure_data (package, ZIF_PACKAGE_ENSURE_TYPE_DESCRIPTION, state, error);
		if (!ret)
			return NULL;
	}

	/* return const string */
	return zif_string_get_value (package->priv->description);
}

/**
 * zif_package_get_license:
 * @package: the #ZifPackage object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the package licence.
 *
 * Return value: the const string or %NULL
 *
 * Since: 0.1.0
 **/
const gchar *
zif_package_get_license (ZifPackage *package, ZifState *state, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->package_id_split != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* not exists */
	if (package->priv->license == NULL) {
		ret = zif_package_ensure_data (package, ZIF_PACKAGE_ENSURE_TYPE_LICENCE, state, error);
		if (!ret)
			return NULL;
	}

	/* return const string */
	return zif_string_get_value (package->priv->license);
}

/**
 * zif_package_get_url:
 * @package: the #ZifPackage object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the homepage URL for the package.
 *
 * Return value: the const string or %NULL
 *
 * Since: 0.1.0
 **/
const gchar *
zif_package_get_url (ZifPackage *package, ZifState *state, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->package_id_split != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* not exists */
	if (package->priv->url == NULL) {
		ret = zif_package_ensure_data (package, ZIF_PACKAGE_ENSURE_TYPE_URL, state, error);
		if (!ret)
			return NULL;
	}

	/* return const string */
	return zif_string_get_value (package->priv->url);
}

/**
 * zif_package_get_filename:
 * @package: the #ZifPackage object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the remote filename for the package, e.g. Packages/net-snmp-5.4.2-3.fc10.i386.rpm
 *
 * Return value: the const string or %NULL
 *
 * Since: 0.1.0
 **/
const gchar *
zif_package_get_filename (ZifPackage *package, ZifState *state, GError **error)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->package_id_split != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* doesn't make much sense */
	if (package->priv->installed) {
		g_set_error_literal (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED,
				     "cannot get remote filename for installed package");
		return NULL;
	}

	/* not exists */
	if (package->priv->location_href == NULL) {
		g_set_error (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED,
			     "no data for %s", package->priv->package_id_split[ZIF_PACKAGE_ID_NAME]);
		return NULL;
	}

	/* return const string */
	return zif_string_get_value (package->priv->location_href);
}

/**
 * zif_package_get_category:
 * @package: the #ZifPackage object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the category the packag is in.
 *
 * Return value: the const string or %NULL
 *
 * Since: 0.1.0
 **/
const gchar *
zif_package_get_category (ZifPackage *package, ZifState *state, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->package_id_split != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* not exists */
	if (package->priv->category == NULL) {
		ret = zif_package_ensure_data (package, ZIF_PACKAGE_ENSURE_TYPE_CATEGORY, state, error);
		if (!ret)
			return NULL;
	}

	/* return const string */
	return zif_string_get_value (package->priv->category);
}

/**
 * zif_package_get_group:
 * @package: the #ZifPackage object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the package group.
 *
 * Return value: the group name string
 *
 * Since: 0.1.0
 **/
const gchar *
zif_package_get_group (ZifPackage *package, ZifState *state, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->package_id_split != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* not exists */
	if (package->priv->group == NULL) {
		ret = zif_package_ensure_data (package, ZIF_PACKAGE_ENSURE_TYPE_GROUP, state, error);
		if (!ret)
			return NULL;
	}

	return zif_string_get_value (package->priv->group);
}

/**
 * zif_package_get_size:
 * @package: the #ZifPackage object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the size of the package.
 * This is the installed size for installed packages, and the download size for
 * remote packages.
 *
 * Return value: the package size, or 0 for failure
 *
 * Since: 0.1.0
 **/
guint64
zif_package_get_size (ZifPackage *package, ZifState *state, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_PACKAGE (package), 0);
	g_return_val_if_fail (package->priv->package_id_split != NULL, 0);
	g_return_val_if_fail (zif_state_valid (state), 0);
	g_return_val_if_fail (error == NULL || *error == NULL, 0);

	if (package->priv->size == 0) {
		ret = zif_package_ensure_data (package, ZIF_PACKAGE_ENSURE_TYPE_SIZE, state, error);
		if (!ret)
			return 0;
	}

	return package->priv->size;
}

/**
 * zif_package_get_files:
 * @package: the #ZifPackage object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the file list for the package.
 *
 * Return value: the reference counted #GPtrArray, use g_ptr_array_unref() when done
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_package_get_files (ZifPackage *package, ZifState *state, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* not exists */
	if (package->priv->files == NULL) {
		ret = zif_package_ensure_data (package, ZIF_PACKAGE_ENSURE_TYPE_FILES, state, error);
		if (!ret)
			return NULL;
	}

	/* return refcounted */
	return g_ptr_array_ref (package->priv->files);
}

/**
 * zif_package_get_requires:
 * @package: the #ZifPackage object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets all the package requires.
 *
 * Return value: the reference counted #GPtrArray, use g_ptr_array_unref() when done
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_package_get_requires (ZifPackage *package, ZifState *state, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->package_id_split != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* not exists */
	if (package->priv->requires == NULL) {
		ret = zif_package_ensure_data (package, ZIF_PACKAGE_ENSURE_TYPE_REQUIRES, state, error);
		if (!ret)
			return NULL;
	}

	/* return refcounted */
	return g_ptr_array_ref (package->priv->requires);
}

/**
 * zif_package_get_provides:
 * @package: the #ZifPackage object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Get all the package provides.
 *
 * Return value: the reference counted #GPtrArray, use g_ptr_array_unref() when done
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_package_get_provides (ZifPackage *package, ZifState *state, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (package->priv->package_id_split != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* not exists */
	if (package->priv->provides == NULL) {
		ret = zif_package_ensure_data (package, ZIF_PACKAGE_ENSURE_TYPE_PROVIDES, state, error);
		if (!ret)
			return NULL;
	}

	/* return refcounted */
	return g_ptr_array_ref (package->priv->provides);
}

/**
 * zif_package_set_time_file:
 * @package: the #ZifPackage object
 * @time_file: the unix time the file was created
 *
 * Sets the UNIX time the file was created.
 *
 * Since: 0.1.2
 **/
void
zif_package_set_time_file (ZifPackage *package, guint64 time_file)
{
	g_return_if_fail (ZIF_IS_PACKAGE (package));
	package->priv->time_file = time_file;
}

/**
 * zif_package_get_time_file:
 * @package: the #ZifPackage object
 *
 * Get the time the file was created.
 *
 * Return value: the UNIX time, or 0 for unknown.
 *
 * Since: 0.1.2
 **/
guint64
zif_package_get_time_file (ZifPackage *package)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), 0);
	return package->priv->time_file;
}

/**
 * zif_package_set_installed:
 * @package: the #ZifPackage object
 * @installed: If the package is installed
 *
 * Sets the package installed status.
 *
 * Since: 0.1.0
 **/
void
zif_package_set_installed (ZifPackage *package, gboolean installed)
{
	g_return_if_fail (ZIF_IS_PACKAGE (package));
	package->priv->installed = installed;
}

/**
 * zif_package_set_id:
 * @package: the #ZifPackage object
 * @package_id: A PackageId defining the object
 *
 * Sets the unique id for the package.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_package_set_id (ZifPackage *package, const gchar *package_id, GError **error)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (package->priv->package_id == NULL, FALSE);

	/* not a valid package id */
	if (!zif_package_id_check (package_id)) {
		g_set_error (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED,
			     "not a valid package-id: %s", package_id);
		return FALSE;
	}
	package->priv->package_id = g_strdup (package_id);
	package->priv->package_id_split = zif_package_id_split (package_id);
	return TRUE;
}

/**
 * zif_package_set_summary:
 * @package: the #ZifPackage object
 * @summary: the package summary
 *
 * Sets the package summary.
 *
 * Since: 0.1.0
 **/
void
zif_package_set_summary (ZifPackage *package, ZifString *summary)
{
	g_return_if_fail (ZIF_IS_PACKAGE (package));
	g_return_if_fail (summary != NULL);
	g_return_if_fail (package->priv->summary == NULL);

	package->priv->summary = zif_string_ref (summary);
}

/**
 * zif_package_set_description:
 * @package: the #ZifPackage object
 * @description: the package description
 *
 * Sets the package description.
 *
 * Since: 0.1.0
 **/
void
zif_package_set_description (ZifPackage *package, ZifString *description)
{
	g_return_if_fail (ZIF_IS_PACKAGE (package));
	g_return_if_fail (description != NULL);
	g_return_if_fail (package->priv->description == NULL);

	package->priv->description = zif_string_ref (description);
}

/**
 * zif_package_set_license:
 * @package: the #ZifPackage object
 * @license: license
 *
 * Sets the package license.
 *
 * Since: 0.1.0
 **/
void
zif_package_set_license (ZifPackage *package, ZifString *license)
{
	g_return_if_fail (ZIF_IS_PACKAGE (package));
	g_return_if_fail (license != NULL);
	g_return_if_fail (package->priv->license == NULL);

	package->priv->license = zif_string_ref (license);
}

/**
 * zif_package_set_url:
 * @package: the #ZifPackage object
 * @url: The package homepage URL
 *
 * Sets the project homepage URL.
 *
 * Since: 0.1.0
 **/
void
zif_package_set_url (ZifPackage *package, ZifString *url)
{
	g_return_if_fail (ZIF_IS_PACKAGE (package));
	g_return_if_fail (url != NULL);
	g_return_if_fail (package->priv->url == NULL);

	package->priv->url = zif_string_ref (url);
}

/**
 * zif_package_set_location_href:
 * @package: the #ZifPackage object
 * @location_href: the remote download filename
 *
 * Sets the remote download location.
 *
 * Since: 0.1.0
 **/
void
zif_package_set_location_href (ZifPackage *package, ZifString *location_href)
{
	g_return_if_fail (ZIF_IS_PACKAGE (package));
	g_return_if_fail (location_href != NULL);
	g_return_if_fail (package->priv->location_href == NULL);

	package->priv->location_href = zif_string_ref (location_href);
}

/**
 * zif_package_set_category:
 * @package: the #ZifPackage object
 * @category: category
 *
 * Sets the package category.
 *
 * Since: 0.1.0
 **/
void
zif_package_set_category (ZifPackage *package, ZifString *category)
{
	g_return_if_fail (ZIF_IS_PACKAGE (package));
	g_return_if_fail (category != NULL);
	g_return_if_fail (package->priv->category == NULL);

	package->priv->category = zif_string_ref (category);
}

/**
 * zif_package_set_group:
 * @package: the #ZifPackage object
 * @group: the package group
 *
 * Sets the package group.
 *
 * Since: 0.1.0
 **/
void
zif_package_set_group (ZifPackage *package, ZifString *group)
{
	g_return_if_fail (ZIF_IS_PACKAGE (package));
	g_return_if_fail (group != NULL);
	g_return_if_fail (package->priv->group == NULL);

	package->priv->group = zif_string_ref (group);
}

/**
 * zif_package_set_size:
 * @package: the #ZifPackage object
 * @size: the package size in bytes
 *
 * Sets the package size in bytes.
 *
 * Since: 0.1.0
 **/
void
zif_package_set_size (ZifPackage *package, guint64 size)
{
	g_return_if_fail (ZIF_IS_PACKAGE (package));
	g_return_if_fail (size != 0);
	g_return_if_fail (package->priv->size == 0);

	package->priv->size = size;
}

/**
 * zif_package_set_files:
 * @package: the #ZifPackage object
 * @files: the package filelist
 *
 * Sets the package file list.
 *
 * Since: 0.1.0
 **/
void
zif_package_set_files (ZifPackage *package, GPtrArray *files)
{
	g_return_if_fail (ZIF_IS_PACKAGE (package));
	g_return_if_fail (files != NULL);
	g_return_if_fail (package->priv->files == NULL);

	package->priv->files = g_ptr_array_ref (files);
}

/**
 * zif_package_set_requires:
 * @package: the #ZifPackage object
 * @requires: the package requires
 *
 * Sets the package requires.
 *
 * Since: 0.1.0
 **/
void
zif_package_set_requires (ZifPackage *package, GPtrArray *requires)
{
	g_return_if_fail (ZIF_IS_PACKAGE (package));
	g_return_if_fail (requires != NULL);
	g_return_if_fail (package->priv->requires == NULL);

	package->priv->requires = g_ptr_array_ref (requires);
}

/**
 * zif_package_set_provides:
 * @package: the #ZifPackage object
 * @provides: the package provides
 *
 * Sets the package provides
 *
 * Since: 0.1.0
 **/
void
zif_package_set_provides (ZifPackage *package, GPtrArray *provides)
{
	g_return_if_fail (ZIF_IS_PACKAGE (package));
	g_return_if_fail (provides != NULL);
	g_return_if_fail (package->priv->provides == NULL);

	package->priv->provides = g_ptr_array_ref (provides);
}

/**
 * zif_package_finalize:
 **/
static void
zif_package_finalize (GObject *object)
{
	ZifPackage *package;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_PACKAGE (object));
	package = ZIF_PACKAGE (object);

	g_free (package->priv->package_id);
	g_strfreev (package->priv->package_id_split);
	if (package->priv->summary != NULL)
		zif_string_unref (package->priv->summary);
	if (package->priv->description != NULL)
		zif_string_unref (package->priv->description);
	if (package->priv->license != NULL)
		zif_string_unref (package->priv->license);
	if (package->priv->url != NULL)
		zif_string_unref (package->priv->url);
	if (package->priv->category != NULL)
		zif_string_unref (package->priv->category);
	if (package->priv->location_href != NULL)
		zif_string_unref (package->priv->location_href);
	if (package->priv->files != NULL)
		g_ptr_array_unref (package->priv->files);
	if (package->priv->requires != NULL)
		g_ptr_array_unref (package->priv->requires);
	if (package->priv->provides != NULL)
		g_ptr_array_unref (package->priv->provides);

	G_OBJECT_CLASS (zif_package_parent_class)->finalize (object);
}

/**
 * zif_package_class_init:
 **/
static void
zif_package_class_init (ZifPackageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_package_finalize;
	g_type_class_add_private (klass, sizeof (ZifPackagePrivate));
}

/**
 * zif_package_init:
 **/
static void
zif_package_init (ZifPackage *package)
{
	package->priv = ZIF_PACKAGE_GET_PRIVATE (package);
	package->priv->package_id_split = NULL;
	package->priv->package_id = NULL;
	package->priv->summary = NULL;
	package->priv->description = NULL;
	package->priv->license = NULL;
	package->priv->url = NULL;
	package->priv->category = NULL;
	package->priv->files = NULL;
	package->priv->requires = NULL;
	package->priv->provides = NULL;
	package->priv->location_href = NULL;
	package->priv->installed = FALSE;
	package->priv->group = NULL;
	package->priv->size = 0;
}

/**
 * zif_package_new:
 *
 * Return value: A new #ZifPackage class instance.
 *
 * Since: 0.1.0
 **/
ZifPackage *
zif_package_new (void)
{
	ZifPackage *package;
	package = g_object_new (ZIF_TYPE_PACKAGE, NULL);
	return ZIF_PACKAGE (package);
}

