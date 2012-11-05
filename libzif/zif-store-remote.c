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
 * SECTION:zif-store-remote
 * @short_description: Store for remote packages
 *
 * A #ZifStoreRemote is a subclassed #ZifStore and operates on remote objects.
 * A repository is another name for a #ZifStoreRemote.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <gio/gio.h>

#include "zif-category.h"
#include "zif-config.h"
#include "zif-download-private.h"
#include "zif-groups.h"
#include "zif-lock.h"
#include "zif-md-comps.h"
#include "zif-md-delta.h"
#include "zif-md-filelists-sql.h"
#include "zif-md-filelists-xml.h"
#include "zif-md-metalink.h"
#include "zif-md-mirrorlist.h"
#include "zif-md-other-sql.h"
#include "zif-md-primary-sql.h"
#include "zif-md-primary-xml.h"
#include "zif-md-updateinfo.h"
#include "zif-media.h"
#include "zif-monitor.h"
#include "zif-object-array.h"
#include "zif-package-array.h"
#include "zif-package.h"
#include "zif-package-remote.h"
#include "zif-state-private.h"
#include "zif-store.h"
#include "zif-store-local.h"
#include "zif-store-remote-private.h"
#include "zif-update-private.h"
#include "zif-utils-private.h"

#define ZIF_STORE_REMOTE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_STORE_REMOTE, ZifStoreRemotePrivate))

typedef enum {
	ZIF_STORE_REMOTE_PARSER_SECTION_CHECKSUM,
	ZIF_STORE_REMOTE_PARSER_SECTION_CHECKSUM_UNCOMPRESSED,
	ZIF_STORE_REMOTE_PARSER_SECTION_TIMESTAMP,
	ZIF_STORE_REMOTE_PARSER_SECTION_UNKNOWN
} ZifStoreRemoteParserSection;

struct _ZifStoreRemotePrivate
{
	gchar			*id;			/* fedora */
	gchar			*name;			/* Fedora $arch */
	gchar			*name_expanded;		/* Fedora i386 */
	gchar			*directory;		/* /var/cache/yum/fedora */
	gchar			*repomd_filename;	/* /var/cache/yum/fedora/repomd.xml */
	gchar			**baseurl;		/* http://dl.fp.org/pub/fedora/16/i386 */
	gchar			*mirrorlist;		/* http://dl.fp.org/mirrorlist.txt */
	gchar			*metalink;		/* http://m.fp.org/ml?repo=f-15&arch=i386 */
	gchar			*cache_dir;		/* /var/cache/yum */
	gchar			*repo_filename;		/* /etc/yum.repos.d/fedora.repo */
	gchar			*media_id;		/* 1273587559.563492 */
	gchar			*pubkey;		/* file:///etc/pki/rpm-gpg/RPM-GPG-KEY */
	guint			 metadata_expire;	/* in seconds */
	guint			 download_retries;
	gboolean		 enabled;
	gboolean		 loaded;
	gboolean		 loaded_metadata;
	ZifMd			*md_other_sql;
	ZifMd			*md_primary_sql;
	ZifMd			*md_primary_xml;
	ZifMd			*md_filelists_sql;
	ZifMd			*md_filelists_xml;
	ZifMd			*md_metalink;
	ZifMd			*md_mirrorlist;
	ZifMd			*md_comps;
	ZifMd			*md_updateinfo;
	ZifMd			*md_delta;
	ZifConfig		*config;
	ZifDownload		*download;
	ZifMonitor		*monitor;
	ZifLock			*lock;
	ZifMedia		*media;
	ZifGroups		*groups;
	GPtrArray		*packages;
	ZifMdKind		 parser_type;
	/* temp data for the xml parser */
	ZifStoreRemoteParserSection parser_section;
	GKeyFile		*file;
};

G_DEFINE_TYPE (ZifStoreRemote, zif_store_remote, ZIF_TYPE_STORE)

static gboolean zif_store_remote_load_metadata (ZifStoreRemote *store, ZifState *state, GError **error);
static gboolean zif_store_remote_load (ZifStore *store, ZifState *state, GError **error);

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
 * zif_store_remote_get_primary:
 **/
static ZifMd *
zif_store_remote_get_primary (ZifStoreRemote *store, GError **error)
{
	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);

	/* are we now disabled */
	if (!zif_store_get_enabled (ZIF_STORE (store))) {
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_NOT_ENABLED,
			     "%s is not enabled after md load",
			     store->priv->id);
		goto out;
	}

	if (zif_md_get_location (store->priv->md_primary_sql) != NULL)
		return store->priv->md_primary_sql;
	if (zif_md_get_location (store->priv->md_primary_xml) != NULL)
		return store->priv->md_primary_xml;

	/* no support */
	g_set_error (error, ZIF_STORE_ERROR,
		     ZIF_STORE_ERROR_FAILED,
		     "remote store %s has no primary",
		     store->priv->id);
out:
	return NULL;
}

/**
 * zif_store_remote_get_filelists:
 **/
static ZifMd *
zif_store_remote_get_filelists (ZifStoreRemote *store, GError **error)
{
	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);

	if (zif_md_get_location (store->priv->md_filelists_sql) != NULL)
		return store->priv->md_filelists_sql;
	if (zif_md_get_location (store->priv->md_filelists_xml) != NULL)
		return store->priv->md_filelists_xml;

	/* no support */
	g_set_error (error,
		     ZIF_STORE_ERROR,
		     ZIF_STORE_ERROR_NO_SUPPORT,
		     "remote store %s has no filelists",
		     store->priv->id);
	return NULL;
}

/**
 * zif_store_remote_get_md_from_type:
 * @store: A #ZifStoreRemote
 * @type: the #ZifMdKind to get
 *
 * Gets the metadata object of a specific type.
 *
 * Return value: (transfer none): A #ZifMd
 *
 * Since: 0.2.1
 **/
ZifMd *
zif_store_remote_get_md_from_type (ZifStoreRemote *store, ZifMdKind type)
{
	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (type != ZIF_MD_KIND_UNKNOWN, NULL);

	if (type == ZIF_MD_KIND_FILELISTS_SQL)
		return store->priv->md_filelists_sql;
	if (type == ZIF_MD_KIND_FILELISTS_XML)
		return store->priv->md_filelists_xml;
	if (type == ZIF_MD_KIND_PRIMARY_SQL)
		return store->priv->md_primary_sql;
	if (type == ZIF_MD_KIND_PRIMARY_XML)
		return store->priv->md_primary_xml;
	if (type == ZIF_MD_KIND_OTHER_SQL)
		return store->priv->md_other_sql;
	if (type == ZIF_MD_KIND_COMPS_GZ)
		return store->priv->md_comps;
	if (type == ZIF_MD_KIND_UPDATEINFO)
		return store->priv->md_updateinfo;
	if (type == ZIF_MD_KIND_METALINK)
		return store->priv->md_metalink;
	if (type == ZIF_MD_KIND_MIRRORLIST)
		return store->priv->md_mirrorlist;
	if (type == ZIF_MD_KIND_PRESTODELTA)
		return store->priv->md_delta;
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
	guint i, j;
	ZifMd *md;
	ZifStoreRemote *store = user_data;
	GString *string;

	/* data */
	if (g_strcmp0 (element_name, "data") == 0) {

		/* reset */
		store->priv->parser_type = ZIF_MD_KIND_UNKNOWN;

		/* find type */
		for (i=0; attribute_names[i] != NULL; i++) {
			if (g_strcmp0 (attribute_names[i], "type") == 0) {
				if (g_strcmp0 (attribute_values[i], "primary") == 0)
					store->priv->parser_type = ZIF_MD_KIND_PRIMARY_XML;
				else if (g_strcmp0 (attribute_values[i], "primary_db") == 0)
					store->priv->parser_type = ZIF_MD_KIND_PRIMARY_SQL;
				else if (g_strcmp0 (attribute_values[i], "filelists") == 0)
					store->priv->parser_type = ZIF_MD_KIND_FILELISTS_XML;
				else if (g_strcmp0 (attribute_values[i], "filelists_db") == 0)
					store->priv->parser_type = ZIF_MD_KIND_FILELISTS_SQL;
				else if (g_strcmp0 (attribute_values[i], "other") == 0)
					store->priv->parser_type = ZIF_MD_KIND_OTHER_XML;
				else if (g_strcmp0 (attribute_values[i], "other_db") == 0)
					store->priv->parser_type = ZIF_MD_KIND_OTHER_SQL;
				else if (g_strcmp0 (attribute_values[i], "group") == 0)
					store->priv->parser_type = ZIF_MD_KIND_COMPS;
				else if (g_strcmp0 (attribute_values[i], "group_gz") == 0)
					store->priv->parser_type = ZIF_MD_KIND_COMPS_GZ;
				else if (g_strcmp0 (attribute_values[i], "prestodelta") == 0)
					store->priv->parser_type = ZIF_MD_KIND_PRESTODELTA;
				else if (g_strcmp0 (attribute_values[i], "updateinfo") == 0)
					store->priv->parser_type = ZIF_MD_KIND_UPDATEINFO;
				else if (g_strcmp0 (attribute_values[i], "pkgtags") == 0)
					store->priv->parser_type = ZIF_MD_KIND_PKGTAGS;
				else {
					/* we ignore anything else, but print an error to the console */
					string = g_string_new ("");
					g_string_append_printf (string, "unhandled data type '%s', expecting ", attribute_values[i]);

					/* list all the types we support */
					for (j=1; j<ZIF_MD_KIND_LAST; j++)
						g_string_append_printf (string, "%s, ", zif_md_kind_to_text (j));

					/* remove triling comma and space */
					g_string_set_size (string, string->len - 2);

					/* return error */
					g_warning ("%s", string->str);
					g_string_free (string, TRUE);
				}
				break;
			}
		}
		store->priv->parser_section = ZIF_STORE_REMOTE_PARSER_SECTION_UNKNOWN;
		goto out;
	}

	/* not a section we recognise */
	if (store->priv->parser_type == ZIF_MD_KIND_UNKNOWN)
		goto out;

	/* get MetaData object */
	md = zif_store_remote_get_md_from_type (store, store->priv->parser_type);
	if (md == NULL)
		goto out;

	/* location */
	if (g_strcmp0 (element_name, "location") == 0) {
		for (i=0; attribute_names[i] != NULL; i++) {
			if (g_strcmp0 (attribute_names[i], "href") == 0) {
				zif_md_set_location (md, attribute_values[i]);
				continue;
			}
			if (g_strcmp0 (attribute_names[i], "xml:base") == 0) {
				/* I know this seems insane, but sometimes
				 * the base repo isn't where the repo file
				 * is so we have to be sure and add the
				 * xml:base as well. A spec might be nice,
				 * but alas. */
				zif_download_location_add_uri (store->priv->download,
							       attribute_values[i],
							       NULL);
				continue;
			}
		}
		store->priv->parser_section = ZIF_STORE_REMOTE_PARSER_SECTION_UNKNOWN;
		goto out;
	}

	/* checksum */
	if (g_strcmp0 (element_name, "checksum") == 0) {
		for (i=0; attribute_names[i] != NULL; i++) {
			if (g_strcmp0 (attribute_names[i], "type") == 0) {
				zif_md_set_checksum_type (md, zif_store_remote_checksum_type_from_text (attribute_values[i]));
				break;
			}
		}
		store->priv->parser_section = ZIF_STORE_REMOTE_PARSER_SECTION_CHECKSUM;
		goto out;
	}

	/* checksum */
	if (g_strcmp0 (element_name, "open-checksum") == 0) {
		store->priv->parser_section = ZIF_STORE_REMOTE_PARSER_SECTION_CHECKSUM_UNCOMPRESSED;
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
		store->priv->parser_type = ZIF_MD_KIND_UNKNOWN;
}

/**
 * zif_store_remote_parser_text:
 **/
static void
zif_store_remote_parser_text (GMarkupParseContext *context, const gchar *text, gsize text_len,
			      gpointer user_data, GError **error)

{
	ZifMd *md;
	ZifStoreRemote *store = user_data;

	if (store->priv->parser_type == ZIF_MD_KIND_UNKNOWN)
		return;

	/* get MetaData object */
	md = zif_store_remote_get_md_from_type (store, store->priv->parser_type);
	if (md == NULL)
		return;

	if (store->priv->parser_section == ZIF_STORE_REMOTE_PARSER_SECTION_CHECKSUM)
		zif_md_set_checksum (md, text);
	else if (store->priv->parser_section == ZIF_STORE_REMOTE_PARSER_SECTION_CHECKSUM_UNCOMPRESSED)
		zif_md_set_checksum_uncompressed (md, text);
	else if (store->priv->parser_section == ZIF_STORE_REMOTE_PARSER_SECTION_TIMESTAMP)
		zif_md_set_timestamp (md, atol (text));
}

/**
 * zif_store_remote_find_delta:
 * @store: A #ZifStoreRemote
 * @update: New package to update to
 * @installed: Package that is currently installed
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find a update delta between to packages. This might allow Zif to download
 * much less data, at the expense of the amount of CPU taken during the
 * update when the delta package is rebuilt.
 *
 * Return value: (transfer full): A delta object or %NULL. Free with g_object_unref() when done.
 *
 * Since: 0.1.3
 **/
ZifDelta *
zif_store_remote_find_delta (ZifStoreRemote *store,
			     ZifPackage *update,
			     ZifPackage *installed,
			     ZifState *state,
			     GError **error)
{
	ZifDelta *delta = NULL;

	/* nothing */
	if (store->priv->md_delta == NULL) {
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_NO_SUPPORT,
			     "repo %s does not have prestodata",
			     zif_store_get_id (ZIF_STORE (store)));
		goto out;
	}

	/* get delta if it exists */
	delta = zif_md_delta_search_for_package (ZIF_MD_DELTA (store->priv->md_delta),
						 zif_package_get_id (update),
						 zif_package_get_id (installed),
						 state,
						 error);
out:
	return delta;
}

/**
 * zif_store_remote_download_full:
 * @store: A #ZifStoreRemote
 * @filename: Filename to download, e.g. "Packages/hal-0.1.0.rpm"
 * @directory: Directory to put the downloaded file, e.g. "/var/cache/zif"
 * @size: Expected size in bytes, or 0
 * @content_types: Comma delimited expected content types of the file, or %NULL
 * @checksum_type: Checksum type, e.g. %G_CHECKSUM_SHA256, or 0
 * @checksum: Expected checksum of the file, or %NULL
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Downloads a remote package to a local directory.
 * NOTE: if @filename is "Packages/hal-0.1.0.rpm" and @directory is "/var/cache/zif"
 * then the downloaded file will "/var/cache/zif/hal-0.1.0.rpm"
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.5
 **/
gboolean
zif_store_remote_download_full (ZifStoreRemote *store,
				const gchar *filename,
				const gchar *directory,
				guint64 size,
				const gchar *content_types,
				GChecksumType checksum_type,
				const gchar *checksum,
				ZifState *state,
				GError **error)
{
	gboolean ret = FALSE;
	GCancellable *cancellable;
	gchar *basename = NULL;
	gchar *filename_local = NULL;
	GError *error_local = NULL;
	ZifState *state_local;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (store->priv->id != NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (directory != NULL, FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* if not online, then this is fatal */
	ret = zif_config_get_boolean (store->priv->config, "network", NULL);
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED_AS_OFFLINE,
			     "failed to download %s as offline", filename);
		goto out;
	}

	/* check this isn't an absolute path */
	if (g_str_has_prefix (filename, "/")) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED_AS_OFFLINE,
			     "filename %s' should not be an absolute path", filename);
		ret = FALSE;
		goto out;
	}

repomd_confirm:

	/* setup state */
	if (store->priv->loaded_metadata) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   5, /* load */
					   95, /* download */
					   -1);
		if (!ret)
			goto out;
	}

	/* if not already loaded, load */
	if (!store->priv->loaded_metadata) {
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_load_metadata (store, state_local, &error_local);
		if (!ret) {
			g_set_error (error, error_local->domain, error_local->code,
				     "failed to load metadata: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* we need at least one baseurl */
	if (zif_download_location_get_size (store->priv->download) == 0) {
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED_TO_DOWNLOAD,
			     "no locations for %s", store->priv->id);
		ret = FALSE;
		goto out;
	}

	/* get the location to download to */
	basename = g_path_get_basename (filename);
	filename_local = g_build_filename (directory, basename, NULL);

	/* ensure path is valid */
	cancellable = zif_state_get_cancellable (state);
	ret = zif_ensure_parent_dir_exists (filename_local,
					    cancellable,
					    error);
	if (!ret)
		goto out;

	/* try to use all uris */
	state_local = zif_state_get_child (state);
	ret = zif_download_location_full (store->priv->download,
					  filename,
					  filename_local,
					  size,
					  content_types,
					  checksum_type,
					  checksum,
					  state_local,
					  &error_local);
	if (!ret) {
		g_debug ("failed to download on attempt %i (non-fatal): %s",
			 store->priv->download_retries, error_local->message);
		g_clear_error (&error_local);
	}

	/* we failed to get the metadata from any source, so try to refresh the repomd.xml */
	if (!ret && store->priv->download_retries > 1) {

		/* we might go backwards */
		ret = zif_state_reset (state);
		if (!ret)
			goto out;

		/* delete invalid repomd */
		store->priv->loaded_metadata = FALSE;
		g_unlink (store->priv->repomd_filename);

		/* retry this a few times */
		store->priv->download_retries--;
		g_debug ("confirming repomd.xml as repodata file does not exist");

		goto repomd_confirm;
	}

	/* nothing */
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED_TO_DOWNLOAD,
			     "failed to download %s from any sources (and after retrying)", filename);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	g_free (basename);
	g_free (filename_local);
	return ret;
}


/**
 * zif_store_remote_download:
 * @store: A #ZifStoreRemote
 * @filename: Filename to download, e.g. "Packages/hal-0.1.0.rpm"
 * @directory: Directory to put the downloaded file, e.g. "/var/cache/zif"
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Downloads a remote package to a local directory.
 * NOTE: if @filename is "Packages/hal-0.1.0.rpm" and @directory is "/var/cache/zif"
 * then the downloaded file will "/var/cache/zif/hal-0.1.0.rpm"
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_store_remote_download (ZifStoreRemote *store,
			   const gchar *filename,
			   const gchar *directory,
			   ZifState *state,
			   GError **error)
{
	g_warning ("You probably want to be using "
		   "zif_store_remote_download_full() instead!");
	return zif_store_remote_download_full (store,
					       filename,
					       directory,
					       0,
					       NULL,
					       G_CHECKSUM_MD5,
					       NULL,
					       state,
					       error);
}

/**
 * zif_store_remote_add_changelog:
 **/
static gboolean
zif_store_remote_add_changelog (ZifStoreRemote *store, ZifUpdate *update,
				ZifPackageRemote *package_remote,
				ZifState *state, GError **error)
{
	const gchar *pkgid;
	const gchar *version;
	gboolean ret = TRUE;
	gchar **split_installed = NULL;
	gchar *to_array[] = { NULL, NULL };
	GError *error_local = NULL;
	GPtrArray *array_installed = NULL;
	GPtrArray *changelog = NULL;
	GPtrArray *existing_changelog;
	guint i;
	ZifChangeset *changeset;
	ZifPackage *package_installed = NULL;
	ZifState *state_local;
	ZifStore *store_local = NULL;

	/* check we've not added this before */
	existing_changelog = zif_update_get_changelog (update);
	if (existing_changelog->len > 0)
		goto out;

	/* setup state */
	ret = zif_state_set_steps (state,
				   error,
				   30, /* get changelog */
				   60, /* resolve */
				   10, /* add changeset */
				   -1);
	if (!ret)
		goto out;

	/* get pkgid */
	pkgid = zif_package_get_pkgid (ZIF_PACKAGE (package_remote));

	/* get changelog and add to ZifUpdate */
	state_local = zif_state_get_child (state);
	changelog = zif_md_get_changelog (ZIF_MD (store->priv->md_other_sql),
					  pkgid, state_local, &error_local);
	if (changelog == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "failed to get changelog: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* get the newest installed package with this name and arch */
	state_local = zif_state_get_child (state);
	store_local = zif_store_local_new ();
	to_array[0] = (gchar *) zif_package_get_name_arch (ZIF_PACKAGE (package_remote));
	array_installed = zif_store_resolve_full (store_local,
						  to_array,
						  ZIF_STORE_RESOLVE_FLAG_USE_NAME_ARCH,
						  state_local,
						  &error_local);
	if (array_installed == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "failed to resolve installed package for update: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* something found, so get newest */
	if (array_installed->len != 0) {
		package_installed = zif_package_array_get_newest (array_installed,
								  &error_local);
		if (package_installed == NULL) {
			ret = FALSE;
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "failed to get newest for %s: %s",
				     zif_package_get_printable (ZIF_PACKAGE (package_remote)),
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
		split_installed = zif_package_id_split (zif_package_get_id (package_installed));
	}

	/* add the changesets (the changelog) to the update */
	for (i=0; i<changelog->len; i++) {
		changeset = g_ptr_array_index (changelog, i);
		zif_update_add_changeset (update, changeset);

		/* abort when the changeset is older than what we have installed */
		if (split_installed != NULL) {
			version = zif_changeset_get_version (changeset);
			if (version != NULL &&
			    zif_compare_evr (split_installed[ZIF_PACKAGE_ID_VERSION],
					     version) >= 0)
				break;
		}
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	g_strfreev (split_installed);
	if (package_installed != NULL)
		g_object_unref (package_installed);
	if (array_installed != NULL)
		g_ptr_array_unref (array_installed);
	if (store_local != NULL)
		g_object_unref (store_local);
	if (changelog != NULL)
		g_ptr_array_unref (changelog);
	g_ptr_array_unref (existing_changelog);
	return ret;
}

/**
 * zif_store_remote_get_update_detail:
 * @store: A #ZifStoreRemote
 * @package_id: The package_id of the package to find
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Gets the update detail for a package.
 *
 * Return value: (transfer full): A %ZifUpdate, or %NULL for failure
 *
 * Since: 0.1.0
 **/
ZifUpdate *
zif_store_remote_get_update_detail (ZifStoreRemote *store, const gchar *package_id,
				    ZifState *state, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *packages = NULL;
	ZifMd *md;
	ZifPackageRemote *package_remote;
	ZifState *state_local;
	ZifUpdate *update_tmp = NULL;
	ZifUpdate *update = NULL;

	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* setup state */
	if (store->priv->loaded_metadata) {
		ret = zif_state_set_steps (state,
					   error,
					   30, /* get detail for package */
					   20, /* find package */
					   50, /* add changeset */
					   -1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   50, /* load metadata */
					   10, /* get detail for package */
					   10, /* find package */
					   30, /* add changeset */
					   -1);
	}
	if (!ret)
		goto out;

	/* if not already loaded, load */
	if (!store->priv->loaded_metadata) {
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_load_metadata (store, state_local, &error_local);
		if (!ret) {
			g_set_error (error, error_local->domain, error_local->code,
				     "failed to load metadata: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* actually get the data */
	state_local = zif_state_get_child (state);
	array = zif_md_updateinfo_get_detail_for_package (ZIF_MD_UPDATEINFO (store->priv->md_updateinfo), package_id,
							  state_local, &error_local);
	if (array == NULL) {
		/* ignore the case where we try to get updatinfo on repos
		 * such as fedora, which do not have updateinfo */
		if (error_local->domain == ZIF_MD_ERROR &&
		    error_local->code == ZIF_MD_ERROR_NO_SUPPORT) {

			/* generate some dummy update object so we can
			 * still store the changelog */
			g_debug ("repo %s does not support updateinfo: %s, "
				 "faking it for changelog fallback",
				 zif_store_get_id (ZIF_STORE (store)),
				 error_local->message);
			g_clear_error (&error_local);
			array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
			update_tmp = zif_update_new ();
			zif_update_set_title (update_tmp, "Generic fallback update detail");
			g_ptr_array_add (array, update_tmp);

			/* manually finish the abandoned state */
			ret = zif_state_finished (state_local, error);
			if (!ret)
				goto out;

		} else {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "failed to find any details in updateinfo (but in primary): %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}
	if (array->len != 1) {
		/* FIXME: is this valid? */
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "invalid number of update entries: %i",
			     array->len);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* get ZifPackage for package-id */
	md = zif_store_remote_get_primary (store, error);
	if (md == NULL)
		goto out;
	state_local = zif_state_get_child (state);
	packages = zif_md_find_package (md, package_id, state_local, &error_local);
	if (packages == NULL) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "cannot find package in primary repo: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* fatal */
	if (packages->len == 0) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "cannot find package in primary repo: %s", package_id);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* add the changelog data */
	update_tmp = g_ptr_array_index (array, 0);
	package_remote = ZIF_PACKAGE_REMOTE (g_ptr_array_index (packages, 0));
	state_local = zif_state_get_child (state);
	ret = zif_store_remote_add_changelog (store,
					      update_tmp,
					      package_remote,
					      state_local,
					      error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	update = g_object_ref (update_tmp);
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	if (packages != NULL)
		g_ptr_array_unref (packages);
	return update;
}

/**
 * zif_store_remote_download_repomd:
 * @store: A #ZifStoreRemote
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Redownloads a new repomd file, which contains the links to all new
 * metadata with the new checksums.
 *
 * Return value: %TRUE for failure
 *
 * Since: 0.1.2
 **/
gboolean
zif_store_remote_download_repomd (ZifStoreRemote *store,
				  ZifState *state,
				  GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	ZifState *state_local;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (store->priv->id != NULL, FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* if not online, then this is fatal */
	ret = zif_config_get_boolean (store->priv->config, "network", NULL);
	if (!ret) {
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED_AS_OFFLINE,
			     "failed to download %s as offline",
			     store->priv->repomd_filename);
		goto out;
	}

	/* set steps */
	if (store->priv->loaded) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   20, /* load repo file */
					   80, /* download */
					   -1);
		if (!ret)
			goto out;
	}

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_load (ZIF_STORE (store),
					     state_local,
					     error);
		if (!ret)
			goto out;

		/* done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* ensure we don't skip a valid-looking repomd.xml */
	g_unlink (store->priv->repomd_filename);

	/* download new file */
	store->priv->loaded_metadata = TRUE;
	state_local = zif_state_get_child (state);
	ret = zif_store_remote_download_full (store,
					      "repodata/repomd.xml",
					      store->priv->directory,
					      0,
					      "application/xml",
					      G_CHECKSUM_MD5,
					      NULL,
					      state_local,
					      &error_local);
	store->priv->loaded_metadata = FALSE;
	if (!ret) {
		g_set_error (error,
			     error_local->domain,
			     error_local->code,
			     "failed to download missing repomd: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * zif_store_remote_parse_repomd:
 **/
static gboolean
zif_store_remote_parse_repomd (ZifStoreRemote *store,
			       ZifState *state,
			       GError **error)
{
	guint i;
	const gchar *location;
	gboolean ret = TRUE;
	gchar *contents = NULL;
	gchar *basename;
	gchar *filename;
	gboolean primary_okay = FALSE;
	guint max_age;
	gsize size;
	ZifMd *md;
	GMarkupParseContext *context = NULL;
	const GMarkupParser gpk_store_remote_markup_parser = {
		zif_store_remote_parser_start_element,
		zif_store_remote_parser_end_element,
		zif_store_remote_parser_text,
		NULL, /* passthrough */
		NULL /* error */
	};

	/* set steps */
	ret = zif_state_set_steps (state,
				   error,
				   80, /* get contents */
				   20, /* parse */
				   -1);
	if (!ret)
		goto out;

	/* get repo contents */
	zif_state_set_allow_cancel (state, FALSE);
	ret = g_file_get_contents (store->priv->repomd_filename,
				   &contents,
				   &size,
				   error);
	if (!ret)
		goto out;

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* create parser */
	context = g_markup_parse_context_new (&gpk_store_remote_markup_parser,
					      G_MARKUP_PREFIX_ERROR_POSITION,
					      store, NULL);

	/* parse data */
	zif_state_set_allow_cancel (state, FALSE);
	ret = g_markup_parse_context_parse (context,
					    contents,
					    (gssize) size,
					    error);
	if (!ret)
		goto out;

	/* get the maximum age of the repo files */
	max_age = zif_config_get_uint (store->priv->config, "metadata_expire", NULL);

	/* set MD id and filename for each repo type */
	for (i=1; i<ZIF_MD_KIND_LAST; i++) {
		md = zif_store_remote_get_md_from_type (store, i);
		if (md == NULL)
			continue;

		/* no metalink? */
		if (i == ZIF_MD_KIND_METALINK)
			continue;

		/* no mirrorlist? */
		if (i == ZIF_MD_KIND_MIRRORLIST)
			continue;

		/* ensure we have at least one primary */
		location = zif_md_get_location (md);
		if (location != NULL &&
		    (i == ZIF_MD_KIND_PRIMARY_SQL ||
		     i == ZIF_MD_KIND_PRIMARY_XML)) {
			primary_okay = TRUE;
		}

		/* location not set */
		if (location == NULL)
			continue;

		/* set MD id and filename */
		basename = g_path_get_basename (location);
		filename = g_build_filename (store->priv->directory,
					     basename, NULL);
		zif_md_set_filename (md, filename);
		zif_md_set_max_age (md, max_age);
		g_free (basename);
		g_free (filename);
	}

	/* messed up repo file */
	if (!primary_okay) {
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "failed to get primary metadata location for %s",
			     store->priv->id);
		ret = FALSE;
		goto out;
	}

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	if (context != NULL)
		g_markup_parse_context_free (context);
	g_free (contents);
	return ret;
}

/**
 * zif_store_remote_get_repomd:
 **/
static gboolean
zif_store_remote_get_repomd (ZifStoreRemote *store,
			     ZifState *state,
			     GError **error)
{
	gboolean ret = TRUE;
	GCancellable *cancellable;
	gchar *tmp;
	GError *error_local = NULL;
	guint i;
	ZifState *state_local;
	ZifStoreRemotePrivate *priv = store->priv;

	/* clear download locations that will not be valid anymore */
	zif_download_location_clear (store->priv->download);

	/* always add the baseurl as a location if it is set */
	if (store->priv->baseurl != NULL) {
		for (i=0; priv->baseurl[i] != NULL; i++) {
			zif_download_location_add_uri (priv->download,
						       priv->baseurl[i],
						       NULL);
		}
	}

	/* set steps */
	if (store->priv->baseurl != NULL) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   40, /* download mirrorlist / metalink */
					   20, /* load locations */
					   40, /* download repomd */
					   -1);
		if (!ret)
			goto out;
	}

	/* ensure path is valid */
	cancellable = zif_state_get_cancellable (state);
	ret = zif_ensure_parent_dir_exists (store->priv->repomd_filename,
					    cancellable,
					    error);
	if (!ret)
		goto out;

	/* do we have a baseurl, if so just use that */
	if (store->priv->baseurl != NULL) {
		tmp = g_build_filename (store->priv->baseurl[0],
					"repodata/repomd.xml", NULL);
		state_local = zif_state_get_child (state);
		ret = zif_download_file_full (store->priv->download,
					      tmp,
					      store->priv->repomd_filename,
					      0, /* size */
					      "application/xml",
					      G_CHECKSUM_MD5,
					      NULL, /* checksum */
					      state_local,
					      &error_local);
		g_free (tmp);
		if (!ret) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED_TO_DOWNLOAD,
				     "failed to download repomd.xml from baseurl: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
		goto out;
	}

	/* do we have a mirrorlist */
	if (store->priv->mirrorlist != NULL) {
		state_local = zif_state_get_child (state);

		ret = zif_download_file_full (store->priv->download,
					      store->priv->mirrorlist,
					      zif_md_get_filename (store->priv->md_mirrorlist),
					      0, /* size */
					      "text/plain",
					      G_CHECKSUM_MD5,
					      NULL, /* checksum */
					      state_local,
					      &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED_TO_DOWNLOAD,
				     "failed to download mirrorlist.txt: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;

		/* get mirrors */
		state_local = zif_state_get_child (state);
		ret = zif_download_location_add_md (store->priv->download,
						    store->priv->md_mirrorlist,
						    state_local,
						    &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED_TO_DOWNLOAD,
				     "failed to add mirrors from mirrorlist: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;

		/* download repomd */
		state_local = zif_state_get_child (state);
		ret = zif_download_location_full (store->priv->download,
					          "repodata/repomd.xml",
					          store->priv->repomd_filename,
					          0, /* size */
					          "application/xml",
					          G_CHECKSUM_MD5,
					          NULL, /* checksum */
					          state_local,
					          &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED_TO_DOWNLOAD,
				     "failed to add download repomd.xml using mirrorlist: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
		goto out;
	}

	/* do we have a metalink */
	if (store->priv->metalink != NULL) {
		state_local = zif_state_get_child (state);

		ret = zif_download_file_full (store->priv->download,
					      store->priv->metalink,
					      zif_md_get_filename (store->priv->md_metalink),
					      0, /* size */
					      "application/xml",
					      G_CHECKSUM_MD5,
					      NULL, /* checksum */
					      state_local,
					      &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED_TO_DOWNLOAD,
				     "failed to download metalink.xml: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;

		/* get mirrors */
		state_local = zif_state_get_child (state);
		ret = zif_download_location_add_md (store->priv->download,
						    store->priv->md_metalink,
						    state_local,
						    &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED_TO_DOWNLOAD,
				     "failed to add mirrors from mirrorlist: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;

		/* download repomd */
		state_local = zif_state_get_child (state);
		ret = zif_download_location_full (store->priv->download,
					          "repodata/repomd.xml",
					          store->priv->repomd_filename,
					          0, /* size */
					          "application/xml",
					          G_CHECKSUM_MD5,
					          NULL, /* checksum */
					          state_local,
					          &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED_TO_DOWNLOAD,
				     "failed to add download repomd.xml using mirrorlist: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
		goto out;
	}

	/* we failed */
	g_set_error_literal (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "failed to download repodata.xml as no sources");
out:
	return ret;
}

/**
 * zif_store_remote_process_repomd:
 **/
static gboolean
zif_store_remote_process_repomd (ZifStoreRemote *store,
				 ZifState *state,
				 GError **error)
{
	gboolean ret = TRUE;
	GError *error_local = NULL;
	ZifState *state_local;

	/* set steps */
	if (store->priv->mirrorlist == NULL &&
	    store->priv->metalink == NULL) {
		zif_state_set_number_steps (state, 1); /* parse */
	} else if (store->priv->mirrorlist != NULL &&
		   store->priv->metalink != NULL) {
		g_assert_not_reached ();
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   30, /* parse */
					   30, /* check */
					   40, /* parse */
					   -1);
		if (!ret)
			goto out;
	}

	/* parse the repomd */
	state_local = zif_state_get_child (state);
	ret = zif_store_remote_parse_repomd (store,
					     state_local,
					     &error_local);
	if (!ret) {
		g_unlink (store->priv->repomd_filename);
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_RECOVERABLE,
			     "failed to parse repomd.xml: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* does mirrorlist look okay */
	if (store->priv->mirrorlist != NULL) {
		state_local = zif_state_get_child (state);
		ret = zif_md_check_uncompressed (store->priv->md_mirrorlist,
						 state_local,
						 &error_local);
		if (!ret) {
			if (error_local->domain == ZIF_MD_ERROR &&
			    (error_local->code == ZIF_MD_ERROR_CHECKSUM_INVALID ||
			     error_local->code == ZIF_MD_ERROR_FILE_TOO_OLD ||
			     error_local->code == ZIF_MD_ERROR_FAILED_TO_LOAD ||
			     error_local->code == ZIF_MD_ERROR_FILE_NOT_EXISTS)) {
				g_unlink (store->priv->repomd_filename);
				g_unlink (zif_md_get_filename (store->priv->md_mirrorlist));
				g_set_error (error,
					     ZIF_STORE_ERROR,
					     ZIF_STORE_ERROR_RECOVERABLE,
					     "failed to check mirrorlist: %s",
					     error_local->message);
				g_error_free (error_local);
			} else {
				g_set_error (error,
					     ZIF_STORE_ERROR,
					     ZIF_STORE_ERROR_FAILED,
					     "failed to check mirrorlist: %s",
					     error_local->message);
				g_error_free (error_local);
			}
			goto out;
		}

		/* done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;

		/* add mirrorlist entries */
		state_local = zif_state_get_child (state);
		ret = zif_download_location_add_md (store->priv->download,
						    store->priv->md_mirrorlist,
						    state_local,
						    &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "failed to add mirrors from mirrorlist: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* does metalink look okay */
	if (store->priv->metalink != NULL) {
		state_local = zif_state_get_child (state);

		ret = zif_md_check_uncompressed (store->priv->md_metalink,
						 state_local,
						 &error_local);
		if (!ret) {
			if (error_local->domain == ZIF_MD_ERROR &&
			    (error_local->code == ZIF_MD_ERROR_CHECKSUM_INVALID ||
			     error_local->code == ZIF_MD_ERROR_FILE_TOO_OLD ||
			     error_local->code == ZIF_MD_ERROR_FILE_NOT_EXISTS)) {
				g_unlink (store->priv->repomd_filename);
				g_unlink (zif_md_get_filename (store->priv->md_metalink));
				g_set_error (error,
					     ZIF_STORE_ERROR,
					     ZIF_STORE_ERROR_RECOVERABLE,
					     "failed to check metalink: %s",
					     error_local->message);
				g_error_free (error_local);
			} else {
				g_set_error (error,
					     ZIF_STORE_ERROR,
					     ZIF_STORE_ERROR_FAILED,
					     "failed to check metalink: %s",
					     error_local->message);
				g_error_free (error_local);
			}
			goto out;
		}

		/* done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;

		/* add metalink entries */
		state_local = zif_state_get_child (state);
		ret = zif_download_location_add_md (store->priv->download,
						    store->priv->md_metalink,
						    state_local,
						    &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED,
				     "failed to add mirrors from mirrorlist: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}
out:
	return ret;
}

/**
 * zif_store_remote_load_metadata:
 *
 * This function does the following things:
 *
 * - opens repomd.xml (downloading it if it doesn't exist)
 * - parses the contents, and populates the ZifMd types
 * - parses metalink and mirrorlink into lists of plain urls
 * - checks all the compressed metadata checksums are valid, else they are deleted
 * - checks all the uncompressed metadata checksums are valid, else they are deleted
 **/
static gboolean
zif_store_remote_load_metadata (ZifStoreRemote *store, ZifState *state, GError **error)
{
	gboolean ret = TRUE;
	gboolean file_exists = TRUE;
	GError *error_local = NULL;
	ZifState *state_local;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* already loaded */
	if (store->priv->loaded_metadata)
		goto out;

	/* does repomd.xml exist */
	file_exists = g_file_test (store->priv->repomd_filename,
				   G_FILE_TEST_EXISTS);

	/* set steps */
	if (file_exists) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* download */
					   20, /* parse */
					   -1);
		if (!ret)
			goto out;
	}

	/* simple case */
	if (file_exists) {
		g_debug ("%s already exists",
			 store->priv->repomd_filename);
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_process_repomd (store,
						       state_local,
						       &error_local);
		if (!ret) {
			if (g_error_matches (error_local,
					     ZIF_STORE_ERROR,
					     ZIF_STORE_ERROR_RECOVERABLE)) {
				g_debug ("ignoring %s", error_local->message);
				g_error_free (error_local);
				zif_state_reset (state);
				ret = zif_store_remote_load_metadata (store,
								      state,
								      error);
			} else {
				g_propagate_error (error, error_local);
			}
			goto out;
		}

		/* done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
		goto out;
	}

	/* get the repomd */
	g_debug ("%s does not exist", store->priv->repomd_filename);
	state_local = zif_state_get_child (state);
	ret = zif_store_remote_get_repomd (store,
					   state_local,
					   error);
	if (!ret)
		goto out;

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* process repomd.xml */
	state_local = zif_state_get_child (state);
	ret = zif_store_remote_process_repomd (store,
					       state_local,
					       &error_local);
	if (!ret) {
		if (g_error_matches (error_local,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_RECOVERABLE)) {
			g_debug ("ignoring %s", error_local->message);
			g_error_free (error_local);
			zif_state_reset (state);
			ret = zif_store_remote_load_metadata (store,
							      state,
							      error);
		} else {
			g_propagate_error (error, error_local);
		}
		goto out;
	}

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * zif_store_file_decompress:
 **/
static gboolean
zif_store_file_decompress (const gchar *filename, ZifState *state, GError **error)
{
	gboolean ret = TRUE;
	gboolean compressed;
	gchar *filename_uncompressed = NULL;

	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* only do for compressed filenames */
	compressed = zif_file_is_compressed_name (filename);
	if (!compressed) {
		g_debug ("%s not compressed", filename);
		goto out;
	}

	/* get new name */
	filename_uncompressed = zif_file_get_uncompressed_name (filename);

	/* decompress */
	ret = zif_file_decompress (filename, filename_uncompressed, state, error);
out:
	g_free (filename_uncompressed);
	return ret;
}

/**
 * zif_store_remote_refresh_md:
 **/
static gboolean
zif_store_remote_refresh_md (ZifStoreRemote *remote,
			     ZifMd *md,
			     gboolean force,
			     ZifState *state,
			     GError **error)
{
	const gchar *filename;
	gboolean ret;
	GError *error_local = NULL;
	ZifState *state_local = NULL;
	const gchar *content_type = NULL;

	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* setup progress */
	ret = zif_state_set_steps (state,
				   error,
				   20, /* check uncompressed */
				   60, /* download */
				   20, /* decompress */
				   -1);
	if (!ret)
		goto out;

	/* get filename */
	filename = zif_md_get_filename (md);
	if (filename == NULL) {
		g_debug ("no filename set for %s",
			   zif_md_kind_to_text (zif_md_get_kind (md)));
		ret = zif_state_finished (state, error);
		goto out;
	}

	/* get content type */
	content_type = zif_guess_content_type (filename);

	/* does current uncompressed file equal what repomd says it should be */
	state_local = zif_state_get_child (state);
	ret = zif_md_check_uncompressed (md,
					 state_local,
					 &error_local);
	if (!ret) {
		g_debug ("failed to verify md (%s), so will attempt update",
			 error_local->message);
		g_clear_error (&error_local);
		ret = zif_state_finished (state_local, error);
		if (!ret)
			goto out;
	} else if (!force) {
		g_debug ("%s is okay, and we're not forcing",
			   zif_md_kind_to_text (zif_md_get_kind (md)));
		ret = zif_state_finished (state, error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* download new file */
	state_local = zif_state_get_child (state);
	filename = zif_md_get_location (md);
	ret = zif_store_remote_download_full (remote,
					      filename,
					      remote->priv->directory,
					      0,
					      content_type,
					      G_CHECKSUM_MD5,
					      NULL,
					      state_local,
					      &error_local);
	if (!ret) {
		g_set_error (error, error_local->domain, error_local->code,
			     "failed to refresh %s (%s): %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)),
			     filename,
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* decompress */
	state_local = zif_state_get_child (state);
	filename = zif_md_get_filename (md);
	ret = zif_store_file_decompress (filename, state_local, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to decompress %s for %s: %s",
			     filename,
			     zif_md_kind_to_text (zif_md_get_kind (md)),
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * zif_store_remote_refresh:
 **/
static gboolean
zif_store_remote_refresh (ZifStore *store, gboolean force, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	ZifState *state_local = NULL;
	ZifState *state_loop = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifMd *md;
	guint i;
	gboolean md_priority[ZIF_MD_KIND_LAST];

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* take lock */
	ret = zif_state_take_lock (state,
				   ZIF_LOCK_TYPE_METADATA_WRITE,
				   error);
	if (!ret)
		goto out;

	/* if not online, then this is fatal */
	ret = zif_config_get_boolean (remote->priv->config, "network", NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED_AS_OFFLINE,
				     "failed to refresh as offline");
		goto out;
	}

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (state,
				   error,
				   15, /* download repomd */
				   5, /* load metadata */
				   80, /* refresh each metadata */
				   -1);
	if (!ret)
		goto out;

	/* download new repomd file */
	state_local = zif_state_get_child (state);
	ret = zif_store_remote_download_full (remote,
					      "repodata/repomd.xml",
					      remote->priv->directory,
					      0,
					      "application/xml",
					      G_CHECKSUM_MD5,
					      NULL,
					      state_local,
					      &error_local);
	if (!ret) {
		g_set_error (error, error_local->domain, error_local->code,
			     "failed to download repomd: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* reload */
	state_local = zif_state_get_child (state);
	ret = zif_store_remote_load_metadata (remote, state_local, &error_local);
	if (!ret) {
		g_set_error (error, error_local->domain, error_local->code,
			     "failed to load updated metadata: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* do in nested completion */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, ZIF_MD_KIND_LAST - 1);

	/* initialize failed state */
	for (i = 0; i < ZIF_MD_KIND_LAST; i++)
		md_priority[i] = FALSE;

	/* refresh each repo type in a specific order, so we can avoid
	 * downloading duplicate copies of the same data */
	for (i=1; i < ZIF_MD_KIND_LAST; i++) {

		/* don't download xml if sqlite is available */
		if (i == ZIF_MD_KIND_PRIMARY_XML &&
		    md_priority[ZIF_MD_KIND_PRIMARY_SQL]) {
			g_debug ("skipping download of xml as sqlite exists");
			goto skip;
		}
		if (i == ZIF_MD_KIND_FILELISTS_XML &&
		    md_priority[ZIF_MD_KIND_FILELISTS_SQL]) {
			g_debug ("skipping download of xml as sqlite exists");
			goto skip;
		}
		if (i == ZIF_MD_KIND_OTHER_XML &&
		    md_priority[ZIF_MD_KIND_OTHER_SQL]) {
			g_debug ("skipping download of xml as sqlite exists");
			goto skip;
		}
		if (i == ZIF_MD_KIND_COMPS &&
		    md_priority[ZIF_MD_KIND_COMPS_GZ]) {
			g_debug ("skipping download of xml as sqlite exists");
			goto skip;
		}

		/* get md */
		md = zif_store_remote_get_md_from_type (remote, i);
		if (md != NULL) {
			/* refresh this md object */
			state_loop = zif_state_get_child (state_local);
			ret = zif_store_remote_refresh_md (remote, md, force, state_loop, error);
			if (!ret)
				goto out;

			/* mark this one as done */
			md_priority[i] = TRUE;
		}
skip:
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
	return ret;
}

/**
 * zif_store_remote_load:
 *
 * This function has to be fast, so don't download anything or load any
 * databases until zif_store_remote_load_metadata().
 **/
static gboolean
zif_store_remote_load (ZifStore *store, ZifState *state, GError **error)
{
	gboolean got_baseurl = FALSE;
	gboolean ret = TRUE;
	gboolean gpgcheck;
	gchar *baseurl_temp;
	gchar *enabled = NULL;
	gchar *filename;
	gchar *media_root;
	gchar *metadata_expire = NULL;
	gchar *temp;
	GError *error_local = NULL;
	guint i;
	guint mirrorlist_expire;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);
	g_return_val_if_fail (remote->priv->repo_filename != NULL, FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* already loaded */
	if (remote->priv->loaded)
		goto out;

	/* setup state with the correct number of steps */
	ret = zif_state_set_steps (state,
				   error,
				   80, /* load from file */
				   20, /* parse */
				   -1);
	if (!ret)
		goto out;

	/* load repo file */
	remote->priv->file = zif_load_multiline_key_file (remote->priv->repo_filename,
							 &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "failed to load keyfile %s: %s",
			     remote->priv->repo_filename,
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* name */
	remote->priv->name = g_key_file_get_string (remote->priv->file,
						    remote->priv->id,
						    "name",
						    &error_local);
	if (error_local != NULL) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to get name: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* media id, for matching in .discinfo */
	remote->priv->media_id = g_key_file_get_string (remote->priv->file,
							remote->priv->id,
							"mediaid",
							NULL);

	/* the value to expire the cache by */
	metadata_expire = g_key_file_get_string (remote->priv->file,
						 remote->priv->id,
						 "metadata_expire",
						 NULL);
	if (metadata_expire != NULL)
		remote->priv->metadata_expire = zif_time_string_to_seconds (metadata_expire);

	/* enabled is not a required key */
	enabled = g_key_file_get_string (remote->priv->file,
					 remote->priv->id,
					 "enabled",
					 NULL);

	/* convert to bool, otherwise assume valid */
	if (enabled != NULL)
		remote->priv->enabled = zif_boolean_from_text (enabled);
	else
		remote->priv->enabled = TRUE;

	/* set the runtime enabled state */
	zif_store_set_enabled (store, remote->priv->enabled);

	/* find the baseurl for this device */
	if (remote->priv->media_id != NULL) {
		/* find the root for the media id */
		media_root = zif_media_get_root_from_id (remote->priv->media, remote->priv->media_id);
		if (media_root != NULL) {
			zif_download_location_add_uri (remote->priv->download, media_root, NULL);
		} else {
			g_warning ("cannot find media %s, disabling source", remote->priv->media_id);
			remote->priv->enabled = FALSE;
		}
	}

	/* expand out */
	remote->priv->name_expanded = zif_config_expand_substitutions (remote->priv->config,
								       remote->priv->name,
								       error);
	if (remote->priv->name_expanded == NULL) {
		ret = FALSE;
		goto out;
	}

	/* get base url array (allowed to be blank) */
	temp = g_key_file_get_string (remote->priv->file,
				      remote->priv->id,
				      "baseurl",
				      NULL);
	if (temp != NULL && temp[0] != '\0') {
		baseurl_temp = zif_config_expand_substitutions (remote->priv->config,
								temp,
								error);
		if (baseurl_temp == NULL) {
			ret = FALSE;
			goto out;
		}
		remote->priv->baseurl = g_strsplit (baseurl_temp, ";", -1);
		for (i=0; remote->priv->baseurl[i] != NULL; i++) {
			zif_download_location_add_uri (remote->priv->download,
						       remote->priv->baseurl[i],
						       NULL);
		}
		got_baseurl = TRUE;
		g_free (baseurl_temp);
	}
	g_free (temp);

	/* get mirror list (allowed to be blank) */
	temp = g_key_file_get_string (remote->priv->file,
				      remote->priv->id,
				      "mirrorlist",
				      NULL);
	if (temp != NULL && temp[0] != '\0') {
		remote->priv->mirrorlist = zif_config_expand_substitutions (remote->priv->config,
									    temp,
									    error);
		if (remote->priv->mirrorlist == NULL) {
			ret = FALSE;
			goto out;
		}
	}
	g_free (temp);

	/* get metalink (allowed to be blank) */
	temp = g_key_file_get_string (remote->priv->file,
				      remote->priv->id,
				      "metalink",
				      NULL);
	if (temp != NULL && temp[0] != '\0') {
		remote->priv->metalink = zif_config_expand_substitutions (remote->priv->config,
									  temp,
									  error);
		if (remote->priv->metalink == NULL) {
			ret = FALSE;
			goto out;
		}
	}
	g_free (temp);

	/* get public key for repo */
	gpgcheck = g_key_file_get_boolean (remote->priv->file,
					   remote->priv->id,
					   "gpgcheck",
					   NULL);
	temp = g_key_file_get_string (remote->priv->file,
				      remote->priv->id,
				      "gpgkey",
				      NULL);
	if (gpgcheck && temp != NULL) {
		remote->priv->pubkey = zif_config_expand_substitutions (remote->priv->config,
									temp,
									error);
		if (remote->priv->pubkey == NULL) {
			ret = FALSE;
			goto out;
		}
	}
	g_free (temp);

	/* urgh.. yum allows mirrorlist= to be used as well as metalink= for metalink URLs */
	if (remote->priv->metalink == NULL &&
	    remote->priv->mirrorlist != NULL &&
	    g_strstr_len (remote->priv->mirrorlist, -1, "metalink?") != NULL) {
		/* swap */
		remote->priv->metalink = remote->priv->mirrorlist;
		remote->priv->mirrorlist = NULL;
	}

	/* .repo file is broken and has both metalink and baseurl */
	if (got_baseurl && remote->priv->metalink != NULL) {
		g_debug ("%s has both baseurl and metalink, ignoring metalink",
			 remote->priv->repo_filename);
		g_free (remote->priv->metalink);
		remote->priv->metalink = NULL;
	}

	/* we have to set this here in case we are using the metalink to download repodata.xml */
	if (remote->priv->metalink != NULL) {
		filename = g_build_filename (remote->priv->directory, "metalink.xml", NULL);
		zif_md_set_filename (remote->priv->md_metalink, filename);
		mirrorlist_expire = zif_config_get_uint (remote->priv->config,
							 "mirrorlist_expire",
							 NULL);
		zif_md_set_max_age (remote->priv->md_metalink,
				    mirrorlist_expire);
		g_free (filename);
	}

	/* we have to set this here in case we are using the mirrorlist to download repodata.xml */
	if (remote->priv->mirrorlist != NULL) {
		filename = g_build_filename (remote->priv->directory, "mirrorlist.txt", NULL);
		zif_md_set_filename (remote->priv->md_mirrorlist, filename);
		mirrorlist_expire = zif_config_get_uint (remote->priv->config,
							 "mirrorlist_expire",
							 NULL);
		zif_md_set_max_age (remote->priv->md_mirrorlist,
				    mirrorlist_expire);
		g_free (filename);
	}

	/* we need either a base url or mirror list for an enabled store */
	if (remote->priv->enabled &&
	    !got_baseurl &&
	    remote->priv->metalink == NULL &&
	    remote->priv->mirrorlist == NULL &&
	    remote->priv->media_id == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "baseurl, mediaid, metalink or mirrorlist required");
		ret = FALSE;
		goto out;
	}

	/* okay */
	remote->priv->loaded = TRUE;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	g_free (metadata_expire);
	g_free (enabled);
	return ret;
}

/**
 * zif_store_remote_get_string:
 * @store: A #ZifStoreRemote
 * @key: A key name to retrieve, e.g. "keepcache"
 * @error: A #GError, or %NULL
 *
 * Gets a string value from the repo, falling back to the config file
 * if not found.
 *
 * Return value: An allocated value, or %NULL
 *
 * Since: 0.2.6
 **/
gchar *
zif_store_remote_get_string (ZifStoreRemote *store,
			     const gchar *key,
			     GError **error)
{
	gchar *tmp;

	/* get from config first */
	tmp = g_key_file_get_string (store->priv->file,
				     store->priv->id,
				     key,
				     NULL);
	if (tmp != NULL)
		goto out;
	tmp = zif_config_get_string (store->priv->config,
				     key,
				     error);
out:
	return tmp;
}

/**
 * zif_store_remote_get_boolean:
 * @store: A #ZifStoreRemote
 * @key: A key name to retrieve, e.g. "keepcache"
 * @error: A #GError, or %NULL
 *
 * Gets a boolean value from the repo, falling back to the config file
 * if not found.
 *
 * Return value: %TRUE or %FALSE
 *
 * Since: 0.2.6
 **/
gboolean
zif_store_remote_get_boolean (ZifStoreRemote *store,
			      const gchar *key,
			      GError **error)
{
	gboolean ret = FALSE;
	gchar *tmp;

	/* get from config first */
	tmp = zif_store_remote_get_string (store,
					   key,
					   error);
	if (tmp != NULL) {
		ret = zif_boolean_from_text (tmp);
		goto out;
	}
out:
	g_free (tmp);
	return ret;
}

/**
 * zif_store_remote_remove_packages:
 **/
static gboolean
zif_store_remote_remove_packages (ZifStoreRemote *remote, GError **error)
{
	gchar *packages_dir;
	const gchar *filename;
	gchar *package_filename;
	GDir *dir;
	GFile *file;
	gboolean ret = TRUE;

	/* open directory */
	packages_dir = g_build_filename (remote->priv->directory,
					 "packages", NULL);
	dir = g_dir_open (packages_dir, 0, NULL);
	if (dir == NULL)
		goto out;

	/* search directory */
	do {
		filename = g_dir_read_name (dir);
		if (filename == NULL)
			break;
		if (!g_str_has_suffix (filename, ".rpm"))
			continue;

		/* now we're sure it's an rpm file, delete it */
		package_filename = g_build_filename (packages_dir,
						     filename, NULL);
		file = g_file_new_for_path (package_filename);
		ret = g_file_delete (file, NULL, error);
		g_object_unref (file);
		g_free (package_filename);
	} while (ret);
out:
	if (dir != NULL)
		g_dir_close (dir);
	g_free (packages_dir);
	return ret;
}

/**
 * zif_store_remote_clean:
 **/
static gboolean
zif_store_remote_clean (ZifStore *store, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	gboolean exists;
	GError *error_local = NULL;
	GFile *file;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifState *state_local;
	ZifMd *md;
	guint i;
	const gchar *location;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* setup state with the correct number of steps */
	if (remote->priv->loaded_metadata) {
		ret = zif_state_set_steps (state,
					   error,
					   90, /* clean each repo */
					   10, /* clean repomd */
					   -1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   90, /* load metadata */
					   8, /* clean each repo */
					   2, /* clean repomd */
					   -1);
	}
	if (!ret)
		goto out;

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_load_metadata (remote, state_local, &error_local);
		if (!ret) {
			/* ignore this error */
			g_debug ("failed to load metadata xml: %s\n", error_local->message);
			g_error_free (error_local);
			ret = TRUE;
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* set MD id and filename for each repo type */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, ZIF_MD_KIND_LAST - 1);
	for (i=1; i<ZIF_MD_KIND_LAST; i++) {
		md = zif_store_remote_get_md_from_type (remote, i);
		if (md == NULL) {
			/* TODO: until we've created ZifMdComps and ZifMdOther we'll get warnings here */
			g_debug ("failed to get local store for %s with %s", zif_md_kind_to_text (i), remote->priv->id);
			goto skip;
		}

		/* location not set */
		location = zif_md_get_location (md);
		if (location == NULL) {
			g_debug ("no location set for %s with %s", zif_md_kind_to_text (i), remote->priv->id);
			goto skip;
		}

		/* clean md */
		ret = zif_md_clean (md, &error_local);
		if (!ret) {
			if (error_local->domain == ZIF_MD_ERROR &&
			    error_local->code == ZIF_MD_ERROR_NO_FILENAME) {
				g_debug ("failed to clean %s as no filename in %s",
					 zif_md_kind_to_text (i),
					 remote->priv->id);
				g_clear_error (&error_local);
				goto skip;
			}
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to clean %s: %s", zif_md_kind_to_text (i), error_local->message);
			g_error_free (error_local);
			goto out;
		}
skip:
		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* clean master (last) */
	exists = g_file_test (remote->priv->repomd_filename, G_FILE_TEST_EXISTS);
	if (exists) {
		file = g_file_new_for_path (remote->priv->repomd_filename);
		ret = g_file_delete (file, NULL, &error_local);
		g_object_unref (file);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to delete metadata file %s: %s",
				     remote->priv->repomd_filename, error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* remove packages */
	ret = zif_store_remote_remove_packages (remote, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * zif_store_remote_get_local_directory:
 * @store: A #ZifStoreRemote
 *
 * Gets the directory used for this repo, e.g. /var/cache/yum/i386/fedora
 *
 * Return value: The directory the repo downloads cache file to
 *
 * Since: 0.1.3
 **/
const gchar *
zif_store_remote_get_local_directory (ZifStoreRemote *store)
{
	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	return store->priv->directory;
}

/**
 * zif_store_remote_set_id:
 * @store: A #ZifStoreRemote
 * @id: The repository id, e.g. "fedora"
 *
 * Sets the ID for the #ZifStoreRemote
 *
 * Since: 0.1.3
 **/
void
zif_store_remote_set_id (ZifStoreRemote *store, const gchar *id)
{
	guint i;
	ZifMd *md;

	g_return_if_fail (ZIF_IS_STORE_REMOTE (store));
	g_return_if_fail (id != NULL);
	g_return_if_fail (store->priv->id == NULL);

	/* save */
	g_debug ("setting store %s", id);
	store->priv->id = g_strdup (id);

	/* set MD id for each repo type */
	for (i=1; i<ZIF_MD_KIND_LAST; i++) {
		md = zif_store_remote_get_md_from_type (store, i);
		if (md == NULL)
			continue;
		zif_md_set_id (md, store->priv->id);
	}
}

/**
 * zif_store_remote_set_from_file:
 * @state: A #ZifState to use for progress reporting
 *
 * Since: 0.1.0
 **/
gboolean
zif_store_remote_set_from_file (ZifStoreRemote *store,
				const gchar *repo_filename,
				const gchar *id,
				ZifState *state,
				GError **error)
{
	gboolean ret = TRUE;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (repo_filename != NULL, FALSE);
	g_return_val_if_fail (!store->priv->loaded, FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* save */
	zif_store_remote_set_id (store, id);
	store->priv->repo_filename = g_strdup (repo_filename);
	store->priv->directory = g_build_filename (store->priv->cache_dir,
						   store->priv->id,
						   NULL);

	/* repomd location */
	store->priv->repomd_filename = g_build_filename (store->priv->cache_dir,
							 store->priv->id,
							 "repomd.xml",
							 NULL);

	/* setup watch */
	ret = zif_monitor_add_watch (store->priv->monitor,
				     repo_filename,
				     &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "failed to setup watch: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get data */
	ret = zif_store_remote_load (ZIF_STORE (store),
				     state,
				     &error_local);
	if (!ret) {
		g_set_error (error,
			     error_local->domain,
			     error_local->code,
			     "failed to load store %s: %s",
			     id, error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	/* save */
	return ret;
}

/**
 * zif_store_remote_set_enabled:
 * @store: A #ZifStoreRemote
 * @enabled: If the object should be enabled
 * @error: A #GError, or %NULL
 *
 * Enable or disable a remote repository.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.6
 **/
gboolean
zif_store_remote_set_enabled (ZifStoreRemote *store,
			      gboolean enabled,
			      ZifState *state,
			      GError **error)
{
	GKeyFile *file;
	gboolean ret;
	GError *error_local = NULL;
	gchar *data;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (store->priv->id != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* take lock */
	ret = zif_state_take_lock (state,
				   ZIF_LOCK_TYPE_REPO_WRITE,
				   error);
	if (!ret)
		goto out;

	/* load file */
	file = zif_load_multiline_key_file (store->priv->repo_filename,
					    &error_local);
	if (file == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_STORE_ERROR,
			     ZIF_STORE_ERROR_FAILED,
			     "failed to load store file: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* toggle enabled */
	store->priv->enabled = enabled;
	g_key_file_set_boolean (file, store->priv->id, "enabled", store->priv->enabled);

	/* save new data to file */
	data = g_key_file_to_data (file, NULL, &error_local);
	if (data == NULL) {
		ret = FALSE;
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to get save data: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}
	ret = g_file_set_contents (store->priv->repo_filename, data, -1, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to save: %s", error_local->message);
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
zif_store_remote_resolve (ZifStore *store,
			  gchar **search,
			  ZifStoreResolveFlags flags,
			  ZifState *state,
			  GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifState *state_local;
	ZifMd *primary;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (remote->priv->id != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* setup state */
	if (remote->priv->loaded_metadata) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   20, /* resolve */
					   -1);
		if (!ret)
			goto out;
	}

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_load_metadata (remote, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     error_local->domain,
				     error_local->code,
				     "failed to load metadata for %s: %s",
				     remote->priv->id,
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	state_local = zif_state_get_child (state);
	primary = zif_store_remote_get_primary (remote, error);
	if (primary == NULL)
		goto out;
	array = zif_md_resolve_full (primary,
				     search,
				     flags,
				     state_local,
				     error);
	if (array == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return array;
}

/**
 * zif_store_remote_search_name:
 **/
static GPtrArray *
zif_store_remote_search_name (ZifStore *store, gchar **search, ZifState *state, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifState *state_local;
	ZifMd *md;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (remote->priv->id != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* setup state */
	if (remote->priv->loaded_metadata) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   2, /* load */
					   98, /* search name */
					   -1);
		if (!ret)
			goto out;
	}

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_load_metadata (remote, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     error_local->domain,
				     error_local->code,
				     "failed to load metadata for %s: %s",
				     remote->priv->id,
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	state_local = zif_state_get_child (state);
	md = zif_store_remote_get_primary (remote, error);
	if (md == NULL)
		goto out;
	array = zif_md_search_name (md, search, state_local, error);
	if (array == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return array;
}

/**
 * zif_store_remote_search_details:
 **/
static GPtrArray *
zif_store_remote_search_details (ZifStore *store, gchar **search, ZifState *state, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifState *state_local;
	ZifMd *md;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (remote->priv->id != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* setup state */
	if (remote->priv->loaded_metadata) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   20, /* search details */
					   -1);
		if (!ret)
			goto out;
	}

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_load_metadata (remote, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     error_local->domain,
				     error_local->code,
				     "failed to load metadata for %s: %s",
				     remote->priv->id,
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	state_local = zif_state_get_child (state);
	md = zif_store_remote_get_primary (remote, error);
	if (md == NULL)
		goto out;
	array = zif_md_search_details (md, search, state_local, error);
	if (array == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return array;
}

/**
 * zif_store_remote_search_category_resolve:
 **/
static ZifPackage *
zif_store_remote_search_category_resolve (ZifStore *store, const gchar *name, ZifState *state, GError **error)
{
	gboolean ret;
	ZifStore *store_local = NULL;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifPackage *package = NULL;
	ZifState *state_local;
	const gchar *to_array[] = { NULL, NULL };

	g_return_val_if_fail (zif_state_valid (state), NULL);

	store_local = zif_store_local_new ();

	/* setup steps */
	ret = zif_state_set_steps (state,
				   error,
				   90, /* resolve local */
				   10, /* resolve remote */
				   -1);
	if (!ret)
		goto out;

	/* is already installed? */
	state_local = zif_state_get_child (state);
	to_array[0] = name;
	array = zif_store_resolve (store_local, (gchar**) to_array, state_local, &error_local);
	if (array == NULL) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to resolve installed package %s: %s", name, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* get newest, ignore error */
	package = zif_package_array_get_newest (array, NULL);
	if (package != NULL) {
		/* we don't need to do the second part */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
		goto out;
	}

	/* clear array */
	g_ptr_array_unref (array);

	/* is available in this repo? */
	state_local = zif_state_get_child (state);
	to_array[0] = name;
	array = zif_store_resolve (ZIF_STORE (store), (gchar**)to_array, state_local, &error_local);
	if (array == NULL) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to resolve installed package %s: %s", name, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* get newest, ignore error */
	package = zif_package_array_get_newest (array, NULL);
	if (package != NULL)
		goto out;

	/* we suck */
	g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED_TO_FIND,
		     "failed to resolve installed package %s installed or in this repo", name);
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	if (store_local != NULL)
		g_object_unref (store_local);
	return package;
}

/**
 * zif_store_remote_search_category:
 **/
static GPtrArray *
zif_store_remote_search_category (ZifStore *store, gchar **group_id, ZifState *state, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *array_names = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifState *state_local;
	ZifState *state_loop;
	ZifPackage *package;
	const gchar *name;
	const gchar *location;
	guint i;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (remote->priv->id != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* setup state */
	if (remote->priv->loaded_metadata) {
		ret = zif_state_set_steps (state,
					   error,
					   10, /* get packages */
					   90, /* search category */
					   -1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   90, /* load metadata */
					   2, /* get packages */
					   8, /* search category */
					   -1);
	}
	if (!ret)
		goto out;

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_load_metadata (remote, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     error_local->domain,
				     error_local->code,
				     "failed to load metadata for %s: %s",
				     remote->priv->id,
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* does this repo have comps data? */
	location = zif_md_get_location (remote->priv->md_comps);
	if (location == NULL) {
		/* empty array, as we want success */
		array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
		ret = zif_state_finished (state, error);
		if (!ret)
			goto out;
		goto out;
	}

	/* get package names for group */
	state_local = zif_state_get_child (state);
	array_names = zif_md_comps_get_packages_for_group (ZIF_MD_COMPS (remote->priv->md_comps),
							   group_id[0], state_local, &error_local);
	if (array_names == NULL) {
		/* ignore when group isn't present, TODO: use GError code */
		if (g_str_has_prefix (error_local->message, "could not find group")) {
			array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
			g_error_free (error_local);
			ret = zif_state_finished (state, error);
			if (!ret)
				goto out;
			goto out;
		}
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to get packages for group %s: %s", group_id[0], error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* setup state */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, array_names->len);

	/* results array */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* resolve names */
	for (i=0; i<array_names->len; i++) {
		name = g_ptr_array_index (array_names, i);

		/* state */
		state_loop = zif_state_get_child (state_local);
		package = zif_store_remote_search_category_resolve (store, name, state_loop, &error_local);
		if (package == NULL) {
			/* ignore when package isn't present */
			if (error_local->code == ZIF_STORE_ERROR_FAILED_TO_FIND) {
				g_clear_error (&error_local);
				g_debug ("Failed to find %s installed or in repo %s", name, remote->priv->id);
				ret = zif_state_finished (state_loop, error);
				if (!ret)
					goto out;
				goto ignore_error;
			}

			g_set_error (error, error_local->domain, error_local->code,
				     "failed to get resolve %s for %s: %s", name, group_id[0], error_local->message);
			g_error_free (error_local);

			/* undo all our hard work */
			g_ptr_array_unref (array);
			array = NULL;
			goto out;
		}

		/* add to array */
		g_ptr_array_add (array, package);
ignore_error:
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
	if (array_names != NULL)
		g_ptr_array_unref (array_names);
	return array;
}

/**
 * zif_store_remote_search_group:
 **/
static GPtrArray *
zif_store_remote_search_group (ZifStore *store, gchar **search, ZifState *state, GError **error)
{
	guint i;
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *array_tmp = NULL;
	gchar **search_cats = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifState *state_local;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (remote->priv->id != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* setup state */
	if (remote->priv->loaded_metadata) {
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

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_load_metadata (remote, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     error_local->domain,
				     error_local->code,
				     "failed to load metadata for %s: %s",
				     remote->priv->id,
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* we can't just use zif_md_primary_*_search_group() as this searches
	 * by *rpm* group, which isn't what we want -- instead we need to get
	 * the list of categories for each group, and then return results. */
	array_tmp = zif_groups_get_cats_for_group (remote->priv->groups, search[0], error);
	if (array_tmp == NULL)
		goto out;

	/* no results for this group enum is not fatal */
	if (array_tmp->len == 0) {
		ret = zif_state_finished (state, error);
		if (!ret)
			goto out;
		array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
		goto out;
	}

	/* convert from pointer array to (gchar **) */
	search_cats = g_new0 (gchar *, array_tmp->len + 1);
	for (i=0; i < array_tmp->len; i++)
		search_cats[i] = g_strdup (g_ptr_array_index (array_tmp, i));

	/* now search by category */
	state_local = zif_state_get_child (state);
	array = zif_store_remote_search_category (store, search_cats, state_local, error);
	if (array == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	if (array_tmp != NULL)
		g_ptr_array_unref (array_tmp);
	g_strfreev (search_cats);
	return array;
}

/**
 * zif_store_remote_find_package:
 **/
static ZifPackage *
zif_store_remote_find_package (ZifStore *store,
			       const gchar *package_id,
			       ZifState *state,
			       GError **error)
{
	gboolean ret;
	GPtrArray *array = NULL;
	ZifPackage *package = NULL;
	GError *error_local = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifState *state_local;
	ZifMd *primary;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (remote->priv->id != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* setup state */
	if (remote->priv->loaded_metadata) {
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

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_load_metadata (remote, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     error_local->domain,
				     error_local->code,
				     "failed to load metadata for %s: %s",
				     remote->priv->id,
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* search with predicate, TODO: search version (epoch+release) */
	state_local = zif_state_get_child (state);
	primary = zif_store_remote_get_primary (remote, error);
	if (primary == NULL)
		goto out;
	array = zif_md_find_package (primary, package_id, state_local, &error_local);
	if (array == NULL) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to search: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

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
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	return package;
}

/**
 * zif_store_remote_get_packages:
 **/
static GPtrArray *
zif_store_remote_get_packages (ZifStore *store, ZifState *state, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifState *state_local;
	ZifMd *primary;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (remote->priv->id != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* setup state */
	if (remote->priv->loaded_metadata) {
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

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_load_metadata (remote, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     error_local->domain,
				     error_local->code,
				     "failed to load metadata for %s: %s",
				     remote->priv->id,
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	primary = zif_store_remote_get_primary (remote, error);
	if (primary == NULL)
		goto out;
	state_local = zif_state_get_child (state);
	array = zif_md_get_packages (primary, state_local, error);
	if (array == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return array;
}

/**
 * zif_store_remote_get_categories:
 **/
static GPtrArray *
zif_store_remote_get_categories (ZifStore *store, ZifState *state, GError **error)
{
	gboolean ret;
	guint i, j;
	const gchar *location;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *array_cats = NULL;
	GPtrArray *array_groups;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifState *state_local;
	ZifState *state_loop;
	ZifCategory *group;
	ZifCategory *category;
	ZifCategory *category_tmp;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (remote->priv->id != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* setup state */
	if (remote->priv->loaded_metadata) {
		ret = zif_state_set_steps (state,
					   error,
					   50, /* get categories */
					   50, /* get groups */
					   -1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   90, /* load metadata */
					   5, /* get categories */
					   5, /* get groups */
					   -1);
	}
	if (!ret)
		goto out;

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_load_metadata (remote, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     error_local->domain,
				     error_local->code,
				     "failed to load metadata for %s: %s",
				     remote->priv->id,
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* does this repo have comps data? */
	location = zif_md_get_location (remote->priv->md_comps);
	if (location == NULL) {
		/* empty array, as we want success */
		array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

		/* this section done */
		ret = zif_state_finished (state, error);
		if (!ret)
			goto out;

		goto out;
	}

	/* get list of categories */
	state_local = zif_state_get_child (state);
	array_cats = zif_md_comps_get_categories (ZIF_MD_COMPS (remote->priv->md_comps), state_local, &error_local);
	if (array_cats == NULL) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to get categories: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* results array */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* no results */
	if (array_cats->len == 0)
		goto skip;

	/* setup steps */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, array_cats->len);

	/* get groups for categories */
	for (i=0; i<array_cats->len; i++) {
		category = g_ptr_array_index (array_cats, i);

		/* get the groups for this category */
		state_loop = zif_state_get_child (state_local);
		array_groups = zif_md_comps_get_groups_for_category (ZIF_MD_COMPS (remote->priv->md_comps),
									  zif_category_get_id (category), state_loop, &error_local);
		if (array_groups == NULL) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to get groups for %s: %s", zif_category_get_id (category), error_local->message);
			g_error_free (error_local);

			/* undo the work we've already done */
			g_ptr_array_unref (array);
			array = NULL;
			goto out;
		}

		/* only add categories which have groups */
		if (array_groups->len > 0) {

			/* first, add the parent */
			g_ptr_array_add (array, g_object_ref (category));

			/* second, add the groups belonging to this parent */
			for (j=0; j<array_groups->len; j++) {
				group = g_ptr_array_index (array_groups, j);
				category_tmp = g_object_ref (group);
				g_ptr_array_add (array, category_tmp);
			}
		}

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
	}
skip:
	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	if (array_cats != NULL)
		g_ptr_array_unref (array_cats);
	return array;
}

/**
 * zif_store_remote_what_provides:
 **/
static GPtrArray *
zif_store_remote_what_provides (ZifStore *store, GPtrArray *depends,
				ZifState *state, GError **error)
{
	gboolean ret;
	gchar **search = NULL;
	GError *error_local = NULL;
	GPtrArray *array_filelists = NULL;
	GPtrArray *array = NULL;
	GPtrArray *array_primary = NULL;
	guint i;
	guint idx = 0;
	ZifDepend *depend_tmp;
	ZifMd *filelists;
	ZifMd *primary;
	ZifState *state_local;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* setup state */
	if (remote->priv->loaded_metadata) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   60, /* load */
					   20, /* what provides (primary) */
					   20, /* what provides (filelists) */
					   -1);
		if (!ret)
			goto out;
	}

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_load_metadata (remote, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     error_local->domain,
				     error_local->code,
				     "failed to load metadata for %s: %s",
				     remote->priv->id,
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* get provides from primary */
	primary = zif_store_remote_get_primary (remote, error);
	if (primary == NULL)
		goto out;
	state_local = zif_state_get_child (state);
	array_primary = zif_md_what_provides (primary,
					      depends,
					      state_local,
					      error);
	if (array_primary == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* get provides from filelists for SQL */
	filelists = zif_store_remote_get_filelists (remote, NULL);
	if (filelists != NULL &&
	    ZIF_IS_MD_FILELISTS_SQL (filelists)) {
		/* convert the depends that look like file paths into a GStrv */
		search = g_new0 (gchar *, depends->len + 1);
		for (i = 0; i < depends->len; i++) {
			depend_tmp = g_ptr_array_index (depends, i);
			if (zif_depend_get_flag (depend_tmp) != ZIF_DEPEND_FLAG_ANY)
				continue;
			if (zif_depend_get_name (depend_tmp)[0] != '/')
				continue;
			search[idx++] = g_strdup (zif_depend_get_name (depend_tmp));
		}
		if (idx > 0) {
			state_local = zif_state_get_child (state);
			array_filelists = zif_store_search_file (store,
								 search,
								 state_local,
								 error);
			if (array_filelists == NULL)
				goto out;
		}
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	array = zif_object_array_new ();
	zif_object_array_add_array (array, array_primary);
	if (array_filelists != NULL)
		zif_object_array_add_array (array, array_filelists);

	/* filter duplicates */
	zif_package_array_filter_duplicates (array);
out:
	g_strfreev (search);
	if (array_primary != NULL)
		g_ptr_array_unref (array_primary);
	if (array_filelists != NULL)
		g_ptr_array_unref (array_filelists);
	return array;
}

/**
 * zif_store_remote_what_requires:
 **/
static GPtrArray *
zif_store_remote_what_requires (ZifStore *store, GPtrArray *depends,
				 ZifState *state, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifState *state_local;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifMd *primary;

	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* setup state */
	if (remote->priv->loaded_metadata) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   20, /* obsoletes */
					   -1);
		if (!ret)
			goto out;
	}

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_load_metadata (remote, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     error_local->domain,
				     error_local->code,
				     "failed to load metadata for %s: %s",
				     remote->priv->id,
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* get details */
	state_local = zif_state_get_child (state);
	primary = zif_store_remote_get_primary (remote, error);
	if (primary == NULL)
		goto out;
	array = zif_md_what_requires (primary, depends,
				      state_local, error);
	if (array == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return array;
}

/**
 * zif_store_remote_what_obsoletes:
 **/
static GPtrArray *
zif_store_remote_what_obsoletes (ZifStore *store, GPtrArray *depends,
				 ZifState *state, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifState *state_local;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifMd *primary;

	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* setup state */
	if (remote->priv->loaded_metadata) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   20, /* obsoletes */
					   -1);
		if (!ret)
			goto out;
	}

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_load_metadata (remote, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     error_local->domain,
				     error_local->code,
				     "failed to load metadata for %s: %s",
				     remote->priv->id,
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* get details */
	state_local = zif_state_get_child (state);
	primary = zif_store_remote_get_primary (remote, error);
	if (primary == NULL)
		goto out;
	array = zif_md_what_obsoletes (primary, depends,
				       state_local, error);
	if (array == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return array;
}

/**
 * zif_store_remote_what_conflicts:
 **/
static GPtrArray *
zif_store_remote_what_conflicts (ZifStore *store, GPtrArray *depends,
				 ZifState *state, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifState *state_local;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifMd *primary;

	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* setup state */
	if (remote->priv->loaded_metadata) {
		zif_state_set_number_steps (state, 1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   80, /* load */
					   20, /* conflicts */
					   -1);
		if (!ret)
			goto out;
	}

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_load_metadata (remote, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     error_local->domain,
				     error_local->code,
				     "failed to load metadata for %s: %s",
				     remote->priv->id,
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* get details */
	state_local = zif_state_get_child (state);
	primary = zif_store_remote_get_primary (remote, error);
	if (primary == NULL)
		goto out;
	array = zif_md_what_conflicts (primary, depends,
				       state_local, error);
	if (array == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return array;
}

/**
 * zif_store_remote_convert_pkgids_to_packages:
 *
 * Converts from an array of pkgid's (gchar *) and converts to an
 * array of packages (ZifPackage *).
 **/
static GPtrArray *
zif_store_remote_convert_pkgids_to_packages (ZifMd *primary,
					     GPtrArray *pkgids,
					     ZifState *state,
					     GError **error)
{
	const gchar *pkgid;
	const gchar *to_array[] = { NULL, NULL };
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *array_tmp;
	GPtrArray *tmp;
	guint i;
	ZifState *state_local;

	/* setup state */
	zif_state_set_number_steps (state, pkgids->len);

	array_tmp = zif_object_array_new ();
	for (i=0; i<pkgids->len; i++) {
		pkgid = g_ptr_array_index (pkgids, i);

		/* get the results (should just be one) */
		to_array[0] = pkgid;
		state_local = zif_state_get_child (state);
		tmp = zif_md_search_pkgid (primary,
					   (gchar **) to_array,
					   state_local,
					   &error_local);
		if (tmp == NULL) {
			g_set_error (error,
				     ZIF_STORE_ERROR,
				     ZIF_STORE_ERROR_FAILED_TO_FIND,
				     "failed to resolve pkgId to package: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* add to main array */
		zif_object_array_add_array (array_tmp, tmp);
		g_ptr_array_unref (tmp);

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* success */
	array = g_ptr_array_ref (array_tmp);
out:
	g_ptr_array_unref (array_tmp);
	return array;
}

/**
 * zif_store_remote_search_file:
 **/
static GPtrArray *
zif_store_remote_search_file (ZifStore *store, gchar **search, ZifState *state, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *pkgids;
	GPtrArray *array = NULL;
	ZifState *state_local;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifMd *primary;
	ZifMd *filelists;

	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* setup state */
	if (remote->priv->loaded_metadata) {
		ret = zif_state_set_steps (state,
					   error,
					   98, /* search file */
					   2, /* get pkgids */
					   -1);
	} else {
		ret = zif_state_set_steps (state,
					   error,
					   90, /* load metadata */
					   5, /* search file */
					   5, /* get pkgids */
					   -1);
	}
	if (!ret)
		goto out;

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		state_local = zif_state_get_child (state);
		ret = zif_store_remote_load_metadata (remote, state_local, &error_local);
		if (!ret) {
			g_set_error (error,
				     error_local->domain,
				     error_local->code,
				     "failed to load metadata for %s: %s",
				     remote->priv->id,
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* gets a list of pkgId's that match this file */
	state_local = zif_state_get_child (state);
	filelists = zif_store_remote_get_filelists (remote, error);
	if (filelists == NULL)
		goto out;
	pkgids = zif_md_search_file (filelists,
				     search, state_local, &error_local);
	if (pkgids == NULL) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to load get list of pkgids: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* get primary */
	primary = zif_store_remote_get_primary (remote, error);
	if (primary == NULL)
		goto out;

	/* resolve the pkgId to a set of packages */
	state_local = zif_state_get_child (state);
	array = zif_store_remote_convert_pkgids_to_packages (primary,
							     pkgids,
							     state_local,
							     error);
	if (array == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return array;
}

/**
 * zif_store_remote_is_devel:
 * @store: A #ZifStoreRemote
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Finds out if the repository is a development repository.
 *
 * Return value: %TRUE or %FALSE
 *
 * Since: 0.1.0
 **/
gboolean
zif_store_remote_is_devel (ZifStoreRemote *store, ZifState *state, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (store->priv->id != NULL, FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		ret = zif_store_remote_load (ZIF_STORE (store), state, &error_local);
		if (!ret) {
			g_set_error (error, error_local->domain, error_local->code,
				     "failed to load store file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* do tests */
	if (g_str_has_suffix (store->priv->id, "-debuginfo"))
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
 * @store: A #ZifStoreRemote
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
 * @store: A #ZifStoreRemote
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Get the name of this repository.
 *
 * Return value: The repository name, e.g. "Fedora"
 *
 * Since: 0.1.0
 **/
const gchar *
zif_store_remote_get_name (ZifStoreRemote *store, ZifState *state, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (store->priv->id != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		ret = zif_store_remote_load (ZIF_STORE (store), state, &error_local);
		if (!ret) {
			g_set_error (error, error_local->domain, error_local->code,
				     "failed to load store file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}
out:
	return store->priv->name_expanded;
}

/**
 * zif_store_remote_get_pubkey:
 * @store: A #ZifStoreRemote
 *
 * Get the public key URL for this repository.
 *
 * Return value: The key URL, or %NULL.
 *
 * Since: 0.2.4
 **/
const gchar *
zif_store_remote_get_pubkey (ZifStoreRemote *store)
{
	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (store->priv->id != NULL, NULL);
	return store->priv->pubkey;
}

/**
 * zif_store_remote_get_enabled:
 * @store: A #ZifStoreRemote
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Find out if this repository is enabled in the repo file.
 *
 * NOTE: the repo can still be runtime disabled by using
 * zif_store_set_enabled(store, FALSE)
 *
 * Return value: %TRUE or %FALSE
 *
 * Since: 0.1.0
 **/
gboolean
zif_store_remote_get_enabled (ZifStoreRemote *store, ZifState *state, GError **error)
{
	GError *error_local = NULL;
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (store->priv->id != NULL, FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		ret = zif_store_remote_load (ZIF_STORE (store), state, &error_local);
		if (!ret) {
			g_set_error (error, error_local->domain, error_local->code,
				     "failed to load store file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}
out:
	return store->priv->enabled;
}

/**
 * zif_store_remote_get_files:
 * Return value: (transfer full): An array of strings
 **/
GPtrArray *
zif_store_remote_get_files (ZifStoreRemote *store, ZifPackage *package,
			    ZifState *state, GError **error)
{
	ZifMd *filelists;
	GPtrArray *array = NULL;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	filelists = zif_store_remote_get_filelists (store, error);
	if (filelists == NULL)
		goto out;
	array = zif_md_get_files (filelists, package, state, error);
out:
	return array;
}

/**
 * zif_store_remote_get_requires:
 * Return value: (transfer full): An array of #ZifDepend's, free with g_ptr_array_unref()
 **/
GPtrArray *
zif_store_remote_get_requires (ZifStoreRemote *store, ZifPackage *package,
			       ZifState *state, GError **error)
{
	ZifMd *primary;
	GPtrArray *array = NULL;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	primary = zif_store_remote_get_primary (store, error);
	if (primary == NULL)
		goto out;
	array = zif_md_get_requires (primary, package, state, error);
out:
	return array;
}

/**
 * zif_store_remote_get_provides:
 * Return value: (transfer full): An array of #ZifDepend's, free with g_ptr_array_unref()
 **/
GPtrArray *
zif_store_remote_get_provides (ZifStoreRemote *store, ZifPackage *package,
			    ZifState *state, GError **error)
{
	ZifMd *primary;
	GPtrArray *array = NULL;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	primary = zif_store_remote_get_primary (store, error);
	if (primary == NULL)
		goto out;
	array = zif_md_get_provides (primary, package, state, error);
out:
	return array;
}

/**
 * zif_store_remote_get_obsoletes:
 * Return value: (transfer full): An array of #ZifDepend's, free with g_ptr_array_unref()
 **/
GPtrArray *
zif_store_remote_get_obsoletes (ZifStoreRemote *store, ZifPackage *package,
			        ZifState *state, GError **error)
{
	ZifMd *primary;
	GPtrArray *array = NULL;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	primary = zif_store_remote_get_primary (store, error);
	if (primary == NULL)
		goto out;
	array = zif_md_get_obsoletes (primary, package, state, error);
out:
	return array;
}

/**
 * zif_store_remote_get_conflicts:
 * Return value: (transfer full): An array of #ZifDepend's, free with g_ptr_array_unref()
 **/
GPtrArray *
zif_store_remote_get_conflicts (ZifStoreRemote *store, ZifPackage *package,
			        ZifState *state, GError **error)
{
	ZifMd *primary;
	GPtrArray *array = NULL;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	primary = zif_store_remote_get_primary (store, error);
	if (primary == NULL)
		goto out;
	array = zif_md_get_conflicts (primary, package, state, error);
out:
	return array;
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
	g_free (store->priv->mirrorlist);
	g_strfreev (store->priv->baseurl);
	g_free (store->priv->metalink);
	g_free (store->priv->pubkey);

	store->priv->loaded = FALSE;
	store->priv->loaded_metadata = FALSE;
	store->priv->enabled = FALSE;
	store->priv->id = NULL;
	store->priv->name = NULL;
	store->priv->name_expanded = NULL;
	store->priv->repo_filename = NULL;
	store->priv->baseurl = NULL;
	store->priv->mirrorlist = NULL;
	store->priv->metalink = NULL;
	store->priv->pubkey = NULL;

	g_debug ("store file changed");
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
	g_free (store->priv->repo_filename);
	g_strfreev (store->priv->baseurl);
	g_free (store->priv->mirrorlist);
	g_free (store->priv->metalink);
	g_free (store->priv->pubkey);
	g_free (store->priv->cache_dir);
	g_free (store->priv->repomd_filename);
	g_free (store->priv->directory);

	if (store->priv->file != NULL)
		g_key_file_free (store->priv->file);

	g_object_unref (store->priv->md_other_sql);
	g_object_unref (store->priv->md_primary_sql);
	g_object_unref (store->priv->md_primary_xml);
	g_object_unref (store->priv->md_filelists_sql);
	g_object_unref (store->priv->md_filelists_xml);
	g_object_unref (store->priv->md_comps);
	g_object_unref (store->priv->md_updateinfo);
	g_object_unref (store->priv->md_metalink);
	g_object_unref (store->priv->md_mirrorlist);
	g_object_unref (store->priv->md_delta);
	g_object_unref (store->priv->config);
	g_object_unref (store->priv->monitor);
	g_object_unref (store->priv->lock);
	g_object_unref (store->priv->media);
	g_object_unref (store->priv->groups);
	g_object_unref (store->priv->download);

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
	store_class->search_category = zif_store_remote_search_category;
	store_class->search_details = zif_store_remote_search_details;
	store_class->search_group = zif_store_remote_search_group;
	store_class->search_file = zif_store_remote_search_file;
	store_class->resolve = zif_store_remote_resolve;
	store_class->what_provides = zif_store_remote_what_provides;
	store_class->what_requires = zif_store_remote_what_requires;
	store_class->what_obsoletes = zif_store_remote_what_obsoletes;
	store_class->what_conflicts = zif_store_remote_what_conflicts;
	store_class->get_packages = zif_store_remote_get_packages;
	store_class->find_package = zif_store_remote_find_package;
	store_class->get_categories = zif_store_remote_get_categories;
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
	gchar *cache_dir = NULL;
	guint i;
	GError *error = NULL;
	ZifMd *md;

	store->priv = ZIF_STORE_REMOTE_GET_PRIVATE (store);
	store->priv->config = zif_config_new ();
	store->priv->monitor = zif_monitor_new ();
	store->priv->lock = zif_lock_new ();
	store->priv->media = zif_media_new ();
	store->priv->groups = zif_groups_new ();
	store->priv->download = zif_download_new ();
	store->priv->md_filelists_sql = zif_md_filelists_sql_new ();
	store->priv->md_filelists_xml = zif_md_filelists_xml_new ();
	store->priv->md_other_sql = zif_md_other_sql_new ();
	store->priv->md_primary_sql = zif_md_primary_sql_new ();
	store->priv->md_primary_xml = zif_md_primary_xml_new ();
	store->priv->md_metalink = zif_md_metalink_new ();
	store->priv->md_mirrorlist = zif_md_mirrorlist_new ();
	store->priv->md_comps = zif_md_comps_new ();
	store->priv->md_updateinfo = zif_md_updateinfo_new ();
	store->priv->md_delta = zif_md_delta_new ();
	store->priv->parser_type = ZIF_MD_KIND_UNKNOWN;
	store->priv->parser_section = ZIF_STORE_REMOTE_PARSER_SECTION_UNKNOWN;
	g_signal_connect (store->priv->monitor, "changed", G_CALLBACK (zif_store_remote_file_monitor_cb), store);

	/* get cache */
	cache_dir = zif_config_get_string (store->priv->config, "cachedir", &error);
	if (cache_dir == NULL) {
		g_warning ("failed to get cachedir: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* expand */
	store->priv->cache_dir = zif_config_expand_substitutions (store->priv->config, cache_dir, &error);
	if (store->priv->cache_dir == NULL) {
		g_warning ("failed to get expand substitutions: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* set set parent reference on each repo */
	for (i=1; i<ZIF_MD_KIND_LAST; i++) {
		md = zif_store_remote_get_md_from_type (store, i);
		if (md != NULL)
			zif_md_set_store (md, ZIF_STORE (store));
	}

	/* set download retries */
	store->priv->download_retries = zif_config_get_uint (store->priv->config,
							     "retries", NULL);
out:
	g_free (cache_dir);
}

/**
 * zif_store_remote_new:
 *
 * Return value: A new #ZifStoreRemote instance.
 *
 * Since: 0.1.0
 **/
ZifStore *
zif_store_remote_new (void)
{
	ZifStoreRemote *store;
	store = g_object_new (ZIF_TYPE_STORE_REMOTE, NULL);
	return ZIF_STORE (store);
}

