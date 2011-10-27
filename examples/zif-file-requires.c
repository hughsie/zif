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

#include "config.h"

#include <glib.h>
#include <zif.h>

int
main (int argc, char **argv)
{
	gboolean ret;
	gchar *dirname;
	GError *error = NULL;
	GHashTable *hash;
	gpointer result;
	GPtrArray *packages;
	GPtrArray *req;
	GPtrArray *stores;
	guint i, j;
	ZifConfig *config;
	ZifDepend *depend;
	ZifPackage *package;
	ZifState *state;

	g_type_init ();
	g_thread_init (NULL);

	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	config = zif_config_new ();
	g_assert (config != NULL);

	ret = zif_config_set_filename (config, "../etc/zif.conf", &error);
	g_assert_no_error (error);
	g_assert (ret);

	stores = zif_store_array_new ();

	state = zif_state_new ();

	zif_state_reset (state);
	ret = zif_store_array_add_local (stores, state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	zif_state_reset (state);
	ret = zif_store_array_add_remote_enabled (stores, state, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* get all the file provides for every local file */
	zif_state_reset (state);
	packages = zif_store_array_get_packages (stores, state, &error);
	g_assert_no_error (error);
	g_assert (packages != NULL);
	for (i=0; i<packages->len; i++) {
		package = g_ptr_array_index (packages, i);
		zif_state_reset (state);
		req = zif_package_get_requires (package, state, &error);
		g_assert_no_error (error);
		g_assert (req != NULL);
		for (j=0; j<req->len; j++) {
			depend = g_ptr_array_index (req, j);
			if (zif_depend_get_flag (depend) != ZIF_DEPEND_FLAG_ANY)
				continue;
			if (zif_depend_get_name (depend)[0] != '/')
				continue;
//			dirname = g_path_get_dirname (zif_depend_get_name (depend));
			dirname = g_strdup (zif_depend_get_name (depend));
			result = g_hash_table_lookup (hash, dirname);
			if (result == NULL) {
				g_print ("%s (%s), \n", dirname, zif_package_get_name (package));
				g_hash_table_insert (hash, g_strdup (dirname), g_strdup (dirname));
			}
			g_free (dirname);
		}
		g_ptr_array_unref (req);
	}
	g_ptr_array_unref (packages);
	return 0;
}

