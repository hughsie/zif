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
 * SECTION:zif-legal
 * @short_description: A #ZifLegal object allows the user to check licenses
 *
 * #ZifLegal allows the user to see if a specific license string is free
 * according to the FSF.
 * Before checking any strings, the backing legal file has to be set with
 * zif_legal_set_filename() and any checks prior to that will fail.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

#include "zif-legal.h"
#include "zif-monitor.h"

#include "egg-debug.h"

#define ZIF_LEGAL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_LEGAL, ZifLegalPrivate))

struct _ZifLegalPrivate
{
	gboolean		 loaded;
	ZifMonitor		*monitor;
	GHashTable		*hash;
	gchar			*filename;
};

G_DEFINE_TYPE (ZifLegal, zif_legal, G_TYPE_OBJECT)
static gpointer zif_legal_object = NULL;

/**
 * zif_legal_error_quark:
 *
 * Return value: Our personal error quark.
 *
 * Since: 0.0.1
 **/
GQuark
zif_legal_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("zif_legal_error");
	return quark;
}

/**
 * zif_legal_load:
 **/
static gboolean
zif_legal_load (ZifLegal *legal, GError **error)
{
	gchar *data = NULL;
	gchar **lines = NULL;
	gboolean ret = FALSE;
	GError *error_local = NULL;
	guint i;

	/* nothing set */
	if (legal->priv->filename == NULL) {
		g_set_error_literal (error, ZIF_LEGAL_ERROR, ZIF_LEGAL_ERROR_FAILED,
				     "no legal filename has been set; use zif_legal_set_filename()");
		goto out;
	}

	/* load from file */
	ret = g_file_get_contents (legal->priv->filename, &data, NULL, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_LEGAL_ERROR, ZIF_LEGAL_ERROR_FAILED,
			     "failed to load data: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* setup watch */
	ret = zif_monitor_add_watch (legal->priv->monitor, legal->priv->filename, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_LEGAL_ERROR, ZIF_LEGAL_ERROR_FAILED,
			     "failed to setup watch: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* add licenses */
	lines = g_strsplit (data, "\n", -1);
	for (i=0; lines[i] != NULL; i++)
		g_hash_table_insert (legal->priv->hash, g_strdup (lines[i]), GINT_TO_POINTER(1));

	/* how many licenses? */
	egg_debug ("Added %i licenses to database", i);
	legal->priv->loaded = TRUE;
out:
	g_free (data);
	g_strfreev (lines);
	return ret;
}

/**
 * zif_legal_is_free_part:
 **/
static gboolean
zif_legal_is_free_part (ZifLegal *legal, const gchar *string)
{
	gpointer exists;

	/* exists in hash */
	exists = g_hash_table_lookup (legal->priv->hash, string);
	return (exists != NULL);
}

/**
 * zif_legal_is_free:
 * @legal: the #ZifLegal object
 * @string: the legal string to check, e.g. ""Zend and wxWidgets""
 * @is_free: if the string is a valid free legal
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds out if the package is classified as free software.
 *
 * Return value: %FALSE for failure to load legal data.
 *
 * Since: 0.0.1
 **/
gboolean
zif_legal_is_free (ZifLegal *legal, const gchar *string, gboolean *is_free, GError **error)
{
	gboolean ret = TRUE;
	gboolean one_free_group = FALSE;
	gboolean group_is_free;
	gchar **groups = NULL;
	gchar **licenses;
	guint i;
	guint j;

	g_return_val_if_fail (ZIF_IS_LEGAL (legal), FALSE);
	g_return_val_if_fail (string != NULL, FALSE);
	g_return_val_if_fail (is_free != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not loaded yet */
	if (!legal->priv->loaded) {
		ret = zif_legal_load (legal, error);
		if (!ret)
			goto out;
	}

	/* split AND */
	groups = g_strsplit (string, " and ", -1);
	for (i=0; groups[i] != NULL; i++) {
		/* remove grouping */
		g_strdelimit (groups[i], "()", ' ');

		/* split OR clase */
		licenses = g_strsplit (groups[i], " or ", 0);

		group_is_free = FALSE;
		for (j=0; licenses[j] != NULL; j++) {

			/* remove 'and later' */
			g_strdelimit (licenses[j], "+", ' ');
			g_strstrip (licenses[j]);

			/* nothing to process */
			if (licenses[j][0] == '\0')
				continue;

			/* find out if this chunk is free */
			if (zif_legal_is_free_part (legal, licenses[j])) {
				one_free_group = TRUE;
				group_is_free = TRUE;
				break;
			}
		}
		g_strfreev (licenses);

		/* shortcut, we're not going to pass now */
		if (!group_is_free) {
			*is_free = FALSE;
			goto out;
		}
	}

	/* at least one section is non-free */
	if (!one_free_group) {
		*is_free = FALSE;
		goto out;
	};

	/* otherwise okay */
	*is_free = TRUE;
out:
	egg_debug ("string %s is %s", string, *is_free ? "FREE" : "NONFREE");
	g_strfreev (groups);
	return ret;
}

/**
 * zif_legal_set_filename:
 * @legal: the #ZifLegal object
 * @filename: the system wide legal file, e.g. "/etc/yum.conf"
 *
 * Sets the filename to use as the system wide legal file.
 *
 * Since: 0.0.1
 **/
void
zif_legal_set_filename (ZifLegal *legal, const gchar *filename)
{
	g_return_if_fail (ZIF_IS_LEGAL (legal));
	g_return_if_fail (filename != NULL);
	g_return_if_fail (!legal->priv->loaded);

	g_free (legal->priv->filename);
	legal->priv->filename = g_strdup (filename);
}

/**
 * zif_legal_file_monitor_cb:
 **/
static void
zif_legal_file_monitor_cb (ZifMonitor *monitor, ZifLegal *legal)
{
	egg_warning ("legal file changed");
	g_hash_table_remove_all (legal->priv->hash);
	legal->priv->loaded = FALSE;
}

/**
 * zif_legal_finalize:
 **/
static void
zif_legal_finalize (GObject *object)
{
	ZifLegal *legal;
	g_return_if_fail (ZIF_IS_LEGAL (object));
	legal = ZIF_LEGAL (object);

	g_hash_table_unref (legal->priv->hash);
	g_object_unref (legal->priv->monitor);
	g_free (legal->priv->filename);

	G_OBJECT_CLASS (zif_legal_parent_class)->finalize (object);
}

/**
 * zif_legal_class_init:
 **/
static void
zif_legal_class_init (ZifLegalClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_legal_finalize;
	g_type_class_add_private (klass, sizeof (ZifLegalPrivate));
}

/**
 * zif_legal_init:
 **/
static void
zif_legal_init (ZifLegal *legal)
{
	legal->priv = ZIF_LEGAL_GET_PRIVATE (legal);
	legal->priv->loaded = FALSE;
	legal->priv->hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	legal->priv->filename = NULL;
	legal->priv->monitor = zif_monitor_new ();
	g_signal_connect (legal->priv->monitor, "changed", G_CALLBACK (zif_legal_file_monitor_cb), legal);
}

/**
 * zif_legal_new:
 *
 * Return value: A new #ZifLegal class instance.
 *
 * Since: 0.0.1
 **/
ZifLegal *
zif_legal_new (void)
{
	if (zif_legal_object != NULL) {
		g_object_ref (zif_legal_object);
	} else {
		zif_legal_object = g_object_new (ZIF_TYPE_LEGAL, NULL);
		g_object_add_weak_pointer (zif_legal_object, &zif_legal_object);
	}
	return ZIF_LEGAL (zif_legal_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_legal_test (EggTest *test)
{
	ZifLegal *legal;
	gboolean ret;
	gboolean is_free;
	GError *error = NULL;

	if (!egg_test_start (test, "ZifLegal"))
		return;

	/************************************************************/
	egg_test_title (test, "get legal");
	legal = zif_legal_new ();
	egg_test_assert (test, legal != NULL);

	/* set filename */
	zif_legal_set_filename (legal, "../test/share/licenses.txt");

	/************************************************************/
	egg_test_title (test, "check free legal (1)");
	ret = zif_legal_is_free (legal, "GPLv2+", &is_free, &error);
	if (!ret)
		egg_test_failed (test, "failed to load: %s", error->message);
	egg_test_assert (test, is_free);

	/************************************************************/
	egg_test_title (test, "check free legal (2)");
	ret = zif_legal_is_free (legal, "Zend and wxWidgets", &is_free, &error);
	if (!ret)
		egg_test_failed (test, "failed to load: %s", error->message);
	egg_test_assert (test, is_free);

	/************************************************************/
	egg_test_title (test, "check non-free legal");
	ret = zif_legal_is_free (legal, "Zend and wxWidgets and MSCPL", &is_free, &error);
	if (!ret)
		egg_test_failed (test, "failed to load: %s", error->message);
	egg_test_assert (test, !is_free);

	g_object_unref (legal);

	egg_test_end (test);
}
#endif

