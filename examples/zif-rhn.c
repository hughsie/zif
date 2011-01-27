/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Richard Hughes <richard@hughsie.com>
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

/**
 * zif_rhn_package_print:
 **/
static void
zif_rhn_package_print (ZifPackage *package)
{
	const gchar *text;
	GError *error = NULL;
	GPtrArray *array;
	guint i;
	ZifDepend *depend;
	ZifState *state;

	state = zif_state_new ();

	g_print ("id=%s\n",
		 zif_package_get_id (package));

	g_print ("summary=%s\n",
		 zif_package_get_summary (package, state, &error));
	g_assert_no_error (error);

	zif_state_reset (state);
	g_print ("description=%s\n",
		 zif_package_get_description (package, state, &error));
	g_assert_no_error (error);

	zif_state_reset (state);
	g_print ("license=%s\n",
		 zif_package_get_license (package, state, &error));
	g_assert_no_error (error);

	zif_state_reset (state);
	g_print ("group=%s\n",
		 zif_package_get_group (package, state, &error));
	g_assert_no_error (error);

	zif_state_reset (state);
	g_print ("category=%s\n",
		 zif_package_get_category (package, state, &error));
	g_assert_no_error (error);

	zif_state_reset (state);
	g_print ("url=%s\n",
		 zif_package_get_url (package, state, &error));
	g_assert_no_error (error);

	zif_state_reset (state);
	g_print ("size=%"G_GUINT64_FORMAT"\n",
		 zif_package_get_size (package, state, &error));
	g_assert_no_error (error);

	g_print ("files:\n");
	zif_state_reset (state);
	array = zif_package_get_files (package, state, &error);
	g_assert_no_error (error);
	for (i=0; i<array->len; i++)
		g_print ("\t%s\n", (const gchar *) g_ptr_array_index (array, i));

	g_print ("requires:\n");
	zif_state_reset (state);
	array = zif_package_get_requires (package, state, &error);
	g_assert_no_error (error);
	for (i=0; i<array->len; i++) {
		depend = g_ptr_array_index (array, i);
		text = zif_depend_get_description (depend);
		g_print ("\t%s\n", text);
	}

	g_print ("provides:\n");
	zif_state_reset (state);
	array = zif_package_get_provides (package, state, &error);
	g_assert_no_error (error);
	for (i=0; i<array->len; i++) {
		depend = g_ptr_array_index (array, i);
		text = zif_depend_get_description (depend);
		g_print ("\t%s\n", text);
	}

	g_print ("obsoletes:\n");
	zif_state_reset (state);
	array = zif_package_get_obsoletes (package, state, &error);
	g_assert_no_error (error);
	for (i=0; i<array->len; i++) {
		depend = g_ptr_array_index (array, i);
		text = zif_depend_get_description (depend);
		g_print ("\t%s\n", text);
	}

	g_print ("conflicts:\n");
	zif_state_reset (state);
	array = zif_package_get_conflicts (package, state, &error);
	g_assert_no_error (error);
	for (i=0; i<array->len; i++) {
		depend = g_ptr_array_index (array, i);
		text = zif_depend_get_description (depend);
		g_print ("\t%s\n", text);
	}

	g_object_unref (state);
}

int
main (int argc, char **argv)
{
	gboolean ret;
	gchar *password;
	gchar *username;
	gchar *version;
	GError *error = NULL;
	GPtrArray *array;
	guint i;
	ZifConfig *config;
	ZifPackage *package;
	ZifState *state;
	ZifStore *store;

	g_type_init ();
	g_thread_init (NULL);
	zif_init ();

	/* the config file provides defaults */
	config = zif_config_new ();
	ret = zif_config_set_filename (config, "./rhn.conf", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* connect to RHN */
	store = zif_store_rhn_new ();
	zif_store_rhn_set_server (ZIF_STORE_RHN (store),
				  "https://rhn.redhat.com/rpc/api");
	zif_store_rhn_set_channel (ZIF_STORE_RHN (store),
				   "rhel-i386-client-6");
	zif_store_rhn_set_precache (ZIF_STORE_RHN (store),
				     ZIF_PACKAGE_RHN_PRECACHE_GET_DETAILS |
				     ZIF_PACKAGE_RHN_PRECACHE_LIST_FILES |
				     ZIF_PACKAGE_RHN_PRECACHE_LIST_DEPS);
	username =  zif_config_get_string (config, "username", NULL);
	password =  zif_config_get_string (config, "password", NULL);
	ret = zif_store_rhn_login (ZIF_STORE_RHN (store),
				   username,
				   password,
				   &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_free (username);
	g_free (password);

	/* show the session key and version */
	version = zif_store_rhn_get_version (ZIF_STORE_RHN (store),
					     &error);
	g_assert_no_error (error);
	g_assert (version != NULL);
	g_debug ("version = '%s', session_key = %s",
		 version,
		 zif_store_rhn_get_session_key (ZIF_STORE_RHN (store)));
	g_free (version);

	/* get all the packages */
	state = zif_state_new ();
	array = zif_store_get_packages (store, state, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		zif_rhn_package_print (package);
	}
	g_ptr_array_unref (array);

	/* logout */
	ret = zif_store_rhn_logout (ZIF_STORE_RHN (store),
				    &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_object_unref (store);
	g_object_unref (state);
	g_object_unref (config);
	return 0;
}

