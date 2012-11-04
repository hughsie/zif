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
 * @short_description: Metadata base class
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
#include <sys/types.h>
#include <attr/xattr.h>

#include "zif-config.h"
#include "zif-md.h"
#include "zif-state-private.h"
#include "zif-store-remote-private.h"
#include "zif-utils.h"

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
	ZifStore		*store;
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
 * Return value: An error quark.
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
 * @md: A #ZifMd
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
 * @md: A #ZifMd
 *
 * Gets the md identifier, usually the repo name.
 *
 * Return value: The repo identifier.
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
 * @md: A #ZifMd
 *
 * Gets the compressed filename of the repo.
 *
 * Return value: The filename, e.g. "/var/cache/dave.xml.bz2"
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
 * @md: A #ZifMd
 *
 * Gets the location of the repo.
 *
 * Return value: The location
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
 * @md: A #ZifMd
 *
 * Gets the type of the repo.
 *
 * Return value: The type
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
 * @md: A #ZifMd
 *
 * Gets the uncompressed filename of the repo.
 *
 * Return value: The uncompressed filename, e.g. "/var/cache/dave.xml"
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
 * @md: A #ZifMd
 * @filename: The base filename, e.g. "master.xml.bz2"
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

	/* the same */
	if (g_strcmp0 (md->priv->filename, filename) == 0)
		return;

	/* this is the compressed name */
	g_free (md->priv->filename);
	md->priv->filename = g_strdup (filename);

	/* this is the uncompressed name */
	g_free (md->priv->filename_uncompressed);
	md->priv->filename_uncompressed = zif_file_get_uncompressed_name (filename);
}

/**
 * zif_md_set_max_age:
 * @md: A #ZifMd
 * @max_age: The maximum permitted value of the metadata, or 0
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
 * @md: A #ZifMd
 * @timestamp: The timestamp value
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
 * @md: A #ZifMd
 * @location: The location
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

	/* the same */
	if (g_strcmp0 (md->priv->location, location) == 0)
		return;

	/* save new value */
	g_free (md->priv->location);
	md->priv->location = g_strdup (location);
}

/**
 * zif_md_set_checksum:
 * @md: A #ZifMd
 * @checksum: The checksum value
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
 * @md: A #ZifMd
 * @checksum_uncompressed: The uncompressed checksum value
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
 * @md: A #ZifMd
 * @checksum_type: The checksum type
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
 * @md: A #ZifMd
 * @id: The repository id, e.g. "fedora"
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

	/* the same */
	if (g_strcmp0 (md->priv->id, id) == 0)
		return;

	g_free (md->priv->id);
	md->priv->id = g_strdup (id);
}

/**
 * zif_md_set_store:
 * @md: A #ZifMd
 * @remote: The #ZifStore that created this metadata
 *
 * Sets the remote store for this metadata.
 *
 * Since: 0.2.1
 **/
void
zif_md_set_store (ZifMd *md, ZifStore *store)
{
	g_return_if_fail (ZIF_IS_MD (md));
	g_return_if_fail (store != NULL);

	/* do not take a reference, else the parent never goes away */
	md->priv->store = store;
}

/**
 * zif_md_get_store:
 * @md: A #ZifMd
 *
 * Gets the remote store for this metadata.
 *
 * Return value: (transfer none): A #ZifStore or %NULL for unset
 *
 * Since: 0.2.1
 **/
ZifStore *
zif_md_get_store (ZifMd *md)
{
	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	return md->priv->store;
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
 * zif_md_load_get_repomd_and_download:
 **/
static gboolean
zif_md_load_get_repomd_and_download (ZifMd *md, ZifState *state, GError **error)
{
	const gchar *content_type;
	const gchar *location;
	gboolean ret;
	gchar *dirname = NULL;
	GError *error_local = NULL;
	ZifState *state_local;

	/* set steps */
	ret = zif_state_set_steps (state,
				   error,
				   5, /* download new repomd */
				   2, /* load the new repomd */
				   90, /* download new compressed repo file */
				   3, /* check compressed file against new repomd */
				   -1);
	if (!ret)
		goto out;

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
	state_local = zif_state_get_child (state);
	ret = zif_store_remote_download_repomd (ZIF_STORE_REMOTE (md->priv->store),
						state_local,
						&error_local);
	if (!ret) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_DOWNLOAD,
			     "failed to download repomd after failing checksum: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* reload new data */
	state_local = zif_state_get_child (state);
	ret = zif_store_load (md->priv->store, state_local, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_DOWNLOAD,
			     "failed to load repomd after downloading new copy: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* delete file if it exists */
	zif_md_delete_file (md->priv->filename);

	/* This local copy is working around a suspected compiler bug
	 * where md->priv->location is assumed to not change for the
	 * entire scope of this function.
	 *
	 * Downloading a new repomd probably means different metadata
	 * files, which also means a sure call to zif_md_set_location().
	 *
	 * Accessing the old (freed, and invalid) pointer at
	 * md->priv->location will result in a possible crash, and a
	 * 'Invalid read of size 1' in valgrind.
	 *
	 * By *forcing* the compiler to re-get location rather than use
	 * the invalid register copy we ensure the new location is used.
	 * Phew! */
	location = zif_md_get_location (md);
	content_type = zif_guess_content_type (location);

	/* download file */
	state_local = zif_state_get_child (state);
	dirname = g_path_get_dirname (md->priv->filename);
	ret = zif_store_remote_download_full (ZIF_STORE_REMOTE (md->priv->store),
					      location,
					      dirname,
					      0,
					      content_type,
					      G_CHECKSUM_MD5,
					      NULL,
					      state_local,
					      &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_FAILED_DOWNLOAD,
			     "failed to download missing compressed file: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* check newly downloaded compressed file */
	state_local = zif_state_get_child (state);
	ret = zif_md_check_compressed (md,
				       state_local,
				       &error_local);
	if (!ret) {
		g_propagate_error (error, error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	g_free (dirname);
	return ret;
}

/**
 * zif_md_load_check_and_get_compressed:
 **/
static gboolean
zif_md_load_check_and_get_compressed (ZifMd *md, ZifState *state, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	ZifState *state_local;

	/* set steps */
	ret = zif_state_set_steps (state,
				   error,
				   10, /* check compressed */
				   60, /* get new compressed */
				   10, /* decompress compressed */
				   20, /* check uncompressed */
				   -1);
	if (!ret)
		goto out;

	/* check compressed file */
	state_local = zif_state_get_child (state);
	ret = zif_md_check_compressed (md,
				       state_local,
				       &error_local);
	if (!ret) {
		if (error_local->domain == ZIF_MD_ERROR &&
		    (error_local->code == ZIF_MD_ERROR_CHECKSUM_INVALID ||
		     error_local->code == ZIF_MD_ERROR_FILE_TOO_OLD ||
		     error_local->code == ZIF_MD_ERROR_FILE_NOT_EXISTS)) {
			g_debug ("ignoring %s and regetting repomd",
				 error_local->message);
			g_clear_error (&error_local);

			/* fake */
			ret = zif_state_finished (state_local, error);
			if (!ret)
				goto out;

			/* this section done */
			ret = zif_state_done (state, error);
			if (!ret)
				goto out;

			/* failed checksum, likely the repomd is out of date too */
			state_local = zif_state_get_child (state);
			ret = zif_md_load_get_repomd_and_download (md,
								   state_local,
								   error);
			if (!ret)
				goto out;
		} else {
			g_debug ("pushing %i %s", error_local->code, error_local->message);
			g_propagate_error (error, error_local);
			goto out;
		}
	} else {
		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* delete uncompressed file if it exists */
	zif_md_delete_file (md->priv->filename_uncompressed);

	/* decompress file */
	g_debug ("decompressing file");
	state_local = zif_state_get_child (state);
	ret = zif_file_decompress (md->priv->filename,
				   md->priv->filename_uncompressed,
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
	ret = zif_md_check_uncompressed (md,
				         state_local,
				         error);
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
 * zif_md_load:
 * @md: A #ZifMd
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Load the metadata store.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_md_load (ZifMd *md, ZifState *state, GError **error)
{
	gboolean ret;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);
	ZifState *state_local;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_MD (md), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (klass != NULL, FALSE);

	/* no support */
	if (klass->load == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "operation cannot be performed on md type %s",
			     zif_md_kind_to_text (zif_md_get_kind (md)));
		return FALSE;
	}

	/* set steps */
	ret = zif_state_set_steps (state,
				   error,
				   20, /* check uncompressed */
				   60, /* get if not valid */
				   20, /* klass->load */
				   -1);
	if (!ret)
		goto out;

	/* optimise: if uncompressed file is okay, then don't even check the compressed file */
	state_local = zif_state_get_child (state);
	ret = zif_md_check_uncompressed (md,
					 state_local,
					 &error_local);
	if (!ret) {
		if (error_local->domain == ZIF_MD_ERROR &&
		    (error_local->code == ZIF_MD_ERROR_CHECKSUM_INVALID ||
		     error_local->code == ZIF_MD_ERROR_FILE_TOO_OLD ||
		     error_local->code == ZIF_MD_ERROR_FILE_NOT_EXISTS)) {
			g_debug ("ignoring %s and regetting repomd",
				 error_local->message);
			g_clear_error (&error_local);

			/* fake */
			ret = zif_state_finished (state_local, error);
			if (!ret)
				goto out;

			/* this section done */
			ret = zif_state_done (state, error);
			if (!ret)
				goto out;

			/* failed checksum, likely the repomd is out of date too */
			state_local = zif_state_get_child (state);
			ret = zif_md_load_check_and_get_compressed (md,
								    state_local,
								    error);
			if (!ret)
				goto out;
		} else {
			g_propagate_error (error, error_local);
			goto out;
		}
	} else {
		/* done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* done */
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
	return ret;
}

/**
 * zif_md_unload:
 * @md: A #ZifMd
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Unload the metadata store.
 *
 * Return value: %TRUE for success, %FALSE otherwise
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
	g_return_val_if_fail (klass != NULL, FALSE);

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
 * zif_md_resolve_full:
 * @md: A #ZifMd
 * @search: Search term, e.g. "gnome-power-manager"
 * @flags: A bitfield of %ZifStoreResolveFlags, e.g. %ZIF_STORE_RESOLVE_FLAG_USE_NAME_ARCH
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Finds all remote packages that match the name exactly.
 *
 * Return value: (element-type ZifPackageRemote) (transfer container): An array of #ZifPackageRemote's
 *
 * Since: 0.2.4
 **/
GPtrArray *
zif_md_resolve_full (ZifMd *md,
		     gchar **search,
		     ZifStoreResolveFlags flags,
		     ZifState *state,
		     GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (klass != NULL, NULL);

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
	array = klass->resolve (md, search, flags, state, error);
out:
	return array;
}

/**
 * zif_md_resolve:
 * @md: A #ZifMd
 * @search: Search term, e.g. "gnome-power-manager"
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Finds all remote packages that match the name exactly.
 *
 * Return value: (element-type ZifPackageRemote) (transfer container): An array of #ZifPackageRemote's
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_md_resolve (ZifMd *md,
		gchar **search,
		ZifState *state,
		GError **error)
{
	return zif_md_resolve_full (md,
				    search,
				    ZIF_STORE_RESOLVE_FLAG_USE_NAME,
				    state,
				    error);
}

/**
 * zif_md_search_file:
 * @md: A #ZifMd
 * @search: Search term, e.g. "/usr/bin/powertop"
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Gets a list of all packages that contain the file.
 * Results are pkgId's descriptors, i.e. 64 bit hashes as test.
 *
 * Return value: (element-type utf8) (transfer container): A string list of pkgId's
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
	g_return_val_if_fail (klass != NULL, NULL);

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
 * @md: A #ZifMd
 * @search: Search term, e.g. "power"
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Finds all packages that match the name.
 *
 * Return value: (element-type ZifPackageRemote) (transfer container): An array of #ZifPackageRemote's
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
	g_return_val_if_fail (klass != NULL, NULL);

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
 * @md: A #ZifMd
 * @search: Search term, e.g. "advanced"
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Finds all packages that match the name or description.
 *
 * Return value: (element-type ZifPackageRemote) (transfer container): An array of #ZifPackageRemote's
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
	g_return_val_if_fail (klass != NULL, NULL);

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
 * @md: A #ZifMd
 * @search: Search term, e.g. "games/console"
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Finds all packages that match the group.
 *
 * Return value: (element-type ZifPackageRemote) (transfer container): An array of #ZifPackageRemote's
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
	g_return_val_if_fail (klass != NULL, NULL);

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
 * @md: A #ZifMd
 * @search: (array zero-terminated=1) (element-type utf8): The search terms as a 64 bit hash
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Finds all packages that match the given pkgId.
 *
 * Return value: (element-type ZifPackageRemote) (transfer container): An array of #ZifPackageRemote's
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
	g_return_val_if_fail (klass != NULL, NULL);

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
 * @md: A #ZifMd
 * @depends: an array of #ZifDepend's provide
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Finds all packages that match the given provide.
 *
 * Return value: (element-type ZifPackageRemote) (transfer container): An array of #ZifPackageRemote's
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_md_what_provides (ZifMd *md, GPtrArray *depends,
		      ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (depends != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (klass != NULL, NULL);

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
	array = klass->what_provides (md, depends, state, error);
out:
	return array;
}

/**
 * zif_md_what_requires:
 * @md: A #ZifMd
 * @depends: an array of #ZifDepend's provide
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Finds all packages that match the given provide.
 *
 * Return value: (element-type ZifPackageRemote) (transfer container): An array of #ZifPackageRemote's
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_md_what_requires (ZifMd *md, GPtrArray *depends,
		      ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (depends != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (klass != NULL, NULL);

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
	array = klass->what_requires (md, depends, state, error);
out:
	return array;
}

/**
 * zif_md_what_obsoletes:
 * @md: A #ZifMd
 * @depends: an array of #ZifDepend's obsolete
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Finds all packages that obsolete the given provide.
 *
 * Return value: (element-type ZifPackageRemote) (transfer container): An array of #ZifPackageRemote's
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_md_what_obsoletes (ZifMd *md, GPtrArray *depends,
		       ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (depends != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (klass != NULL, NULL);

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
	array = klass->what_obsoletes (md, depends, state, error);
out:
	return array;
}

/**
 * zif_md_what_conflicts:
 * @md: A #ZifMd
 * @depends: an array of #ZifDepend's conflict
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Finds all packages that conflict with the given depends.
 *
 * Return value: (element-type ZifPackageRemote) (transfer container): An array of #ZifPackageRemote's
 *
 * Since: 0.1.3
 **/
GPtrArray *
zif_md_what_conflicts (ZifMd *md, GPtrArray *depends,
		       ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifMdClass *klass = ZIF_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_MD (md), NULL);
	g_return_val_if_fail (depends != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (klass != NULL, NULL);

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
	array = klass->what_conflicts (md, depends, state, error);
out:
	return array;
}

/**
 * zif_md_find_package:
 * @md: A #ZifMd
 * @package_id: A PackageId to match
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Finds all packages that match PackageId.
 *
 * Return value: (element-type ZifPackageRemote) (transfer container): An array of #ZifPackageRemote's
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
	g_return_val_if_fail (klass != NULL, NULL);

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
 * @md: A #ZifMd
 * @pkgid: A internal pkgid to match
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Gets the changelog data for a specific package
 *
 * Return value: (element-type ZifChangeset) (transfer container): An array of #ZifChangeset's
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
	g_return_val_if_fail (klass != NULL, NULL);

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
 * @md: A #ZifMd
 * @package: A %ZifPackage
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Gets the file list for a specific package.
 *
 * Return value: (element-type utf8) (transfer container): an array of strings
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
	g_return_val_if_fail (klass != NULL, NULL);

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
 * @md: A #ZifMd
 * @package: A %ZifPackage
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Gets the provides for a specific package.
 *
 * Return value: (element-type ZifDepend) (transfer container): An array of #ZifDepend's
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
	g_return_val_if_fail (klass != NULL, NULL);

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
 * @md: A #ZifMd
 * @package: A %ZifPackage
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Gets the requires for a specific package.
 *
 * Return value: (element-type ZifDepend) (transfer container): An array of #ZifDepend's
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
	g_return_val_if_fail (klass != NULL, NULL);

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
 * @md: A #ZifMd
 * @package: A %ZifPackage
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Gets the obsoletes for a specific package.
 *
 * Return value: (element-type ZifDepend) (transfer container): An array of #ZifDepend's
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
	g_return_val_if_fail (klass != NULL, NULL);

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
 * @md: A #ZifMd
 * @package: A %ZifPackage
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Gets the conflicts for a specific package.
 *
 * Return value: (element-type ZifDepend) (transfer container): An array of #ZifDepend's
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
	g_return_val_if_fail (klass != NULL, NULL);

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
 * @md: A #ZifMd
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Returns all packages in the repo.
 *
 * Return value: (element-type ZifPackageRemote) (transfer container): An array of #ZifPackageRemote's
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
	g_return_val_if_fail (klass != NULL, NULL);

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
 * @md: A #ZifMd
 * @error: A #GError, or %NULL
 *
 * Clean the metadata store.
 *
 * Return value: %TRUE for success, %FALSE otherwise
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
 * zif_md_check_age:
 **/
static gboolean
zif_md_check_age (ZifMd *md, GFile *file, GError **error)
{
	gboolean ret = FALSE;
	GFileInfo *file_info = NULL;
	GError *error_local = NULL;
	guint64 modified, age;
	gchar *filename = NULL;

	/* get file attributes */
	file_info = g_file_query_info (file,
				       G_FILE_ATTRIBUTE_TIME_MODIFIED,
				       G_FILE_QUERY_INFO_NONE,
				       NULL,
				       &error_local);
	if (file_info == NULL) {
		if (error_local->domain == G_IO_ERROR &&
		    error_local->code == G_IO_ERROR_NOT_FOUND) {
			g_set_error (error,
				     ZIF_MD_ERROR,
				     ZIF_MD_ERROR_FILE_NOT_EXISTS,
				     "cannot query information: %s",
				     error_local->message);
			g_error_free (error_local);
		} else {
			g_propagate_error (error, error_local);
		}
		ret = FALSE;
		goto out;
	}

	/* check age */
	modified = g_file_info_get_attribute_uint64 (file_info,
						     G_FILE_ATTRIBUTE_TIME_MODIFIED);
	age = time (NULL) - modified;
	filename = g_file_get_path (file);
	g_debug ("age of %s is %" G_GUINT64_FORMAT
		 " hours (max-age is %" G_GUINT64_FORMAT " hours)",
		   filename, age / (60 * 60), md->priv->max_age / (60 * 60));
	ret = (md->priv->max_age == 0 || age < md->priv->max_age);
	if (!ret) {
		/* the file is too old */
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_FILE_TOO_OLD,
			     "data is too old: %s", filename);
		goto out;
	}
out:
	g_free (filename);
	if (file_info != NULL)
		g_object_unref (file_info);
	return ret;
}

/**
 * zif_md_file_checksum_matches_no_xattr:
 **/
static gboolean
zif_md_file_checksum_matches_no_xattr (GFile *file,
				       const gchar *checksum_wanted,
				       GChecksumType checksum_type,
				       ZifState *state,
				       GError **error)
{
	gboolean ret;
	GCancellable *cancellable;
	gchar *checksum = NULL;
	gchar *data = NULL;
	gchar *filename = NULL;
	GError *error_local = NULL;
	gint rc;
	gsize length;

	/* setup state */
	ret = zif_state_set_steps (state,
				   error,
				   20, /* load file */
				   80, /* calc checksum */
				   -1);
	if (!ret)
		goto out;

	/* set action */
	filename = g_file_get_path (file);
	zif_state_action_start (state, ZIF_STATE_ACTION_CHECKING, filename);

	/* get contents */
	cancellable = zif_state_get_cancellable (state);
	ret = g_file_load_contents (file,
				    cancellable,
				    &data,
				    &length,
				    NULL,
				    &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_FILE_NOT_EXISTS,
			     "failed to get contents of %s: %s",
			     filename,
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	checksum = g_compute_checksum_for_data (checksum_type,
						(guchar*) data,
						length);

	/* matches? */
	ret = (g_strcmp0 (checksum, checksum_wanted) == 0);
	if (!ret) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_CHECKSUM_INVALID,
			     "checksum incorrect, wanted %s, got %s for %s",
			     checksum_wanted, checksum, filename);
		goto out;
	}

	/* set xattr */
	rc = setxattr (filename,
		       "user.Zif.MdChecksum",
		       checksum,
		       strlen (checksum) + 1,
		       XATTR_CREATE);
	if (rc < 0) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_CHECKSUM_INVALID,
			     "failed to set xattr on %s",
			     filename);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	g_free (filename);
	g_free (data);
	g_free (checksum);
	return ret;
}

/**
 * zif_md_file_checksum_matches:
 **/
static gboolean
zif_md_file_checksum_matches (GFile *file,
			      const gchar *checksum_wanted,
			      GChecksumType checksum_type,
			      ZifState *state,
			      GError **error)
{
	gboolean ret;
	gchar buffer[256];
	gchar *filename;
	gssize length;

	/* check to see if we have a cached checksum */
	filename = g_file_get_path (file);
	length = getxattr (filename,
			   "user.Zif.MdChecksum",
			   buffer,
			   sizeof (buffer));
	if (length < 0) {
		ret = zif_md_file_checksum_matches_no_xattr (file,
							     checksum_wanted,
							     checksum_type,
							     state,
							     error);
		goto out;
	}

	/* matches? */
	ret = (g_strcmp0 (buffer, checksum_wanted) == 0);
	if (!ret) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_CHECKSUM_INVALID,
			     "xattr checksum incorrect, wanted %s, got %s for %s",
			     checksum_wanted, buffer, filename);
		goto out;
	}
out:
	g_free (filename);
	return ret;
}

/**
 * zif_md_check_compressed:
 * @md: A #ZifMd
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Check the metadata files to make sure they are valid.
 *
 * Return value: %TRUE if the MD file was read and checked.
 *
 * Since: 0.2.2
 **/
gboolean
zif_md_check_compressed (ZifMd *md, ZifState *state, GError **error)
{
	const gchar *filename;
	gboolean ret = FALSE;
	GCancellable *cancellable;
	GFile *file = NULL;

	g_return_val_if_fail (ZIF_IS_MD (md), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (md->priv->id != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* these are not compressed */
	if (md->priv->kind == ZIF_MD_KIND_METALINK ||
	    md->priv->kind == ZIF_MD_KIND_MIRRORLIST) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_SUPPORT,
			     "no compressed metadata for %s",
			     zif_md_kind_to_text (md->priv->kind));
		ret = FALSE;
		goto out;
	}

	/* no checksum set */
	filename = md->priv->filename;
	if (filename == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR,
			     "no filename for %s [%s]",
			     md->priv->id,
			     zif_md_kind_to_text (md->priv->kind));
		ret = FALSE;
		goto out;
	}

	/* does file exist */
	file = g_file_new_for_path (filename);
	cancellable = zif_state_get_cancellable (state);
	ret = g_file_query_exists (file, cancellable);
	if (!ret) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_FILE_NOT_EXISTS,
			     "%s  not found",
			     filename);
		goto out;
	}

	/* check age */
	ret = zif_md_check_age (md, file, error);
	if (!ret)
		goto out;

	/* no checksum set */
	if (md->priv->checksum == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_FAILED,
			     "checksum not set for %s", filename);
		ret = FALSE;
		goto out;
	}

	/* compute checksum */
	ret = zif_md_file_checksum_matches (file,
					    md->priv->checksum,
					    md->priv->checksum_type,
					    state,
					    error);
	if (!ret)
		goto out;

	g_debug ("%s compressed checksum correct (%s)",
		 filename, md->priv->checksum);
out:
	if (file != NULL)
		g_object_unref (file);
	return ret;
}

/**
 * zif_md_check_uncompressed:
 * @md: A #ZifMd
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Check the metadata files to make sure they are valid.
 *
 * Return value: %TRUE if the MD file was read and checked.
 *
 * Since: 0.2.2
 **/
gboolean
zif_md_check_uncompressed (ZifMd *md, ZifState *state, GError **error)
{
	const gchar *checksum_wanted;
	const gchar *filename;
	gboolean ret = FALSE;
	GCancellable *cancellable;
	gchar *data = NULL;
	gchar **lines = NULL;
	GError *error_local = NULL;
	GFile *file = NULL;
	gsize length;
	guint i;
	ZifState *state_local;

	g_return_val_if_fail (ZIF_IS_MD (md), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);
	g_return_val_if_fail (md->priv->id != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* setup state */
	ret = zif_state_set_steps (state,
				   error,
				   20, /* load */
				   80, /* check checksum */
				   -1);
	if (!ret)
		goto out;

	/* no checksum set */
	filename = md->priv->filename_uncompressed;
	if (filename == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_NO_FILENAME,
			     "no filename for %s [%s]",
			     md->priv->id,
			     zif_md_kind_to_text (md->priv->kind));
		ret = FALSE;
		goto out;
	}

	/* set action */
	zif_state_action_start (state, ZIF_STATE_ACTION_CHECKING, filename);

	/* check age */
	file = g_file_new_for_path (filename);
	ret = zif_md_check_age (md, file, error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* get contents */
	if (md->priv->kind == ZIF_MD_KIND_METALINK ||
	    md->priv->kind == ZIF_MD_KIND_MIRRORLIST) {
		cancellable = zif_state_get_cancellable (state);
		ret = g_file_load_contents (file,
					    cancellable,
					    &data,
					    &length,
					    NULL,
					    &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_MD_ERROR,
				     ZIF_MD_ERROR_FILE_NOT_EXISTS,
				     "failed to get contents of %s: %s",
				     filename,
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* metalink has no checksum... */
	if (md->priv->kind == ZIF_MD_KIND_METALINK) {

		/* is this a valid xml file */
		ret = g_strstr_len (data, length, "<metalink") != NULL;
		if (!ret) {
			g_set_error_literal (error,
					     ZIF_MD_ERROR,
					     ZIF_MD_ERROR_FAILED_TO_LOAD,
					     "metalink file was not well formed");
			goto out;
		}

		g_debug ("skipping checksum check on metalink");
		ret = zif_state_finished (state, error);
		goto out;
	}

	/* mirrorlist has no checksum... */
	if (md->priv->kind == ZIF_MD_KIND_MIRRORLIST) {

		/* check the mirrorlist contains at least
		 * one non-comment or empty line */
		ret = FALSE;
		lines = g_strsplit (data, "\n", -1);
		for (i = 0; lines[i] != NULL; i++) {
			if (lines[i][0] != '#' &&
			    lines[i][0] != '\0') {
				ret = TRUE;
			}
		}
		if (!ret) {
			g_set_error (error,
				     ZIF_MD_ERROR,
				     ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "mirrorlist file was not well formed: %s",
				     lines[0]);
			goto out;
		}

		g_debug ("skipping checksum check on mirrorlist");
		ret = zif_state_finished (state, error);
		goto out;
	}

	/* no checksum set */
	checksum_wanted = md->priv->checksum_uncompressed;
	if (checksum_wanted == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_FAILED,
			     "checksum not set for %s", filename);
		ret = FALSE;
		goto out;
	}

	/* compute checksum */
	state_local = zif_state_get_child (state);
	ret = zif_md_file_checksum_matches (file,
					    md->priv->checksum_uncompressed,
					    md->priv->checksum_type,
					    state_local,
					    error);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	if (file != NULL)
		g_object_unref (file);
	g_free (data);
	g_strfreev (lines);
	return ret;
}


/**
 * zif_md_file_check:
 * @md: A #ZifMd
 * @use_uncompressed: If we should check only the uncompresed version
 * @valid: If the metadata is valid, i.e. the checksums are correct
 * @state: A #ZifState to use for progress reporting
 * @error: A #GError, or %NULL
 *
 * Check the metadata files to make sure they are valid.
 *
 * NOTE: Don't use this function, the semantics are horrible.
 *
 * Return value: %TRUE if the MD file was read and checked.
 *
 * Since: 0.1.0
 **/
gboolean
zif_md_file_check (ZifMd *md, gboolean use_uncompressed, gboolean *valid,
		   ZifState *state, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;

	g_warning ("don't use zif_md_file_check() it's broken. "
		   "Use zif_md_check_uncompressed() instead");

	/* just emulate */
	if (use_uncompressed)
		ret = zif_md_check_uncompressed (md, state, &error_local);
	else
		ret = zif_md_check_compressed (md, state, &error_local);
	*valid = ret;
	if (ret)
		goto out;
	if (error_local->domain == ZIF_MD_ERROR &&
	    error_local->code == ZIF_MD_ERROR_NO_SUPPORT) {
		*valid = TRUE;
		g_debug ("ignoring %s", error_local->message);
		g_error_free (error_local);
		ret = TRUE;
	} else if (error_local->domain == ZIF_MD_ERROR &&
	    (error_local->code == ZIF_MD_ERROR_NO_SUPPORT ||
	     error_local->code == ZIF_MD_ERROR_FILE_TOO_OLD ||
	     error_local->code == ZIF_MD_ERROR_FILE_NOT_EXISTS ||
	     error_local->code == ZIF_MD_ERROR_CHECKSUM_INVALID)) {
		g_debug ("ignoring %s", error_local->message);
		g_error_free (error_local);
		ret = zif_state_finished (state, error);
	} else {
		g_debug ("failed to check %s", error_local->message);
		g_propagate_error (error, error_local);
	}
out:
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
	 * ZifMd:kind:
	 *
	 * Since: 0.1.3
	 */
	pspec = g_param_spec_uint ("kind", NULL, NULL,
				   ZIF_MD_KIND_UNKNOWN, G_MAXUINT, 0,
				   G_PARAM_CONSTRUCT | G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_KIND, pspec);

	/**
	 * ZifMd:filename:
	 *
	 * Since: 0.1.3
	 */
	pspec = g_param_spec_string ("filename", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_FILENAME, pspec);

	/**
	 * ZifMd:location:
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
	md->priv->store = NULL;
	md->priv->config = zif_config_new ();
}

/**
 * zif_md_new:
 *
 * Return value: A new #ZifMd instance.
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

