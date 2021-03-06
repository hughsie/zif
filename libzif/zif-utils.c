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
 * @short_description: Simple utility functions
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
#include <lzma.h>
#include <fnmatch.h>
#include <gpgme.h>

#include "zif-utils-private.h"
#include "zif-package.h"

/**
 * zif_utils_gpg_check_signature:
 **/
static gboolean
zif_utils_gpg_check_signature (gpgme_signature_t signature,
			       GError **error)
{
	gboolean ret = FALSE;

	/* look at the signature status */
	switch (gpgme_err_code (signature->status)) {
	case GPG_ERR_NO_ERROR:
		ret = TRUE;
		break;
	case GPG_ERR_SIG_EXPIRED:
	case GPG_ERR_KEY_EXPIRED:
		g_set_error (error,
			     ZIF_UTILS_ERROR,
			     ZIF_UTILS_ERROR_FAILED,
			     "valid signature '%s' has expired",
			     signature->fpr);
		break;
	case GPG_ERR_CERT_REVOKED:
		g_set_error (error,
			     ZIF_UTILS_ERROR,
			     ZIF_UTILS_ERROR_FAILED,
			     "valid signature '%s' has been revoked",
			     signature->fpr);
		break;
	case GPG_ERR_BAD_SIGNATURE:
		g_set_error (error,
			     ZIF_UTILS_ERROR,
			     ZIF_UTILS_ERROR_FAILED,
			     "'%s' is not a valid signature",
			     signature->fpr);
		break;
	case GPG_ERR_NO_PUBKEY:
		g_set_error (error,
			     ZIF_UTILS_ERROR,
			     ZIF_UTILS_ERROR_FAILED,
			     "Could not check signature '%s' as no public key",
			     signature->fpr);
		break;
	default:
		g_set_error (error,
			     ZIF_UTILS_ERROR,
			     ZIF_UTILS_ERROR_FAILED,
			     "gpgme failed to verify signature '%s'",
			     signature->fpr);
		break;
	}
	return ret;
}

gboolean
zif_utils_gpg_verify (const gchar *filename,
		      const gchar *filename_gpg,
		      GError **error)
{
	gboolean ret = FALSE;
	gpgme_ctx_t ctx = NULL;
	gpgme_data_t repomd_gpg = NULL;
	gpgme_data_t repomd = NULL;
	gpgme_error_t rc;
	gpgme_signature_t s;
	gpgme_verify_result_t result;

	/* check version */
	gpgme_check_version (NULL);

	/* startup gpgme */
	rc = gpg_err_init ();
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     ZIF_UTILS_ERROR,
			     ZIF_UTILS_ERROR_FAILED,
			     "failed to startup GPG: %s",
			     gpgme_strerror (rc));
		goto out;
	}

	/* create a new GPG context */
	rc = gpgme_new (&ctx);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     ZIF_UTILS_ERROR,
			     ZIF_UTILS_ERROR_FAILED,
			     "failed to create context: %s",
			     gpgme_strerror (rc));
		goto out;
	}

	/* set the protocol */
	rc = gpgme_set_protocol (ctx, GPGME_PROTOCOL_OpenPGP);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     ZIF_UTILS_ERROR,
			     ZIF_UTILS_ERROR_FAILED,
			     "failed to set protocol: %s",
			     gpgme_strerror (rc));
		goto out;
	}

	/* enable armor mode */
	gpgme_set_armor (ctx, TRUE);

	/* load file */
	rc = gpgme_data_new_from_file (&repomd, filename, 1);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     ZIF_UTILS_ERROR,
			     ZIF_UTILS_ERROR_FAILED,
			     "failed to load repomd: %s",
			     gpgme_strerror (rc));
		goto out;
	}

	/* load signature */
	rc = gpgme_data_new_from_file (&repomd_gpg, filename_gpg, 1);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     ZIF_UTILS_ERROR,
			     ZIF_UTILS_ERROR_FAILED,
			     "failed to load repomd.asc: %s",
			     gpgme_strerror (rc));
		goto out;
	}

	/* verify the repodata.xml */
	g_debug ("verifying %s with %s", filename, filename_gpg);
	rc = gpgme_op_verify (ctx, repomd_gpg, repomd, NULL);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     ZIF_UTILS_ERROR,
			     ZIF_UTILS_ERROR_FAILED,
			     "failed to verify: %s",
			     gpgme_strerror (rc));
		goto out;
	}

	/* verify the result */
	result = gpgme_op_verify_result (ctx);
	if (result == NULL) {
		g_set_error_literal (error,
				     ZIF_UTILS_ERROR,
				     ZIF_UTILS_ERROR_FAILED,
				     "no result record from libgpgme");
		goto out;
	}

	/* look at each signature */
	for (s = result->signatures; s != NULL ; s = s->next ) {
		ret = zif_utils_gpg_check_signature (s, error);
		if (!ret)
			goto out;
	}

	/* success */
	ret = TRUE;
out:
	if (ctx != NULL)
		gpgme_release (ctx);
	gpgme_data_release (repomd_gpg);
	gpgme_data_release (repomd);
	return ret;
}

/**
 * zif_utils_error_quark:
 *
 * Return value: An error quark.
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
 * zif_init_once_cb:
 **/
static gpointer
zif_init_once_cb (gpointer user_data)
{
	gint retval;
	retval = rpmReadConfigFiles (NULL, NULL);
	if (retval != 0) {
		g_warning ("failed to read config files");
		return NULL;
	}
	return GINT_TO_POINTER (1);
}

/**
 * zif_init:
 *
 * This is called automatically to initialize libzif.
 * You normally don't have to call this function manually.
 *
 * Return value: %TRUE if we initialised correctly
 *
 * Since: 0.1.0
 **/
gboolean
zif_init (void)
{
	static GOnce init_once = G_ONCE_INIT;
	g_once (&init_once, zif_init_once_cb, NULL);
	return GPOINTER_TO_INT (init_once.retval);
}

/**
 * zif_guess_content_type:
 * @filename: The target filename
 *
 * Guesses a content type, based on the filename ending.
 *
 * Return value: a content type, or %NULL
 *
 * Since: 0.1.5
 **/
const gchar *
zif_guess_content_type (const gchar *filename)
{
	if (g_str_has_suffix (filename, ".gz"))
		return "application/x-gzip,application/gzip";
	if (g_str_has_suffix (filename, ".bz2"))
		return "application/x-bzip,application/bzip";
	if (g_str_has_suffix (filename, ".xml"))
		return "application/xml";
	if (g_str_has_suffix (filename, "mirrorlist.txt"))
		return "text/plain";
	g_warning ("cannot guess content type for %s", filename);
	return NULL;
}

/**
 * zif_boolean_from_text:
 * @text: The input text
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
 * @array: An array of #ZifPackage's to print
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

	for (i = 0; i < array->len; i++) {
		package = g_ptr_array_index (array, i);
		zif_package_print (package);
	}
}

/**
 * zif_package_id_build:
 * @name: The package name, e.g. "hal"
 * @version: The package version, e.g. "1.0.0-fc14"
 * @arch: The package architecture, e.g. "i386"
 * @data: The package data, typically the repo name, or "installed"
 *
 * Formats a PackageId structure.
 *
 * Return value: A PackageId value, or %NULL if invalid
 *
 * Since: 0.2.4
 **/
gchar *
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
 * Return value: A PackageId value, or %NULL if invalid
 *
 * Since: 0.1.0
 **/
gchar *
zif_package_id_from_nevra (const gchar *name,
			   guint epoch,
			   const gchar *version,
			   const gchar *release,
			   const gchar *arch,
			   const gchar *data)
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
 * zif_package_id_compare_nevra:
 * @package_id1: The package ID, e.g. "hal;1:1.01-3;i386;fedora"
 * @package_id2: The package ID, e.g. "hal;1:1.01-3;i386;updates-testing"
 *
 * Compares the NEVRA sections in two package ID strings, ignoring the fourth
 * data section.
 *
 * Return value: %TRUE if the NEVRA are equal
 *
 * Since: 0.2.5
 **/
gboolean
zif_package_id_compare_nevra (const gchar *package_id1,
			      const gchar *package_id2)
{
	gboolean ret = FALSE;
	gchar **split1;
	gchar **split2;

	/* split up into 4 usable sections */
	split1 = zif_package_id_split (package_id1);
	split2 = zif_package_id_split (package_id2);

	if (g_strcmp0 (split1[ZIF_PACKAGE_ID_NAME],    split2[ZIF_PACKAGE_ID_NAME]) == 0 &&
	    g_strcmp0 (split1[ZIF_PACKAGE_ID_VERSION], split2[ZIF_PACKAGE_ID_VERSION]) == 0 &&
	    g_strcmp0 (split1[ZIF_PACKAGE_ID_ARCH],    split2[ZIF_PACKAGE_ID_ARCH]) == 0)
		ret = TRUE;

	g_strfreev (split1);
	g_strfreev (split2);
	return ret;
}

/**
 * zif_package_id_to_nevra:
 * @package_id: The package ID, e.g. "hal;1:1.01-3;i386;fedora"
 * @name: The returned package name, e.g. "hal"
 * @epoch: The returned package epoch, e.g. 1 or 0 for none.
 * @version: The returned package version, e.g. "1.0.0"
 * @release: The returned package release, e.g. "2"
 * @arch: The returned package architecture, e.g. "i386"
 *
 * Parses a PackageId structure to a NEVRA.
 *
 * Return value: %TRUE if the string was parsed okay
 *
 * Since: 0.1.3
 **/
gboolean
zif_package_id_to_nevra (const gchar *package_id,
			 gchar **name, guint *epoch, gchar **version,
			 gchar **release, gchar **arch)
{
	gboolean ret = FALSE;
	gchar **split;
	gchar *ver;
	gchar *ver_start;
	guint i;

	/* split up into 4 usable sections */
	split = zif_package_id_split (package_id);

	/* nothing */
	if (epoch != NULL)
		*epoch = 0;

	/* we could have "1-2" or "3:1-2" */
	ver_start = ver = split[ZIF_PACKAGE_ID_VERSION];
	for (i = 0; ver[i] != '\0'; i++) {
		if (ver[i] == ':') {
			ver[i] = '\0';
			ver_start = &ver[i+1];
			if (epoch != NULL)
				*epoch = atoi (ver);
		}
		if (ver[i] == '-') {
			ver[i] = '\0';
			if (version != NULL)
				*version = g_strdup (ver_start);
			if (release != NULL)
				*release = g_strdup (&ver[i+1]);
			ret = TRUE;
		}
	}
	if (!ret)
		goto out;

	/* only do this when we know we've got the version */
	if (name != NULL)
		*name = g_strdup (split[ZIF_PACKAGE_ID_NAME]);
	if (arch != NULL)
		*arch = g_strdup (split[ZIF_PACKAGE_ID_ARCH]);
out:
	g_strfreev (split);
	return ret;
}

/**
 * zif_package_convert_evr_full:
 * @evr: epoch, version, release
 * @epoch: The package epoch
 * @version: The package version
 * @release: The package release
 * @distro: The package distro, or %NULL
 *
 * Modifies evr, so pass in copy
 *
 * Return value: %TRUE if the EVR was parsed
 *
 * Since: 0.2.1
 **/
gboolean
zif_package_convert_evr_full (gchar *evr,
			      const gchar **epoch,
			      const gchar **version,
			      const gchar **release,
			      const gchar **distro)
{
	gchar *find;

	g_return_val_if_fail (evr != NULL, FALSE);

	/* split possible epoch and version */
	find = strchr (evr, ':');
	if (find != NULL) {
		*find = '\0';
		*epoch = evr;
		*version = find+1;
	} else {
		*epoch = NULL;
		*version = evr;
	}

	/* split possible release */
	find = strrchr (*version, '-');
	if (find != NULL) {
		*find = '\0';
		*release = find+1;
	} else {
		*release = NULL;
	}

	/* split possible and optional distro */
	if (distro != NULL) {
		if (*release != NULL) {
			find = strrchr (*release, '.');
			if (find != NULL) {
				*find = '\0';
				*distro = find+1;
			} else {
				*distro = NULL;
			}
		} else {
			*distro = NULL;
		}
	}
	return TRUE;
}

/**
 * zif_package_convert_evr:
 * @evr: epoch, version, release
 * @epoch: The package epoch
 * @version: The package version
 * @release: The package release (note: with any distro)
 *
 * Modifies evr, so pass in copy
 *
 * Return value: %TRUE if the EVR was parsed
 *
 * Since: 0.1.0
 **/
gboolean
zif_package_convert_evr (gchar *evr,
			 const gchar **epoch,
			 const gchar **version,
			 const gchar **release)
{
	return zif_package_convert_evr_full (evr,
					     epoch,
					     version,
					     release,
					     NULL);
}

/**
 * zif_compare_evr_full:
 * @a: The first version string, or %NULL
 * @b: The second version string, or %NULL
 * @compare_mode: the way the versions are compared
 *
 * Compare two [epoch:]version[-release] strings
 *
 * Return value: 1 for a>b, 0 for a==b, -1 for b>a
 *
 * Since: 0.2.1
 **/
gint
zif_compare_evr_full (const gchar *a, const gchar *b,
		      ZifPackageCompareMode compare_mode)
{
	gint val = 0;
	gchar a_tmp[128]; /* 128 bytes should be enough for anybody, heh */
	gchar b_tmp[128];
	const gchar *ae, *av, *ar, *ad;
	const gchar *be, *bv, *br, *bd;

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
	g_strlcpy (a_tmp, a, 128);
	g_strlcpy (b_tmp, b, 128);

	/* split */
	zif_package_convert_evr_full (a_tmp, &ae, &av, &ar, &ad);
	zif_package_convert_evr_full (b_tmp, &be, &bv, &br, &bd);

	/* compare distro */
	if (ad != NULL &&
	    bd != NULL &&
	    compare_mode == ZIF_PACKAGE_COMPARE_MODE_DISTRO) {
		val = rpmvercmp (ad, bd);
		if (val != 0)
			goto out;
	}

	/* compare epoch */
	if (ae != NULL && be != NULL) {
		val = rpmvercmp (ae, be);
		if (val != 0)
			goto out;
	} else if (ae != NULL && atoi (ae) > 0) {
		val = 1;
		goto out;
	} else if (be != NULL && atoi (be) > 0) {
		val = -1;
		goto out;
	}

	/* compare version */
	val = rpmvercmp (av, bv);
	if (val != 0)
		goto out;

	/* compare release */
	if (ar != NULL && br != NULL) {
		val = rpmvercmp (ar, br);
		if (val != 0)
			goto out;
	}

	/* compare distro */
	if (ad != NULL && bd != NULL) {
		val = rpmvercmp (ad, bd);
		if (val != 0)
			goto out;
	}
out:
	return val;
}

/**
 * zif_compare_evr:
 * @a: The first version string, or %NULL
 * @b: The second version string, or %NULL
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
	gint ret;
	ret = zif_compare_evr_full (a, b,
				    ZIF_PACKAGE_COMPARE_MODE_VERSION);
	return ret;
}

/**
 * zif_arch_is_native:
 * @a: The first arch string
 * @b: The second arch string
 *
 * Compare two architectures to see if they are native, so for instance
 * i386 is native on a i686 system, but x64 isn't.
 *
 * Return value: %TRUE if the architecture is compatible
 *
 * Since: 0.1.3
 **/
gboolean
zif_arch_is_native (const gchar *a, const gchar *b)
{
	/* same */
	if (g_strcmp0 (a, b) == 0)
		return TRUE;

	/* 32bit intel */
	if (g_str_has_suffix (a, "86") &&
	    g_str_has_suffix (b, "86"))
		return TRUE;

	/* others */
	return FALSE;
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
	gzFile f_in = NULL;
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
		g_set_error (error,
			     ZIF_UTILS_ERROR,
			     ZIF_UTILS_ERROR_FAILED_TO_READ,
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
 * zif_file_decompress_lzma:
 **/
static gboolean
zif_file_decompress_lzma (const gchar *in, const gchar *out, ZifState *state, GError **error)
{
	gboolean ret = FALSE;
	gint size;
	gint written;
	FILE *f_in = NULL;
	FILE *f_out = NULL;
	guchar in_buf[ZIF_BUFFER_SIZE];
	guchar out_buf[ZIF_BUFFER_SIZE];
	GCancellable *cancellable;

	lzma_ret r;
	lzma_stream stream = LZMA_STREAM_INIT;
	lzma_stream *strm = &stream;
	lzma_action action;

	g_return_val_if_fail (in != NULL, FALSE);
	g_return_val_if_fail (out != NULL, FALSE);
	g_return_val_if_fail (zif_state_valid (state), FALSE);

	/* get cancellable */
	cancellable = zif_state_get_cancellable (state);

	r = lzma_auto_decoder(strm, UINT64_MAX, 0);
	if (r == LZMA_MEM_ERROR) {
		g_set_error (error,
			     ZIF_UTILS_ERROR,
			     ZIF_UTILS_ERROR_FAILED,
			     "out of memory");
		goto out;
	}
	else if (r != LZMA_OK) {
		g_set_error (error,
			     ZIF_UTILS_ERROR,
			     ZIF_UTILS_ERROR_FAILED,
			     "internal error");
		goto out;
	}

	/* open file for reading */
	f_in = fopen (in, "rb");
	if (f_in == NULL) {
		g_set_error (error,
			     ZIF_UTILS_ERROR,
			     ZIF_UTILS_ERROR_FAILED_TO_READ,
			     "cannot open %s for reading", in);
		goto out;
	}

	/* open file for writing */
	f_out = fopen (out, "w");
	if (f_out == NULL) {
		g_set_error (error,
			     ZIF_UTILS_ERROR,
			     ZIF_UTILS_ERROR_FAILED_TO_WRITE,
			     "cannot open %s for writing", out);
		goto out;
	}

	strm->avail_in = 0;
	strm->next_out = out_buf;
	strm->avail_out = ZIF_BUFFER_SIZE;

	action = LZMA_RUN;

	/* read in all data in chunks */
	while (r == LZMA_OK) {
		/* read data */
		if (strm->avail_in == 0) {
			size = fread (in_buf, 1, ZIF_BUFFER_SIZE, f_in);
	
			/* error */
			if (ferror (f_in)) {
				g_set_error_literal (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_FAILED_TO_READ,
						     "failed read");
				goto out;
			}
	
			if (feof (f_in))
				action = LZMA_FINISH;
	
			strm->next_in = in_buf;
			strm->avail_in = size;
		}

		r = lzma_code (strm, action);

		/* write data */
		if (strm->avail_out == 0 || r != LZMA_OK) {
			size = ZIF_BUFFER_SIZE - strm->avail_out;

			written = fwrite (out_buf, 1, size, f_out);
			if (written != size) {
				g_set_error (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_FAILED_TO_WRITE,
					     "only wrote %i/%i bytes", written, size);
				goto out;
			}

			strm->next_out = out_buf;
			strm->avail_out = ZIF_BUFFER_SIZE;
		}

		/* is cancelled */
		ret = !g_cancellable_is_cancelled (cancellable);
		if (!ret) {
			g_set_error_literal (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_CANCELLED, "cancelled");
			goto out;
		}
	}

	/* failed to read */
	if (r != LZMA_STREAM_END) {
		g_set_error (error, ZIF_UTILS_ERROR, ZIF_UTILS_ERROR_FAILED,
			     "did not decompress file: %s", in);
		goto out;
	}

	/* success */
	ret = TRUE;
out:
	lzma_end (strm);
	if (f_in != NULL)
		fclose (f_in);
	if (f_out != NULL)
		fclose (f_out);
	return ret;
}

/**
 * zif_file_decompress:
 * @in: A filename to unpack
 * @out: The file to create
 * @state: A #ZifState to use for progress reporting
 * @error: A %GError
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

	/* lzma */
	if (g_str_has_suffix (in, "lzma") ||
	    g_str_has_suffix (in, "xz")) {
		ret = zif_file_decompress_lzma (in, out, state, error);
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
 * @filename: A filename to unpack
 * @directory: The directory to unpack into
 * @error: A %GError
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
 * @filename: A filename, e.g. "/lib/dave.tar.gz"
 *
 * Finds the uncompressed filename for a compressed file.
 *
 * Return value: The uncompressed file name, e.g. /lib/dave.tar, use g_free() to free.
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
	else if (len > 4 && g_str_has_suffix (tmp, ".xz"))
		tmp[len-3] = '\0';
	else if (len > 5 && g_str_has_suffix (tmp, ".bz2"))
		tmp[len-4] = '\0';

	/* return newly allocated string */
	return tmp;
}

/**
 * zif_file_is_compressed_name:
 * @filename: A filename, e.g. /lib/dave.tar.gz
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
 * @package_id: The ';' delimited PackageID to split
 *
 * Splits a PackageID into the correct number of parts, checking the correct
 * number of delimiters are present.
 *
 * Return value: (element-type utf8) (transfer full): A #GStrv or %NULL if invalid
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
 * @package_id: The ';' delimited PackageID to split
 *
 * Gets the package name for a PackageID. This is 9x faster than using
 * zif_package_id_split() where you only need the %ZIF_PACKAGE_ID_NAME
 * component.
 *
 * Return value: A string or %NULL if invalid, use g_free() to free
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
 * @package_id: A PackageID to format
 *
 * Formats the package ID in a way that is suitable to show the user.
 *
 * Return value: A string or %NULL if invalid, use g_free() to free
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
 * @package_id: A PackageID to check
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
 * @value: A yum time string, e.g. "7h"
 *
 * Converts a yum time string into the number of seconds.
 *
 * Return value: A number of seconds, or zero for failure to parse.
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

/**
 * zif_str_compare_regex:
 * @a: The string
 * @b: The pattern to match
 *
 * Compares one string against another
 *
 * Return value: %TRUE for success
 *
 * Since: 0.2.4
 **/
gboolean
zif_str_compare_regex (const gchar *a, const gchar *b)
{
	return g_regex_match_simple (b, a, G_REGEX_OPTIMIZE, 0);
}

/**
 * zif_str_compare_glob:
 * @a: The string
 * @b: The pattern to match
 *
 * Compares one string against another
 *
 * Return value: %TRUE for success
 *
 * Since: 0.2.4
 **/
gboolean
zif_str_compare_glob (const gchar *a, const gchar *b)
{
	return fnmatch (b, a, 0) == 0;
}

/**
 * zif_str_compare_equal:
 * @a: The string
 * @b: The pattern to match
 *
 * Compares one string against another
 *
 * Return value: %TRUE for success
 *
 * Since: 0.2.4
 **/
gboolean
zif_str_compare_equal (const gchar *a, const gchar *b)
{
	return strcmp (a, b) == 0;
}

/**
 * zif_load_multiline_key_file: (skip)
 * @filename: The repo file to load
 * @error: A #GError, or %NULL
 *
 * The source.repo files are not standard GKeyFiles as they can contain
 * multiple lines for a key value, e.g.
 *
 * [multiline1]
 * name=Multiline1
 * baseurl=http://download1.fedoraproject.org/
 *	http://download2.fedoraproject.org/
 * enabled=true
 *
 * Return value: A #GKeyFile, or %NULL
 *
 * Since: 0.2.4
 **/
GKeyFile *
zif_load_multiline_key_file (const gchar *filename,
			     GError **error)
{
	gboolean ret;
	gchar *data = NULL;
	gchar **lines = NULL;
	GKeyFile *file = NULL;
	gsize len;
	GString *string = NULL;
	guint i;

	/* load file */
	ret = g_file_get_contents (filename, &data, &len, error);
	if (!ret)
		goto out;

	/* split into lines */
	string = g_string_new ("");
	lines = g_strsplit (data, "\n", -1);
	for (i = 0; lines[i] != NULL; i++) {
		/* if a line starts with whitespace, then append it on
		 * the previous line */
		g_strdelimit (lines[i], "\t", ' ');
		if (lines[i][0] == ' ' && string->len > 0) {
			g_string_set_size (string, string->len - 1);
			g_string_append_printf (string,
						";%s\n",
						g_strchug (lines[i]));
		} else {
			g_string_append_printf (string,
						"%s\n",
						lines[i]);
		}
	}

	/* remove final newline */
	if (string->len > 0)
		g_string_set_size (string, string->len - 1);

	/* load modified lines */
	file = g_key_file_new ();
	ret = g_key_file_load_from_data (file,
					 string->str,
					 -1,
					 G_KEY_FILE_KEEP_COMMENTS,
					 error);
	if (!ret) {
		g_key_file_free (file);
		file = NULL;
		goto out;
	}
out:
	if (string != NULL)
		g_string_free (string, TRUE);
	g_free (data);
	g_strfreev (lines);
	return file;
}

/**
 * zif_string_replace: (skip)
 * @string: A GString
 * @search: The string to search for
 * @replace: The string to replace with
 *
 * Replaces all instances of @search with @replace trying to be as fast
 * as possible and only reallocating the string when @replace is larger
 * than @search.
 *
 * Return value: The number of replacements that were done.
 *
 * Since: 0.2.4
 **/
guint
zif_string_replace (GString *string,
		    const gchar *search,
		    const gchar *replace)
{
	guint s_len = strlen (search);
	guint r_len = strlen (replace);
	gchar *tmp;
	guint replacements = 0;
	guint extra_size;

	g_return_val_if_fail (string != NULL, 0);
	g_return_val_if_fail (search != NULL, 0);
	g_return_val_if_fail (replace != NULL, 0);

	/* trivial */
	if (string->len == 0)
		goto out;

	/* get the first found */
	tmp = strstr (string->str, search);
	if (tmp == NULL)
		goto out;

	/* CASE 1: no need to realloc, no need to shunt */
	if (r_len == s_len) {
		while (tmp != NULL)  {
			memmove (tmp, replace, r_len);
			replacements++;
			tmp = strstr (tmp + r_len, search);
		};
		goto out;
	}

	/* CASE 2: no need to realloc, but need to shunt */
	if (r_len < s_len) {
		while (tmp != NULL)  {
			memmove (tmp, replace, r_len);
			memmove (tmp + r_len,
				 tmp + s_len,
				 strlen (tmp + s_len) + 1);
			string->len -= (s_len - r_len);
			replacements++;
			tmp = strstr (tmp + r_len, search);
		};
		goto out;
	}

	/* CASE 3: need to realloc, need to shunt */
	while (tmp != NULL)  {
		replacements++;
		tmp = strstr (tmp + s_len, search);
	};
	extra_size = (r_len - s_len) * replacements;
	g_string_set_size (string, string->len + extra_size);

	/* now we've re-alloc'd the string, shunt */
	tmp = strstr (string->str, search);
	while (tmp != NULL)  {
		memmove (tmp + r_len,
			 tmp + s_len,
			 strlen (tmp) - s_len + 1);
		memmove (tmp, replace, r_len);
		tmp = strstr (tmp + r_len, search);
	};
out:
	return replacements;
}

/**
 * zif_package_id_convert_basic:
 * @package_id: A package ID
 *
 * Returns a "basic" package-id that does not have the repo appended.
 * For instance, "hal;0.1.2;i386;installed:fedora" would be converted
 * to "hal;0.1.2;i386;installed".
 *
 * Return value: The basic package-id.
 *
 * Since: 0.2.5
 **/
gchar *
zif_package_id_convert_basic (const gchar *package_id)
{
	gchar *package_id_new;
	gchar **split = NULL;
	gchar *tmp;

	/* can we shortcut? */
	tmp = g_strstr_len (package_id, -1, ":");
	if (tmp == NULL) {
		package_id_new = g_strdup (package_id);
		goto out;
	}

	/* remove the :repo_id suffix */
	split = zif_package_id_split (package_id);
	tmp = g_strstr_len (split[ZIF_PACKAGE_ID_DATA], -1, ":");
	if (tmp != NULL)
		*tmp = '\0';

	/* rebuild the package ID */
	package_id_new = zif_package_id_build (split[ZIF_PACKAGE_ID_NAME],
					       split[ZIF_PACKAGE_ID_VERSION],
					       split[ZIF_PACKAGE_ID_ARCH],
					       split[ZIF_PACKAGE_ID_DATA]);
out:
	g_strfreev (split);
	return package_id_new;
}

/**
 * zif_ensure_parent_dir_exists:
 * @filename: A full path
 * @cancellable: a #GCancellable, or %NULL
 * @error: A #GError, or %NULL
 *
 * Creates the parent dir for a file if it does not already exist.
 *
 * Return value: %TRUE for already exists or created okay.
 *
 * Since: 0.2.5
 **/
gboolean
zif_ensure_parent_dir_exists (const gchar *filename,
			      GCancellable *cancellable,
			      GError **error)
{
	gboolean ret = TRUE;
	gchar *dirname = NULL;
	GFile *file;

	/* does already exist */
	dirname = g_path_get_dirname (filename);
	file = g_file_new_for_path (dirname);
	if (g_file_query_exists (file, cancellable))
		goto out;

	/* no, so create */
	ret = g_file_make_directory_with_parents (file,
						  cancellable,
						  error);
out:
	g_free (dirname);
	g_object_unref (file);
	return ret;
}
