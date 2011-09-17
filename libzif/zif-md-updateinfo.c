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
 * SECTION:zif-md-updateinfo
 * @short_description: Updateinfo metadata
 *
 * Provide access to the updateinfo repo metadata.
 * This object is a subclass of #ZifMd
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <glib.h>

#include "zif-config.h"
#include "zif-md.h"
#include "zif-md-updateinfo.h"
#include "zif-update.h"
#include "zif-update-info.h"
#include "zif-utils.h"

#define ZIF_MD_UPDATEINFO_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_MD_UPDATEINFO, ZifMdUpdateinfoPrivate))

typedef enum {
	ZIF_MD_UPDATEINFO_SECTION_UPDATE,
	ZIF_MD_UPDATEINFO_SECTION_UNKNOWN
} ZifMdUpdateinfoSection;

typedef enum {
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_ID,
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_TITLE,
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_DESCRIPTION,
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_ISSUED,
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_REBOOT,
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_REFERENCES,
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_PKGLIST,
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_UNKNOWN
} ZifMdUpdateinfoSectionGroup;

typedef enum {
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_PKGLIST_PACKAGE,
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_PKGLIST_FILENAME,
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_PKGLIST_UNKNOWN
} ZifMdUpdateinfoSectionUpdatePkglistType;

/**
 * ZifMdUpdateinfoPrivate:
 *
 * Private #ZifMdUpdateinfo data
 **/
struct _ZifMdUpdateinfoPrivate
{
	gboolean			 loaded;
	ZifConfig			*config;
	GPtrArray			*array_updates;		/* stored as ZifUpdate */
	/* for parser */
	ZifMdUpdateinfoSection		 section;
	ZifMdUpdateinfoSectionGroup	 section_group;
	ZifMdUpdateinfoSectionUpdatePkglistType section_group_type;
	ZifUpdate			*update_temp;
	ZifUpdateInfo			*update_info_temp;
	ZifPackage			*package_temp;
};

G_DEFINE_TYPE (ZifMdUpdateinfo, zif_md_updateinfo, ZIF_TYPE_MD)

/**
 * zif_md_updateinfo_fix_iso8601:
 *
 * '2010-12-07 16:26' -> '2010-12-07T16:26Z'
 **/
static gchar *
zif_md_updateinfo_fix_iso8601 (const gchar *iso8601)
{
	gchar **split;
	gchar *fixed = NULL;

	/* split */
	split = g_strsplit (iso8601, " ", -1);
	if (g_strv_length (split) != 2) {
		g_warning ("failed to parse %s", iso8601);
		goto out;
	}

	/* repair */
	fixed = g_strdup_printf ("%sT%sZ", split[0], split[1]);
out:
	g_strfreev (split);
	return fixed;
}

/**
 * zif_md_updateinfo_parser_start_element:
 **/
static void
zif_md_updateinfo_parser_start_element (GMarkupParseContext *context, const gchar *element_name,
					const gchar **attribute_names, const gchar **attribute_values,
					gpointer user_data, GError **error)
{
	guint i;
	GError *error_local = NULL;
	gchar *iso8601;
	gchar *package_id = NULL;
	ZifUpdateKind update_kind;
	ZifMdUpdateinfo *updateinfo = user_data;

	g_return_if_fail (ZIF_IS_MD_UPDATEINFO (updateinfo));

	/* group element */
	if (updateinfo->priv->section == ZIF_MD_UPDATEINFO_SECTION_UNKNOWN) {

		/* start of list */
		if (g_strcmp0 (element_name, "updates") == 0)
			goto out;

		/* start of update */
		if (g_strcmp0 (element_name, "update") == 0) {
			updateinfo->priv->section = ZIF_MD_UPDATEINFO_SECTION_UPDATE;

			/* already exists -- how? */
			if (updateinfo->priv->update_temp != NULL) {
				g_warning ("failed to add %s",
					     zif_update_get_id (updateinfo->priv->update_temp));
				g_object_unref (updateinfo->priv->update_temp);
			}
			updateinfo->priv->update_temp = zif_update_new ();

			/* find the update status and type as a bonus */
			for (i=0; attribute_names[i] != NULL; i++) {
				if (g_strcmp0 (attribute_names[i], "status") == 0) {
					zif_update_set_state (updateinfo->priv->update_temp,
							      zif_update_state_from_string (attribute_values[i]));
				} else if (g_strcmp0 (attribute_names[i], "type") == 0) {
					update_kind = zif_update_kind_from_string (attribute_values[i]);
					if (update_kind == ZIF_UPDATE_KIND_UNKNOWN)
						g_warning ("failed to match update kind from: %s",
							   attribute_values[i]);
					zif_update_set_kind (updateinfo->priv->update_temp,
							     update_kind);
				} else if (g_strcmp0 (attribute_names[i], "from") == 0) {
					zif_update_set_source (updateinfo->priv->update_temp,
							       attribute_values[i]);
				}
			}
			goto out;
		}

		g_warning ("unhandled element: %s", element_name);

		goto out;
	}

	/* update element */
	if (updateinfo->priv->section == ZIF_MD_UPDATEINFO_SECTION_UPDATE) {

		if (updateinfo->priv->section_group == ZIF_MD_UPDATEINFO_SECTION_UPDATE_UNKNOWN) {
			if (g_strcmp0 (element_name, "release") == 0)
				goto out;
			if (g_strcmp0 (element_name, "id") == 0) {
				updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_ID;
				goto out;
			}
			if (g_strcmp0 (element_name, "title") == 0) {
				updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_TITLE;
				goto out;
			}
			if (g_strcmp0 (element_name, "description") == 0) {
				updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_DESCRIPTION;
				goto out;
			}
			if (g_strcmp0 (element_name, "reboot_suggested") == 0) {
				updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_REBOOT;
				goto out;
			}
			if (g_strcmp0 (element_name, "issued") == 0) {
				updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_ISSUED;

				/* find the issued date */
				for (i=0; attribute_names[i] != NULL; i++) {
					if (g_strcmp0 (attribute_names[i], "date") == 0) {
						iso8601 = zif_md_updateinfo_fix_iso8601 (attribute_values[i]);
						zif_update_set_issued (updateinfo->priv->update_temp, iso8601);
						g_free (iso8601);
					}
				}
				goto out;
			}
			if (g_strcmp0 (element_name, "references") == 0) {
				updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_REFERENCES;
				goto out;
			}
			if (g_strcmp0 (element_name, "pkglist") == 0) {
				updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_PKGLIST;
				goto out;
			}
			g_warning ("unhandled update base tag: %s", element_name);
			goto out;

		} else if (updateinfo->priv->section_group == ZIF_MD_UPDATEINFO_SECTION_UPDATE_REFERENCES) {
			if (g_strcmp0 (element_name, "reference") == 0) {

				/* already exists -- how? */
				if (updateinfo->priv->update_info_temp != NULL) {
					g_warning ("failed to add %s",
						     zif_update_info_get_title (updateinfo->priv->update_info_temp));
					g_object_unref (updateinfo->priv->update_info_temp);
				}
				updateinfo->priv->update_info_temp = zif_update_info_new ();

				/* find the details about the info */
				for (i=0; attribute_names[i] != NULL; i++) {
					if (g_strcmp0 (attribute_names[i], "href") == 0) {
						zif_update_info_set_url (updateinfo->priv->update_info_temp,
									 attribute_values[i]);
					}
					if (g_strcmp0 (attribute_names[i], "title") == 0) {
						zif_update_info_set_title (updateinfo->priv->update_info_temp,
									   attribute_values[i]);
					}
					if (g_strcmp0 (attribute_names[i], "type") == 0) {
						zif_update_info_set_kind (updateinfo->priv->update_info_temp,
									  zif_update_info_kind_from_string (attribute_values[i]));
					}
				}

				goto out;
			}

			g_warning ("unhandled references tag: %s", element_name);
			goto out;

		} else if (updateinfo->priv->section_group == ZIF_MD_UPDATEINFO_SECTION_UPDATE_PKGLIST) {
			if (g_strcmp0 (element_name, "collection") == 0)
				goto out;
			if (g_strcmp0 (element_name, "name") == 0)
				goto out;
			if (g_strcmp0 (element_name, "reboot_suggested") == 0)
				goto out;
			//TODO: is this better than src?
			if (g_strcmp0 (element_name, "filename") == 0)
				goto out;

			if (g_strcmp0 (element_name, "package") == 0) {
				const gchar *name = NULL;
				guint epoch = 0;
				const gchar *version = NULL;
				const gchar *release = NULL;
				const gchar *arch = NULL;
				const gchar *src = NULL;
				const gchar *data;
				ZifString *string;
				gboolean ret;

				/* already exists -- how? */
				if (updateinfo->priv->package_temp != NULL) {
					g_warning ("failed to add %s",
						     zif_package_get_id (updateinfo->priv->package_temp));
					g_object_unref (updateinfo->priv->package_temp);
				}
				updateinfo->priv->package_temp = zif_package_new ();

				/* find the details about the package */
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
					else if (g_strcmp0 (attribute_names[i], "src") == 0)
						src = attribute_values[i];
				}

				/* create a package from what we know */
				data = zif_md_get_id (ZIF_MD (updateinfo));
				package_id = zif_package_id_from_nevra (name, epoch, version, release, arch, data);
				ret = zif_package_set_id (updateinfo->priv->package_temp, package_id, &error_local);
				if (!ret) {
					g_warning ("failed to set %s: %s", package_id, error_local->message);
					g_error_free (error_local);
					goto out;
				}
				string = zif_string_new (src);
				zif_package_set_location_href (updateinfo->priv->package_temp, string);
				zif_string_unref (string);
				goto out;
			}

			g_warning ("unexpected pklist tag: %s", element_name);
		}

		g_warning ("unexpected update tag: %s", element_name);
	}

	g_warning ("unhandled base tag: %s", element_name);

out:
	g_free (package_id);
	return;
}

/**
 * zif_md_updateinfo_add_vendor_info:
 **/
static void
zif_md_updateinfo_add_vendor_info (ZifMdUpdateinfo *md, ZifUpdate *update)
{
	const gchar *source;
	gchar *url = NULL;
	guint releasever;
	ZifUpdateInfo *update_info = NULL;

	/* only link Fedora updates to Bohdi */
	source = zif_update_get_source (update);
	if (g_strcmp0 (source, "updates@fedoraproject.org") != 0) {
		g_debug ("no vendor info for update source %s", source);
		goto out;
	}

	/* get the release version */
	releasever = zif_config_get_uint (md->priv->config,
					  "releasever", NULL);

	/* construct a URL, ideally this would be in the metadata... */
	url = g_strdup_printf ("https://admin.fedoraproject.org/updates/F%i/%s",
			       releasever, zif_update_get_id (update));

	/* add info to update */
	update_info = zif_update_info_new ();
	zif_update_info_set_kind (update_info, ZIF_UPDATE_INFO_KIND_VENDOR);
	zif_update_info_set_title (update_info, zif_update_get_id (update));
	zif_update_info_set_url (update_info, url);
	zif_update_add_update_info (update, update_info);
out:
	if (update_info != NULL)
		g_object_unref (update_info);
	g_free (url);
}

/**
 * zif_md_updateinfo_parser_end_element:
 **/
static void
zif_md_updateinfo_parser_end_element (GMarkupParseContext *context, const gchar *element_name,
				      gpointer user_data, GError **error)
{
	ZifMdUpdateinfo *updateinfo = user_data;

	/* no element */
	if (updateinfo->priv->section == ZIF_MD_UPDATEINFO_SECTION_UNKNOWN) {

		/* end of list */
		if (g_strcmp0 (element_name, "updates") == 0)
			goto out;

		g_warning ("unhandled base end tag: %s", element_name);
	}

	/* update element */
	if (updateinfo->priv->section == ZIF_MD_UPDATEINFO_SECTION_UPDATE) {

		/* end of update */
		if (g_strcmp0 (element_name, "update") == 0) {
			updateinfo->priv->section = ZIF_MD_UPDATEINFO_SECTION_UNKNOWN;

			/* always add an implicit vendor URL */
			zif_md_updateinfo_add_vendor_info (updateinfo,
							   updateinfo->priv->update_temp);

			/* add to array */
			g_ptr_array_add (updateinfo->priv->array_updates, updateinfo->priv->update_temp);
			updateinfo->priv->update_temp = NULL;
			goto out;
		}

		if (g_strcmp0 (element_name, "id") == 0 ||
		    g_strcmp0 (element_name, "title") == 0 ||
		    g_strcmp0 (element_name, "release") == 0 ||
		    g_strcmp0 (element_name, "description") == 0 ||
		    g_strcmp0 (element_name, "issued") == 0) {
			updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_UNKNOWN;
			goto out;
		}

		if (updateinfo->priv->section_group == ZIF_MD_UPDATEINFO_SECTION_UPDATE_REBOOT) {

			/* add property */
			if (g_strcmp0 (element_name, "reboot_suggested") == 0) {
				zif_update_set_reboot (updateinfo->priv->update_temp, TRUE);
				updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_UNKNOWN;
				goto out;
			}
			g_warning ("unhandled reboot_suggested end tag: %s", element_name);
			goto out;
		}

		if (updateinfo->priv->section_group == ZIF_MD_UPDATEINFO_SECTION_UPDATE_REFERENCES) {

			if (g_strcmp0 (element_name, "references") == 0) {
				updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_UNKNOWN;
				goto out;
			}

			if (g_strcmp0 (element_name, "reference") == 0) {
				zif_update_add_update_info (updateinfo->priv->update_temp,
							    updateinfo->priv->update_info_temp);
				g_object_unref (updateinfo->priv->update_info_temp);
				updateinfo->priv->update_info_temp = NULL;
				goto out;
			}
			g_warning ("unhandled references end tag: %s", element_name);
			goto out;
		}

		if (updateinfo->priv->section_group == ZIF_MD_UPDATEINFO_SECTION_UPDATE_PKGLIST) {

			if (g_strcmp0 (element_name, "pkglist") == 0) {
				updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_UNKNOWN;
				goto out;
			}

			if (g_strcmp0 (element_name, "name") == 0)
				goto out;
			if (g_strcmp0 (element_name, "filename") == 0)
				goto out;
			if (g_strcmp0 (element_name, "collection") == 0)
				goto out;
			if (g_strcmp0 (element_name, "reboot_suggested") == 0)
				goto out;

			/* add to the update */
			if (g_strcmp0 (element_name, "package") == 0) {
				zif_update_add_package (updateinfo->priv->update_temp,
							updateinfo->priv->package_temp);
				g_object_unref (updateinfo->priv->package_temp);
				updateinfo->priv->package_temp = NULL;
				goto out;
			}

			g_warning ("unhandled pkglist end tag: %s", element_name);
		}

		g_warning ("unhandled update end tag: %s", element_name);
		goto out;
	}

	g_warning ("unhandled end tag: %s", element_name);
out:
	return;
}

/**
 * zif_md_updateinfo_parser_text:
 **/
static void
zif_md_updateinfo_parser_text (GMarkupParseContext *context, const gchar *text, gsize text_len,
			       gpointer user_data, GError **error)

{
	ZifMdUpdateinfo *updateinfo = user_data;

	/* skip whitespace */
	if (text_len < 1 || text[0] == ' ' || text[0] == '\t' || text[0] == '\n')
		goto out;

	/* group section */
	if (updateinfo->priv->section == ZIF_MD_UPDATEINFO_SECTION_UPDATE) {
		if (updateinfo->priv->section_group == ZIF_MD_UPDATEINFO_SECTION_UPDATE_ID) {
			zif_update_set_id (updateinfo->priv->update_temp, text);
			goto out;
		}
		if (updateinfo->priv->section_group == ZIF_MD_UPDATEINFO_SECTION_UPDATE_TITLE) {
			zif_update_set_title (updateinfo->priv->update_temp, text);
			goto out;
		}
		if (updateinfo->priv->section_group == ZIF_MD_UPDATEINFO_SECTION_UPDATE_DESCRIPTION) {
			zif_update_set_description (updateinfo->priv->update_temp, text);
			goto out;
		}
		goto out;
	}
out:
	return;
}

/**
 * zif_md_updateinfo_unload:
 **/
static gboolean
zif_md_updateinfo_unload (ZifMd *md, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	return ret;
}

/**
 * zif_md_updateinfo_load:
 **/
static gboolean
zif_md_updateinfo_load (ZifMd *md, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	gchar *contents = NULL;
	const gchar *filename;
	gsize size;
	GMarkupParseContext *context = NULL;
	const GMarkupParser gpk_md_updateinfo_markup_parser = {
		zif_md_updateinfo_parser_start_element,
		zif_md_updateinfo_parser_end_element,
		zif_md_updateinfo_parser_text,
		NULL, /* passthrough */
		NULL /* error */
	};
	ZifMdUpdateinfo *updateinfo = ZIF_MD_UPDATEINFO (md);

	g_return_val_if_fail (ZIF_IS_MD_UPDATEINFO (md), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* already loaded */
	if (updateinfo->priv->loaded) {
		ret = TRUE;
		goto out;
	}

	/* get filename */
	filename = zif_md_get_filename_uncompressed (md);
	if (filename == NULL) {
		g_set_error_literal (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
				     "failed to get filename for updateinfo");
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
	context = g_markup_parse_context_new (&gpk_md_updateinfo_markup_parser, G_MARKUP_PREFIX_ERROR_POSITION, updateinfo, NULL);

	/* parse data */
	zif_state_set_allow_cancel (state, FALSE);
	ret = g_markup_parse_context_parse (context, contents, (gssize) size, error);
	if (!ret)
		goto out;

	updateinfo->priv->loaded = TRUE;
out:
	if (context != NULL)
		g_markup_parse_context_free (context);
	g_free (contents);
	return ret;
}

/**
 * zif_md_updateinfo_get_detail:
 * @md: A #ZifMdUpdateinfo
 * @state: A %ZifState
 * @error: A #GError, or %NULL
 *
 * Gets all the available update data.
 *
 * Return value: (transfer full): #GPtrArray of #ZifUpdate's, free with g_ptr_array_unref()
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_md_updateinfo_get_detail (ZifMdUpdateinfo *md,
			      ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	gboolean ret;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_MD_UPDATEINFO (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* if not already loaded, load */
	if (!md->priv->loaded) {
		ret = zif_md_load (ZIF_MD (md), state, &error_local);
		if (!ret) {
			g_propagate_prefixed_error (error,
						    error_local,
						    "failed to get load updateinfo: ");
			goto out;
		}
	}

	array = g_ptr_array_ref (md->priv->array_updates);
out:
	return array;
}

/**
 * zif_md_updateinfo_get_detail_for_package:
 * @md: A #ZifMdUpdateinfo
 * @package_id: The package ID to use
 * @state: A %ZifState
 * @error: A #GError, or %NULL
 *
 * Gets the list of update details for the package_id.
 *
 * Return value: (transfer full): #GPtrArray of #ZifUpdate's, free with g_ptr_array_unref()
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_md_updateinfo_get_detail_for_package (ZifMdUpdateinfo *md, const gchar *package_id,
					  ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	GPtrArray *array_tmp;
	guint i;
	guint j;
	guint len;
	gboolean ret;
	GError *error_local = NULL;
	ZifUpdate *update;
	ZifPackage *package;

	g_return_val_if_fail (ZIF_IS_MD_UPDATEINFO (md), NULL);
	g_return_val_if_fail (package_id != NULL, NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* if not already loaded, load */
	if (!md->priv->loaded) {
		ret = zif_md_load (ZIF_MD (md), state, &error_local);
		if (!ret) {
			g_propagate_prefixed_error (error,
						    error_local,
						    "failed to get load updateinfo: ");
			goto out;
		}
	}

	/* get packages in this group */
	len = md->priv->array_updates->len;
	for (i=0; i<len; i++) {
		update = g_ptr_array_index (md->priv->array_updates, i);

		/* have we matched on any entries */
		ret = FALSE;

		/* get the list of packages updated by this update */
		array_tmp = zif_update_get_packages (update);
		for (j=0; j<array_tmp->len; j++) {
			package = g_ptr_array_index (array_tmp, j);
			if (g_strcmp0 (zif_package_get_id (package), package_id) == 0) {
				ret = TRUE;
				break;
			}
		}

		/* we found a package match */
		if (ret) {
			if (array == NULL)
				array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
			g_ptr_array_add (array, g_object_ref (update));
		}
		g_ptr_array_unref (array_tmp);
	}

	/* nothing found */
	if (array == NULL) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
			     "could not find package (%i in sack): %s", len, package_id);
	}
out:
	return array;
}

/**
 * zif_md_updateinfo_finalize:
 **/
static void
zif_md_updateinfo_finalize (GObject *object)
{
	ZifMdUpdateinfo *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_MD_UPDATEINFO (object));
	md = ZIF_MD_UPDATEINFO (object);

	g_object_unref (md->priv->config);
	g_ptr_array_unref (md->priv->array_updates);

	G_OBJECT_CLASS (zif_md_updateinfo_parent_class)->finalize (object);
}

/**
 * zif_md_updateinfo_class_init:
 **/
static void
zif_md_updateinfo_class_init (ZifMdUpdateinfoClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifMdClass *md_class = ZIF_MD_CLASS (klass);
	object_class->finalize = zif_md_updateinfo_finalize;

	/* map */
	md_class->load = zif_md_updateinfo_load;
	md_class->unload = zif_md_updateinfo_unload;
	g_type_class_add_private (klass, sizeof (ZifMdUpdateinfoPrivate));
}

/**
 * zif_md_updateinfo_init:
 **/
static void
zif_md_updateinfo_init (ZifMdUpdateinfo *md)
{
	md->priv = ZIF_MD_UPDATEINFO_GET_PRIVATE (md);
	md->priv->config = zif_config_new ();
	md->priv->loaded = FALSE;
	md->priv->section = ZIF_MD_UPDATEINFO_SECTION_UNKNOWN;
	md->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_UNKNOWN;
	md->priv->section_group_type = ZIF_MD_UPDATEINFO_SECTION_UPDATE_PKGLIST_UNKNOWN;
	md->priv->update_temp = NULL;
	md->priv->update_info_temp = NULL;
	md->priv->package_temp = NULL;
	md->priv->array_updates = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

/**
 * zif_md_updateinfo_new:
 *
 * Return value: A new #ZifMdUpdateinfo instance.
 *
 * Since: 0.1.0
 **/
ZifMd *
zif_md_updateinfo_new (void)
{
	ZifMdUpdateinfo *md;
	md = g_object_new (ZIF_TYPE_MD_UPDATEINFO,
			   "kind", ZIF_MD_KIND_UPDATEINFO,
			   NULL);
	return ZIF_MD (md);
}

