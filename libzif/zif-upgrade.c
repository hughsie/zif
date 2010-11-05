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
 * SECTION:zif-upgrade
 * @short_mirrorlist: Generic object to represent a distribution upgrade.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>

#include "zif-upgrade.h"

#define ZIF_UPGRADE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_UPGRADE, ZifUpgradePrivate))

struct _ZifUpgradePrivate
{
	gchar			*id;
	gboolean		 stable;
	gboolean		 enabled;
	guint			 version;
	gchar			*baseurl;
	gchar			*mirrorlist;
	gchar			*install_mirrorlist;
};

enum {
	PROP_0,
	PROP_ID,
	PROP_STABLE,
	PROP_ENABLED,
	PROP_VERSION,
	PROP_BASEURL,
	PROP_MIRRORLIST,
	PROP_INSTALL_MIRROR_LIST,
	PROP_LAST
};

G_DEFINE_TYPE (ZifUpgrade, zif_upgrade, G_TYPE_OBJECT)

/**
 * zif_upgrade_get_enabled:
 * @upgrade: the #ZifUpgrade object
 *
 * Gets if the upgrade is enabled.
 * A disabled upgrade may not be upgradable to.
 *
 * Return value: the enabled of upgrade, e.g. %PK_UPGRADE_ENABLED_ENUM_STABLE.
 *
 * Since: 0.1.3
 **/
gboolean
zif_upgrade_get_enabled (ZifUpgrade *upgrade)
{
	g_return_val_if_fail (ZIF_IS_UPGRADE (upgrade), FALSE);
	return upgrade->priv->enabled;
}

/**
 * zif_upgrade_get_version:
 * @upgrade: the #ZifUpgrade object
 *
 * Gets the upgrade version.
 *
 * Return value: the enabled of upgrade, or 0 for unset.
 *
 * Since: 0.1.3
 **/
guint
zif_upgrade_get_version (ZifUpgrade *upgrade)
{
	g_return_val_if_fail (ZIF_IS_UPGRADE (upgrade), 0);
	return upgrade->priv->enabled;
}

/**
 * zif_upgrade_get_id:
 * @upgrade: the #ZifUpgrade object
 *
 * Gets the ID for this upgrade.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.1.3
 **/
const gchar *
zif_upgrade_get_id (ZifUpgrade *upgrade)
{
	g_return_val_if_fail (ZIF_IS_UPGRADE (upgrade), NULL);
	return upgrade->priv->id;
}

/**
 * zif_upgrade_get_baseurl:
 * @upgrade: the #ZifUpgrade object
 *
 * Gets the baseurl for this upgrade.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.1.3
 **/
const gchar *
zif_upgrade_get_baseurl (ZifUpgrade *upgrade)
{
	g_return_val_if_fail (ZIF_IS_UPGRADE (upgrade), NULL);
	return upgrade->priv->baseurl;
}

/**
 * zif_upgrade_get_mirrorlist:
 * @upgrade: the #ZifUpgrade object
 *
 * Gets the mirrorlist for this upgrade.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.1.3
 **/
const gchar *
zif_upgrade_get_mirrorlist (ZifUpgrade *upgrade)
{
	g_return_val_if_fail (ZIF_IS_UPGRADE (upgrade), NULL);
	return upgrade->priv->mirrorlist;
}

/**
 * zif_upgrade_get_install_mirrorlist:
 * @upgrade: the #ZifUpgrade object
 *
 * Gets the install mirrorlist.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.1.3
 **/
const gchar *
zif_upgrade_get_install_mirrorlist (ZifUpgrade *upgrade)
{
	g_return_val_if_fail (ZIF_IS_UPGRADE (upgrade), NULL);
	return upgrade->priv->install_mirrorlist;
}

/**
 * zif_upgrade_get_stable:
 * @upgrade: the #ZifUpgrade object
 *
 * Gets if the upgrade is a stable.
 *
 * Return value: %TRUE for a stable.
 *
 * Since: 0.1.3
 **/
gboolean
zif_upgrade_get_stable (ZifUpgrade *upgrade)
{
	g_return_val_if_fail (ZIF_IS_UPGRADE (upgrade), FALSE);
	return upgrade->priv->stable;
}

/**
 * zif_upgrade_set_enabled:
 * @upgrade: the #ZifUpgrade object
 * @enabled: If the upgrade is enabled
 *
 * Sets the upgrade enabled status.
 *
 * Since: 0.1.3
 **/
void
zif_upgrade_set_enabled (ZifUpgrade *upgrade, gboolean enabled)
{
	g_return_if_fail (ZIF_IS_UPGRADE (upgrade));
	upgrade->priv->enabled = enabled;
}

/**
 * zif_upgrade_set_version:
 * @upgrade: the #ZifUpgrade object
 * @version: the upgrade version, e.g. 15
 *
 * Sets the version of upgrade.
 *
 * Since: 0.1.3
 **/
void
zif_upgrade_set_version (ZifUpgrade *upgrade, guint version)
{
	g_return_if_fail (ZIF_IS_UPGRADE (upgrade));
	upgrade->priv->version = version;
}

/**
 * zif_upgrade_set_id:
 * @upgrade: the #ZifUpgrade object
 * @id: the upgrade ID
 *
 * Sets the upgrade ID.
 *
 * Since: 0.1.3
 **/
void
zif_upgrade_set_id (ZifUpgrade *upgrade, const gchar *id)
{
	g_return_if_fail (ZIF_IS_UPGRADE (upgrade));
	g_return_if_fail (id != NULL);
	g_return_if_fail (upgrade->priv->id == NULL);

	upgrade->priv->id = g_strdup (id);
}

/**
 * zif_upgrade_set_baseurl:
 * @upgrade: the #ZifUpgrade object
 * @baseurl: the upgrade baseurl
 *
 * Sets the upgrade baseurl.
 *
 * Since: 0.1.3
 **/
void
zif_upgrade_set_baseurl (ZifUpgrade *upgrade, const gchar *baseurl)
{
	g_return_if_fail (ZIF_IS_UPGRADE (upgrade));
	g_return_if_fail (baseurl != NULL);
	g_return_if_fail (upgrade->priv->baseurl == NULL);

	upgrade->priv->baseurl = g_strdup (baseurl);
}

/**
 * zif_upgrade_set_mirrorlist:
 * @upgrade: the #ZifUpgrade object
 * @mirrorlist: the upgrade mirrorlist
 *
 * Sets the upgrade mirrorlist.
 *
 * Since: 0.1.3
 **/
void
zif_upgrade_set_mirrorlist (ZifUpgrade *upgrade, const gchar *mirrorlist)
{
	g_return_if_fail (ZIF_IS_UPGRADE (upgrade));
	g_return_if_fail (mirrorlist != NULL);
	g_return_if_fail (upgrade->priv->mirrorlist == NULL);

	upgrade->priv->mirrorlist = g_strdup (mirrorlist);
}

/**
 * zif_upgrade_set_install_mirrorlist:
 * @upgrade: the #ZifUpgrade object
 * @install_mirrorlist: the upgrade install_mirrorlist time
 *
 * Sets the time the upgrade install mirrorlist.
 *
 * Since: 0.1.3
 **/
void
zif_upgrade_set_install_mirrorlist (ZifUpgrade *upgrade, const gchar *install_mirrorlist)
{
	g_return_if_fail (ZIF_IS_UPGRADE (upgrade));
	g_return_if_fail (install_mirrorlist != NULL);
	g_return_if_fail (upgrade->priv->install_mirrorlist == NULL);

	upgrade->priv->install_mirrorlist = g_strdup (install_mirrorlist);
}

/**
 * zif_upgrade_set_stable:
 * @upgrade: the #ZifUpgrade object
 * @stable: if the upgrade is stable
 *
 * Sets if the upgrade is stable and suitable for end users.
 *
 * Since: 0.1.3
 **/
void
zif_upgrade_set_stable (ZifUpgrade *upgrade, gboolean stable)
{
	g_return_if_fail (ZIF_IS_UPGRADE (upgrade));
	upgrade->priv->stable = stable;
}

/**
 * zif_upgrade_get_property:
 **/
static void
zif_upgrade_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	ZifUpgrade *upgrade = ZIF_UPGRADE (object);
	ZifUpgradePrivate *priv = upgrade->priv;

	switch (prop_id) {
	case PROP_ENABLED:
		g_value_set_uint (value, priv->enabled);
		break;
	case PROP_VERSION:
		g_value_set_uint (value, priv->version);
		break;
	case PROP_ID:
		g_value_set_string (value, priv->id);
		break;
	case PROP_BASEURL:
		g_value_set_string (value, priv->baseurl);
		break;
	case PROP_MIRRORLIST:
		g_value_set_string (value, priv->mirrorlist);
		break;
	case PROP_INSTALL_MIRROR_LIST:
		g_value_set_string (value, priv->install_mirrorlist);
		break;
	case PROP_STABLE:
		g_value_set_boolean (value, priv->stable);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * zif_upgrade_set_property:
 **/
static void
zif_upgrade_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
}

/**
 * zif_upgrade_finalize:
 **/
static void
zif_upgrade_finalize (GObject *object)
{
	ZifUpgrade *upgrade;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_UPGRADE (object));
	upgrade = ZIF_UPGRADE (object);

	g_free (upgrade->priv->id);
	g_free (upgrade->priv->baseurl);
	g_free (upgrade->priv->mirrorlist);
	g_free (upgrade->priv->install_mirrorlist);

	G_OBJECT_CLASS (zif_upgrade_parent_class)->finalize (object);
}

/**
 * zif_upgrade_class_init:
 **/
static void
zif_upgrade_class_init (ZifUpgradeClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_upgrade_finalize;
	object_class->get_property = zif_upgrade_get_property;
	object_class->set_property = zif_upgrade_set_property;

	/**
	 * ZifUpgrade:enabled:
	 *
	 * Since: 0.1.3
	 */
	pspec = g_param_spec_boolean ("enabled", NULL, NULL,
				      FALSE,
				      G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_ENABLED, pspec);

	/**
	 * ZifUpgrade:version:
	 *
	 * Since: 0.1.3
	 */
	pspec = g_param_spec_uint ("version", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_VERSION, pspec);

	/**
	 * ZifUpgrade:id:
	 *
	 * Since: 0.1.3
	 */
	pspec = g_param_spec_string ("id", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_ID, pspec);

	/**
	 * ZifUpgrade:baseurl:
	 *
	 * Since: 0.1.3
	 */
	pspec = g_param_spec_string ("baseurl", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_BASEURL, pspec);

	/**
	 * ZifUpgrade:mirrorlist:
	 *
	 * Since: 0.1.3
	 */
	pspec = g_param_spec_string ("mirrorlist", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_MIRRORLIST, pspec);

	/**
	 * ZifUpgrade:install-mirrorlist:
	 *
	 * Since: 0.1.3
	 */
	pspec = g_param_spec_string ("install-mirrorlist", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_INSTALL_MIRROR_LIST, pspec);

	/**
	 * ZifUpgrade:stable:
	 *
	 * Since: 0.1.3
	 */
	pspec = g_param_spec_boolean ("stable", NULL, NULL,
				      FALSE,
				      G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_STABLE, pspec);

	g_type_class_add_private (klass, sizeof (ZifUpgradePrivate));
}

/**
 * zif_upgrade_init:
 **/
static void
zif_upgrade_init (ZifUpgrade *upgrade)
{
	upgrade->priv = ZIF_UPGRADE_GET_PRIVATE (upgrade);
}

/**
 * zif_upgrade_new:
 *
 * Return value: A new #ZifUpgrade class instance.
 *
 * Since: 0.1.3
 **/
ZifUpgrade *
zif_upgrade_new (void)
{
	ZifUpgrade *upgrade;
	upgrade = g_object_new (ZIF_TYPE_UPGRADE, NULL);
	return ZIF_UPGRADE (upgrade);
}

