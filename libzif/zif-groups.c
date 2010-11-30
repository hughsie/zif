/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2010 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-groups
 * @short_description: Category to group mapping
 *
 * In Zif, we have a few groups that are enumerated, and categories that are
 * not enumerated and are custom to the vendor. The mapping from categories
 * to groups (and vice versa) is done with a mapping file which has to be
 * set using zif_groups_set_mapping_file() before any queries are done.
 *
 * In zif parlance, a group is a single string, e.g. "education" and
 * a category is two strings, a parent and chld that are joined with a
 * delimiter, e.g. "apps;education".
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>

#include <glib.h>

#include "zif-groups.h"
#include "zif-monitor.h"

#define ZIF_GROUPS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_GROUPS, ZifGroupsPrivate))

/**
 * ZifGroupsPrivate:
 *
 * Private #ZifGroups data
 **/
struct _ZifGroupsPrivate
{
	gboolean		 loaded;
	GPtrArray		*groups;
	GPtrArray		*categories;
	GHashTable		*hash;
	gchar			*mapping_file;
	ZifMonitor		*monitor;
	guint			 monitor_changed_id;
};

G_DEFINE_TYPE (ZifGroups, zif_groups, G_TYPE_OBJECT)
static gpointer zif_groups_object = NULL;

/**
 * zif_groups_error_quark:
 *
 * Return value: An error quark.
 *
 * Since: 0.1.0
 **/
GQuark
zif_groups_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("zif_groups_error");
	return quark;
}

/**
 * zif_groups_set_mapping_file:
 * @groups: A #ZifGroups
 * @mapping_file: Mapping filename from categories to groups
 * @error: A #GError, or %NULL
 *
 * This sets up the file that is used to map categories to group enums.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_groups_set_mapping_file (ZifGroups *groups, const gchar *mapping_file, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_GROUPS (groups), FALSE);
	g_return_val_if_fail (groups->priv->mapping_file == NULL, FALSE);
	g_return_val_if_fail (!groups->priv->loaded, FALSE);
	g_return_val_if_fail (mapping_file != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check file exists */
	ret = g_file_test (mapping_file, G_FILE_TEST_IS_REGULAR);
	if (!ret) {
		g_set_error (error, ZIF_GROUPS_ERROR, ZIF_GROUPS_ERROR_FAILED,
			     "mapping file %s does not exist", mapping_file);
		goto out;
	}

	/* setup watch */
	ret = zif_monitor_add_watch (groups->priv->monitor, mapping_file, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_GROUPS_ERROR, ZIF_GROUPS_ERROR_FAILED,
			     "failed to setup watch: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	groups->priv->mapping_file = g_strdup (mapping_file);
out:
	return ret;
}

/**
 * zif_groups_load:
 * @groups: A #ZifGroups
 * @error: A #GError, or %NULL
 *
 * Loads the mapping file from disk into memory.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_groups_load (ZifGroups *groups, GError **error)
{
	gboolean ret = TRUE;
	gchar *data = NULL;
	gchar **lines = NULL;
	gchar **cols;
	gchar **entries;
	guint i, j;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_GROUPS (groups), FALSE);
	g_return_val_if_fail (groups->priv->categories->len == 0, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already loaded */
	if (groups->priv->loaded)
		goto out;

	/* no mapping file */
	if (groups->priv->mapping_file == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_GROUPS_ERROR,
			     ZIF_GROUPS_ERROR_FAILED,
			     "no mapping file set, so cannot load group lists");
		goto out;
	}

	/* get data */
	ret = g_file_get_contents (groups->priv->mapping_file, &data, NULL, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_GROUPS_ERROR, ZIF_GROUPS_ERROR_FAILED,
			     "failed to get groups data: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* process each line */
	lines = g_strsplit (data, "\n", 0);
	for (i=0; lines[i] != NULL; i++) {
		cols = g_strsplit (lines[i], "=", -1);
		if (g_strv_length (cols) != 2)
			goto cont;

		/* add to groups list */
		g_ptr_array_add (groups->priv->groups, g_strdup (cols[0]));

		/* add entries to cats list and dist */
		entries = g_strsplit (cols[1], ",", -1);
		for (j=0; entries[j] != NULL; j++) {
			g_ptr_array_add (groups->priv->categories, g_strdup (entries[j]));
			g_hash_table_insert (groups->priv->hash, g_strdup (entries[j]), g_strdup (cols[0]));
		}
		g_strfreev (entries);
cont:
		g_strfreev (cols);
	}

	groups->priv->loaded = TRUE;

out:
	g_free (data);
	g_strfreev (lines);
	return ret;
}

/**
 * zif_groups_get_groups:
 * @groups: A #ZifGroups
 * @error: A #GError, or %NULL
 *
 * Gets the groups supported by the packaging system.
 *
 * Return value: An array of the string groups that are supported, free
 * with g_ptr_array_unref() when done.
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_groups_get_groups (ZifGroups *groups, GError **error)
{
	GError *error_local = NULL;
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_GROUPS (groups), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* if not already loaded, load */
	if (!groups->priv->loaded) {
		ret = zif_groups_load (groups, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_GROUPS_ERROR, ZIF_GROUPS_ERROR_FAILED,
				     "failed to load config file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}
out:
	return g_ptr_array_ref (groups->priv->groups);
}

/**
 * zif_groups_get_categories:
 * @groups: A #ZifGroups
 * @error: A #GError, or %NULL
 *
 * Gets the categories supported by the packaging system.
 *
 * Return value: category list as an array of strings, free
 * with g_ptr_array_unref() when done.
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_groups_get_categories (ZifGroups *groups, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	GError *error_local = NULL;
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_GROUPS (groups), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* if not already loaded, load */
	if (!groups->priv->loaded) {
		ret = zif_groups_load (groups, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_GROUPS_ERROR, ZIF_GROUPS_ERROR_FAILED,
				     "failed to load config file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* deep copy */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
	for (i=0; i<groups->priv->categories->len; i++)
		g_ptr_array_add (array, g_strdup (g_ptr_array_index (groups->priv->categories, i)));
out:
	return array;
}

/**
 * zif_groups_get_cats_for_group:
 * @groups: A #ZifGroups
 * @group_enum: A group enumeration, e.g. "education"
 * @error: A #GError, or %NULL
 *
 * Gets all the categories that map to to this group enumeration.
 *
 * Return value: category list as an array of strings, free
 * with g_ptr_array_unref() when done.
 *
 * Since: 0.1.1
 **/
GPtrArray *
zif_groups_get_cats_for_group (ZifGroups *groups, const gchar *group_enum, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	GError *error_local = NULL;
	gboolean ret;
	ZifGroupsPrivate *priv;
	const gchar *category;
	const gchar *group;

	g_return_val_if_fail (ZIF_IS_GROUPS (groups), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* get private instance */
	priv = groups->priv;

	/* if not already loaded, load */
	if (!priv->loaded) {
		ret = zif_groups_load (groups, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_GROUPS_ERROR, ZIF_GROUPS_ERROR_FAILED,
				     "failed to load config file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* create results array, as even missing groups do not end in failure */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);

	/* go through categories and get groups, if they match, then add */
	for (i=0; i < priv->categories->len; i++) {
		category = g_ptr_array_index (priv->categories, i);

		/* get cat -> group mapping */
		group = (const gchar *)g_hash_table_lookup (groups->priv->hash, category);

		/* add to results array */
		if (g_strcmp0 (group, group_enum) == 0)
			g_ptr_array_add (array, g_strdup (category));;
	}
out:
	return array;
}

/**
 * zif_groups_get_group_for_cat:
 * @groups: A #ZifGroups
 * @cat: Category name, e.g. "games/action"
 * @error: A #GError, or %NULL
 *
 * Returns the group enumerated type for the category.
 *
 * Return value: A specific group name or %NULL
 *
 * Since: 0.1.0
 **/
const gchar *
zif_groups_get_group_for_cat (ZifGroups *groups, const gchar *cat, GError **error)
{
	const gchar *group = NULL;
	GError *error_local = NULL;
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_GROUPS (groups), NULL);
	g_return_val_if_fail (cat != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* if not already loaded, load */
	if (!groups->priv->loaded) {
		ret = zif_groups_load (groups, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_GROUPS_ERROR, ZIF_GROUPS_ERROR_FAILED,
				     "failed to load config file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* get cat -> group mapping */
	group = (const gchar *)g_hash_table_lookup (groups->priv->hash, cat);
	if (group == NULL) {
		g_set_error (error, ZIF_GROUPS_ERROR, ZIF_GROUPS_ERROR_FAILED,
			     "failed to get group for %s", cat);
		goto out;
	}
out:
	return group;
}

/**
 * zif_groups_file_monitor_cb:
 **/
static void
zif_groups_file_monitor_cb (ZifMonitor *monitor, ZifGroups *groups)
{
	/* free invalid data */
	groups->priv->loaded = FALSE;
	g_ptr_array_set_size (groups->priv->categories, 0);
	g_hash_table_remove_all (groups->priv->hash);

	g_debug ("mapping file changed");
}

/**
 * zif_groups_finalize:
 **/
static void
zif_groups_finalize (GObject *object)
{
	ZifGroups *groups;
	g_return_if_fail (ZIF_IS_GROUPS (object));
	groups = ZIF_GROUPS (object);

	g_ptr_array_unref (groups->priv->groups);
	g_ptr_array_unref (groups->priv->categories);
	g_hash_table_unref (groups->priv->hash);
	g_free (groups->priv->mapping_file);
	g_signal_handler_disconnect (groups->priv->monitor, groups->priv->monitor_changed_id);
	g_object_unref (groups->priv->monitor);

	G_OBJECT_CLASS (zif_groups_parent_class)->finalize (object);
}

/**
 * zif_groups_class_init:
 **/
static void
zif_groups_class_init (ZifGroupsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_groups_finalize;
	g_type_class_add_private (klass, sizeof (ZifGroupsPrivate));
}

/**
 * zif_groups_init:
 **/
static void
zif_groups_init (ZifGroups *groups)
{
	groups->priv = ZIF_GROUPS_GET_PRIVATE (groups);
	groups->priv->mapping_file = NULL;
	groups->priv->loaded = FALSE;
	groups->priv->groups = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);;
	groups->priv->categories = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
	groups->priv->hash = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free, (GDestroyNotify) g_free);
	groups->priv->monitor = zif_monitor_new ();
	groups->priv->monitor_changed_id =
		g_signal_connect (groups->priv->monitor, "changed",
				  G_CALLBACK (zif_groups_file_monitor_cb), groups);
}

/**
 * zif_groups_new:
 *
 * Return value: A new #ZifGroups instance.
 *
 * Since: 0.1.0
 **/
ZifGroups *
zif_groups_new (void)
{
	if (zif_groups_object != NULL) {
		g_object_ref (zif_groups_object);
	} else {
		zif_groups_object = g_object_new (ZIF_TYPE_GROUPS, NULL);
		g_object_add_weak_pointer (zif_groups_object, &zif_groups_object);
	}
	return ZIF_GROUPS (zif_groups_object);
}

