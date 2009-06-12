/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

#include "zif-config.h"
#include "zif-sack.h"
#include "zif-store.h"
#include "zif-repos.h"
#include "zif-sack-remote.h"
#include "zif-utils.h"

#include "egg-debug.h"

G_DEFINE_TYPE (ZifSackRemote, zif_sack_remote, ZIF_TYPE_SACK)

/**
 * zif_sack_remote_finalize:
 **/
static void
zif_sack_remote_finalize (GObject *object)
{
	ZifSackRemote *sack;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_SACK_REMOTE (object));
	sack = ZIF_SACK_REMOTE (object);

	G_OBJECT_CLASS (zif_sack_remote_parent_class)->finalize (object);
}

/**
 * zif_sack_remote_class_init:
 **/
static void
zif_sack_remote_class_init (ZifSackRemoteClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_sack_remote_finalize;
}

/**
 * zif_sack_remote_init:
 **/
static void
zif_sack_remote_init (ZifSackRemote *sack)
{
	GPtrArray *array;
	ZifRepos *repos;
	GError *error = NULL;

	repos = zif_repos_new ();
	array = zif_repos_get_stores_enabled (repos, &error);
	if (array == NULL) {
		egg_warning ("failed to get enabled stores: %s", error->message);
		g_error_free (error);
		goto out;
	}
	zif_sack_add_stores (ZIF_SACK (sack), array);
	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);
out:
	g_object_unref (repos);
}

/**
 * zif_sack_remote_new:
 * Return value: A new sack_remote class instance.
 **/
ZifSackRemote *
zif_sack_remote_new (void)
{
	ZifSackRemote *sack;
	sack = g_object_new (ZIF_TYPE_SACK_REMOTE, NULL);
	return ZIF_SACK_REMOTE (sack);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_sack_remote_test (EggTest *test)
{
	ZifSackRemote *sack;
	ZifConfig *config;
	ZifRepos *repos;
	GPtrArray *array;
	GError *error = NULL;
	gchar *repos_dir;

	if (!egg_test_start (test, "ZifSackRemote"))
		return;

	/* set this up as zifmy */
	config = zif_config_new ();
	zif_config_set_filename (config, "../test/etc/yum.conf", NULL);
	repos_dir = zif_config_get_string (config, "reposdir", NULL);

	repos = zif_repos_new ();
	zif_repos_set_repos_dir (repos, repos_dir, NULL);

	/************************************************************/
	egg_test_title (test, "get sack");
	sack = zif_sack_remote_new ();
	egg_test_assert (test, sack != NULL);

	/************************************************************/
	egg_test_title (test, "resolve across all sack");
	array = zif_sack_resolve (ZIF_SACK (sack), "kernel", &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load metadata '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "resolve correct length");
	if (array->len == 4)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);

	zif_list_print_array (array);
	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);

	g_object_unref (sack);
	g_object_unref (config);
	g_object_unref (repos);
	g_free (repos_dir);

	egg_test_end (test);
}
#endif

