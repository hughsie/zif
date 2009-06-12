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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>

#include "zif-repo-md.h"

#include "egg-debug.h"
#include "egg-string.h"

#define ZIF_REPO_MD_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_REPO_MD, ZifRepoMdPrivate))

struct ZifRepoMdPrivate
{
	gboolean		 loaded;
	gchar			*id;
	gchar			*local_path;
	gchar			*cache_dir;
	gchar			*filename;	/* /var/cache/yum/fedora/repo.sqlite */
	gchar			*filename_raw;	/* /var/cache/yum/fedora/repo.sqlite.bz2 */
	const ZifRepoMdInfoData	*info_data;
};

G_DEFINE_TYPE (ZifRepoMd, zif_repo_md, G_TYPE_OBJECT)

/**
 * zif_repo_md_type_to_text:
 **/
const gchar *
zif_repo_md_type_to_text (ZifRepoMdType type)
{
	if (type == ZIF_REPO_MD_TYPE_FILELISTS)
		return "filelists";
	if (type == ZIF_REPO_MD_TYPE_PRIMARY)
		return "primary";
	if (type == ZIF_REPO_MD_TYPE_OTHER)
		return "other";
	if (type == ZIF_REPO_MD_TYPE_COMPS)
		return "comps";
	return "unknown";
}

/**
 * zif_repo_md_get_id:
 **/
const gchar *
zif_repo_md_get_id (ZifRepoMd *md)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), NULL);
	return md->priv->id;
}

/**
 * zif_repo_md_get_filename:
 **/
const gchar *
zif_repo_md_get_filename (ZifRepoMd *md)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), NULL);
	return md->priv->filename;
}

/**
 * zif_repo_md_get_filename_raw:
 **/
const gchar *
zif_repo_md_get_filename_raw (ZifRepoMd *md)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), NULL);
	return md->priv->filename_raw;
}

/**
 * zif_repo_md_get_local_path:
 **/
const gchar *
zif_repo_md_get_local_path (ZifRepoMd *md)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), NULL);
	return md->priv->local_path;
}

/**
 * zif_repo_md_get_info_data:
 **/
const ZifRepoMdInfoData *
zif_repo_md_get_info_data (ZifRepoMd *md)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), NULL);
	return md->priv->info_data;
}

/**
 * zif_repo_md_set_cache_dir:
 **/
gboolean
zif_repo_md_set_cache_dir (ZifRepoMd *md, const gchar *cache_dir)
{
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);
	g_return_val_if_fail (md->priv->cache_dir == NULL, FALSE);
	g_return_val_if_fail (cache_dir != NULL, FALSE);

	/* check directory exists */
	ret = g_file_test (cache_dir, G_FILE_TEST_IS_DIR);
	if (!ret)
		goto out;
	md->priv->cache_dir = g_strdup (cache_dir);
out:
	return ret;
}

/**
 * zif_repo_md_set_base_filename:
 *
 * ONLY TO BE USED BY ZifRepoMdMaster
 **/
gboolean
zif_repo_md_set_base_filename (ZifRepoMd *md, const gchar *base_filename)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);
	g_return_val_if_fail (md->priv->cache_dir != NULL, FALSE);
	g_return_val_if_fail (md->priv->filename == NULL, FALSE);
	g_return_val_if_fail (base_filename != NULL, FALSE);

	md->priv->filename = g_build_filename (md->priv->local_path, base_filename, NULL);
	return TRUE;
}

/**
 * zif_repo_md_set_id:
 **/
gboolean
zif_repo_md_set_id (ZifRepoMd *md, const gchar *id)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);
	g_return_val_if_fail (md->priv->cache_dir != NULL, FALSE);
	g_return_val_if_fail (md->priv->id == NULL, FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	md->priv->id = g_strdup (id);
	md->priv->local_path = g_build_filename (md->priv->cache_dir, id, NULL);
	return TRUE;
}

/**
 * zif_repo_md_set_info_data:
 **/
gboolean
zif_repo_md_set_info_data (ZifRepoMd *md, const ZifRepoMdInfoData *info_data)
{
	guint len;
	gchar *tmp;

	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);
	g_return_val_if_fail (md->priv->info_data == NULL, FALSE);
	g_return_val_if_fail (info_data != NULL, FALSE);
	md->priv->info_data = info_data;

	/* get local file name */
	tmp = g_path_get_basename (info_data->location);
	len = strlen (tmp);
	md->priv->filename_raw = g_build_filename (md->priv->local_path, tmp, NULL);

	/* remove compression extension */
	if (g_str_has_suffix (tmp, ".gz"))
		tmp[len-3] = '\0';
	else if (g_str_has_suffix (tmp, ".bz2"))
		tmp[len-4] = '\0';
	md->priv->filename = g_build_filename (md->priv->local_path, tmp, NULL);

	g_free (tmp);
	return TRUE;
}

/**
 * zif_repo_md_print:
 **/
void
zif_repo_md_print (ZifRepoMd *md)
{
	g_return_if_fail (ZIF_IS_REPO_MD (md));
	g_return_if_fail (md->priv->id != NULL);

	/* if not already loaded, load */
	if (!md->priv->loaded)
		return;

	g_print ("id=%s\n", md->priv->id);
	g_print ("cache_dir=%s\n", md->priv->cache_dir);
	g_print ("local_path=%s\n", md->priv->local_path);
//	g_print ("type: %s\n", zif_repo_md_type_to_text (i));
	g_print (" location: %s\n", md->priv->info_data->location);
	g_print (" checksum: %s\n", md->priv->info_data->checksum);
	g_print (" checksum_open: %s\n", md->priv->info_data->checksum_open);
	g_print (" timestamp: %i\n", md->priv->info_data->timestamp);
}

/**
 * zif_repo_md_load:
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

/**
 * zif_repo_md_check:
 **/
gboolean
zif_repo_md_check (ZifRepoMd *md, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	gchar *data = NULL;
	gchar *checksum = NULL;
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
	ret = g_file_get_contents (md->priv->filename, &data, &length, &error_local);
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
	g_free (data);
	g_free (checksum);
	return ret;
}

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
	g_free (md->priv->cache_dir);
	g_free (md->priv->local_path);
	g_free (md->priv->filename);
	g_free (md->priv->filename_raw);

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
	md->priv->info_data = NULL;
	md->priv->cache_dir = NULL;
	md->priv->local_path = NULL;
	md->priv->filename = NULL;
	md->priv->filename_raw = NULL;
}

/**
 * zif_repo_md_new:
 * Return value: A new repo_md class instance.
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
//	gchar *text;
	GError *error = NULL;

	if (!egg_test_start (test, "ZifRepoMd"))
		return;

	/************************************************************/
	egg_test_title (test, "get store_remote md");
	md = zif_repo_md_new ();
	egg_test_assert (test, md != NULL);

	/************************************************************/
	egg_test_title (test, "set cache dir");
	ret = zif_repo_md_set_cache_dir (md, "./test/cache");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

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

	/************************************************************/
	egg_test_title (test, "check");
	ret = zif_repo_md_check (md, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to check '%s'", error->message);

	g_object_unref (md);

	egg_test_end (test);
}
#endif

