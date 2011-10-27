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
	GError *error = NULL;
	GPtrArray *packages;
	guint i;
	ZifConfig *config;
	ZifPackage *package;
	ZifState *state;
	ZifStore *store;

	g_type_init ();
	g_thread_init (NULL);

	/* the config file provides defaults */
	config = zif_config_new ();
	ret = zif_config_set_filename (config, "../etc/zif.conf", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* create a local store -- we don't have to set the prefix as
	 * we're using the default value in the config file */
	store = zif_store_local_new ();

	/* use progress reporting -- noo need to set the number of steps
	 * as we're only using one method that needs the state */
	state = zif_state_new ();

	/* get all the packages in the store */
	packages = zif_store_get_packages (store, state, &error);
	g_assert_no_error (error);
	g_assert (packages != NULL);
	for (i=0; i<packages->len; i++) {
		package = g_ptr_array_index (packages, i);
		g_print ("%s\n", zif_package_get_printable (package));
	}

	g_ptr_array_unref (packages);
	g_object_unref (config);
	g_object_unref (store);
	g_object_unref (state);
	return 0;
}

