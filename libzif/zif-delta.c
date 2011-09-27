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
 * @short_sequence: Package delta information
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>

#include "zif-delta-private.h"
#include "zif-utils.h"

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
 * zif_delta_error_quark:
 *
 * Return value: An error quark.
 *
 * Since: 0.2.5
 **/
GQuark
zif_delta_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("zif_delta_error");
	return quark;
}

/**
 * zif_delta_get_id:
 * @delta: A #ZifDelta
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
 * @delta: A #ZifDelta
 *
 * Gets the size and size of the upsize.
 *
 * Return value: The size of the upsize, or 0 for unset.
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
 * @delta: A #ZifDelta
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
 * @delta: A #ZifDelta
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
 * @delta: A #ZifDelta
 *
 * Gets the size this delta was checksum.
 *
 * Return value: The string value, or %NULL.
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
 * @delta: A #ZifDelta
 * @id: A delta identifier
 *
 * Sets the delta identifier.
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
 * @delta: A #ZifDelta
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
 * @delta: A #ZifDelta
 * @filename: The delta filename
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
 * @delta: A #ZifDelta
 * @sequence: The delta sequence
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
 * @delta: A #ZifDelta
 * @checksum: The delta checksum size
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
 * zif_build_filename_from_basename:
 **/
static gchar *
zif_build_filename_from_basename (const gchar *directory,
				  const gchar *filename)
{
	gchar *basename;
	gchar *filename_local;

	g_return_val_if_fail (directory != NULL, NULL);
	g_return_val_if_fail (filename != NULL, NULL);

	basename = g_path_get_basename (filename);
	filename_local = g_build_filename (directory, basename, NULL);

	g_free (basename);
	return filename_local;
}

/**
 * zif_delta_rebuild:
 * @delta: A #ZifDelta
 * @directory: A local directory to save to
 * @filename: Filename to save the constructed rpm
 * @error: A #GError, or %NULL
 *
 * Rebuilds an rpm from delta.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.2.5
 **/
gboolean
zif_delta_rebuild (ZifDelta *delta,
		   const gchar *directory,
		   const gchar *filename,
		   GError **error)
{
	gboolean ret;
	gchar *applydeltarpm_cmd = NULL;
	gchar *arch = NULL;
	gchar *drpm_filename = NULL;
	gchar *rpm_filename = NULL;
	gchar *std_error = NULL;
	gint exit_status;

	g_return_val_if_fail (ZIF_IS_DELTA (delta), FALSE);
	g_return_val_if_fail (directory != NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get delta rpm local filename */
	drpm_filename = zif_build_filename_from_basename (directory,
	                                                  zif_delta_get_filename (delta));
	if (drpm_filename == NULL) {
		ret = FALSE;
		goto out;
	}

	/* get rpm local filename */
	rpm_filename = zif_build_filename_from_basename (directory,
	                                                 filename);
	if (rpm_filename == NULL) {
		ret = FALSE;
		goto out;
	}

	/* get the package arch */
	ret = zif_package_id_to_nevra (zif_delta_get_id (delta),
				       NULL, NULL, NULL, NULL,
				       &arch);
	if (!ret)
		goto out;

	applydeltarpm_cmd = g_strdup_printf ("applydeltarpm -a %s %s %s", arch, drpm_filename, rpm_filename);
	g_debug ("executing: %s", applydeltarpm_cmd);
	ret = g_spawn_command_line_sync (applydeltarpm_cmd,
	                                 NULL /* stdout */,
	                                 &std_error,
	                                 &exit_status,
	                                 error);
	if (!ret) {
		goto out;
	} else if (!WIFEXITED (exit_status) || WEXITSTATUS (exit_status) != 0) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_DELTA_ERROR,
			     ZIF_DELTA_ERROR_REBUILD_FAILED,
			     "applydeltarpm failed: %s",
			     std_error);
		goto out;
	}
	g_unlink (drpm_filename);

out:
	g_free (applydeltarpm_cmd);
	g_free (std_error);
	g_free (arch);
	g_free (drpm_filename);
	g_free (rpm_filename);
	return ret;
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
 * Return value: A new #ZifDelta instance.
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

