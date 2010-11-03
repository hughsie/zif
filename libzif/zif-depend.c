/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2010 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-depend
 * @short_version: Generic object to represent some information about an
 * package encoded dependency.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

#include "zif-depend.h"

#define ZIF_DEPEND_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_DEPEND, ZifDependPrivate))

struct _ZifDependPrivate
{
	gchar			*name;
	ZifDependFlag		 flag;
	gchar			*version;
};

enum {
	PROP_0,
	PROP_FLAG,
	PROP_NAME,
	PROP_VERSION,
	PROP_LAST
};

G_DEFINE_TYPE (ZifDepend, zif_depend, G_TYPE_OBJECT)

/**
 * zif_depend_flag_to_string:
 * @flag: the #ZifDependFlag
 *
 * Returns a string representation of the #ZifDependFlag.
 *
 * Return value: string value
 *
 * Since: 0.1.0
 **/
const gchar *
zif_depend_flag_to_string (ZifDependFlag flag)
{
	if (flag == ZIF_DEPEND_FLAG_ANY)
		return "~";
	if (flag == ZIF_DEPEND_FLAG_LESS)
		return "<";
	if (flag == ZIF_DEPEND_FLAG_GREATER)
		return ">";
	if (flag == ZIF_DEPEND_FLAG_EQUAL)
		return "=";
	return "unknown";
}

/**
 * zif_depend_to_string:
 * @flag: a valid #ZifDependFlag object
 *
 * Returns a string representation of the #ZifDepend object.
 *
 * Return value: string value
 *
 * Since: 0.1.0
 **/
gchar *
zif_depend_to_string (ZifDepend *depend)
{
	g_return_val_if_fail (ZIF_IS_DEPEND (depend), NULL);
	if (depend->priv->version == NULL)
		return g_strdup (depend->priv->name);
	return g_strdup_printf ("%s %s %s",
				depend->priv->name,
				zif_depend_flag_to_string (depend->priv->flag),
				depend->priv->version);
}

/**
 * zif_depend_get_flag:
 * @depend: the #ZifDepend object
 *
 * Gets the depend flag.
 *
 * Return value: the flag of depend, e.g. %ZIF_DEPEND_FLAG_LESS.
 *
 * Since: 0.1.3
 **/
ZifDependFlag
zif_depend_get_flag (ZifDepend *depend)
{
	g_return_val_if_fail (ZIF_IS_DEPEND (depend), ZIF_DEPEND_FLAG_UNKNOWN);
	return depend->priv->flag;
}

/**
 * zif_depend_get_name:
 * @depend: the #ZifDepend object
 *
 * Gets the name for this depend.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.1.3
 **/
const gchar *
zif_depend_get_name (ZifDepend *depend)
{
	g_return_val_if_fail (ZIF_IS_DEPEND (depend), NULL);
	return depend->priv->name;
}

/**
 * zif_depend_get_version:
 * @depend: the #ZifDepend object
 *
 * Gets the version for this depend.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.1.3
 **/
const gchar *
zif_depend_get_version (ZifDepend *depend)
{
	g_return_val_if_fail (ZIF_IS_DEPEND (depend), NULL);
	return depend->priv->version;
}

/**
 * zif_depend_set_flag:
 * @depend: the #ZifDepend object
 * @flag: If the depend is flag
 *
 * Sets the depend flag status.
 *
 * Since: 0.1.3
 **/
void
zif_depend_set_flag (ZifDepend *depend, ZifDependFlag flag)
{
	g_return_if_fail (ZIF_IS_DEPEND (depend));
	depend->priv->flag = flag;
}

/**
 * zif_depend_set_name:
 * @depend: the #ZifDepend object
 * @name: the depend name
 *
 * Sets the depend name.
 *
 * Since: 0.1.3
 **/
void
zif_depend_set_name (ZifDepend *depend, const gchar *name)
{
	g_return_if_fail (ZIF_IS_DEPEND (depend));
	g_return_if_fail (name != NULL);
	g_return_if_fail (depend->priv->name == NULL);

	depend->priv->name = g_strdup (name);
}

/**
 * zif_depend_set_version:
 * @depend: the #ZifDepend object
 * @version: the depend version
 *
 * Sets the depend version.
 *
 * Since: 0.1.3
 **/
void
zif_depend_set_version (ZifDepend *depend, const gchar *version)
{
	g_return_if_fail (ZIF_IS_DEPEND (depend));
	g_return_if_fail (version != NULL);
	g_return_if_fail (depend->priv->version == NULL);

	depend->priv->version = g_strdup (version);
}

/**
 * zif_depend_get_property:
 **/
static void
zif_depend_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	ZifDepend *depend = ZIF_DEPEND (object);
	ZifDependPrivate *priv = depend->priv;

	switch (prop_id) {
	case PROP_FLAG:
		g_value_set_uint (value, priv->flag);
		break;
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	case PROP_VERSION:
		g_value_set_string (value, priv->version);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * zif_depend_set_property:
 **/
static void
zif_depend_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
}

/**
 * zif_depend_finalize:
 **/
static void
zif_depend_finalize (GObject *object)
{
	ZifDepend *depend;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_DEPEND (object));
	depend = ZIF_DEPEND (object);

	g_free (depend->priv->name);
	g_free (depend->priv->version);

	G_OBJECT_CLASS (zif_depend_parent_class)->finalize (object);
}

/**
 * zif_depend_class_init:
 **/
static void
zif_depend_class_init (ZifDependClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_depend_finalize;
	object_class->get_property = zif_depend_get_property;
	object_class->set_property = zif_depend_set_property;

	/**
	 * ZifDepend:flag:
	 *
	 * Since: 0.1.3
	 */
	pspec = g_param_spec_uint ("flag", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_FLAG, pspec);

	/**
	 * ZifDepend:name:
	 *
	 * Since: 0.1.3
	 */
	pspec = g_param_spec_string ("name", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_NAME, pspec);

	/**
	 * ZifDepend:version:
	 *
	 * Since: 0.1.3
	 */
	pspec = g_param_spec_string ("version", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_VERSION, pspec);

	g_type_class_add_private (klass, sizeof (ZifDependPrivate));
}

/**
 * zif_depend_init:
 **/
static void
zif_depend_init (ZifDepend *depend)
{
	depend->priv = ZIF_DEPEND_GET_PRIVATE (depend);
	depend->priv->flag = ZIF_DEPEND_FLAG_UNKNOWN;
	depend->priv->name = NULL;
	depend->priv->version = NULL;
}

/**
 * zif_depend_new:
 *
 * Return value: A new #ZifDepend class instance.
 *
 * Since: 0.1.3
 **/
ZifDepend *
zif_depend_new (void)
{
	ZifDepend *depend;
	depend = g_object_new (ZIF_TYPE_DEPEND, NULL);
	return ZIF_DEPEND (depend);
}

