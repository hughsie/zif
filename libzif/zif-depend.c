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
 * @short_description: A package dependency
 *
 * An object to represent some information about an
 * encoded dependency.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

#include "zif-depend-private.h"
#include "zif-utils.h"
#include "zif-string.h"

#define ZIF_DEPEND_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_DEPEND, ZifDependPrivate))

struct _ZifDependPrivate
{
	ZifString		*name;
	ZifDependFlag		 flag;
	ZifString		*version;
	gchar			*description;
	gboolean		 description_ok;
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
 * zif_depend_compare:
 * @a: A #ZifDepend
 * @b: A #ZifDepend to compare
 *
 * Compares one dependancy against another.
 * This is basically a zif_compare_evr() on the versions.
 *
 * Return value: 1 for a>b, 0 for a==b, -1 for b>a, or G_MAXINT for error
 *
 * Since: 0.1.3
 **/
gint
zif_depend_compare (ZifDepend *a, ZifDepend *b)
{
	g_return_val_if_fail (a != NULL, G_MAXINT);
	g_return_val_if_fail (b != NULL, G_MAXINT);

	/* fall back to comparing the evr */
	return zif_compare_evr (zif_depend_get_version (a),
				zif_depend_get_version (b));
}

/**
 * zif_depend_satisfies:
 * @got: The #ZifDepend we've got
 * @need: The #ZifDepend we need
 *
 * Returns if the dependency will be satisfied with what we've got.
 *
 * Return value: %TRUE if okay, %FALSE otherwise
 *
 * Since: 0.1.3
 **/
gboolean
zif_depend_satisfies (ZifDepend *got, ZifDepend *need)
{
	gboolean ret = FALSE;
	ZifDependFlag flag_got;
	ZifDependFlag flag_need;
	const gchar *name_got = zif_string_get_value (got->priv->name);
	const gchar *name_need = zif_string_get_value (need->priv->name);
	const gchar *version_got;
	const gchar *version_need;

	/* check the first character rather than setting up the SSE2
	 * version of strcmp which is slow to tear down */
	if (name_got[0] != name_need[0])
		goto out;

	/* name does not match */
	ret = (g_strcmp0 (name_got, name_need) == 0);
	if (!ret)
		goto out;

	/* get cached values of the flags */
	flag_got = got->priv->flag;
	flag_need = need->priv->flag;
	g_return_val_if_fail (flag_got != ZIF_DEPEND_FLAG_UNKNOWN, FALSE);
	g_return_val_if_fail (flag_need != ZIF_DEPEND_FLAG_UNKNOWN, FALSE);

	/* 'Requires: hal' or 'Obsoletes: hal' - not any particular version */
	if (flag_need == ZIF_DEPEND_FLAG_ANY ||
	    flag_got == ZIF_DEPEND_FLAG_ANY) {
		ret = TRUE;
		goto out;
	}

	/* get cached values of the versions */
	version_got = zif_depend_get_version (got);
	version_need = zif_depend_get_version (need);

	/* 'Requires: hal = 0.5.8' - both equal */
	if (flag_got == ZIF_DEPEND_FLAG_EQUAL &&
	    flag_need == ZIF_DEPEND_FLAG_EQUAL) {
		ret = (zif_compare_evr (version_got, version_need) == 0);
		goto out;
	}

	/* 'Requires: hal > 0.5.7' - greater */
	if (flag_need == ZIF_DEPEND_FLAG_GREATER) {
		ret = (zif_compare_evr (version_got, version_need) > 0);
		goto out;
	}

	/* 'Requires: hal < 0.5.7' - less */
	if (flag_need == ZIF_DEPEND_FLAG_LESS) {
		ret = (zif_compare_evr (version_got, version_need) < 0);
		goto out;
	}

	/* 'Requires: hal >= 0.5.7' - greater */
	if (flag_need == (ZIF_DEPEND_FLAG_GREATER | ZIF_DEPEND_FLAG_EQUAL)) {
		ret = (zif_compare_evr (version_got, version_need) >= 0);
		goto out;
	}

	/* 'Requires: hal <= 0.5.7' - less */
	if (flag_need == (ZIF_DEPEND_FLAG_LESS | ZIF_DEPEND_FLAG_EQUAL)) {
		ret = (zif_compare_evr (version_got, version_need) <= 0);
		goto out;
	}

	/* got: bash >= 0.2.0, need: bash = 0.3.0' - only valid when versions are equal */
	if (flag_got == (ZIF_DEPEND_FLAG_GREATER | ZIF_DEPEND_FLAG_EQUAL) &&
	    flag_need == ZIF_DEPEND_FLAG_EQUAL) {
		ret = (zif_compare_evr (version_got, version_need) <= 0);
		goto out;
	}

	/* got: bash >= 0.2.0, need: bash = 0.3.0' - only valid when versions are equal */
	if (flag_got == (ZIF_DEPEND_FLAG_LESS | ZIF_DEPEND_FLAG_EQUAL) &&
	    flag_need == ZIF_DEPEND_FLAG_EQUAL) {
		ret = (zif_compare_evr (version_got, version_need) >= 0);
		goto out;
	}

	/* got: bash < 0.2.0, need: bash = 0.3.0' - never valid */
	if (flag_got == ZIF_DEPEND_FLAG_LESS &&
	    flag_need == ZIF_DEPEND_FLAG_EQUAL) {
		ret = FALSE;
		goto out;
	}

	/* got: bash > 0.2.0, need: bash = 0.3.0' - never valid */
	if (flag_got == ZIF_DEPEND_FLAG_GREATER &&
	    flag_need == ZIF_DEPEND_FLAG_EQUAL) {
		ret = FALSE;
		goto out;
	}

	/* not sure */
	ret = FALSE;
	g_warning ("not sure how to compare %s and %s for %s:%s",
		   zif_depend_flag_to_string (flag_got),
		   zif_depend_flag_to_string (flag_need),
		   zif_depend_get_description (got),
		   zif_depend_get_description (need));
out:
	return ret;
}

/**
 * zif_depend_flag_to_string:
 * @flag: A #ZifDependFlag
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
	if (flag == (ZIF_DEPEND_FLAG_LESS | ZIF_DEPEND_FLAG_EQUAL))
		return "<=";
	if (flag == (ZIF_DEPEND_FLAG_GREATER | ZIF_DEPEND_FLAG_EQUAL))
		return ">=";
	return "???";
}

/**
 * zif_depend_to_string:
 * @depend: A #ZifDepend
 *
 * Returns a string representation of the #ZifDepend object.
 *
 * Note: this function is deprecated, use zif_depend_get_description()
 * instead as it is more efficient.
 *
 * Return value: string value, free with g_free()
 *
 * Since: 0.1.0
 **/
gchar *
zif_depend_to_string (ZifDepend *depend)
{
	g_return_val_if_fail (ZIF_IS_DEPEND (depend), NULL);
	if (depend->priv->version == NULL)
		return g_strdup (zif_string_get_value (depend->priv->name));
	return g_strdup_printf ("%s %s %s",
				zif_string_get_value (depend->priv->name),
				zif_depend_flag_to_string (depend->priv->flag),
				zif_string_get_value (depend->priv->version));
}

/**
 * zif_depend_get_description:
 * @depend: A #ZifDepend
 *
 * Returns a string representation of the #ZifDepend object.
 *
 * Return value: string value
 *
 * Since: 0.1.3
 **/
const gchar *
zif_depend_get_description (ZifDepend *depend)
{
	g_return_val_if_fail (depend != NULL, NULL);
	g_return_val_if_fail (depend->priv->name != NULL, NULL);
	g_return_val_if_fail (depend->priv->flag != ZIF_DEPEND_FLAG_UNKNOWN, NULL);

	/* does not exist, or not valid */
	if (!depend->priv->description_ok) {
		g_free (depend->priv->description);
		depend->priv->description = g_strdup_printf ("[%s %s %s]",
							     zif_string_get_value (depend->priv->name),
							     zif_depend_flag_to_string (depend->priv->flag),
							     depend->priv->version ? zif_string_get_value (depend->priv->version) : "");
		depend->priv->description_ok = TRUE;
	}
	return depend->priv->description;
}

/**
 * zif_depend_get_flag:
 * @depend: A #ZifDepend
 *
 * Gets the depend flag.
 *
 * Return value: A flag of depend, e.g. %ZIF_DEPEND_FLAG_LESS.
 *
 * Since: 0.1.3
 **/
ZifDependFlag
zif_depend_get_flag (ZifDepend *depend)
{
	g_return_val_if_fail (depend != NULL, ZIF_DEPEND_FLAG_UNKNOWN);
	return depend->priv->flag;
}

/**
 * zif_depend_get_name:
 * @depend: A #ZifDepend
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
	g_return_val_if_fail (depend != NULL, NULL);
	if (depend->priv->name == NULL)
		return NULL;
	return zif_string_get_value (depend->priv->name);
}

/**
 * zif_depend_get_version:
 * @depend: A #ZifDepend
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
	g_return_val_if_fail (depend != NULL, NULL);
	if (depend->priv->version == NULL)
		return NULL;
	return zif_string_get_value (depend->priv->version);
}

/**
 * zif_depend_set_flag:
 * @depend: A #ZifDepend
 * @flag: If the depend is flag
 *
 * Sets the depend flag status.
 *
 * Since: 0.1.3
 **/
void
zif_depend_set_flag (ZifDepend *depend, ZifDependFlag flag)
{
	g_return_if_fail (depend != NULL);
	g_return_if_fail (flag != ZIF_DEPEND_FLAG_UNKNOWN);
	depend->priv->flag = flag;
	depend->priv->description_ok = FALSE;
}

/**
 * zif_depend_set_name:
 * @depend: A #ZifDepend
 * @name: The depend name
 *
 * Sets the depend name.
 *
 * Since: 0.1.3
 **/
void
zif_depend_set_name (ZifDepend *depend, const gchar *name)
{
	g_return_if_fail (depend != NULL);
	g_return_if_fail (name != NULL);
	g_return_if_fail (depend->priv->name == NULL);

	depend->priv->name = zif_string_new (name);
	depend->priv->description_ok = FALSE;
}

/**
 * zif_depend_set_name_str:
 * @depend: A #ZifDepend
 * @name: The depend name
 *
 * Sets the depend name.
 *
 * Since: 0.1.3
 **/
void
zif_depend_set_name_str (ZifDepend *depend, ZifString *name)
{
	g_return_if_fail (depend != NULL);
	g_return_if_fail (name != NULL);
	g_return_if_fail (depend->priv->name == NULL);

	depend->priv->name = zif_string_ref (name);
	depend->priv->description_ok = FALSE;
}

/**
 * zif_depend_set_version:
 * @depend: A #ZifDepend
 * @version: The depend version
 *
 * Sets the depend version.
 *
 * Since: 0.1.3
 **/
void
zif_depend_set_version (ZifDepend *depend, const gchar *version)
{
	g_return_if_fail (depend != NULL);
	g_return_if_fail (depend->priv->version == NULL);

	if (version != NULL)
		depend->priv->version = zif_string_new (version);
	depend->priv->description_ok = FALSE;
}

/**
 * zif_depend_set_version_str:
 * @depend: A #ZifDepend
 * @version: The depend version
 *
 * Sets the depend version.
 *
 * Since: 0.1.3
 **/
void
zif_depend_set_version_str (ZifDepend *depend, ZifString *version)
{
	g_return_if_fail (depend != NULL);
	g_return_if_fail (version != NULL);
	g_return_if_fail (depend->priv->version == NULL);

	depend->priv->version = zif_string_ref (version);
	depend->priv->description_ok = FALSE;
}

/**
 * zif_depend_string_to_flag:
 **/
static ZifDependFlag
zif_depend_string_to_flag (const gchar *value)
{
	if (g_strcmp0 (value, "~") == 0)
		return ZIF_DEPEND_FLAG_ANY;
	if (g_strcmp0 (value, "<") == 0)
		return ZIF_DEPEND_FLAG_LESS;
	if (g_strcmp0 (value, ">") == 0)
		return ZIF_DEPEND_FLAG_GREATER;
	if (g_strcmp0 (value, "=") == 0 ||
	    g_strcmp0 (value, "==") == 0)
		return ZIF_DEPEND_FLAG_EQUAL;
	if (g_strcmp0 (value, ">=") == 0)
		return ZIF_DEPEND_FLAG_GREATER |
		       ZIF_DEPEND_FLAG_EQUAL;
	if (g_strcmp0 (value, "<=") == 0)
		return ZIF_DEPEND_FLAG_LESS |
		       ZIF_DEPEND_FLAG_EQUAL;
	return ZIF_DEPEND_FLAG_UNKNOWN;
}

/**
 * zif_depend_parse_description:
 * @depend: A #ZifDepend
 * @value: Depend string, e.g. "obsolete-package < 1.0.0"
 * @error: A #GError, or %NULL
 *
 * Parses a depend string and sets internal state.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.1.3
 **/
gboolean
zif_depend_parse_description (ZifDepend *depend, const gchar *value, GError **error)
{
	gchar **split = NULL;
	gboolean ret = TRUE;
	ZifDependFlag flag;

	g_return_val_if_fail (ZIF_IS_DEPEND (depend), FALSE);
	g_return_val_if_fail (value != FALSE, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* cut up */
	split = g_strsplit (value, " ", -1);

	/* just the name */
	if (g_strv_length (split) == 1) {
		zif_depend_set_flag (depend, ZIF_DEPEND_FLAG_ANY);
		zif_depend_set_name (depend, split[0]);
		goto out;
	}

	/* three sections to parse */
	if (g_strv_length (split) == 3) {
		flag = zif_depend_string_to_flag (split[1]);
		if (flag == ZIF_DEPEND_FLAG_UNKNOWN) {
			ret = FALSE;
			g_set_error (error, 1, 0,
				     "failed to parse depend flag '%s' from %s",
				     split[1], value);
			goto out;
		}
		zif_depend_set_name (depend, split[0]);
		zif_depend_set_flag (depend, flag);
		zif_depend_set_version (depend, split[2]);
		goto out;
	}

	/* failed */
	ret = FALSE;
	g_set_error (error, 1, 0, "failed to parse '%s' as ZifDepend", value);
out:
	g_strfreev (split);
	return ret;
}

/**
 * zif_depend_flag_desc_to_flag:
 **/
static ZifDependFlag
zif_depend_flag_desc_to_flag (const gchar *flags)
{
	if (flags == NULL)
		return ZIF_DEPEND_FLAG_ANY;
	if (g_strcmp0 (flags, "EQ") == 0)
		return ZIF_DEPEND_FLAG_EQUAL;
	if (g_strcmp0 (flags, "LT") == 0)
		return ZIF_DEPEND_FLAG_LESS;
	if (g_strcmp0 (flags, "GT") == 0)
		return ZIF_DEPEND_FLAG_GREATER;
	if (g_strcmp0 (flags, "LE") == 0)
		return ZIF_DEPEND_FLAG_LESS | ZIF_DEPEND_FLAG_EQUAL;
	if (g_strcmp0 (flags, "GE") == 0)
		return ZIF_DEPEND_FLAG_GREATER | ZIF_DEPEND_FLAG_EQUAL;
	g_warning ("unknown flag string %s", flags);
	return ZIF_DEPEND_FLAG_UNKNOWN;
}

/**
 * zif_depend_new_from_data_full:
 * @keys: Data keys, that do not have to be NULL terminated
 * @values: Data values, that do not have to be NULL terminated
 * @len: the number of values in the array
 *
 * Return value: A new #ZifDepend instance, or %NULL for error
 *
 * Since: 0.2.5
 **/
ZifDepend *
zif_depend_new_from_data_full (const gchar **keys,
			       const gchar **values,
			       guint len)
{
	guint i;
	GString *version_tmp;
	const gchar *name = NULL;
	const gchar *version = NULL;
	ZifDepend *depend = NULL;
	ZifDependFlag flag = ZIF_DEPEND_FLAG_ANY;

	g_assert (keys != NULL);
	g_assert (values != NULL);

	/* parse the data */
	version_tmp = g_string_new ("");
	for (i = 0; i < len; i++) {
		if (g_strcmp0 (keys[i], "name") == 0) {
			name = values[i];
		} else if (g_strcmp0 (keys[i], "epoch") == 0) {
			/* only add epoch if not zero */
			if (values[i] != NULL &&
			    g_strcmp0 (values[i], "0") != 0) {
				g_string_append (version_tmp, values[i]);
			}
		} else if (g_strcmp0 (keys[i], "ver") == 0 ||
			   g_strcmp0 (keys[i], "version") == 0) {
			/* only add version if not NULL */
			if (values[i] != NULL) {
				if (version_tmp->len > 0)
					g_string_append (version_tmp, ":");
				g_string_append (version_tmp, values[i]);
			}
		} else if (g_strcmp0 (keys[i], "rel") == 0 ||
			   g_strcmp0 (keys[i], "release") == 0) {
			/* only add release if not NULL */
			if (values[i] != NULL) {
				if (version_tmp->len > 0)
					g_string_append (version_tmp, "-");
				g_string_append (version_tmp, values[i]);
			}
		} else if (g_strcmp0 (keys[i], "flags") == 0) {
			flag = zif_depend_flag_desc_to_flag (values[i]);
		} else if (g_strcmp0 (keys[i], "pre") == 0) {
			/* no idea what this is for */;
		} else {
			g_warning ("unrecognized: %s=%s",
				   keys[i], values[i]);
		}
	}

	/* don't set an empty string */
	if (version_tmp->len > 0)
		version = version_tmp->str;

	/* create the new object */
	depend = g_object_new (ZIF_TYPE_DEPEND,
			       "name", name,
			       "flag", flag,
			       "version", version,
			       NULL);
	g_string_free (version_tmp, TRUE);
	return depend;
}

/**
 * zif_depend_new_from_data:
 * @keys: Data keys, that have to be NULL terminated
 * @values: Data values, that have to be NULL terminated
 *
 * Return value: A new #ZifDepend instance, or %NULL for error
 *
 * Since: 0.2.1
 **/
ZifDepend *
zif_depend_new_from_data (const gchar **keys,
			  const gchar **values)
{
	guint len_keys;
	guint len_values;
	ZifDepend *depend = NULL;

	/* check these match */
	len_keys = g_strv_length ((gchar **) keys);
	len_values = g_strv_length ((gchar **) values);
	if (len_keys != len_values) {
		g_critical ("cannot agree on depend data length, "
			    "perhaps you should be using "
			    "zif_depend_new_from_data_full() instead?");
		goto out;
	}

	/* parse */
	depend = zif_depend_new_from_data_full (keys, values, len_keys);
out:
	return depend;
}

/**
 * zif_depend_new_from_values:
 * @name: The depend name
 * @flag: The depend flag
 * @version: The depend version
 *
 * Return value: A new #ZifDepend instance, or %NULL for error
 *
 * Since: 0.2.4
 **/
ZifDepend *
zif_depend_new_from_values (const gchar *name,
			    ZifDependFlag flag,
			    const gchar *version)
{
	ZifDepend *depend;

	g_assert (name != NULL);
	g_assert (version != NULL);

	/* create the new object */
	depend = g_object_new (ZIF_TYPE_DEPEND,
			       "name", name,
			       "flag", flag,
			       "version", version,
			       NULL);
	return depend;
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
		g_value_set_string (value, zif_string_get_value (priv->name));
		break;
	case PROP_VERSION:
		g_value_set_string (value, zif_string_get_value (priv->version));
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
	ZifDepend *depend = ZIF_DEPEND (object);

	switch (prop_id) {
	case PROP_FLAG:
		zif_depend_set_flag (depend, g_value_get_uint (value));
		break;
	case PROP_NAME:
		zif_depend_set_name (depend, g_value_get_string (value));
		break;
	case PROP_VERSION:
		zif_depend_set_version (depend, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
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

	if (depend->priv->name != NULL)
		zif_string_unref (depend->priv->name);
	if (depend->priv->version != NULL)
		zif_string_unref (depend->priv->version);
	g_free (depend->priv->description);

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
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_FLAG, pspec);

	/**
	 * ZifDepend:name:
	 *
	 * Since: 0.1.3
	 */
	pspec = g_param_spec_string ("name", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_NAME, pspec);

	/**
	 * ZifDepend:version:
	 *
	 * Since: 0.1.3
	 */
	pspec = g_param_spec_string ("version", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
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
}

/**
 * zif_depend_new:
 *
 * Return value: A new #ZifDepend instance.
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

