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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>

#include "dum-config.h"
#include "dum-store-remote.h"
#include "dum-repos.h"
#include "dum-utils.h"
#include "dum-monitor.h"

#include "egg-debug.h"
#include "egg-string.h"

#define DUM_REPOS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DUM_TYPE_REPOS, DumReposPrivate))

struct DumReposPrivate
{
	gboolean		 loaded;
	gchar			*repos_dir;
	DumMonitor		*monitor;
	GPtrArray		*list;
	GPtrArray		*enabled;
};

G_DEFINE_TYPE (DumRepos, dum_repos, G_TYPE_OBJECT)
static gpointer dum_repos_object = NULL;

/**
 * dum_repos_set_repos_dir:
 **/
gboolean
dum_repos_set_repos_dir (DumRepos *repos, const gchar *repos_dir, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;

	g_return_val_if_fail (DUM_IS_REPOS (repos), FALSE);
	g_return_val_if_fail (repos->priv->repos_dir == NULL, FALSE);
	g_return_val_if_fail (!repos->priv->loaded, FALSE);
	g_return_val_if_fail (repos_dir != NULL, FALSE);

	/* check directory exists */
	ret = g_file_test (repos_dir, G_FILE_TEST_IS_DIR);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "repo directory %s does not exist", repos_dir);
		goto out;
	}

	/* setup watch */
	ret = dum_monitor_add_watch (repos->priv->monitor, repos_dir, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to setup watch: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	repos->priv->repos_dir = g_strdup (repos_dir);
out:
	return ret;
}

/**
 * dum_repos_get_for_filename:
 **/
static gboolean
dum_repos_get_for_filename (DumRepos *repos, const gchar *filename, GError **error)
{
	GError *error_local = NULL;
	GKeyFile *file;
	gchar **repos_groups = NULL;
	DumStoreRemote *store;
	gboolean ret;
	gchar *path;
	guint i;

	/* find all the id's in this file */
	file = g_key_file_new ();
	path = g_build_filename (repos->priv->repos_dir, filename, NULL);
	ret = g_key_file_load_from_file (file, path, G_KEY_FILE_NONE, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to load %s: %s", path, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* for each group, add a store object */
	repos_groups = g_key_file_get_groups (file, NULL);
	for (i=0; repos_groups[i] != NULL; i++) {
		store = dum_store_remote_new ();
		ret = dum_store_remote_set_from_file (store, path, repos_groups[i], &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to set from %s: %s", path, error_local->message);
			g_error_free (error_local);
			break;
		}
		g_ptr_array_add (repos->priv->list, store);
	}
out:
	g_strfreev (repos_groups);
	g_free (path);
	g_key_file_free (file);
	return ret;
}

/**
 * dum_repos_load:
 **/
gboolean
dum_repos_load (DumRepos *repos, GError **error)
{
	gboolean ret = TRUE;
	DumStoreRemote *store;
	GError *error_local = NULL;
	GDir *dir;
	const gchar *filename;
	guint i;

	g_return_val_if_fail (DUM_IS_REPOS (repos), FALSE);
	g_return_val_if_fail (repos->priv->repos_dir != NULL, FALSE);

	/* already loaded */
	if (repos->priv->loaded)
		goto out;

	/* search repos dir */
	dir = g_dir_open (repos->priv->repos_dir, 0, &error_local);
	if (dir == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to list directory: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* get all repo files */
	filename = g_dir_read_name (dir);
	while (filename != NULL) {
		if (g_str_has_suffix (filename, ".repo")) {

			/* setup watch */
			ret = dum_monitor_add_watch (repos->priv->monitor, filename, &error_local);
			if (!ret) {
				if (error != NULL)
					*error = g_error_new (1, 0, "failed to setup watch: %s", error_local->message);
				g_error_free (error_local);
				break;
			}

			/* add all repos for filename */
			ret = dum_repos_get_for_filename (repos, filename, &error_local);
			if (!ret) {
				if (error != NULL)
					*error = g_error_new (1, 0, "failed to get filename %s: %s", filename, error_local->message);
				g_error_free (error_local);
				g_ptr_array_foreach (repos->priv->list, (GFunc) g_object_unref, NULL);
				g_ptr_array_set_size (repos->priv->list, 0);
				ret = FALSE;
				break;
			}
		}
		filename = g_dir_read_name (dir);
	}
	g_dir_close (dir);

	/* we failed one file, abandon attempt */
	if (!ret)
		goto out;

	/* find enabled */
	for (i=0; i<repos->priv->list->len; i++) {
		store = g_ptr_array_index (repos->priv->list, i);

		/* get repo enabled state */
		ret = dum_store_remote_get_enabled (store, &error_local);
		if (error_local != NULL) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to get repo state for %s: %s", dum_store_get_id (DUM_STORE (store)), error_local->message);
			g_ptr_array_foreach (repos->priv->enabled, (GFunc) g_object_unref, NULL);
			g_ptr_array_set_size (repos->priv->enabled, 0);
			ret = FALSE;
			goto out;
		}

		/* if enabled, add to array */
		if (ret)
			g_ptr_array_add (repos->priv->enabled, g_object_ref (store));
	}
	/* all loaded okay */
	repos->priv->loaded = TRUE;
	ret = TRUE;

out:
	return ret;
}

/**
 * dum_repos_get_stores:
 **/
GPtrArray *
dum_repos_get_stores (DumRepos *repos, GError **error)
{
	GPtrArray *array = NULL;
	GError *error_local;
	DumStoreRemote *store;
	gboolean ret;
	guint i;

	g_return_val_if_fail (DUM_IS_REPOS (repos), FALSE);

	/* if not already loaded, load */
	if (!repos->priv->loaded) {
		ret = dum_repos_load (repos, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load repos: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* make a copy */
	array = g_ptr_array_new ();
	for (i=0; i<repos->priv->list->len; i++) {
		store = g_ptr_array_index (repos->priv->list, i);
		g_ptr_array_add (array, g_object_ref (store));
	}
out:
	return array;
}

/**
 * dum_repos_get_stores_enabled:
 **/
GPtrArray *
dum_repos_get_stores_enabled (DumRepos *repos, GError **error)
{
	GPtrArray *array = NULL;
	GError *error_local;
	DumStoreRemote *store;
	gboolean ret;
	guint i;

	g_return_val_if_fail (DUM_IS_REPOS (repos), FALSE);

	/* if not already loaded, load */
	if (!repos->priv->loaded) {
		ret = dum_repos_load (repos, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load enabled repos: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* make a copy */
	array = g_ptr_array_new ();
	for (i=0; i<repos->priv->enabled->len; i++) {
		store = g_ptr_array_index (repos->priv->enabled, i);
		g_ptr_array_add (array, g_object_ref (store));
	}
out:
	return array;
}

/**
 * dum_repos_get_store:
 **/
DumStoreRemote *
dum_repos_get_store (DumRepos *repos, const gchar	*id, GError **error)
{
	guint i;
	DumStoreRemote *store = NULL;
	DumStoreRemote *store_tmp;
	const gchar *id_tmp;
	GError *error_local;
	gboolean ret;

	g_return_val_if_fail (DUM_IS_REPOS (repos), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	/* if not already loaded, load */
	if (!repos->priv->loaded) {
		ret = dum_repos_load (repos, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load repos: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* search though all the cached repos */
	for (i=0; i<repos->priv->list->len; i++) {
		store_tmp = g_ptr_array_index (repos->priv->list, i);

		/* get the id */
		id_tmp = dum_store_get_id (DUM_STORE (store_tmp));
		if (id_tmp == NULL) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to get id");
			goto out;
		}

		/* is it what we want? */
		if (strcmp (id_tmp, id) == 0) {
			store = g_object_ref (store_tmp);
			break;
		}
	}
out:
	return store;
}

/**
 * dum_repos_file_monitor_cb:
 **/
static void
dum_repos_file_monitor_cb (DumMonitor *monitor, DumRepos *repos)
{
	g_ptr_array_foreach (repos->priv->list, (GFunc) g_object_unref, NULL);
	g_ptr_array_foreach (repos->priv->enabled, (GFunc) g_object_unref, NULL);
	g_ptr_array_set_size (repos->priv->list, 0);
	g_ptr_array_set_size (repos->priv->enabled, 0);
	egg_debug ("repo file changed");
}

/**
 * dum_repos_finalize:
 **/
static void
dum_repos_finalize (GObject *object)
{
	DumRepos *repos;

	g_return_if_fail (object != NULL);
	g_return_if_fail (DUM_IS_REPOS (object));
	repos = DUM_REPOS (object);

	g_object_unref (repos->priv->monitor);
	g_free (repos->priv->repos_dir);

	g_ptr_array_foreach (repos->priv->list, (GFunc) g_object_unref, NULL);
	g_ptr_array_foreach (repos->priv->enabled, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (repos->priv->list, TRUE);
	g_ptr_array_free (repos->priv->enabled, TRUE);

	G_OBJECT_CLASS (dum_repos_parent_class)->finalize (object);
}

/**
 * dum_repos_class_init:
 **/
static void
dum_repos_class_init (DumReposClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dum_repos_finalize;
	g_type_class_add_private (klass, sizeof (DumReposPrivate));
}

/**
 * dum_repos_init:
 **/
static void
dum_repos_init (DumRepos *repos)
{
	repos->priv = DUM_REPOS_GET_PRIVATE (repos);
	repos->priv->repos_dir = NULL;
	repos->priv->list = g_ptr_array_new ();
	repos->priv->enabled = g_ptr_array_new ();
	repos->priv->monitor = dum_monitor_new ();
	g_signal_connect (repos->priv->monitor, "changed", G_CALLBACK (dum_repos_file_monitor_cb), repos);
}

/**
 * dum_repos_new:
 * Return value: A new repos class instance.
 **/
DumRepos *
dum_repos_new (void)
{
	if (dum_repos_object != NULL) {
		g_object_ref (dum_repos_object);
	} else {
		dum_repos_object = g_object_new (DUM_TYPE_REPOS, NULL);
		g_object_add_weak_pointer (dum_repos_object, &dum_repos_object);
	}
	return DUM_REPOS (dum_repos_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
dum_repos_test (EggTest *test)
{
	DumStoreRemote *store;
	DumConfig *config;
	DumRepos *repos;
	GPtrArray *array;
	GError *error = NULL;
	const gchar *value;
	guint i;
	gchar *repos_dir;
	gboolean ret;

	if (!egg_test_start (test, "DumRepos"))
		return;

	/* set this up as dummy */
	config = dum_config_new ();
	dum_config_set_filename (config, "../test/etc/yum.conf", NULL);
	repos_dir = dum_config_get_string (config, "reposdir", NULL);

	/************************************************************/
	egg_test_title (test, "get repos");
	repos = dum_repos_new ();
	egg_test_assert (test, repos != NULL);

	/************************************************************/
	egg_test_title (test, "set repos dir %s", repos_dir);
	ret = dum_repos_set_repos_dir (repos, repos_dir, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set repos dir '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "get list of repos");
	array = dum_repos_get_stores (repos, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load metadata '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "list correct length");
	if (array->len == 2)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);

	for (i=0; i<array->len; i++) {
		store = g_ptr_array_index (array, i);
		dum_store_print (DUM_STORE (store));
	}

	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);

	/************************************************************/
	egg_test_title (test, "get list of enabled repos");
	array = dum_repos_get_stores_enabled (repos, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load metadata '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "enabled correct length");
	if (array->len == 2)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);

	/* get ref for next test */
	store = g_object_ref (g_ptr_array_index (array, 0));
	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);

	/************************************************************/
	egg_test_title (test, "get name");
	value = dum_store_remote_get_name (store, NULL);
	if (egg_strequal (value, "Fedora 10 - i386"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid name '%s'", value);
	g_object_unref (store);

	g_object_unref (repos);
	g_object_unref (config);
	g_free (repos_dir);

	egg_test_end (test);
}
#endif

