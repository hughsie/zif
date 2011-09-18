/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-history
 * @short_description: Discover details about past transactions
 *
 * #ZifHistory allows the user to see past transaction details to
 * see what was installed, upgraded and the reasons why.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <sqlite3.h>
#include <stdlib.h>

#include "zif-config.h"
#include "zif-history.h"
#include "zif-monitor.h"
#include "zif-utils.h"

#define ZIF_HISTORY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_HISTORY, ZifHistoryPrivate))

struct _ZifHistoryPrivate
{
	gboolean		 loaded;
	gchar			*filename;
	sqlite3			*db;
	ZifConfig		*config;
};

G_DEFINE_TYPE (ZifHistory, zif_history, G_TYPE_OBJECT)
static gpointer zif_history_object = NULL;

/**
 * zif_history_error_quark:
 *
 * Return value: An error quark.
 *
 * Since: 0.2.4
 **/
GQuark
zif_history_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("zif_history_error");
	return quark;
}

/**
 * zif_history_load:
 **/
static gboolean
zif_history_load (ZifHistory *history, GError **error)
{
	const gchar *statement;
	gboolean ret = TRUE;
	gchar *error_msg = NULL;
	gint rc;

	/* already loaded */
	if (history->priv->loaded)
		goto out;

	/* nothing set */
	if (history->priv->filename == NULL) {
		history->priv->filename = zif_config_get_string (history->priv->config,
								 "history_db",
								 error);
		if (history->priv->filename == NULL)
			goto out;
	}

	/* open db */
	g_debug ("trying to open database '%s'", history->priv->filename);
	rc = sqlite3_open (history->priv->filename, &history->priv->db);
	if (rc != SQLITE_OK) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_HISTORY_ERROR,
			     ZIF_HISTORY_ERROR_FAILED,
			     "Can't open history database: %s",
			     sqlite3_errmsg (history->priv->db));
		sqlite3_close (history->priv->db);
		goto out;
	}

	/* we don't need to keep doing fsync */
	sqlite3_exec (history->priv->db,
		      "PRAGMA synchronous=OFF",
		      NULL, NULL, NULL);

	/* check transactions */
	rc = sqlite3_exec (history->priv->db,
			   "SELECT * FROM packages LIMIT 1",
			   NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_debug ("creating table to repair: %s", error_msg);
		sqlite3_free (error_msg);
		statement = "CREATE TABLE version ("
			    "schema_version INTEGER DEFAULT 1,"
			    "imported INTEGER DEFAULT 0);";
		sqlite3_exec (history->priv->db,
			      statement,
			      NULL, NULL, NULL);
		statement = "CREATE TABLE packages ("
			    "transaction_id INTEGER PRIMARY KEY AUTOINCREMENT,"
			    "installed_by INTEGER DEFAULT -1,"
			    "command_line TEXT,"
			    "from_repo TEXT,"
			    "reason TEXT,"
			    "releasever INTEGER DEFAULT 0,"
			    "name TEXT,"
			    "version TEXT,"
			    "arch TEXT,"
			    "timestamp INTEGER DEFAULT 0);";
		sqlite3_exec (history->priv->db,
			      statement,
			      NULL, NULL, NULL);
	}

	/* yippee */
	history->priv->loaded = TRUE;
out:
	return ret;
}

/**
 * zif_history_add_entry:
 * @history: A #ZifHistory
 * @package: A #ZifPackage
 * @timestamp: A timestamp
 * @reason: A %ZifTransactionReason
 * @error: A #GError, or %NULL
 *
 * Adds an entry into the zif history store
 *
 * Return value: %TRUE on success
 *
 * Since: 0.2.4
 **/
gboolean
zif_history_add_entry (ZifHistory *history,
		       ZifPackage *package,
		       guint timestamp,
		       ZifTransactionReason reason,
		       guint uid,
		       const gchar *command_line,
		       GError **error)
{
	gboolean ret;
	gint rc;
	guint releasever;
	const gchar *repo_id;
	sqlite3_stmt *statement = NULL;

	g_return_val_if_fail (ZIF_IS_HISTORY (history), FALSE);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure database is loaded */
	ret = zif_history_load (history, error);
	if (!ret)
		goto out;

	/* prepare statement */
	rc = sqlite3_prepare_v2 (history->priv->db,
				 "INSERT INTO packages ("
				 "installed_by, "
				 "command_line, "
				 "from_repo, "
				 "reason, "
				 "releasever, "
				 "name, "
				 "version, "
				 "arch, "
				 "timestamp) "
				 "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
				 -1, &statement, NULL);
	if (rc != SQLITE_OK) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_HISTORY_ERROR,
			     ZIF_HISTORY_ERROR_FAILED,
			     "failed to prepare statement: %s",
			     sqlite3_errmsg (history->priv->db));
		goto out;
	}

	/* FIXME: get from version */
	releasever = 16;

	/* remove any installed prefix */
	repo_id = zif_package_get_data (package);
	if (g_str_has_prefix (repo_id, "installed:"))
		repo_id += 10;

	/* bind data, so that the freeform proxy text cannot be used to inject SQL */
	sqlite3_bind_int (statement,
			  1,
			  uid);
	sqlite3_bind_text (statement,
			  2,
			  command_line,
			  -1,
			  SQLITE_STATIC);
	sqlite3_bind_text (statement,
			  3,
			  repo_id,
			  -1,
			  SQLITE_STATIC);
	sqlite3_bind_text (statement,
			  4,
			  zif_transaction_reason_to_string (reason),
			  -1, SQLITE_STATIC);
	sqlite3_bind_int (statement,
			  5,
			  releasever);
	sqlite3_bind_text (statement,
			  6,
			  zif_package_get_name (package),
			  -1,
			  SQLITE_STATIC);
	sqlite3_bind_text (statement,
			  7,
			  zif_package_get_version (package),
			  -1,
			  SQLITE_STATIC);
	sqlite3_bind_text (statement,
			  8,
			  zif_package_get_arch (package),
			  -1,
			  SQLITE_STATIC);
	sqlite3_bind_int (statement,
			  9,
			  timestamp);

	/* execute statement */
	rc = sqlite3_step (statement);
	if (rc != SQLITE_DONE) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_HISTORY_ERROR,
			     ZIF_HISTORY_ERROR_FAILED,
			     "failed to execute statement: %s",
			     sqlite3_errmsg (history->priv->db));
		goto out;
	}

	ret = TRUE;
out:
	if (statement != NULL)
		sqlite3_finalize (statement);
	return ret;
}

/**
 * zif_history_get_transactions_sqlite_cb:
 **/
static gint
zif_history_get_transactions_sqlite_cb (void *data,
					gint argc,
					gchar **argv,
					gchar **col_name)
{
	gint i;
	guint timestamp;
	GArray **array = (GArray **) data;

	for (i=0; i<argc; i++) {
		timestamp = atoi (argv[i]);
		g_array_append_val (*array, timestamp);
	}
	return 0;
}

/**
 * zif_history_list_transactions:
 * @history: A #ZifHistory
 * @error: A #GError, or %NULL
 *
 * Returns an array of transaction timestamps.
 * Each timestamp may correspond to a number of modified packages.
 *
 * Return value: (transfer full): an #GArray of guint
 *
 * Since: 0.2.4
 **/
GArray *
zif_history_list_transactions (ZifHistory *history, GError **error)
{
	const gchar *statement;
	GArray *array = NULL;
	GArray *array_tmp = NULL;
	gboolean ret = TRUE;
	gchar *error_msg = NULL;
	gint rc;

	g_return_val_if_fail (ZIF_IS_HISTORY (history), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* ensure database is loaded */
	ret = zif_history_load (history, error);
	if (!ret)
		goto out;

	/* return all the different transaction timestamps */
	array_tmp = g_array_new (FALSE, FALSE, sizeof (guint));
	statement = "SELECT DISTINCT timestamp FROM packages ORDER BY timestamp ASC";
	rc = sqlite3_exec (history->priv->db,
			   statement,
			   zif_history_get_transactions_sqlite_cb,
			   &array_tmp,
			   &error_msg);
	if (rc != SQLITE_OK) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_HISTORY_ERROR,
			     ZIF_HISTORY_ERROR_FAILED,
			     "SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

	/* success */
	array = g_array_ref (array_tmp);
out:
	if (array_tmp != NULL)
		g_array_unref (array_tmp);
	return array;
}

/**
 * zif_history_get_packages_sqlite_cb:
 **/
static gint
zif_history_get_packages_sqlite_cb (void *data,
				    gint argc,
				    gchar **argv,
				    gchar **col_name)
{
	gboolean ret;
	gchar *package_id;
	ZifPackage *package;
	GPtrArray **array = (GPtrArray **) data;

	package = zif_package_new ();
	package_id = zif_package_id_build (argv[0],
					   argv[1],
					   argv[2],
					   argv[3]);
	ret = zif_package_set_id (package,
				  package_id,
				  NULL);
	g_assert (ret);
	g_debug ("add %s", package_id);
	g_ptr_array_add (*array, package);
	g_free (package_id);

	return 0;
}

/**
 * zif_history_get_packages:
 * @history: A #ZifHistory
 * @timestamp: A timestamp
 * @error: A #GError, or %NULL
 *
 * Return all the packages that were modified on a specified timestamp.
 *
 * Return value: (transfer full): A #GPtrArray of ZifPackage's with the specified timestamp
 *
 * Since: 0.2.4
 **/
GPtrArray *
zif_history_get_packages (ZifHistory *history,
			  guint timestamp,
			  GError **error)
{
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;
	GPtrArray *array = NULL;
	GPtrArray *array_tmp = NULL;

	g_return_val_if_fail (ZIF_IS_HISTORY (history), NULL);
	g_return_val_if_fail (timestamp != 0, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* ensure database is loaded */
	ret = zif_history_load (history, error);
	if (!ret)
		goto out;

	/* return all the different transaction timestamps */
	array_tmp = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	statement = g_strdup_printf ("SELECT name, version, arch, from_repo "
				     "FROM packages WHERE timestamp = %i",
				     timestamp);
	rc = sqlite3_exec (history->priv->db,
			   statement,
			   zif_history_get_packages_sqlite_cb,
			   &array_tmp,
			   &error_msg);
	if (rc != SQLITE_OK) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_HISTORY_ERROR,
			     ZIF_HISTORY_ERROR_FAILED,
			     "SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

	/* success */
	array = g_ptr_array_ref (array_tmp);
out:
	g_free (statement);
	if (array_tmp != NULL)
		g_ptr_array_unref (array_tmp);
	return array;
}

/**
 * zif_history_get_uid_sqlite_cb:
 **/
static gint
zif_history_get_uid_sqlite_cb (void *data,
			       gint argc,
			       gchar **argv,
			       gchar **col_name)
{
	guint *uid = (guint *) data;
	*uid = atoi (argv[0]);
	return 0;
}

/**
 * zif_history_get_uid:
 * @history: A #ZifHistory
 * @package: A #ZifPackage
 * @timestamp: A timestamp
 * @error: A #GError, or %NULL
 *
 * Gets the user id for the specified package for the given timestamp.
 *
 * Return value: A UID, or %G_MAXUINT for error
 *
 * Since: 0.2.4
 **/
guint
zif_history_get_uid (ZifHistory *history,
		     ZifPackage *package,
		     guint timestamp,
		     GError **error)
{
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;
	guint uid = G_MAXUINT;

	g_return_val_if_fail (ZIF_IS_HISTORY (history), G_MAXUINT);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), G_MAXUINT);
	g_return_val_if_fail (timestamp != 0, G_MAXUINT);
	g_return_val_if_fail (error == NULL || *error == NULL, G_MAXUINT);

	/* ensure database is loaded */
	ret = zif_history_load (history, error);
	if (!ret)
		goto out;

	/* return all the different transaction timestamps */
	statement = g_strdup_printf ("SELECT installed_by FROM packages "
				     "WHERE timestamp = %i AND "
				     "name = '%s' AND "
				     "version = '%s' AND "
				     "arch = '%s' LIMIT 1;",
				     timestamp,
				     zif_package_get_name (package),
				     zif_package_get_version (package),
				     zif_package_get_arch (package));
	rc = sqlite3_exec (history->priv->db,
			   statement,
			   zif_history_get_uid_sqlite_cb,
			   &uid,
			   &error_msg);
	if (rc != SQLITE_OK) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_HISTORY_ERROR,
			     ZIF_HISTORY_ERROR_FAILED,
			     "SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}
out:
	g_free (statement);
	return uid;
}

/**
 * zif_history_get_string_sqlite_cb:
 **/
static gint
zif_history_get_string_sqlite_cb (void *data,
				  gint argc,
				  gchar **argv,
				  gchar **col_name)
{
	gchar **id = (gchar **) data;
	*id = g_strdup (argv[0]);
	return 0;
}

/**
 * zif_history_get_cmdline:
 * @history: A #ZifHistory
 * @package: A #ZifPackage
 * @timestamp: A timestamp
 * @error: A #GError, or %NULL
 *
 * Gets the command line used to process the specified package for the
 * given timestamp.
 *
 * Return value: The command line, or %NULL for error
 *
 * Since: 0.2.4
 **/
gchar *
zif_history_get_cmdline (ZifHistory *history,
			 ZifPackage *package,
			 guint timestamp,
			 GError **error)
{
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;
	gchar *cmdline = NULL;

	g_return_val_if_fail (ZIF_IS_HISTORY (history), NULL);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (timestamp != 0, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* ensure database is loaded */
	ret = zif_history_load (history, error);
	if (!ret)
		goto out;

	/* return all the different transaction timestamps */
	statement = g_strdup_printf ("SELECT command_line FROM packages "
				     "WHERE timestamp = %i AND "
				     "name = '%s' AND "
				     "version = '%s' AND "
				     "arch = '%s' LIMIT 1;",
				     timestamp,
				     zif_package_get_name (package),
				     zif_package_get_version (package),
				     zif_package_get_arch (package));
	rc = sqlite3_exec (history->priv->db,
			   statement,
			   zif_history_get_string_sqlite_cb,
			   &cmdline,
			   &error_msg);
	if (rc != SQLITE_OK) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_HISTORY_ERROR,
			     ZIF_HISTORY_ERROR_FAILED,
			     "SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}
out:
	g_free (statement);
	return cmdline;
}

/**
 * zif_history_get_repo:
 * @history: A #ZifHistory
 * @package: A #ZifPackage
 * @timestamp: A timestamp
 * @error: A #GError, or %NULL
 *
 * Gets the source store id for the specified package for the given
 * timestamp.
 *
 * Return value: The store ID, or %NULL for error
 *
 * Since: 0.2.4
 **/
gchar *
zif_history_get_repo (ZifHistory *history,
		      ZifPackage *package,
		      guint timestamp,
		      GError **error)
{
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;
	gchar *repo_id = NULL;

	g_return_val_if_fail (ZIF_IS_HISTORY (history), NULL);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (timestamp != 0, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* ensure database is loaded */
	ret = zif_history_load (history, error);
	if (!ret)
		goto out;

	/* return all the different transaction timestamps */
	statement = g_strdup_printf ("SELECT from_repo FROM packages "
				     "WHERE timestamp = %i AND "
				     "name = '%s' AND "
				     "version = '%s' AND "
				     "arch = '%s' LIMIT 1;",
				     timestamp,
				     zif_package_get_name (package),
				     zif_package_get_version (package),
				     zif_package_get_arch (package));
	rc = sqlite3_exec (history->priv->db,
			   statement,
			   zif_history_get_string_sqlite_cb,
			   &repo_id,
			   &error_msg);
	if (rc != SQLITE_OK) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_HISTORY_ERROR,
			     ZIF_HISTORY_ERROR_FAILED,
			     "SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}
	if (repo_id == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_HISTORY_ERROR,
			     ZIF_HISTORY_ERROR_FAILED,
			     "Failed to find %s",
			     zif_package_get_printable (package));
		goto out;
	}
out:
	g_free (statement);
	return repo_id;
}

/**
 * zif_history_get_reason:
 * @history: A #ZifHistory
 * @package: A #ZifPackage
 * @timestamp: A timestamp
 * @error: A #GError, or %NULL
 *
 * Gets the transaction reason for the specified package and the given
 * timestamp.
 *
 * Return value: The %ZifTransactionReason, or %ZIF_TRANSACTION_REASON_INVALID for error
 *
 * Since: 0.2.4
 **/
ZifTransactionReason
zif_history_get_reason (ZifHistory *history,
			ZifPackage *package,
			guint timestamp,
			GError **error)
{
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;
	gchar *reason_str = NULL;
	ZifTransactionReason reason = ZIF_TRANSACTION_REASON_INVALID;

	g_return_val_if_fail (ZIF_IS_HISTORY (history), reason);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), reason);
	g_return_val_if_fail (timestamp != 0, reason);
	g_return_val_if_fail (error == NULL || *error == NULL, reason);

	/* ensure database is loaded */
	ret = zif_history_load (history, error);
	if (!ret)
		goto out;

	/* return all the different transaction timestamps */
	statement = g_strdup_printf ("SELECT reason FROM packages "
				     "WHERE timestamp = %i AND "
				     "name = '%s' AND "
				     "version = '%s' AND "
				     "arch = '%s' LIMIT 1;",
				     timestamp,
				     zif_package_get_name (package),
				     zif_package_get_version (package),
				     zif_package_get_arch (package));
	rc = sqlite3_exec (history->priv->db,
			   statement,
			   zif_history_get_string_sqlite_cb,
			   &reason_str,
			   &error_msg);
	if (rc != SQLITE_OK) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_HISTORY_ERROR,
			     ZIF_HISTORY_ERROR_FAILED,
			     "SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}
	if (reason_str == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_HISTORY_ERROR,
			     ZIF_HISTORY_ERROR_FAILED,
			     "Failed to find %s",
			     zif_package_get_printable (package));
		goto out;
	}
	reason = zif_transaction_reason_from_string (reason_str);
out:
	g_free (statement);
	return reason;
}

/**
 * zif_history_get_repo_newest:
 * @history: A #ZifHistory
 * @package: A #ZifPackage
 * @error: A #GError, or %NULL
 *
 * Gets the source repository for a specific package.
 * Note: this will return the repo for the most recently installed
 * version of the package.
 *
 * Return value: The remote store ID for the installed package, or %NULL
 *
 * Since: 0.2.4
 **/
gchar *
zif_history_get_repo_newest (ZifHistory *history,
			     ZifPackage *package,
			     GError **error)
{
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;
	gchar *repo_id = NULL;

	g_return_val_if_fail (ZIF_IS_HISTORY (history), NULL);
	g_return_val_if_fail (ZIF_IS_PACKAGE (package), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* ensure database is loaded */
	ret = zif_history_load (history, error);
	if (!ret)
		goto out;

	/* return all the different transaction timestamps */
	statement = g_strdup_printf ("SELECT from_repo FROM packages WHERE "
				     "name = '%s' AND "
				     "version = '%s' AND "
				     "arch = '%s' ORDER BY timestamp ASC LIMIT 1;",
				     zif_package_get_name (package),
				     zif_package_get_version (package),
				     zif_package_get_arch (package));
	rc = sqlite3_exec (history->priv->db,
			   statement,
			   zif_history_get_string_sqlite_cb,
			   &repo_id,
			   &error_msg);
	if (rc != SQLITE_OK) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_HISTORY_ERROR,
			     ZIF_HISTORY_ERROR_FAILED,
			     "SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}
	if (repo_id == NULL) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_HISTORY_ERROR,
			     ZIF_HISTORY_ERROR_FAILED,
			     "Failed to find %s",
			     zif_package_get_printable (package));
		goto out;
	}
out:
	g_free (statement);
	return repo_id;
}

/**
 * zif_history_import:
 * @history: A #ZifHistory
 * @db: A #ZifDb
 * @error: A #GError, or %NULL
 *
 * Imports a legacy yumdb database into the zif history store
 *
 * Return value: %TRUE on success
 *
 * Since: 0.2.4
 **/
gboolean
zif_history_import (ZifHistory *history,
		    ZifDb *db,
		    GError **error)
{
	gboolean ret;
	guint i;
	GPtrArray *packages = NULL;
	ZifPackage *package;
	ZifTransactionReason reason;
	guint uid;
	guint timestamp;
	gchar *tmp;

	g_return_val_if_fail (ZIF_IS_HISTORY (history), FALSE);
	g_return_val_if_fail (ZIF_IS_DB (db), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure database is loaded */
	ret = zif_history_load (history, error);
	if (!ret)
		goto out;

	/* get all packages in database */
	packages = zif_db_get_packages (db, error);
	if (packages == NULL) {
		ret = FALSE;
		goto out;
	}

	/* import each package */
	for (i = 0; i < packages->len; i++) {
		package = g_ptr_array_index (packages, i);

		g_debug ("Importing %s", zif_package_get_id (package));

		/* get reason */
		reason = ZIF_TRANSACTION_REASON_INVALID;
		tmp = zif_db_get_string (db,
					 package,
					 "reason",
					 NULL);
		if (g_strcmp0 (tmp, "dep") == 0) {
			reason = ZIF_TRANSACTION_REASON_INSTALL_DEPEND;
		} else if (g_strcmp0 (tmp, "user") == 0) {
			reason = ZIF_TRANSACTION_REASON_INSTALL_USER_ACTION;
		}
		g_free (tmp);

		/* get user */
		uid = G_MAXUINT;
		tmp = zif_db_get_string (db,
					 package,
					 "installed_by",
					 NULL);
		if (tmp != NULL)
			uid = atoi (tmp);
		g_free (tmp);

		/* get timestamp */
		timestamp = 0;
		tmp = zif_db_get_string (db,
					 package,
					 "from_repo_timestamp",
					 NULL);
		if (tmp != NULL)
			timestamp = atoi (tmp);
		g_free (tmp);

		/* get repo_id */
		tmp = zif_db_get_string (db,
					 package,
					 "from_repo",
					 NULL);
		if (tmp != NULL)
			zif_package_set_repo_id (package, tmp);
		g_free (tmp);

		/* add to database */
		ret = zif_history_add_entry (history,
					     package,
					     timestamp,
					     reason,
					     uid,
					     "unknown command",
					     error);
		if (!ret)
			goto out;
	}

	/* TODO: set the import time on the database */
out:
	if (packages != NULL)
		g_ptr_array_unref (packages);
	return ret;
}

/**
 * zif_history_set_repo_for_store_sqlite_cb:
 **/
static gint
zif_history_set_repo_for_store_sqlite_cb (void *data,
					  gint argc,
					  gchar **argv,
					  gchar **col_name)
{
	gchar *package_id;
	ZifPackage *package;
	ZifState *state;
	ZifStore *store = (ZifStore *) data;

	/* find the package in the store */
	package_id = zif_package_id_build (argv[0], argv[1], argv[2],
					   zif_store_get_id (store));
	state = zif_state_new ();
	package = zif_store_find_package (store, package_id, state, NULL);
	if (package == NULL)
		goto out;

	/* set the repo it came from */
	g_debug ("set %s on %s", argv[3], package_id);
	zif_package_set_repo_id (package, argv[3]);
out:
	g_object_unref (state);
	if (package != NULL)
		g_object_unref (package);
	return 0;
}

gboolean
zif_history_set_repo_for_store (ZifHistory *history,
				ZifStore *store,
				GError **error)
{
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;

	g_return_val_if_fail (ZIF_IS_HISTORY (history), FALSE);
	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure database is loaded */
	ret = zif_history_load (history, error);
	if (!ret)
		goto out;

	/* return all the different transaction timestamps */
	statement = g_strdup_printf ("SELECT name, version, arch, from_repo "
				     "FROM packages "
				     "ORDER BY timestamp ASC;");
	rc = sqlite3_exec (history->priv->db,
			   statement,
			   zif_history_set_repo_for_store_sqlite_cb,
			   store,
			   &error_msg);
	if (rc != SQLITE_OK) {
		ret = FALSE;
		g_set_error (error,
			     ZIF_HISTORY_ERROR,
			     ZIF_HISTORY_ERROR_FAILED,
			     "SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}
out:
	g_free (statement);
	return ret;
}

/**
 * zif_history_finalize:
 **/
static void
zif_history_finalize (GObject *object)
{
	ZifHistory *history;
	g_return_if_fail (ZIF_IS_HISTORY (object));
	history = ZIF_HISTORY (object);

	g_free (history->priv->filename);

	/* close the database */
	sqlite3_close (history->priv->db);
	g_object_unref (history->priv->config);

	G_OBJECT_CLASS (zif_history_parent_class)->finalize (object);
}

/**
 * zif_history_class_init:
 **/
static void
zif_history_class_init (ZifHistoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_history_finalize;
	g_type_class_add_private (klass, sizeof (ZifHistoryPrivate));
}

/**
 * zif_history_init:
 **/
static void
zif_history_init (ZifHistory *history)
{
	history->priv = ZIF_HISTORY_GET_PRIVATE (history);
	history->priv->config = zif_config_new ();
}

/**
 * zif_history_new:
 *
 * Return value: A new #ZifHistory instance.
 *
 * Since: 0.2.4
 **/
ZifHistory *
zif_history_new (void)
{
	if (zif_history_object != NULL) {
		g_object_ref (zif_history_object);
	} else {
		zif_history_object = g_object_new (ZIF_TYPE_HISTORY, NULL);
		g_object_add_weak_pointer (zif_history_object, &zif_history_object);
	}
	return ZIF_HISTORY (zif_history_object);
}

