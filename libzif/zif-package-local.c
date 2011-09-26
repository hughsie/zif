/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2010 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-package-local
 * @short_description: Local package
 *
 * This object is a subclass of #ZifPackage
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmdb.h>
#include <rpm/rpmts.h>

#include "zif-db.h"
#include "zif-history.h"
#include "zif-utils.h"
#include "zif-package.h"
#include "zif-package-local.h"
#include "zif-groups.h"
#include "zif-string.h"
#include "zif-depend.h"

#define ZIF_PACKAGE_LOCAL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_PACKAGE_LOCAL, ZifPackageLocalPrivate))

/**
 * ZifPackageLocalPrivate:
 *
 * Private #ZifPackageLocal data
 **/
struct _ZifPackageLocalPrivate
{
	Header			 header;
	ZifGroups		*groups;
	ZifDb			*db;
	ZifHistory		*history;
	gchar			*key_id;
};

G_DEFINE_TYPE (ZifPackageLocal, zif_package_local, ZIF_TYPE_PACKAGE)

/**
 * zif_get_header_string:
 **/
static ZifString *
zif_get_header_string (Header header, rpmTag tag)
{
	gint retval;
	ZifString *data = NULL;
	rpmtd td;

	td = rpmtdNew ();
	retval = headerGet (header, tag, td, HEADERGET_MINMEM);

	if (retval != 1)
		goto out;
	data = zif_string_new (rpmtdGetString (td));
out:
	rpmtdFreeData (td);
	rpmtdFree (td);
	return data;
}

/**
 * zif_get_header_key_id:
 **/
static gchar *
zif_get_header_key_id (Header header, rpmTag tag)
{
	gint retval;
	char *format;
	gchar *data = NULL;
	rpmtd td;

	td = rpmtdNew ();
	retval = headerGet (header, tag, td, HEADERGET_MINMEM);
	if (retval != 1)
		goto out;

	/* format the signature as a text id */
	format = rpmtdFormat (td, RPMTD_FORMAT_PGPSIG, NULL);
	if (format != NULL) {
		/* copy this, so we can free with g_free() */
		data = g_strdup (format);
		free (format);
	}
out:
	rpmtdFreeData (td);
	rpmtdFree (td);
	return data;
}

/**
 * zif_get_header_u32:
 **/
static guint
zif_get_header_u32 (Header header, rpmTag tag)
{
	gint retval;
	uint32_t *data_p;
	guint data = 0;
	rpmtd td;

	td = rpmtdNew ();
	retval = headerGet (header, RPMTAG_SIZE, td, HEADERGET_MINMEM);
	if (retval != 1)
		goto out;
	data_p = rpmtdGetUint32 (td);
	if (data_p != NULL)
		data = *data_p;
out:
	rpmtdFreeData (td);
	rpmtdFree (td);
	return data;
}

/**
 * zif_get_header_uint32_index:
 **/
static GPtrArray *
zif_get_header_uint32_index (Header header, rpmTag tag, guint length)
{
	gint retval;
	guint32 *data;
	GPtrArray *array = NULL;
	rpmtd td;
	guint i;

	td = rpmtdNew ();
	retval = headerGet (header, tag, td, HEADERGET_MINMEM);
	if (retval != 1)
		goto out;
	array = g_ptr_array_new ();
	data = rpmtdGetUint32 (td);
	for (i=0;i<length; i++)
		g_ptr_array_add (array, GUINT_TO_POINTER (*(data+i)));
out:
	rpmtdFreeData (td);
	rpmtdFree (td);
	return array;
}

/**
 * zif_get_header_string_array:
 **/
static GPtrArray *
zif_get_header_string_array (Header header, rpmTag tag)
{
	gint retval;
	const gchar *data;
	GPtrArray *array = NULL;
	rpmtd td;

	td = rpmtdNew ();
	retval = headerGet (header, tag, td, HEADERGET_DEFAULT);
	if (retval != 1)
		goto out;
	array = g_ptr_array_new_with_free_func (g_free);
	data = rpmtdNextString (td);
	while (data != NULL) {
		g_ptr_array_add (array, g_strdup (data));
		data = rpmtdNextString (td);
	}
out:
	rpmtdFreeData (td);
	rpmtdFree (td);
	return array;
}

/**
 * zif_package_local_get_depends_from_name_flags_version:
 **/
static GPtrArray *
zif_package_local_get_depends_from_name_flags_version (GPtrArray *names, GPtrArray *flags, GPtrArray *versions)
{
	guint i;
	rpmsenseFlags rpmflags;
	ZifDepend *depend;
	ZifDependFlag flag;
	const gchar *name;
	const gchar *version;
	GPtrArray *array;

	/* create requires */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; i<names->len; i++) {

		/* ignore rpmlib flags */
		rpmflags = GPOINTER_TO_UINT (g_ptr_array_index (flags, i));
		if ((rpmflags & RPMSENSE_RPMLIB) > 0)
			continue;

		/* convert to enums */
		flag = ZIF_DEPEND_FLAG_UNKNOWN;
		if ((rpmflags & RPMSENSE_LESS) > 0)
			flag += ZIF_DEPEND_FLAG_LESS;
		if ((rpmflags & RPMSENSE_GREATER) > 0)
			flag += ZIF_DEPEND_FLAG_GREATER;
		if ((rpmflags & RPMSENSE_EQUAL) > 0)
			flag += ZIF_DEPEND_FLAG_EQUAL;

		/* no version means any */
		if (flag == ZIF_DEPEND_FLAG_UNKNOWN)
			flag = ZIF_DEPEND_FLAG_ANY;

		/* unknown */
		name = g_ptr_array_index (names, i);

		/* some packages are broken and don't set
		 * RPMSENSE_RPMLIB for rpmlib depends */
		if (g_str_has_prefix (name, "rpmlib("))
			continue;

		version = g_ptr_array_index (versions, i);
		depend = zif_depend_new_from_values (name,
						     flag,
						     version);
		g_ptr_array_add (array, depend);
	}
	return array;
}

/*
 * zif_package_local_ensure_data:
 */
static gboolean
zif_package_local_ensure_data (ZifPackage *pkg,
			       ZifPackageEnsureType type,
			       ZifState *state,
			       GError **error)
{
	GPtrArray *files;
	GPtrArray *dirnames;
	GPtrArray *basenames;
	GPtrArray *fileindex;
	guint i;
	gchar *filename;
	guint size;
	ZifString *tmp;
	const gchar *text;
	const gchar *group;
	GPtrArray *depends;
	GPtrArray *flags;
	GPtrArray *names;
	GPtrArray *versions;
	gboolean ret = FALSE;
	Header header = ZIF_PACKAGE_LOCAL(pkg)->priv->header;

	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* eigh? */
	if (header == NULL) {
		g_set_error (error,
			     ZIF_PACKAGE_ERROR,
			     ZIF_PACKAGE_ERROR_FAILED,
			     "no header for %s",
			     zif_package_get_printable (pkg));
		goto out;
	}

	if (type == ZIF_PACKAGE_ENSURE_TYPE_FILES) {
		/* files */
		basenames = zif_get_header_string_array (header, RPMTAG_BASENAMES);

		/* create the files */
		if (basenames != NULL) {

			/* get the mapping */
			dirnames = zif_get_header_string_array (header, RPMTAG_DIRNAMES);
			fileindex = zif_get_header_uint32_index (header, RPMTAG_DIRINDEXES, basenames->len);
			if (basenames->len != fileindex->len) {
				g_set_error_literal (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED,
						     "internal error, basenames length is not the same as index length, "
						     "possibly corrupt db?");
				goto out;
			}
			if (fileindex->len > fileindex->len) {
				g_set_error_literal (error,
						     ZIF_PACKAGE_ERROR,
						     ZIF_PACKAGE_ERROR_FAILED,
						     "internal error, fileindex length is bigger than index length, "
						     "possibly corrupt db?");
				goto out;
			}

			files = g_ptr_array_new_with_free_func (g_free);
			for (i=0; i<basenames->len; i++) {
				guint idx;
				idx = GPOINTER_TO_UINT (g_ptr_array_index (fileindex, i));
				if (idx > dirnames->len) {
					g_warning ("index bigger than dirnames (%i > %i) for package %s [%s], i=%i, dn=%i, bn=%i, fi=%i",
						     idx, dirnames->len, zif_package_get_package_id (pkg),
						     (const gchar *) g_ptr_array_index (basenames, i),
						     i, dirnames->len, basenames->len, fileindex->len);
					continue;
				}
				filename = g_strconcat (g_ptr_array_index (dirnames, idx), g_ptr_array_index (basenames, i), NULL);
				g_ptr_array_add (files, filename);
			}
			zif_package_set_files (pkg, files);
			g_ptr_array_unref (files);

			/* free, as we have files */
			g_ptr_array_unref (dirnames);
			g_ptr_array_unref (basenames);
			g_ptr_array_unref (fileindex);
		} else {
			files = g_ptr_array_new_with_free_func (g_free);
			zif_package_set_files (pkg, files);
			g_ptr_array_unref (files);
		}

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_SUMMARY) {
		/* summary */
		tmp = zif_get_header_string (header, RPMTAG_SUMMARY);
		zif_package_set_summary (pkg, tmp);
		zif_string_unref (tmp);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_LICENCE) {
		/* license */
		tmp = zif_get_header_string (header, RPMTAG_LICENSE);
		zif_package_set_license (pkg, tmp);
		zif_string_unref (tmp);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_DESCRIPTION) {
		/* description */
		tmp = zif_get_header_string (header, RPMTAG_DESCRIPTION);
		if (tmp == NULL) {
			g_warning ("no description for %s",
				   zif_package_get_id (pkg));
			tmp = zif_string_new ("");
		}
		zif_package_set_description (pkg, tmp);
		zif_string_unref (tmp);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_URL) {
		/* url */
		tmp = zif_get_header_string (header, RPMTAG_URL);
		if (tmp != NULL) {
			zif_package_set_url (pkg, tmp);
			zif_string_unref (tmp);
		}

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_SIZE) {
		/* size */
		size = zif_get_header_u32 (header, RPMTAG_SIZE);
		if (size != 0)
			zif_package_set_size (pkg, size);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_CATEGORY) {
		/* category */
		tmp = zif_get_header_string (header, RPMTAG_GROUP);
		zif_package_set_category (pkg, tmp);
		zif_string_unref (tmp);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_SOURCE_FILENAME) {

		/* source rpm */
		tmp = zif_get_header_string (header, RPMTAG_SOURCERPM);
		zif_package_set_source_filename (pkg, tmp);
		zif_string_unref (tmp);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_GROUP) {
		/* group */
		text = zif_package_get_category (pkg, state, error);
		if (text == NULL)
			goto out;
		group = zif_groups_get_group_for_cat (ZIF_PACKAGE_LOCAL(pkg)->priv->groups, text, error);
		if (group == NULL)
			goto out;

		tmp = zif_string_new (group);
		zif_package_set_group (pkg, tmp);
		zif_string_unref (tmp);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_REQUIRES) {
		/* requires */
		names = zif_get_header_string_array (header, RPMTAG_REQUIRENAME);
		if (names == NULL) {
			depends = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
			zif_package_set_requires (pkg, depends);
			g_ptr_array_unref (depends);
		} else {
			versions = zif_get_header_string_array (header, RPMTAG_REQUIREVERSION);
			flags = zif_get_header_uint32_index (header, RPMTAG_REQUIREFLAGS, names->len);
			depends = zif_package_local_get_depends_from_name_flags_version (names, flags, versions);
			zif_package_set_requires (pkg, depends);
			g_ptr_array_unref (depends);
			g_ptr_array_unref (names);
			g_ptr_array_unref (versions);
			g_ptr_array_unref (flags);
		}

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_PROVIDES) {
		/* provides */
		names = zif_get_header_string_array (header, RPMTAG_PROVIDENAME);
		if (names == NULL) {
			depends = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
			zif_package_set_provides (pkg, depends);
			g_ptr_array_unref (depends);
		} else {
			versions = zif_get_header_string_array (header, RPMTAG_PROVIDEVERSION);
			flags = zif_get_header_uint32_index (header, RPMTAG_PROVIDEFLAGS, names->len);
			depends = zif_package_local_get_depends_from_name_flags_version (names, flags, versions);
			zif_package_set_provides (pkg, depends);
			g_ptr_array_unref (depends);
			g_ptr_array_unref (names);
			g_ptr_array_unref (versions);
			g_ptr_array_unref (flags);
		}

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_CONFLICTS) {
		/* conflicts */
		names = zif_get_header_string_array (header, RPMTAG_CONFLICTNAME);
		if (names == NULL) {
			depends = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
			zif_package_set_conflicts (pkg, depends);
			g_ptr_array_unref (depends);
		} else {
			versions = zif_get_header_string_array (header, RPMTAG_CONFLICTVERSION);
			flags = zif_get_header_uint32_index (header, RPMTAG_CONFLICTFLAGS, names->len);
			depends = zif_package_local_get_depends_from_name_flags_version (names, flags, versions);
			zif_package_set_conflicts (pkg, depends);
			g_ptr_array_unref (depends);
			g_ptr_array_unref (names);
			g_ptr_array_unref (versions);
			g_ptr_array_unref (flags);
		}

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_OBSOLETES) {
		/* obsoletes */
		names = zif_get_header_string_array (header, RPMTAG_OBSOLETENAME);
		if (names == NULL) {
			depends = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
			zif_package_set_obsoletes (pkg, depends);
			g_ptr_array_unref (depends);
		} else {
			versions = zif_get_header_string_array (header, RPMTAG_OBSOLETEVERSION);
			flags = zif_get_header_uint32_index (header, RPMTAG_OBSOLETEFLAGS, names->len);
			depends = zif_package_local_get_depends_from_name_flags_version (names, flags, versions);
			zif_package_set_obsoletes (pkg, depends);
			g_ptr_array_unref (depends);
			g_ptr_array_unref (names);
			g_ptr_array_unref (versions);
			g_ptr_array_unref (flags);
		}
	} else {
		g_set_error (error,
			     ZIF_PACKAGE_ERROR,
			     ZIF_PACKAGE_ERROR_NO_SUPPORT,
			     "Ensure type '%s' not supported on ZifPackageLocal",
			     zif_package_ensure_type_to_string (type));
		goto out;
	}

	/* success */
	ret = TRUE;
out:
	return ret;
}

/**
 * zif_package_local_get_header:
 * @pkg: A #ZifPackageLocal
 *
 * Gets the RPM header object for the package.
 *
 * Return value: (transfer none): The rpm Header structure, or %NULL if unset
 *
 * Since: 0.1.0
 **/
gpointer
zif_package_local_get_header (ZifPackageLocal *pkg)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE_LOCAL (pkg), NULL);
	return pkg->priv->header;
}

/**
 * zif_package_local_set_from_header:
 * @pkg: A #ZifPackageLocal
 * @header: A rpm Header structure
 * @flags: a bitfield indicating if we should lookup and fix the package-id data
 * @error: A #GError, or %NULL
 *
 * Sets the local package from an RPM header object.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.3
 **/
gboolean
zif_package_local_set_from_header (ZifPackageLocal *pkg,
				   gpointer header,
				   ZifPackageLocalFlags flags,
				   GError **error)
{
	const gchar *arch = NULL;
	const gchar *name = NULL;
	const gchar *origin = NULL;
	const gchar *release = NULL;
	const gchar *version = NULL;
	gboolean installed;
	gboolean ret = FALSE;
	gchar *from_repo = NULL;
	gchar *package_id = NULL;
	gchar *package_id_tmp = NULL;
	guint epoch = 0;
	ZifString *pkgid = NULL;

	g_return_val_if_fail (ZIF_IS_PACKAGE_LOCAL (pkg), FALSE);
	g_return_val_if_fail (header != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* save header so we can read when required */
	pkg->priv->header = headerLink (header);

	/* get NEVRA */
	name = headerGetString(header, RPMTAG_NAME);
	epoch = headerGetNumber(header, RPMTAG_EPOCH);
	version = headerGetString(header, RPMTAG_VERSION);
	release = headerGetString(header, RPMTAG_RELEASE);
	arch = headerGetString(header, RPMTAG_ARCH);
	/* XXX this never contains anything in practise */
	origin = headerGetString(header, RPMTAG_PACKAGEORIGIN);

	/* set the pkgid */
	pkgid = zif_get_header_string (header, RPMTAG_SHA1HEADER);
	if (pkgid == NULL) {
		g_set_error (error,
			     ZIF_PACKAGE_ERROR,
			     ZIF_PACKAGE_ERROR_NO_SUPPORT,
			     "no pkgid for %s-%s-%s",
			     name, version, release);
		goto out;
	}
	zif_package_set_pkgid (ZIF_PACKAGE (pkg), pkgid);

	/* non-installed ZifPackageLocal objects are local files */
	installed = zif_package_is_installed (ZIF_PACKAGE (pkg));

	/* origin may be blank if the user is using yumdb rather than librpm */
	if (installed && origin != NULL) {
		g_debug ("simple case, origin set as %s", origin);
		package_id = zif_package_id_from_nevra (name,
							epoch,
							version,
							release,
							arch,
							origin);
	} else {
		package_id = zif_package_id_from_nevra (name,
							epoch,
							version,
							release,
							arch,
							installed ? "installed" : "local");
	}

	/* set id */
	ret = zif_package_set_id (ZIF_PACKAGE (pkg), package_id, error);
	if (!ret)
		goto out;

	/* get repo_id for installed package from history */
	if (installed &&
	    (flags & ZIF_PACKAGE_LOCAL_FLAG_USE_HISTORY) > 0) {
		from_repo = zif_history_get_repo_newest (pkg->priv->history,
							 ZIF_PACKAGE (pkg),
							 NULL);
		if (from_repo != NULL) {
			zif_package_set_repo_id (ZIF_PACKAGE (pkg),
						 from_repo);
		}
	}

	/* get repo_id for installed package from yumdb */
	if (from_repo == NULL &&
	    installed &&
	    (flags & ZIF_PACKAGE_LOCAL_FLAG_USE_YUMDB) > 0) {
		from_repo = zif_db_get_string (pkg->priv->db,
					       ZIF_PACKAGE (pkg),
					       "from_repo",
					       NULL);
		if (from_repo != NULL) {
			zif_package_set_repo_id (ZIF_PACKAGE (pkg),
						 from_repo);
		}
	}
out:
	if (pkgid != NULL)
		zif_string_unref (pkgid);
	g_free (from_repo);
	g_free (package_id);
	g_free (package_id_tmp);
	return ret;
}

/**
 * zif_package_local_set_from_filename:
 * @pkg: A #ZifPackageLocal
 * @filename: A filename
 * @error: A #GError, or %NULL
 *
 * Sets a local package object from a local file.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_package_local_set_from_filename (ZifPackageLocal *pkg, const gchar *filename, GError **error)
{
	rpmRC rc;
	FD_t fd = NULL;
	Header hdr = NULL;
	rpmts ts = NULL;
	gboolean ret = FALSE;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_PACKAGE_LOCAL (pkg), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* open the file for reading */
	fd = Fopen (filename, "r.fdio");
	if (fd == NULL) {
		g_set_error (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED,
			     "failed to open %s", filename);
		goto out;
	}
	if (Ferror(fd)) {
		g_set_error (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED,
			     "failed to open %s: %s", filename, Fstrerror(fd));
		goto out;
	}

	/* create an empty transaction set */
	ts = rpmtsCreate ();

	/* we don't want to abort on missing keys */
	rpmtsSetVSFlags (ts, _RPMVSF_NOSIGNATURES);

	/* read in the file */
	rc = rpmReadPackageFile (ts, fd, filename, &hdr);
	if (rc != RPMRC_OK) {
		/* we only return SHA1 and MD5 failures, as we're not
		 * checking signatures at this stage */
		g_set_error (error,
			     ZIF_PACKAGE_ERROR,
			     ZIF_PACKAGE_ERROR_FAILED,
			     "%s could not be verified",
			     filename);
		goto out;
	}

	/* convert and upscale */
	headerConvert (hdr, HEADERCONV_RETROFIT_V3);

	/* set from header */
	ret = zif_package_local_set_from_header (pkg,
						 hdr,
						 ZIF_PACKAGE_LOCAL_FLAG_NOTHING,
						 &error_local);
	if (!ret) {
		g_set_error (error, ZIF_PACKAGE_ERROR, ZIF_PACKAGE_ERROR_FAILED,
			     "failed to set from header: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* save this in case it's added to the transaction */
	zif_package_set_cache_filename (ZIF_PACKAGE (pkg), filename);

out:
	/* close header and file */
	if (ts != NULL)
		rpmtsFree (ts);
	if (hdr != NULL)
		headerFree (hdr);
	if (fd != NULL)
		Fclose (fd);
	return ret;
}

/**
 * zif_package_local_get_key_id:
 * @pkg: A #ZifPackageLocal
 *
 * Gets a signature key identifier for the package, e.g.
 * "RSA/SHA256, Thu Sep 23 17:25:34 2010, Key ID 421caddb97a1071f"
 *
 * Return value: an ID for success, %NULL for failure
 *
 * Since: 0.1.3
 **/
const gchar *
zif_package_local_get_key_id (ZifPackageLocal *pkg)
{
	g_return_val_if_fail (ZIF_IS_PACKAGE_LOCAL (pkg), NULL);

	/* cached copy */
	if (pkg->priv->key_id != NULL)
		goto out;

	/* try RSA first */
	pkg->priv->key_id = zif_get_header_key_id (pkg->priv->header,
						   RPMTAG_RSAHEADER);
	if (pkg->priv->key_id != NULL)
		goto out;

	/* try DSA second */
	pkg->priv->key_id = zif_get_header_key_id (pkg->priv->header,
						   RPMTAG_DSAHEADER);
	if (pkg->priv->key_id != NULL)
		goto out;

	/* nothing, but this isn't an error */
out:
	return pkg->priv->key_id;
}

/**
 * zif_package_local_finalize:
 **/
static void
zif_package_local_finalize (GObject *object)
{
	ZifPackageLocal *pkg;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_PACKAGE_LOCAL (object));
	pkg = ZIF_PACKAGE_LOCAL (object);

	g_free (pkg->priv->key_id);
	g_object_unref (pkg->priv->groups);
	g_object_unref (pkg->priv->db);
	g_object_unref (pkg->priv->history);
	if (pkg->priv->header != NULL)
		headerFree (pkg->priv->header);

	G_OBJECT_CLASS (zif_package_local_parent_class)->finalize (object);
}

/**
 * zif_package_local_class_init:
 **/
static void
zif_package_local_class_init (ZifPackageLocalClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifPackageClass *package_class = ZIF_PACKAGE_CLASS (klass);
	object_class->finalize = zif_package_local_finalize;

	package_class->ensure_data = zif_package_local_ensure_data;

	g_type_class_add_private (klass, sizeof (ZifPackageLocalPrivate));
}

/**
 * zif_package_local_init:
 **/
static void
zif_package_local_init (ZifPackageLocal *pkg)
{
	pkg->priv = ZIF_PACKAGE_LOCAL_GET_PRIVATE (pkg);
	pkg->priv->groups = zif_groups_new ();
	pkg->priv->db = zif_db_new ();
	pkg->priv->history = zif_history_new ();
	pkg->priv->header = NULL;
}

/**
 * zif_package_local_new:
 *
 * Return value: A new #ZifPackageLocal instance.
 *
 * Since: 0.1.0
 **/
ZifPackage *
zif_package_local_new (void)
{
	ZifPackage *pkg;
	pkg = g_object_new (ZIF_TYPE_PACKAGE_LOCAL, NULL);
	return ZIF_PACKAGE (pkg);
}

