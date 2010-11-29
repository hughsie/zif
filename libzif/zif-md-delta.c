/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-md-delta
 * @short_description: Delta metadata
 *
 * Provide access to the delta repo metadata.
 * This object is a subclass of #ZifMd
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <glib.h>

#include "zif-md.h"
#include "zif-md-delta.h"
#include "zif-delta.h"
#include "zif-utils.h"

#define ZIF_MD_DELTA_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_MD_DELTA, ZifMdDeltaPrivate))

typedef enum {
	ZIF_MD_DELTA_XML_NEWPACKAGE,
	ZIF_MD_DELTA_XML_UNKNOWN
} ZifMdDeltaXml;

typedef enum {
	ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA,
	ZIF_MD_DELTA_XML_NEWPACKAGE_UNKNOWN
} ZifMdDeltaXmlNewpackage;

typedef enum {
	ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_FILENAME,
	ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_SIZE,
	ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_SEQUENCE,
	ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_CHECKSUM,
	ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_UNKNOWN
} ZifMdDeltaXmlNewpackageDelta;

/**
 * ZifMdDeltaPrivate:
 *
 * Private #ZifMdDelta data
 **/
struct _ZifMdDeltaPrivate
{
	gboolean			 loaded;
	GHashTable			*hash_newpackages;		/* value as GPtrArray */
	/* for parser */
	ZifMdDeltaXml			 section;
	ZifMdDeltaXmlNewpackage		 section_newpackage;
	ZifMdDeltaXmlNewpackageDelta	 section_newpackage_delta;
	ZifDelta			*delta_temp;
	GPtrArray			*array_temp;
	ZifPackage			*package_temp;
	gchar				*name_temp;
	gchar				*arch_temp;
};

G_DEFINE_TYPE (ZifMdDelta, zif_md_delta, ZIF_TYPE_MD)

/**
 * zif_md_delta_parser_start_element:
 **/
static void
zif_md_delta_parser_start_element (GMarkupParseContext *context, const gchar *element_name,
				   const gchar **attribute_names, const gchar **attribute_values,
				   gpointer user_data, GError **error)
{
	guint i;
	gchar *package_id = NULL;
	ZifMdDelta *delta = user_data;
	const gchar *name = NULL;
	guint epoch = 0;
	const gchar *version = NULL;
	const gchar *release = NULL;
	const gchar *arch = NULL;

	g_return_if_fail (ZIF_IS_MD_DELTA (delta));

	/* group element */
	if (delta->priv->section == ZIF_MD_DELTA_XML_UNKNOWN) {

		/* start of list */
		if (g_strcmp0 (element_name, "prestodelta") == 0)
			goto out;

		/* start of delta */
		if (g_strcmp0 (element_name, "newpackage") == 0) {
			delta->priv->section = ZIF_MD_DELTA_XML_NEWPACKAGE;

			/* find the package-id */
			for (i=0; attribute_names[i] != NULL; i++) {
				if (g_strcmp0 (attribute_names[i], "name") == 0)
					name = attribute_values[i];
				else if (g_strcmp0 (attribute_names[i], "epoch") == 0)
					epoch = atoi (attribute_values[i]);
				else if (g_strcmp0 (attribute_names[i], "version") == 0)
					version = attribute_values[i];
				else if (g_strcmp0 (attribute_names[i], "release") == 0)
					release = attribute_values[i];
				else if (g_strcmp0 (attribute_names[i], "arch") == 0)
					arch = attribute_values[i];
			}

			/* use this as the key for the hash table */
			package_id = zif_package_id_from_nevra (name, epoch, version, release, arch,
								zif_md_get_id (ZIF_MD(delta)));
			g_debug ("adding update package_id=%s", package_id);

			/* we carry this around so we can add deltas to it */
			delta->priv->array_temp = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
			g_hash_table_insert (delta->priv->hash_newpackages, g_strdup (package_id), delta->priv->array_temp);

			/* required so we can construct a full package id for the deltas */
			delta->priv->name_temp = g_strdup (name);
			delta->priv->arch_temp = g_strdup (arch);
			goto out;
		}

		g_warning ("unhandled element: %s", element_name);
		goto out;
	}

	/* delta element */
	if (delta->priv->section == ZIF_MD_DELTA_XML_NEWPACKAGE) {
		if (delta->priv->section_newpackage == ZIF_MD_DELTA_XML_NEWPACKAGE_UNKNOWN) {
			if (g_strcmp0 (element_name, "delta") == 0) {
				delta->priv->section_newpackage = ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA;
				delta->priv->delta_temp = zif_delta_new ();

				/* find the package-id */
				for (i=0; attribute_names[i] != NULL; i++) {
					if (g_strcmp0 (attribute_names[i], "oldepoch") == 0)
						epoch = atoi (attribute_values[i]);
					else if (g_strcmp0 (attribute_names[i], "oldversion") == 0)
						version = attribute_values[i];
					else if (g_strcmp0 (attribute_names[i], "oldrelease") == 0)
						release = attribute_values[i];
				}

				/* use this as the key for the hash table */
				package_id = zif_package_id_from_nevra (delta->priv->name_temp, epoch, version,
									release, delta->priv->arch_temp,
									zif_md_get_id (ZIF_MD(delta)));
				zif_delta_set_id (delta->priv->delta_temp, package_id);
				g_ptr_array_add (delta->priv->array_temp, delta->priv->delta_temp);
				g_debug ("adding delta package_id=%s", package_id);

				goto out;
			}

			g_warning ("unhandled newpackage-unknown base tag: %s", element_name);
			goto out;
		}

		if (delta->priv->section_newpackage == ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA) {
			if (g_strcmp0 (element_name, "filename") == 0) {
				delta->priv->section_newpackage_delta = ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_FILENAME;
				goto out;
			}
			if (g_strcmp0 (element_name, "sequence") == 0) {
				delta->priv->section_newpackage_delta = ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_SEQUENCE;
				goto out;
			}
			if (g_strcmp0 (element_name, "size") == 0) {
				delta->priv->section_newpackage_delta = ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_SIZE;
				goto out;
			}
			if (g_strcmp0 (element_name, "checksum") == 0) {
				delta->priv->section_newpackage_delta = ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_CHECKSUM;
				goto out;
			}
			g_warning ("unhandled newpackage-delta base tag: %s", element_name);
			goto out;
		}
		g_warning ("unexpected delta tag: %s", element_name);
		goto out;
	}

	g_warning ("unhandled base tag: %s", element_name);

out:
	g_free (package_id);
	return;
}

/**
 * zif_md_delta_parser_end_element:
 **/
static void
zif_md_delta_parser_end_element (GMarkupParseContext *context, const gchar *element_name,
				      gpointer user_data, GError **error)
{
	ZifMdDelta *delta = user_data;

	/* no element */
	if (delta->priv->section == ZIF_MD_DELTA_XML_UNKNOWN) {

		/* end of list */
		if (g_strcmp0 (element_name, "prestodelta") == 0)
			goto out;

		g_warning ("unhandled base end tag: %s", element_name);
		goto out;
	}

	if (delta->priv->section == ZIF_MD_DELTA_XML_NEWPACKAGE) {

		if (delta->priv->section_newpackage == ZIF_MD_DELTA_XML_NEWPACKAGE_UNKNOWN) {
			/* end of package */
			if (g_strcmp0 (element_name, "newpackage") == 0) {
				delta->priv->section = ZIF_MD_DELTA_XML_UNKNOWN;
				delta->priv->array_temp = NULL;
				g_free (delta->priv->name_temp);
				g_free (delta->priv->arch_temp);
				delta->priv->name_temp = NULL;
				delta->priv->arch_temp = NULL;
				goto out;
			}

			g_warning ("unhandled newpackage-unknown end tag: %s", element_name);
			goto out;
		}

		if (delta->priv->section_newpackage == ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA) {

			if (delta->priv->section_newpackage_delta == ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_UNKNOWN) {

				/* end of delta */
				if (g_strcmp0 (element_name, "delta") == 0) {
					delta->priv->section_newpackage = ZIF_MD_DELTA_XML_NEWPACKAGE_UNKNOWN;
					delta->priv->delta_temp = NULL;
					goto out;
				}
				g_warning ("unhandled newpackage-delta-unknown end tag: %s", element_name);
				goto out;
			}

			if (delta->priv->section_newpackage_delta == ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_FILENAME) {

				/* end of delta */
				if (g_strcmp0 (element_name, "filename") == 0) {
					delta->priv->section_newpackage_delta = ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_UNKNOWN;
					goto out;
				}
				g_warning ("unhandled newpackage-delta-filename end tag: %s", element_name);
				goto out;
			}

			if (delta->priv->section_newpackage_delta == ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_CHECKSUM) {

				/* end of delta */
				if (g_strcmp0 (element_name, "checksum") == 0) {
					delta->priv->section_newpackage_delta = ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_UNKNOWN;
					goto out;
				}
				g_warning ("unhandled newpackage-delta-checksum end tag: %s", element_name);
				goto out;
			}

			if (delta->priv->section_newpackage_delta == ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_SEQUENCE) {

				/* end of delta */
				if (g_strcmp0 (element_name, "sequence") == 0) {
					delta->priv->section_newpackage_delta = ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_UNKNOWN;
					goto out;
				}
				g_warning ("unhandled newpackage-delta-sequence end tag: %s", element_name);
				goto out;
			}

			if (delta->priv->section_newpackage_delta == ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_SIZE) {

				/* end of delta */
				if (g_strcmp0 (element_name, "size") == 0) {
					delta->priv->section_newpackage_delta = ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_UNKNOWN;
					goto out;
				}
				g_warning ("unhandled newpackage-delta-size end tag: %s", element_name);
				goto out;
			}

			g_warning ("unhandled newpackage-delta end tag: %s", element_name);
			goto out;
		}

		g_warning ("unhandled delta end tag: %s", element_name);
		goto out;
	}

	g_warning ("unhandled end tag: %s", element_name);
out:
	return;
}

/**
 * zif_md_delta_parser_text:
 **/
static void
zif_md_delta_parser_text (GMarkupParseContext *context, const gchar *text, gsize text_len,
			       gpointer user_data, GError **error)

{
	ZifMdDelta *delta = user_data;

	/* skip whitespace */
	if (text_len < 1 || text[0] == ' ' || text[0] == '\t' || text[0] == '\n')
		goto out;

	/* group section */
	if (delta->priv->section == ZIF_MD_DELTA_XML_NEWPACKAGE) {
		if (delta->priv->section_newpackage == ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA) {
			if (delta->priv->section_newpackage_delta == ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_FILENAME) {
				zif_delta_set_filename (delta->priv->delta_temp, text);
				goto out;
			}
			if (delta->priv->section_newpackage_delta == ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_CHECKSUM) {
				zif_delta_set_checksum (delta->priv->delta_temp, text);
				goto out;
			}
			if (delta->priv->section_newpackage_delta == ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_SEQUENCE) {
				zif_delta_set_sequence (delta->priv->delta_temp, text);
				goto out;
			}
			if (delta->priv->section_newpackage_delta == ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_SIZE) {
				zif_delta_set_size (delta->priv->delta_temp, atoi (text));
				goto out;
			}
			g_warning ("unhandled newpackage-delta text tag: %s", text);
			goto out;
		}
		g_warning ("unhandled newpackage text tag: %s", text);
		goto out;
	}
out:
	return;
}

/**
 * zif_md_delta_unload:
 **/
static gboolean
zif_md_delta_unload (ZifMd *md, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	return ret;
}

/**
 * zif_md_delta_load:
 **/
static gboolean
zif_md_delta_load (ZifMd *md, ZifState *state, GError **error)
{
	gboolean ret = TRUE;
	gchar *contents = NULL;
	const gchar *filename;
	gsize size;
	GMarkupParseContext *context = NULL;
	const GMarkupParser gpk_md_delta_markup_parser = {
		zif_md_delta_parser_start_element,
		zif_md_delta_parser_end_element,
		zif_md_delta_parser_text,
		NULL, /* passthrough */
		NULL /* error */
	};
	ZifMdDelta *delta = ZIF_MD_DELTA (md);

	g_return_val_if_fail (ZIF_IS_MD_DELTA (md), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* already loaded */
	if (delta->priv->loaded)
		goto out;

	/* get filename */
	filename = zif_md_get_filename_uncompressed (md);
	if (filename == NULL) {
		g_set_error_literal (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
				     "failed to get filename for delta");
		goto out;
	}

	/* open database */
	g_debug ("filename = %s", filename);

	/* get repo contents */
	zif_state_set_allow_cancel (state, FALSE);
	ret = g_file_get_contents (filename, &contents, &size, error);
	if (!ret)
		goto out;

	/* create parser */
	context = g_markup_parse_context_new (&gpk_md_delta_markup_parser, G_MARKUP_PREFIX_ERROR_POSITION, delta, NULL);

	/* parse data */
	zif_state_set_allow_cancel (state, FALSE);
	ret = g_markup_parse_context_parse (context, contents, (gssize) size, error);
	if (!ret)
		goto out;

	delta->priv->loaded = TRUE;
out:
	if (context != NULL)
		g_markup_parse_context_free (context);
	g_free (contents);
	return ret;
}

/**
 * zif_md_delta_get_array_for_package:
 **/
static GPtrArray *
zif_md_delta_get_array_for_package (ZifMdDelta *md, const gchar *package_id, ZifState *state, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;

	/* if not already loaded, load */
	if (!md->priv->loaded) {
		ret = zif_md_load (ZIF_MD (md), state, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to get load delta: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* look this up in the hash */
	array = g_hash_table_lookup (md->priv->hash_newpackages, package_id);
	if (array == NULL) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
			     "could not find update package: %s", package_id);
		goto out;
	}
out:
	return array;
}

/**
 * zif_md_delta_search_for_package:
 * @md: the #ZifMdDelta object
 * @package_id_update: the package-id that is available as an update
 * @package_id_installed: the package-id that is installed
 * @state: the %ZifState object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the delta details for the package_id.
 *
 * Return value: #ZifDelta or %NULL, free with g_object_unref()
 *
 * Since: 0.1.0
 **/
ZifDelta *
zif_md_delta_search_for_package (ZifMdDelta *md,
				 const gchar *package_id_update, const gchar *package_id_installed,
				 ZifState *state, GError **error)
{
	guint i;
	ZifDelta *delta = NULL;
	ZifDelta *delta_tmp;
	GPtrArray *array = NULL;

	g_return_val_if_fail (ZIF_IS_MD_DELTA (md), NULL);
	g_return_val_if_fail (package_id_update != NULL, NULL);
	g_return_val_if_fail (package_id_installed != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* get the array of possible deltas */
	array = zif_md_delta_get_array_for_package (md, package_id_update, state, error);
	if (array == NULL)
		goto out;

	/* find the installed package */
	for (i=0; i<array->len; i++) {
		delta_tmp = g_ptr_array_index (array, i);
		if (g_strcmp0 (zif_delta_get_id (delta_tmp), package_id_installed) == 0) {
			delta = g_object_ref (delta_tmp);
			break;
		}
	}

	/* nothing found */
	if (delta == NULL) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
			     "could not find installed package: %s", package_id_installed);
	}
out:
	return delta;
}

/**
 * zif_md_delta_finalize:
 **/
static void
zif_md_delta_finalize (GObject *object)
{
	ZifMdDelta *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_MD_DELTA (object));
	md = ZIF_MD_DELTA (object);

	g_hash_table_unref (md->priv->hash_newpackages);

	G_OBJECT_CLASS (zif_md_delta_parent_class)->finalize (object);
}

/**
 * zif_md_delta_class_init:
 **/
static void
zif_md_delta_class_init (ZifMdDeltaClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifMdClass *md_class = ZIF_MD_CLASS (klass);
	object_class->finalize = zif_md_delta_finalize;

	/* map */
	md_class->load = zif_md_delta_load;
	md_class->unload = zif_md_delta_unload;
	g_type_class_add_private (klass, sizeof (ZifMdDeltaPrivate));
}

/**
 * zif_md_delta_init:
 **/
static void
zif_md_delta_init (ZifMdDelta *md)
{
	md->priv = ZIF_MD_DELTA_GET_PRIVATE (md);
	md->priv->loaded = FALSE;
	md->priv->section = ZIF_MD_DELTA_XML_UNKNOWN;
	md->priv->section_newpackage = ZIF_MD_DELTA_XML_NEWPACKAGE_UNKNOWN;
	md->priv->section_newpackage_delta = ZIF_MD_DELTA_XML_NEWPACKAGE_DELTA_UNKNOWN;
	md->priv->delta_temp = NULL;
	md->priv->array_temp = NULL;
	md->priv->hash_newpackages = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_ptr_array_unref);
}

/**
 * zif_md_delta_new:
 *
 * Return value: A new #ZifMdDelta class instance.
 *
 * Since: 0.1.0
 **/
ZifMd *
zif_md_delta_new (void)
{
	ZifMdDelta *md;
	md = g_object_new (ZIF_TYPE_MD_DELTA,
			   "kind", ZIF_MD_KIND_PRESTODELTA,
			   NULL);
	return ZIF_MD (md);
}

