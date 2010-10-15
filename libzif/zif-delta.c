/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either checksum 2 of the License, or
 * (at your option) any later checksum.
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
 * SECTION:zif-delta
 * @short_sequence: Generic object to represent some information about a delta.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>

#include "zif-delta.h"

#define ZIF_DELTA_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_DELTA, ZifDeltaPrivate))

struct _ZifDeltaPrivate
{
	gchar			*id;
	guint64			 size;
	gchar			*filename;
	gchar			*sequence;
	gchar			*checksum;
};

enum {
	PROP_0,
	PROP_ID,
	PROP_SIZE,
	PROP_FILENAME,
	PROP_SEQUENCE,
	PROP_CHECKSUM,
	PROP_LAST
};

G_DEFINE_TYPE (ZifDelta, zif_delta, G_TYPE_OBJECT)

/**
 * zif_delta_get_id:
 * @delta: the #ZifDelta object
 *
 * Gets the id for this delta.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_delta_get_id (ZifDelta *delta)
{
	g_return_val_if_fail (ZIF_IS_DELTA (delta), NULL);
	return delta->priv->id;
}

/**
 * zif_delta_get_size:
 * @delta: the #ZifDelta object
 *
 * Gets the size and size of the upsize.
 *
 * Return value: the size of the upsize, or 0 for unset.
 *
 * Since: 0.1.0
 **/
guint64
zif_delta_get_size (ZifDelta *delta)
{
	g_return_val_if_fail (ZIF_IS_DELTA (delta), 0);
	return delta->priv->size;
}

/**
 * zif_delta_get_filename:
 * @delta: the #ZifDelta object
 *
 * Gets the filename for this delta.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_delta_get_filename (ZifDelta *delta)
{
	g_return_val_if_fail (ZIF_IS_DELTA (delta), NULL);
	return delta->priv->filename;
}

/**
 * zif_delta_get_sequence:
 * @delta: the #ZifDelta object
 *
 * Gets the sequence for this delta.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_delta_get_sequence (ZifDelta *delta)
{
	g_return_val_if_fail (ZIF_IS_DELTA (delta), NULL);
	return delta->priv->sequence;
}

/**
 * zif_delta_get_checksum:
 * @delta: the #ZifDelta object
 *
 * Gets the size this delta was checksum.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_delta_get_checksum (ZifDelta *delta)
{
	g_return_val_if_fail (ZIF_IS_DELTA (delta), NULL);
	return delta->priv->checksum;
}

/**
 * zif_delta_set_id:
 * @delta: the #ZifDelta object
 * @id: the delta id
 *
 * Sets the delta id.
 *
 * Since: 0.1.0
 **/
void
zif_delta_set_id (ZifDelta *delta, const gchar *id)
{
	g_return_if_fail (ZIF_IS_DELTA (delta));
	g_return_if_fail (id != NULL);
	g_return_if_fail (delta->priv->id == NULL);

	delta->priv->id = g_strdup (id);
}

/**
 * zif_delta_set_size:
 * @delta: the #ZifDelta object
 * @size: The size of the delta
 *
 * Sets the delta size status.
 *
 * Since: 0.1.0
 **/
void
zif_delta_set_size (ZifDelta *delta, guint64 size)
{
	g_return_if_fail (ZIF_IS_DELTA (delta));
	delta->priv->size = size;
}

/**
 * zif_delta_set_filename:
 * @delta: the #ZifDelta object
 * @filename: the delta filename
 *
 * Sets the delta filename.
 *
 * Since: 0.1.0
 **/
void
zif_delta_set_filename (ZifDelta *delta, const gchar *filename)
{
	g_return_if_fail (ZIF_IS_DELTA (delta));
	g_return_if_fail (filename != NULL);
	g_return_if_fail (delta->priv->filename == NULL);

	delta->priv->filename = g_strdup (filename);
}

/**
 * zif_delta_set_sequence:
 * @delta: the #ZifDelta object
 * @sequence: the delta sequence
 *
 * Sets the delta sequence.
 *
 * Since: 0.1.0
 **/
void
zif_delta_set_sequence (ZifDelta *delta, const gchar *sequence)
{
	g_return_if_fail (ZIF_IS_DELTA (delta));
	g_return_if_fail (sequence != NULL);
	g_return_if_fail (delta->priv->sequence == NULL);

	delta->priv->sequence = g_strdup (sequence);
}

/**
 * zif_delta_set_checksum:
 * @delta: the #ZifDelta object
 * @checksum: the delta checksum size
 *
 * Sets the size the delta was checksum.
 *
 * Since: 0.1.0
 **/
void
zif_delta_set_checksum (ZifDelta *delta, const gchar *checksum)
{
	g_return_if_fail (ZIF_IS_DELTA (delta));
	g_return_if_fail (checksum != NULL);
	g_return_if_fail (delta->priv->checksum == NULL);

	delta->priv->checksum = g_strdup (checksum);
}

/**
 * zif_delta_get_property:
 **/
static void
zif_delta_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	ZifDelta *delta = ZIF_DELTA (object);
	ZifDeltaPrivate *priv = delta->priv;

	switch (prop_id) {
	case PROP_ID:
		g_value_set_string (value, priv->id);
		break;
	case PROP_SIZE:
		g_value_set_uint64 (value, priv->size);
		break;
	case PROP_FILENAME:
		g_value_set_string (value, priv->filename);
		break;
	case PROP_SEQUENCE:
		g_value_set_string (value, priv->sequence);
		break;
	case PROP_CHECKSUM:
		g_value_set_string (value, priv->checksum);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * zif_delta_set_property:
 **/
static void
zif_delta_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
}

/**
 * zif_delta_finalize:
 **/
static void
zif_delta_finalize (GObject *object)
{
	ZifDelta *delta;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_DELTA (object));
	delta = ZIF_DELTA (object);

	g_free (delta->priv->id);
	g_free (delta->priv->filename);
	g_free (delta->priv->sequence);
	g_free (delta->priv->checksum);

	G_OBJECT_CLASS (zif_delta_parent_class)->finalize (object);
}

/**
 * zif_delta_class_init:
 **/
static void
zif_delta_class_init (ZifDeltaClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_delta_finalize;
	object_class->get_property = zif_delta_get_property;
	object_class->set_property = zif_delta_set_property;

	/**
	 * ZifDelta:id:
	 *
	 * Since: 0.1.0
	 */
	pspec = g_param_spec_string ("id", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_ID, pspec);

	/**
	 * ZifDelta:size:
	 *
	 * Since: 0.1.0
	 */
	pspec = g_param_spec_uint64 ("size", NULL, NULL,
				     0, G_MAXUINT64, 0,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_SIZE, pspec);

	/**
	 * ZifDelta:filename:
	 *
	 * Since: 0.1.0
	 */
	pspec = g_param_spec_string ("filename", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_FILENAME, pspec);

	/**
	 * ZifDelta:sequence:
	 *
	 * Since: 0.1.0
	 */
	pspec = g_param_spec_string ("sequence", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_SEQUENCE, pspec);

	/**
	 * ZifDelta:checksum:
	 *
	 * Since: 0.1.0
	 */
	pspec = g_param_spec_string ("checksum", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_CHECKSUM, pspec);

	g_type_class_add_private (klass, sizeof (ZifDeltaPrivate));
}

/**
 * zif_delta_init:
 **/
static void
zif_delta_init (ZifDelta *delta)
{
	delta->priv = ZIF_DELTA_GET_PRIVATE (delta);
	delta->priv->id = NULL;
	delta->priv->size = 0;
	delta->priv->filename = NULL;
	delta->priv->sequence = NULL;
	delta->priv->checksum = NULL;
}

/**
 * zif_delta_new:
 *
 * Return value: A new #ZifDelta class instance.
 *
 * Since: 0.1.0
 **/
ZifDelta *
zif_delta_new (void)
{
	ZifDelta *delta;
	delta = g_object_new (ZIF_TYPE_DELTA, NULL);
	return ZIF_DELTA (delta);
}

