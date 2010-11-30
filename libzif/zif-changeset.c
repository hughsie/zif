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
 * SECTION:zif-changeset
 * @short_description: ChangeLog data entry
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>

#include "zif-changeset.h"

#define ZIF_CHANGESET_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_CHANGESET, ZifChangesetPrivate))

struct _ZifChangesetPrivate
{
	guint64			 date;
	gchar			*author;
	gchar			*description;
	gchar			*version;
};

enum {
	PROP_0,
	PROP_DATE,
	PROP_AUTHOR,
	PROP_DESCRIPTION,
	PROP_VERSION,
	PROP_LAST
};

G_DEFINE_TYPE (ZifChangeset, zif_changeset, G_TYPE_OBJECT)

/**
 * zif_changeset_get_date:
 * @changeset: A #ZifChangeset
 *
 * Gets the date and date of the update.
 *
 * Return value: The date of the update, or 0 for unset.
 *
 * Since: 0.1.0
 **/
guint64
zif_changeset_get_date (ZifChangeset *changeset)
{
	g_return_val_if_fail (ZIF_IS_CHANGESET (changeset), 0);
	return changeset->priv->date;
}

/**
 * zif_changeset_get_author:
 * @changeset: A #ZifChangeset
 *
 * Gets the author for this changeset.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_changeset_get_author (ZifChangeset *changeset)
{
	g_return_val_if_fail (ZIF_IS_CHANGESET (changeset), NULL);
	return changeset->priv->author;
}

/**
 * zif_changeset_get_description:
 * @changeset: A #ZifChangeset
 *
 * Gets the description for this changeset.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_changeset_get_description (ZifChangeset *changeset)
{
	g_return_val_if_fail (ZIF_IS_CHANGESET (changeset), NULL);
	return changeset->priv->description;
}

/**
 * zif_changeset_get_version:
 * @changeset: A #ZifChangeset
 *
 * Gets the date this changeset was version.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_changeset_get_version (ZifChangeset *changeset)
{
	g_return_val_if_fail (ZIF_IS_CHANGESET (changeset), NULL);
	return changeset->priv->version;
}

/**
 * zif_changeset_set_date:
 * @changeset: A #ZifChangeset
 * @date: The date of the changeset
 *
 * Sets the changeset date status.
 *
 * Since: 0.1.0
 **/
void
zif_changeset_set_date (ZifChangeset *changeset, guint64 date)
{
	g_return_if_fail (ZIF_IS_CHANGESET (changeset));
	changeset->priv->date = date;
}

/**
 * zif_changeset_strreplace:
 **/
static gboolean
zif_changeset_strreplace (GString *string, const gchar *find, const gchar *replace)
{
	gchar **array;
	gchar *value;

	/* common case, not found */
	if (g_strstr_len (string->str, -1, find) == NULL)
		return FALSE;

	/* split apart and rejoin with new delimiter */
	array = g_strsplit (string->str, find, 0);
	value = g_strjoinv (replace, array);
	g_strfreev (array);
	g_string_assign (string, value);
	g_free (value);
	return TRUE;
}

/**
 * zif_changeset_set_author:
 * @changeset: A #ZifChangeset
 * @author: The changeset author
 *
 * Sets the changeset author. Some anti-mangling expansions are
 * performed, e.g. '[AT]' is replaced with '@'.
 *
 * Since: 0.1.0
 **/
void
zif_changeset_set_author (ZifChangeset *changeset, const gchar *author)
{
	GString *temp;

	g_return_if_fail (ZIF_IS_CHANGESET (changeset));
	g_return_if_fail (author != NULL);
	g_return_if_fail (changeset->priv->author == NULL);

	/* try to unmangle */
	temp = g_string_new (author);
	zif_changeset_strreplace (temp, " at ", "@");
	zif_changeset_strreplace (temp, "[at]", "@");
	zif_changeset_strreplace (temp, " AT ", "@");
	zif_changeset_strreplace (temp, "[AT]", "@");
	zif_changeset_strreplace (temp, " dot ", ".");
	zif_changeset_strreplace (temp, "[dot]", ".");
	zif_changeset_strreplace (temp, " DOT ", ".");
	zif_changeset_strreplace (temp, "[DOT]", ".");
	zif_changeset_strreplace (temp, " gmail com", "@gmail.com");
	zif_changeset_strreplace (temp, " googlemail com", "@googlemail.com");
	zif_changeset_strreplace (temp, " redhat com", "@redhat.com");

	changeset->priv->author = g_string_free (temp, FALSE);
}

/**
 * zif_changeset_set_description:
 * @changeset: A #ZifChangeset
 * @description: The changeset description
 *
 * Sets the changeset description.
 *
 * Since: 0.1.0
 **/
void
zif_changeset_set_description (ZifChangeset *changeset, const gchar *description)
{
	g_return_if_fail (ZIF_IS_CHANGESET (changeset));
	g_return_if_fail (description != NULL);
	g_return_if_fail (changeset->priv->description == NULL);

	changeset->priv->description = g_strdup (description);
}

/**
 * zif_changeset_set_version:
 * @changeset: A #ZifChangeset
 * @version: The changeset version date
 *
 * Sets the date the changeset was version.
 *
 * Since: 0.1.0
 **/
void
zif_changeset_set_version (ZifChangeset *changeset, const gchar *version)
{
	g_return_if_fail (ZIF_IS_CHANGESET (changeset));
	g_return_if_fail (version != NULL);
	g_return_if_fail (changeset->priv->version == NULL);

	changeset->priv->version = g_strdup (version);
}

/**
 * zif_changeset_parse_header:
 * @changeset: A #ZifChangeset
 * @header: The package header, e.g "Ania Hughes &lt;ahughes&amp;redhat.com&gt; - 2.29.91-1.fc13"
 * @error: A #GError, or %NULL
 *
 * Sets the author and version from the package header.
 *
 * Return value: %TRUE if the data was parsed correctly
 *
 * Since: 0.1.0
 **/
gboolean
zif_changeset_parse_header (ZifChangeset *changeset, const gchar *header, GError **error)
{
	gboolean ret = FALSE;
	gchar *temp = NULL;
	gchar *found;
	guint len;

	g_return_val_if_fail (ZIF_IS_CHANGESET (changeset), FALSE);
	g_return_val_if_fail (header != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check if there is a version field */
	len = strlen (header);
	if (header[len-1] == '>') {
		zif_changeset_set_author (changeset, header);
		ret = TRUE;
		goto out;
	}

	/* operate on copy */
	temp = g_strdup (header);

	/* get last space */
	found = g_strrstr (temp, " ");
	if (found == NULL) {
		g_set_error (error, 1, 0, "format invalid: %s", header);
		goto out;
	}

	/* set version */
	zif_changeset_set_version (changeset, found + 1);

	/* trim to first non-space or '-' char */
	for (;found != temp;found--) {
		if (*found != ' ' && *found != '-')
			break;
	}

	/* terminate here */
	found[1] = '\0';

	/* set author */
	zif_changeset_set_author (changeset, temp);

	/* success */
	ret = TRUE;
out:
	g_free (temp);
	return ret;
}

/**
 * zif_changeset_get_property:
 **/
static void
zif_changeset_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	ZifChangeset *changeset = ZIF_CHANGESET (object);
	ZifChangesetPrivate *priv = changeset->priv;

	switch (prop_id) {
	case PROP_DATE:
		g_value_set_uint64 (value, priv->date);
		break;
	case PROP_AUTHOR:
		g_value_set_string (value, priv->author);
		break;
	case PROP_DESCRIPTION:
		g_value_set_string (value, priv->description);
		break;
	case PROP_VERSION:
		g_value_set_string (value, priv->version);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * zif_changeset_set_property:
 **/
static void
zif_changeset_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
}

/**
 * zif_changeset_finalize:
 **/
static void
zif_changeset_finalize (GObject *object)
{
	ZifChangeset *changeset;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_CHANGESET (object));
	changeset = ZIF_CHANGESET (object);

	g_free (changeset->priv->author);
	g_free (changeset->priv->description);
	g_free (changeset->priv->version);

	G_OBJECT_CLASS (zif_changeset_parent_class)->finalize (object);
}

/**
 * zif_changeset_class_init:
 **/
static void
zif_changeset_class_init (ZifChangesetClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_changeset_finalize;
	object_class->get_property = zif_changeset_get_property;
	object_class->set_property = zif_changeset_set_property;

	/**
	 * ZifChangeset:date:
	 *
	 * Since: 0.1.0
	 */
	pspec = g_param_spec_uint64 ("date", NULL, NULL,
				     0, G_MAXUINT64, 0,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_DATE, pspec);

	/**
	 * ZifChangeset:author:
	 *
	 * Since: 0.1.0
	 */
	pspec = g_param_spec_string ("author", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_AUTHOR, pspec);

	/**
	 * ZifChangeset:description:
	 *
	 * Since: 0.1.0
	 */
	pspec = g_param_spec_string ("description", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_DESCRIPTION, pspec);

	/**
	 * ZifChangeset:version:
	 *
	 * Since: 0.1.0
	 */
	pspec = g_param_spec_string ("version", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_VERSION, pspec);

	g_type_class_add_private (klass, sizeof (ZifChangesetPrivate));
}

/**
 * zif_changeset_init:
 **/
static void
zif_changeset_init (ZifChangeset *changeset)
{
	changeset->priv = ZIF_CHANGESET_GET_PRIVATE (changeset);
	changeset->priv->date = 0;
	changeset->priv->author = NULL;
	changeset->priv->description = NULL;
	changeset->priv->version = NULL;
}

/**
 * zif_changeset_new:
 *
 * Return value: A new #ZifChangeset instance.
 *
 * Since: 0.1.0
 **/
ZifChangeset *
zif_changeset_new (void)
{
	ZifChangeset *changeset;
	changeset = g_object_new (ZIF_TYPE_CHANGESET, NULL);
	return ZIF_CHANGESET (changeset);
}

