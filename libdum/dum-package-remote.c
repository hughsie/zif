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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>

#include "egg-debug.h"

#include "dum-utils.h"
#include "dum-package-remote.h"
#include "dum-groups.h"
#include "dum-string.h"

#define DUM_PACKAGE_REMOTE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DUM_TYPE_PACKAGE_REMOTE, DumPackageRemotePrivate))

struct DumPackageRemotePrivate
{
	DumGroups		*groups;
	gchar			*sql_id;
};

G_DEFINE_TYPE (DumPackageRemote, dum_package_remote, DUM_TYPE_PACKAGE)

/**
 * dum_package_remote_set_from_repo:
 **/
gboolean
dum_package_remote_set_from_repo (DumPackageRemote *pkg, guint length, gchar **type, gchar **data, const gchar *repo_id, GError **error)
{
	guint i;
	const gchar *name = NULL;
	const gchar *epoch = NULL;
	const gchar *version = NULL;
	const gchar *release = NULL;
	const gchar *arch = NULL;
	PkPackageId *id;
	DumString *string;

	g_return_val_if_fail (DUM_IS_PACKAGE_REMOTE (pkg), FALSE);
	g_return_val_if_fail (type != NULL, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);

	/* get the ID */
	for (i=0; i<length; i++) {
		if (g_strcmp0 (type[i], "name") == 0) {
			name = data[i];
		} else if (g_strcmp0 (type[i], "epoch") == 0) {
			epoch = data[i];
		} else if (g_strcmp0 (type[i], "version") == 0) {
			version = data[i];
		} else if (g_strcmp0 (type[i], "release") == 0) {
			release = data[i];
		} else if (g_strcmp0 (type[i], "arch") == 0) {
			arch = data[i];
		} else if (g_strcmp0 (type[i], "summary") == 0) {
			string = dum_string_new (data[i]);
			dum_package_set_summary (DUM_PACKAGE (pkg), string);
			dum_string_unref (string);
		} else if (g_strcmp0 (type[i], "description") == 0) {
			string = dum_string_new (data[i]);
			dum_package_set_description (DUM_PACKAGE (pkg), string);
			dum_string_unref (string);
		} else if (g_strcmp0 (type[i], "url") == 0) {
			string = dum_string_new (data[i]);
			dum_package_set_url (DUM_PACKAGE (pkg), string);
			dum_string_unref (string);
		} else if (g_strcmp0 (type[i], "rpm_license") == 0) {
			string = dum_string_new (data[i]);
			dum_package_set_license (DUM_PACKAGE (pkg), string);
			dum_string_unref (string);
		} else if (g_strcmp0 (type[i], "rpm_group") == 0) {
			string = dum_string_new (data[i]);
			dum_package_set_category (DUM_PACKAGE (pkg), string);
			dum_string_unref (string);
		} else if (g_strcmp0 (type[i], "size_package") == 0) {
			dum_package_set_size (DUM_PACKAGE (pkg), atoi (data[i]));
		} else if (g_strcmp0 (type[i], "pkgId") == 0) {
			pkg->priv->sql_id = g_strdup (data[i]);
		} else if (g_strcmp0 (type[i], "location_href") == 0) {
			string = dum_string_new (data[i]);
			dum_package_set_location_href (DUM_PACKAGE (pkg), string);
			dum_string_unref (string);
		} else {
			egg_warning ("unregognised: %s=%s", type[i], data[i]);
		}
	}
	dum_package_set_installed (DUM_PACKAGE (pkg), FALSE);
	id = dum_package_id_from_nevra (name, epoch, version, release, arch, repo_id);
	dum_package_set_id (DUM_PACKAGE (pkg), id);
	pk_package_id_free (id);
	return TRUE;
}

/**
 * dum_package_remote_finalize:
 **/
static void
dum_package_remote_finalize (GObject *object)
{
	DumPackageRemote *pkg;

	g_return_if_fail (object != NULL);
	g_return_if_fail (DUM_IS_PACKAGE_REMOTE (object));
	pkg = DUM_PACKAGE_REMOTE (object);

	g_free (pkg->priv->sql_id);
	g_object_unref (pkg->priv->groups);

	G_OBJECT_CLASS (dum_package_remote_parent_class)->finalize (object);
}

/**
 * dum_package_remote_class_init:
 **/
static void
dum_package_remote_class_init (DumPackageRemoteClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dum_package_remote_finalize;
	g_type_class_add_private (klass, sizeof (DumPackageRemotePrivate));
}

/**
 * dum_package_remote_init:
 **/
static void
dum_package_remote_init (DumPackageRemote *pkg)
{
	pkg->priv = DUM_PACKAGE_REMOTE_GET_PRIVATE (pkg);
	pkg->priv->sql_id = NULL;
	pkg->priv->groups = dum_groups_new ();
}

/**
 * dum_package_remote_new:
 * Return value: A new package_remote class instance.
 **/
DumPackageRemote *
dum_package_remote_new (void)
{
	DumPackageRemote *pkg;
	pkg = g_object_new (DUM_TYPE_PACKAGE_REMOTE, NULL);
	return DUM_PACKAGE_REMOTE (pkg);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
dum_package_remote_test (EggTest *test)
{
	DumPackageRemote *pkg;

	if (!egg_test_start (test, "DumPackageRemote"))
		return;

	/************************************************************/
	egg_test_title (test, "get package_remote");
	pkg = dum_package_remote_new ();
	egg_test_assert (test, pkg != NULL);

	g_object_unref (pkg);

	egg_test_end (test);
}
#endif

