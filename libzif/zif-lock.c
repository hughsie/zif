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
 * SECTION:zif-lock
 * @short_description: Lock the package system
 *
 * This object works with the generic lock file.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <sys/types.h>
#include <unistd.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "zif-lock.h"
#include "zif-config.h"

#define ZIF_LOCK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_LOCK, ZifLockPrivate))

/**
 * ZifLockPrivate:
 *
 * Private #ZifLock data
 **/
struct _ZifLockPrivate
{
	ZifConfig		*config;
	guint			 refcount[ZIF_LOCK_TYPE_LAST];
};

static gpointer zif_lock_object = NULL;

G_DEFINE_TYPE (ZifLock, zif_lock, G_TYPE_OBJECT)

/**
 * zif_lock_error_quark:
 *
 * Return value: An error quark.
 *
 * Since: 0.1.0
 **/
GQuark
zif_lock_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("zif_lock_error");
	return quark;
}

/**
 * zif_lock_is_instance_valid:
 *
 * Return value: %TRUE if a singleton instance already exists
 *
 * Since: 0.1.6
 **/
gboolean
zif_lock_is_instance_valid (void)
{
	return (zif_lock_object != NULL);
}

/**
 * zif_lock_type_to_string:
 *
 * Return value: The string representation of the type
 *
 * Since: 0.1.6
 **/
const gchar *
zif_lock_type_to_string (ZifLockType lock_type)
{
	if (lock_type == ZIF_LOCK_TYPE_RPMDB_WRITE)
		return "rpmdb-write";
	if (lock_type == ZIF_LOCK_TYPE_REPO_WRITE)
		return "repo-write";
	if (lock_type == ZIF_LOCK_TYPE_METADATA_WRITE)
		return "metadata-write";
	return "unknown";
}

/**
 * zif_lock_get_pid:
 **/
static guint
zif_lock_get_pid (ZifLock *lock, const gchar *filename, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	guint64 pid = 0;
	gchar *contents = NULL;
	gchar *endptr = NULL;

	g_return_val_if_fail (ZIF_IS_LOCK (lock), FALSE);

	/* file doesn't exists */
	ret = g_file_test (filename, G_FILE_TEST_EXISTS);
	if (!ret) {
		g_set_error_literal (error,
				     ZIF_LOCK_ERROR,
				     ZIF_LOCK_ERROR_FAILED,
				     "lock file not present");
		goto out;
	}

	/* get contents */
	ret = g_file_get_contents (filename, &contents, NULL, &error_local);
	if (!ret) {
		g_set_error (error,
			     ZIF_LOCK_ERROR,
			     ZIF_LOCK_ERROR_FAILED,
			     "lock file not set: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* convert to int */
	pid = g_ascii_strtoull (contents, &endptr, 10);

	/* failed to parse */
	if (contents == endptr) {
		g_set_error (error, ZIF_LOCK_ERROR, ZIF_LOCK_ERROR_FAILED,
			     "failed to parse pid: %s", contents);
		pid = 0;
		goto out;
	}

	/* too large */
	if (pid > G_MAXUINT) {
		g_set_error (error, ZIF_LOCK_ERROR, ZIF_LOCK_ERROR_FAILED,
			     "pid too large %" G_GUINT64_FORMAT, pid);
		pid = 0;
		goto out;
	}
out:
	g_free (contents);
	return (guint) pid;
}

/**
 * zif_lock_get_filename_for_type:
 **/
static gchar *
zif_lock_get_filename_for_type (ZifLock *lock,
				ZifLockType type,
				GError **error)
{
	gboolean compat_mode;
	gchar *pidfile = NULL;
	gchar *filename = NULL;

	/* get the lock file root */
	pidfile = zif_config_get_string (lock->priv->config,
					 "pidfile", error);
	if (pidfile == NULL)
		goto out;

	/* get filename */
	compat_mode = zif_config_get_boolean (lock->priv->config,
					      "lock_compat", NULL);
	if (compat_mode) {
		filename = g_strdup_printf ("%s.lock", pidfile);
	} else {
		filename = g_strdup_printf ("%s-%s.lock",
					    pidfile,
					    zif_lock_type_to_string (type));
	}
out:
	g_free (pidfile);
	return filename;
}

/**
 * zif_lock_get_cmdline_for_pid:
 **/
static gchar *
zif_lock_get_cmdline_for_pid (guint pid)
{
	gboolean ret;
	gchar *filename = NULL;
	gchar *data = NULL;
	gchar *cmdline = NULL;
	GError *error = NULL;

	/* find the cmdline */
	filename = g_strdup_printf ("/proc/%i/cmdline", pid);
	ret = g_file_get_contents (filename, &data, NULL, &error);
	if (ret) {
		cmdline = g_strdup_printf ("%s (%i)", data, pid);
	} else {
		g_warning ("failed to get cmdline: %s", error->message);
		cmdline = g_strdup_printf ("unknown (%i)", pid);
	}
	g_free (data);
	return cmdline;
}

/**
 * zif_lock_take:
 * @lock: A #ZifLock
 * @type: A ZifLockType, e.g. %ZIF_LOCK_TYPE_RPMDB_WRITE
 * @error: A #GError, or %NULL
 *
 * Tries to take a lock for the packaging system.
 *
 * Return value: %TRUE if we locked, else %FALSE and the error is set
 *
 * Since: 0.1.6
 **/
gboolean
zif_lock_take (ZifLock *lock, ZifLockType type, GError **error)
{
	gboolean ret = FALSE;
	gchar *cmdline = NULL;
	gchar *filename = NULL;
	gchar *pid_filename = NULL;
	gchar *pid_text = NULL;
	GError *error_local = NULL;
	guint pid;

	g_return_val_if_fail (ZIF_IS_LOCK (lock), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (lock->priv->refcount[type] == 0) {

		/* get the lock filename */
		filename = zif_lock_get_filename_for_type (lock,
							   type,
							   error);
		if (filename == NULL)
			goto out;

		/* does file already exists? */
		if (g_file_test (filename, G_FILE_TEST_EXISTS)) {

			/* check the pid is still valid */
			pid = zif_lock_get_pid (lock, filename, error);
			if (pid == 0)
				goto out;

			/* pid is not still running? */
			pid_filename = g_strdup_printf ("/proc/%i/cmdline",
							pid);
			ret = g_file_test (pid_filename, G_FILE_TEST_EXISTS);
			if (ret) {
				ret = FALSE;
				cmdline = zif_lock_get_cmdline_for_pid (pid);
				g_set_error (error,
					     ZIF_LOCK_ERROR,
					     ZIF_LOCK_ERROR_ALREADY_LOCKED,
					     "already locked by %s",
					     cmdline);
				goto out;
			}
		}

		/* create file with our process ID */
		pid_text = g_strdup_printf ("%i", getpid ());
		ret = g_file_set_contents (filename,
					   pid_text,
					   -1,
					   &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_LOCK_ERROR,
				     ZIF_LOCK_ERROR_PERMISSION,
				     "failed to obtain lock '%s': %s",
				     zif_lock_type_to_string (type),
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* increment ref count */
	lock->priv->refcount[type]++;

	/* success */
	ret = TRUE;
out:
	g_free (pid_text);
	g_free (pid_filename);
	g_free (filename);
	g_free (cmdline);
	return ret;
}

/**
 * zif_lock_release:
 * @lock: A #ZifLock
 * @type: A ZifLockType, e.g. %ZIF_LOCK_TYPE_RPMDB_WRITE
 * @error: A #GError, or %NULL
 *
 * Tries to release a lock for the packaging system.
 *
 * Return value: %TRUE if we locked, else %FALSE and the error is set
 *
 * Since: 0.1.6
 **/
gboolean
zif_lock_release (ZifLock *lock, ZifLockType type, GError **error)
{
	gboolean ret = FALSE;
	gchar *filename = NULL;
	GError *error_local = NULL;
	GFile *file = NULL;

	g_return_val_if_fail (ZIF_IS_LOCK (lock), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* never took */
	if (lock->priv->refcount[type] == 0) {
		g_set_error (error,
			     ZIF_LOCK_ERROR,
			     ZIF_LOCK_ERROR_NOT_LOCKED,
			     "Lock %s was never taken",
			     zif_lock_type_to_string (type));
		goto out;
	}

	/* idecrement ref count */
	lock->priv->refcount[type]--;

	/* delete file? */
	if (lock->priv->refcount[type] == 0) {
		/* get the lock filename */
		filename = zif_lock_get_filename_for_type (lock,
							   type,
							   error);
		if (filename == NULL)
			goto out;

		/* unlink */
		file = g_file_new_for_path (filename);
		ret = g_file_delete (file, NULL, &error_local);
		if (!ret) {
			g_set_error (error,
				     ZIF_LOCK_ERROR,
				     ZIF_LOCK_ERROR_PERMISSION,
				     "failed to write: %s",
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* success */
	ret = TRUE;
out:
	if (file != NULL)
		g_object_unref (file);
	g_free (filename);
	return ret;
}

/**
 * zif_lock_is_locked:
 * @lock: A #ZifLock
 * @pid: The PID of the process holding the lock, or %NULL
 *
 * Gets the lock state.
 *
 * This function is DEPRECATED as it's not threadsafe.
 *
 * Return value: %TRUE if we are already locked
 *
 * Since: 0.1.0
 **/
gboolean
zif_lock_is_locked (ZifLock *lock, guint *pid)
{
	g_return_val_if_fail (ZIF_IS_LOCK (lock), FALSE);

	g_warning ("Do not use zif_lock_is_locked(), it's deprecated");
	return (lock->priv->refcount[ZIF_LOCK_TYPE_RPMDB_WRITE] > 0);
}

/**
 * zif_lock_set_locked:
 * @lock: A #ZifLock
 * @pid: A PID of the process holding the lock, or %NULL
 * @error: A #GError, or %NULL
 *
 * Tries to lock the packaging system.
 *
 * This function is DEPRECATED. Use zif_lock_take() instead.
 *
 * Return value: %TRUE if we locked, else %FALSE and the error is set
 *
 * Since: 0.1.0
 **/
gboolean
zif_lock_set_locked (ZifLock *lock, guint *pid, GError **error)
{
	g_return_val_if_fail (ZIF_IS_LOCK (lock), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	g_warning ("Do not use zif_lock_set_locked(), it's deprecated");
	return zif_lock_take (lock, ZIF_LOCK_TYPE_RPMDB_WRITE, error);
}

/**
 * zif_lock_set_unlocked:
 * @lock: A #ZifLock
 * @error: A #GError, or %NULL
 *
 * Unlocks the packaging system.
 *
 * This function is DEPRECATED. Use zif_lock_take() instead.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_lock_set_unlocked (ZifLock *lock, GError **error)
{
	g_return_val_if_fail (ZIF_IS_LOCK (lock), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	g_warning ("Do not use zif_lock_set_unlocked(), it's deprecated");
	return zif_lock_release (lock, ZIF_LOCK_TYPE_RPMDB_WRITE, error);
}

/**
 * zif_lock_finalize:
 **/
static void
zif_lock_finalize (GObject *object)
{
	guint i;
	ZifLock *lock;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_LOCK (object));
	lock = ZIF_LOCK (object);

	/* unlock if we hold the lock */
	for (i=0; i<ZIF_LOCK_TYPE_LAST; i++) {
		if (lock->priv->refcount[i] > 0) {
			g_warning ("held lock %s at shutdown",
				   zif_lock_type_to_string (i));
			zif_lock_release (lock, i, NULL);
		}
	}

	g_object_unref (lock->priv->config);

	G_OBJECT_CLASS (zif_lock_parent_class)->finalize (object);
}

/**
 * zif_lock_class_init:
 **/
static void
zif_lock_class_init (ZifLockClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_lock_finalize;

	g_type_class_add_private (klass, sizeof (ZifLockPrivate));
}

/**
 * zif_lock_init:
 **/
static void
zif_lock_init (ZifLock *lock)
{
	lock->priv = ZIF_LOCK_GET_PRIVATE (lock);
	lock->priv->config = zif_config_new ();
}

/**
 * zif_lock_new:
 *
 * Return value: A new lock instance.
 *
 * Since: 0.1.0
 **/
ZifLock *
zif_lock_new (void)
{
	if (zif_lock_object != NULL) {
		g_object_ref (zif_lock_object);
	} else {
		zif_lock_object = g_object_new (ZIF_TYPE_LOCK, NULL);
		g_object_add_weak_pointer (zif_lock_object, &zif_lock_object);
	}
	return ZIF_LOCK (zif_lock_object);
}

