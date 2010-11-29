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
 * SECTION:zif-package-remote
 * @short_description: Remote package object
 *
 * This object is a subclass of #ZifPackage
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>

#include "zif-utils.h"
#include "zif-package-remote.h"
#include "zif-groups.h"
#include "zif-string.h"
#include "zif-store-remote.h"

#define ZIF_PACKAGE_REMOTE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_PACKAGE_REMOTE, ZifPackageRemotePrivate))

/**
 * ZifPackageRemotePrivate:
 *
 * Private #ZifPackageRemote data
 **/
struct _ZifPackageRemotePrivate
{
	ZifGroups		*groups;
	ZifStoreRemote		*store_remote;
	gchar			*pkgid;
};

G_DEFINE_TYPE (ZifPackageRemote, zif_package_remote, ZIF_TYPE_PACKAGE)

/**
 * zif_package_remote_set_from_repo:
 * @pkg: the #ZifPackageRemote object
 * @length: length of data and type arrays
 * @type: data type array
 * @data: data value array
 * @repo_id: the repository id
 * @error: a #GError which is used on failure, or %NULL
 *
 * Sets details on a remote package from repo data derived from the metadata xml.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_package_remote_set_from_repo (ZifPackageRemote *pkg, guint length, gchar **type, gchar **data, const gchar *repo_id, GError **error)
{
	guint i;
	gboolean ret;
	const gchar *name = NULL;
	guint epoch = 0;
	const gchar *version = NULL;
	const gchar *release = NULL;
	const gchar *arch = NULL;
	gchar *package_id = NULL;
	ZifString *string;
	gchar *endptr = NULL;
	guint64 time_file = 0;

	g_return_val_if_fail (ZIF_IS_PACKAGE_REMOTE (pkg), FALSE);
	g_return_val_if_fail (type != NULL, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get the ID */
	for (i=0; i<length; i++) {
		if (g_strcmp0 (type[i], "name") == 0) {
			name = data[i];
		} else if (g_strcmp0 (type[i], "epoch") == 0) {
			epoch = g_ascii_strtoull (data[i], &endptr, 10);
			if (data[i] == endptr)
				g_warning ("failed to parse epoch %s", data[i]);
		} else if (g_strcmp0 (type[i], "version") == 0) {
			version = data[i];
		} else if (g_strcmp0 (type[i], "release") == 0) {
			release = data[i];
		} else if (g_strcmp0 (type[i], "arch") == 0) {
			arch = data[i];
		} else if (g_strcmp0 (type[i], "summary") == 0) {
			string = zif_string_new (data[i]);
			zif_package_set_summary (ZIF_PACKAGE (pkg), string);
			zif_string_unref (string);
		} else if (g_strcmp0 (type[i], "description") == 0) {
			string = zif_string_new (data[i]);
			zif_package_set_description (ZIF_PACKAGE (pkg), string);
			zif_string_unref (string);
		} else if (g_strcmp0 (type[i], "url") == 0) {
			string = zif_string_new (data[i]);
			zif_package_set_url (ZIF_PACKAGE (pkg), string);
			zif_string_unref (string);
		} else if (g_strcmp0 (type[i], "rpm_license") == 0) {
			string = zif_string_new (data[i]);
			zif_package_set_license (ZIF_PACKAGE (pkg), string);
			zif_string_unref (string);
		} else if (g_strcmp0 (type[i], "rpm_group") == 0) {
			string = zif_string_new (data[i]);
			zif_package_set_category (ZIF_PACKAGE (pkg), string);
			zif_string_unref (string);
		} else if (g_strcmp0 (type[i], "size_package") == 0) {
			zif_package_set_size (ZIF_PACKAGE (pkg), atoi (data[i]));
		} else if (g_strcmp0 (type[i], "pkgId") == 0) {
			pkg->priv->pkgid = g_strdup (data[i]);
		} else if (g_strcmp0 (type[i], "location_href") == 0) {
			string = zif_string_new (data[i]);
			zif_package_set_location_href (ZIF_PACKAGE (pkg), string);
			zif_string_unref (string);
		} else if (g_strcmp0 (type[i], "time_file") == 0) {
			time_file = g_ascii_strtoull (data[i], &endptr, 10);
			if (data[i] == endptr)
				g_warning ("failed to parse time_file %s", data[i]);
			zif_package_set_time_file (ZIF_PACKAGE (pkg), time_file);
		} else {
			g_warning ("unrecognized: %s=%s", type[i], data[i]);
		}
	}

	zif_package_set_installed (ZIF_PACKAGE (pkg), FALSE);
	package_id = zif_package_id_from_nevra (name, epoch, version, release, arch, repo_id);
	ret = zif_package_set_id (ZIF_PACKAGE (pkg), package_id, error);
	if (!ret)
		goto out;
out:
	g_free (package_id);
	return ret;
}

/**
 * zif_package_remote_get_pkgid:
 * @pkg: the #ZifPackageRemote object
 *
 * Gets the pkgid used internally to track the package item.
 *
 * Return value: the pkgid hash.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_package_remote_get_pkgid (ZifPackageRemote *pkg)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE_REMOTE (pkg), NULL);
	return pkg->priv->pkgid;
}

/**
 * zif_package_remote_set_pkgid:
 * @pkg: the #ZifPackageRemote object
 * @pkgid: the pkgid hash.
 *
 * Sets the pkgid used internally to track the package item.
 *
 * Since: 0.1.0
 **/
void
zif_package_remote_set_pkgid (ZifPackageRemote *pkg, const gchar *pkgid)
{
	g_return_if_fail (ZIF_IS_PACKAGE_REMOTE (pkg));
	g_return_if_fail (pkgid != NULL);
	g_return_if_fail (pkg->priv->pkgid == NULL);
	pkg->priv->pkgid = g_strdup (pkgid);
}

/**
 * zif_package_remote_ensure_cache_filename:
 **/
static gboolean
zif_package_remote_ensure_cache_filename (ZifPackageRemote *pkg, ZifState *state, GError **error)
{
	const gchar *filename;
	const gchar *directory;
	gchar *basename = NULL;
	gchar *cache_filename = NULL;
	gboolean ret = FALSE;

	/* get filename */
	filename = zif_package_get_filename (ZIF_PACKAGE (pkg), state, error);
	if (filename == NULL)
		goto out;

	/* get the path */
	basename = g_path_get_basename (filename);
	directory = zif_store_remote_get_local_directory (pkg->priv->store_remote);
	if (directory == NULL) {
		g_set_error (error,
			     ZIF_PACKAGE_ERROR,
			     ZIF_PACKAGE_ERROR_FAILED,
			     "failed to get local directory for %s",
			     zif_package_get_printable (ZIF_PACKAGE (pkg)));
		goto out;
	}

	/* save in the package */
	ret = TRUE;
	cache_filename = g_build_filename (directory, "packages", basename, NULL);
	zif_package_set_cache_filename (ZIF_PACKAGE (pkg), cache_filename);
out:
	g_free (basename);
	g_free (cache_filename);
	return ret;
}

/**
 * zif_package_remote_download:
 * @pkg: the #ZifPackageRemote object
 * @directory: the local directory to save to, or %NULL to use the package cache
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Downloads a package.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.3
 **/
gboolean
zif_package_remote_download (ZifPackageRemote *pkg, const gchar *directory, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	ZifStoreRemote *store_remote = NULL;
	GError *error_local = NULL;
	ZifState *state_local = NULL;
	const gchar *filename;
	gchar *directory_new = NULL;

	g_return_val_if_fail (ZIF_IS_PACKAGE_REMOTE (pkg), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* setup steps */
	ret = zif_state_set_steps (state,
				   error,
				   5,
				   95,
				   -1);
	if (!ret)
		goto out;

	/* directory is optional */
	if (directory == NULL) {
		directory = zif_store_remote_get_local_directory (pkg->priv->store_remote);
		if (directory == NULL) {
			g_set_error (error,
				     ZIF_PACKAGE_ERROR,
				     ZIF_PACKAGE_ERROR_FAILED,
				     "failed to get local directory for %s",
				     zif_package_get_printable (ZIF_PACKAGE (pkg)));
			goto out;
		}
		directory_new = g_build_filename (directory, "packages", NULL);
	} else {
		directory_new = g_strdup (directory);
	}

	/* get filename */
	state_local = zif_state_get_child (state);
	filename = zif_package_get_filename (ZIF_PACKAGE (pkg), state_local, error);
	if (filename == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* create a chain of states */
	state_local = zif_state_get_child (state);

	/* download from the store */
	ret = zif_store_remote_download (pkg->priv->store_remote,
					 filename,
					 directory_new,
					 state_local,
					 &error_local);
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
	g_free (directory_new);
	if (store_remote != NULL)
		g_object_unref (store_remote);
	return ret;
}

/**
 * zif_package_remote_set_store_remote:
 * @pkg: the #ZifPackageRemote object
 * @store: the #ZifStoreRemote that created this package
 *
 * Sets the store used to create this package, which we may need of we ever
 * need to ensure() data at runtime.
 *
 * Since: 0.1.0
 **/
void
zif_package_remote_set_store_remote (ZifPackageRemote *pkg, ZifStoreRemote *store)
{
	g_return_if_fail (ZIF_IS_PACKAGE_REMOTE (pkg));
	g_return_if_fail (ZIF_IS_STORE_REMOTE (store));
	g_return_if_fail (pkg->priv->store_remote == NULL);
	pkg->priv->store_remote = g_object_ref (store);
}


/**
 * zif_package_remote_get_update_detail:
 * @package: the #ZifPackageRemote object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the update detail for a package.
 *
 * Return value: a %ZifUpdate, or %NULL for failure
 *
 * Since: 0.1.3
 **/
ZifUpdate *
zif_package_remote_get_update_detail (ZifPackageRemote *package, ZifState *state, GError **error)
{
	ZifUpdate *update = NULL;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_PACKAGE_REMOTE (package), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (package->priv->store_remote != NULL, NULL);

	/* download from the store */
	update = zif_store_remote_get_update_detail (package->priv->store_remote,
						     zif_package_get_id (ZIF_PACKAGE (package)),
						     state, &error_local);
	if (update == NULL) {
		g_set_error (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED,
			     "cannot get update detail from store: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	return update;
}

/*
 * zif_package_remote_ensure_data:
 */
static gboolean
zif_package_remote_ensure_data (ZifPackage *pkg, ZifPackageEnsureType type, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	ZifString *tmp;
	const gchar *text;
	const gchar *group;
	ZifPackageRemote *pkg_remote = ZIF_PACKAGE_REMOTE (pkg);

	g_return_val_if_fail (zif_state_valid (state), FALSE);

	if (type == ZIF_PACKAGE_ENSURE_TYPE_FILES) {

		/* get the file list for this package */
		array = zif_store_remote_get_files (pkg_remote->priv->store_remote, pkg, state, error);
		if (array == NULL)
			goto out;

		/* set for this package */
		zif_package_set_files (pkg, array);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_REQUIRES) {

		/* get the file list for this package */
		array = zif_store_remote_get_requires (pkg_remote->priv->store_remote, pkg, state, error);
		if (array == NULL)
			goto out;

		/* set for this package */
		zif_package_set_requires (pkg, array);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_PROVIDES) {

		/* get the file list for this package */
		array = zif_store_remote_get_provides (pkg_remote->priv->store_remote, pkg, state, error);
		if (array == NULL)
			goto out;

		/* set for this package */
		zif_package_set_provides (pkg, array);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_OBSOLETES) {

		/* get the file list for this package */
		array = zif_store_remote_get_obsoletes (pkg_remote->priv->store_remote, pkg, state, error);
		if (array == NULL)
			goto out;

		/* set for this package */
		zif_package_set_obsoletes (pkg, array);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_CONFLICTS) {

		/* get the file list for this package */
		array = zif_store_remote_get_conflicts (pkg_remote->priv->store_remote, pkg, state, error);
		if (array == NULL)
			goto out;

		/* set for this package */
		zif_package_set_conflicts (pkg, array);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_CACHE_FILENAME) {

		/* get the file list for this package */
		ret = zif_package_remote_ensure_cache_filename (pkg_remote, state, error);
		if (!ret)
			goto out;

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_GROUP) {
		/* group */
		text = zif_package_get_category (pkg, state, error);
		if (text == NULL)
			goto out;
		group = zif_groups_get_group_for_cat (pkg_remote->priv->groups, text, error);
		if (group == NULL)
			goto out;

		tmp = zif_string_new (group);
		zif_package_set_group (pkg, tmp);
		zif_string_unref (tmp);
	} else {
		g_set_error (error, 1, 0,
			     "Getting ensure type '%s' not supported on a ZifPackageRemote",
			     zif_package_ensure_type_to_string (type));
		goto out;
	}

	/* success */
	ret = TRUE;
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_package_remote_finalize:
 **/
static void
zif_package_remote_finalize (GObject *object)
{
	ZifPackageRemote *pkg;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_PACKAGE_REMOTE (object));
	pkg = ZIF_PACKAGE_REMOTE (object);

	g_free (pkg->priv->pkgid);
	g_object_unref (pkg->priv->groups);
	if (pkg->priv->store_remote != NULL)
		g_object_unref (pkg->priv->store_remote);

	G_OBJECT_CLASS (zif_package_remote_parent_class)->finalize (object);
}

/**
 * zif_package_remote_class_init:
 **/
static void
zif_package_remote_class_init (ZifPackageRemoteClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifPackageClass *package_class = ZIF_PACKAGE_CLASS (klass);
	object_class->finalize = zif_package_remote_finalize;
	package_class->ensure_data = zif_package_remote_ensure_data;
	g_type_class_add_private (klass, sizeof (ZifPackageRemotePrivate));
}

/**
 * zif_package_remote_init:
 **/
static void
zif_package_remote_init (ZifPackageRemote *pkg)
{
	pkg->priv = ZIF_PACKAGE_REMOTE_GET_PRIVATE (pkg);
	pkg->priv->pkgid = NULL;
	pkg->priv->store_remote = NULL;
	pkg->priv->groups = zif_groups_new ();
}

/**
 * zif_package_remote_new:
 *
 * Return value: A new #ZifPackageRemote class instance.
 *
 * Since: 0.1.0
 **/
ZifPackage *
zif_package_remote_new (void)
{
	ZifPackage *pkg;
	pkg = g_object_new (ZIF_TYPE_PACKAGE_REMOTE, NULL);
	return ZIF_PACKAGE (pkg);
}

