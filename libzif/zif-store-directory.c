/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-store-directory
 * @short_description: Store for a bare folder of packages
 *
 * A #ZifStoreDirectory is a subclassed #ZifStore and operates on file objects.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

#include "zif-monitor.h"
#include "zif-package-local.h"
#include "zif-state.h"
#include "zif-store-directory.h"

#define ZIF_STORE_DIRECTORY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_STORE_DIRECTORY, ZifStoreDirectoryPrivate))

struct _ZifStoreDirectoryPrivate
{
	gboolean		 recursive;
	gchar			*path;
	guint			 monitor_changed_id;
	ZifMonitor		*monitor;
};

G_DEFINE_TYPE (ZifStoreDirectory, zif_store_directory, ZIF_TYPE_STORE)

/**
 * zif_store_directory_set_path:
 * @store: A #ZifStoreDirectory
 * @path: The directory with packages in, e.g. "/tmp/packages"
 * @recursive: If we should also add all sub-directories
 * @error: A #GError, or %NULL
 *
 * Sets the path to use for the store. The path should contain some
 * one or more rpm files.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.3.0
 **/
gboolean
zif_store_directory_set_path (ZifStoreDirectory *store,
			      const gchar *path,
			      gboolean recursive,
			      GError **error)
{
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_STORE_DIRECTORY (store), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check file exists */
	ret = g_file_test (path, G_FILE_TEST_IS_DIR);
	if (!ret) {
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "path %s does not exist",
			     path);
		goto out;
	}

	/* is the same */
	if (g_strcmp0 (path, store->priv->path) == 0)
		goto out;

	/* empty cache */
	zif_store_unload (ZIF_STORE (store), NULL);

	/* setup watch */
	ret = zif_monitor_add_watch (store->priv->monitor, path, error);
	if (!ret)
		goto out;

	/* save new value */
	g_free (store->priv->path);
	store->priv->path = g_strdup (path);
	store->priv->recursive = recursive;
out:
	return ret;
}

/**
 * zif_store_directory_get_path:
 * @store: A #ZifStoreDirectory
 *
 * Gets the path for the store.
 *
 * Return value: The install path, e.g. "/tmp/packages"
 *
 * Since: 0.3.0
 **/
const gchar *
zif_store_directory_get_path (ZifStoreDirectory *store)
{
	g_return_val_if_fail (ZIF_IS_STORE_DIRECTORY (store), NULL);
	return store->priv->path;
}

/**
 * zif_store_directory_load_file:
 **/
static gboolean
zif_store_directory_load_file (ZifStoreDirectory *directory,
			       const gchar *filename,
			       ZifState *state,
			       GError **error)
{
	gboolean ret;
	ZifPackage *package_tmp;

	/* read the file */
	package_tmp = zif_package_local_new ();
	ret = zif_package_local_set_from_filename (ZIF_PACKAGE_LOCAL (package_tmp),
						   filename,
						   error);
	if (!ret)
		goto out;

	/* add to the store */
	ret = zif_store_add_package (ZIF_STORE (directory),
				     package_tmp,
				     error);
	if (!ret)
		goto out;
out:
	g_object_unref (package_tmp);
	return ret;
}

/**
 * zif_store_directory_search_dir:
 **/
static gboolean
zif_store_directory_search_dir (ZifStoreDirectory *directory,
			        const gchar *path,
			        GPtrArray *results,
			        GError **error)
{
	const gchar *filename;
	gboolean is_dir;
	gboolean ret = TRUE;
	gchar *path_tmp;
	GDir *dir;

	/* search for files and directories */
	dir = g_dir_open (path, 0, error);
	if (dir == NULL)
		goto out;
	filename = g_dir_read_name (dir);
	while (filename != NULL) {
		path_tmp = g_build_filename (path, filename, NULL);

		/* is this a directory and are we recursive */
		is_dir = g_file_test (path_tmp, G_FILE_TEST_IS_DIR);
		if (is_dir && directory->priv->recursive) {
			ret = zif_store_directory_search_dir (directory,
							      path_tmp,
							      results,
							      error);

		/* check if an rpm file, TODO: use mime-type */
		} else if (g_str_has_suffix (filename, ".rpm")) {
			g_ptr_array_add (results,
					 g_strdup (path_tmp));
		}
		g_free (path_tmp);

		/* we encountered an error */
		if (!ret)
			goto out;
		filename = g_dir_read_name (dir);
	}
out:
	if (dir != NULL)
		g_dir_close (dir);
	return ret;
}

/**
 * zif_store_directory_load:
 **/
static gboolean
zif_store_directory_load (ZifStore *store, ZifState *state, GError **error)
{
	const gchar *filename;
	gboolean ret;
	GPtrArray *array = NULL;
	guint i;
	ZifState *state_local;
	ZifState *state_loop;
	ZifStoreDirectory *directory = ZIF_STORE_DIRECTORY (store);

	/* not yet set */
	if (directory->priv->path == NULL) {
		ret = FALSE;
		g_set_error_literal (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "directory path unset");
		goto out;
	}

	/* set steps */
	ret = zif_state_set_steps (state,
				   error,
				   10, /* search directory */
				   90, /* add packages */
				   -1);
	if (!ret)
		goto out;

	/* find a list of all the packages in all the directories */
	array = g_ptr_array_new_with_free_func (g_free);
	ret = zif_store_directory_search_dir (directory,
					      directory->priv->path,
					      array,
					      error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* create a package for each file and add to the store */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, array->len);
	for (i = 0; i < array->len; i++) {
		filename = g_ptr_array_index (array, i);
		state_loop = zif_state_get_child (state_local);
		ret = zif_store_directory_load_file (directory,
						     filename,
						     state_loop,
						     error);
		if (!ret)
			goto out;

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;

	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_store_directory_get_id:
 **/
static const gchar *
zif_store_directory_get_id (ZifStore *store)
{
	ZifStoreDirectory *directory = ZIF_STORE_DIRECTORY (store);
	return directory->priv->path;
}

/**
 * zif_store_directory_file_monitor_cb:
 **/
static void
zif_store_directory_file_monitor_cb (ZifMonitor *monitor,
				     ZifStoreDirectory *directory)
{
	g_debug ("directory %s changed", directory->priv->path);
	zif_store_unload (ZIF_STORE (directory), NULL);
}

/**
 * zif_store_directory_finalize:
 **/
static void
zif_store_directory_finalize (GObject *object)
{
	ZifStoreDirectory *store;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_STORE_DIRECTORY (object));
	store = ZIF_STORE_DIRECTORY (object);

	g_signal_handler_disconnect (store->priv->monitor,
				     store->priv->monitor_changed_id);
	g_object_unref (store->priv->monitor);
	g_free (store->priv->path);

	G_OBJECT_CLASS (zif_store_directory_parent_class)->finalize (object);
}

/**
 * zif_store_directory_class_init:
 **/
static void
zif_store_directory_class_init (ZifStoreDirectoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifStoreClass *store_class = ZIF_STORE_CLASS (klass);
	object_class->finalize = zif_store_directory_finalize;

	/* map */
	store_class->load = zif_store_directory_load;
	store_class->get_id = zif_store_directory_get_id;

	g_type_class_add_private (klass, sizeof (ZifStoreDirectoryPrivate));
}

/**
 * zif_store_directory_init:
 **/
static void
zif_store_directory_init (ZifStoreDirectory *store)
{
	store->priv = ZIF_STORE_DIRECTORY_GET_PRIVATE (store);
	store->priv->monitor = zif_monitor_new ();
	store->priv->monitor_changed_id =
		g_signal_connect (store->priv->monitor, "changed",
				  G_CALLBACK (zif_store_directory_file_monitor_cb), store);
}

/**
 * zif_store_directory_new:
 *
 * Return value: A new #ZifStoreDirectory instance.
 *
 * Since: 0.3.0
 **/
ZifStore *
zif_store_directory_new (void)
{
	ZifStore *store;
	store = g_object_new (ZIF_TYPE_STORE_DIRECTORY, NULL);
	return ZIF_STORE (store);
}
