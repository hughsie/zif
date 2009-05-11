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

#include "dum-config.h"
#include "dum-sack.h"
#include "dum-store.h"
#include "dum-repos.h"
#include "dum-sack-remote.h"
#include "dum-utils.h"

#include "egg-debug.h"

G_DEFINE_TYPE (DumSackRemote, dum_sack_remote, DUM_TYPE_SACK)

/**
 * dum_sack_remote_finalize:
 **/
static void
dum_sack_remote_finalize (GObject *object)
{
	DumSackRemote *sack;

	g_return_if_fail (object != NULL);
	g_return_if_fail (DUM_IS_SACK_REMOTE (object));
	sack = DUM_SACK_REMOTE (object);

	G_OBJECT_CLASS (dum_sack_remote_parent_class)->finalize (object);
}

/**
 * dum_sack_remote_class_init:
 **/
static void
dum_sack_remote_class_init (DumSackRemoteClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dum_sack_remote_finalize;
}

/**
 * dum_sack_remote_init:
 **/
static void
dum_sack_remote_init (DumSackRemote *sack)
{
	GPtrArray *array;
	DumRepos *repos;
	GError *error = NULL;

	repos = dum_repos_new ();
	array = dum_repos_get_stores_enabled (repos, &error);
	if (array == NULL) {
		egg_warning ("failed to get enabled stores: %s", error->message);
		g_error_free (error);
		goto out;
	}
	dum_sack_add_stores (DUM_SACK (sack), array);
	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);
out:
	g_object_unref (repos);
}

/**
 * dum_sack_remote_new:
 * Return value: A new sack_remote class instance.
 **/
DumSackRemote *
dum_sack_remote_new (void)
{
	DumSackRemote *sack;
	sack = g_object_new (DUM_TYPE_SACK_REMOTE, NULL);
	return DUM_SACK_REMOTE (sack);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
dum_sack_remote_test (EggTest *test)
{
	DumSackRemote *sack;
	DumConfig *config;
	DumRepos *repos;
	GPtrArray *array;
	GError *error = NULL;
	gchar *repos_dir;

	if (!egg_test_start (test, "DumSackRemote"))
		return;

	/* set this up as dummy */
	config = dum_config_new ();
	dum_config_set_filename (config, "../test/etc/yum.conf", NULL);
	repos_dir = dum_config_get_string (config, "reposdir", NULL);

	repos = dum_repos_new ();
	dum_repos_set_repos_dir (repos, repos_dir, NULL);

	/************************************************************/
	egg_test_title (test, "get sack");
	sack = dum_sack_remote_new ();
	egg_test_assert (test, sack != NULL);

	/************************************************************/
	egg_test_title (test, "resolve across all sack");
	array = dum_sack_resolve (DUM_SACK (sack), "kernel", &error);
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

	dum_list_print_array (array);
	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);

	g_object_unref (sack);
	g_object_unref (config);
	g_object_unref (repos);
	g_free (repos_dir);

	egg_test_end (test);
}
#endif

