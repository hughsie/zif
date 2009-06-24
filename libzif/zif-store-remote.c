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
#include <gio/gio.h>

#include "zif-utils.h"
#include "zif-config.h"
#include "zif-package.h"
#include "zif-package-remote.h"
#include "zif-store.h"
#include "zif-store-remote.h"
#include "zif-store-local.h"
#include "zif-repo-md-primary.h"
#include "zif-repo-md-filelists.h"
#include "zif-repo-md-metalink.h"
#include "zif-monitor.h"
#include "zif-download.h"

#include "egg-debug.h"
#include "egg-string.h"

#define ZIF_STORE_REMOTE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_STORE_REMOTE, ZifStoreRemotePrivate))

typedef enum {
	ZIF_STORE_REMOTE_PARSER_SECTION_CHECKSUM,
	ZIF_STORE_REMOTE_PARSER_SECTION_CHECKSUM_OPEN,
	ZIF_STORE_REMOTE_PARSER_SECTION_TIMESTAMP,
	ZIF_STORE_REMOTE_PARSER_SECTION_UNKNOWN
} ZifStoreRemoteParserSection;

typedef enum {
	ZIF_STORE_REMOTE_MD_TYPE_PRIMARY,
	ZIF_STORE_REMOTE_MD_TYPE_FILELISTS,
	ZIF_STORE_REMOTE_MD_TYPE_OTHER,
	ZIF_STORE_REMOTE_MD_TYPE_COMPS,
	ZIF_STORE_REMOTE_MD_TYPE_METALINK,
	ZIF_STORE_REMOTE_MD_TYPE_UNKNOWN
} ZifStoreRemoteMdType;

typedef struct {
	guint		 timestamp;
	gchar		*location;	/* repodata/35d817e-primary.sqlite.bz2 */
	gchar		*checksum;	/* of compressed file */
	gchar		*checksum_open;	/* of uncompressed file */
	GChecksumType	 checksum_type;
} ZifStoreRemoteInfoData;

struct _ZifStoreRemotePrivate
{
	gchar			*id;			/* fedora */
	gchar			*name;			/* Fedora $arch */
	gchar			*name_expanded;		/* Fedora 1386 */
	gchar			*directory;		/* /var/cache/yum/fedora */
	gchar			*repomd_filename;	/* /var/cache/yum/fedora/repomd.xml */
	GPtrArray		*baseurls;		/* http://download.fedora.org/ */
	gchar			*mirrorlist;
	gchar			*metalink;
	gchar			*cache_dir;		/* /var/cache/yum */
	gchar			*repo_filename;		/* /etc/yum.repos.d/fedora.repo */
	gboolean		 enabled;
	gboolean		 loaded;
	gboolean		 loaded_md;
	ZifRepoMd		*md_primary;
	ZifRepoMd		*md_filelists;
	ZifRepoMd		*md_metalink;
	ZifConfig		*config;
	ZifMonitor		*monitor;
	GPtrArray		*packages;
	ZifStoreRemoteInfoData	*data[ZIF_STORE_REMOTE_MD_TYPE_UNKNOWN];
	/* temp data for the xml parser */
	ZifStoreRemoteMdType	 parser_type;
	ZifStoreRemoteMdType	 parser_section;
};

G_DEFINE_TYPE (ZifStoreRemote, zif_store_remote, ZIF_TYPE_STORE)

static gboolean zif_store_remote_load_md (ZifStoreRemote *store, GError **error);

/**
 * zif_store_remote_checksum_type_from_text:
 **/
static GChecksumType
zif_store_remote_checksum_type_from_text (const gchar *type)
{
	if (g_strcmp0 (type, "sha") == 0)
		return G_CHECKSUM_SHA1;
	if (g_strcmp0 (type, "sha1") == 0)
		return G_CHECKSUM_SHA1;
	if (g_strcmp0 (type, "sha256") == 0)
		return G_CHECKSUM_SHA256;
	return G_CHECKSUM_MD5;
}

/**
 * zif_store_remote_md_type_to_text:
 **/
static const gchar *
zif_store_remote_md_type_to_text (ZifStoreRemoteMdType type)
{
	if (type == ZIF_STORE_REMOTE_MD_TYPE_FILELISTS)
		return "filelists";
	if (type == ZIF_STORE_REMOTE_MD_TYPE_PRIMARY)
		return "primary";
	if (type == ZIF_STORE_REMOTE_MD_TYPE_OTHER)
		return "other";
	if (type == ZIF_STORE_REMOTE_MD_TYPE_COMPS)
		return "comps";
	if (type == ZIF_STORE_REMOTE_MD_TYPE_METALINK)
		return "metalink";
	return "unknown";
}

/**
 * zif_store_remote_get_md_from_type:
 **/
static ZifRepoMd *
zif_store_remote_get_md_from_type (ZifStoreRemote *store, ZifStoreRemoteMdType type)
{
	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (type != ZIF_STORE_REMOTE_MD_TYPE_UNKNOWN, NULL);

	if (type == ZIF_STORE_REMOTE_MD_TYPE_FILELISTS)
		return store->priv->md_filelists;
	if (type == ZIF_STORE_REMOTE_MD_TYPE_PRIMARY)
		return store->priv->md_primary;
	if (type == ZIF_STORE_REMOTE_MD_TYPE_OTHER)
		return NULL;
	if (type == ZIF_STORE_REMOTE_MD_TYPE_COMPS)
		return NULL;
	if (type == ZIF_STORE_REMOTE_MD_TYPE_METALINK)
		return store->priv->md_metalink;
	return NULL;
}

/**
 * zif_store_remote_parser_start_element:
 **/
static void
zif_store_remote_parser_start_element (GMarkupParseContext *context, const gchar *element_name,
				       const gchar **attribute_names, const gchar **attribute_values,
				       gpointer user_data, GError **error)
{
	guint i;
	ZifStoreRemote *store = user_data;
	ZifStoreRemoteMdType parser_type = store->priv->parser_type;

	/* data */
	if (g_strcmp0 (element_name, "data") == 0) {

		/* reset */
		store->priv->parser_type = ZIF_STORE_REMOTE_MD_TYPE_UNKNOWN;

		/* find type */
		for (i=0; attribute_names[i] != NULL; i++) {
			if (g_strcmp0 (attribute_names[i], "type") == 0) {
				if (g_strcmp0 (attribute_values[i], "primary_db") == 0)
					store->priv->parser_type = ZIF_STORE_REMOTE_MD_TYPE_PRIMARY;
				else if (g_strcmp0 (attribute_values[i], "filelists_db") == 0)
					store->priv->parser_type = ZIF_STORE_REMOTE_MD_TYPE_FILELISTS;
				else if (g_strcmp0 (attribute_values[i], "other_db") == 0)
					store->priv->parser_type = ZIF_STORE_REMOTE_MD_TYPE_OTHER;
				else if (g_strcmp0 (attribute_values[i], "group_gz") == 0)
					store->priv->parser_type = ZIF_STORE_REMOTE_MD_TYPE_COMPS;
				break;
			}
		}
		store->priv->parser_section = ZIF_STORE_REMOTE_PARSER_SECTION_UNKNOWN;
		goto out;
	}

	/* not a section we recognise */
	if (store->priv->parser_type == ZIF_STORE_REMOTE_MD_TYPE_UNKNOWN)
		goto out;

	/* location */
	if (g_strcmp0 (element_name, "location") == 0) {
		for (i=0; attribute_names[i] != NULL; i++) {
			if (g_strcmp0 (attribute_names[i], "href") == 0) {
				store->priv->data[parser_type]->location = g_strdup (attribute_values[i]);
				break;
			}
		}
		store->priv->parser_section = ZIF_STORE_REMOTE_PARSER_SECTION_UNKNOWN;
		goto out;
	}

	/* checksum */
	if (g_strcmp0 (element_name, "checksum") == 0) {
		for (i=0; attribute_names[i] != NULL; i++) {
			if (g_strcmp0 (attribute_names[i], "type") == 0) {
				store->priv->data[parser_type]->checksum_type = zif_store_remote_checksum_type_from_text (attribute_values[i]);
				break;
			}
		}
		store->priv->parser_section = ZIF_STORE_REMOTE_PARSER_SECTION_CHECKSUM;
		goto out;
	}

	/* checksum */
	if (g_strcmp0 (element_name, "open-checksum") == 0) {
		store->priv->parser_section = ZIF_STORE_REMOTE_PARSER_SECTION_CHECKSUM_OPEN;
		goto out;
	}

	/* timestamp */
	if (g_strcmp0 (element_name, "timestamp") == 0) {
		store->priv->parser_section = ZIF_STORE_REMOTE_PARSER_SECTION_TIMESTAMP;
		goto out;
	}
out:
	return;
}

/**
 * zif_store_remote_parser_end_element:
 **/
static void
zif_store_remote_parser_end_element (GMarkupParseContext *context, const gchar *element_name,
				     gpointer user_data, GError **error)
{
	ZifStoreRemote *store = user_data;

	/* reset */
	store->priv->parser_section = ZIF_STORE_REMOTE_PARSER_SECTION_UNKNOWN;
	if (g_strcmp0 (element_name, "data") == 0)
		store->priv->parser_type = ZIF_STORE_REMOTE_MD_TYPE_UNKNOWN;
}


/**
 * zif_store_remote_parser_text:
 **/
static void
zif_store_remote_parser_text (GMarkupParseContext *context, const gchar *text, gsize text_len,
			      gpointer user_data, GError **error)

{
	ZifStoreRemote *store = user_data;
	ZifStoreRemoteMdType parser_type = store->priv->parser_type;

	if (parser_type == ZIF_STORE_REMOTE_MD_TYPE_UNKNOWN)
		return;

	if (store->priv->parser_section == ZIF_STORE_REMOTE_PARSER_SECTION_CHECKSUM)
		store->priv->data[parser_type]->checksum = g_strdup (text);
	else if (store->priv->parser_section == ZIF_STORE_REMOTE_PARSER_SECTION_CHECKSUM_OPEN)
		store->priv->data[parser_type]->checksum_open = g_strdup (text);
	else if (store->priv->parser_section == ZIF_STORE_REMOTE_PARSER_SECTION_TIMESTAMP)
		store->priv->data[parser_type]->timestamp = atol (text);
}

/**
 * zif_store_remote_expand_vars:
 **/
static gchar *
zif_store_remote_expand_vars (const gchar *name)
{
	gchar *name1;
	gchar *name2;

	name1 = egg_strreplace (name, "$releasever", "11");
	name2 = egg_strreplace (name1, "$basearch", "i386");

	g_free (name1);
	return name2;
}

/**
 * zif_store_remote_download_try:
 **/
static gboolean
zif_store_remote_download_try (ZifStoreRemote *store, const gchar *uri, const gchar *filename, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	ZifDownload *download = NULL;

	/* download object */
	download = zif_download_new ();
	ret = zif_download_file (download, uri, filename, cancellable, completion, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to download %s from %s: %s", filename, uri, error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	g_object_unref (download);
	return ret;
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
	guint i;
	guint len;
	gboolean ret = FALSE;
	gchar *uri = NULL;
	GError *error_local = NULL;
	gchar *filename_local = NULL;
	gchar *basename = NULL;
	const gchar *baseurl;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (store->priv->id != NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (directory != NULL, FALSE);

	/* if not already loaded, load */
	if (!store->priv->loaded_md) {
		ret = zif_store_remote_load_md (store, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load metadata: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* we need at least one baseurl */
	if (store->priv->baseurls->len == 0) {
		if (error != NULL)
			*error = g_error_new (1, 0, "no baseurls for %s", store->priv->id);
		goto out;
	}

	/* get the location to download to */
	basename = g_path_get_basename (filename);
	filename_local = g_build_filename (directory, basename, NULL);

	/* try to use all uris */
	len = store->priv->baseurls->len;
	for (i=0; i<len; i++) {

		/* build url */
		baseurl = g_ptr_array_index (store->priv->baseurls, 0);
		uri = g_build_filename (baseurl, "repodata", basename, NULL);

		/* try download */
		ret = zif_store_remote_download_try (store, uri, filename_local, cancellable, completion, &error_local);

		/* free */
		g_free (uri);

		/* succeeded, otherwise retry with new mirrors */
		if (ret)
			break;

		/* free error */
		egg_debug ("%s", error_local->message);
		g_error_free (error_local);
	}

	/* nothing */
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to download from any sources");
		goto out;
	}
out:
	g_free (basename);
	g_free (filename_local);
	return ret;
}

/**
 * zif_store_remote_add_metalink:
 **/
static gboolean
zif_store_remote_add_metalink (ZifStoreRemote *store, GError **error)
{
	guint i;
	GPtrArray *array;
	GError *error_local = NULL;
	const gchar *uri;

	/* get mirrors */
	array = zif_repo_md_metalink_get_mirrors (ZIF_REPO_MD_METALINK (store->priv->md_metalink), 50, &error_local);
	if (array == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to add mirrors: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* add array */
	for (i=0; i<array->len; i++) {
		uri = g_ptr_array_index (array, i);
		g_ptr_array_add (store->priv->baseurls, g_strdup (uri));
	}

	/* free */
	g_ptr_array_foreach (array, (GFunc) g_free, NULL);
	g_ptr_array_free (array, TRUE);
out:
	return (array != NULL);
}

/**
 * zif_store_remote_add_mirrorlist:
 **/
static gboolean
zif_store_remote_add_mirrorlist (ZifStoreRemote *store, GError **error)
{
	gboolean ret;
	gchar *filename;
	gchar *contents = NULL;
	gchar **lines = NULL;
	GError *error_local = NULL;
	guint i;

	/* get contents */
	filename = g_build_filename (store->priv->directory, "mirrorlist.txt", NULL);
	ret = g_file_get_contents (filename, &contents, NULL, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get contents %s: %s", filename, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* split, and add mirrorlists */
	lines = g_strsplit (contents, "\n", -1);
	for (i=0; lines[i] != NULL; i++) {
		if (lines[i][0] == '\0' ||
		    lines[i][0] == '#')
			continue;
		g_ptr_array_add (store->priv->baseurls, g_strdup (lines[i]));
	}
out:
	g_strfreev (lines);
	g_free (filename);
	g_free (contents);
	return ret;
}

/**
 * zif_store_remote_load_md:
 **/
static gboolean
zif_store_remote_load_md (ZifStoreRemote *store, GError **error)
{
	guint i;
	ZifStoreRemoteInfoData **data;
	gboolean ret = TRUE;
	gchar *contents = NULL;
	gchar *basename;
	gchar *filename;
	gsize size;
	GError *error_local = NULL;
	ZifRepoMd *md;
	GMarkupParseContext *context = NULL;
	const GMarkupParser gpk_store_remote_markup_parser = {
		zif_store_remote_parser_start_element,
		zif_store_remote_parser_end_element,
		zif_store_remote_parser_text,
		NULL, /* passthrough */
		NULL /* error */
	};

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);

	/* already loaded */
	if (store->priv->loaded_md)
		goto out;

	/* get repo contents */
	ret = g_file_get_contents (store->priv->repomd_filename, &contents, &size, error);
	if (!ret)
		goto out;

	/* create parser */
	context = g_markup_parse_context_new (&gpk_store_remote_markup_parser, G_MARKUP_PREFIX_ERROR_POSITION, store, NULL);

	/* parse data */
	ret = g_markup_parse_context_parse (context, contents, (gssize) size, error);
	if (!ret)
		goto out;

	/* check we've got the needed data */
	data = store->priv->data;
	for (i=0; i<ZIF_STORE_REMOTE_MD_TYPE_UNKNOWN; i++) {
		if (data[i]->location != NULL && (data[i]->checksum == NULL || data[i]->timestamp == 0)) {
			if (error != NULL)
				*error = g_error_new (1, 0, "cannot load md for %s (loc=%s, sum=%s, sum_open=%s, ts=%i)",
						      zif_store_remote_md_type_to_text (i), data[i]->location, data[i]->checksum, data[i]->checksum_open, data[i]->timestamp);
			ret = FALSE;
			goto out;
		}
	}

	/* metalink is specified in the repo file */
	if (store->priv->metalink != NULL)
		store->priv->data[ZIF_STORE_REMOTE_MD_TYPE_METALINK]->location = g_strdup (store->priv->metalink);

	/* set MD id and filename for each repo type */
	for (i=0; i<ZIF_STORE_REMOTE_MD_TYPE_UNKNOWN; i++) {
		md = zif_store_remote_get_md_from_type (store, i);
		if (md == NULL) {
			egg_warning ("failed to get local store for %s", zif_store_remote_md_type_to_text (i));
			continue;
		}

		/* location not set */
		if (store->priv->data[i]->location == NULL) {
			egg_warning ("no location set for %s", zif_store_remote_md_type_to_text (i));
			continue;
		}

		/* set MD id and filename */
		basename = g_path_get_basename (store->priv->data[i]->location);
		filename = g_build_filename (store->priv->directory, basename, NULL);
		zif_repo_md_set_id (md, store->priv->id);
		zif_repo_md_set_filename (md, filename);
		g_free (basename);
		g_free (filename);
	}

	/* extract details from mirrorlist */
	if (store->priv->mirrorlist != NULL) {
		ret = zif_store_remote_add_mirrorlist (store, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to add mirrorlist: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* extract details from metalink */
	if (store->priv->metalink != NULL) {
		ret = zif_store_remote_add_metalink (store, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to add metalink: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* all okay */
	store->priv->loaded_md = TRUE;

#if 0
	/* check all metadata */
	ret = zif_store_remote_check (store, NULL, NULL, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to check during load: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}
#endif

out:
	if (context != NULL)
		g_markup_parse_context_free (context);
	g_free (contents);
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
	ZifCompletion *completion_local;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifRepoMd *md;
	guint i;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	completion_local = zif_completion_new ();

	/* setup completion with the correct number of steps */
	if (completion != NULL) {
		zif_completion_set_number_steps (completion, (ZIF_STORE_REMOTE_MD_TYPE_UNKNOWN * 2) + 2);
		zif_completion_set_child (completion, completion_local);
	}

	/* download new file */
	ret = zif_store_remote_download (remote, remote->priv->repomd_filename, remote->priv->directory, cancellable, completion_local, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to download repomd: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	if (completion != NULL)
		zif_completion_done (completion);

	/* reload */
	ret = zif_store_remote_load_md (remote, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to load updated metadata: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	if (completion != NULL)
		zif_completion_done (completion);

	/* refresh each repo type */
	for (i=0; i<ZIF_STORE_REMOTE_MD_TYPE_UNKNOWN; i++) {
		md = zif_store_remote_get_md_from_type (remote, i);
		if (md == NULL) {
			egg_warning ("failed to get local store for %s", zif_store_remote_md_type_to_text (i));
			continue;
		}

		/* download new file */
		filename = zif_repo_md_get_filename (md);
		ret = zif_store_remote_download (remote, filename, remote->priv->directory, cancellable, completion_local, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to refresh %s: %s", zif_store_remote_md_type_to_text (i), error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		if (completion != NULL)
			zif_completion_done (completion);

		/* decompress */
		ret = zif_file_untar (filename, remote->priv->directory, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to decompress %s for %s: %s", filename, zif_store_remote_md_type_to_text (i), error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		if (completion != NULL)
			zif_completion_done (completion);
	}

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
	gchar *temp;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);
	g_return_val_if_fail (remote->priv->repo_filename != NULL, FALSE);

	/* already loaded */
	if (remote->priv->loaded)
		goto out;

	/* setup completion with the correct number of steps */
	if (completion != NULL)
		zif_completion_set_number_steps (completion, 3);

	file = g_key_file_new ();
	ret = g_key_file_load_from_file (file, remote->priv->repo_filename, G_KEY_FILE_NONE, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to load %s: %s", remote->priv->repo_filename, error_local->message);
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
		g_ptr_array_add (remote->priv->baseurls, zif_store_remote_expand_vars (temp));
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
	if (remote->priv->enabled && remote->priv->baseurls->len == 0 && remote->priv->metalink == NULL && remote->priv->mirrorlist == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "baseurl, metalink or mirrorlist required");
		ret = FALSE;
		goto out;
	}

	/* okay */
	remote->priv->loaded = TRUE;

	/* this section done */
	if (completion != NULL) {
		zif_completion_done (completion);
		zif_completion_set_percentage (completion, 100);
	}
out:
	g_free (enabled);
	if (file != NULL)
		g_key_file_free (file);
	return ret;
}

/**
 * zif_store_remote_check_type:
 **/
static gboolean
zif_store_remote_check_type (ZifStoreRemote *store, ZifStoreRemoteMdType type, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	const gchar *filename = NULL;
	gchar *data = NULL;
	gchar *checksum = NULL;
	const gchar *checksum_wanted = NULL;
	gsize length;
	ZifRepoMd *md;

	/* get MD */
	md = zif_store_remote_get_md_from_type (store, type);
	if (md == NULL) {
		egg_warning ("failed to get local store");
		goto out;
	}

	/* get correct filename */
	filename = zif_repo_md_get_filename (md);
	if (filename == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get filename");
		goto out;
	}

	/* get contents */
	ret = g_file_get_contents (filename, &data, &length, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get contents: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* compute checksum */
	checksum = g_compute_checksum_for_data (store->priv->data[type]->checksum_type, (guchar*) data, length);

	/* get the one we want */
	if (type == ZIF_STORE_REMOTE_MD_TYPE_COMPS)
		checksum_wanted = store->priv->data[type]->checksum;
	else
		checksum_wanted = store->priv->data[type]->checksum_open;

	/* matches? */
	ret = (g_strcmp0 (checksum, checksum_wanted) == 0);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "checksum incorrect, wanted %s, got %s", checksum_wanted, checksum);
	}
out:
	g_free (data);
	g_free (checksum);
	return ret;
}

/**
 * zif_store_remote_check:
 **/
gboolean
zif_store_remote_check (ZifStoreRemote *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	guint i;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (store->priv->id != NULL, FALSE);

	/* setup completion with the correct number of steps */
	if (completion != NULL)
		zif_completion_set_number_steps (completion, ZIF_STORE_REMOTE_MD_TYPE_UNKNOWN);

	/* if not already loaded, load */
	if (!store->priv->loaded_md) {
		ret = zif_store_remote_load_md (store, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load metadata: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* refresh each repo type */
	for (i=0; i<ZIF_STORE_REMOTE_MD_TYPE_UNKNOWN; i++) {
		ret = zif_store_remote_check_type (store, i, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to check type %s: %s", zif_store_remote_md_type_to_text (i), error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		if (completion != NULL)
			zif_completion_done (completion);
	}
out:
	return ret;
}

/**
 * zif_store_remote_clean:
 **/
static gboolean
zif_store_remote_clean (ZifStore *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = FALSE;
	gboolean exists;
	GError *error_local = NULL;
	GFile *file;
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
	exists = g_file_test (remote->priv->repomd_filename, G_FILE_TEST_EXISTS);
	if (exists) {
		file = g_file_new_for_path (remote->priv->repomd_filename);
		ret = g_file_delete (file, NULL, &error_local);
		g_object_unref (file);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to delete metadata file %s: %s",
						      remote->priv->repomd_filename, error_local->message);
			g_error_free (error_local);
			goto out;
		}
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
zif_store_remote_set_from_file (ZifStoreRemote *store, const gchar *repo_filename, const gchar *id, GError **error)
{
	GError *error_local = NULL;
	gboolean ret = TRUE;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (repo_filename != NULL, FALSE);
	g_return_val_if_fail (id != NULL, FALSE);
	g_return_val_if_fail (store->priv->id == NULL, FALSE);
	g_return_val_if_fail (!store->priv->loaded, FALSE);

	/* save */
	egg_debug ("setting store %s", id);
	store->priv->id = g_strdup (id);
	store->priv->repo_filename = g_strdup (repo_filename);
	store->priv->directory = g_build_filename (store->priv->cache_dir, store->priv->id, NULL);

	/* repomd location */
	store->priv->repomd_filename = g_build_filename (store->priv->cache_dir, store->priv->id, "repomd.xml", NULL);

	/* setup watch */
	ret = zif_monitor_add_watch (store->priv->monitor, repo_filename, &error_local);
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
	ret = g_key_file_load_from_file (file, store->priv->repo_filename, G_KEY_FILE_KEEP_COMMENTS, &error_local);
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
	ret = g_file_set_contents (store->priv->repo_filename, data, -1, &error_local);
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
}

/**
 * zif_store_remote_resolve:
 **/
static GPtrArray *
zif_store_remote_resolve (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	/* load metadata */
	if (!remote->priv->loaded_md) {
		ret = zif_store_remote_load_md (remote, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load xml: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	array = zif_repo_md_primary_resolve (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), search, error);
out:
	return array;
}

/**
 * zif_store_remote_search_name:
 **/
static GPtrArray *
zif_store_remote_search_name (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	/* load metadata */
	if (!remote->priv->loaded_md) {
		ret = zif_store_remote_load_md (remote, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load xml: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	array = zif_repo_md_primary_search_name (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), search, error);
out:
	return array;
}

/**
 * zif_store_remote_search_details:
 **/
static GPtrArray *
zif_store_remote_search_details (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	/* load metadata */
	if (!remote->priv->loaded_md) {
		ret = zif_store_remote_load_md (remote, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load xml: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	array = zif_repo_md_primary_search_details (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), search, error);
out:
	return array;
}

/**
 * zif_store_remote_search_group:
 **/
static GPtrArray *
zif_store_remote_search_group (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	/* load metadata */
	if (!remote->priv->loaded_md) {
		ret = zif_store_remote_load_md (remote, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load xml: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	array = zif_repo_md_primary_search_group (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), search, error);
out:
	return array;
}

/**
 * zif_store_remote_find_package:
 **/
static ZifPackage *
zif_store_remote_find_package (ZifStore *store, const PkPackageId *id, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	GPtrArray *array = NULL;
	ZifPackage *package = NULL;
	GError *error_local = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	/* load metadata */
	if (!remote->priv->loaded_md) {
		ret = zif_store_remote_load_md (remote, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load xml: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

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
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	/* load metadata */
	if (!remote->priv->loaded_md) {
		ret = zif_store_remote_load_md (remote, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load xml: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	array = zif_repo_md_primary_get_packages (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), error);
out:
	return array;
}


/**
 * zif_store_remote_get_updates:
 **/
static GPtrArray *
zif_store_remote_get_updates (ZifStore *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
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

	/* load metadata */
	if (!remote->priv->loaded_md) {
		ret = zif_store_remote_load_md (remote, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load xml: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

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
	gboolean ret;
	GError *error_local = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	/* load metadata */
	if (!remote->priv->loaded_md) {
		ret = zif_store_remote_load_md (remote, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load xml: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}
	//FIXME: load other MD
out:
	return g_ptr_array_new ();
}

/**
 * zif_store_remote_search_file:
 **/
static GPtrArray *
zif_store_remote_search_file (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *pkgids;
	GPtrArray *array = NULL;
	GPtrArray *tmp;
	ZifPackage *package;
	const gchar *pkgid;
	guint i, j;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	/* load metadata */
	if (!remote->priv->loaded_md) {
		ret = zif_store_remote_load_md (remote, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load xml: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

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
	gboolean ret;
	GError *error_local = NULL;

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
	gboolean ret;
	GError *error_local = NULL;

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
	g_free (store->priv->repo_filename);
	g_ptr_array_foreach (store->priv->baseurls, (GFunc) g_free, NULL);
	g_ptr_array_set_size (store->priv->baseurls, 0);
	g_free (store->priv->mirrorlist);
	g_free (store->priv->metalink);

	store->priv->loaded = FALSE;
	store->priv->loaded_md = FALSE;
	store->priv->enabled = FALSE;
	store->priv->id = NULL;
	store->priv->name = NULL;
	store->priv->name_expanded = NULL;
	store->priv->repo_filename = NULL;
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
	ZifStoreRemoteInfoData **data;
	guint i;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_STORE_REMOTE (object));
	store = ZIF_STORE_REMOTE (object);

	g_free (store->priv->id);
	g_free (store->priv->name);
	g_free (store->priv->name_expanded);
	g_free (store->priv->repo_filename);
	g_free (store->priv->mirrorlist);
	g_free (store->priv->metalink);
	g_free (store->priv->cache_dir);
	g_free (store->priv->repomd_filename);
	g_free (store->priv->directory);

	g_object_unref (store->priv->md_primary);
	g_object_unref (store->priv->md_filelists);
	g_object_unref (store->priv->md_metalink);
	g_object_unref (store->priv->config);
	g_object_unref (store->priv->monitor);

	g_ptr_array_foreach (store->priv->baseurls, (GFunc) g_free, NULL);
	g_ptr_array_free (store->priv->baseurls, TRUE);

	data = store->priv->data;
	for (i=0; i<ZIF_STORE_REMOTE_MD_TYPE_UNKNOWN; i++) {
		g_free (data[i]->location);
		g_free (data[i]->checksum);
		g_free (data[i]->checksum_open);
		g_free (data[i]);
	}

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
	guint i;
	GError *error = NULL;
	ZifStoreRemoteInfoData **data;

	store->priv = ZIF_STORE_REMOTE_GET_PRIVATE (store);
	store->priv->loaded = FALSE;
	store->priv->loaded_md = FALSE;
	store->priv->id = NULL;
	store->priv->name = NULL;
	store->priv->directory = NULL;
	store->priv->name_expanded = NULL;
	store->priv->enabled = FALSE;
	store->priv->repo_filename = NULL;
	store->priv->baseurls = g_ptr_array_new ();
	store->priv->mirrorlist = NULL;
	store->priv->metalink = NULL;
	store->priv->config = zif_config_new ();
	store->priv->monitor = zif_monitor_new ();
	store->priv->md_filelists = ZIF_REPO_MD (zif_repo_md_filelists_new ());
	store->priv->md_primary = ZIF_REPO_MD (zif_repo_md_primary_new ());
	store->priv->md_metalink = ZIF_REPO_MD (zif_repo_md_metalink_new ());
	store->priv->parser_type = ZIF_STORE_REMOTE_MD_TYPE_UNKNOWN;
	store->priv->parser_section = ZIF_STORE_REMOTE_PARSER_SECTION_UNKNOWN;
	g_signal_connect (store->priv->monitor, "changed", G_CALLBACK (zif_store_remote_file_monitor_cb), store);

	data = store->priv->data;
	for (i=0; i<ZIF_STORE_REMOTE_MD_TYPE_UNKNOWN; i++) {
		data[i] = g_new0 (ZifStoreRemoteInfoData, 1);
		data[i]->location = NULL;
		data[i]->checksum = NULL;
		data[i]->checksum_open = NULL;
		data[i]->timestamp = 0;
	}

	/* name */
	store->priv->cache_dir = zif_config_get_string (store->priv->config, "cachedir", &error);
	if (store->priv->cache_dir == NULL) {
		egg_warning ("failed to get cachedir: %s", error->message);
		g_error_free (error);
	}
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
	array = zif_store_remote_get_updates (ZIF_STORE (store), NULL, NULL, &error);
	if (array == NULL)
		egg_test_failed (test, "no data: %s", error->message);
	else if (array->len > 0)
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
	if (egg_strequal (id, "Fedora 11 - i386"))
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

