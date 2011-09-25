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
 * SECTION:zif-md-primary-xml
 * @short_description: PrimaryXml metadata
 *
 * Provide access to the primary_xml repo metadata.
 * This object is a subclass of #ZifMd
 */

typedef enum {
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE,
	ZIF_MD_PRIMARY_XML_SECTION_UNKNOWN
} ZifMdPrimaryXmlSection;

typedef enum {
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_NAME,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_ARCH,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_VERSION,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_CHECKSUM,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_SUMMARY,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_DESCRIPTION,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_URL,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_SIZE,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_LICENCE,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_LOCATION,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_GROUP,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_PROVIDES,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_REQUIRES,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_OBSOLETES,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_CONFLICTS,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_SOURCERPM,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_UNKNOWN
} ZifMdPrimaryXmlSectionPackage;

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <gio/gio.h>

#include "zif-config.h"
#include "zif-md.h"
#include "zif-utils.h"
#include "zif-depend.h"
#include "zif-md-primary-xml.h"
#include "zif-package-remote.h"
#include "zif-object-array.h"

#define ZIF_MD_PRIMARY_XML_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_MD_PRIMARY_XML, ZifMdPrimaryXmlPrivate))

/**
 * ZifMdPrimaryXmlPrivate:
 *
 * Private #ZifMdPrimaryXml data
 **/
struct _ZifMdPrimaryXmlPrivate
{
	gboolean			 loaded;
	ZifMdPrimaryXmlSection		 section;
	ZifMdPrimaryXmlSectionPackage	 section_package;
	ZifPackage			*package_temp;
	GPtrArray			*package_provides_temp;
	GPtrArray			*package_requires_temp;
	GPtrArray			*package_obsoletes_temp;
	GPtrArray			*package_conflicts_temp;
	GPtrArray			*array;
	gchar				*package_name_temp;
	gchar				*package_arch_temp;
	gchar				*package_version_temp;
	gchar				*package_release_temp;
	guint				 package_epoch_temp;
	ZifConfig			*config;
	ZifPackageCompareMode		 compare_mode;
};

G_DEFINE_TYPE (ZifMdPrimaryXml, zif_md_primary_xml, ZIF_TYPE_MD)

/**
 * zif_md_primary_xml_unload:
 **/
static gboolean
zif_md_primary_xml_unload (ZifMd *md, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	return ret;
}

/**
 * zif_md_primary_xml_parser_start_element:
 **/
static void
zif_md_primary_xml_parser_start_element (GMarkupParseContext *context, const gchar *element_name,
					const gchar **attribute_names, const gchar **attribute_values,
					gpointer user_data, GError **error)
{
	guint i;
	ZifDepend *depend = NULL;
	ZifString *tmp;
	ZifMdPrimaryXml *primary_xml = user_data;

	g_return_if_fail (ZIF_IS_MD_PRIMARY_XML (primary_xml));

	/* group element */
	if (primary_xml->priv->section == ZIF_MD_PRIMARY_XML_SECTION_UNKNOWN) {

		/* start of list */
		if (g_strcmp0 (element_name, "metadata") == 0)
			goto out;

		/* start of update */
		if (g_strcmp0 (element_name, "package") == 0) {
			primary_xml->priv->section = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE;
			primary_xml->priv->package_temp = ZIF_PACKAGE (zif_package_remote_new ());
			primary_xml->priv->package_provides_temp = zif_object_array_new ();
			primary_xml->priv->package_requires_temp = zif_object_array_new ();
			primary_xml->priv->package_obsoletes_temp = zif_object_array_new ();
			primary_xml->priv->package_conflicts_temp = zif_object_array_new ();
			zif_package_set_compare_mode (primary_xml->priv->package_temp,
						      primary_xml->priv->compare_mode);
			goto out;
		}

		g_warning ("unhandled element: %s", element_name);

		goto out;
	}

	/* update element */
	if (primary_xml->priv->section == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE) {

		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_UNKNOWN) {
			if (g_strcmp0 (element_name, "packager") == 0 ||
			    g_strcmp0 (element_name, "format") == 0 ||
			    g_strcmp0 (element_name, "file") == 0 ||
			    g_strcmp0 (element_name, "rpm:vendor") == 0 ||
			    g_strcmp0 (element_name, "rpm:buildhost") == 0 ||
			    g_strcmp0 (element_name, "rpm:header-range") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_UNKNOWN;
				goto out;
			}
			if (g_strcmp0 (element_name, "rpm:sourcerpm") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_SOURCERPM;
				goto out;
			}
			if (g_strcmp0 (element_name, "name") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_NAME;
				goto out;
			}
			if (g_strcmp0 (element_name, "checksum") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_CHECKSUM;
				goto out;
			}
			if (g_strcmp0 (element_name, "arch") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_ARCH;
				goto out;
			}
			if (g_strcmp0 (element_name, "summary") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_SUMMARY;
				goto out;
			}
			if (g_strcmp0 (element_name, "description") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_DESCRIPTION;
				goto out;
			}
			if (g_strcmp0 (element_name, "url") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_URL;
				goto out;
			}
			if (g_strcmp0 (element_name, "version") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_VERSION;
				for (i=0; attribute_names[i] != NULL; i++) {
					if (g_strcmp0 (attribute_names[i], "rel") == 0) {
						primary_xml->priv->package_release_temp = g_strdup (attribute_values[i]);
					} else if (g_strcmp0 (attribute_names[i], "epoch") == 0) {
						primary_xml->priv->package_epoch_temp = atoi (attribute_values[i]);
					} else if (g_strcmp0 (attribute_names[i], "ver") == 0) {
						primary_xml->priv->package_version_temp = g_strdup (attribute_values[i]);
					}
				}
				goto out;
			}
			if (g_strcmp0 (element_name, "size") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_SIZE;
				for (i=0; attribute_names[i] != NULL; i++) {
					if (g_strcmp0 (attribute_names[i], "package") == 0) {
						zif_package_set_size (primary_xml->priv->package_temp, atoi (attribute_values[i]));
					}
				}
				goto out;
			}
			if (g_strcmp0 (element_name, "time") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_VERSION;
				for (i=0; attribute_names[i] != NULL; i++) {
					if (g_strcmp0 (attribute_names[i], "file") == 0) {
						zif_package_set_time_file (primary_xml->priv->package_temp, atoi (attribute_values[i]));
					}
				}
				goto out;
			}
			if (g_strcmp0 (element_name, "location") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_LOCATION;
				for (i=0; attribute_names[i] != NULL; i++) {
					if (g_strcmp0 (attribute_names[i], "href") == 0) {
						tmp = zif_string_new (attribute_values[i]);
						zif_package_set_location_href (primary_xml->priv->package_temp,
									       tmp);
						zif_string_unref (tmp);
					}
				}
				goto out;
			}
			if (g_strcmp0 (element_name, "rpm:license") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_LICENCE;
				goto out;
			}
			if (g_strcmp0 (element_name, "rpm:group") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_GROUP;
				goto out;
			}
			if (g_strcmp0 (element_name, "rpm:provides") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_PROVIDES;
				goto out;
			}
			if (g_strcmp0 (element_name, "rpm:requires") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_REQUIRES;
				goto out;
			}
			if (g_strcmp0 (element_name, "rpm:obsoletes") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_OBSOLETES;
				goto out;
			}
			if (g_strcmp0 (element_name, "rpm:conflicts") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_CONFLICTS;
				goto out;
			}
			g_warning ("unhandled update base tag: %s", element_name);
			goto out;

		} else if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_REQUIRES) {
			if (g_strcmp0 (element_name, "rpm:entry") == 0) {
				depend = zif_depend_new_from_data (attribute_names,
								   attribute_values);
				/* some repos are broken */
				if (!g_str_has_prefix (zif_depend_get_name (depend), "rpmlib(")) {
					g_ptr_array_add (primary_xml->priv->package_requires_temp,
							 g_object_ref (depend));
				}
				goto out;
			}
		} else if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_OBSOLETES) {
			if (g_strcmp0 (element_name, "rpm:entry") == 0) {
				depend = zif_depend_new_from_data (attribute_names,
								   attribute_values);
				g_ptr_array_add (primary_xml->priv->package_obsoletes_temp,
						 g_object_ref (depend));
				goto out;
			}
		} else if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_CONFLICTS) {
			if (g_strcmp0 (element_name, "rpm:entry") == 0) {
				depend = zif_depend_new_from_data (attribute_names,
								   attribute_values);
				g_ptr_array_add (primary_xml->priv->package_conflicts_temp,
						 g_object_ref (depend));
				goto out;
			}
		} else if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_PROVIDES) {
			if (g_strcmp0 (element_name, "rpm:entry") == 0) {
				depend = zif_depend_new_from_data (attribute_names,
								   attribute_values);
				/* some repos are broken */
				if (!g_str_has_prefix (zif_depend_get_name (depend), "rpmlib(")) {
					g_ptr_array_add (primary_xml->priv->package_provides_temp,
							 g_object_ref (depend));
				}
				goto out;
			}
			goto out;
		}
		g_warning ("unhandled package tag: %s", element_name);
	}

	g_warning ("unhandled base tag: %s", element_name);
out:
	if (depend != NULL)
		g_object_unref (depend);
	return;
}

/**
 * zif_md_primary_xml_parser_end_element:
 **/
static void
zif_md_primary_xml_parser_end_element (GMarkupParseContext *context, const gchar *element_name,
				      gpointer user_data, GError **error)
{
	ZifMdPrimaryXml *primary_xml = user_data;
	gchar *package_id = NULL;
	GError *error_local = NULL;
	gboolean ret;
	ZifStore *store;

	/* no element */
	if (primary_xml->priv->section == ZIF_MD_PRIMARY_XML_SECTION_UNKNOWN) {
		/* end of list */
		if (g_strcmp0 (element_name, "metadata") == 0)
			goto out;
		g_warning ("unhandled base end tag: %s", element_name);
	}

	/* update element */
	if (primary_xml->priv->section == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE) {

		/* end of update */
		if (g_strcmp0 (element_name, "package") == 0) {
			primary_xml->priv->section = ZIF_MD_PRIMARY_XML_SECTION_UNKNOWN;

			/* add to array */
			package_id = zif_package_id_from_nevra (primary_xml->priv->package_name_temp,
								primary_xml->priv->package_epoch_temp,
								primary_xml->priv->package_version_temp,
								primary_xml->priv->package_release_temp,
								primary_xml->priv->package_arch_temp,
								zif_md_get_id (ZIF_MD (primary_xml)));
			ret = zif_package_set_id (primary_xml->priv->package_temp, package_id, &error_local);
			if (ret) {
				g_ptr_array_add (primary_xml->priv->array,
						 primary_xml->priv->package_temp);
				zif_package_set_provides (primary_xml->priv->package_temp,
							  primary_xml->priv->package_provides_temp);
				zif_package_set_requires (primary_xml->priv->package_temp,
							  primary_xml->priv->package_requires_temp);
				zif_package_set_obsoletes (primary_xml->priv->package_temp,
							   primary_xml->priv->package_obsoletes_temp);
				zif_package_set_conflicts (primary_xml->priv->package_temp,
							   primary_xml->priv->package_conflicts_temp);
			} else {
				g_warning ("failed to set %s: %s", package_id, error_local->message);
				g_object_unref (primary_xml->priv->package_temp);
				g_error_free (error_local);
				goto out;
			}

			/* set the store the package came from */
			store = zif_md_get_store (ZIF_MD (primary_xml));
			if (store != NULL) {
				zif_package_remote_set_store_remote (ZIF_PACKAGE_REMOTE (primary_xml->priv->package_temp),
								     ZIF_STORE_REMOTE (store));
			}

			primary_xml->priv->package_temp = NULL;
			g_free (primary_xml->priv->package_name_temp);
			g_free (primary_xml->priv->package_version_temp);
			g_free (primary_xml->priv->package_release_temp);
			g_free (primary_xml->priv->package_arch_temp);
			g_ptr_array_unref (primary_xml->priv->package_provides_temp);
			g_ptr_array_unref (primary_xml->priv->package_requires_temp);
			g_ptr_array_unref (primary_xml->priv->package_obsoletes_temp);
			g_ptr_array_unref (primary_xml->priv->package_conflicts_temp);
			goto out;
		}

		/* do not change section */
		if (g_strcmp0 (element_name, "rpm:entry") == 0) {
			goto out;
		}

		if (g_strcmp0 (element_name, "name") == 0 ||
		    g_strcmp0 (element_name, "summary") == 0 ||
		    g_strcmp0 (element_name, "arch") == 0 ||
		    g_strcmp0 (element_name, "version") == 0 ||
		    g_strcmp0 (element_name, "checksum") == 0 ||
		    g_strcmp0 (element_name, "file") == 0 ||
		    g_strcmp0 (element_name, "time") == 0 ||
		    g_strcmp0 (element_name, "size") == 0 ||
		    g_strcmp0 (element_name, "rpm:license") == 0 ||
		    g_strcmp0 (element_name, "rpm:vendor") == 0 ||
		    g_strcmp0 (element_name, "rpm:group") == 0 ||
		    g_strcmp0 (element_name, "rpm:buildhost") == 0 ||
		    g_strcmp0 (element_name, "rpm:provides") == 0 ||
		    g_strcmp0 (element_name, "rpm:requires") == 0 ||
		    g_strcmp0 (element_name, "rpm:obsoletes") == 0 ||
		    g_strcmp0 (element_name, "rpm:conflicts") == 0 ||
		    g_strcmp0 (element_name, "rpm:sourcerpm") == 0 ||
		    g_strcmp0 (element_name, "rpm:header-range") == 0 ||
		    g_strcmp0 (element_name, "location") == 0 ||
		    g_strcmp0 (element_name, "format") == 0 ||
		    g_strcmp0 (element_name, "packager") == 0 ||
		    g_strcmp0 (element_name, "description") == 0 ||
		    g_strcmp0 (element_name, "url") == 0) {
			primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_UNKNOWN;
			goto out;
		}

		g_warning ("unhandled update end tag: %s", element_name);
		goto out;
	}

	g_warning ("unhandled end tag: %s", element_name);
out:
	g_free (package_id);
	return;
}

/**
 * zif_md_primary_xml_parser_text:
 **/
static void
zif_md_primary_xml_parser_text (GMarkupParseContext *context, const gchar *text, gsize text_len,
			       gpointer user_data, GError **error)

{
	ZifMdPrimaryXml *primary_xml = user_data;
	ZifString *string = NULL;

	/* skip whitespace */
	if (text_len < 1 || text[0] == ' ' || text[0] == '\t' || text[0] == '\n')
		goto out;

	/* group section */
	if (primary_xml->priv->section == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE) {
		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_UNKNOWN)
			goto out;
		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_NAME) {
			primary_xml->priv->package_name_temp = g_strdup (text);
			goto out;
		}
		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_ARCH) {
			primary_xml->priv->package_arch_temp = g_strdup (text);
			goto out;
		}
		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_SUMMARY) {
			string = zif_string_new (text);
			zif_package_set_summary (primary_xml->priv->package_temp, string);
			goto out;
		}
		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_DESCRIPTION) {
			string = zif_string_new (text);
			zif_package_set_description (primary_xml->priv->package_temp, string);
			goto out;
		}
		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_URL) {
			string = zif_string_new (text);
			zif_package_set_url (primary_xml->priv->package_temp, string);
			goto out;
		}
		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_GROUP) {
			string = zif_string_new (text);
			zif_package_set_category (primary_xml->priv->package_temp, string);
			goto out;
		}
		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_SOURCERPM) {
			string = zif_string_new (text);
			zif_package_set_source_filename (primary_xml->priv->package_temp, string);
			goto out;
		}
		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_LICENCE) {
			string = zif_string_new (text);
			zif_package_set_license (primary_xml->priv->package_temp, string);
			goto out;
		}
		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_CHECKSUM) {
			string = zif_string_new (text);
			zif_package_set_pkgid (primary_xml->priv->package_temp, string);
			goto out;
		}
		g_warning ("not saving: %s", text);
		goto out;
	}
out:
	if (string != NULL)
		zif_string_unref (string);
	return;
}

/**
 * zif_md_primary_xml_load:
 **/
static gboolean
zif_md_primary_xml_load (ZifMd *md, ZifState *state, GError **error)
{
	const gchar *filename;
	gboolean ret;
	gchar *contents = NULL;
	gsize size;
	ZifMdPrimaryXml *primary_xml = ZIF_MD_PRIMARY_XML (md);
	GMarkupParseContext *context = NULL;
	const GMarkupParser gpk_md_primary_xml_markup_parser = {
		zif_md_primary_xml_parser_start_element,
		zif_md_primary_xml_parser_end_element,
		zif_md_primary_xml_parser_text,
		NULL, /* passthrough */
		NULL /* error */
	};

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_XML (md), FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* already loaded */
	if (primary_xml->priv->loaded)
		goto out;

	/* get the compare mode */
	primary_xml->priv->compare_mode = zif_config_get_enum (primary_xml->priv->config,
							       "pkg_compare_mode",
							       zif_package_compare_mode_from_string,
							       error);
	if (primary_xml->priv->compare_mode == G_MAXUINT)
		goto out;

	/* get filename */
	filename = zif_md_get_filename_uncompressed (md);
	if (filename == NULL) {
		g_set_error_literal (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
				     "failed to get filename for primary_xml");
		goto out;
	}

	/* open database */
	g_debug ("filename = %s", filename);
	zif_state_set_allow_cancel (state, FALSE);
	ret = g_file_get_contents (filename, &contents, &size, error);
	if (!ret)
		goto out;

	/* create parser */
	context = g_markup_parse_context_new (&gpk_md_primary_xml_markup_parser, G_MARKUP_PREFIX_ERROR_POSITION, primary_xml, NULL);

	/* parse data */
	zif_state_set_allow_cancel (state, FALSE);
	ret = g_markup_parse_context_parse (context, contents, (gssize) size, error);
	if (!ret)
		goto out;

	/* we don't need to keep syncing */
	primary_xml->priv->loaded = TRUE;
out:
	if (context != NULL)
		g_markup_parse_context_free (context);
	g_free (contents);
	return primary_xml->priv->loaded;
}

typedef gboolean (*ZifPackageFilterFunc)		(ZifPackage		*package,
							 gpointer		 user_data,
							 ZifStrCompareFunc	 compare_func);

/**
 * zif_md_primary_xml_filter:
 **/
static GPtrArray *
zif_md_primary_xml_filter (ZifMd *md,
			   ZifPackageFilterFunc filter_func,
			   gpointer user_data,
			   ZifStrCompareFunc compare_func,
			   ZifState *state,
			   GError **error)
{
	GPtrArray *array = NULL;
	GPtrArray *packages;
	ZifPackage *package;
	guint i;
	gboolean ret;
	GError *error_local = NULL;
	ZifState *state_local;
	ZifMdPrimaryXml *md_primary = ZIF_MD_PRIMARY_XML (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_XML (md), NULL);
	g_return_val_if_fail (zif_state_valid (state), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* setup state */
	if (md_primary->priv->loaded) {
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

	/* if not already loaded, load */
	if (!md_primary->priv->loaded) {
		state_local = zif_state_get_child (state);
		ret = zif_md_load (ZIF_MD (md), state_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to load md_primary_xml file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* search array */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	packages = md_primary->priv->array;
	for (i=0; i<packages->len; i++) {
		package = g_ptr_array_index (packages, i);
		if (filter_func (package, user_data, compare_func))
			g_ptr_array_add (array, g_object_ref (package));
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return array;
}

/**
 * zif_md_primary_xml_resolve_name_cb:
 **/
static gboolean
zif_md_primary_xml_resolve_name_cb (ZifPackage *package,
				    gpointer user_data,
				    ZifStrCompareFunc compare_func)
{
	guint i;
	const gchar *value;
	gchar **search = (gchar **) user_data;
	value = zif_package_get_name (package);
	for (i=0; search[i] != NULL; i++) {
		if (compare_func (value, search[i]))
			return TRUE;
	}
	return FALSE;
}

/**
 * zif_md_primary_xml_resolve_name_arch_kill_arch:
 **/
static gboolean
zif_md_primary_xml_resolve_name_arch_kill_arch (ZifPackage *package,
						gpointer user_data,
						ZifStrCompareFunc compare_func)
{
	guint i;
	const gchar *value;
	gchar *tmp;
	gchar **search = g_strdupv (user_data);

	/* we don't use this code path for non-noarch packages as it
	 * means copying the search GStrv each time */
	for (i=0; search[i] != NULL; i++) {
		tmp = strrchr (search[i], '.');
		if (tmp != NULL)
			*tmp = '\0';
	}
	value = zif_package_get_name (package);
	for (i=0; search[i] != NULL; i++) {
		if (compare_func (value, search[i]))
			return TRUE;
	}
	g_strfreev (search);
	return FALSE;
}

/**
 * zif_md_primary_xml_resolve_name_arch_cb:
 **/
static gboolean
zif_md_primary_xml_resolve_name_arch_cb (ZifPackage *package,
					 gpointer user_data,
					 ZifStrCompareFunc compare_func)
{
	guint i;
	const gchar *value;
	gchar **search = (gchar **) user_data;

	/* a noarch package has to be handled specially */
	value = zif_package_get_arch (package);
	if (g_strcmp0 (value, "noarch") == 0) {
		return zif_md_primary_xml_resolve_name_arch_kill_arch (package,
								       user_data,
								       compare_func);
	}

	value = zif_package_get_name_arch (package);
	for (i=0; search[i] != NULL; i++) {
		if (compare_func (value, search[i]))
			return TRUE;
	}
	return FALSE;
}

/**
 * zif_md_primary_xml_resolve_name_version_cb:
 **/
static gboolean
zif_md_primary_xml_resolve_name_version_cb (ZifPackage *package,
					    gpointer user_data,
					    ZifStrCompareFunc compare_func)
{
	guint i;
	const gchar *value;
	gchar **search = (gchar **) user_data;
	value = zif_package_get_name_version (package);
	for (i=0; search[i] != NULL; i++) {
		if (compare_func (value, search[i]))
			return TRUE;
	}
	return FALSE;
}

/**
 * zif_md_primary_xml_resolve_name_version_arch_cb:
 **/
static gboolean
zif_md_primary_xml_resolve_name_version_arch_cb (ZifPackage *package,
						 gpointer user_data,
						 ZifStrCompareFunc compare_func)
{
	guint i;
	const gchar *value;
	gchar **search = (gchar **) user_data;
	value = zif_package_get_name_version_arch (package);
	for (i=0; search[i] != NULL; i++) {
		if (compare_func (value, search[i]))
			return TRUE;
	}
	return FALSE;
}

/**
 * zif_md_primary_xml_resolve:
 **/
static GPtrArray *
zif_md_primary_xml_resolve (ZifMd *md,
			    gchar **search,
			    ZifStoreResolveFlags flags,
			    ZifState *state,
			    GError **error)
{
	gboolean ret;
	GPtrArray *array = NULL;
	GPtrArray *array_tmp;
	GPtrArray *tmp;
	guint cnt = 0;
	guint i;
	ZifState *state_local;
	ZifStrCompareFunc compare_func;

	g_return_val_if_fail (zif_state_valid (state), NULL);

	/* find out how many steps we need to do */
	cnt += ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME) > 0);
	cnt += ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME_ARCH) > 0);
	cnt += ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME_VERSION) > 0);
	cnt += ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME_VERSION_ARCH) > 0);
	zif_state_set_number_steps (state, cnt);

	/* allow globbing (slow) or a regular expressions (much slower) */
	if ((flags & ZIF_STORE_RESOLVE_FLAG_USE_REGEX) > 0)
		compare_func = zif_str_compare_regex;
	else if ((flags & ZIF_STORE_RESOLVE_FLAG_USE_GLOB) > 0)
		compare_func = zif_str_compare_glob;
	else
		compare_func = zif_str_compare_equal;

	array_tmp = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* name */
	if ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME) > 0) {
		state_local = zif_state_get_child (state);
		tmp = zif_md_primary_xml_filter (md,
						 zif_md_primary_xml_resolve_name_cb,
						 (gpointer) search,
						 compare_func,
						 state_local,
						 error);
		if (tmp == NULL)
			goto out;
		for (i=0; i<tmp->len; i++)
			g_ptr_array_add (array_tmp, g_object_ref (g_ptr_array_index (tmp, i)));
		g_ptr_array_unref (tmp);

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* name.arch */
	if ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME_ARCH) > 0) {
		state_local = zif_state_get_child (state);
		tmp = zif_md_primary_xml_filter (md,
						 zif_md_primary_xml_resolve_name_arch_cb,
						 (gpointer) search,
						 compare_func,
						 state_local,
						 error);
		if (tmp == NULL)
			goto out;
		for (i=0; i<tmp->len; i++)
			g_ptr_array_add (array_tmp, g_object_ref (g_ptr_array_index (tmp, i)));
		g_ptr_array_unref (tmp);

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* name-version */
	if ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME_VERSION) > 0) {
		state_local = zif_state_get_child (state);
		tmp = zif_md_primary_xml_filter (md,
						 zif_md_primary_xml_resolve_name_version_cb,
						 (gpointer) search,
						 compare_func,
						 state_local,
						 error);
		if (tmp == NULL)
			goto out;
		for (i=0; i<tmp->len; i++)
			g_ptr_array_add (array_tmp, g_object_ref (g_ptr_array_index (tmp, i)));
		g_ptr_array_unref (tmp);

		/* this section done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* name-version.arch */
	if ((flags & ZIF_STORE_RESOLVE_FLAG_USE_NAME_VERSION_ARCH) > 0) {
		state_local = zif_state_get_child (state);
		tmp = zif_md_primary_xml_filter (md,
						 zif_md_primary_xml_resolve_name_version_arch_cb,
						 (gpointer) search,
						 compare_func,
						 state_local,
						 error);
		if (tmp == NULL)
			goto out;
		for (i=0; i<tmp->len; i++)
			g_ptr_array_add (array_tmp, g_object_ref (g_ptr_array_index (tmp, i)));
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
 * zif_md_primary_xml_search_name_cb:
 **/
static gboolean
zif_md_primary_xml_search_name_cb (ZifPackage *package,
				   gpointer user_data,
				   ZifStrCompareFunc compare_func)
{
	guint i;
	const gchar *value;
	gchar **search = (gchar **) user_data;
	value = zif_package_get_name (package);
	for (i=0; search[i] != NULL; i++) {
		if (g_strstr_len (value, -1, search[i]) != NULL)
			return TRUE;
	}
	return FALSE;
}

/**
 * zif_md_primary_xml_search_name:
 **/
static GPtrArray *
zif_md_primary_xml_search_name (ZifMd *md, gchar **search, ZifState *state, GError **error)
{
	g_return_val_if_fail (zif_state_valid (state), NULL);
	return zif_md_primary_xml_filter (md,
					  zif_md_primary_xml_search_name_cb,
					  (gpointer) search,
					  NULL,
					  state, error);
}

/**
 * zif_md_primary_xml_search_details_cb:
 **/
static gboolean
zif_md_primary_xml_search_details_cb (ZifPackage *package,
				      gpointer user_data,
				      ZifStrCompareFunc compare_func)
{
	guint i;
	gboolean ret = FALSE;
	const gchar *name;
	const gchar *summary;
	const gchar *description;
	ZifState *state_tmp;
	GError *error = NULL;
	gchar **search = (gchar **) user_data;

	state_tmp = zif_state_new ();
	name = zif_package_get_name (package);
	summary = zif_package_get_summary (package, state_tmp, &error);
	if (summary == NULL) {
		g_warning ("failed to get summary: %s", error->message);
		g_error_free (error);
		goto out;
	}
	description = zif_package_get_description (package, state_tmp, &error);
	if (description == NULL) {
		g_warning ("failed to get description: %s", error->message);
		g_error_free (error);
		goto out;
	}
	for (i=0; search[i] != NULL; i++) {
		if (g_strstr_len (name, -1, search[i]) != NULL) {
			ret = TRUE;
			break;
		}
		if (g_strstr_len (summary, -1, search[i]) != NULL) {
			ret = TRUE;
			break;
		}
		if (g_strstr_len (description, -1, search[i]) != NULL) {
			ret = TRUE;
			break;
		}
	}
out:
	g_object_unref (state_tmp);
	return ret;
}

/**
 * zif_md_primary_xml_search_details:
 **/
static GPtrArray *
zif_md_primary_xml_search_details (ZifMd *md, gchar **search, ZifState *state, GError **error)
{
	g_return_val_if_fail (zif_state_valid (state), NULL);
	return zif_md_primary_xml_filter (md,
					  zif_md_primary_xml_search_details_cb,
					  (gpointer) search,
					  NULL,
					  state,
					  error);
}

/**
 * zif_md_primary_xml_search_group_cb:
 **/
static gboolean
zif_md_primary_xml_search_group_cb (ZifPackage *package,
				    gpointer user_data,
				    ZifStrCompareFunc compare_func)
{
	guint i;
	gboolean ret = FALSE;
	const gchar *value;
	ZifState *state_tmp;
	GError *error = NULL;
	gchar **search = (gchar **) user_data;

	state_tmp = zif_state_new ();
	value = zif_package_get_category (package, state_tmp, &error);
	if (value == NULL) {
		g_warning ("failed to get category: %s", error->message);
		g_error_free (error);
		goto out;
	}
	for (i=0; search[i] != NULL; i++) {
		if (g_strstr_len (value, -1, search[i]) != NULL) {
			ret = TRUE;
			goto out;
		}
	}
out:
	g_object_unref (state_tmp);
	return ret;
}

/**
 * zif_md_primary_xml_search_group:
 **/
static GPtrArray *
zif_md_primary_xml_search_group (ZifMd *md, gchar **search, ZifState *state, GError **error)
{
	g_return_val_if_fail (zif_state_valid (state), NULL);
	return zif_md_primary_xml_filter (md,
					  zif_md_primary_xml_search_group_cb,
					  (gpointer) search,
					  NULL,
					  state,
					  error);
}

/**
 * zif_md_primary_xml_search_pkgid_cb:
 **/
static gboolean
zif_md_primary_xml_search_pkgid_cb (ZifPackage *package,
				    gpointer user_data,
				    ZifStrCompareFunc compare_func)
{
	guint i;
	const gchar *pkgid;
	gchar **search = (gchar **) user_data;
	pkgid = zif_package_get_pkgid (package);
	for (i=0; search[i] != NULL; i++) {
		if (g_strcmp0 (pkgid, search[i]) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * zif_md_primary_xml_search_pkgid:
 **/
static GPtrArray *
zif_md_primary_xml_search_pkgid (ZifMd *md, gchar **search, ZifState *state, GError **error)
{
	g_return_val_if_fail (zif_state_valid (state), NULL);
	return zif_md_primary_xml_filter (md,
					  zif_md_primary_xml_search_pkgid_cb,
					  (gpointer) search,
					  NULL,
					  state,
					  error);
}

/**
 * zif_md_primary_xml_what_provides_cb:
 **/
static gboolean
zif_md_primary_xml_what_provides_cb (ZifPackage *package,
				     gpointer user_data,
				     ZifStrCompareFunc compare_func)
{
	guint i, j;
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	ZifState *state_tmp;
	ZifDepend *depend_tmp;
	ZifDepend *depend;
	GError *error = NULL;
	GPtrArray *depends = (GPtrArray *) user_data;

	state_tmp = zif_state_new ();
	array = zif_package_get_provides (package, state_tmp, &error);
	if (array == NULL) {
		g_warning ("failed to get provides: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* find a depends string */
	for (i=0; i<array->len; i++) {
		depend_tmp = g_ptr_array_index (array, i);
		for (j=0; j<depends->len; j++) {
			depend = g_ptr_array_index (depends, j);
			if (zif_depend_satisfies (depend_tmp, depend)) {
				ret = TRUE;
				goto out;
			}
		}
	}
out:
	g_object_unref (state_tmp);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_md_primary_xml_what_requires_cb:
 **/
static gboolean
zif_md_primary_xml_what_requires_cb (ZifPackage *package,
				     gpointer user_data,
				     ZifStrCompareFunc compare_func)
{
	guint i, j;
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	ZifState *state_tmp;
	ZifDepend *depend_tmp;
	ZifDepend *depend;
	GError *error = NULL;
	GPtrArray *depends = (GPtrArray *) user_data;

	state_tmp = zif_state_new ();
	array = zif_package_get_requires (package, state_tmp, &error);
	if (array == NULL) {
		g_warning ("failed to get requires: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* find a depends string */
	for (i=0; i<array->len; i++) {
		depend_tmp = g_ptr_array_index (array, i);
		for (j=0; j<depends->len; j++) {
			depend = g_ptr_array_index (depends, j);
			if (zif_depend_satisfies (depend_tmp, depend)) {
				ret = TRUE;
				goto out;
			}
		}
	}
out:
	g_object_unref (state_tmp);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_md_primary_xml_what_obsoletes_cb:
 **/
static gboolean
zif_md_primary_xml_what_obsoletes_cb (ZifPackage *package,
				      gpointer user_data,
				      ZifStrCompareFunc compare_func)
{
	guint i, j;
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	ZifState *state_tmp;
	ZifDepend *depend_tmp;
	ZifDepend *depend;
	GError *error = NULL;
	GPtrArray *depends = (GPtrArray *) user_data;

	state_tmp = zif_state_new ();
	array = zif_package_get_obsoletes (package, state_tmp, &error);
	if (array == NULL) {
		g_warning ("failed to get obsoletes: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* find a depends string */
	for (i=0; i<array->len; i++) {
		depend_tmp = g_ptr_array_index (array, i);
		for (j=0; j<depends->len; j++) {
			depend = g_ptr_array_index (depends, j);
			if (zif_depend_satisfies (depend_tmp, depend)) {
				ret = TRUE;
				goto out;
			}
		}
	}
out:
	g_object_unref (state_tmp);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_md_primary_xml_what_conflicts_cb:
 **/
static gboolean
zif_md_primary_xml_what_conflicts_cb (ZifPackage *package,
				      gpointer user_data,
				      ZifStrCompareFunc compare_func)
{
	guint i, j;
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	ZifState *state_tmp;
	ZifDepend *depend_tmp;
	ZifDepend *depend;
	GError *error = NULL;
	GPtrArray *depends = (GPtrArray *) user_data;

	state_tmp = zif_state_new ();
	array = zif_package_get_conflicts (package, state_tmp, &error);
	if (array == NULL) {
		g_warning ("failed to get conflicts: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* find a depends string */
	for (i=0; i<array->len; i++) {
		depend_tmp = g_ptr_array_index (array, i);
		for (j=0; j<depends->len; j++) {
			depend = g_ptr_array_index (depends, j);
			if (zif_depend_satisfies (depend_tmp, depend)) {
				ret = TRUE;
				goto out;
			}
		}
	}
out:
	g_object_unref (state_tmp);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_md_primary_xml_what_provides:
 **/
static GPtrArray *
zif_md_primary_xml_what_provides (ZifMd *md, GPtrArray *depends,
				  ZifState *state, GError **error)
{
	g_return_val_if_fail (zif_state_valid (state), NULL);
	return zif_md_primary_xml_filter (md,
					  zif_md_primary_xml_what_provides_cb,
					  (gpointer) depends,
					  NULL,
					  state,
					  error);
}

/**
 * zif_md_primary_xml_what_requires:
 **/
static GPtrArray *
zif_md_primary_xml_what_requires (ZifMd *md, GPtrArray *depends,
				  ZifState *state, GError **error)
{
	g_return_val_if_fail (zif_state_valid (state), NULL);
	return zif_md_primary_xml_filter (md,
					  zif_md_primary_xml_what_requires_cb,
					  (gpointer) depends,
					  NULL,
					  state,
					  error);
}

/**
 * zif_md_primary_xml_what_obsoletes:
 **/
static GPtrArray *
zif_md_primary_xml_what_obsoletes (ZifMd *md, GPtrArray *depends,
				   ZifState *state, GError **error)
{
	g_return_val_if_fail (zif_state_valid (state), NULL);
	return zif_md_primary_xml_filter (md,
					  zif_md_primary_xml_what_obsoletes_cb,
					  (gpointer) depends,
					  NULL,
					  state,
					  error);
}

/**
 * zif_md_primary_xml_what_conflicts:
 **/
static GPtrArray *
zif_md_primary_xml_what_conflicts (ZifMd *md, GPtrArray *depends,
				   ZifState *state, GError **error)
{
	g_return_val_if_fail (zif_state_valid (state), NULL);
	return zif_md_primary_xml_filter (md,
					  zif_md_primary_xml_what_conflicts_cb,
					  (gpointer) depends,
					  NULL,
					  state,
					  error);
}

/**
 * zif_md_primary_xml_find_package_cb:
 **/
static gboolean
zif_md_primary_xml_find_package_cb (ZifPackage *package,
				    gpointer user_data,
				    ZifStrCompareFunc compare_func)
{
	const gchar *value;
	const gchar *search = (const gchar *) user_data;
	value = zif_package_get_id (package);
	return (g_strcmp0 (value, search) == 0);
}

/**
 * zif_md_primary_xml_find_package:
 **/
static GPtrArray *
zif_md_primary_xml_find_package (ZifMd *md,
				 const gchar *package_id,
				 ZifState *state,
				 GError **error)
{
	g_return_val_if_fail (zif_state_valid (state), NULL);
	return zif_md_primary_xml_filter (md,
					  zif_md_primary_xml_find_package_cb,
					  (gpointer) package_id,
					  NULL,
					  state,
					  error);
}

/**
 * zif_md_primary_xml_get_packages:
 **/
static GPtrArray *
zif_md_primary_xml_get_packages (ZifMd *md, ZifState *state, GError **error)
{
	gboolean ret;
	GPtrArray *array = NULL;
	ZifMdPrimaryXml *primary_xml = ZIF_MD_PRIMARY_XML (md);

	/* if not already loaded, load */
	if (!primary_xml->priv->loaded) {
		ret = zif_md_load (ZIF_MD (md), state, error);
		if (!ret)
			goto out;
	}

	/* just return a ref to the ZifPackage array */
	array = g_ptr_array_ref (primary_xml->priv->array);
out:
	return array;
}

/**
 * zif_md_primary_xml_get_depends:
 **/
static GPtrArray *
zif_md_primary_xml_get_depends (ZifMd *md,
				const gchar *type,
				ZifPackage *package,
				ZifState *state,
				GError **error)
{
	guint i;
	ZifMdPrimaryXml *primary_xml = ZIF_MD_PRIMARY_XML (md);
	GPtrArray *array;
	GPtrArray *depends = NULL;
	ZifPackage *pkg_tmp;

	array = primary_xml->priv->array;
	for (i=0; i<array->len; i++) {
		pkg_tmp = g_ptr_array_index (array, i);
		if (zif_package_compare (pkg_tmp, package) == 0) {
			if (g_strcmp0 (type, "provides") == 0) {
				depends = zif_package_get_provides (pkg_tmp,
								    state,
								    error);
			} else if (g_strcmp0 (type, "requires") == 0) {
				depends = zif_package_get_requires (pkg_tmp,
								    state,
								    error);
			} else if (g_strcmp0 (type, "obsoletes") == 0) {
				depends = zif_package_get_obsoletes (pkg_tmp,
								     state,
								     error);
			} else if (g_strcmp0 (type, "conflicts") == 0) {
				depends = zif_package_get_conflicts (pkg_tmp,
								     state,
								     error);
			} else {
				g_assert_not_reached ();
			}
			goto out;
		}
	}
	if (depends == NULL) {
		g_set_error (error,
			     ZIF_MD_ERROR,
			     ZIF_MD_ERROR_FAILED,
			     "Failed to find package %s in %s",
			     zif_package_get_printable (package),
			     zif_md_get_id (md));
	}
out:
	return depends;
}

/**
 * zif_md_primary_xml_get_provides:
 **/
static GPtrArray *
zif_md_primary_xml_get_provides (ZifMd *md, ZifPackage *package,
				 ZifState *state, GError **error)
{
	return zif_md_primary_xml_get_depends (md, "provides", package, state, error);
}

/**
 * zif_md_primary_xml_get_requires:
 **/
static GPtrArray *
zif_md_primary_xml_get_requires (ZifMd *md, ZifPackage *package,
				 ZifState *state, GError **error)
{
	return zif_md_primary_xml_get_depends (md, "requires", package, state, error);
}

/**
 * zif_md_primary_xml_get_obsoletes:
 **/
static GPtrArray *
zif_md_primary_xml_get_obsoletes (ZifMd *md, ZifPackage *package,
				  ZifState *state, GError **error)
{
	return zif_md_primary_xml_get_depends (md, "obsoletes", package, state, error);
}

/**
 * zif_md_primary_xml_get_conflicts:
 **/
static GPtrArray *
zif_md_primary_xml_get_conflicts (ZifMd *md, ZifPackage *package,
				  ZifState *state, GError **error)
{
	return zif_md_primary_xml_get_depends (md, "conflicts", package, state, error);
}

/**
 * zif_md_primary_xml_finalize:
 **/
static void
zif_md_primary_xml_finalize (GObject *object)
{
	ZifMdPrimaryXml *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_MD_PRIMARY_XML (object));
	md = ZIF_MD_PRIMARY_XML (object);

	g_ptr_array_unref (md->priv->array);
	g_object_unref (md->priv->config);

	G_OBJECT_CLASS (zif_md_primary_xml_parent_class)->finalize (object);
}

/**
 * zif_md_primary_xml_class_init:
 **/
static void
zif_md_primary_xml_class_init (ZifMdPrimaryXmlClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifMdClass *md_class = ZIF_MD_CLASS (klass);
	object_class->finalize = zif_md_primary_xml_finalize;

	/* map */
	md_class->load = zif_md_primary_xml_load;
	md_class->unload = zif_md_primary_xml_unload;
	md_class->search_name = zif_md_primary_xml_search_name;
	md_class->search_details = zif_md_primary_xml_search_details;
	md_class->search_group = zif_md_primary_xml_search_group;
	md_class->search_pkgid = zif_md_primary_xml_search_pkgid;
	md_class->what_provides = zif_md_primary_xml_what_provides;
	md_class->what_requires = zif_md_primary_xml_what_requires;
	md_class->what_obsoletes = zif_md_primary_xml_what_obsoletes;
	md_class->what_conflicts = zif_md_primary_xml_what_conflicts;
	md_class->resolve = zif_md_primary_xml_resolve;
	md_class->get_packages = zif_md_primary_xml_get_packages;
	md_class->find_package = zif_md_primary_xml_find_package;
	md_class->get_provides = zif_md_primary_xml_get_provides;
	md_class->get_requires = zif_md_primary_xml_get_requires;
	md_class->get_obsoletes = zif_md_primary_xml_get_obsoletes;
	md_class->get_conflicts = zif_md_primary_xml_get_conflicts;

	g_type_class_add_private (klass, sizeof (ZifMdPrimaryXmlPrivate));
}

/**
 * zif_md_primary_xml_init:
 **/
static void
zif_md_primary_xml_init (ZifMdPrimaryXml *md)
{
	md->priv = ZIF_MD_PRIMARY_XML_GET_PRIVATE (md);
	md->priv->array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	md->priv->loaded = FALSE;
	md->priv->section = ZIF_MD_PRIMARY_XML_SECTION_UNKNOWN;
	md->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_UNKNOWN;
	md->priv->package_temp = NULL;
	md->priv->config = zif_config_new ();
}

/**
 * zif_md_primary_xml_new:
 *
 * Return value: A new #ZifMdPrimaryXml instance.
 *
 * Since: 0.1.0
 **/
ZifMd *
zif_md_primary_xml_new (void)
{
	ZifMdPrimaryXml *md;
	md = g_object_new (ZIF_TYPE_MD_PRIMARY_XML,
			   "kind", ZIF_MD_KIND_PRIMARY_XML,
			   NULL);
	return ZIF_MD (md);
}

