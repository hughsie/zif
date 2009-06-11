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

#include "dum-sack.h"
#include "dum-groups.h"
#include "dum-store-local.h"
#include "dum-sack-local.h"
#include "dum-utils.h"

G_DEFINE_TYPE (DumSackLocal, dum_sack_local, DUM_TYPE_SACK)

/**
 * dum_sack_local_finalize:
 **/
static void
dum_sack_local_finalize (GObject *object)
{
	DumSackLocal *sack;

	g_return_if_fail (object != NULL);
	g_return_if_fail (DUM_IS_SACK_LOCAL (object));
	sack = DUM_SACK_LOCAL (object);

	G_OBJECT_CLASS (dum_sack_local_parent_class)->finalize (object);
}

/**
 * dum_sack_local_class_init:
 **/
static void
dum_sack_local_class_init (DumSackLocalClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dum_sack_local_finalize;
}

/**
 * dum_sack_local_init:
 **/
static void
dum_sack_local_init (DumSackLocal *sack)
{
	DumStoreLocal *store;
	store = dum_store_local_new ();
	dum_sack_add_store (DUM_SACK (sack), DUM_STORE (store));
	g_object_unref (store);
}

/**
 * dum_sack_local_new:
 * Return value: A new sack_local class instance.
 **/
DumSackLocal *
dum_sack_local_new (void)
{
	DumSackLocal *sack;
	sack = g_object_new (DUM_TYPE_SACK_LOCAL, NULL);
	return DUM_SACK_LOCAL (sack);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
dum_sack_local_test (EggTest *test)
{
	DumGroups *groups;
	DumSackLocal *sack;
	DumStoreLocal *store;
	GPtrArray *array;
	GError *error = NULL;
	gboolean ret;

	if (!egg_test_start (test, "DumSackLocal"))
		return;

	/************************************************************/
	egg_test_title (test, "set prefix");
	store = dum_store_local_new ();
	ret = dum_store_local_set_prefix (store, "/", &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set prefix '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "set groups");
	groups = dum_groups_new ();
	ret = dum_groups_set_mapping_file (groups, "../test/share/yum-comps-groups.conf", &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set groups '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "get sack");
	sack = dum_sack_local_new ();
	egg_test_assert (test, sack != NULL);

	/************************************************************/
	egg_test_title (test, "resolve across all sack");
	array = dum_sack_resolve (DUM_SACK (sack), "kernel", &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get sack '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "resolve correct length");
	if (array->len >= 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);

	dum_list_print_array (array);
	g_ptr_array_foreach (array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (array, TRUE);

	g_object_unref (groups);
	g_object_unref (sack);
	egg_test_end (test);
}
#endif

