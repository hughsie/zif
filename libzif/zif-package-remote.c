/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2010 Richard Hughes <richard@hughsie.com>
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
 * @short_description: Remote package
 *
 * This object is a subclass of #ZifPackage
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>

#include "zif-groups.h"
#include "zif-package-local.h"
#include "zif-package-private.h"
#include "zif-package-remote.h"
#include "zif-store-remote-private.h"
#include "zif-string.h"
#include "zif-utils.h"

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
	ZifPackage		*installed;
};

G_DEFINE_TYPE (ZifPackageRemote, zif_package_remote, ZIF_TYPE_PACKAGE)

/**
 * zif_package_remote_set_from_repo:
 * @pkg: A #ZifPackageRemote
 * @length: length of data and type arrays
 * @type: The data type array
 * @data: The data value array
 * @repo_id: The repository id
 * @error: A #GError, or %NULL
 *
 * Sets details on a remote package from repo data derived from the metadata xml.
 *
 * Return value: %TRUE for success, %FALSE otherwise
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
			string = zif_string_new (data[i]);
			zif_package_set_pkgid (ZIF_PACKAGE (pkg), string);
			zif_string_unref (string);
		} else if (g_strcmp0 (type[i], "location_href") == 0) {
			string = zif_string_new (data[i]);
			zif_package_set_location_href (ZIF_PACKAGE (pkg), string);
			zif_string_unref (string);
		} else if (g_strcmp0 (type[i], "rpm_sourcerpm") == 0) {
			string = zif_string_new (data[i]);
			zif_package_set_source_filename (ZIF_PACKAGE (pkg), string);
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
 * zif_package_remote_rebuild_delta:
 * @pkg: A #ZifPackageRemote
 * @delta: A #ZifDelta
 * @directory: A local directory to save to, or %NULL to use the package cache
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Rebuilds an rpm from delta.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.2.5
 **/
gboolean
zif_package_remote_rebuild_delta (ZifPackageRemote *pkg,
				  ZifDelta *delta,
				  const gchar *directory,
				  ZifState *state,
				  GError **error)
{
	gboolean ret;
	gchar *directory_new = NULL;
	const gchar *filename = NULL;
	ZifState *state_local = NULL;

	g_return_val_if_fail (ZIF_IS_PACKAGE_REMOTE (pkg), FALSE);
	g_return_val_if_fail (ZIF_IS_DELTA (delta), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* setup steps */
	ret = zif_state_set_steps (state,
				   error,
				   5, /* get filename */
				   95, /* download */
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

	/* get rpm local filename */
	state_local = zif_state_get_child (state);
	filename = zif_package_get_filename (ZIF_PACKAGE (pkg), state_local, error);
	if (filename == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* rebuild rpm from delta */
	ret = zif_delta_rebuild (delta, directory_new, filename, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

out:
	g_free (directory_new);
	return ret;
}

/**
 * zif_package_remote_download_delta:
 * @pkg: A #ZifPackageRemote
 * @directory: A local directory to save to, or %NULL to use the package cache
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Downloads a delta rpm if it exists.
 *
 * Return value: (transfer full): A %ZifDelta, or %NULL for failure
 *
 * Since: 0.2.5
 **/
ZifDelta *
zif_package_remote_download_delta (ZifPackageRemote *pkg,
				   const gchar *directory,
				   ZifState *state,
				   GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	ZifState *state_local = NULL;
	gchar *directory_new = NULL;
	ZifDelta *delta = NULL;

	g_return_val_if_fail (ZIF_IS_PACKAGE_REMOTE (pkg), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* setup steps */
	ret = zif_state_set_steps (state,
				   error,
				   10, /* parse metadata */
				   90, /* download */
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

	/* parse delta metadata */
	state_local = zif_state_get_child (state);
	delta = zif_package_remote_get_delta (pkg, state_local, error);
	if (delta == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* create a chain of states */
	state_local = zif_state_get_child (state);

	/* download from the store */
	ret = zif_store_remote_download_full (pkg->priv->store_remote,
					      zif_delta_get_filename (delta),
					      directory_new,
					      zif_delta_get_size (delta),
					      "application/x-rpm",
					      G_CHECKSUM_MD5,
					      NULL,
					      state_local,
					      &error_local);
	if (!ret) {
		g_set_error (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED,
			     "cannot download delta from store: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

out:
	g_free (directory_new);
	return delta;
}

/**
 * zif_package_remote_download:
 * @pkg: A #ZifPackageRemote
 * @directory: A local directory to save to, or %NULL to use the package cache
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Downloads a package.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.3
 **/
gboolean
zif_package_remote_download (ZifPackageRemote *pkg,
			     const gchar *directory,
			     ZifState *state,
			     GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	ZifState *state_local = NULL;
	const gchar *filename;
	gchar *directory_new = NULL;
	guint64 size;

	g_return_val_if_fail (ZIF_IS_PACKAGE_REMOTE (pkg), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* setup steps */
	ret = zif_state_set_steps (state,
				   error,
				   5, /* get filename */
				   5, /* get size */
				   90,
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

	/* get size */
	state_local = zif_state_get_child (state);
	size = zif_package_get_size (ZIF_PACKAGE (pkg), state_local, error);
	if (size == 0)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* create a chain of states */
	state_local = zif_state_get_child (state);

	/* download from the store */
	ret = zif_store_remote_download_full (pkg->priv->store_remote,
					      filename,
					      directory_new,
					      size,
					      "application/x-rpm",
					      G_CHECKSUM_MD5,
					      NULL,
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
	return ret;
}

/**
 * zif_package_remote_set_store_remote:
 * @pkg: A #ZifPackageRemote
 * @store: A #ZifStoreRemote that created this package
 *
 * Sets the store used to create this package, which we may need of we ever
 * need to ensure() data at runtime.
 *
 * This also sets the package to have a trust of
 * %ZIF_PACKAGE_TRUST_KIND_PUBKEY_UNVERIFIED if the repo claims to
 * support GPG signing or %ZIF_PACKAGE_TRUST_KIND_NONE otherwise.
 *
 * Since: 0.1.0
 **/
void
zif_package_remote_set_store_remote (ZifPackageRemote *pkg,
				     ZifStoreRemote *store)
{
	const gchar *pubkey;

	g_return_if_fail (ZIF_IS_PACKAGE_REMOTE (pkg));
	g_return_if_fail (ZIF_IS_STORE_REMOTE (store));
	g_return_if_fail (pkg->priv->store_remote == NULL);

	/* is the remote store protected with public keys */
	pubkey = zif_store_remote_get_pubkey (store);
	if (pubkey == NULL) {
		zif_package_set_trust_kind (ZIF_PACKAGE (pkg),
					    ZIF_PACKAGE_TRUST_KIND_NONE);
	} else {
		zif_package_set_trust_kind (ZIF_PACKAGE (pkg),
					    ZIF_PACKAGE_TRUST_KIND_PUBKEY_UNVERIFIED);
	}

	pkg->priv->store_remote = g_object_ref (store);
}

/**
 * zif_package_remote_get_store_remote:
 * @pkg: A #ZifPackageRemote
 *
 * Gets the store used to create this package.
 *
 * Return value: (transfer full): A refcounted %ZifStoreRemote, or %NULL for failure.
 * Use g_object_unref() when done.
 *
 * Since: 0.1.3
 **/
ZifStoreRemote *
zif_package_remote_get_store_remote (ZifPackageRemote *pkg)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE_REMOTE (pkg), NULL);
	if (pkg->priv->store_remote == NULL)
		return NULL;
	return g_object_ref (pkg->priv->store_remote);
}

/**
 * zif_package_remote_set_installed:
 * @pkg: A #ZifPackageRemote
 * @installed: A #ZifPackage that created this package
 *
 * Sets the installed package this package updates.
 *
 * Since: 0.1.3
 **/
void
zif_package_remote_set_installed (ZifPackageRemote *pkg, ZifPackage *installed)
{
	g_return_if_fail (ZIF_IS_PACKAGE_REMOTE (pkg));
	g_return_if_fail (ZIF_IS_PACKAGE_LOCAL (installed));

	if (pkg->priv->installed != NULL)
		g_object_unref (pkg->priv->installed);
	pkg->priv->installed = g_object_ref (installed);
}

/**
 * zif_package_remote_get_installed:
 * @pkg: A #ZifPackageRemote
 *
 * Gets the installed package this package updates.
 *
 * Return value: (transfer full): A refcounted %ZifPackage, or %NULL for failure.
 * Use g_object_unref() when done.
 *
 * Since: 0.1.3
 **/
ZifPackage *
zif_package_remote_get_installed (ZifPackageRemote *pkg)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE_REMOTE (pkg), NULL);
	if (pkg->priv->installed == NULL)
		return NULL;
	return g_object_ref (pkg->priv->installed);
}

/**
 * zif_package_remote_get_delta:
 * @package: A #ZifPackageRemote
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Gets the update detail for a package if it exists.
 *
 * Return value: (transfer full): A %ZifUpdate, or %NULL for failure
 *
 * Since: 0.1.3
 **/
ZifDelta *
zif_package_remote_get_delta (ZifPackageRemote *package, ZifState *state, GError **error)
{
	ZifDelta *delta = NULL;
	GError *error_local = NULL;

	/* not assigned */
	if (package->priv->store_remote == NULL) {
		g_set_error (error,
			     ZIF_PACKAGE_ERROR,
			     ZIF_PACKAGE_ERROR_FAILED,
			     "remote source not set %s",
			     zif_package_get_printable (ZIF_PACKAGE (package)));
		goto out;
	}

	/* not resolved */
	if (package->priv->installed == NULL) {
		g_set_error (error,
			     ZIF_PACKAGE_ERROR,
			     ZIF_PACKAGE_ERROR_FAILED,
			     "no installed package %s, try using pk_transaction_resolve()",
			     zif_package_get_printable (ZIF_PACKAGE (package)));
		goto out;
	}

	/* find */
	delta = zif_store_remote_find_delta (package->priv->store_remote,
					     ZIF_PACKAGE (package),
					     package->priv->installed,
					     state,
					     &error_local);
	if (delta == NULL) {
		g_set_error (error,
			     ZIF_PACKAGE_ERROR,
			     ZIF_PACKAGE_ERROR_FAILED,
			     "no delta for %s -> %s : %s",
			     zif_package_get_printable (package->priv->installed),
			     zif_package_get_printable (ZIF_PACKAGE (package)),
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	return delta;
}

/**
 * zif_package_remote_get_update_detail:
 * @package: A #ZifPackageRemote
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Gets the update detail for a package.
 *
 * Return value: (transfer full): A %ZifUpdate, or %NULL for failure
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
		if (error_local->domain == ZIF_STORE_ERROR &&
		    error_local->code == ZIF_STORE_ERROR_NO_SUPPORT) {
			g_set_error (error,
				     ZIF_PACKAGE_ERROR,
				     ZIF_PACKAGE_ERROR_NO_SUPPORT,
				     "no support for getting update detail: %s",
				     error_local->message);
		} else {
			g_set_error (error,
				     ZIF_PACKAGE_ERROR,
				     ZIF_PACKAGE_ERROR_FAILED,
				     "cannot get update detail from store: %s",
				     error_local->message);
		}
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
zif_package_remote_ensure_data (ZifPackage *pkg,
				ZifPackageEnsureType type,
				ZifState *state,
				GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	ZifString *tmp = NULL;
	const gchar *text;
	const gchar *group;
	ZifPackageRemote *pkg_remote = ZIF_PACKAGE_REMOTE (pkg);

	g_return_val_if_fail (zif_state_valid (state), FALSE);

	if (type == ZIF_PACKAGE_ENSURE_TYPE_FILES) {

		/* never been set */
		if (pkg_remote->priv->store_remote == NULL) {
			g_set_error (error,
				     ZIF_PACKAGE_ERROR,
				     ZIF_PACKAGE_ERROR_FAILED,
				     "no remote store set on %s",
				     zif_package_get_printable (pkg));
			goto out;
		}

		/* get the file list for this package */
		array = zif_store_remote_get_files (pkg_remote->priv->store_remote,
						    pkg,
						    state,
						    error);
		if (array == NULL)
			goto out;

		/* set for this package */
		zif_package_set_files (pkg, array);
		zif_package_set_provides_files (pkg, array);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_DESCRIPTION) {

		/* some repo data doesn't include this for each package,
		 * so just set this to something sane rather than
		 * showing an error */
		tmp = zif_string_new ("No description provided");
		zif_package_set_description (pkg, tmp);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_REQUIRES) {

		/* never been set */
		if (pkg_remote->priv->store_remote == NULL) {
			g_set_error (error,
				     ZIF_PACKAGE_ERROR,
				     ZIF_PACKAGE_ERROR_FAILED,
				     "no remote store set on %s",
				     zif_package_get_printable (pkg));
			goto out;
		}

		/* get the file list for this package */
		array = zif_store_remote_get_requires (pkg_remote->priv->store_remote,
						       pkg,
						       state,
						       error);
		if (array == NULL)
			goto out;

		/* set for this package */
		zif_package_set_requires (pkg, array);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_PROVIDES) {

		/* never been set */
		if (pkg_remote->priv->store_remote == NULL) {
			g_set_error (error,
				     ZIF_PACKAGE_ERROR,
				     ZIF_PACKAGE_ERROR_FAILED,
				     "no remote store set on %s",
				     zif_package_get_printable (pkg));
			goto out;
		}

		/* get the file list for this package */
		array = zif_store_remote_get_provides (pkg_remote->priv->store_remote,
						       pkg,
						       state,
						       error);
		if (array == NULL)
			goto out;

		/* set for this package */
		zif_package_set_provides (pkg, array);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_OBSOLETES) {

		/* never been set */
		if (pkg_remote->priv->store_remote == NULL) {
			g_set_error (error,
				     ZIF_PACKAGE_ERROR,
				     ZIF_PACKAGE_ERROR_FAILED,
				     "no remote store set on %s",
				     zif_package_get_printable (pkg));
			goto out;
		}

		/* get the file list for this package */
		array = zif_store_remote_get_obsoletes (pkg_remote->priv->store_remote,
						        pkg,
						        state,
						        error);
		if (array == NULL)
			goto out;

		/* set for this package */
		zif_package_set_obsoletes (pkg, array);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_CONFLICTS) {

		/* never been set */
		if (pkg_remote->priv->store_remote == NULL) {
			g_set_error (error,
				     ZIF_PACKAGE_ERROR,
				     ZIF_PACKAGE_ERROR_FAILED,
				     "no remote store set on %s",
				     zif_package_get_printable (pkg));
			goto out;
		}

		/* get the file list for this package */
		array = zif_store_remote_get_conflicts (pkg_remote->priv->store_remote,
						        pkg,
						        state,
						        error);
		if (array == NULL)
			goto out;

		/* set for this package */
		zif_package_set_conflicts (pkg, array);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_CACHE_FILENAME) {

		/* get the file list for this package */
		ret = zif_package_remote_ensure_cache_filename (pkg_remote,
								state,
								error);
		if (!ret)
			goto out;

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_GROUP) {
		/* group */
		text = zif_package_get_category (pkg, state, error);
		if (text == NULL)
			goto out;
		group = zif_groups_get_group_for_cat (pkg_remote->priv->groups,
						      text,
						      state,
						      error);
		if (group == NULL)
			goto out;

		tmp = zif_string_new (group);
		zif_package_set_group (pkg, tmp);
	} else {
		g_set_error (error,
			     ZIF_PACKAGE_ERROR,
			     ZIF_PACKAGE_ERROR_NO_SUPPORT,
			     "Ensure type '%s' not supported on ZifPackageRemote %s",
			     zif_package_ensure_type_to_string (type),
			     zif_package_get_printable (pkg));
		goto out;
	}

	/* success */
	ret = TRUE;
out:
	if (tmp != NULL)
		zif_string_unref (tmp);
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

	g_object_unref (pkg->priv->groups);
	if (pkg->priv->store_remote != NULL)
		g_object_unref (pkg->priv->store_remote);
	if (pkg->priv->installed != NULL)
		g_object_unref (pkg->priv->installed);

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
	pkg->priv->groups = zif_groups_new ();
}

/**
 * zif_package_remote_new:
 *
 * Return value: A new #ZifPackageRemote instance.
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

