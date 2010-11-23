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
 * SECTION:zif-utils
 * @short_description: Simple utility functions useful to zif
 *
 * Common, non-object functions are declared here.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmdb.h>
#include <archive.h>
#include <archive_entry.h>
#include <bzlib.h>
#include <zlib.h>

#include "zif-utils.h"
#include "zif-package.h"

/**
 * zif_utils_error_quark:
 *
 * Return value: Our personal error quark.
 *
 * Since: 0.1.0
 **/
GQuark
zif_utils_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("zif_utils_error");
	return quark;
}

/**
 * zif_init:
 *
 * This must be called before any of the zif_* functions are called.
 *
 * Return value: %TRUE if we initialised correctly
 *
 * Since: 0.1.0
 **/
gboolean
zif_init (void)
{
	gint retval;

	retval = rpmReadConfigFiles (NULL, NULL);
	if (retval != 0) {
		g_warning ("failed to read config files");
		return FALSE;
	}

	return TRUE;
}

/**
 * zif_boolean_from_text:
 * @text: the input text
 *
 * Convert a text boolean into it's enumerated boolean state
 *
 * Return value: %TRUE for positive, %FALSE for negative
 *
 * Since: 0.1.0
 **/
gboolean
zif_boolean_from_text (const gchar *text)
{
	g_return_val_if_fail (text != NULL, FALSE);
	if (g_ascii_strcasecmp (text, "true") == 0 ||
	    g_ascii_strcasecmp (text, "yes") == 0 ||
	    g_ascii_strcasecmp (text, "1") == 0)
		return TRUE;
	return FALSE;
}

/**
 * zif_list_print_array:
 * @array: The string array to print
 *
 * Print an array of strings to %STDOUT.
 *
 * Since: 0.1.0
 **/
void
zif_list_print_array (GPtrArray *array)
{
	guint i;
	ZifPackage *package;

	for (i=0;i<array->len;i++) {
		package = g_ptr_array_index (array, i);
		zif_package_print (package);
	}
}

/**
 * zif_package_id_build:
 **/
static gchar *
zif_package_id_build (const gchar *name, const gchar *version,
		      const gchar *arch, const gchar *data)
{
	g_return_val_if_fail (name != NULL, NULL);
	return g_strjoin (";", name,
			  version != NULL ? version : "",
			  arch != NULL ? arch : "",
			  data != NULL ? data : "",
			  NULL);
}

/**
 * zif_package_id_from_nevra:
 * @name: The package name, e.g. "hal"
 * @epoch: The package epoch, e.g. 1 or 0 for none.
 * @version: The package version, e.g. "1.0.0"
 * @release: The package release, e.g. "2"
 * @arch: The package architecture, e.g. "i386"
 * @data: The package data, typically the repo name, or "installed"
 *
 * Formats a PackageId structure from a NEVRA.
 *
 * Return value: The PackageId value, or %NULL if invalid
 *
 * Since: 0.1.0
 **/
gchar *
zif_package_id_from_nevra (const gchar *name, guint epoch, const gchar *version, const gchar *release, const gchar *arch, const gchar *data)
{
	gchar *version_compound;
	gchar *package_id;

	/* do we include an epoch? */
	if (epoch == 0)
		version_compound = g_strdup_printf ("%s-%s", version, release);
	else
		version_compound = g_strdup_printf ("%i:%s-%s", epoch, version, release);

	package_id = zif_package_id_build (name, version_compound, arch, data);
	g_free (version_compound);
	return package_id;
}

/**
 * zif_package_convert_evr:
 * @evr: epoch, version, release
 * @epoch: the package epoch
 * @version: the package version
 * @release: the package release
 *
 * Modifies evr, so pass in copy
 *
 * Return value: %TRUE if the EVR was parsed
 **/
gboolean
zif_package_convert_evr (gchar *evr, const gchar **epoch, const gchar **version, const gchar **release)
{
	gchar *find;

	g_return_val_if_fail (evr != NULL, FALSE);

	/* set to NULL initially */
	*version = NULL;

	/* split possible epoch */
	find = strstr (evr, ":");
	if (find != NULL) {
		*find = '\0';
		*epoch = evr;
		*version = find+1;
	} else {
		*epoch = NULL;
		*version = evr;
	}

	/* split possible release */
	find = g_strrstr (*version, "-");
	if (find != NULL) {
		*find = '\0';
		*release = find+1;
	} else {
		*release = NULL;
	}

	return TRUE;
}

/**
 * zif_compare_evr:
 * @a: the first version string, or %NULL
 * @b: the second version string, or %NULL
 *
 * Compare two [epoch:]version[-release] strings
 *
 * Return value: 1 for a>b, 0 for a==b, -1 for b>a
 *
 * Since: 0.1.0
 **/
gint
zif_compare_evr (const gchar *a, const gchar *b)
{
	gint val = 0;
	gchar *ad = NULL;
	gchar *bd = NULL;
	const gchar *ae, *av, *ar;
	const gchar *be, *bv, *br;

	/* exactly the same, optimise */
	if (g_strcmp0 (a, b) == 0)
		goto out;

	/* deal with one evr being NULL and the other a value */
	if (a != NULL && b == NULL) {
		val = 1;
		goto out;
	}
	if (a == NULL && b != NULL) {
		val = -1;
		goto out;
	}

	/* copy */
	ad = g_strdup (a);
	bd = g_strdup (b);

	/* split */
	zif_package_convert_evr (ad, &ae, &av, &ar);
	zif_package_convert_evr (bd, &be, &bv, &br);

	/* compare epoch */
	if (ae != NULL && be != NULL) {
		val = rpmvercmp (ae, be);
		if (val != 0)
			goto out;
	} else if (ae != NULL && atol (ae) > 0) {
		val = 1;
		goto out;
	} else if (be != NULL && atol (be) > 0) {
		val = -1;
		goto out;
	}

	/* compare version */
	val = rpmvercmp (av, bv);
	if (val != 0)
		goto out;

	/* compare release */
	if (ar != NULL && br != NULL)
		val = rpmvercmp (ar, br);

out:
	g_free (ad);
	g_free (bd);
	return val;
}

#define ZIF_BUFFER_SIZE 16384

/**
 * zif_file_decompress_zlib:
 **/
static gboolean
zif_file_decompress_zlib (const gchar *in, const gchar *out, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	gint size;
	gint written;
	gzFile *f_in = NULL;
	FILE *f_out = NULL;
	guchar buf[ZIF_BUFFER_SIZE];
	GCancellable *cancellable;

	g_return_val_if_fail (in != NULL, FALSE);
	g_return_val_if_fail (out != NULL, FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* get cancellable */
	cancellable = zif_state_get_cancellable (state);

	/* open file for reading */
	f_in = gzopen (in, "rb");
	if (f_in == NULL) {
		g_set_error (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_FAILED_TO_READ,
			     "cannot open %s for reading", in);
		goto out;
	}

	/* open file for writing */
	f_out = fopen (out, "w");
	if (f_out == NULL) {
		g_set_error (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_FAILED_TO_WRITE,
			     "cannot open %s for writing", out);
		goto out;
	}

	/* read in all data in chunks */
	while (TRUE) {
		/* read data */
		size = gzread (f_in, buf, ZIF_BUFFER_SIZE);
		if (size == 0)
			break;

		/* error */
		if (size < 0) {
			g_set_error_literal (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_FAILED_TO_READ,
					     "failed read");
			goto out;
		}

		/* write data */
		written = fwrite (buf, 1, size, f_out);
		if (written != size) {
			g_set_error (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_FAILED_TO_WRITE,
				     "only wrote %i/%i bytes", written, size);
			goto out;
		}

		/* is cancelled */
		ret = !g_cancellable_is_cancelled (cancellable);
		if (!ret) {
			g_set_error_literal (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_CANCELLED, "cancelled");
			goto out;
		}
	}

	/* success */
	ret = TRUE;
out:
	if (f_in != NULL)
		gzclose (f_in);
	if (f_out != NULL)
		fclose (f_out);
	return ret;
}

/**
 * zif_file_decompress_bz2:
 **/
static gboolean
zif_file_decompress_bz2 (const gchar *in, const gchar *out, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	FILE *f_in = NULL;
	FILE *f_out = NULL;
	BZFILE *b = NULL;
	gint size;
	gint written;
	gchar buf[ZIF_BUFFER_SIZE];
	gint bzerror = BZ_OK;
	GCancellable *cancellable;

	g_return_val_if_fail (in != NULL, FALSE);
	g_return_val_if_fail (out != NULL, FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* get cancellable */
	cancellable = zif_state_get_cancellable (state);

	/* open file for reading */
	f_in = fopen (in, "r");
	if (f_in == NULL) {
		g_set_error (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_FAILED_TO_READ,
			     "cannot open %s for reading", in);
		goto out;
	}

	/* open file for writing */
	f_out = fopen (out, "w");
	if (f_out == NULL) {
		g_set_error (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_FAILED_TO_WRITE,
			     "cannot open %s for writing", out);
		goto out;
	}

	/* read in file */
	b = BZ2_bzReadOpen (&bzerror, f_in, 0, 0, NULL, 0);
	if (bzerror != BZ_OK) {
		g_set_error (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_FAILED_TO_READ,
			     "cannot open %s for bz2 reading", in);
		goto out;
	}

	/* read in all data in chunks */
	while (bzerror != BZ_STREAM_END) {
		/* read data */
		size = BZ2_bzRead (&bzerror, b, buf, ZIF_BUFFER_SIZE);
		if (bzerror != BZ_OK && bzerror != BZ_STREAM_END) {
			g_set_error_literal (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_FAILED,
					     "failed to decompress");
			goto out;
		}

		/* write data */
		written = fwrite (buf, 1, size, f_out);
		if (written != size) {
			g_set_error (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_FAILED_TO_WRITE,
				     "only wrote %i/%i bytes", written, size);
			goto out;
		}

		/* is cancelled */
		ret = !g_cancellable_is_cancelled (cancellable);
		if (!ret) {
			g_set_error_literal (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_CANCELLED,
					     "cancelled");
			goto out;
		}
	}

	/* failed to read */
	if (bzerror != BZ_STREAM_END) {
		g_set_error (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_FAILED,
			     "did not decompress file: %s", in);
		goto out;
	}

	/* success */
	ret = TRUE;
out:
	if (b != NULL)
		BZ2_bzReadClose (&bzerror, b);
	if (f_in != NULL)
		fclose (f_in);
	if (f_out != NULL)
		fclose (f_out);
	return ret;
}

/**
 * zif_file_decompress:
 * @in: the filename to unpack
 * @out: the file to create
 * @state: a #ZifState to use for progress reporting
 * @error: a valid %GError
 *
 * Decompress files into a directory
 *
 * Return value: %TRUE if the file was decompressed
 *
 * Since: 0.1.0
 **/
gboolean
zif_file_decompress (const gchar *in, const gchar *out, ZifState *state, GError **error)
{
	gboolean ret = FALSE;

	g_return_val_if_fail (in != NULL, FALSE);
	g_return_val_if_fail (out != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* set action */
	zif_state_action_start (state, ZIF_STATE_ACTION_DECOMPRESSING, in);

	/* bz2 */
	if (g_str_has_suffix (in, "bz2")) {
		ret = zif_file_decompress_bz2 (in, out, state, error);
		goto out;
	}

	/* zlib */
	if (g_str_has_suffix (in, "gz")) {
		ret = zif_file_decompress_zlib (in, out, state, error);
		goto out;
	}

	/* no support */
	g_set_error (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_FAILED,
		     "no support to decompress file: %s", in);
out:
	return ret;
}

/**
 * zif_file_untar:
 * @filename: the filename to unpack
 * @directory: the directory to unpack into
 * @error: a valid %GError
 *
 * Untar files into a directory
 *
 * Return value: %TRUE if the file was decompressed
 *
 * Since: 0.1.0
 **/
gboolean
zif_file_untar (const gchar *filename, const gchar *directory, GError **error)
{
	gboolean ret = FALSE;
	struct archive *arch = NULL;
	struct archive_entry *entry;
	int r;
	int retval;
	gchar *retcwd;
	gchar buf[PATH_MAX];

	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (directory != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* save the PWD as we chdir to extract */
	retcwd = getcwd (buf, PATH_MAX);
	if (retcwd == NULL) {
		g_set_error_literal (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_FAILED,
				     "failed to get cwd");
		goto out;
	}

	/* we can only read tar achives */
	arch = archive_read_new ();
	archive_read_support_format_all (arch);
	archive_read_support_compression_all (arch);

	/* open the tar file */
	r = archive_read_open_file (arch, filename, ZIF_BUFFER_SIZE);
	if (r) {
		g_set_error (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_FAILED_TO_READ,
			     "cannot open: %s", archive_error_string (arch));
		goto out;
	}

	/* switch to our destination directory */
	retval = chdir (directory);
	if (retval != 0) {
		g_set_error (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_FAILED,
			     "failed chdir to %s", directory);
		goto out;
	}

	/* decompress each file */
	for (;;) {
		r = archive_read_next_header (arch, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			g_set_error (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_FAILED,
				     "cannot read header: %s", archive_error_string (arch));
			goto out;
		}
		r = archive_read_extract (arch, entry, 0);
		if (r != ARCHIVE_OK) {
			g_set_error (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_FAILED,
				     "cannot extract: %s", archive_error_string (arch));
			goto out;
		}
	}

	/* completed all okay */
	ret = TRUE;
out:
	/* close the archive */
	if (arch != NULL) {
		archive_read_close (arch);
		archive_read_finish (arch);
	}

	/* switch back to PWD */
	retval = chdir (buf);
	if (retval != 0)
		g_warning ("cannot chdir back!");

	return ret;
}

/**
 * zif_file_get_uncompressed_name:
 * @filename: the filename, e.g. /lib/dave.tar.gz
 *
 * Finds the uncompressed filename.
 *
 * Return value: the uncompressed file name, e.g. /lib/dave.tar, use g_free() to free.
 *
 * Since: 0.1.0
 **/
gchar *
zif_file_get_uncompressed_name (const gchar *filename)
{
	guint len;
	gchar *tmp;

	g_return_val_if_fail (filename != NULL, NULL);

	/* remove compression extension */
	tmp = g_strdup (filename);
	len = strlen (tmp);
	if (len > 4 && g_str_has_suffix (tmp, ".gz"))
		tmp[len-3] = '\0';
	else if (len > 5 && g_str_has_suffix (tmp, ".bz2"))
		tmp[len-4] = '\0';

	/* return newly allocated string */
	return tmp;
}

/**
 * zif_file_is_compressed_name:
 * @filename: the filename, e.g. /lib/dave.tar.gz
 *
 * Finds out if the filename is compressed
 *
 * Return value: %TRUE if the file needs decompression
 *
 * Since: 0.1.0
 **/
gboolean
zif_file_is_compressed_name (const gchar *filename)
{
	g_return_val_if_fail (filename != NULL, FALSE);

	if (g_str_has_suffix (filename, ".gz"))
		return TRUE;
	if (g_str_has_suffix (filename, ".bz2"))
		return TRUE;

	return FALSE;
}

/**
 * zif_package_id_split:
 * @package_id: the ; delimited PackageID to split
 *
 * Splits a PackageID into the correct number of parts, checking the correct
 * number of delimiters are present.
 *
 * Return value: a GStrv or %NULL if invalid, use g_strfreev() to free
 *
 * Since: 0.1.0
 **/
gchar **
zif_package_id_split (const gchar *package_id)
{
	gchar **sections = NULL;

	if (package_id == NULL)
		goto out;

	/* split by delimeter ';' */
	sections = g_strsplit (package_id, ";", -1);
	if (g_strv_length (sections) != 4)
		goto out;

	/* name has to be valid */
	if (sections[0][0] != '\0')
		return sections;
out:
	g_strfreev (sections);
	return NULL;
}

/**
 * zif_package_id_get_name:
 * @package_id: the ; delimited PackageID to split
 *
 * Gets the package name for a PackageID. This is 9x faster than using
 * zif_package_id_split() where you only need the %ZIF_PACKAGE_ID_NAME
 * component.
 *
 * Return value: a string or %NULL if invalid, use g_free() to free
 *
 * Since: 0.1.1
 **/
gchar *
zif_package_id_get_name (const gchar *package_id)
{
	gchar *name = NULL;
	guint i;

	/* invalid */
	if (package_id == NULL || package_id[0] == '\0')
		goto out;

	/* find the first ; char */
	for (i=1; package_id[i] != '\0'; i++) {
		if (package_id[i] == ';') {
			name = g_strndup (package_id, i);
			goto out;
		}
	}
out:
	return name;
}

/**
 * zif_package_id_get_printable:
 * @package_id: the PackageID to format
 *
 * Formats the package ID in a way that is suitable to show the user.
 *
 * Return value: a string or %NULL if invalid, use g_free() to free
 *
 * Since: 0.1.3
 **/
gchar *
zif_package_id_get_printable (const gchar *package_id)
{
	gchar *printable = NULL;
	gchar **split;

	/* format */
	split = zif_package_id_split (package_id);
	if (split == NULL)
		goto out;
	printable = g_strdup_printf ("%s-%s.%s (%s)",
				     split[0],
				     split[1],
				     split[2],
				     split[3]);
out:
	g_strfreev (split);
	return printable;
}

/**
 * zif_package_id_check:
 * @package_id: the PackageID to check
 *
 * Return value: %TRUE if the PackageID was well formed.
 *
 * Since: 0.1.0
 **/
gboolean
zif_package_id_check (const gchar *package_id)
{
	gchar **sections;
	gboolean ret;

	/* NULL check */
	if (package_id == NULL)
		return FALSE;

	/* UTF8 */
	ret = g_utf8_validate (package_id, -1, NULL);
	if (!ret) {
		g_warning ("invalid UTF8!");
		return FALSE;
	}

	/* correct number of sections */
	sections = zif_package_id_split (package_id);
	if (sections == NULL)
		return FALSE;

	/* all okay */
	g_strfreev (sections);
	return TRUE;
}

/**
 * zif_time_string_to_seconds:
 * @value: the yum time string, e.g. "7h"
 *
 * Converts a yum time string into the number of seconds.
 *
 * Return value: the number of seconds, or zero for failure to parse.
 *
 * Since: 0.1.0
 **/
guint
zif_time_string_to_seconds (const gchar *value)
{
	guint len;
	guint timeval = 0;
	gchar suffix;
	gchar *value_copy = NULL;
	gchar *endptr = NULL;

	g_return_val_if_fail (value != NULL, 0);

	/* long enough */
	len = strlen (value);
	if (len < 2)
		goto out;

	/* yum-speak for "never" */
	if (g_strcmp0 (value, "-1") == 0)
		goto out;

	/* get suffix */
	suffix = value[len-1];

	/* remove suffix */
	value_copy = g_strdup (value);
	value_copy[len-1] = '\0';

	/* convert to number */
	timeval = g_ascii_strtoull (value_copy, &endptr, 10);
	if (value_copy == endptr) {
		g_warning ("failed to convert %s", value_copy);
		goto out;
	}

	/* seconds, minutes, hours, days */
	if (suffix == 's')
		timeval *= 1;
	else if (suffix == 'm')
		timeval *= 60;
	else if (suffix == 'h')
		timeval *= 60*60;
	else if (suffix == 'd')
		timeval *= 24*60*60;
	else
		timeval = 0;

out:
	g_free (value_copy);
	return timeval;
}

