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
	GPtrArray *stores;
	guint i;
	ZifConfig *config;
	ZifLock *lock;
	ZifState *state;
	ZifStore *store;
	ZifRepos *repos;

	g_type_init ();
	g_thread_init (NULL);
	zif_init ();

	/* the config file provides defaults */
	config = zif_config_new ();
	ret = zif_config_set_filename (config, "../etc/zif.conf", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* lock the transaction early, to avoid waiting later */
	lock = zif_lock_new ();
	ret = zif_lock_set_locked (lock, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* create a local store -- we don't have to set the prefix as
	 * we're using the default value in the config file */
	repos = zif_repos_new ();

	/* use progress reporting -- noo need to set the number of steps
	 * as we're only using one method that needs the state */
	state = zif_state_new ();

	/* get all the enabled repos */
	zif_state_reset (state);
	stores = zif_repos_get_stores_enabled (repos, state, &error);
	g_assert_no_error (error);
	g_assert (stores != NULL);
	for (i=0; i<stores->len; i++) {
		store = g_ptr_array_index (stores, i);
		g_print ("%s\n", zif_store_get_id (store));
	}

	/* unlock now, although it's not the end of the world if we just
	 * return, as on next start the pid will be gone and the lock
	 * will succed anyway */
	ret = zif_lock_set_unlocked (lock, &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_ptr_array_unref (stores);
	g_object_unref (lock);
	g_object_unref (config);
	g_object_unref (repos);
	g_object_unref (state);
	return 0;
}

