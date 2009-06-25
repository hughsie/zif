/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-repo-md
 * @short_description: Metadata file common functionality
 *
 * This provides an abstract metadata class.
 * It is implemented by #ZifRepoMdFilelists, #ZifRepoMdMaster and #ZifRepoMdPrimary.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>

#include "zif-utils.h"
#include "zif-repo-md.h"

#include "egg-debug.h"
#include "egg-string.h"

#define ZIF_REPO_MD_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_REPO_MD, ZifRepoMdPrivate))

/**
 * ZifRepoMdPrivate:
 *
 * Private #ZifRepoMd data
 **/
struct _ZifRepoMdPrivate
{
	gboolean		 loaded;
	gchar			*id;			/* fedora */
	gchar			*filename;		/* /var/cache/yum/fedora/repo.sqlite.bz2 */
	gchar			*filename_uncompressed;	/* /var/cache/yum/fedora/repo.sqlite */
};

G_DEFINE_TYPE (ZifRepoMd, zif_repo_md, G_TYPE_OBJECT)

/**
 * zif_repo_md_get_id:
 * @md: the #ZifRepoMd object
 *
 * Gets the md identifier, usually the repo name.
 *
 * Return value: the repo id.
 **/
const gchar *
zif_repo_md_get_id (ZifRepoMd *md)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), NULL);
	return md->priv->id;
}

/**
 * zif_repo_md_get_filename:
 * @md: the #ZifRepoMd object
 *
 * Gets the compressed filename of the repo.
 *
 * Return value: the filename
 **/
const gchar *
zif_repo_md_get_filename (ZifRepoMd *md)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), NULL);
	return md->priv->filename;
}

/**
 * zif_repo_md_get_filename_uncompressed:
 * @md: the #ZifRepoMd object
 *
 * Gets the uncompressed filename of the repo.
 *
 * Return value: the filename
 **/
const gchar *
zif_repo_md_get_filename_uncompressed (ZifRepoMd *md)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), NULL);
	return md->priv->filename_uncompressed;
}

/**
 * zif_repo_md_set_filename:
 * @md: the #ZifRepoMd object
 * @filename: the base filename, e.g. "master.xml.bz2"
 *
 * Sets the filename of the compressed file.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_repo_md_set_filename (ZifRepoMd *md, const gchar *filename)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);
	g_return_val_if_fail (md->priv->filename == NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);

	/* this is the compressed name */
	md->priv->filename = g_strdup (filename);

	/* this is the uncompressed name */
	md->priv->filename_uncompressed = zif_file_get_uncompressed_name (filename);

	return TRUE;
}

/**
 * zif_repo_md_set_id:
 * @md: the #ZifRepoMd object
 * @id: the repository id, e.g. "fedora"
 *
 * Sets the repository ID for this metadata.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_repo_md_set_id (ZifRepoMd *md, const gchar *id)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);
	g_return_val_if_fail (md->priv->id == NULL, FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	md->priv->id = g_strdup (id);
	return TRUE;
}

/**
 * zif_repo_md_load:
 * @md: the #ZifRepoMd object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Load the metadata store.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_repo_md_load (ZifRepoMd *md, GError **error)
{
	ZifRepoMdClass *klass = ZIF_REPO_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);

	/* no support */
	if (klass->load == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "operation cannot be performed on this md");
		return FALSE;
	}

	return klass->load (md, error);
}

/**
 * zif_repo_md_clean:
 * @md: the #ZifRepoMd object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Clean the metadata store.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_repo_md_clean (ZifRepoMd *md, GError **error)
{
	ZifRepoMdClass *klass = ZIF_REPO_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);

	/* no support */
	if (klass->clean == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "operation cannot be performed on this md");
		return FALSE;
	}

	return klass->clean (md, error);
}

#if 0
/**
 * zif_repo_md_check:
 * @md: the #ZifRepoMd object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Check the metadata store.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_repo_md_check (ZifRepoMd *md, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	gchar *data = NULL;
	gchar *checksum = NULL;
	gchar *filename = NULL;
	const gchar *checksum_wanted = NULL;
	gsize length;

	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);
	g_return_val_if_fail (md->priv->id != NULL, FALSE);
	g_return_val_if_fail (md->priv->filename != NULL, FALSE);

	/* if not already loaded, load */
	if (!md->priv->loaded) {
		ret = zif_repo_md_load (md, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load metadata: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* get contents */
	filename = g_build_filename (md->priv->local_path, md->priv->filename, NULL);
	ret = g_file_get_contents (filename, &data, &length, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get contents: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* compute checksum */
	checksum = g_compute_checksum_for_data (md->priv->info_data->checksum_type, (guchar*) data, length);

	/* get the one we want */
	checksum_wanted = md->priv->info_data->checksum;

	/* matches? */
	ret = (strcmp (checksum, checksum_wanted) == 0);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "checksum incorrect, wanted %s, got %s", checksum_wanted, checksum);
	}
out:
	g_free (filename);
	g_free (data);
	g_free (checksum);
	return ret;
}
#endif

/**
 * zif_repo_md_finalize:
 **/
static void
zif_repo_md_finalize (GObject *object)
{
	ZifRepoMd *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_REPO_MD (object));
	md = ZIF_REPO_MD (object);

	g_free (md->priv->id);
	g_free (md->priv->filename);

	G_OBJECT_CLASS (zif_repo_md_parent_class)->finalize (object);
}

/**
 * zif_repo_md_class_init:
 **/
static void
zif_repo_md_class_init (ZifRepoMdClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_repo_md_finalize;
	g_type_class_add_private (klass, sizeof (ZifRepoMdPrivate));
}

/**
 * zif_repo_md_init:
 **/
static void
zif_repo_md_init (ZifRepoMd *md)
{
	md->priv = ZIF_REPO_MD_GET_PRIVATE (md);
	md->priv->loaded = FALSE;
	md->priv->id = NULL;
	md->priv->filename = NULL;
}

/**
 * zif_repo_md_new:
 *
 * Return value: A new #ZifRepoMd class instance.
 **/
ZifRepoMd *
zif_repo_md_new (void)
{
	ZifRepoMd *md;
	md = g_object_new (ZIF_TYPE_REPO_MD, NULL);
	return ZIF_REPO_MD (md);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_repo_md_test (EggTest *test)
{
	ZifRepoMd *md;
	gboolean ret;
	GError *error = NULL;

	if (!egg_test_start (test, "ZifRepoMd"))
		return;

	/************************************************************/
	egg_test_title (test, "get store_remote md");
	md = zif_repo_md_new ();
	egg_test_assert (test, md != NULL);

	/************************************************************/
	egg_test_title (test, "loaded");
	egg_test_assert (test, !md->priv->loaded);

	/************************************************************/
	egg_test_title (test, "set id");
	ret = zif_repo_md_set_id (md, "fedora");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "load");
	ret = zif_repo_md_load (md, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "loaded");
	egg_test_assert (test, md->priv->loaded);

	g_object_unref (md);

	egg_test_end (test);
}
#endif

