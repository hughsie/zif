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
 * SECTION:zif-media
 * @short_description: Media repository support
 *
 * #ZifMedia allows Zif to use external media repositories.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <gio/gio.h>

#include "zif-media.h"

#define ZIF_MEDIA_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_MEDIA, ZifMediaPrivate))

struct _ZifMediaPrivate
{
	GVolumeMonitor		*volume_monitor;
};

G_DEFINE_TYPE (ZifMedia, zif_media, G_TYPE_OBJECT)
static gpointer zif_media_object = NULL;

/**
 * zif_media_get_root_for_mount:
 */
static gchar *
zif_media_get_root_for_mount (GMount *mount, const gchar *media_id)
{
	GFile *root;
	gchar *root_path;
	GFile *discinfo;
	gchar *discinfo_path;
	gboolean ret;
	GError *error = NULL;
	gchar *retval = NULL;
	gchar *contents = NULL;
	gchar **lines = NULL;

	/* check if any installed media is an install disk */
	root = g_mount_get_root (mount);
	root_path = g_file_get_path (root);
	discinfo_path = g_build_filename (root_path, ".discinfo", NULL);
	discinfo = g_file_new_for_path (discinfo_path);

	/* .discinfo exists */
	ret = g_file_query_exists (discinfo, NULL);
	g_warning ("checking for %s: %s", discinfo_path, ret ? "yes" : "no");
	if (!ret)
		goto out;

	/* we match media_id? */
	ret = g_file_load_contents (discinfo, NULL, &contents, NULL, NULL, &error);
	if (!ret) {
		g_warning ("failed to get contents: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* split data */
	lines = g_strsplit (contents, "\n", -1);
	if (g_strv_length (lines) < 4) {
		g_warning ("not enough data in .discinfo");
		goto out;
	}

	/* matches */
	ret = (g_strcmp0 (lines[0], media_id) == 0);
	if (!ret) {
		g_warning ("failed to match media id");
		goto out;
	}

	/* get a copy */
	retval = g_strdup (root_path);
out:
	g_strfreev (lines);
	g_free (contents);
	g_free (root_path);
	g_free (discinfo_path);
	g_object_unref (root);
	g_object_unref (discinfo);
	return retval;
}

/**
 * zif_media_get_root_from_id:
 * @media: the #ZifMedia object
 * @media_id: the media id to find, e.g. "133123.1232133"
 *
 * Finds the media root for a given media id.
 *
 * Return value: The media root, or %NULL. Free with g_free()
 *
 * Since: 0.1.0
 **/
gchar *
zif_media_get_root_from_id (ZifMedia *media, const gchar *media_id)
{
	GList *mounts, *l;
	GMount *mount;
	gchar *retval = NULL;

	/* go though each mount point */
	mounts = g_volume_monitor_get_mounts (media->priv->volume_monitor);
	for (l=mounts; l != NULL; l=l->next) {
		mount = l->data;
		retval = zif_media_get_root_for_mount (mount, media_id);
		if (retval != NULL)
			break;
	}

	g_list_foreach (mounts, (GFunc) g_object_unref, NULL);
	g_list_free (mounts);
	return retval;
}

/**
 * zif_media_finalize:
 **/
static void
zif_media_finalize (GObject *object)
{
	ZifMedia *media;
	g_return_if_fail (ZIF_IS_MEDIA (object));
	media = ZIF_MEDIA (object);

	g_object_unref (media->priv->volume_monitor);

	G_OBJECT_CLASS (zif_media_parent_class)->finalize (object);
}

/**
 * zif_media_class_init:
 **/
static void
zif_media_class_init (ZifMediaClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_media_finalize;
	g_type_class_add_private (klass, sizeof (ZifMediaPrivate));
}

/**
 * zif_media_init:
 **/
static void
zif_media_init (ZifMedia *media)
{
	media->priv = ZIF_MEDIA_GET_PRIVATE (media);
	media->priv->volume_monitor = g_volume_monitor_get ();
}

/**
 * zif_media_new:
 *
 * Return value: A new #ZifMedia class instance.
 *
 * Since: 0.1.0
 **/
ZifMedia *
zif_media_new (void)
{
	if (zif_media_object != NULL) {
		g_object_ref (zif_media_object);
	} else {
		zif_media_object = g_object_new (ZIF_TYPE_MEDIA, NULL);
		g_object_add_weak_pointer (zif_media_object, &zif_media_object);
	}
	return ZIF_MEDIA (zif_media_object);
}

