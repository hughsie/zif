/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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

#include <string.h>

#include <glib.h>

#include "dum-groups.h"
#include "dum-monitor.h"

#include "egg-debug.h"
#include "egg-string.h"

#define DUM_GROUPS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DUM_TYPE_GROUPS, DumGroupsPrivate))

struct DumGroupsPrivate
{
	gboolean		 loaded;
	PkBitfield		 groups;
	GPtrArray		*categories;
	GHashTable		*hash;
	gchar			*mapping_file;
	DumMonitor		*monitor;
};

G_DEFINE_TYPE (DumGroups, dum_groups, G_TYPE_OBJECT)
static gpointer dum_groups_object = NULL;

/**
 * dum_groups_set_mapping_file:
 **/
gboolean
dum_groups_set_mapping_file (DumGroups *groups, const gchar *mapping_file, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;

	g_return_val_if_fail (DUM_IS_GROUPS (groups), FALSE);
	g_return_val_if_fail (groups->priv->mapping_file == NULL, FALSE);
	g_return_val_if_fail (!groups->priv->loaded, FALSE);
	g_return_val_if_fail (mapping_file != NULL, FALSE);

	/* check file exists */
	ret = g_file_test (mapping_file, G_FILE_TEST_IS_REGULAR);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "mapping file %s does not exist", mapping_file);
		goto out;
	}

	/* setup watch */
	ret = dum_monitor_add_watch (groups->priv->monitor, mapping_file, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to setup watch: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	groups->priv->mapping_file = g_strdup (mapping_file);
out:
	return ret;
}

/**
 * dum_groups_load:
 **/
gboolean
dum_groups_load (DumGroups *groups, GError **error)
{
	gboolean ret = TRUE;
	gchar *data = NULL;
	gchar **lines = NULL;
	gchar **cols;
	gchar **entries;
	guint i, j;
	PkGroupEnum group;
	GError *error_local = NULL;

	g_return_val_if_fail (DUM_IS_GROUPS (groups), FALSE);
	g_return_val_if_fail (groups->priv->mapping_file != NULL, FALSE);
	g_return_val_if_fail (groups->priv->categories->len == 0, FALSE);

	/* already loaded */
	if (groups->priv->loaded)
		goto out;

	/* get data */
	ret = g_file_get_contents (groups->priv->mapping_file, &data, NULL, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get groups data: %s", error_local->message);
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
		group = pk_group_enum_from_text (cols[0]);
		pk_bitfield_add (groups->priv->groups, group);

		/* add entries to cats list and dist */
		entries = g_strsplit (cols[1], ",", -1);
		for (j=0; entries[j] != NULL; j++) {
			g_ptr_array_add (groups->priv->categories, g_strdup (entries[j]));
			g_hash_table_insert (groups->priv->hash, g_strdup (entries[j]), GUINT_TO_POINTER(group));
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
 * dum_groups_get_groups:
 **/
PkBitfield
dum_groups_get_groups (DumGroups *groups, GError **error)
{
	GError *error_local;
	gboolean ret;

	g_return_val_if_fail (DUM_IS_GROUPS (groups), 0);

	/* if not already loaded, load */
	if (!groups->priv->loaded) {
		ret = dum_groups_load (groups, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load config file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}
out:
	return groups->priv->groups;
}

/**
 * dum_groups_get_categories:
 **/
GPtrArray *
dum_groups_get_categories (DumGroups *groups, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	GError *error_local;
	gboolean ret;

	g_return_val_if_fail (DUM_IS_GROUPS (groups), NULL);

	/* if not already loaded, load */
	if (!groups->priv->loaded) {
		ret = dum_groups_load (groups, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load config file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	array = g_ptr_array_new ();
	for (i=0; i<groups->priv->categories->len; i++)
		g_ptr_array_add (array, g_strdup (g_ptr_array_index (groups->priv->categories, i)));
out:
	return array;
}

/**
 * dum_groups_get_group_for_cat:
 **/
PkGroupEnum
dum_groups_get_group_for_cat (DumGroups *groups, const gchar *cat, GError **error)
{
	gpointer data;
	PkGroupEnum group = PK_GROUP_ENUM_UNKNOWN;
	GError *error_local;
	gboolean ret;

	g_return_val_if_fail (DUM_IS_GROUPS (groups), PK_GROUP_ENUM_UNKNOWN);
	g_return_val_if_fail (cat != NULL, PK_GROUP_ENUM_UNKNOWN);

	/* if not already loaded, load */
	if (!groups->priv->loaded) {
		ret = dum_groups_load (groups, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to load config file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* get cat -> group mapping */
	data = g_hash_table_lookup (groups->priv->hash, cat);
	if (data == NULL)
		goto out;

	group = GPOINTER_TO_INT(data);
out:
	return group;
}

/**
 * dum_groups_file_monitor_cb:
 **/
static void
dum_groups_file_monitor_cb (DumMonitor *monitor, DumGroups *groups)
{
	/* free invalid data */
	groups->priv->loaded = FALSE;
	g_ptr_array_foreach (groups->priv->categories, (GFunc) g_free, NULL);
	g_ptr_array_set_size (groups->priv->categories, 0);
	g_hash_table_remove_all (groups->priv->hash);

	egg_debug ("mapping file changed");
}

/**
 * dum_groups_finalize:
 **/
static void
dum_groups_finalize (GObject *object)
{
	DumGroups *groups;
	g_return_if_fail (DUM_IS_GROUPS (object));
	groups = DUM_GROUPS (object);

	g_ptr_array_foreach (groups->priv->categories, (GFunc) g_free, NULL);
	g_ptr_array_free (groups->priv->categories, TRUE);
	g_hash_table_unref (groups->priv->hash);
	g_free (groups->priv->mapping_file);
	g_object_unref (groups->priv->monitor);

	G_OBJECT_CLASS (dum_groups_parent_class)->finalize (object);
}

/**
 * dum_groups_class_init:
 **/
static void
dum_groups_class_init (DumGroupsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dum_groups_finalize;
	g_type_class_add_private (klass, sizeof (DumGroupsPrivate));
}

/**
 * dum_groups_init:
 **/
static void
dum_groups_init (DumGroups *groups)
{
	groups->priv = DUM_GROUPS_GET_PRIVATE (groups);
	groups->priv->mapping_file = NULL;
	groups->priv->loaded = FALSE;
	groups->priv->groups = 0;
	groups->priv->categories = g_ptr_array_new ();
	groups->priv->hash = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free, NULL);
	groups->priv->monitor = dum_monitor_new ();
	g_signal_connect (groups->priv->monitor, "changed", G_CALLBACK (dum_groups_file_monitor_cb), groups);
}

/**
 * dum_groups_new:
 * Return value: A new groups class instance.
 **/
DumGroups *
dum_groups_new (void)
{
	if (dum_groups_object != NULL) {
		g_object_ref (dum_groups_object);
	} else {
		dum_groups_object = g_object_new (DUM_TYPE_GROUPS, NULL);
		g_object_add_weak_pointer (dum_groups_object, &dum_groups_object);
	}
	return DUM_GROUPS (dum_groups_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
dum_groups_test (EggTest *test)
{
	DumGroups *groups;
	gboolean ret;
	GPtrArray *array;
	GError *error = NULL;
	PkGroupEnum group;
	PkBitfield groups_bit;
	gchar *text;

	if (!egg_test_start (test, "DumGroups"))
		return;

	/************************************************************/
	egg_test_title (test, "get groups");
	groups = dum_groups_new ();
	egg_test_assert (test, groups != NULL);

	/************************************************************/
	egg_test_title (test, "set mapping file");
	ret = dum_groups_set_mapping_file (groups, "../test/share/yum-comps-groups.conf", &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set file '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "load");
	ret = dum_groups_load (groups, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "get groups");
	groups_bit = dum_groups_get_groups (groups, NULL);
	text = pk_group_bitfield_to_text (groups_bit);
	if (egg_strequal (text, "admin-tools;desktop-gnome;desktop-kde;desktop-other;"
				"education;fonts;games;graphics;internet;"
				"legacy;localization;multimedia;office;other;programming;"
				"publishing;servers;system;virtualization"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid groups '%s'", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "get categories");
	array = dum_groups_get_categories (groups, NULL);
	if (array->len > 100)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid size '%i'", array->len);
	g_ptr_array_foreach (array, (GFunc) g_free, NULL);
	g_ptr_array_free (array, TRUE);

	/************************************************************/
	egg_test_title (test, "get group for cat");
	group = dum_groups_get_group_for_cat (groups, "language-support;kashubian-support", NULL);
	if (egg_strequal (pk_group_enum_to_text (group), "localization"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid groups '%s'", pk_group_enum_to_text (group));

	g_object_unref (groups);

	egg_test_end (test);
}
#endif

