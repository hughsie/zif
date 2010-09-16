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

/**
 * SECTION:zif-update
 * @short_description: Generic object to represent some information about an update.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>

#include "egg-debug.h"

#include "zif-update.h"

#define ZIF_UPDATE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_UPDATE, ZifUpdatePrivate))

struct _ZifUpdatePrivate
{
	ZifUpdateState		 state;
	ZifUpdateKind		 kind;
	gchar			*id;
	gchar			*title;
	gchar			*description;
	gchar			*issued;
	gboolean		 reboot;
	GPtrArray		*update_infos;
	GPtrArray		*packages;
	GPtrArray		*changelog;
};

enum {
	PROP_0,
	PROP_STATE,
	PROP_KIND,
	PROP_ID,
	PROP_TITLE,
	PROP_DESCRIPTION,
	PROP_ISSUED,
	PROP_REBOOT,
	PROP_LAST
};

G_DEFINE_TYPE (ZifUpdate, zif_update, G_TYPE_OBJECT)

/**
 * zif_update_state_from_string:
 **/
ZifUpdateState
zif_update_state_from_string (const gchar *state)
{
	if (g_strcmp0 (state, "stable") == 0)
		return ZIF_UPDATE_STATE_UNKNOWN;
	if (g_strcmp0 (state, "testing") == 0)
		return ZIF_UPDATE_STATE_TESTING;
	egg_warning ("unknown update state: %s", state);
	return ZIF_UPDATE_STATE_UNKNOWN;
}

/**
 * zif_update_kind_from_string:
 **/
ZifUpdateKind
zif_update_kind_from_string (const gchar *kind)
{
	if (g_strcmp0 (kind, "bugfix") == 0)
		return ZIF_UPDATE_KIND_BUGFIX;
	if (g_strcmp0 (kind, "security") == 0)
		return ZIF_UPDATE_KIND_SECURITY;
	if (g_strcmp0 (kind, "enhancement") == 0)
		return ZIF_UPDATE_KIND_ENHANCEMENT;
	if (g_strcmp0 (kind, "newpackage") == 0)
		return ZIF_UPDATE_KIND_NEWPACKAGE;
	egg_warning ("unknown update kind: %s", kind);
	return ZIF_UPDATE_KIND_UNKNOWN;
}

/**
 * zif_update_state_to_string:
 **/
const gchar *
zif_update_state_to_string (ZifUpdateState state)
{
	if (state == ZIF_UPDATE_STATE_STABLE)
		return "stable";
	if (state == ZIF_UPDATE_STATE_TESTING)
		return "testing";
	return NULL;
}

/**
 * zif_update_kind_to_string:
 **/
const gchar *
zif_update_kind_to_string (ZifUpdateKind kind)
{
	if (kind == ZIF_UPDATE_KIND_BUGFIX)
		return "bugfix";
	if (kind == ZIF_UPDATE_KIND_SECURITY)
		return "security";
	if (kind == ZIF_UPDATE_KIND_ENHANCEMENT)
		return "enhancement";
	if (kind == ZIF_UPDATE_KIND_NEWPACKAGE)
		return "newpackage";
	return NULL;
}

/**
 * zif_update_get_state:
 * @update: the #ZifUpdate object
 *
 * Gets the update state.
 *
 * Return value: the state of update, e.g. %PK_UPDATE_STATE_ENUM_STABLE.
 *
 * Since: 0.1.0
 **/
ZifUpdateState
zif_update_get_state (ZifUpdate *update)
{
	g_return_val_if_fail (ZIF_IS_UPDATE (update), ZIF_UPDATE_STATE_UNKNOWN);
	return update->priv->state;
}

/**
 * zif_update_get_kind:
 * @update: the #ZifUpdate object
 *
 * Gets the update kind.
 *
 * Return value: the state of update, e.g. %PK_INFO_ENUM_SECURITY.
 *
 * Since: 0.1.0
 **/
ZifUpdateKind
zif_update_get_kind (ZifUpdate *update)
{
	g_return_val_if_fail (ZIF_IS_UPDATE (update), ZIF_UPDATE_KIND_UNKNOWN);
	return update->priv->state;
}

/**
 * zif_update_get_id:
 * @update: the #ZifUpdate object
 *
 * Gets the ID for this update.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_update_get_id (ZifUpdate *update)
{
	g_return_val_if_fail (ZIF_IS_UPDATE (update), NULL);
	return update->priv->id;
}

/**
 * zif_update_get_title:
 * @update: the #ZifUpdate object
 *
 * Gets the title for this update.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_update_get_title (ZifUpdate *update)
{
	g_return_val_if_fail (ZIF_IS_UPDATE (update), NULL);
	return update->priv->title;
}

/**
 * zif_update_get_description:
 * @update: the #ZifUpdate object
 *
 * Gets the description for this update.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_update_get_description (ZifUpdate *update)
{
	g_return_val_if_fail (ZIF_IS_UPDATE (update), NULL);
	return update->priv->description;
}

/**
 * zif_update_get_issued:
 * @update: the #ZifUpdate object
 *
 * Gets the time this update was issued.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.1.0
 **/
const gchar *
zif_update_get_issued (ZifUpdate *update)
{
	g_return_val_if_fail (ZIF_IS_UPDATE (update), NULL);
	return update->priv->issued;
}

/**
 * zif_update_get_reboot:
 * @update: the #ZifUpdate object
 *
 * Gets if the update requires a reboot.
 *
 * Return value: %TRUE for a reboot.
 *
 * Since: 0.1.0
 **/
gboolean
zif_update_get_reboot (ZifUpdate *update)
{
	g_return_val_if_fail (ZIF_IS_UPDATE (update), FALSE);
	return update->priv->reboot;
}

/**
 * zif_update_get_update_infos:
 * @update: the #ZifUpdate object
 *
 * Gets the update info for this update.
 *
 * Return value: A refcounted #GPtrArray of #ZifUpdateInfo, or %NULL. Free with g_ptr_array_unref().
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_update_get_update_infos (ZifUpdate *update)
{
	g_return_val_if_fail (ZIF_IS_UPDATE (update), NULL);
	return g_ptr_array_ref (update->priv->update_infos);
}

/**
 * zif_update_get_packages:
 * @update: the #ZifUpdate object
 *
 * Gets the packages for this update.
 *
 * Return value: A refcounted #GPtrArray of #ZifPackage, or %NULL. Free with g_ptr_array_unref().
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_update_get_packages (ZifUpdate *update)
{
	g_return_val_if_fail (ZIF_IS_UPDATE (update), NULL);
	return g_ptr_array_ref (update->priv->packages);
}

/**
 * zif_update_get_changelog:
 * @update: the #ZifUpdate object
 *
 * Gets the changelog for this update.
 *
 * Return value: A refcounted #GPtrArray of #ZifChangeset's, or %NULL. Free with g_ptr_array_unref().
 *
 * Since: 0.1.0
 **/
GPtrArray *
zif_update_get_changelog (ZifUpdate *update)
{
	g_return_val_if_fail (ZIF_IS_UPDATE (update), NULL);
	return g_ptr_array_ref (update->priv->changelog);
}

/**
 * zif_update_set_state:
 * @update: the #ZifUpdate object
 * @state: If the update is state
 *
 * Sets the update state status.
 *
 * Since: 0.1.0
 **/
void
zif_update_set_state (ZifUpdate *update, ZifUpdateState state)
{
	g_return_if_fail (ZIF_IS_UPDATE (update));
	update->priv->state = state;
}

/**
 * zif_update_set_kind:
 * @update: the #ZifUpdate object
 * @kind: the update kind, e.g. %PK_INFO_ENUM_SECURITY.
 *
 * Sets the kind of update.
 *
 * Since: 0.1.0
 **/
void
zif_update_set_kind (ZifUpdate *update, ZifUpdateKind kind)
{
	g_return_if_fail (ZIF_IS_UPDATE (update));
	update->priv->kind = kind;
}

/**
 * zif_update_set_id:
 * @update: the #ZifUpdate object
 * @id: the update ID
 *
 * Sets the update ID.
 *
 * Since: 0.1.0
 **/
void
zif_update_set_id (ZifUpdate *update, const gchar *id)
{
	g_return_if_fail (ZIF_IS_UPDATE (update));
	g_return_if_fail (id != NULL);
	g_return_if_fail (update->priv->id == NULL);

	update->priv->id = g_strdup (id);
}

/**
 * zif_update_set_title:
 * @update: the #ZifUpdate object
 * @title: the update title
 *
 * Sets the update title.
 *
 * Since: 0.1.0
 **/
void
zif_update_set_title (ZifUpdate *update, const gchar *title)
{
	g_return_if_fail (ZIF_IS_UPDATE (update));
	g_return_if_fail (title != NULL);
	g_return_if_fail (update->priv->title == NULL);

	update->priv->title = g_strdup (title);
}

/**
 * zif_update_set_description:
 * @update: the #ZifUpdate object
 * @description: the update description
 *
 * Sets the update description.
 *
 * Since: 0.1.0
 **/
void
zif_update_set_description (ZifUpdate *update, const gchar *description)
{
	g_return_if_fail (ZIF_IS_UPDATE (update));
	g_return_if_fail (description != NULL);
	g_return_if_fail (update->priv->description == NULL);

	update->priv->description = g_strdup (description);
}

/**
 * zif_update_set_issued:
 * @update: the #ZifUpdate object
 * @issued: the update issued time
 *
 * Sets the time the update was issued.
 *
 * Since: 0.1.0
 **/
void
zif_update_set_issued (ZifUpdate *update, const gchar *issued)
{
	g_return_if_fail (ZIF_IS_UPDATE (update));
	g_return_if_fail (issued != NULL);
	g_return_if_fail (update->priv->issued == NULL);

	update->priv->issued = g_strdup (issued);
}

/**
 * zif_update_set_reboot:
 * @update: the #ZifUpdate object
 * @reboot: if the update requires a reboot
 *
 * Sets the update reboot status
 *
 * Since: 0.1.0
 **/
void
zif_update_set_reboot (ZifUpdate *update, gboolean reboot)
{
	g_return_if_fail (ZIF_IS_UPDATE (update));

	update->priv->reboot = reboot;
}

/**
 * zif_update_add_update_info:
 * @update: the #ZifUpdate object
 * @update_info: the #ZifUpdateInfo
 *
 * Adds some update info to the update.
 *
 * Since: 0.1.0
 **/
void
zif_update_add_update_info (ZifUpdate *update, ZifUpdateInfo *update_info)
{
	g_return_if_fail (ZIF_IS_UPDATE (update));
	g_return_if_fail (update_info != NULL);
	g_ptr_array_add (update->priv->update_infos, g_object_ref (update_info));
}

/**
 * zif_update_add_package:
 * @update: the #ZifUpdate object
 * @package: the #ZifPackage
 *
 * Adds some update info to the update.
 *
 * Since: 0.1.0
 **/
void
zif_update_add_package (ZifUpdate *update, ZifPackage *package)
{
	g_return_if_fail (ZIF_IS_UPDATE (update));
	g_return_if_fail (package != NULL);
	g_ptr_array_add (update->priv->packages, g_object_ref (package));
}

/**
 * zif_update_add_changeset:
 * @update: the #ZifUpdate object
 * @changeset: the #ZifChangeset
 *
 * Adds a changeset to the update.
 *
 * Since: 0.1.0
 **/
void
zif_update_add_changeset (ZifUpdate *update, ZifChangeset *changeset)
{
	guint i;
	guint64 date;
	guint64 date_tmp;
	ZifChangeset *changeset_tmp;

	g_return_if_fail (ZIF_IS_UPDATE (update));
	g_return_if_fail (changeset != NULL);

	/* check this changeset hasn't already been added */
	date = zif_changeset_get_date (changeset);
	for (i=0; i < update->priv->changelog->len; i++) {
		changeset_tmp = g_ptr_array_index (update->priv->changelog, i);
		date_tmp = zif_changeset_get_date (changeset_tmp);
		if (date == date_tmp) {
			egg_warning ("Already added changeset %i to %s",
				     (int)date_tmp, zif_update_get_id (update));
			goto out;
		}
	}

	/* all okay */
	g_ptr_array_add (update->priv->changelog, g_object_ref (changeset));
out:
	return;
}

/**
 * zif_update_get_property:
 **/
static void
zif_update_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	ZifUpdate *update = ZIF_UPDATE (object);
	ZifUpdatePrivate *priv = update->priv;

	switch (prop_id) {
	case PROP_STATE:
		g_value_set_uint (value, priv->state);
		break;
	case PROP_KIND:
		g_value_set_uint (value, priv->kind);
		break;
	case PROP_ID:
		g_value_set_string (value, priv->id);
		break;
	case PROP_TITLE:
		g_value_set_string (value, priv->title);
		break;
	case PROP_DESCRIPTION:
		g_value_set_string (value, priv->description);
		break;
	case PROP_ISSUED:
		g_value_set_string (value, priv->issued);
		break;
	case PROP_REBOOT:
		g_value_set_boolean (value, priv->reboot);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * zif_update_set_property:
 **/
static void
zif_update_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
}

/**
 * zif_update_finalize:
 **/
static void
zif_update_finalize (GObject *object)
{
	ZifUpdate *update;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_UPDATE (object));
	update = ZIF_UPDATE (object);

	g_free (update->priv->id);
	g_free (update->priv->title);
	g_free (update->priv->description);
	g_free (update->priv->issued);
	g_ptr_array_unref (update->priv->update_infos);
	g_ptr_array_unref (update->priv->packages);
	g_ptr_array_unref (update->priv->changelog);

	G_OBJECT_CLASS (zif_update_parent_class)->finalize (object);
}

/**
 * zif_update_class_init:
 **/
static void
zif_update_class_init (ZifUpdateClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_update_finalize;
	object_class->get_property = zif_update_get_property;
	object_class->set_property = zif_update_set_property;

	/**
	 * ZifUpdate:state:
	 *
	 * Since: 0.1.0
	 */
	pspec = g_param_spec_uint ("state", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_STATE, pspec);

	/**
	 * ZifUpdate:kind:
	 *
	 * Since: 0.1.0
	 */
	pspec = g_param_spec_uint ("kind", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_KIND, pspec);

	/**
	 * ZifUpdate:id:
	 *
	 * Since: 0.1.0
	 */
	pspec = g_param_spec_string ("id", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_ID, pspec);

	/**
	 * ZifUpdate:title:
	 *
	 * Since: 0.1.0
	 */
	pspec = g_param_spec_string ("title", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_TITLE, pspec);

	/**
	 * ZifUpdate:description:
	 *
	 * Since: 0.1.0
	 */
	pspec = g_param_spec_string ("description", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_DESCRIPTION, pspec);

	/**
	 * ZifUpdate:issued:
	 *
	 * Since: 0.1.0
	 */
	pspec = g_param_spec_string ("issued", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_ISSUED, pspec);

	/**
	 * ZifUpdate:reboot:
	 *
	 * Since: 0.1.0
	 */
	pspec = g_param_spec_boolean ("reboot", NULL, NULL,
				      FALSE,
				      G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_REBOOT, pspec);

	g_type_class_add_private (klass, sizeof (ZifUpdatePrivate));
}

/**
 * zif_update_init:
 **/
static void
zif_update_init (ZifUpdate *update)
{
	update->priv = ZIF_UPDATE_GET_PRIVATE (update);
	update->priv->state = ZIF_UPDATE_STATE_UNKNOWN;
	update->priv->kind = ZIF_UPDATE_KIND_UNKNOWN;
	update->priv->id = NULL;
	update->priv->title = NULL;
	update->priv->description = NULL;
	update->priv->issued = NULL;
	update->priv->reboot = FALSE;
	update->priv->update_infos = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	update->priv->packages = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	update->priv->changelog = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

/**
 * zif_update_new:
 *
 * Return value: A new #ZifUpdate class instance.
 *
 * Since: 0.1.0
 **/
ZifUpdate *
zif_update_new (void)
{
	ZifUpdate *update;
	update = g_object_new (ZIF_TYPE_UPDATE, NULL);
	return ZIF_UPDATE (update);
}

