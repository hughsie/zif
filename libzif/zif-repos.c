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
 * SECTION:zif-repos
 * @short_description: A #ZifRepos object manages software sources
 *
 * A #ZifRepos is an object that allows easy interfacing with remote
 * repositories.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>

#include "zif-config.h"
#include "zif-state.h"
#include "zif-store-remote.h"
#include "zif-repos.h"
#include "zif-utils.h"
#include "zif-monitor.h"

#include "egg-debug.h"

#define ZIF_REPOS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_REPOS, ZifReposPrivate))

struct _ZifReposPrivate
{
	gboolean		 loaded;
	gchar			*repos_dir;
	ZifMonitor		*monitor;
	GPtrArray		*list;
	GPtrArray		*enabled;
};

G_DEFINE_TYPE (ZifRepos, zif_repos, G_TYPE_OBJECT)
static gpointer zif_repos_object = NULL;

/**
 * zif_repos_error_quark:
 *
 * Return value: Our personal error quark.
 *
 * Since: 0.1.0
 **/
GQuark
zif_repos_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("zif_repos_error");
	return quark;
}

/**
 * zif_repos_set_repos_dir:
 * @repos: the #ZifRepos object
 * @repos_dir: the directory, e.g. "/etc/yum.repos.d"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Set the repository directory.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_repos_set_repos_dir (ZifRepos *repos, const gchar *repos_dir, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_REPOS (repos), FALSE);
	g_return_val_if_fail (repos->priv->repos_dir == NULL, FALSE);
	g_return_val_if_fail (!repos->priv->loaded, FALSE);
	g_return_val_if_fail (repos_dir != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check directory exists */
	ret = g_file_test (repos_dir, G_FILE_TEST_IS_DIR);
	if (!ret) {
		g_set_error (error, ZIF_REPOS_ERROR, ZIF_REPOS_ERROR_FAILED,
			     "repo directory %s does not exist", repos_dir);
		goto out;
	}

	/* setup watch */
	ret = zif_monitor_add_watch (repos->priv->monitor, repos_dir, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_REPOS_ERROR, ZIF_REPOS_ERROR_FAILED,
			     "failed to setup watch: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	repos->priv->repos_dir = g_strdup (repos_dir);
out:
	return ret;
}

/**
 * zif_repos_get_for_filename:
 **/
static gboolean
zif_repos_get_for_filename (ZifRepos *repos, const gchar *filename, ZifState *state, GError **error)
{
	GError *error_local = NULL;
	GKeyFile *file;
	gchar **repos_groups = NULL;
	ZifStoreRemote *store;
	ZifState *state_local;
	gboolean ret;
	gchar *path;
	guint i;

	/* find all the id's in this file */
	file = g_key_file_new ();
	path = g_build_filename (repos->priv->repos_dir, filename, NULL);
	ret = g_key_file_load_from_file (file, path, G_KEY_FILE_NONE, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_REPOS_ERROR, ZIF_REPOS_ERROR_FAILED,
			     "failed to load %s: %s", path, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* for each group, add a store object */
	repos_groups = g_key_file_get_groups (file, NULL);

	/* set number of stores */
	zif_state_set_number_steps (state, g_strv_length (repos_groups));

	/* create each repo */
	for (i=0; repos_groups[i] != NULL; i++) {
		store = zif_store_remote_new ();
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_set_from_file (store, path, repos_groups[i], state_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_REPOS_ERROR, ZIF_REPOS_ERROR_FAILED,
				     "failed to set from %s: %s", path, error_local->message);
			g_error_free (error_local);
			break;
		}
		g_ptr_array_add (repos->priv->list, store);

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}
out:
	g_strfreev (repos_groups);
	g_free (path);
	g_key_file_free (file);
	return ret;
}

/*
 * zif_repos_sort_store_cb:
 */
static gint
zif_repos_sort_store_cb (ZifStore **store1, ZifStore **store2)
{
	return g_strcmp0 (zif_store_get_id (*store1), zif_store_get_id (*store2));
}

/**
 * zif_repos_load:
 * @repos: the #ZifRepos object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Load the repository, and parse it's config file.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_repos_load (ZifRepos *repos, ZifState *state, GError **error)
{
	gboolean ret = TRUE;
	ZifStoreRemote *store;
	ZifState *state_local;
	GError *error_local = NULL;
	GDir *dir;
	const gchar *filename;
	guint i;
	GPtrArray *repofiles = NULL;

	g_return_val_if_fail (ZIF_IS_REPOS (repos), FALSE);
	g_return_val_if_fail (repos->priv->repos_dir != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already loaded */
	if (repos->priv->loaded)
		goto out;

	/* search repos dir */
	dir = g_dir_open (repos->priv->repos_dir, 0, &error_local);
	if (dir == NULL) {
		g_set_error (error, ZIF_REPOS_ERROR, ZIF_REPOS_ERROR_FAILED,
			     "failed to list directory: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* find the repo files we care about */
	repofiles = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
	filename = g_dir_read_name (dir);
	while (filename != NULL) {
		if (g_str_has_suffix (filename, ".repo"))
			g_ptr_array_add (repofiles, g_strdup (filename));
		filename = g_dir_read_name (dir);
	}
	g_dir_close (dir);

	/* setup state with the correct number of steps */
	zif_state_set_number_steps (state, repofiles->len + 1);

	/* for each repo files */
	for (i=0; i < repofiles->len; i++) {

		/* setup watch */
		filename = g_ptr_array_index (repofiles, i);
		ret = zif_monitor_add_watch (repos->priv->monitor, filename, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_REPOS_ERROR, ZIF_REPOS_ERROR_FAILED,
				     "failed to setup watch: %s", error_local->message);
			g_error_free (error_local);
			break;
		}

		/* add all repos for filename */
		state_local = zif_state_get_child (state);
		ret = zif_repos_get_for_filename (repos, filename, state_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_REPOS_ERROR, ZIF_REPOS_ERROR_FAILED,
				     "failed to get filename %s: %s", filename, error_local->message);
			g_error_free (error_local);
			g_ptr_array_set_size (repos->priv->list, 0);
			ret = FALSE;
			break;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* we failed one file, abandon attempt */
	if (!ret)
		goto out;

	/* need to sort by id predictably */
	g_ptr_array_sort (repos->priv->list, (GCompareFunc) zif_repos_sort_store_cb);

	/* find enabled */
	for (i=0; i<repos->priv->list->len; i++) {
		store = g_ptr_array_index (repos->priv->list, i);

		/* get repo enabled state */
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_get_enabled (store, state_local, &error_local);
		if (error_local != NULL) {
			g_set_error (error, ZIF_REPOS_ERROR, ZIF_REPOS_ERROR_FAILED,
				     "failed to get repo state for %s: %s", zif_store_get_id (ZIF_STORE (store)), error_local->message);
			g_ptr_array_set_size (repos->priv->enabled, 0);
			ret = FALSE;
			goto out;
		}

		/* if enabled, add to array */
		if (ret)
			g_ptr_array_add (repos->priv->enabled, g_object_ref (store));
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* all loaded okay */
	repos->priv->loaded = TRUE;
	ret = TRUE;

out:
	if (repofiles != NULL)
		g_ptr_array_unref (repofiles);
	return ret;
}

/**
 * zif_repos_get_stores:
 * @repos: the #ZifRepos object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the enabled and disabled remote stores.
 *
 * Return value: a list of #ZifStore's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_repos_get_stores (ZifRepos *repos, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	GError *error_local = NULL;
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_REPOS (repos), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* if not already loaded, load */
	if (!repos->priv->loaded) {
		ret = zif_repos_load (repos, state, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_REPOS_ERROR, ZIF_REPOS_ERROR_FAILED,
				     "failed to load repos: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* make a copy */
	array = g_ptr_array_ref (repos->priv->list);
out:
	return array;
}

/**
 * zif_repos_get_stores_enabled:
 * @repos: the #ZifRepos object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the enabled remote stores.
 *
 * Return value: a list of #ZifStore's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_repos_get_stores_enabled (ZifRepos *repos, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	GError *error_local = NULL;
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_REPOS (repos), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* if not already loaded, load */
	if (!repos->priv->loaded) {
		ret = zif_repos_load (repos, state, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_REPOS_ERROR, ZIF_REPOS_ERROR_FAILED,
				     "failed to load enabled repos: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* make a copy */
	array = g_ptr_array_ref (repos->priv->enabled);
out:
	return array;
}

/**
 * zif_repos_get_store:
 * @repos: the #ZifRepos object
 * @id: the repository id, e.g. "fedora"
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the store matching the ID.
 *
 * Return value: A #ZifStoreRemote object, or %NULL
 *
 * Since: 0.1.0
 **/
ZifStoreRemote *
zif_repos_get_store (ZifRepos *repos, const gchar *id, ZifState *state, GError **error)
{
	guint i;
	ZifStoreRemote *store = NULL;
	ZifStoreRemote *store_tmp;
	const gchar *id_tmp;
	GError *error_local = NULL;
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_REPOS (repos), NULL);
	g_return_val_if_fail (id != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* if not already loaded, load */
	if (!repos->priv->loaded) {
		ret = zif_repos_load (repos, state, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_REPOS_ERROR, ZIF_REPOS_ERROR_FAILED,
				     "failed to load repos: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* search though all the cached repos */
	for (i=0; i<repos->priv->list->len; i++) {
		store_tmp = g_ptr_array_index (repos->priv->list, i);

		/* get the id */
		id_tmp = zif_store_get_id (ZIF_STORE (store_tmp));
		if (id_tmp == NULL) {
			g_set_error_literal (error, ZIF_REPOS_ERROR, ZIF_REPOS_ERROR_FAILED,
					     "failed to get id");
			goto out;
		}

		/* is it what we want? */
		if (strcmp (id_tmp, id) == 0) {
			store = g_object_ref (store_tmp);
			break;
		}
	}

	/* we found nothing */
	if (store == NULL) {
		g_set_error (error, ZIF_REPOS_ERROR, ZIF_REPOS_ERROR_FAILED,
			     "failed to find store '%s'", id);
	}
out:
	return store;
}

/**
 * zif_repos_file_monitor_cb:
 **/
static void
zif_repos_file_monitor_cb (ZifMonitor *monitor, ZifRepos *repos)
{
	g_ptr_array_set_size (repos->priv->list, 0);
	g_ptr_array_set_size (repos->priv->enabled, 0);
	repos->priv->loaded = FALSE;
	egg_debug ("repo file changed");
}

/**
 * zif_repos_finalize:
 **/
static void
zif_repos_finalize (GObject *object)
{
	ZifRepos *repos;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_REPOS (object));
	repos = ZIF_REPOS (object);

	g_object_unref (repos->priv->monitor);
	g_free (repos->priv->repos_dir);

	g_ptr_array_unref (repos->priv->list);
	g_ptr_array_unref (repos->priv->enabled);

	G_OBJECT_CLASS (zif_repos_parent_class)->finalize (object);
}

/**
 * zif_repos_class_init:
 **/
static void
zif_repos_class_init (ZifReposClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_repos_finalize;
	g_type_class_add_private (klass, sizeof (ZifReposPrivate));
}

/**
 * zif_repos_init:
 **/
static void
zif_repos_init (ZifRepos *repos)
{
	repos->priv = ZIF_REPOS_GET_PRIVATE (repos);
	repos->priv->repos_dir = NULL;
	repos->priv->list = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	repos->priv->enabled = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	repos->priv->monitor = zif_monitor_new ();
	g_signal_connect (repos->priv->monitor, "changed", G_CALLBACK (zif_repos_file_monitor_cb), repos);
}

/**
 * zif_repos_new:
 *
 * Return value: A new #ZifRepos class instance.
 *
 * Since: 0.1.0
 **/
ZifRepos *
zif_repos_new (void)
{
	if (zif_repos_object != NULL) {
		g_object_ref (zif_repos_object);
	} else {
		zif_repos_object = g_object_new (ZIF_TYPE_REPOS, NULL);
		g_object_add_weak_pointer (zif_repos_object, &zif_repos_object);
	}
	return ZIF_REPOS (zif_repos_object);
}

