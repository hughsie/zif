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
 * SECTION:zif-repo-md-metalink
 * @short_description: Metalink metadata functionality
 *
 * Provide access to the metalink repo metadata.
 * This object is a subclass of #ZifRepoMd
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <glib.h>

#include "zif-repo-md.h"
#include "zif-repo-md-metalink.h"

#include "egg-debug.h"
#include "egg-string.h"

#define ZIF_REPO_MD_METALINK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_REPO_MD_METALINK, ZifRepoMdMetalinkPrivate))

typedef enum {
	ZIF_REPO_MD_METALINK_PARSER_SECTION_URL,
	ZIF_REPO_MD_METALINK_PARSER_SECTION_UNKNOWN
} ZifRepoMdMetalinkParserSection;

typedef enum {
	ZIF_REPO_MD_METALINK_PROTOCOL_TYPE_FTP,
	ZIF_REPO_MD_METALINK_PROTOCOL_TYPE_HTTP,
	ZIF_REPO_MD_METALINK_PROTOCOL_TYPE_RSYNC,
	ZIF_REPO_MD_METALINK_PROTOCOL_TYPE_UNKNOWN
} ZifRepoMdMetalinkProtocolType;

typedef struct {
	ZifRepoMdMetalinkProtocolType	 protocol;
	gchar				*uri;
	guint				 preference;
} ZifRepoMdMetalinkData;

/**
 * ZifRepoMdMetalinkPrivate:
 *
 * Private #ZifRepoMdMetalink data
 **/
struct _ZifRepoMdMetalinkPrivate
{
	gboolean			 loaded;
	GPtrArray			*array;
	/* for parser */
	ZifRepoMdMetalinkParserSection	 section;
	ZifRepoMdMetalinkData		*temp;
};

G_DEFINE_TYPE (ZifRepoMdMetalink, zif_repo_md_metalink, ZIF_TYPE_REPO_MD)

/**
 * zif_repo_md_metalink_protocol_type_from_text:
 **/
static ZifRepoMdMetalinkProtocolType
zif_repo_md_metalink_protocol_type_from_text (const gchar *type_text)
{
	if (g_strcmp0 (type_text, "ftp") == 0)
		return ZIF_REPO_MD_METALINK_PROTOCOL_TYPE_FTP;
	if (g_strcmp0 (type_text, "http") == 0)
		return ZIF_REPO_MD_METALINK_PROTOCOL_TYPE_HTTP;
	if (g_strcmp0 (type_text, "rsync") == 0)
		return ZIF_REPO_MD_METALINK_PROTOCOL_TYPE_RSYNC;
	return ZIF_REPO_MD_METALINK_PROTOCOL_TYPE_UNKNOWN;
}

/**
 * zif_repo_md_metalink_parser_start_element:
 **/
static void
zif_repo_md_metalink_parser_start_element (GMarkupParseContext *context, const gchar *element_name,
					   const gchar **attribute_names, const gchar **attribute_values,
					   gpointer user_data, GError **error)
{
	guint i;
	ZifRepoMdMetalink *metalink = user_data;

	g_return_if_fail (ZIF_IS_REPO_MD_METALINK (metalink));
	g_return_if_fail (metalink->priv->temp == NULL);

	/* just ignore non url entries */
	if (g_strcmp0 (element_name, "url") != 0) {
		metalink->priv->temp = NULL;
		metalink->priv->section = ZIF_REPO_MD_METALINK_PARSER_SECTION_UNKNOWN;
		goto out;
	}

	/* create new element */
	metalink->priv->section = ZIF_REPO_MD_METALINK_PARSER_SECTION_URL;
	metalink->priv->temp = g_new0 (ZifRepoMdMetalinkData, 1);

	/* read keys */
	for (i=0; attribute_names[i] != NULL; i++) {
		if (g_strcmp0 (attribute_names[i], "protocol") == 0)
			metalink->priv->temp->protocol = zif_repo_md_metalink_protocol_type_from_text (attribute_values[i]);
		if (g_strcmp0 (attribute_names[i], "preference") == 0)
			metalink->priv->temp->preference = atoi (attribute_values[i]);
	}

	/* add to array */
	g_ptr_array_add (metalink->priv->array, metalink->priv->temp);
out:
	return;
}

/**
 * zif_repo_md_metalink_parser_end_element:
 **/
static void
zif_repo_md_metalink_parser_end_element (GMarkupParseContext *context, const gchar *element_name,
					 gpointer user_data, GError **error)
{
	ZifRepoMdMetalink *metalink = user_data;
	metalink->priv->temp = NULL;
	metalink->priv->section = ZIF_REPO_MD_METALINK_PARSER_SECTION_UNKNOWN;
}


/**
 * zif_repo_md_metalink_parser_text:
 **/
static void
zif_repo_md_metalink_parser_text (GMarkupParseContext *context, const gchar *text, gsize text_len,
				  gpointer user_data, GError **error)

{
	ZifRepoMdMetalink *metalink = user_data;

	if (metalink->priv->section != ZIF_REPO_MD_METALINK_PARSER_SECTION_URL)
		goto out;

	/* shouldn't happen */
	if (metalink->priv->temp == NULL) {
		egg_warning ("no data!");
		goto out;
	}

	/* save uri */
	metalink->priv->temp->uri = g_strdup (text);
out:
	return;
}

/**
 * zif_repo_md_metalink_clean:
 **/
static gboolean
zif_repo_md_metalink_clean (ZifRepoMd *md, GError **error)
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
			*error = g_error_new (1, 0, "failed to get filename for metalink");
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
 * zif_repo_md_metalink_load:
 **/
static gboolean
zif_repo_md_metalink_load (ZifRepoMd *md, GError **error)
{
	gboolean ret = TRUE;
	gchar *contents = NULL;
	const gchar *filename;
	gsize size;
	GMarkupParseContext *context = NULL;
	const GMarkupParser gpk_repo_md_metalink_markup_parser = {
		zif_repo_md_metalink_parser_start_element,
		zif_repo_md_metalink_parser_end_element,
		zif_repo_md_metalink_parser_text,
		NULL, /* passthrough */
		NULL /* error */
	};
	ZifRepoMdMetalink *metalink = ZIF_REPO_MD_METALINK (md);

	g_return_val_if_fail (ZIF_IS_REPO_MD_METALINK (md), FALSE);

	/* already loaded */
	if (metalink->priv->loaded)
		goto out;

	/* get filename */
	filename = zif_repo_md_get_filename_uncompressed (md);
	if (filename == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get filename for metalink");
		goto out;
	}

	/* open database */
	egg_debug ("filename = %s", filename);

	/* get repo contents */
	ret = g_file_get_contents (filename, &contents, &size, error);
	if (!ret)
		goto out;

	/* create parser */
	context = g_markup_parse_context_new (&gpk_repo_md_metalink_markup_parser, G_MARKUP_PREFIX_ERROR_POSITION, metalink, NULL);

	/* parse data */
	ret = g_markup_parse_context_parse (context, contents, (gssize) size, error);
	if (!ret)
		goto out;

	metalink->priv->loaded = TRUE;
out:
	if (context != NULL)
		g_markup_parse_context_free (context);
	g_free (contents);
	return ret;
}

/**
 * zif_repo_md_metalink_get_mirrors:
 * @md: the #ZifRepoMdMetalink object
 * @threshold: the threshold in percent
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all mirrors we should use.
 *
 * Return value: the uris to use as an array of strings
 **/
GPtrArray *
zif_repo_md_metalink_get_mirrors (ZifRepoMdMetalink *md, guint threshold, GError **error)
{
	gboolean ret;
	guint len;
	GPtrArray *array = NULL;
	GError *error_local = NULL;
	ZifRepoMdMetalinkData *data;
	guint i;
	ZifRepoMdMetalink *metalink = ZIF_REPO_MD_METALINK (md);

	g_return_val_if_fail (ZIF_IS_REPO_MD_METALINK (md), FALSE);

	/* if not already loaded, load */
	if (!metalink->priv->loaded) {
		ret = zif_repo_md_metalink_load (ZIF_REPO_MD (md), &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to get mirrors from metalink: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* get list */
	array = g_ptr_array_new ();
	len = metalink->priv->array->len;
	for (i=0; i<len; i++) {
		data = g_ptr_array_index (metalink->priv->array, i);

		/* ignore not http mirrors */
		if (data->protocol != ZIF_REPO_MD_METALINK_PROTOCOL_TYPE_HTTP)
			continue;

		/* ignore low priority */
		if (data->preference >= threshold)
			g_ptr_array_add (array, g_strdup (data->uri));
	}
out:
	return array;
}

/**
 * zif_repo_md_metalink_finalize:
 **/
static void
zif_repo_md_metalink_finalize (GObject *object)
{
	ZifRepoMdMetalink *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_REPO_MD_METALINK (object));
	md = ZIF_REPO_MD_METALINK (object);

	g_ptr_array_foreach (md->priv->array, (GFunc) g_free, NULL);
	g_ptr_array_free (md->priv->array, TRUE);

	G_OBJECT_CLASS (zif_repo_md_metalink_parent_class)->finalize (object);
}

/**
 * zif_repo_md_metalink_class_init:
 **/
static void
zif_repo_md_metalink_class_init (ZifRepoMdMetalinkClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifRepoMdClass *repo_md_class = ZIF_REPO_MD_CLASS (klass);
	object_class->finalize = zif_repo_md_metalink_finalize;

	/* map */
	repo_md_class->load = zif_repo_md_metalink_load;
	repo_md_class->clean = zif_repo_md_metalink_clean;
	g_type_class_add_private (klass, sizeof (ZifRepoMdMetalinkPrivate));
}

/**
 * zif_repo_md_metalink_init:
 **/
static void
zif_repo_md_metalink_init (ZifRepoMdMetalink *md)
{
	md->priv = ZIF_REPO_MD_METALINK_GET_PRIVATE (md);
	md->priv->loaded = FALSE;
	md->priv->array = g_ptr_array_new ();
}

/**
 * zif_repo_md_metalink_new:
 *
 * Return value: A new #ZifRepoMdMetalink class instance.
 **/
ZifRepoMdMetalink *
zif_repo_md_metalink_new (void)
{
	ZifRepoMdMetalink *md;
	md = g_object_new (ZIF_TYPE_REPO_MD_METALINK, NULL);
	return ZIF_REPO_MD_METALINK (md);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_repo_md_metalink_test (EggTest *test)
{
	ZifRepoMdMetalink *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	const gchar *uri;

	if (!egg_test_start (test, "ZifRepoMdMetalink"))
		return;

	/************************************************************/
	egg_test_title (test, "get repo_md_metalink md");
	md = zif_repo_md_metalink_new ();
	egg_test_assert (test, md != NULL);

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
	egg_test_title (test, "set filename");
	ret = zif_repo_md_set_filename (ZIF_REPO_MD (md), "../test/cache/fedora/metalink.xml");
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

	/************************************************************/
	egg_test_title (test, "get mirros(50)");
	array = zif_repo_md_metalink_get_mirrors (md, 50, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to search '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "correct number");
	if (array->len == 44)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect value %i", array->len);

	/************************************************************/
	egg_test_title (test, "correct value");
	uri = g_ptr_array_index (array, 0);
	if (g_strcmp0 (uri, "http://www.mirrorservice.org/sites/download.fedora.redhat.com/pub/fedora/linux/releases/11/Everything/i386/os/repodata/repomd.xml") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get correct url '%s'", uri);

	g_ptr_array_foreach (array, (GFunc) g_free, NULL);
	g_ptr_array_free (array, TRUE);

	g_object_unref (md);

	egg_test_end (test);
}
#endif
