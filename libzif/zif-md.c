/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2010 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-md
 * @short_description: Metadata file common functionality
 *
 * This provides an abstract metadata class.
 * It is implemented by #ZifMdFilelistsSql, #ZifMdFilelistsXml, #ZifMdPrimaryXml,
 * #ZifMdPrimarySql and many others.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <stdlib.h>

#include "zif-utils.h"
#include "zif-md.h"
#include "zif-config.h"

#define ZIF_MD_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_MD, ZifMdPrivate))

/**
 * ZifMdPrivate:
 *
 * Private #ZifMd data
 **/
struct _ZifMdPrivate
{
	gboolean		 loaded;
	gchar			*id;			/* fedora */
	gchar			*filename;		/* /var/cache/yum/fedora/repo.sqlite.bz2 */
	gchar			*filename_uncompressed;	/* /var/cache/yum/fedora/repo.sqlite */
	guint			 timestamp;
	gchar			*location;		/* repodata/35d817e-primary.sqlite.bz2 */
	gchar			*checksum;		/* of compressed file */
	gchar			*checksum_uncompressed;	/* of uncompressed file */
	GChecksumType		 checksum_type;
	ZifMdKind		 kind;
	ZifStoreRemote		*remote;
	ZifConfig		*config;
	guint64			 max_age;
};

enum {
	PROP_0,
	PROP_KIND,
	PROP_FILENAME,
	PROP_LOCATION,
	PROP_LAST
};

G_DEFINE_TYPE (ZifMd, zif_md, G_TYPE_OBJECT)

/**
 * zif_md_error_quark:
 *
 * Return value: Our personal error quark.
 *
 * Since: 0.1.0
 **/
GQuark
zif_md_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("zif_md_error");
	return quark;
}

/**
 * zif_md_get_is_loaded:
 * @md: the #ZifMd object
 *
 * Gets if the metadata has already been loaded.
 *
 * Return value: %TRUE if the repo is loaded.
 *
 * Since: 0.1.0
 **/
gboolean
zif_md_get_is_loaded (ZifMd *md)
{
	g_return_val_if_fail (ZIF_IS_MD (md), FALSE);
	return md->priv->loaded;
}

/**
 * zif_md_get_id:
 * @md: the #ZifMd object
 *
 * Gets the md identifier, usually the repo name.
 *
 * Return value: the repo id.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_md_get_id (ZifMd *md)
{
	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	return md->priv->id;
}

/**
 * zif_md_get_filename:
 * @md: the #ZifMd object
 *
 * Gets the compressed filename of the repo.
 *
 * Return value: the filename
 *
 * Since: 0.1.0
 **/
const gchar *
zif_md_get_filename (ZifMd *md)
{
	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	return md->priv->filename;
}

/**
 * zif_md_get_location:
 * @md: the #ZifMd object
 *
 * Gets the location of the repo.
 *
 * Return value: the location
 *
 * Since: 0.1.0
 **/
const gchar *
zif_md_get_location (ZifMd *md)
{
	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	return md->priv->location;
}

/**
 * zif_md_get_kind:
 * @md: the #ZifMd object
 *
 * Gets the type of the repo.
 *
 * Return value: the type
 *
 * Since: 0.1.0
 **/
ZifMdKind
zif_md_get_kind (ZifMd *md)
{
	g_return_val_if_fail (ZIF_IS_MD (md), ZIF_MD_KIND_UNKNOWN);
	return md->priv->kind;
}

/**
 * zif_md_get_filename_uncompressed:
 * @md: the #ZifMd object
 *
 * Gets the uncompressed filename of the repo.
 *
 * Return value: the filename
 *
 * Since: 0.1.0
 **/
const gchar *
zif_md_get_filename_uncompressed (ZifMd *md)
{
	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	return md->priv->filename_uncompressed;
}

/**
 * zif_md_set_filename:
 * @md: the #ZifMd object
 * @filename: the base filename, e.g. "master.xml.bz2"
 *
 * Sets the filename of the compressed file.
 *
 * Since: 0.1.0
 **/
void
zif_md_set_filename (ZifMd *md, const gchar *filename)
{
	g_return_if_fail (ZIF_IS_MD (md));
	g_return_if_fail (filename != NULL);

	/* this is the compressed name */
	g_free (md->priv->filename);
	md->priv->filename = g_strdup (filename);

	/* this is the uncompressed name */
	g_free (md->priv->filename_uncompressed);
	md->priv->filename_uncompressed = zif_file_get_uncompressed_name (filename);
}

/**
 * zif_md_set_max_age:
 * @md: the #ZifMd object
 * @max_age: the maximum permitted value of the metadata, or 0
 *
 * Sets the maximum age of the metadata file. Any files older than this will
 * be deleted and re-downloaded.
 *
 * Since: 0.1.0
 **/
void
zif_md_set_max_age (ZifMd *md, guint64 max_age)
{
	g_return_if_fail (ZIF_IS_MD (md));
	md->priv->max_age = max_age;
}

/**
 * zif_md_set_timestamp:
 * @md: the #ZifMd object
 * @timestamp: the timestamp value
 *
 * Sets the timestamp of the compressed file.
 *
 * Since: 0.1.0
 **/
void
zif_md_set_timestamp (ZifMd *md, guint timestamp)
{
	g_return_if_fail (ZIF_IS_MD (md));
	g_return_if_fail (timestamp != 0);

	/* save new value */
	md->priv->timestamp = timestamp;
}

/**
 * zif_md_set_location:
 * @md: the #ZifMd object
 * @location: the location
 *
 * Sets the location of the compressed file, e.g. "repodata/35d817e-primary.sqlite.bz2"
 *
 * Since: 0.1.0
 **/
void
zif_md_set_location (ZifMd *md, const gchar *location)
{
	g_return_if_fail (ZIF_IS_MD (md));
	g_return_if_fail (location != NULL);

	/* save new value */
	g_free (md->priv->location);
	md->priv->location = g_strdup (location);
}

/**
 * zif_md_set_checksum:
 * @md: the #ZifMd object
 * @checksum: the checksum value
 *
 * Sets the checksum of the compressed file.
 *
 * Since: 0.1.0
 **/
void
zif_md_set_checksum (ZifMd *md, const gchar *checksum)
{
	g_return_if_fail (ZIF_IS_MD (md));
	g_return_if_fail (checksum != NULL);

	/* save new value */
	g_free (md->priv->checksum);
	md->priv->checksum = g_strdup (checksum);
}

/**
 * zif_md_set_checksum_uncompressed:
 * @md: the #ZifMd object
 * @checksum_uncompressed: the uncompressed checksum value
 *
 * Sets the checksum of the uncompressed file.
 *
 * Since: 0.1.0
 **/
void
zif_md_set_checksum_uncompressed (ZifMd *md, const gchar *checksum_uncompressed)
{
	g_return_if_fail (ZIF_IS_MD (md));
	g_return_if_fail (checksum_uncompressed != NULL);

	/* save new value */
	g_free (md->priv->checksum_uncompressed);
	md->priv->checksum_uncompressed = g_strdup (checksum_uncompressed);
}

/**
 * zif_md_set_checksum_type:
 * @md: the #ZifMd object
 * @checksum_type: the checksum type
 *
 * Sets the checksum_type of the files.
 *
 * Since: 0.1.0
 **/
void
zif_md_set_checksum_type (ZifMd *md, GChecksumType checksum_type)
{
	g_return_if_fail (ZIF_IS_MD (md));

	/* save new value */
	md->priv->checksum_type = checksum_type;
}

/**
 * zif_md_set_id:
 * @md: the #ZifMd object
 * @id: the repository id, e.g. "fedora"
 *
 * Sets the repository ID for this metadata.
 *
 * Since: 0.1.0
 **/
void
zif_md_set_id (ZifMd *md, const gchar *id)
{
	g_return_if_fail (ZIF_IS_MD (md));
	g_return_if_fail (id != NULL);

	g_free (md->priv->id);
	md->priv->id = g_strdup (id);
}

/**
 * zif_md_set_store_remote:
 * @md: the #ZifMd object
 * @remote: the #ZifStoreRemote that created this metadata object
 *
 * Sets the remote store for this metadata.
 *
 * Since: 0.1.0
 **/
void
zif_md_set_store_remote (ZifMd *md, ZifStoreRemote *remote)
{
	g_return_if_fail (ZIF_IS_MD (md));
	g_return_if_fail (remote != NULL);

	/* do not take a reference, else the parent never goes away */
	md->priv->remote = remote;
}

/**
 * zif_md_get_store_remote:
 * @md: the #ZifMd object
 *
 * Gets the remote store for this metadata.
 *
 * Return value: A #ZifStoreRemote or %NULL for unset
 *
 * Since: 0.1.0
 **/
ZifStoreRemote *
zif_md_get_store_remote (ZifMd *md)
{
	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	return md->priv->remote;
}

/**
 * zif_md_delete_file:
 **/
static gboolean
zif_md_delete_file (const gchar *filename)
{
	gint retval;
	gboolean ret;

	/* file exists? */
	ret = g_file_test (filename, G_FILE_TEST_EXISTS);
	if (!ret)
		goto out;

	g_debug ("deleting %s", filename);

	/* remove */
	retval = g_unlink (filename);
	if (retval != 0) {
		g_warning ("failed to delete %s", filename);
		ret = FALSE;
	}
out:
	return ret;
}

/**
 * zif_md_load:
 * @md: the #ZifMd object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Load the metadata store.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_md_load (ZifMd *md, ZifState *state, GError **error)
{
	gboolean ret;
	gboolean uncompressed_check;
	gchar *dirname = NULL;
	GError *error_local = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);
	ZifState *state_local;
	ZifState *state_repair;

	g_return_val_if_fail (ZIF_IS_MD (md), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no support */
	if (klass->load == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "operation cannot be performed on md type %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)));
		return FALSE;
	}

	/* set steps:
	 *
	 * 1. check uncompressed
	 * 2. check compressed
	 * 3. get new compressed
	 * 3. nothing
	 * 4. decompress compressed
	 * 5. check compressed
	 * 6. klass->load
	 */
	zif_state_set_number_steps (state, 6);

	/* optimise: if uncompressed file is okay, then don't even check the compressed file */
	state_local = zif_state_get_child (state);
	uncompressed_check = zif_md_file_check (md, TRUE, state_local, &error_local);
	if (uncompressed_check) {
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
		goto skip_compressed_check;
	}

	/* display any warning */
	g_debug ("failed checksum for uncompressed: %s", error_local->message);
	g_clear_error (&error_local);
	zif_state_reset (state_local);

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* check compressed file */
	state_local = zif_state_get_child (state);
	ret = zif_md_file_check (md, FALSE, state_local, &error_local);
	if (!ret) {

		/* this one really is fatal */
		if (g_strstr_len (error_local->message, -1, "no filename") != NULL) {
			g_propagate_error (error, error_local);
			goto out;
		}

		g_debug ("failed checksum for compressed: %s", error_local->message);
		g_clear_error (&error_local);

		/* repair this, as we want to continue */
		ret = zif_state_finished (state_local, error);
		if (!ret)
			goto out;

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;

		/* set steps:
		 *
		 * 1. download new repomd
		 * 2. load the new repomd
		 * 3. download new compressed repo file
		 * 4. check compressed file against new repomd
		 */
		state_local = zif_state_get_child (state);
		zif_state_set_number_steps (state_local, 4);

		/* if not online, then this is fatal */
		ret = zif_config_get_boolean (md->priv->config, "network", NULL);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_AS_OFFLINE,
				     "failed to check %s checksum for %s and offline",
				     zif_md_kind_to_text (md->priv->kind), md->priv->id);
			goto out;
		}

		/* reget repomd in case it's changed */
		g_debug ("regetting repomd as checksum was invalid");
		state_repair = zif_state_get_child (state_local);
		ret = zif_store_remote_download_repomd (md->priv->remote, state_repair, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_DOWNLOAD,
				     "failed to download repomd after failing checksum: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;

		/* reload new data */
		state_repair = zif_state_get_child (state_local);
		ret = zif_store_load (ZIF_STORE (md->priv->remote), state_repair, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_DOWNLOAD,
				     "failed to load repomd after downloading new copy: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;

		/* delete file if it exists */
		zif_md_delete_file (md->priv->filename);

		/* download file */
		state_repair = zif_state_get_child (state_local);
		dirname = g_path_get_dirname (md->priv->filename);
		ret = zif_store_remote_download (md->priv->remote, md->priv->location, dirname, state_repair, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_DOWNLOAD,
				     "failed to download missing compressed file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;

		/* check newly downloaded compressed file */
		state_repair = zif_state_get_child (state_local);
		ret = zif_md_file_check (md, FALSE, state_repair, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
				     "failed checksum on downloaded file: %s", error_local->message);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	} else {
		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* delete uncompressed file if it exists */
	zif_md_delete_file (md->priv->filename_uncompressed);

	/* decompress file */
	g_debug ("decompressing file");
	state_local = zif_state_get_child (state);
	ret = zif_file_decompress (md->priv->filename, md->priv->filename_uncompressed,
				   state_local, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
			     "failed to decompress: %s", error_local->message);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* check newly uncompressed file */
	state_local = zif_state_get_child (state);
	ret = zif_md_file_check (md, TRUE, state_local, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
			     "failed checksum on decompressed file: %s", error_local->message);
		goto out;
	}

skip_compressed_check:

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* do subclassed load */
	state_local = zif_state_get_child (state);
	ret = klass->load (md, state_local, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* all okay */
	md->priv->loaded = TRUE;
out:
	g_free (dirname);
	return ret;
}

/**
 * zif_md_unload:
 * @md: the #ZifMd object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Unload the metadata store.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_md_unload (ZifMd *md, ZifState *state, GError **error)
{
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no support */
	if (klass->unload == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "operation cannot be performed on md type %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)));
		return FALSE;
	}

	return klass->unload (md, state, error);
}

/**
 * zif_md_resolve:
 * @md: the #ZifMd object
 * @search: the search term, e.g. "gnome-power-manager"
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all remote packages that match the name exactly.
 *
 * Return value: an array of #ZifPackageRemote's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_md_resolve (ZifMd *md, gchar **search, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->resolve == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "operation cannot be performed on md type %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)));
		goto out;
	}

	/* do subclassed action */
	array = klass->resolve (md, search, state, error);
out:
	return array;
}

/**
 * zif_md_search_file:
 * @md: the #ZifMd object
 * @search: the search term, e.g. "/usr/bin/powertop"
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets a list of all packages that contain the file.
 * Results are pkgId's descriptors, i.e. 64 bit hashes as test.
 *
 * Return value: a string list of pkgId's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_md_search_file (ZifMd *md, gchar **search, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->search_file == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "operation cannot be performed on md type %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)));
		goto out;
	}

	/* do subclassed action */
	array = klass->search_file (md, search, state, error);
out:
	return array;
}

/**
 * zif_md_search_name:
 * @md: the #ZifMd object
 * @search: the search term, e.g. "power"
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all packages that match the name.
 *
 * Return value: an array of #ZifPackageRemote's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_md_search_name (ZifMd *md, gchar **search, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->search_name == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "operation cannot be performed on md type %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)));
		goto out;
	}

	/* do subclassed action */
	array = klass->search_name (md, search, state, error);
out:
	return array;
}

/**
 * zif_md_search_details:
 * @md: the #ZifMd object
 * @search: the search term, e.g. "advanced"
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all packages that match the name or description.
 *
 * Return value: an array of #ZifPackageRemote's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_md_search_details (ZifMd *md, gchar **search, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->search_details == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "operation cannot be performed on md type %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)));
		goto out;
	}

	/* do subclassed action */
	array = klass->search_details (md, search, state, error);
out:
	return array;
}

/**
 * zif_md_search_group:
 * @md: the #ZifMd object
 * @search: the search term, e.g. "games/console"
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all packages that match the group.
 *
 * Return value: an array of #ZifPackageRemote's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_md_search_group (ZifMd *md, gchar **search, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->search_group == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "operation cannot be performed on md type %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)));
		goto out;
	}

	/* do subclassed action */
	array = klass->search_group (md, search, state, error);
out:
	return array;
}

/**
 * zif_md_search_pkgid:
 * @md: the #ZifMd object
 * @search: the search term as a 64 bit hash
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all packages that match the given pkgId.
 *
 * Return value: an array of #ZifPackageRemote's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_md_search_pkgid (ZifMd *md, gchar **search, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->search_pkgid == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "operation cannot be performed on md type %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)));
		goto out;
	}

	/* do subclassed action */
	array = klass->search_pkgid (md, search, state, error);
out:
	return array;
}

/**
 * zif_md_what_provides:
 * @md: the #ZifMd object
 * @depend: the #ZifDepend provide
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all packages that match the given provide.
 *
 * Return value: an array of #ZifPackageRemote's
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_md_what_provides (ZifMd *md, ZifDepend *depend,
		      ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (ZIF_IS_DEPEND (depend), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->what_provides == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "operation cannot be performed on md type %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)));
		goto out;
	}

	/* do subclassed action */
	array = klass->what_provides (md, depend, state, error);
out:
	return array;
}

/**
 * zif_md_what_obsoletes:
 * @md: the #ZifMd object
 * @depend: the #ZifDepend obsolete
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all packages that obsolete the given provide.
 *
 * Return value: an array of #ZifPackageRemote's
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_md_what_obsoletes (ZifMd *md, ZifDepend *depend,
		       ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (ZIF_IS_DEPEND (depend), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->what_obsoletes == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "operation cannot be performed on md type %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)));
		goto out;
	}

	/* do subclassed action */
	array = klass->what_obsoletes (md, depend, state, error);
out:
	return array;
}

/**
 * zif_md_what_conflicts:
 * @md: the #ZifMd object
 * @depend: the #ZifDepend conflict
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all packages that conflict with the given depend.
 *
 * Return value: an array of #ZifPackageRemote's
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_md_what_conflicts (ZifMd *md, ZifDepend *depend,
		       ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (ZIF_IS_DEPEND (depend), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->what_conflicts == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "operation cannot be performed on md type %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)));
		goto out;
	}

	/* do subclassed action */
	array = klass->what_conflicts (md, depend, state, error);
out:
	return array;
}

/**
 * zif_md_find_package:
 * @md: the #ZifMd object
 * @package_id: the PackageId to match
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all packages that match PackageId.
 *
 * Return value: an array of #ZifPackageRemote's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_md_find_package (ZifMd *md, const gchar *package_id, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->find_package == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "operation cannot be performed on md type %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)));
		goto out;
	}

	/* do subclassed action */
	array = klass->find_package (md, package_id, state, error);
out:
	return array;
}

/**
 * zif_md_get_changelog:
 * @md: the #ZifMd object
 * @pkgid: the internal pkgid to match
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the changelog data for a specific package
 *
 * Return value: an array of #ZifChangeset's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_md_get_changelog (ZifMd *md, const gchar *pkgid, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->get_changelog == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "operation cannot be performed on md type %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)));
		goto out;
	}

	/* do subclassed action */
	array = klass->get_changelog (md, pkgid, state, error);
out:
	return array;
}

/**
 * zif_md_get_files:
 * @md: the #ZifMd object
 * @package: the %ZifPackage
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the file list for a specific package.
 *
 * Return value: an array of strings, free with g_ptr_array_unref()
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_md_get_files (ZifMd *md, ZifPackage *package, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->get_files == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "get-files operation cannot be performed on md type %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)));
		goto out;
	}

	/* do subclassed action */
	array = klass->get_files (md, package, state, error);
out:
	return array;
}

/**
 * zif_md_get_provides:
 * @md: the #ZifMd object
 * @package: the %ZifPackage
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the provides for a specific package.
 *
 * Return value: an array of #ZifDepend's, free with g_ptr_array_unref()
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_md_get_provides (ZifMd *md, ZifPackage *package, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->get_provides == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "get provides operation cannot be performed on md type %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)));
		goto out;
	}

	/* do subclassed action */
	array = klass->get_provides (md, package, state, error);
out:
	return array;
}

/**
 * zif_md_get_requires:
 * @md: the #ZifMd object
 * @package: the %ZifPackage
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the requires for a specific package.
 *
 * Return value: an array of #ZifDepend's, free with g_ptr_array_unref()
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_md_get_requires (ZifMd *md, ZifPackage *package, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->get_requires == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "get requires operation cannot be performed on md type %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)));
		goto out;
	}

	/* do subclassed action */
	array = klass->get_requires (md, package, state, error);
out:
	return array;
}

/**
 * zif_md_get_obsoletes:
 * @md: the #ZifMd object
 * @package: the %ZifPackage
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the obsoletes for a specific package.
 *
 * Return value: an array of #ZifDepend's, free with g_ptr_array_unref()
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_md_get_obsoletes (ZifMd *md, ZifPackage *package, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->get_obsoletes == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "get obsoletes operation cannot be performed on md type %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)));
		goto out;
	}

	/* do subclassed action */
	array = klass->get_obsoletes (md, package, state, error);
out:
	return array;
}

/**
 * zif_md_get_conflicts:
 * @md: the #ZifMd object
 * @package: the %ZifPackage
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the conflicts for a specific package.
 *
 * Return value: an array of #ZifDepend's, free with g_ptr_array_unref()
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_md_get_conflicts (ZifMd *md, ZifPackage *package, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->get_conflicts == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "get conflicts operation cannot be performed on md type %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)));
		goto out;
	}

	/* do subclassed action */
	array = klass->get_conflicts (md, package, state, error);
out:
	return array;
}

/**
 * zif_md_get_packages:
 * @md: the #ZifMd object
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Returns all packages in the repo.
 *
 * Return value: an array of #ZifPackageRemote's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_md_get_packages (ZifMd *md, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->get_packages == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "operation cannot be performed on md type %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)));
		goto out;
	}

	/* do subclassed action */
	array = klass->get_packages (md, state, error);
out:
	return array;
}

/**
 * zif_md_clean:
 * @md: the #ZifMd object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Clean the metadata store.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_md_clean (ZifMd *md, GError **error)
{
	gboolean ret = FALSE;
	gboolean exists;
	const gchar *filename;
	GFile *file;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_MD (md), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get filename */
	filename = zif_md_get_filename (md);
	if (filename == NULL) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_NO_FILENAME,
			     "failed to get filename for %s", zif_md_kind_to_text (md->priv->kind));
		ret = FALSE;
		goto out;
	}

	/* file does not exist */
	exists = g_file_test (filename, G_FILE_TEST_EXISTS);
	if (exists) {
		file = g_file_new_for_path (filename);
		ret = g_file_delete (file, NULL, &error_local);
		g_object_unref (file);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
				     "failed to delete metadata file %s: %s", filename, error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* get filename */
	filename = zif_md_get_filename_uncompressed (md);
	if (filename == NULL) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_NO_FILENAME,
			     "failed to get uncompressed filename for %s", zif_md_kind_to_text (md->priv->kind));
		ret = FALSE;
		goto out;
	}

	/* file does not exist */
	exists = g_file_test (filename, G_FILE_TEST_EXISTS);
	if (exists) {
		file = g_file_new_for_path (filename);
		ret = g_file_delete (file, NULL, &error_local);
		g_object_unref (file);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
				     "failed to delete metadata file %s: %s", filename, error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* okay */
	ret = TRUE;
out:
	return ret;
}

/**
 * zif_md_kind_to_text:
 *
 * Since: 0.1.0
 **/
const gchar *
zif_md_kind_to_text (ZifMdKind type)
{
	if (type == ZIF_MD_KIND_FILELISTS_XML)
		return "filelists";
	if (type == ZIF_MD_KIND_FILELISTS_SQL)
		return "filelists_db";
	if (type == ZIF_MD_KIND_PRIMARY_XML)
		return "primary";
	if (type == ZIF_MD_KIND_PRIMARY_SQL)
		return "primary_db";
	if (type == ZIF_MD_KIND_OTHER_XML)
		return "other";
	if (type == ZIF_MD_KIND_OTHER_SQL)
		return "other_db";
	if (type == ZIF_MD_KIND_COMPS)
		return "group";
	if (type == ZIF_MD_KIND_COMPS_GZ)
		return "group_gz";
	if (type == ZIF_MD_KIND_METALINK)
		return "metalink";
	if (type == ZIF_MD_KIND_MIRRORLIST)
		return "mirrorlist";
	if (type == ZIF_MD_KIND_PRESTODELTA)
		return "prestodelta";
	if (type == ZIF_MD_KIND_UPDATEINFO)
		return "updateinfo";
	if (type == ZIF_MD_KIND_PKGTAGS)
		return "pkgtags";
	return "unknown";
}

/**
 * zif_md_file_check:
 * @md: the #ZifMd object
 * @use_uncompressed: If we should check only the uncompresed version
 * @state: a #ZifState to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Check the metadata files to make sure they are valid.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.1.0
 **/
gboolean
zif_md_file_check (ZifMd *md, gboolean use_uncompressed, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	GFile *file = NULL;
	GFileInfo *file_info = NULL;
	GError *error_local = NULL;
	gchar *data = NULL;
	gchar *checksum = NULL;
	const gchar *filename;
	const gchar *checksum_wanted;
	gsize length;
	GCancellable *cancellable;
	guint64 modified, age;

	g_return_val_if_fail (ZIF_IS_MD (md), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (md->priv->id != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* setup state */
	zif_state_set_number_steps (state, 2);

	/* metalink has no checksum... */
	if (md->priv->kind == ZIF_MD_KIND_METALINK ||
	    md->priv->kind == ZIF_MD_KIND_MIRRORLIST) {
		g_debug ("skipping checksum check on %s", zif_md_kind_to_text (md->priv->kind));
		ret = zif_state_finished (state, error);
		goto out;
	}

	/* get correct filename */
	if (use_uncompressed)
		filename = md->priv->filename_uncompressed;
	else
		filename = md->priv->filename;

	/* no checksum set */
	if (filename == NULL) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
			     "no filename for %s [%s]", md->priv->id, zif_md_kind_to_text (md->priv->kind));
		ret = FALSE;
		goto out;
	}

	/* get file attributes */
	file = g_file_new_for_path (filename);
	cancellable = zif_state_get_cancellable (state);
	file_info = g_file_query_info (file, G_FILE_ATTRIBUTE_TIME_MODIFIED, G_FILE_QUERY_INFO_NONE, cancellable, &error_local);
	if (file_info == NULL) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
			     "failed to get file information of %s: %s", filename, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* check age */
	modified = g_file_info_get_attribute_uint64 (file_info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
	age = time (NULL) - modified;
	g_debug ("age of %s is %" G_GUINT64_FORMAT " hours (max-age=%" G_GUINT64_FORMAT " seconds)",
		   filename, age / (60 * 60), md->priv->max_age);
	ret = (md->priv->max_age == 0 || age < md->priv->max_age);
	if (!ret) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FILE_TOO_OLD,
			     "data is too old: %s", filename);
		goto out;
	}

	/* set action */
	zif_state_action_start (state, ZIF_STATE_ACTION_CHECKING, filename);

	/* get contents */
	ret = g_file_load_contents (file, cancellable, &data, &length, NULL, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
			     "failed to get contents of %s: %s", filename, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* get the one we want */
	if (use_uncompressed)
		checksum_wanted = md->priv->checksum_uncompressed;
	else
		checksum_wanted = md->priv->checksum;

	/* no checksum set */
	if (checksum_wanted == NULL) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
			     "checksum not set for %s", filename);
		ret = FALSE;
		goto out;
	}

	/* compute checksum */
	zif_state_set_allow_cancel (state, FALSE);
	checksum = g_compute_checksum_for_data (md->priv->checksum_type, (guchar*) data, length);

	/* matches? */
	ret = (g_strcmp0 (checksum, checksum_wanted) == 0);
	if (!ret) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
			     "checksum incorrect, wanted %s, got %s for %s", checksum_wanted, checksum, filename);
		goto out;
	}
	g_debug ("%s checksum correct (%s)", filename, checksum_wanted);

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	if (file != NULL)
		g_object_unref (file);
	if (file_info != NULL)
		g_object_unref (file_info);
	g_free (data);
	g_free (checksum);
	return ret;
}

/**
 * zif_md_get_property:
 **/
static void
zif_md_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	ZifMd *md = ZIF_MD (object);
	ZifMdPrivate *priv = md->priv;

	switch (prop_id) {
	case PROP_KIND:
		g_value_set_uint (value, priv->kind);
		break;
	case PROP_FILENAME:
		g_value_set_string (value, priv->filename);
		break;
	case PROP_LOCATION:
		g_value_set_string (value, priv->location);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * zif_md_set_property:
 **/
static void
zif_md_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	ZifMd *md = ZIF_MD (object);
	ZifMdPrivate *priv = md->priv;

	switch (prop_id) {
	case PROP_KIND:
		priv->kind = g_value_get_uint (value);
		break;
	case PROP_FILENAME:
		zif_md_set_filename (md, g_value_get_string (value));
		break;
	case PROP_LOCATION:
		zif_md_set_location (md, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * zif_md_finalize:
 **/
static void
zif_md_finalize (GObject *object)
{
	ZifMd *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_MD (object));
	md = ZIF_MD (object);

	g_free (md->priv->id);
	g_free (md->priv->filename);
	g_free (md->priv->filename_uncompressed);
	g_free (md->priv->location);
	g_free (md->priv->checksum);
	g_free (md->priv->checksum_uncompressed);

	g_object_unref (md->priv->config);

	G_OBJECT_CLASS (zif_md_parent_class)->finalize (object);
}

/**
 * zif_md_class_init:
 **/
static void
zif_md_class_init (ZifMdClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_md_finalize;
	object_class->get_property = zif_md_get_property;
	object_class->set_property = zif_md_set_property;

	/**
	 * ZifUpdate:kind:
	 *
	 * Since: 0.1.3
	 */
	pspec = g_param_spec_uint ("kind", NULL, NULL,
				   ZIF_MD_KIND_UNKNOWN, G_MAXUINT, 0,
				   G_PARAM_CONSTRUCT | G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_KIND, pspec);

	/**
	 * ZifUpdate:filename:
	 *
	 * Since: 0.1.3
	 */
	pspec = g_param_spec_string ("filename", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_FILENAME, pspec);

	/**
	 * ZifUpdate:location:
	 *
	 * Since: 0.1.3
	 */
	pspec = g_param_spec_string ("location", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_LOCATION, pspec);

	g_type_class_add_private (klass, sizeof (ZifMdPrivate));
}

/**
 * zif_md_init:
 **/
static void
zif_md_init (ZifMd *md)
{
	md->priv = ZIF_MD_GET_PRIVATE (md);
	md->priv->kind = ZIF_MD_KIND_UNKNOWN;
	md->priv->loaded = FALSE;
	md->priv->id = NULL;
	md->priv->filename = NULL;
	md->priv->timestamp = 0;
	md->priv->location = NULL;
	md->priv->checksum = NULL;
	md->priv->checksum_uncompressed = NULL;
	md->priv->checksum_type = 0;
	md->priv->max_age = 0;
	md->priv->remote = NULL;
	md->priv->config = zif_config_new ();
}

/**
 * zif_md_new:
 *
 * Return value: A new #ZifMd class instance.
 *
 * Since: 0.1.0
 **/
ZifMd *
zif_md_new (void)
{
	ZifMd *md;
	md = g_object_new (ZIF_TYPE_MD, NULL);
	return ZIF_MD (md);
}

