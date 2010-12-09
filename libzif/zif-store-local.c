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
 * SECTION:zif-store-local
 * @short_description: Store for installed packages
 *
 * A #ZifStoreLocal is a subclassed #ZifStore and operates on installed objects.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#define _GNU_SOURCE
#include <string.h>

#include <glib.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmdb.h>
#include <fcntl.h>

#include "zif-config.h"
#include "zif-depend.h"
#include "zif-groups.h"
#include "zif-lock.h"
#include "zif-monitor.h"
#include "zif-object-array.h"
#include "zif-package-array.h"
#include "zif-package-local.h"
#include "zif-store.h"
#include "zif-store-local.h"
#include "zif-string.h"
#include "zif-utils.h"

#define ZIF_STORE_LOCAL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_STORE_LOCAL, ZifStoreLocalPrivate))

struct _ZifStoreLocalPrivate
{
	gboolean		 loaded;
	gchar			*prefix;
	GPtrArray		*packages;
	ZifGroups		*groups;
	ZifMonitor		*monitor;
	ZifLock			*lock;
	ZifConfig		*config;
	guint			 monitor_changed_id;
};

G_DEFINE_TYPE (ZifStoreLocal, zif_store_local, ZIF_TYPE_STORE)
static gpointer zif_store_local_object = NULL;

/**
 * zif_store_local_set_prefix:
 * @store: A #ZifStoreLocal
 * @prefix: The install root, e.g. "/", or NULL to use the default
 * @error: A #GError, or %NULL
 *
 * Sets the prefix to use for the install root.
 *
 * Using @prefix set to %NULL to use the value from the config file
 * has been supported since 0.1.3. Earlier versions will assert.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_store_local_set_prefix (ZifStoreLocal *store, const gchar *prefix, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	gchar *filename = NULL;
	gchar *prefix_real = NULL;

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get from config file */
	if (prefix == NULL) {
		prefix_real = zif_config_get_string (store->priv->config,
						     "prefix",
						     &error_local);
		if (prefix_real == NULL) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "default prefix not available: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
	} else {
		prefix_real = g_strdup (prefix);
	}

	/* check file exists */
	ret = g_file_test (prefix_real, G_FILE_TEST_IS_DIR);
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "prefix %s does not exist", prefix_real);
		goto out;
	}

	/* is the same */
	if (g_strcmp0 (prefix_real, store->priv->prefix) == 0)
		goto out;

	/* empty cache */
	if (store->priv->loaded) {
		g_debug ("abandoning cache");
		g_ptr_array_set_size (store->priv->packages, 0);
		store->priv->loaded = FALSE;
	}

	/* setup watch */
	filename = g_build_filename (prefix_real, "var", "lib", "rpm", "Packages", NULL);
	ret = zif_monitor_add_watch (store->priv->monitor, filename, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to setup watch: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* save new value */
	g_free (store->priv->prefix);
	store->priv->prefix = g_strdup (prefix_real);
out:
	g_free (prefix_real);
	g_free (filename);
	return ret;
}

/**
 * zif_store_local_get_prefix:
 * @store: A #ZifStoreLocal
 *
 * Gets the prefix to use for the install root.
 *
 * Return value: The install prefix, e.g. "/"
 *
 * Since: 0.1.3
 **/
const gchar *
zif_store_local_get_prefix (ZifStoreLocal *store)
{
	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	return store->priv->prefix;
}

/**
 * zif_store_local_load:
 **/
static gboolean
zif_store_local_load (ZifStore *store, ZifState *state, GError **error)
{
	gboolean ret = TRUE;
	GError *error_local = NULL;
	gint retval;
	Header header;
	rpmdb db = NULL;
	rpmdbMatchIterator mi = NULL;
	ZifPackageLocalFlags flags = 0;
	ZifPackage *package;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* not locked */
	ret = zif_lock_is_locked (local->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* already loaded */
	if (local->priv->loaded)
		goto out;

	/* setup steps */
	if (local->priv->prefix == NULL) {
		ret = zif_state_set_steps (state,
					   error,
					   10, /* set prefix */
					   10, /* open db */
					   80, /* add packages */
					   -1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   20, /* open db */
					   80, /* add packages */
					   -1);
	}
	if (!ret)
		goto out;

	/* use default prefix */
	if (local->priv->prefix == NULL) {

		/* set prefix */
		ret = zif_store_local_set_prefix (local, NULL, error);
		if (!ret)
			goto out;

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	retval = rpmdbOpen (local->priv->prefix, &db, O_RDWR, 0777);
	if (retval != 0) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to open rpmdb");
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
	zif_state_set_allow_cancel (state, FALSE);

	/* lookup in yumdb, and speed up for the future */
	flags += ZIF_PACKAGE_LOCAL_FLAG_LOOKUP;
//	flags += ZIF_PACKAGE_LOCAL_FLAG_REPAIR;

	/* get list */
	mi = rpmdbInitIterator (db, RPMDBI_PACKAGES, NULL, 0);
	if (mi == NULL)
		g_warning ("failed to get iterator");
	do {
		header = rpmdbNextIterator (mi);
		if (header == NULL)
			break;
		package = zif_package_local_new ();
		ret = zif_package_local_set_from_header (ZIF_PACKAGE_LOCAL (package),
							 header,
							 flags,
							 &error_local);
		if (!ret) {
			/* we ignore this one */
			if (error_local->domain == ZIF_PACKAGE_ERROR &&
			    error_local->code == ZIF_PACKAGE_ERROR_NO_SUPPORT) {
				g_clear_error (&error_local);
				g_object_unref (package);
			} else {
				g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
					     "failed to set from header: %s", error_local->message);
				g_error_free (error_local);
				g_object_unref (package);
				goto out;
			}
		} else {
			g_ptr_array_add (local->priv->packages, package);
		}
	} while (TRUE);

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* okay */
	local->priv->loaded = TRUE;
out:
	if (db != NULL) {
		rpmdbClose (db);
		rpmdbUnlink (db, NULL);
	}
	if (mi != NULL)
		rpmdbFreeIterator (mi);
	return ret;
}

/**
 * zif_store_local_search_name:
 **/
static GPtrArray *
zif_store_local_search_name (ZifStore *store, gchar **search, ZifState *state, GError **error)
{
	guint i, j;
	GPtrArray *array = NULL;
	GPtrArray *array_tmp = NULL;
	ZifPackage *package;
	const gchar *package_id;
	gchar *split_name;
	GError *error_local = NULL;
	gboolean ret;
	ZifState *state_local = NULL;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* not locked */
	ret = zif_lock_is_locked (local->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* setup steps */
	if (local->priv->loaded) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   20, /* search */
					   -1);
		if (!ret)
			goto out;
	}

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_store_local_load (store, state_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* check we have packages */
	if (local->priv->packages->len == 0) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "no packages in local sack");
		goto out;
	}

	/* setup state with the correct number of steps */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, local->priv->packages->len);

	/* iterate list */
	array_tmp = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		package_id = zif_package_get_id (package);
		split_name = zif_package_id_get_name (package_id);
		for (j=0; search[j] != NULL; j++) {
			if (strcasestr (split_name, search[j]) != NULL) {
				g_ptr_array_add (array_tmp, g_object_ref (package));
				break;
			}
		}
		g_free (split_name);

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	array = g_ptr_array_ref (array_tmp);
out:
	if (array_tmp != NULL)
		g_ptr_array_unref (array_tmp);
	return array;
}

/**
 * zif_store_local_search_category:
 **/
static GPtrArray *
zif_store_local_search_category (ZifStore *store, gchar **search, ZifState *state, GError **error)
{
	guint i, j;
	GPtrArray *array = NULL;
	ZifPackage *package;
	const gchar *category;
	GError *error_local = NULL;
	gboolean ret;
	ZifState *state_local = NULL;
	ZifState *state_loop = NULL;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* not locked */
	ret = zif_lock_is_locked (local->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED, "not locked");
		goto out;
	}

	/* setup steps */
	if (local->priv->loaded) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   20, /* search */
					   -1);
		if (!ret)
			goto out;
	}

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_store_local_load (store, state_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* check we have packages */
	if (local->priv->packages->len == 0) {
		g_warning ("no packages in sack, so nothing to do!");
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* setup state with the correct number of steps */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, local->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		state_loop = zif_state_get_child (state_local);
		category = zif_package_get_category (package, state_loop, NULL);
		for (j=0; search[j] != NULL; j++) {
			if (g_strcmp0 (category, search[j]) == 0) {
				g_ptr_array_add (array, g_object_ref (package));
				break;
			}
		}

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
	return array;
}

/**
 * zif_store_local_earch_details:
 **/
static GPtrArray *
zif_store_local_search_details (ZifStore *store, gchar **search, ZifState *state, GError **error)
{
	guint i, j;
	GPtrArray *array = NULL;
	ZifPackage *package;
	const gchar *package_id;
	const gchar *description;
	gchar *split_name;
	GError *error_local = NULL;
	gboolean ret;
	ZifState *state_local = NULL;
	ZifState *state_loop = NULL;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* not locked */
	ret = zif_lock_is_locked (local->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED, "not locked");
		goto out;
	}

	/* we have a different number of steps depending if we are loaded or not */
	if (local->priv->loaded) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   10, /* load */
					   90, /* search */
					   -1);
		if (!ret)
			goto out;
	}

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_store_local_load (store, state_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* check we have packages */
	if (local->priv->packages->len == 0) {
		g_warning ("no packages in sack, so nothing to do!");
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* setup state with the correct number of steps */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, local->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		package_id = zif_package_get_id (package);
		state_loop = zif_state_get_child (state_local);
		description = zif_package_get_description (package, state_loop, NULL);
		split_name = zif_package_id_get_name (package_id);
		for (j=0; search[j] != NULL; j++) {
			if (strcasestr (split_name, search[j]) != NULL) {
				g_ptr_array_add (array, g_object_ref (package));
				break;
			} else if (strcasestr (description, search[j]) != NULL) {
				g_ptr_array_add (array, g_object_ref (package));
				break;
			}
		}
		g_free (split_name);

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
	return array;
}

/**
 * zif_store_local_search_group:
 **/
static GPtrArray *
zif_store_local_search_group (ZifStore *store, gchar **search, ZifState *state, GError **error)
{
	guint i, j;
	GPtrArray *array = NULL;
	ZifPackage *package;
	GError *error_local = NULL;
	gboolean ret;
	const gchar *group;
	const gchar *group_tmp;
	ZifState *state_local = NULL;
	ZifState *state_loop = NULL;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* not locked */
	ret = zif_lock_is_locked (local->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED, "not locked");
		goto out;
	}

	/* setup steps */
	if (local->priv->loaded) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   20, /* search */
					   -1);
		if (!ret)
			goto out;
	}

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_store_local_load (store, state_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* check we have packages */
	if (local->priv->packages->len == 0) {
		g_warning ("no packages in sack, so nothing to do!");
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* setup state with the correct number of steps */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, local->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		for (j=0; search[j] != NULL; j++) {
			group = search[j];
			state_loop = zif_state_get_child (state_local);
			group_tmp = zif_package_get_group (package, state_loop, NULL);
			if (g_strcmp0 (group, group_tmp) == 0) {
				g_ptr_array_add (array, g_object_ref (package));
				break;
			}
		}

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
	return array;
}

/**
 * zif_store_local_search_file:
 **/
static GPtrArray *
zif_store_local_search_file (ZifStore *store, gchar **search, ZifState *state, GError **error)
{
	guint i, j, l;
	GPtrArray *array = NULL;
	GPtrArray *array_tmp = NULL;
	ZifPackage *package;
	GPtrArray *files;
	GError *error_local = NULL;
	const gchar *filename;
	gboolean ret;
	ZifState *state_local = NULL;
	ZifState *state_loop = NULL;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* not locked */
	ret = zif_lock_is_locked (local->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED, "not locked");
		goto out;
	}

	/* setup steps */
	if (local->priv->loaded) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   20, /* search */
					   -1);
		if (!ret)
			goto out;
	}

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_store_local_load (store, state_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* check we have packages */
	if (local->priv->packages->len == 0) {
		g_warning ("no packages in sack, so nothing to do!");
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}
	g_debug ("using %i local packages", local->priv->packages->len);

	/* setup state with the correct number of steps */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, local->priv->packages->len);

	/* iterate list */
	array_tmp = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		state_loop = zif_state_get_child (state_local);
		files = zif_package_get_files (package, state_loop, &error_local);
		if (files == NULL) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to get file lists: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
		for (j=0; j<files->len; j++) {
			filename = g_ptr_array_index (files, j);
			for (l=0; search[l] != NULL; l++) {
				if (g_strcmp0 (search[l], filename) == 0) {
					g_ptr_array_add (array_tmp, g_object_ref (package));
					break;
				}
			}
		}
		g_ptr_array_unref (files);

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	array = g_ptr_array_ref (array_tmp);
out:
	if (array_tmp != NULL)
		g_ptr_array_unref (array_tmp);
	return array;
}

/**
 * zif_store_local_resolve:
 **/
static GPtrArray *
zif_store_local_resolve (ZifStore *store, gchar **search, ZifState *state, GError **error)
{
	guint i, j;
	GPtrArray *array = NULL;
	ZifPackage *package;
	const gchar *package_id;
	GError *error_local = NULL;
	gboolean ret;
	gchar *split_name;
	ZifState *state_local = NULL;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* not locked */
	ret = zif_lock_is_locked (local->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED, "not locked");
		goto out;
	}

	/* setup steps */
	if (local->priv->loaded) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   20, /* search */
					   -1);
		if (!ret)
			goto out;
	}

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_store_local_load (store, state_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* check we have packages */
	if (local->priv->packages->len == 0) {
		g_warning ("no packages in sack, so nothing to do!");
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* setup state with the correct number of steps */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, local->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		package_id = zif_package_get_id (package);
		split_name = zif_package_id_get_name (package_id);
		for (j=0; search[j] != NULL; j++) {
			if (strcmp (split_name, search[j]) == 0) {
				g_ptr_array_add (array, g_object_ref (package));
				break;
			}
		}
		g_free (split_name);

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
	return array;
}

/**
 * zif_store_local_what_depends:
 **/
static GPtrArray *
zif_store_local_what_depends (ZifStore *store, ZifPackageEnsureType type,
			      GPtrArray *depends, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	GPtrArray *array_tmp = NULL;
	GPtrArray *depends_tmp;
	guint i;
	ZifDepend *depend_tmp;
	GError *error_local = NULL;
	gboolean ret;
	ZifState *state_local = NULL;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (depends != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* not locked */
	ret = zif_lock_is_locked (local->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED, "not locked");
		goto out;
	}

	/* setup steps */
	if (local->priv->loaded) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   20, /* search */
					   -1);
		if (!ret)
			goto out;
	}

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_store_local_load (store, state_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* check we have packages */
	if (local->priv->packages->len == 0) {
		g_warning ("no packages in sack, so nothing to do!");
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* just use the helper function */
	state_local = zif_state_get_child (state);
	array_tmp = zif_object_array_new ();
	for (i=0; i<depends->len; i++) {
		depend_tmp = g_ptr_array_index (depends, i);
		if (type == ZIF_PACKAGE_ENSURE_TYPE_PROVIDES) {
			ret = zif_package_array_provide (local->priv->packages,
							 depend_tmp, NULL,
							 &depends_tmp,
							 state_local,
							 error);
		} else if (type == ZIF_PACKAGE_ENSURE_TYPE_REQUIRES) {
			ret = zif_package_array_require (local->priv->packages,
							 depend_tmp, NULL,
							 &depends_tmp,
							 state_local,
							 error);
		} else if (type == ZIF_PACKAGE_ENSURE_TYPE_CONFLICTS) {
			ret = zif_package_array_conflict (local->priv->packages,
							  depend_tmp, NULL,
							  &depends_tmp,
							  state_local,
							  error);
		} else if (type == ZIF_PACKAGE_ENSURE_TYPE_OBSOLETES) {
			ret = zif_package_array_obsolete (local->priv->packages,
							  depend_tmp, NULL,
							  &depends_tmp,
							  state_local,
							  error);
		} else {
			g_assert_not_reached ();
		}
		if (!ret)
			goto out;

		/* add results */
		zif_object_array_add_array (array_tmp, depends_tmp);
		g_ptr_array_unref (depends_tmp);
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	array = g_ptr_array_ref (array_tmp);
out:
	if (array_tmp != NULL)
		g_ptr_array_unref (array_tmp);
	return array;
}

/**
 * zif_store_local_what_provides:
 **/
static GPtrArray *
zif_store_local_what_provides (ZifStore *store, GPtrArray *depends, ZifState *state, GError **error)
{
	return zif_store_local_what_depends (store,
					     ZIF_PACKAGE_ENSURE_TYPE_PROVIDES,
					     depends,
					     state,
					     error);
}

/**
 * zif_store_local_what_obsoletes:
 **/
static GPtrArray *
zif_store_local_what_obsoletes (ZifStore *store, GPtrArray *depends, ZifState *state, GError **error)
{
	return zif_store_local_what_depends (store,
					     ZIF_PACKAGE_ENSURE_TYPE_OBSOLETES,
					     depends,
					     state,
					     error);
}

/**
 * zif_store_local_what_conflicts:
 **/
static GPtrArray *
zif_store_local_what_conflicts (ZifStore *store, GPtrArray *depends, ZifState *state, GError **error)
{
	return zif_store_local_what_depends (store,
					     ZIF_PACKAGE_ENSURE_TYPE_CONFLICTS,
					     depends,
					     state,
					     error);
}

/**
 * zif_store_local_get_packages:
 **/
static GPtrArray *
zif_store_local_get_packages (ZifStore *store, ZifState *state, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	ZifPackage *package;
	GError *error_local = NULL;
	gboolean ret;
	ZifState *state_local = NULL;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* not locked */
	ret = zif_lock_is_locked (local->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED, "not locked");
		goto out;
	}

	/* setup steps */
	if (local->priv->loaded) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   20, /* search */
					   -1);
		if (!ret)
			goto out;
	}

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_store_local_load (store, state_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* check we have packages */
	if (local->priv->packages->len == 0) {
		g_warning ("no packages in sack, so nothing to do!");
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* setup state with the correct number of steps */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, local->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		g_ptr_array_add (array, g_object_ref (package));

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
	return array;
}

/**
 * zif_store_local_find_package:
 **/
static ZifPackage *
zif_store_local_find_package (ZifStore *store, const gchar *package_id, ZifState *state, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	ZifPackage *package = NULL;
	ZifPackage *package_tmp = NULL;
	GError *error_local = NULL;
	gboolean ret;
	const gchar *package_id_tmp;
	ZifState *state_local = NULL;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* not locked */
	ret = zif_lock_is_locked (local->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED, "not locked");
		goto out;
	}

	/* setup steps */
	if (local->priv->loaded) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   20, /* search */
					   -1);
		if (!ret)
			goto out;
	}

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_store_local_load (store, state_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* check we have packages */
	if (local->priv->packages->len == 0) {
		g_warning ("no packages in sack, so nothing to do!");
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* setup state with the correct number of steps */
	state_local = zif_state_get_child (state);

	/* setup state */
	zif_state_set_number_steps (state_local, local->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<local->priv->packages->len;i++) {
		package_tmp = g_ptr_array_index (local->priv->packages, i);
		package_id_tmp = zif_package_get_id (package_tmp);
		if (g_strcmp0 (package_id_tmp, package_id) == 0)
			g_ptr_array_add (array, g_object_ref (package_tmp));

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* nothing */
	if (array->len == 0) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED_TO_FIND,
				     "failed to find package");
		goto out;
	}

	/* more than one match */
	if (array->len > 1) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_MULTIPLE_MATCHES,
				     "more than one match");
		goto out;
	}

	/* return ref to package */
	package = g_object_ref (g_ptr_array_index (array, 0));

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	g_ptr_array_unref (array);
	return package;
}

/**
 * zif_store_local_get_id:
 **/
static const gchar *
zif_store_local_get_id (ZifStore *store)
{
	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	return "installed";
}

/**
 * zif_store_local_print:
 **/
static void
zif_store_local_print (ZifStore *store)
{
	guint i;
	ZifPackage *package;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_if_fail (ZIF_IS_STORE_LOCAL (store));
	g_return_if_fail (local->priv->prefix != NULL);
	g_return_if_fail (local->priv->packages->len != 0);

	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		zif_package_print (package);
	}
}

/**
 * zif_store_local_file_monitor_cb:
 **/
static void
zif_store_local_file_monitor_cb (ZifMonitor *monitor, ZifStoreLocal *store)
{
	store->priv->loaded = FALSE;

	g_ptr_array_set_size (store->priv->packages, 0);

	g_debug ("rpmdb changed");
}

/**
 * zif_store_local_finalize:
 **/
static void
zif_store_local_finalize (GObject *object)
{
	ZifStoreLocal *store;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_STORE_LOCAL (object));
	store = ZIF_STORE_LOCAL (object);

	g_ptr_array_unref (store->priv->packages);
	g_object_unref (store->priv->groups);
	g_signal_handler_disconnect (store->priv->monitor, store->priv->monitor_changed_id);
	g_object_unref (store->priv->monitor);
	g_object_unref (store->priv->lock);
	g_object_unref (store->priv->config);
	g_free (store->priv->prefix);

	G_OBJECT_CLASS (zif_store_local_parent_class)->finalize (object);
}

/**
 * zif_store_local_class_init:
 **/
static void
zif_store_local_class_init (ZifStoreLocalClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifStoreClass *store_class = ZIF_STORE_CLASS (klass);
	object_class->finalize = zif_store_local_finalize;

	/* map */
	store_class->load = zif_store_local_load;
	store_class->search_name = zif_store_local_search_name;
	store_class->search_category = zif_store_local_search_category;
	store_class->search_details = zif_store_local_search_details;
	store_class->search_group = zif_store_local_search_group;
	store_class->search_file = zif_store_local_search_file;
	store_class->resolve = zif_store_local_resolve;
	store_class->what_provides = zif_store_local_what_provides;
	store_class->what_obsoletes = zif_store_local_what_obsoletes;
	store_class->what_conflicts = zif_store_local_what_conflicts;
	store_class->get_packages = zif_store_local_get_packages;
	store_class->find_package = zif_store_local_find_package;
	store_class->get_id = zif_store_local_get_id;
	store_class->print = zif_store_local_print;

	g_type_class_add_private (klass, sizeof (ZifStoreLocalPrivate));
}

/**
 * zif_store_local_init:
 **/
static void
zif_store_local_init (ZifStoreLocal *store)
{
	store->priv = ZIF_STORE_LOCAL_GET_PRIVATE (store);
	store->priv->packages = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	store->priv->groups = zif_groups_new ();
	store->priv->monitor = zif_monitor_new ();
	store->priv->lock = zif_lock_new ();
	store->priv->config = zif_config_new ();
	store->priv->prefix = NULL;
	store->priv->loaded = FALSE;
	store->priv->monitor_changed_id =
		g_signal_connect (store->priv->monitor, "changed",
				  G_CALLBACK (zif_store_local_file_monitor_cb), store);
}

/**
 * zif_store_local_new:
 *
 * Return value: A new #ZifStoreLocal instance.
 *
 * Since: 0.1.0
 **/
ZifStore *
zif_store_local_new (void)
{
	if (zif_store_local_object != NULL) {
		g_object_ref (zif_store_local_object);
	} else {
		zif_store_local_object = g_object_new (ZIF_TYPE_STORE_LOCAL, NULL);
		g_object_add_weak_pointer (zif_store_local_object, &zif_store_local_object);
	}
	return ZIF_STORE (zif_store_local_object);
}

