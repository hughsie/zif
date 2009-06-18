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
 * SECTION:zif-repo-md-master
 * @short_description: Master metadata functionality
 *
 * Provide access to the master repo metadata.
 * This object is a subclass of #ZifRepoMd
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <gio/gio.h>

#include "zif-repo-md.h"
#include "zif-repo-md-master.h"

#include "egg-debug.h"
#include "egg-string.h"

#define ZIF_REPO_MD_MASTER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_REPO_MD_MASTER, ZifRepoMdMasterPrivate))

typedef enum {
	ZIF_REPO_MD_MASTER_PARSER_SECTION_CHECKSUM,
	ZIF_REPO_MD_MASTER_PARSER_SECTION_CHECKSUM_OPEN,
	ZIF_REPO_MD_MASTER_PARSER_SECTION_TIMESTAMP,
	ZIF_REPO_MD_MASTER_PARSER_SECTION_UNKNOWN
} ZifRepoMdMasterParserSection;

/**
 * ZifRepoMdMasterPrivate:
 *
 * Private #ZifRepoMdMaster data
 **/
struct _ZifRepoMdMasterPrivate
{
	gboolean		 loaded;
	ZifRepoMdInfoData	*data[ZIF_REPO_MD_TYPE_UNKNOWN];
	ZifRepoMdType		 parser_type;
	ZifRepoMdType		 parser_section;
};

G_DEFINE_TYPE (ZifRepoMdMaster, zif_repo_md_master, ZIF_TYPE_REPO_MD)

/**
 * zif_repo_md_master_checksum_type_from_text:
 **/
static GChecksumType
zif_repo_md_master_checksum_type_from_text (const gchar *type)
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
 * zif_repo_md_master_parser_start_element:
 **/
static void
zif_repo_md_master_parser_start_element (GMarkupParseContext *context, const gchar *element_name,
				  const gchar **attribute_names, const gchar **attribute_values,
				  gpointer user_data, GError **error)
{
	guint i;
	ZifRepoMdMaster *md = user_data;
	ZifRepoMdType parser_type = md->priv->parser_type;

	/* data */
	if (strcmp (element_name, "data") == 0) {

		/* reset */
		md->priv->parser_type = ZIF_REPO_MD_TYPE_UNKNOWN;

		/* find type */
		for (i=0; attribute_names[i] != NULL; i++) {
			if (strcmp (attribute_names[i], "type") == 0) {
				if (strcmp (attribute_values[i], "primary_db") == 0)
					md->priv->parser_type = ZIF_REPO_MD_TYPE_PRIMARY;
				else if (strcmp (attribute_values[i], "filelists_db") == 0)
					md->priv->parser_type = ZIF_REPO_MD_TYPE_FILELISTS;
				else if (strcmp (attribute_values[i], "other_db") == 0)
					md->priv->parser_type = ZIF_REPO_MD_TYPE_OTHER;
				else if (strcmp (attribute_values[i], "group_gz") == 0)
					md->priv->parser_type = ZIF_REPO_MD_TYPE_COMPS;
				break;
			}
		}
		md->priv->parser_section = ZIF_REPO_MD_MASTER_PARSER_SECTION_UNKNOWN;
		goto out;
	}

	/* not a section we recognise */
	if (md->priv->parser_type == ZIF_REPO_MD_TYPE_UNKNOWN)
		goto out;

	/* location */
	if (strcmp (element_name, "location") == 0) {
		for (i=0; attribute_names[i] != NULL; i++) {
			if (strcmp (attribute_names[i], "href") == 0) {
				md->priv->data[parser_type]->location = g_strdup (attribute_values[i]);
				break;
			}
		}
		md->priv->parser_section = ZIF_REPO_MD_MASTER_PARSER_SECTION_UNKNOWN;
		goto out;
	}

	/* checksum */
	if (strcmp (element_name, "checksum") == 0) {
		for (i=0; attribute_names[i] != NULL; i++) {
			if (strcmp (attribute_names[i], "type") == 0) {
				md->priv->data[parser_type]->checksum_type = zif_repo_md_master_checksum_type_from_text (attribute_values[i]);
				break;
			}
		}
		md->priv->parser_section = ZIF_REPO_MD_MASTER_PARSER_SECTION_CHECKSUM;
		goto out;
	}

	/* checksum */
	if (strcmp (element_name, "open-checksum") == 0) {
		md->priv->parser_section = ZIF_REPO_MD_MASTER_PARSER_SECTION_CHECKSUM_OPEN;
		goto out;
	}

	/* timestamp */
	if (strcmp (element_name, "timestamp") == 0) {
		md->priv->parser_section = ZIF_REPO_MD_MASTER_PARSER_SECTION_TIMESTAMP;
		goto out;
	}
out:
	return;
}

/**
 * zif_repo_md_master_parser_end_element:
 **/
static void
zif_repo_md_master_parser_end_element (GMarkupParseContext *context, const gchar *element_name,
				gpointer user_data, GError **error)
{
	ZifRepoMdMaster *md = user_data;

	/* reset */
	md->priv->parser_section = ZIF_REPO_MD_MASTER_PARSER_SECTION_UNKNOWN;
	if (strcmp (element_name, "data") == 0)
		md->priv->parser_type = ZIF_REPO_MD_TYPE_UNKNOWN;
}


/**
 * zif_repo_md_master_parser_text:
 **/
static void
zif_repo_md_master_parser_text (GMarkupParseContext *context, const gchar *text, gsize text_len,
			 gpointer user_data, GError **error)

{
	ZifRepoMdMaster *md = user_data;
	ZifRepoMdType parser_type = md->priv->parser_type;

	if (parser_type == ZIF_REPO_MD_TYPE_UNKNOWN)
		return;

	if (md->priv->parser_section == ZIF_REPO_MD_MASTER_PARSER_SECTION_CHECKSUM)
		md->priv->data[parser_type]->checksum = g_strdup (text);
	else if (md->priv->parser_section == ZIF_REPO_MD_MASTER_PARSER_SECTION_CHECKSUM_OPEN)
		md->priv->data[parser_type]->checksum_open = g_strdup (text);
	else if (md->priv->parser_section == ZIF_REPO_MD_MASTER_PARSER_SECTION_TIMESTAMP)
		md->priv->data[parser_type]->timestamp = atol (text);
}

/**
 * zif_repo_md_master_clean:
 **/
static gboolean
zif_repo_md_master_clean (ZifRepoMd *md, GError **error)
{
	gboolean ret = FALSE;
	gboolean exists;
	const gchar *filename;
	GFile *file;
	GError *error_local = NULL;

	/* get filename */
	filename = zif_repo_md_get_filename (md);
	if (filename == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get filename for master");
		goto out;
	}

	/* file does not exist */
	exists = g_file_test (filename, G_FILE_TEST_EXISTS);
	if (exists) {
		file = g_file_new_for_path (filename);
		ret = g_file_delete (file, NULL, &error_local);
		g_object_unref (file);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to delete metadata file %s: %s", filename, error_local->message);
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
 * zif_repo_md_master_load:
 **/
static gboolean
zif_repo_md_master_load (ZifRepoMd *md, GError **error)
{
	guint i;
	ZifRepoMdInfoData **data;
	gboolean ret = TRUE;
	gchar *contents = NULL;
	const gchar *filename = NULL;
	gsize size;
	GMarkupParseContext *context = NULL;
	const GMarkupParser gpk_repo_md_master_markup_parser = {
		zif_repo_md_master_parser_start_element,
		zif_repo_md_master_parser_end_element,
		zif_repo_md_master_parser_text,
		NULL, /* passthrough */
		NULL /* error */
	};
	ZifRepoMdMaster *master = ZIF_REPO_MD_MASTER (md);

	g_return_val_if_fail (ZIF_IS_REPO_MD_MASTER (md), FALSE);

	/* already loaded */
	if (master->priv->loaded)
		goto out;

	/* get contents */
	zif_repo_md_set_base_filename (ZIF_REPO_MD (md), "repomd.xml");
	filename = zif_repo_md_get_filename (md);
	ret = g_file_get_contents (filename, &contents, &size, error);
	if (!ret)
		goto out;

	/* create parser */
	context = g_markup_parse_context_new (&gpk_repo_md_master_markup_parser, G_MARKUP_PREFIX_ERROR_POSITION, master, NULL);

	/* parse data */
	ret = g_markup_parse_context_parse (context, contents, (gssize) size, error);
	if (!ret)
		goto out;

	/* check we've got the needed data */
	data = master->priv->data;
	for (i=0; i<ZIF_REPO_MD_TYPE_UNKNOWN; i++) {
		if (data[i]->location != NULL && (data[i]->checksum == NULL || data[i]->timestamp == 0)) {
			if (error != NULL)
				*error = g_error_new (1, 0, "cannot load md for %s (loc=%s, sum=%s, sum_open=%s, ts=%i)",
						      zif_repo_md_type_to_text (i), data[i]->location, data[i]->checksum, data[i]->checksum_open, data[i]->timestamp);
			ret = FALSE;
			goto out;
		}
	}

	/* all okay */
	master->priv->loaded = TRUE;

out:
	if (context != NULL)
		g_markup_parse_context_free (context);
	g_free (contents);
	return ret;
}

/**
 * zif_repo_md_master_get_info:
 * @md: the #ZifRepoMdMaster object
 * @type: A #ZifRepoMdType
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the information about a repo, loading if not already loaded.
 *
 * Return value: a #ZifRepoMdInfoData object, do not unref.
 **/
const ZifRepoMdInfoData *
zif_repo_md_master_get_info (ZifRepoMdMaster *md, ZifRepoMdType type, GError **error)
{
	GError *error_local = NULL;
	gboolean ret;
	const ZifRepoMdInfoData *infodata = NULL;

	g_return_val_if_fail (ZIF_IS_REPO_MD_MASTER (md), NULL);
	g_return_val_if_fail (type != ZIF_REPO_MD_TYPE_UNKNOWN, NULL);

	/* if not already loaded, load */
	if (!md->priv->loaded) {
		ret = zif_repo_md_master_load (ZIF_REPO_MD (md), &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load metadata: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* maybe not of this type */
	infodata = md->priv->data[type];
out:
	return infodata;
}

#if 0
/**
 * zif_repo_md_master_check:
 **/
gboolean
zif_repo_md_master_check (ZifRepoMdMaster *md, ZifRepoMdType type, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	gchar *filename = NULL;
	gchar *data = NULL;
	gchar *checksum = NULL;
	const gchar *checksum_wanted = NULL;
	gsize length;

	g_return_val_if_fail (ZIF_IS_REPO_MD_MASTER (md), FALSE);
	g_return_val_if_fail (type != ZIF_REPO_MD_TYPE_UNKNOWN, FALSE);
	g_return_val_if_fail (md->priv->id != NULL, FALSE);

	/* if not already loaded, load */
	if (!md->priv->loaded) {
		ret = zif_repo_md_master_load (md, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load metadata: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* get correct filename */
	filename = zif_repo_md_master_get_filename (md, type, NULL);
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
	checksum = g_compute_checksum_for_data (md->priv->data[type]->checksum_type, (guchar*) data, length);

	/* get the one we want */
	if (type == ZIF_REPO_MD_TYPE_COMPS)
		checksum_wanted = md->priv->data[type]->checksum;
	else
		checksum_wanted = md->priv->data[type]->checksum_open;

	/* matches? */
	ret = (strcmp (checksum, checksum_wanted) == 0);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "%s checksum incorrect, wanted %s, got %s", zif_repo_md_type_to_text (type), checksum_wanted, checksum);
	}
out:
	g_free (data);
	g_free (filename);
	g_free (checksum);
	return ret;
}
#endif

/**
 * zif_repo_md_master_finalize:
 **/
static void
zif_repo_md_master_finalize (GObject *object)
{
	guint i;
	ZifRepoMdMaster *md;
	ZifRepoMdInfoData **data;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_REPO_MD_MASTER (object));
	md = ZIF_REPO_MD_MASTER (object);

	data = md->priv->data;
	for (i=0; i<ZIF_REPO_MD_TYPE_UNKNOWN; i++) {
		g_free (data[i]->location);
		g_free (data[i]->checksum);
		g_free (data[i]->checksum_open);
		g_free (data[i]);
	}

	G_OBJECT_CLASS (zif_repo_md_master_parent_class)->finalize (object);
}

/**
 * zif_repo_md_master_class_init:
 **/
static void
zif_repo_md_master_class_init (ZifRepoMdMasterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifRepoMdClass *repo_md_class = ZIF_REPO_MD_CLASS (klass);
	object_class->finalize = zif_repo_md_master_finalize;

	/* map */
	repo_md_class->load = zif_repo_md_master_load;
	repo_md_class->clean = zif_repo_md_master_clean;

	g_type_class_add_private (klass, sizeof (ZifRepoMdMasterPrivate));
}

/**
 * zif_repo_md_master_init:
 **/
static void
zif_repo_md_master_init (ZifRepoMdMaster *md)
{
	guint i;
	ZifRepoMdInfoData **data;

	md->priv = ZIF_REPO_MD_MASTER_GET_PRIVATE (md);
	md->priv->loaded = FALSE;
	md->priv->parser_type = ZIF_REPO_MD_TYPE_UNKNOWN;
	md->priv->parser_section = ZIF_REPO_MD_MASTER_PARSER_SECTION_UNKNOWN;

	data = md->priv->data;
	for (i=0; i<ZIF_REPO_MD_TYPE_UNKNOWN; i++) {
		data[i] = g_new0 (ZifRepoMdInfoData, 1);
		data[i]->location = NULL;
		data[i]->checksum = NULL;
		data[i]->checksum_open = NULL;
		data[i]->timestamp = 0;
	}
}

/**
 * zif_repo_md_master_new:
 *
 * Return value: A new #ZifRepoMdMaster class instance.
 **/
ZifRepoMdMaster *
zif_repo_md_master_new (void)
{
	ZifRepoMdMaster *md;
	md = g_object_new (ZIF_TYPE_REPO_MD_MASTER, NULL);
	return ZIF_REPO_MD_MASTER (md);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_repo_md_master_test (EggTest *test)
{
	ZifRepoMdMaster *md;
	gboolean ret;
	GError *error = NULL;

	if (!egg_test_start (test, "ZifRepoMdMaster"))
		return;

	/************************************************************/
	egg_test_title (test, "get store_remote md");
	md = zif_repo_md_master_new ();
	egg_test_assert (test, md != NULL);

	/************************************************************/
	egg_test_title (test, "set cache dir");
	ret = zif_repo_md_set_cache_dir (ZIF_REPO_MD (md), "../test/cache");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "loaded");
	egg_test_assert (test, !md->priv->loaded);

	/************************************************************/
	egg_test_title (test, "set id");
	ret = zif_repo_md_set_id (ZIF_REPO_MD (md), "fedora");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "load");
	ret = zif_repo_md_load (ZIF_REPO_MD (md), &error);
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

