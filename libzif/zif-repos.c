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
 * SECTION:zif-repos
 * @short_description: Manage software sources
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
#include "zif-monitor.h"
#include "zif-object-array.h"
#include "zif-repos.h"
#include "zif-state.h"
#include "zif-store-local.h"
#include "zif-store-remote.h"
#include "zif-utils-private.h"

#define ZIF_REPOS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_REPOS, ZifReposPrivate))

struct _ZifReposPrivate
{
	gboolean		 loaded;
	gchar			*repos_dir;
	ZifMonitor		*monitor;
	ZifConfig		*config;
	GPtrArray		*list;
	guint			 monitor_changed_id;
};

G_DEFINE_TYPE (ZifRepos, zif_repos, G_TYPE_OBJECT)
static gpointer zif_repos_object = NULL;

/**
 * zif_repos_error_quark:
 *
 * Return value: An error quark.
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
 * @repos: A #ZifRepos
 * @repos_dir: A directory, e.g. "/etc/yum.repos.d", or NULL to use the default
 * @error: A #GError, or %NULL
 *
 * Set the repository directory.
 *
 * Using @repos_dir set to %NULL to use the value from the config file
 * has been supported since 0.1.3. Earlier versions will assert.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_repos_set_repos_dir (ZifRepos *repos, const gchar *repos_dir, GError **error)
{
	gboolean ret = FALSE;
	gchar *repos_dir_real = NULL;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_REPOS (repos), FALSE);
	g_return_val_if_fail (repos->priv->repos_dir == NULL, FALSE);
	g_return_val_if_fail (!repos->priv->loaded, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get from config file */
	if (repos_dir == NULL) {
		repos_dir_real = zif_config_get_string (repos->priv->config,
							"reposdir",
							&error_local);
		if (repos_dir_real == NULL) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "default reposdir not available: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
	} else {
		repos_dir_real = g_strdup (repos_dir);
	}

	/* check directory exists */
	ret = g_file_test (repos_dir_real, G_FILE_TEST_IS_DIR);
	if (!ret) {
		g_set_error (error,
			     ZIF_REPOS_ERROR,
			     ZIF_REPOS_ERROR_FAILED,
			     "repo directory %s does not exist",
			     repos_dir_real);
		goto out;
	}

	/* setup watch */
	ret = zif_monitor_add_watch (repos->priv->monitor,
				     repos_dir_real,
				     &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_REPOS_ERROR,
			     ZIF_REPOS_ERROR_FAILED,
			     "failed to setup watch: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	repos->priv->repos_dir = g_strdup (repos_dir_real);
out:
	g_free (repos_dir_real);
	return ret;
}

/**
 * zif_repos_get_for_filename:
 **/
static gboolean
zif_repos_get_for_filename (ZifRepos *repos,
			    const gchar *filename,
			    ZifState *state,
			    GError **error)
{
	GError *error_local = NULL;
	GKeyFile *file;
	gchar **repos_groups = NULL;
	ZifStoreRemote *store;
	ZifState *state_local;
	gboolean ret = TRUE;
	gchar *path;
	guint i;
	gsize groups_length;

	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* load file */
	path = g_build_filename (repos->priv->repos_dir,
				 filename,
				 NULL);
	file = zif_load_multiline_key_file (path, error);
	if (file == NULL) {
		ret = FALSE;
		goto out;
	}

	/* for each group, add a store object */
	repos_groups = g_key_file_get_groups (file, &groups_length);
	if (groups_length == 0) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_REPOS_ERROR,
			     ZIF_REPOS_ERROR_NO_DATA,
			     "no groups in %s",
			     filename);
		goto out;
	}

	/* set number of stores */
	zif_state_set_number_steps (state, groups_length);

	/* create each repo */
	for (i=0; repos_groups[i] != NULL; i++) {
		store = ZIF_STORE_REMOTE (zif_store_remote_new ());
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_set_from_file (store,
						      path,
						      repos_groups[i],
						      state_local,
						      &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_REPOS_ERROR,
				     ZIF_REPOS_ERROR_FAILED,
				     "failed to set from %s: %s",
				     path,
				     error_local->message);
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
	if (file != NULL)
		g_key_file_free (file);
	return ret;
}

/*
 * zif_repos_sort_store_cb:
 */
static gint
zif_repos_sort_store_cb (ZifStore **store1, ZifStore **store2)
{
	return g_strcmp0 (zif_store_get_id (*store1),
			  zif_store_get_id (*store2));
}

/**
 * zif_repos_load:
 * @repos: A #ZifRepos
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Load the repository, and parse it's config file.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_repos_load (ZifRepos *repos, ZifState *state, GError **error)
{
	const gchar *filename;
	gboolean ret = TRUE;
	GDir *dir;
	GError *error_local = NULL;
	GPtrArray *repofiles = NULL;
	guint i;
	ZifState *state_local;
	ZifState *state_loop;
	ZifStore *local = NULL;
	ZifStoreRemote *store;

	g_return_val_if_fail (ZIF_IS_REPOS (repos), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already loaded */
	if (repos->priv->loaded)
		goto out;

	/* take lock */
	ret = zif_state_take_lock (state,
				   ZIF_LOCK_TYPE_REPO,
				   ZIF_LOCK_MODE_THREAD,
				   error);
	if (!ret)
		goto out;

	/* set action */
	zif_state_action_start (state, ZIF_STATE_ACTION_LOADING_REPOS, NULL);

	/* set steps */
	if (repos->priv->repos_dir == NULL) {
		ret = zif_state_set_steps (state,
					   error,
					   5, /* set repodir */
					   5, /* read the repo filenames */
					   20, /* load the rpmdb */
					   10, /* add each repo */
					   60, /* get enabled */
					   -1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   10, /* read the repo filenames */
					   20, /* load the rpmdb */
					   10, /* add each repo */
					   60, /* get enabled */
					   -1);
	}
	if (!ret)
		goto out;

	/* load default repodir from config file */
	if (repos->priv->repos_dir == NULL) {
		ret = zif_repos_set_repos_dir (repos, NULL, error);
		if (!ret)
			goto out;
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* search repos dir */
	dir = g_dir_open (repos->priv->repos_dir, 0, &error_local);
	if (dir == NULL) {
		g_set_error (error,
			     ZIF_REPOS_ERROR,
			     ZIF_REPOS_ERROR_FAILED,
			     "failed to list directory: %s",
			     error_local->message);
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

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* it might seem odd to open and load the local store here, but
	 * we need to have set the releasever for the repo expansion */
	local = zif_store_local_new ();
	state_local = zif_state_get_child (state);
	ret = zif_store_load (local, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* setup state with the correct number of steps */
	state_local = zif_state_get_child (state);
	if (repofiles->len > 0)
		zif_state_set_number_steps (state_local, repofiles->len);

	/* for each repo files */
	for (i=0; i < repofiles->len; i++) {

		/* setup watch */
		filename = g_ptr_array_index (repofiles, i);
		ret = zif_monitor_add_watch (repos->priv->monitor,
					     filename,
					     &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_REPOS_ERROR,
				     ZIF_REPOS_ERROR_FAILED,
				     "failed to setup watch: %s",
				     error_local->message);
			g_error_free (error_local);
			break;
		}

		/* add all repos for filename */
		state_loop = zif_state_get_child (state_local);
		ret = zif_repos_get_for_filename (repos,
						  filename,
						  state_loop,
						  &error_local);
		if (!ret) {
			if (error_local->domain == ZIF_REPOS_ERROR &&
			    error_local->code == ZIF_REPOS_ERROR_NO_DATA) {
				g_debug ("ignoring: %s", error_local->message);
				g_clear_error (&error_local);
			} else {
				g_set_error (error,
					     ZIF_REPOS_ERROR,
					     ZIF_REPOS_ERROR_FAILED,
					     "failed to get filename %s: %s",
					     filename,
					     error_local->message);
				g_error_free (error_local);
				g_ptr_array_set_size (repos->priv->list, 0);
				ret = FALSE;
				break;
			}
		}

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* we failed one file, abandon attempt */
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* need to sort by id predictably */
	g_ptr_array_sort (repos->priv->list,
			  (GCompareFunc) zif_repos_sort_store_cb);

	/* find enabled */
	state_local = zif_state_get_child (state);
	if (repos->priv->list->len > 0)
		zif_state_set_number_steps (state_local, repos->priv->list->len);
	for (i=0; i<repos->priv->list->len; i++) {
		store = g_ptr_array_index (repos->priv->list, i);

		/* load, which sets the repo enabled state */
		state_loop = zif_state_get_child (state_local);
		ret = zif_store_load (ZIF_STORE (store),
				      state_loop,
				      &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_REPOS_ERROR,
				     ZIF_REPOS_ERROR_FAILED,
				     "failed to get load repo %s: %s",
				     zif_store_get_id (ZIF_STORE (store)),
				     error_local->message);
			ret = FALSE;
			goto out;
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

	/* all loaded okay */
	repos->priv->loaded = TRUE;
	ret = TRUE;

out:
	if (local != NULL)
		g_object_unref (local);
	if (repofiles != NULL)
		g_ptr_array_unref (repofiles);
	return ret;
}

/**
 * zif_repos_get_stores:
 * @repos: A #ZifRepos
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Gets the enabled and disabled remote stores.
 *
 * Return value: (transfer full): A list of #ZifStore's
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
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* if not already loaded, load */
	if (!repos->priv->loaded) {
		ret = zif_repos_load (repos, state, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_REPOS_ERROR,
				     ZIF_REPOS_ERROR_FAILED,
				     "failed to load repos: %s",
				     error_local->message);
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
 * @repos: A #ZifRepos
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Gets the enabled remote stores.
 *
 * Return value: (transfer full): A list of #ZifStore's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_repos_get_stores_enabled (ZifRepos *repos,
			      ZifState *state,
			      GError **error)
{
	GPtrArray *array = NULL;
	GError *error_local = NULL;
	gboolean ret;
	guint i;
	ZifStore *store;

	g_return_val_if_fail (ZIF_IS_REPOS (repos), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* if not already loaded, load */
	if (!repos->priv->loaded) {
		ret = zif_repos_load (repos, state, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_REPOS_ERROR,
				     ZIF_REPOS_ERROR_FAILED,
				     "failed to load enabled repos: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* add stores that have not been disabled */
	array = zif_object_array_new ();
	for (i=0; i<repos->priv->list->len; i++) {
		store = g_ptr_array_index (repos->priv->list, i);
		if (zif_store_get_enabled (store)) {
			zif_object_array_add (array, store);
		}
	}
out:
	return array;
}

/**
 * zif_repos_get_store:
 * @repos: A #ZifRepos
 * @id: A repository id, e.g. "fedora"
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Gets the store matching the ID.
 *
 * Return value: (transfer full): A #ZifStoreRemote object, or %NULL
 *
 * Since: 0.1.0
 **/
ZifStoreRemote *
zif_repos_get_store (ZifRepos *repos,
		     const gchar *id,
		     ZifState *state,
		     GError **error)
{
	guint i;
	ZifStoreRemote *store = NULL;
	ZifStoreRemote *store_tmp;
	const gchar *id_tmp;
	GError *error_local = NULL;
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_REPOS (repos), NULL);
	g_return_val_if_fail (id != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* if not already loaded, load */
	if (!repos->priv->loaded) {
		ret = zif_repos_load (repos, state, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_REPOS_ERROR,
				     ZIF_REPOS_ERROR_FAILED,
				     "failed to load repos: %s",
				     error_local->message);
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
			g_set_error_literal (error,
					     ZIF_REPOS_ERROR,
					     ZIF_REPOS_ERROR_FAILED,
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
		g_set_error (error,
			     ZIF_REPOS_ERROR,
			     ZIF_REPOS_ERROR_FAILED,
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
	repos->priv->loaded = FALSE;
	g_debug ("repo file changed");
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

	g_signal_handler_disconnect (repos->priv->monitor,
				     repos->priv->monitor_changed_id);
	g_object_unref (repos->priv->monitor);
	g_object_unref (repos->priv->config);
	g_free (repos->priv->repos_dir);

	g_ptr_array_unref (repos->priv->list);

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
	repos->priv->monitor = zif_monitor_new ();
	repos->priv->config = zif_config_new ();
	repos->priv->monitor_changed_id =
		g_signal_connect (repos->priv->monitor, "changed",
				  G_CALLBACK (zif_repos_file_monitor_cb), repos);
}

/**
 * zif_repos_new:
 *
 * Return value: A new #ZifRepos instance.
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

