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
#include <stdlib.h>
#include <gio/gio.h>

#include "dum-repo-md.h"
#include "dum-repo-md-master.h"

#include "egg-debug.h"
#include "egg-string.h"

#define DUM_REPO_MD_MASTER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DUM_TYPE_REPO_MD_MASTER, DumRepoMdMasterPrivate))

typedef enum {
	DUM_REPO_MD_MASTER_PARSER_SECTION_CHECKSUM,
	DUM_REPO_MD_MASTER_PARSER_SECTION_CHECKSUM_OPEN,
	DUM_REPO_MD_MASTER_PARSER_SECTION_TIMESTAMP,
	DUM_REPO_MD_MASTER_PARSER_SECTION_UNKNOWN
} DumRepoMdMasterParserSection;

struct DumRepoMdMasterPrivate
{
	gboolean		 loaded;
	DumRepoMdInfoData	*data[DUM_REPO_MD_TYPE_UNKNOWN];
	DumRepoMdType		 parser_type;
	DumRepoMdType		 parser_section;
};

G_DEFINE_TYPE (DumRepoMdMaster, dum_repo_md_master, DUM_TYPE_REPO_MD)

/**
 * dum_repo_md_master_checksum_type_from_text:
 **/
static GChecksumType
dum_repo_md_master_checksum_type_from_text (const gchar *type)
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
 * dum_repo_md_master_parser_start_element:
 **/
static void
dum_repo_md_master_parser_start_element (GMarkupParseContext *context, const gchar *element_name,
				  const gchar **attribute_names, const gchar **attribute_values,
				  gpointer user_data, GError **error)
{
	guint i;
	DumRepoMdMaster *md = user_data;
	DumRepoMdType parser_type = md->priv->parser_type;

	/* data */
	if (strcmp (element_name, "data") == 0) {

		/* reset */
		md->priv->parser_type = DUM_REPO_MD_TYPE_UNKNOWN;

		/* find type */
		for (i=0; attribute_names[i] != NULL; i++) {
			if (strcmp (attribute_names[i], "type") == 0) {
				if (strcmp (attribute_values[i], "primary_db") == 0)
					md->priv->parser_type = DUM_REPO_MD_TYPE_PRIMARY;
				else if (strcmp (attribute_values[i], "filelists_db") == 0)
					md->priv->parser_type = DUM_REPO_MD_TYPE_FILELISTS;
				else if (strcmp (attribute_values[i], "other_db") == 0)
					md->priv->parser_type = DUM_REPO_MD_TYPE_OTHER;
				else if (strcmp (attribute_values[i], "group_gz") == 0)
					md->priv->parser_type = DUM_REPO_MD_TYPE_COMPS;
				break;
			}
		}
		md->priv->parser_section = DUM_REPO_MD_MASTER_PARSER_SECTION_UNKNOWN;
		goto out;
	}

	/* not a section we recognise */
	if (md->priv->parser_type == DUM_REPO_MD_TYPE_UNKNOWN)
		goto out;

	/* location */
	if (strcmp (element_name, "location") == 0) {
		for (i=0; attribute_names[i] != NULL; i++) {
			if (strcmp (attribute_names[i], "href") == 0) {
				md->priv->data[parser_type]->location = g_strdup (attribute_values[i]);
				break;
			}
		}
		md->priv->parser_section = DUM_REPO_MD_MASTER_PARSER_SECTION_UNKNOWN;
		goto out;
	}

	/* checksum */
	if (strcmp (element_name, "checksum") == 0) {
		for (i=0; attribute_names[i] != NULL; i++) {
			if (strcmp (attribute_names[i], "type") == 0) {
				md->priv->data[parser_type]->checksum_type = dum_repo_md_master_checksum_type_from_text (attribute_values[i]);
				break;
			}
		}
		md->priv->parser_section = DUM_REPO_MD_MASTER_PARSER_SECTION_CHECKSUM;
		goto out;
	}

	/* checksum */
	if (strcmp (element_name, "open-checksum") == 0) {
		md->priv->parser_section = DUM_REPO_MD_MASTER_PARSER_SECTION_CHECKSUM_OPEN;
		goto out;
	}

	/* timestamp */
	if (strcmp (element_name, "timestamp") == 0) {
		md->priv->parser_section = DUM_REPO_MD_MASTER_PARSER_SECTION_TIMESTAMP;
		goto out;
	}
out:
	return;
}

/**
 * dum_repo_md_master_parser_end_element:
 **/
static void
dum_repo_md_master_parser_end_element (GMarkupParseContext *context, const gchar *element_name,
				gpointer user_data, GError **error)
{
	DumRepoMdMaster *md = user_data;

	/* reset */
	md->priv->parser_section = DUM_REPO_MD_MASTER_PARSER_SECTION_UNKNOWN;
	if (strcmp (element_name, "data") == 0)
		md->priv->parser_type = DUM_REPO_MD_TYPE_UNKNOWN;
}


/**
 * dum_repo_md_master_parser_text:
 **/
static void
dum_repo_md_master_parser_text (GMarkupParseContext *context, const gchar *text, gsize text_len,
			 gpointer user_data, GError **error)

{
	DumRepoMdMaster *md = user_data;
	DumRepoMdType parser_type = md->priv->parser_type;

	if (parser_type == DUM_REPO_MD_TYPE_UNKNOWN)
		return;

	if (md->priv->parser_section == DUM_REPO_MD_MASTER_PARSER_SECTION_CHECKSUM)
		md->priv->data[parser_type]->checksum = g_strdup (text);
	else if (md->priv->parser_section == DUM_REPO_MD_MASTER_PARSER_SECTION_CHECKSUM_OPEN)
		md->priv->data[parser_type]->checksum_open = g_strdup (text);
	else if (md->priv->parser_section == DUM_REPO_MD_MASTER_PARSER_SECTION_TIMESTAMP)
		md->priv->data[parser_type]->timestamp = atol (text);
}

/**
 * dum_repo_md_master_clean:
 **/
static gboolean
dum_repo_md_master_clean (DumRepoMd *md, GError **error)
{
	gboolean ret = FALSE;
	gboolean exists;
	const gchar *filename;
	GFile *file;
	GError *error_local = NULL;

	/* get filename */
	filename = dum_repo_md_get_filename (md);
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
 * dum_repo_md_master_load:
 **/
static gboolean
dum_repo_md_master_load (DumRepoMd *md, GError **error)
{
	guint i;
	DumRepoMdInfoData **data;
	gboolean ret = TRUE;
	gchar *contents = NULL;
	const gchar *filename = NULL;
	gsize size;
	GMarkupParseContext *context = NULL;
	const GMarkupParser gpk_repo_md_master_markup_parser = {
		dum_repo_md_master_parser_start_element,
		dum_repo_md_master_parser_end_element,
		dum_repo_md_master_parser_text,
		NULL, /* passthrough */
		NULL /* error */
	};
	DumRepoMdMaster *master = DUM_REPO_MD_MASTER (md);

	g_return_val_if_fail (DUM_IS_REPO_MD_MASTER (md), FALSE);

	/* already loaded */
	if (master->priv->loaded)
		goto out;

	/* get contents */
	dum_repo_md_set_base_filename (DUM_REPO_MD (md), "repomd.xml");
	filename = dum_repo_md_get_filename (md);
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
	for (i=0; i<DUM_REPO_MD_TYPE_UNKNOWN; i++) {
		if (data[i]->location != NULL && (data[i]->checksum == NULL || data[i]->timestamp == 0)) {
			if (error != NULL)
				*error = g_error_new (1, 0, "cannot load md for %s (loc=%s, sum=%s, sum_open=%s, ts=%i)",
						      dum_repo_md_type_to_text (i), data[i]->location, data[i]->checksum, data[i]->checksum_open, data[i]->timestamp);
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
 * dum_repo_md_master_get_info:
 **/
const DumRepoMdInfoData *
dum_repo_md_master_get_info (DumRepoMdMaster *md, DumRepoMdType type, GError **error)
{
	GError *error_local = NULL;
	gboolean ret;
	const DumRepoMdInfoData *infodata = NULL;

	g_return_val_if_fail (DUM_IS_REPO_MD_MASTER (md), NULL);
	g_return_val_if_fail (type != DUM_REPO_MD_TYPE_UNKNOWN, NULL);

	/* if not already loaded, load */
	if (!md->priv->loaded) {
		ret = dum_repo_md_master_load (DUM_REPO_MD (md), &error_local);
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
 * dum_repo_md_master_check:
 **/
gboolean
dum_repo_md_master_check (DumRepoMdMaster *md, DumRepoMdType type, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	gchar *filename = NULL;
	gchar *data = NULL;
	gchar *checksum = NULL;
	const gchar *checksum_wanted = NULL;
	gsize length;

	g_return_val_if_fail (DUM_IS_REPO_MD_MASTER (md), FALSE);
	g_return_val_if_fail (type != DUM_REPO_MD_TYPE_UNKNOWN, FALSE);
	g_return_val_if_fail (md->priv->id != NULL, FALSE);

	/* if not already loaded, load */
	if (!md->priv->loaded) {
		ret = dum_repo_md_master_load (md, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load metadata: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* get correct filename */
	filename = dum_repo_md_master_get_filename (md, type, NULL);
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
	if (type == DUM_REPO_MD_TYPE_COMPS)
		checksum_wanted = md->priv->data[type]->checksum;
	else
		checksum_wanted = md->priv->data[type]->checksum_open;

	/* matches? */
	ret = (strcmp (checksum, checksum_wanted) == 0);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "%s checksum incorrect, wanted %s, got %s", dum_repo_md_type_to_text (type), checksum_wanted, checksum);
	}
out:
	g_free (data);
	g_free (filename);
	g_free (checksum);
	return ret;
}
#endif

/**
 * dum_repo_md_master_finalize:
 **/
static void
dum_repo_md_master_finalize (GObject *object)
{
	guint i;
	DumRepoMdMaster *md;
	DumRepoMdInfoData **data;

	g_return_if_fail (object != NULL);
	g_return_if_fail (DUM_IS_REPO_MD_MASTER (object));
	md = DUM_REPO_MD_MASTER (object);

	data = md->priv->data;
	for (i=0; i<DUM_REPO_MD_TYPE_UNKNOWN; i++) {
		g_free (data[i]->location);
		g_free (data[i]->checksum);
		g_free (data[i]->checksum_open);
		g_free (data[i]);
	}

	G_OBJECT_CLASS (dum_repo_md_master_parent_class)->finalize (object);
}

/**
 * dum_repo_md_master_class_init:
 **/
static void
dum_repo_md_master_class_init (DumRepoMdMasterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	DumRepoMdClass *repo_md_class = DUM_REPO_MD_CLASS (klass);
	object_class->finalize = dum_repo_md_master_finalize;

	/* map */
	repo_md_class->load = dum_repo_md_master_load;
	repo_md_class->clean = dum_repo_md_master_clean;

	g_type_class_add_private (klass, sizeof (DumRepoMdMasterPrivate));
}

/**
 * dum_repo_md_master_init:
 **/
static void
dum_repo_md_master_init (DumRepoMdMaster *md)
{
	guint i;
	DumRepoMdInfoData **data;

	md->priv = DUM_REPO_MD_MASTER_GET_PRIVATE (md);
	md->priv->loaded = FALSE;
	md->priv->parser_type = DUM_REPO_MD_TYPE_UNKNOWN;
	md->priv->parser_section = DUM_REPO_MD_MASTER_PARSER_SECTION_UNKNOWN;

	data = md->priv->data;
	for (i=0; i<DUM_REPO_MD_TYPE_UNKNOWN; i++) {
		data[i] = g_new0 (DumRepoMdInfoData, 1);
		data[i]->location = NULL;
		data[i]->checksum = NULL;
		data[i]->checksum_open = NULL;
		data[i]->timestamp = 0;
	}
}

/**
 * dum_repo_md_master_new:
 * Return value: A new repo_md_master class instance.
 **/
DumRepoMdMaster *
dum_repo_md_master_new (void)
{
	DumRepoMdMaster *md;
	md = g_object_new (DUM_TYPE_REPO_MD_MASTER, NULL);
	return DUM_REPO_MD_MASTER (md);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
dum_repo_md_master_test (EggTest *test)
{
	DumRepoMdMaster *md;
	gboolean ret;
	GError *error = NULL;

	if (!egg_test_start (test, "DumRepoMdMaster"))
		return;

	/************************************************************/
	egg_test_title (test, "get store_remote md");
	md = dum_repo_md_master_new ();
	egg_test_assert (test, md != NULL);

	/************************************************************/
	egg_test_title (test, "set cache dir");
	ret = dum_repo_md_set_cache_dir (DUM_REPO_MD (md), "../test/cache");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "loaded");
	egg_test_assert (test, !md->priv->loaded);

	/************************************************************/
	egg_test_title (test, "set id");
	ret = dum_repo_md_set_id (DUM_REPO_MD (md), "fedora");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "load");
	ret = dum_repo_md_load (DUM_REPO_MD (md), &error);
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

