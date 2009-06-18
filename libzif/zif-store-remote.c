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

/**
 * SECTION:zif-store-remote
 * @short_description: A remote store is a store that can operate on remote packages
 *
 * A #ZifStoreRemote is a subclassed #ZifStore and operates on remote objects.
 * A repository is another name for a #ZifStoreRemote.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <stdlib.h>

#include "zif-utils.h"
#include "zif-config.h"
#include "zif-package.h"
#include "zif-package-remote.h"
#include "zif-store.h"
#include "zif-store-remote.h"
#include "zif-store-local.h"
#include "zif-repo-md-master.h"
#include "zif-repo-md-primary.h"
#include "zif-repo-md-filelists.h"
#include "zif-monitor.h"
#include "zif-download.h"

#include "egg-debug.h"
#include "egg-string.h"

#define ZIF_STORE_REMOTE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_STORE_REMOTE, ZifStoreRemotePrivate))

struct _ZifStoreRemotePrivate
{
	gchar			*id;
	gchar			*name;
	gchar			*name_expanded;
	gchar			*filename;
	gchar			*baseurl;
	gchar			*mirrorlist;
	gchar			*metalink;
	gboolean		 enabled;
	gboolean		 loaded;
	ZifRepoMd		*md_master;
	ZifRepoMd		*md_primary;
	ZifRepoMd		*md_filelists;
	ZifConfig		*config;
	ZifMonitor		*monitor;
	GPtrArray		*packages;
};

G_DEFINE_TYPE (ZifStoreRemote, zif_store_remote, ZIF_TYPE_STORE)

/**
 * zif_store_remote_expand_vars:
 **/
static gchar *
zif_store_remote_expand_vars (const gchar *name)
{
	gchar *name1;
	gchar *name2;

	name1 = egg_strreplace (name, "$releasever", "10");
	name2 = egg_strreplace (name1, "$basearch", "i386");

	g_free (name1);
	return name2;
}

/**
 * zif_store_remote_download:
 * @store: the #ZifStoreRemote object
 * @filename: the completion filename to download, e.g. "Packages/hal-0.0.1.rpm"
 * @directory: the directory to put the downloaded file, e.g. "/var/cache/zif"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting, or %NULL
 * @error: a #GError which is used on failure, or %NULL
 *
 * Downloads a remote package to a local directory.
 * NOTE: if @filename is "Packages/hal-0.0.1.rpm" and @directory is "/var/cache/zif"
 * then the downloaded file will "/var/cache/zif/hal-0.0.1.rpm"
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_store_remote_download (ZifStoreRemote *store, const gchar *filename, const gchar *directory, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = FALSE;
	gchar *uri = NULL;
	GError *error_local = NULL;
	ZifDownload *download = NULL;
	gchar *filename_local = NULL;
	gchar *basename = NULL;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (store->priv->id != NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (directory != NULL, FALSE);

	/* we need baseurl */
	if (store->priv->baseurl == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "don't support mirror lists at the moment on %s", store->priv->id);
		goto out;
	}

	/* download object */
	download = zif_download_new ();

	/* download */
	uri = g_build_filename (store->priv->baseurl, "repodata", filename, NULL);
	basename = g_path_get_basename (filename);
	filename_local = g_build_filename (directory, basename, NULL);
	ret = zif_download_file (download, uri, filename_local, cancellable, completion, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to download %s: %s", filename, error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	g_free (basename);
	g_free (filename_local);
	g_free (uri);
	if (download != NULL)
		g_object_unref (download);
	return ret;
}

/**
 * zif_store_remote_refresh:
 **/
static gboolean
zif_store_remote_refresh (ZifStore *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	const gchar *filename;
	const gchar *directory;
	ZifCompletion *completion_local;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	completion_local = zif_completion_new ();

	/* setup completion with the correct number of steps */
	if (completion != NULL) {
		zif_completion_set_number_steps (completion, 3);
		zif_completion_set_child (completion, completion_local);
	}

	/* master */
	filename = zif_repo_md_get_filename_raw (remote->priv->md_master);
	directory = zif_repo_md_get_local_path (remote->priv->md_master);

	/* download new file */
	egg_debug ("filename=%s, directory=%s", filename, directory);
	ret = zif_store_remote_download (ZIF_STORE_REMOTE (store), filename, directory, cancellable, completion_local, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to download master: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	if (completion != NULL)
		zif_completion_done (completion);

	/* reload */
	ret = zif_repo_md_load (remote->priv->md_master, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to load updated metadata: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	if (completion != NULL)
		zif_completion_done (completion);
#if 0
	/* primary */
	ret = zif_repo_md_refresh (remote->priv->md_primary, cancellable, completion_local, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to refresh primary: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	if (completion != NULL)
		zif_completion_done (completion);

	/* filelists */
	ret = zif_repo_md_refresh (remote->priv->md_filelists, cancellable, completion_local, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to refresh filelists: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	if (completion != NULL)
		zif_completion_done (completion);
#endif

out:
	g_object_unref (completion_local);
	return ret;
}

/**
 * zif_store_remote_load:
 **/
static gboolean
zif_store_remote_load (ZifStore *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GKeyFile *file = NULL;
	gboolean ret = TRUE;
	gchar *enabled = NULL;
	GError *error_local = NULL;
	gchar *cache_dir = NULL;
	gchar *temp;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	const ZifRepoMdInfoData *info_data;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);
	g_return_val_if_fail (remote->priv->filename != NULL, FALSE);

	/* already loaded */
	if (remote->priv->loaded)
		goto out;

	/* setup completion with the correct number of steps */
	if (completion != NULL)
		zif_completion_set_number_steps (completion, 3);

	file = g_key_file_new ();
	ret = g_key_file_load_from_file (file, remote->priv->filename, G_KEY_FILE_NONE, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to load %s: %s", remote->priv->filename, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	if (completion != NULL)
		zif_completion_done (completion);

	/* name */
	remote->priv->name = g_key_file_get_string (file, remote->priv->id, "name", &error_local);
	if (error_local != NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get name: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* enabled */
	enabled = g_key_file_get_string (file, remote->priv->id, "enabled", &error_local);
	if (enabled == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get enabled: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* convert to bool */
	remote->priv->enabled = zif_boolean_from_text (enabled);

	/* expand out */
	remote->priv->name_expanded = zif_store_remote_expand_vars (remote->priv->name);

	/* get base url (allowed to be blank) */
	temp = g_key_file_get_string (file, remote->priv->id, "baseurl", NULL);
	if (temp != NULL && temp[0] != '\0')
		remote->priv->baseurl = zif_store_remote_expand_vars (temp);
	g_free (temp);

	/* get mirror list (allowed to be blank) */
	temp = g_key_file_get_string (file, remote->priv->id, "mirrorlist", NULL);
	if (temp != NULL && temp[0] != '\0')
		remote->priv->mirrorlist = zif_store_remote_expand_vars (temp);
	g_free (temp);

	/* get metalink (allowed to be blank) */
	temp = g_key_file_get_string (file, remote->priv->id, "metalink", NULL);
	if (temp != NULL && temp[0] != '\0')
		remote->priv->metalink = zif_store_remote_expand_vars (temp);
	g_free (temp);

	/* we need either a base url or mirror list for an enabled store */
	if (remote->priv->enabled && remote->priv->baseurl == NULL && remote->priv->mirrorlist == NULL && remote->priv->metalink == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "baseurl, metalink or mirrorlist required");
		ret = FALSE;
		goto out;
	}

	/* don't load MD if not enabled */
	if (!remote->priv->enabled) {
		egg_debug ("no loading MD as not enabled");
		goto skip_md;
	}

	/* name */
	cache_dir = zif_config_get_string (remote->priv->config, "cachedir", &error_local);
	if (cache_dir == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get cachedir: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* set cache dir */
	ret = zif_repo_md_set_cache_dir (remote->priv->md_master, cache_dir);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to set cache dir: %s", cache_dir);
		goto out;
	}

	/* get metadata */
	ret = zif_repo_md_set_id (remote->priv->md_master, remote->priv->id);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to set id: %s", remote->priv->id);
		goto out;
	}

	/* don't load metadata for a disabled store */
	if (remote->priv->enabled) {
		ret = zif_repo_md_load (remote->priv->md_master, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* this section done */
	if (completion != NULL)
		zif_completion_done (completion);

	/* set cache dir */
	ret = zif_repo_md_set_cache_dir (remote->priv->md_filelists, cache_dir);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to set cache dir: %s", cache_dir);
		goto out;
	}

	/* get metadata */
	ret = zif_repo_md_set_id (remote->priv->md_filelists, remote->priv->id);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to set id: %s", remote->priv->id);
		goto out;
	}

	/* set info */
	info_data = zif_repo_md_master_get_info	(ZIF_REPO_MD_MASTER (remote->priv->md_master), ZIF_REPO_MD_TYPE_FILELISTS, &error_local);
	if (info_data == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get filelists md info: %s", error_local->message);
		ret = FALSE;
		goto out;
	}
	zif_repo_md_set_info_data (remote->priv->md_filelists, info_data);

	/* set cache dir */
	ret = zif_repo_md_set_cache_dir (remote->priv->md_primary, cache_dir);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to set cache dir: %s", cache_dir);
		goto out;
	}

	/* get metadata */
	ret = zif_repo_md_set_id (remote->priv->md_primary, remote->priv->id);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to set id: %s", remote->priv->id);
		goto out;
	}

	/* set info */
	info_data = zif_repo_md_master_get_info	(ZIF_REPO_MD_MASTER (remote->priv->md_master), ZIF_REPO_MD_TYPE_PRIMARY, &error_local);
	if (info_data == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get primary md info: %s", error_local->message);
		ret = FALSE;
		goto out;
	}
	zif_repo_md_set_info_data (remote->priv->md_primary, info_data);

skip_md:
	/* okay */
	remote->priv->loaded = TRUE;

	/* this section done */
	if (completion != NULL) {
		zif_completion_done (completion);
		zif_completion_set_percentage (completion, 100);
	}
out:
	g_free (cache_dir);
	g_free (enabled);
	if (file != NULL)
		g_key_file_free (file);
	return ret;
}

/**
 * zif_store_remote_clean:
 **/
static gboolean
zif_store_remote_clean (ZifStore *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	/* setup completion with the correct number of steps */
	if (completion != NULL)
		zif_completion_set_number_steps (completion, 3);

	/* clean primary */
	ret = zif_repo_md_clean (remote->priv->md_primary, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to clean primary: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	if (completion != NULL)
		zif_completion_done (completion);

	/* clean filelists */
	ret = zif_repo_md_clean (remote->priv->md_filelists, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to clean filelists: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	if (completion != NULL)
		zif_completion_done (completion);

	/* clean master (last) */
	ret = zif_repo_md_clean (remote->priv->md_master, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to clean master: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	if (completion != NULL)
		zif_completion_done (completion);
out:
	return ret;
}

/**
 * zif_store_remote_set_from_file:
 **/
gboolean
zif_store_remote_set_from_file (ZifStoreRemote *store, const gchar *filename, const gchar *id, GError **error)
{
	GError *error_local = NULL;
	gboolean ret = TRUE;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (id != NULL, FALSE);
	g_return_val_if_fail (store->priv->id == NULL, FALSE);
	g_return_val_if_fail (!store->priv->loaded, FALSE);

	/* save */
	egg_debug ("setting store %s", id);
	store->priv->id = g_strdup (id);
	store->priv->filename = g_strdup (filename);

	/* setup watch */
	ret = zif_monitor_add_watch (store->priv->monitor, filename, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to setup watch: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get data */
	ret = zif_store_remote_load (ZIF_STORE (store), NULL, NULL, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to load %s: %s", id, error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	/* save */
	return ret;
}

/**
 * zif_store_remote_set_enabled:
 * @store: the #ZifStoreRemote object
 * @enabled: If the object should be enabled
 * @error: a #GError which is used on failure, or %NULL
 *
 * Enable or disable a remote repository.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_store_remote_set_enabled (ZifStoreRemote *store, gboolean enabled, GError **error)
{
	GKeyFile *file;
	gboolean ret;
	GError *error_local = NULL;
	gchar *data;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (store->priv->id != NULL, FALSE);

	/* load file */
	file = g_key_file_new ();
	ret = g_key_file_load_from_file (file, store->priv->filename, G_KEY_FILE_KEEP_COMMENTS, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to load store file: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* toggle enabled */
	store->priv->enabled = enabled;
	g_key_file_set_boolean (file, store->priv->id, "enabled", store->priv->enabled);

	/* save new data to file */
	data = g_key_file_to_data (file, NULL, NULL);
	ret = g_file_set_contents (store->priv->filename, data, -1, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to save: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	g_free (data);
	g_key_file_free (file);
out:
	return ret;
}

/**
 * zif_store_remote_print:
 **/
static void
zif_store_remote_print (ZifStore *store)
{
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	g_return_if_fail (ZIF_IS_STORE_REMOTE (store));
	g_return_if_fail (remote->priv->id != NULL);

	g_print ("id: %s\n", remote->priv->id);
	g_print ("name: %s\n", remote->priv->name);
	g_print ("name-expanded: %s\n", remote->priv->name_expanded);
	g_print ("enabled: %i\n", remote->priv->enabled);
	zif_repo_md_print (remote->priv->md_master);
	zif_repo_md_print (remote->priv->md_primary);
	zif_repo_md_print (remote->priv->md_filelists);
}

/**
 * zif_store_remote_resolve:
 **/
static GPtrArray *
zif_store_remote_resolve (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GPtrArray *array;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	array = zif_repo_md_primary_resolve (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), search, error);
	return array;
}

/**
 * zif_store_remote_search_name:
 **/
static GPtrArray *
zif_store_remote_search_name (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GPtrArray *array;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	array = zif_repo_md_primary_search_name (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), search, error);
	return array;
}

/**
 * zif_store_remote_search_details:
 **/
static GPtrArray *
zif_store_remote_search_details (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GPtrArray *array;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	array = zif_repo_md_primary_search_details (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), search, error);
	return array;
}

/**
 * zif_store_remote_search_group:
 **/
static GPtrArray *
zif_store_remote_search_group (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GPtrArray *array;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	array = zif_repo_md_primary_search_group (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), search, error);
	return array;
}

/**
 * zif_store_remote_find_package:
 **/
static ZifPackage *
zif_store_remote_find_package (ZifStore *store, const PkPackageId *id, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GPtrArray *array;
	ZifPackage *package = NULL;
	GError *error_local = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	/* search with predicate, TODO: search version (epoch+release) */
	array = zif_repo_md_primary_find_package (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), id, error);
	if (array == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to search: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* nothing */
	if (array->len == 0) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to find package");
		goto out;
	}

	/* more than one match */
	if (array->len > 1) {
		if (error != NULL)
			*error = g_error_new (1, 0, "more than one match");
		goto out;
	}

	/* return ref to package */
	package = g_object_ref (g_ptr_array_index (array, 0));
out:
	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);
	return package;
}

/**
 * zif_store_remote_get_packages:
 **/
static GPtrArray *
zif_store_remote_get_packages (ZifStore *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GPtrArray *array;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	array = zif_repo_md_primary_get_packages (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), error);
	return array;
}


/**
 * zif_store_remote_get_updates:
 **/
static GPtrArray *
zif_store_remote_get_updates (ZifStore *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	ZifStore *store_local;
	GPtrArray *packages;
	GPtrArray *updates;
	GPtrArray *array = NULL;
	ZifPackage *package;
	ZifPackage *update;
	GError *error_local = NULL;
	guint i, j;
	gint val;
	const PkPackageId *id_package;
	const PkPackageId *id_update;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	/* get list of local packages */
	store_local = ZIF_STORE (zif_store_local_new ());
	packages = zif_store_get_packages (store_local, cancellable, completion, &error_local);
	if (packages == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get local store: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* create array for packages to update */
	array = g_ptr_array_new ();

	/* find each one in a remote repo */
	for (i=0; i<packages->len; i++) {
		package = ZIF_PACKAGE (g_ptr_array_index (packages, i));
		id_package = zif_package_get_id (package);

		/* find package name in repo */
		updates = zif_repo_md_primary_resolve (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), id_package->name, NULL);
		if (updates == NULL) {
			egg_debug ("not found %s", id_package->name);
			continue;
		}

		/* find updates */
		for (j=0; j<updates->len; j++) {
			update = ZIF_PACKAGE (g_ptr_array_index (updates, j));

			/* newer? */
			val = zif_package_compare (update, package);
			if (val > 0) {
				id_update = zif_package_get_id (update);
				egg_debug ("*** update %s from %s to %s", id_package->name, id_package->version, id_update->version);
				g_ptr_array_add (array, g_object_ref (update));
			}
		}
		g_ptr_array_foreach (updates, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (updates, TRUE);
	}

	g_ptr_array_foreach (packages, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (packages, TRUE);
	g_object_unref (store_local);
out:
	return array;
}

/**
 * zif_store_remote_what_provides:
 **/
static GPtrArray *
zif_store_remote_what_provides (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
//	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	//FIXME: load other MD
	return g_ptr_array_new ();
}

/**
 * zif_store_remote_search_file:
 **/
static GPtrArray *
zif_store_remote_search_file (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GError *error_local = NULL;
	GPtrArray *pkgids;
	GPtrArray *array = NULL;
	GPtrArray *tmp;
	ZifPackage *package;
	const gchar *pkgid;
	guint i, j;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	/* gets a list of pkgId's that match this file */
	pkgids = zif_repo_md_filelists_search_file (ZIF_REPO_MD_FILELISTS (remote->priv->md_filelists), search, &error_local);
	if (pkgids == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to load get list of pkgids: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* resolve the pkgId to a set of packages */
	array = g_ptr_array_new ();
	for (i=0; i<pkgids->len; i++) {
		pkgid = g_ptr_array_index (pkgids, i);

		/* get the results (should just be one) */
		tmp = zif_repo_md_primary_search_pkgid (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), pkgid, &error_local);
		if (tmp == NULL) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to resolve pkgId to package: %s", error_local->message);
			g_error_free (error_local);
			/* free what we've collected already */
			g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
			g_ptr_array_free (array, TRUE);
			array = NULL;
			goto out;
		}

		/* add to main array */
		for (j=0; j<tmp->len; j++) {
			package = g_ptr_array_index (tmp, j);
			g_ptr_array_add (array, g_object_ref (package));
		}

		/* free temp array */
		g_ptr_array_foreach (tmp, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (tmp, TRUE);
	}
out:
	return array;
}

/**
 * zif_store_remote_is_devel:
 * @store: the #ZifStoreRemote object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds out if the repository is a development repository.
 *
 * Return value: %TRUE or %FALSE
 **/
gboolean
zif_store_remote_is_devel (ZifStoreRemote *store, GError **error)
{
	GError *error_local = NULL;
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (store->priv->id != NULL, FALSE);

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		ret = zif_store_remote_load (ZIF_STORE (store), NULL, NULL, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load store file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* do tests */
	if (g_str_has_suffix (store->priv->id, "-debuginfo"))
		return TRUE;
	if (g_str_has_suffix (store->priv->id, "-testing"))
		return TRUE;
	if (g_str_has_suffix (store->priv->id, "-debug"))
		return TRUE;
	if (g_str_has_suffix (store->priv->id, "-development"))
		return TRUE;
	if (g_str_has_suffix (store->priv->id, "-source"))
		return TRUE;
out:
	return FALSE;
}

/**
 * zif_store_remote_get_id:
 * @store: the #ZifStoreRemote object
 *
 * Get the id of this repository.
 *
 * Return value: The repository id, e.g. "fedora"
 **/
static const gchar *
zif_store_remote_get_id (ZifStore *store)
{
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	return remote->priv->id;
}

/**
 * zif_store_remote_get_name:
 * @store: the #ZifStoreRemote object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Get the name of this repository.
 *
 * Return value: The repository name, e.g. "Fedora"
 **/
const gchar *
zif_store_remote_get_name (ZifStoreRemote *store, GError **error)
{
	GError *error_local = NULL;
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (store->priv->id != NULL, NULL);

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		ret = zif_store_remote_load (ZIF_STORE (store), NULL, NULL, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load store file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}
out:
	return store->priv->name_expanded;
}

/**
 * zif_store_remote_get_enabled:
 * @store: the #ZifStoreRemote object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find out if this repository is enabled or not.
 *
 * Return value: %TRUE or %FALSE
 **/
gboolean
zif_store_remote_get_enabled (ZifStoreRemote *store, GError **error)
{
	GError *error_local = NULL;
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (store->priv->id != NULL, FALSE);

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		ret = zif_store_remote_load (ZIF_STORE (store), NULL, NULL, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load store file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}
out:
	return store->priv->enabled;
}

/**
 * zif_store_remote_file_monitor_cb:
 **/
static void
zif_store_remote_file_monitor_cb (ZifMonitor *monitor, ZifStoreRemote *store)
{
	/* free invalid data */
	g_free (store->priv->id);
	g_free (store->priv->name);
	g_free (store->priv->name_expanded);
	g_free (store->priv->filename);
	g_free (store->priv->baseurl);
	g_free (store->priv->mirrorlist);
	g_free (store->priv->metalink);

	store->priv->loaded = FALSE;
	store->priv->enabled = FALSE;
	store->priv->id = NULL;
	store->priv->name = NULL;
	store->priv->name_expanded = NULL;
	store->priv->filename = NULL;
	store->priv->baseurl = NULL;
	store->priv->mirrorlist = NULL;
	store->priv->metalink = NULL;

	egg_debug ("store file changed");
}

/**
 * zif_store_remote_finalize:
 **/
static void
zif_store_remote_finalize (GObject *object)
{
	ZifStoreRemote *store;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_STORE_REMOTE (object));
	store = ZIF_STORE_REMOTE (object);

	g_free (store->priv->id);
	g_free (store->priv->name);
	g_free (store->priv->name_expanded);
	g_free (store->priv->filename);
	g_free (store->priv->baseurl);
	g_free (store->priv->mirrorlist);
	g_free (store->priv->metalink);

	g_object_unref (store->priv->md_master);
	g_object_unref (store->priv->md_filelists);
	g_object_unref (store->priv->config);
	g_object_unref (store->priv->monitor);

	G_OBJECT_CLASS (zif_store_remote_parent_class)->finalize (object);
}

/**
 * zif_store_remote_class_init:
 **/
static void
zif_store_remote_class_init (ZifStoreRemoteClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifStoreClass *store_class = ZIF_STORE_CLASS (klass);
	object_class->finalize = zif_store_remote_finalize;

	/* map */
	store_class->load = zif_store_remote_load;
	store_class->clean = zif_store_remote_clean;
	store_class->refresh = zif_store_remote_refresh;
	store_class->search_name = zif_store_remote_search_name;
//	store_class->search_category = zif_store_remote_search_category;
	store_class->search_details = zif_store_remote_search_details;
	store_class->search_group = zif_store_remote_search_group;
	store_class->search_file = zif_store_remote_search_file;
	store_class->resolve = zif_store_remote_resolve;
	store_class->what_provides = zif_store_remote_what_provides;
	store_class->get_packages = zif_store_remote_get_packages;
	store_class->get_updates = zif_store_remote_get_updates;
	store_class->find_package = zif_store_remote_find_package;
	store_class->get_id = zif_store_remote_get_id;
	store_class->print = zif_store_remote_print;

	g_type_class_add_private (klass, sizeof (ZifStoreRemotePrivate));
}

/**
 * zif_store_remote_init:
 **/
static void
zif_store_remote_init (ZifStoreRemote *store)
{
	store->priv = ZIF_STORE_REMOTE_GET_PRIVATE (store);
	store->priv->loaded = FALSE;
	store->priv->id = NULL;
	store->priv->name = NULL;
	store->priv->name_expanded = NULL;
	store->priv->enabled = FALSE;
	store->priv->filename = NULL;
	store->priv->baseurl = NULL;
	store->priv->mirrorlist = NULL;
	store->priv->metalink = NULL;
	store->priv->config = zif_config_new ();
	store->priv->md_master = ZIF_REPO_MD (zif_repo_md_master_new ());
	store->priv->md_filelists = ZIF_REPO_MD (zif_repo_md_filelists_new ());
	store->priv->md_primary = ZIF_REPO_MD (zif_repo_md_primary_new ());
	store->priv->monitor = zif_monitor_new ();
	g_signal_connect (store->priv->monitor, "changed", G_CALLBACK (zif_store_remote_file_monitor_cb), store);
}

/**
 * zif_store_remote_new:
 *
 * Return value: A new #ZifStoreRemote class instance.
 **/
ZifStoreRemote *
zif_store_remote_new (void)
{
	ZifStoreRemote *store;
	store = g_object_new (ZIF_TYPE_STORE_REMOTE, NULL);
	return ZIF_STORE_REMOTE (store);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"
#include "zif-groups.h"

void
zif_store_remote_test (EggTest *test)
{
	ZifGroups *groups;
	ZifStoreRemote *store;
	ZifStoreLocal *store_local;
	ZifConfig *config;
	GPtrArray *array;
	gboolean ret;
	GError *error = NULL;
	const gchar *id;

	if (!egg_test_start (test, "ZifStoreRemote"))
		return;

	/* set this up as zifmy */
	config = zif_config_new ();
	zif_config_set_filename (config, "../test/etc/yum.conf", NULL);

	/************************************************************/
	egg_test_title (test, "get store");
	store = zif_store_remote_new ();
	egg_test_assert (test, store != NULL);

	/************************************************************/
	egg_test_title (test, "load");
	ret = zif_store_remote_set_from_file (store, "../test/repos/fedora.repo", "fedora", &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	/* setup state */
	groups = zif_groups_new ();
	zif_groups_set_mapping_file (groups, "../test/share/yum-comps-groups.conf", NULL);
	store_local = zif_store_local_new ();
	zif_store_local_set_prefix (store_local, "/", NULL);
	/************************************************************/
	egg_test_title (test, "get updates");
	array = zif_store_remote_get_updates (ZIF_STORE (store), NULL, NULL, NULL);
	if (array->len > 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "no updates");
	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);
	g_object_unref (groups);
	g_object_unref (store_local);

	/************************************************************/
	egg_test_title (test, "is devel");
	ret = zif_store_remote_is_devel (store, NULL);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "is enabled");
	ret = zif_store_remote_get_enabled (store, NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get id");
	id = zif_store_get_id (ZIF_STORE (store));
	if (egg_strequal (id, "fedora"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid id '%s'", id);

	/************************************************************/
	egg_test_title (test, "get name");
	id = zif_store_remote_get_name (store, NULL);
	if (egg_strequal (id, "Fedora 10 - i386"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid name '%s'", id);

	/************************************************************/
	egg_test_title (test, "load metadata");
	ret = zif_store_remote_load (ZIF_STORE (store), NULL, NULL, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load metadata '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "resolve");
	array = zif_store_remote_resolve (ZIF_STORE (store), "kernel", NULL, NULL, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to resolve '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "correct number");
	if (array->len >= 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);

	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);

	/************************************************************/
	egg_test_title (test, "search name");
	array = zif_store_remote_search_name (ZIF_STORE (store), "power-manager", NULL, NULL, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to search name '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "search name correct number");
	if (array->len == 2)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);

	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);

	/************************************************************/
	egg_test_title (test, "search details");
	array = zif_store_remote_search_details (ZIF_STORE (store), "browser plugin", NULL, NULL, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to search details '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "search details correct number");
	if (array->len == 5)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);

	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);

	/************************************************************/
	egg_test_title (test, "search file");
	array = zif_store_remote_search_file (ZIF_STORE (store), "/usr/bin/gnome-power-manager", NULL, NULL, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to search details '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "search file correct number");
	if (array->len == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);

	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);

	/************************************************************/
	egg_test_title (test, "set disabled");
	ret = zif_store_remote_set_enabled (store, FALSE, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to disable '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "is enabled");
	ret = zif_store_remote_get_enabled (store, NULL);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "set enabled");
	ret = zif_store_remote_set_enabled (store, TRUE, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to enable '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "is enabled");
	ret = zif_store_remote_get_enabled (store, NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get packages");
	array = zif_store_remote_get_packages (ZIF_STORE (store), NULL, NULL, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get packages '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "correct number");
	if (array->len > 10000)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);

	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);

	g_object_unref (store);
	g_object_unref (config);

	egg_test_end (test);
}
#endif

