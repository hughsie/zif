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
#include <stdlib.h>

#include "dum-utils.h"
#include "dum-config.h"
#include "dum-package.h"
#include "dum-package-remote.h"
#include "dum-store.h"
#include "dum-store-remote.h"
#include "dum-repo-md-master.h"
#include "dum-repo-md-primary.h"
#include "dum-repo-md-filelists.h"
#include "dum-monitor.h"
#include "dum-download.h"

#include "egg-debug.h"
#include "egg-string.h"

#define DUM_STORE_REMOTE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DUM_TYPE_STORE_REMOTE, DumStoreRemotePrivate))

struct DumStoreRemotePrivate
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
	DumRepoMdMaster		*md_master;
	DumRepoMdPrimary	*md_primary;
	DumRepoMdFilelists	*md_filelists;
	DumConfig		*config;
	DumMonitor		*monitor;
	GPtrArray		*packages;
};

G_DEFINE_TYPE (DumStoreRemote, dum_store_remote, DUM_TYPE_STORE)

/**
 * dum_store_remote_expand_vars:
 **/
static gchar *
dum_store_remote_expand_vars (const gchar *name)
{
	gchar *name1;
	gchar *name2;

	name1 = egg_strreplace (name, "$releasever", "10");
	name2 = egg_strreplace (name1, "$basearch", "i386");

	g_free (name1);
	return name2;
}

/**
 * dum_store_remote_download_progress_changed:
 **/
static void
dum_store_remote_download_progress_changed (DumDownload *download, guint value, DumStoreRemote *store)
{
	egg_warning ("percentage: %i", value);
}

/**
 * dum_store_remote_download:
 **/
gboolean
dum_store_remote_download (DumStoreRemote *store, const gchar *filename, const gchar *directory, GError **error)
{
	gboolean ret = FALSE;
	gchar *uri = NULL;
	GError *error_local = NULL;
	DumDownload *download = NULL;

	g_return_val_if_fail (DUM_IS_STORE_REMOTE (store), FALSE);
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
	download = dum_download_new ();
	g_signal_connect (download, "percentage-changed", G_CALLBACK (dum_store_remote_download_progress_changed), store);

	/* download */
	uri = g_strconcat (store->priv->baseurl, filename, NULL);
	ret = dum_download_file (download, uri, "/tmp/moo.rpm", &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to download %s: %s", filename, error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	g_free (uri);
	if (download != NULL)
		g_object_unref (download);
	return ret;
}

/**
 * dum_store_remote_load:
 **/
static gboolean
dum_store_remote_load (DumStore *store, GError **error)
{
	GKeyFile *file = NULL;
	gboolean ret = TRUE;
	gchar *enabled = NULL;
	GError *error_local = NULL;
	gchar *cache_dir = NULL;
	gchar *temp;
	DumStoreRemote *remote = DUM_STORE_REMOTE (store);
	const DumRepoMdInfoData *info_data;

	g_return_val_if_fail (DUM_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);
	g_return_val_if_fail (remote->priv->filename != NULL, FALSE);

	/* already loaded */
	if (remote->priv->loaded)
		goto out;

	file = g_key_file_new ();
	ret = g_key_file_load_from_file (file, remote->priv->filename, G_KEY_FILE_NONE, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to load %s: %s", remote->priv->filename, error_local->message);
		g_error_free (error_local);
		goto out;
	}

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
	remote->priv->enabled = dum_boolean_from_text (enabled);

	/* expand out */
	remote->priv->name_expanded = dum_store_remote_expand_vars (remote->priv->name);

	/* get base url (allowed to be blank) */
	temp = g_key_file_get_string (file, remote->priv->id, "baseurl", NULL);
	if (temp != NULL && temp[0] != '\0')
		remote->priv->baseurl = dum_store_remote_expand_vars (temp);
	g_free (temp);

	/* get mirror list (allowed to be blank) */
	temp = g_key_file_get_string (file, remote->priv->id, "mirrorlist", NULL);
	if (temp != NULL && temp[0] != '\0')
		remote->priv->mirrorlist = dum_store_remote_expand_vars (temp);
	g_free (temp);

	/* get metalink (allowed to be blank) */
	temp = g_key_file_get_string (file, remote->priv->id, "metalink", NULL);
	if (temp != NULL && temp[0] != '\0')
		remote->priv->metalink = dum_store_remote_expand_vars (temp);
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
	cache_dir = dum_config_get_string (remote->priv->config, "cachedir", &error_local);
	if (cache_dir == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get cachedir: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* set cache dir */
	ret = dum_repo_md_set_cache_dir (DUM_REPO_MD (remote->priv->md_master), cache_dir);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to set cache dir: %s", cache_dir);
		goto out;
	}

	/* get metadata */
	ret = dum_repo_md_set_id (DUM_REPO_MD (remote->priv->md_master), remote->priv->id);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to set id: %s", remote->priv->id);
		goto out;
	}

	/* set info */
	info_data = dum_repo_md_master_get_info	(remote->priv->md_master, DUM_REPO_MD_TYPE_PRIMARY, &error_local);
	if (info_data == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get primary md info: %s", error_local->message);
		ret = FALSE;
		goto out;
	}
	dum_repo_md_set_info_data (DUM_REPO_MD (remote->priv->md_master), info_data);

	/* don't load metadata for a disabled store */
	if (remote->priv->enabled) {
		ret = dum_repo_md_load (DUM_REPO_MD (remote->priv->md_master), &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* set cache dir */
	ret = dum_repo_md_set_cache_dir (DUM_REPO_MD (remote->priv->md_filelists), cache_dir);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to set cache dir: %s", cache_dir);
		goto out;
	}

	/* get metadata */
	ret = dum_repo_md_set_id (DUM_REPO_MD (remote->priv->md_filelists), remote->priv->id);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to set id: %s", remote->priv->id);
		goto out;
	}

	/* set info */
	info_data = dum_repo_md_master_get_info	(remote->priv->md_master, DUM_REPO_MD_TYPE_FILELISTS, &error_local);
	if (info_data == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get filelists md info: %s", error_local->message);
		ret = FALSE;
		goto out;
	}
	dum_repo_md_set_info_data (DUM_REPO_MD (remote->priv->md_filelists), info_data);

	/* set cache dir */
	ret = dum_repo_md_set_cache_dir (DUM_REPO_MD (remote->priv->md_primary), cache_dir);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to set cache dir: %s", cache_dir);
		goto out;
	}

	/* get metadata */
	ret = dum_repo_md_set_id (DUM_REPO_MD (remote->priv->md_primary), remote->priv->id);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to set id: %s", remote->priv->id);
		goto out;
	}

	/* set info */
	info_data = dum_repo_md_master_get_info	(remote->priv->md_master, DUM_REPO_MD_TYPE_PRIMARY, &error_local);
	if (info_data == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get primary md info: %s", error_local->message);
		ret = FALSE;
		goto out;
	}
	dum_repo_md_set_info_data (DUM_REPO_MD (remote->priv->md_primary), info_data);

skip_md:
	/* okay */
	remote->priv->loaded = TRUE;

out:
	g_free (cache_dir);
	g_free (enabled);
	if (file != NULL)
		g_key_file_free (file);
	return ret;
}

/**
 * dum_store_remote_set_from_file:
 **/
gboolean
dum_store_remote_set_from_file (DumStoreRemote *store, const gchar *filename, const gchar *id, GError **error)
{
	GError *error_local = NULL;
	gboolean ret = TRUE;

	g_return_val_if_fail (DUM_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (id != NULL, FALSE);
	g_return_val_if_fail (store->priv->id == NULL, FALSE);
	g_return_val_if_fail (!store->priv->loaded, FALSE);

	/* save */
	egg_debug ("setting store %s", id);
	store->priv->id = g_strdup (id);
	store->priv->filename = g_strdup (filename);

	/* setup watch */
	ret = dum_monitor_add_watch (store->priv->monitor, filename, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to setup watch: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get data */
	ret = dum_store_remote_load (DUM_STORE (store), &error_local);
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
 * dum_store_remote_set_enabled:
 **/
gboolean
dum_store_remote_set_enabled (DumStoreRemote *store, gboolean enabled, GError **error)
{
	GKeyFile *file;
	gboolean ret;
	GError *error_local = NULL;
	gchar *data;

	g_return_val_if_fail (DUM_IS_STORE_REMOTE (store), FALSE);
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
 * dum_store_remote_print:
 **/
static void
dum_store_remote_print (DumStore *store)
{
	DumStoreRemote *remote = DUM_STORE_REMOTE (store);

	g_return_if_fail (DUM_IS_STORE_REMOTE (store));
	g_return_if_fail (remote->priv->id != NULL);

	g_print ("id: %s\n", remote->priv->id);
	g_print ("name: %s\n", remote->priv->name);
	g_print ("name-expanded: %s\n", remote->priv->name_expanded);
	g_print ("enabled: %i\n", remote->priv->enabled);
	dum_repo_md_print (DUM_REPO_MD (remote->priv->md_master));
	dum_repo_md_print (DUM_REPO_MD (remote->priv->md_primary));
	dum_repo_md_print (DUM_REPO_MD (remote->priv->md_filelists));
}

/**
 * dum_store_remote_resolve:
 **/
static GPtrArray *
dum_store_remote_resolve (DumStore *store, const gchar *search, GError **error)
{
	GPtrArray *array;
	DumStoreRemote *remote = DUM_STORE_REMOTE (store);

	g_return_val_if_fail (DUM_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	array = dum_repo_md_primary_resolve (remote->priv->md_primary, search, error);
	return array;
}

/**
 * dum_store_remote_search_name:
 **/
static GPtrArray *
dum_store_remote_search_name (DumStore *store, const gchar *search, GError **error)
{
	GPtrArray *array;
	DumStoreRemote *remote = DUM_STORE_REMOTE (store);

	g_return_val_if_fail (DUM_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	array = dum_repo_md_primary_search_name (remote->priv->md_primary, search, error);
	return array;
}

/**
 * dum_store_remote_search_details:
 **/
static GPtrArray *
dum_store_remote_search_details (DumStore *store, const gchar *search, GError **error)
{
	GPtrArray *array;
	DumStoreRemote *remote = DUM_STORE_REMOTE (store);

	g_return_val_if_fail (DUM_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	array = dum_repo_md_primary_search_details (remote->priv->md_primary, search, error);
	return array;
}

/**
 * dum_store_remote_search_group:
 **/
static GPtrArray *
dum_store_remote_search_group (DumStore *store, const gchar *search, GError **error)
{
	GPtrArray *array;
	DumStoreRemote *remote = DUM_STORE_REMOTE (store);

	g_return_val_if_fail (DUM_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	array = dum_repo_md_primary_search_group (remote->priv->md_primary, search, error);
	return array;
}

/**
 * dum_store_remote_find_package:
 **/
static DumPackage *
dum_store_remote_find_package (DumStore *store, const PkPackageId *id, GError **error)
{
	GPtrArray *array;
	DumPackage *package = NULL;
	GError *error_local = NULL;
	DumStoreRemote *remote = DUM_STORE_REMOTE (store);

	g_return_val_if_fail (DUM_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	/* search with predicate, TODO: search version (epoch+release) */
	array = dum_repo_md_primary_find_package (remote->priv->md_primary, id, error);
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
 * dum_store_remote_get_packages:
 **/
static GPtrArray *
dum_store_remote_get_packages (DumStore *store, GError **error)
{
	GPtrArray *array;
	DumStoreRemote *remote = DUM_STORE_REMOTE (store);

	g_return_val_if_fail (DUM_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	array = dum_repo_md_primary_get_packages (remote->priv->md_primary, error);
	return array;
}

/**
 * dum_store_remote_what_provides:
 **/
static GPtrArray *
dum_store_remote_what_provides (DumStore *store, const gchar *search, GError **error)
{
//	DumStoreRemote *remote = DUM_STORE_REMOTE (store);
	//FIXME: load other MD
	return g_ptr_array_new ();
}

/**
 * dum_store_remote_search_file:
 **/
static GPtrArray *
dum_store_remote_search_file (DumStore *store, const gchar *search, GError **error)
{
	GError *error_local = NULL;
	GPtrArray *pkgids;
	GPtrArray *array = NULL;
	GPtrArray *tmp;
	DumPackage *package;
	const gchar *pkgid;
	guint i, j;
	DumStoreRemote *remote = DUM_STORE_REMOTE (store);

	/* gets a list of pkgId's that match this file */
	pkgids = dum_repo_md_filelists_search_file (remote->priv->md_filelists, search, &error_local);
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
		tmp = dum_repo_md_primary_search_pkgid (remote->priv->md_primary, pkgid, &error_local);
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
 * dum_store_remote_is_devel:
 **/
gboolean
dum_store_remote_is_devel (DumStoreRemote *store, GError **error)
{
	GError *error_local = NULL;
	gboolean ret;

	g_return_val_if_fail (DUM_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (store->priv->id != NULL, FALSE);

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		ret = dum_store_remote_load (DUM_STORE (store), &error_local);
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
 * dum_store_remote_get_id:
 **/
static const gchar *
dum_store_remote_get_id (DumStore *store)
{
	DumStoreRemote *remote = DUM_STORE_REMOTE (store);
	g_return_val_if_fail (DUM_IS_STORE_REMOTE (store), NULL);
	return remote->priv->id;
}

/**
 * dum_store_remote_get_name:
 **/
const gchar *
dum_store_remote_get_name (DumStoreRemote *store, GError **error)
{
	GError *error_local = NULL;
	gboolean ret;

	g_return_val_if_fail (DUM_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (store->priv->id != NULL, NULL);

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		ret = dum_store_remote_load (DUM_STORE (store), &error_local);
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
 * dum_store_remote_get_enabled:
 **/
gboolean
dum_store_remote_get_enabled (DumStoreRemote *store, GError **error)
{
	GError *error_local = NULL;
	gboolean ret;

	g_return_val_if_fail (DUM_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (store->priv->id != NULL, FALSE);

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		ret = dum_store_remote_load (DUM_STORE (store), &error_local);
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
 * dum_store_remote_file_monitor_cb:
 **/
static void
dum_store_remote_file_monitor_cb (DumMonitor *monitor, DumStoreRemote *store)
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
 * dum_store_remote_finalize:
 **/
static void
dum_store_remote_finalize (GObject *object)
{
	DumStoreRemote *store;

	g_return_if_fail (object != NULL);
	g_return_if_fail (DUM_IS_STORE_REMOTE (object));
	store = DUM_STORE_REMOTE (object);

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

	G_OBJECT_CLASS (dum_store_remote_parent_class)->finalize (object);
}

/**
 * dum_store_remote_class_init:
 **/
static void
dum_store_remote_class_init (DumStoreRemoteClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	DumStoreClass *store_class = DUM_STORE_CLASS (klass);
	object_class->finalize = dum_store_remote_finalize;

	/* map */
	store_class->load = dum_store_remote_load;
	store_class->search_name = dum_store_remote_search_name;
//	store_class->search_category = dum_store_remote_search_category;
	store_class->search_details = dum_store_remote_search_details;
	store_class->search_group = dum_store_remote_search_group;
	store_class->search_file = dum_store_remote_search_file;
	store_class->resolve = dum_store_remote_resolve;
	store_class->what_provides = dum_store_remote_what_provides;
	store_class->get_packages = dum_store_remote_get_packages;
	store_class->find_package = dum_store_remote_find_package;
	store_class->get_id = dum_store_remote_get_id;
	store_class->print = dum_store_remote_print;

	g_type_class_add_private (klass, sizeof (DumStoreRemotePrivate));
}

/**
 * dum_store_remote_init:
 **/
static void
dum_store_remote_init (DumStoreRemote *store)
{
	store->priv = DUM_STORE_REMOTE_GET_PRIVATE (store);
	store->priv->loaded = FALSE;
	store->priv->id = NULL;
	store->priv->name = NULL;
	store->priv->name_expanded = NULL;
	store->priv->enabled = FALSE;
	store->priv->filename = NULL;
	store->priv->baseurl = NULL;
	store->priv->mirrorlist = NULL;
	store->priv->metalink = NULL;
	store->priv->config = dum_config_new ();
	store->priv->md_master = dum_repo_md_master_new ();
	store->priv->md_filelists = dum_repo_md_filelists_new ();
	store->priv->md_primary = dum_repo_md_primary_new ();
	store->priv->monitor = dum_monitor_new ();
	g_signal_connect (store->priv->monitor, "changed", G_CALLBACK (dum_store_remote_file_monitor_cb), store);
}

/**
 * dum_store_remote_new:
 * Return value: A new store_remote class instance.
 **/
DumStoreRemote *
dum_store_remote_new (void)
{
	DumStoreRemote *store;
	store = g_object_new (DUM_TYPE_STORE_REMOTE, NULL);
	return DUM_STORE_REMOTE (store);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
dum_store_remote_test (EggTest *test)
{
	DumStoreRemote *store;
	DumConfig *config;
	GPtrArray *array;
	gboolean ret;
	GError *error = NULL;
	const gchar *id;

	if (!egg_test_start (test, "DumStoreRemote"))
		return;

	/* set this up as dummy */
	config = dum_config_new ();
	dum_config_set_filename (config, "../test/etc/yum.conf", NULL);

	/************************************************************/
	egg_test_title (test, "get store");
	store = dum_store_remote_new ();
	egg_test_assert (test, store != NULL);

	/************************************************************/
	egg_test_title (test, "load");
	ret = dum_store_remote_set_from_file (store, "../test/repos/fedora.repo", "fedora", &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "is devel");
	ret = dum_store_remote_is_devel (store, NULL);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "is enabled");
	ret = dum_store_remote_get_enabled (store, NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get id");
	id = dum_store_get_id (DUM_STORE (store));
	if (egg_strequal (id, "fedora"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid id '%s'", id);

	/************************************************************/
	egg_test_title (test, "get name");
	id = dum_store_remote_get_name (store, NULL);
	if (egg_strequal (id, "Fedora 10 - i386"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid name '%s'", id);

	/************************************************************/
	egg_test_title (test, "load metadata");
	ret = dum_store_remote_load (DUM_STORE (store), &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load metadata '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "resolve");
	array = dum_store_remote_resolve (DUM_STORE (store), "kernel", &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to resolve '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "correct number");
	if (array->len == 2)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);

	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);

	/************************************************************/
	egg_test_title (test, "search name");
	array = dum_store_remote_search_name (DUM_STORE (store), "power-manager", &error);
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
	array = dum_store_remote_search_details (DUM_STORE (store), "browser plugin", &error);
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
	array = dum_store_remote_search_file (DUM_STORE (store), "/usr/bin/gnome-power-manager", &error);
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
	ret = dum_store_remote_set_enabled (store, FALSE, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to disable '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "is enabled");
	ret = dum_store_remote_get_enabled (store, NULL);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "set enabled");
	ret = dum_store_remote_set_enabled (store, TRUE, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to enable '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "is enabled");
	ret = dum_store_remote_get_enabled (store, NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get packages");
	array = dum_store_remote_get_packages (DUM_STORE (store), &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get packages '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "correct number");
	if (array->len == 11416)
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

