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
	gchar			*filename;
	ZifConfig		*config;
	gboolean		 self_locked;
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
 * zif_lock_get_pid:
 **/
static guint
zif_lock_get_pid (ZifLock *lock, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	guint64 pid = 0;
	gchar *contents = NULL;
	gchar *endptr = NULL;

	g_return_val_if_fail (ZIF_IS_LOCK (lock), FALSE);

	/* get default filename */
	if (lock->priv->filename == NULL)
		lock->priv->filename = zif_config_get_string (lock->priv->config, "pidfile", &error_local);
	if (lock->priv->filename == NULL) {
		g_set_error (error, ZIF_LOCK_ERROR, ZIF_LOCK_ERROR_FAILED,
			     "lock file not set: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* file doesn't exists */
	ret = g_file_test (lock->priv->filename, G_FILE_TEST_EXISTS);
	if (!ret) {
		g_set_error_literal (error, ZIF_LOCK_ERROR, ZIF_LOCK_ERROR_FAILED,
				     "lock file not present");
		goto out;
	}

	/* get contents */
	ret = g_file_get_contents (lock->priv->filename, &contents, NULL, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_LOCK_ERROR, ZIF_LOCK_ERROR_FAILED,
			     "lock file not set: %s", error_local->message);
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
 * zif_lock_is_locked:
 * @lock: A #ZifLock
 * @pid: The PID of the process holding the lock, or %NULL
 *
 * Gets the lock state.
 *
 * Return value: %TRUE if we are already locked
 *
 * Since: 0.1.0
 **/
gboolean
zif_lock_is_locked (ZifLock *lock, guint *pid)
{
	guint pid_tmp;
	gboolean ret = FALSE;
	gchar *filename = NULL;

	g_return_val_if_fail (ZIF_IS_LOCK (lock), FALSE);

	/* optimise as we hold the lock */
	if (lock->priv->self_locked) {
		ret = TRUE;
		if (pid != NULL)
			*pid = getpid ();
		goto out;
	}

	/* get pid */
	pid_tmp = zif_lock_get_pid (lock, NULL);
	if (pid_tmp == 0)
		goto out;

	/* pid is not still running? */
	filename = g_strdup_printf ("/proc/%i/cmdline", pid_tmp);
	ret = g_file_test (filename, G_FILE_TEST_EXISTS);
	if (!ret)
		goto out;

	/* return pid */
	if (pid != NULL)
		*pid = pid_tmp;
out:
	g_free (filename);
	return ret;
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
 * zif_lock_set_locked:
 * @lock: A #ZifLock
 * @pid: A PID of the process holding the lock, or %NULL
 * @error: A #GError, or %NULL
 *
 * Tries to lock the packaging system.
 *
 * Return value: %TRUE if we locked, else %FALSE and the error is set
 *
 * Since: 0.1.0
 **/
gboolean
zif_lock_set_locked (ZifLock *lock, guint *pid, GError **error)
{
	gboolean ret = FALSE;
	guint pid_tmp = 0;
	gchar *pid_text = NULL;
	gchar *cmdline = NULL;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_LOCK (lock), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already locked */
	ret = zif_lock_is_locked (lock, &pid_tmp);
	if (ret) {
		cmdline = zif_lock_get_cmdline_for_pid (pid_tmp);
		g_set_error (error, ZIF_LOCK_ERROR, ZIF_LOCK_ERROR_ALREADY_LOCKED,
			     "already locked by %s", cmdline);
		if (pid != NULL)
			*pid = pid_tmp;
		ret = FALSE;
		goto out;
	}

	/* get default filename */
	if (lock->priv->filename == NULL)
		lock->priv->filename = zif_config_get_string (lock->priv->config, "pidfile", &error_local);
	if (lock->priv->filename == NULL) {
		g_set_error (error, ZIF_LOCK_ERROR, ZIF_LOCK_ERROR_FAILED,
			     "lock file not set: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* save our pid */
	pid_tmp = getpid ();
	pid_text = g_strdup_printf ("%i", pid_tmp);
	ret = g_file_set_contents (lock->priv->filename, pid_text, -1, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_LOCK_ERROR, ZIF_LOCK_ERROR_PERMISSION,
			     "failed to write: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* optimise as we now hold the lock */
	lock->priv->self_locked = TRUE;

	/* return pid */
	if (pid != NULL)
		*pid = pid_tmp;
out:
	g_free (pid_text);
	g_free (cmdline);
	return ret;
}

/**
 * zif_lock_set_unlocked:
 * @lock: A #ZifLock
 * @error: A #GError, or %NULL
 *
 * Unlocks the packaging system.
 *
 * Return value: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.1.0
 **/
gboolean
zif_lock_set_unlocked (ZifLock *lock, GError **error)
{
	gboolean ret = FALSE;
	guint pid = 0;
	guint pid_tmp;
	gint retval;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_LOCK (lock), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* optimise as we hold the lock */
	if (lock->priv->self_locked) {
		lock->priv->self_locked = FALSE;
		goto skip_checks;
	}

	/* are we already locked */
	ret = zif_lock_is_locked (lock, &pid);
	if (!ret) {
		g_set_error_literal (error, ZIF_LOCK_ERROR, ZIF_LOCK_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* is it locked by somethine that isn't us? */
	pid_tmp = getpid ();
	if (pid != pid_tmp) {
		g_set_error (error, ZIF_LOCK_ERROR, ZIF_LOCK_ERROR_ALREADY_LOCKED,
			     "locked by %i, cannot unlock", pid_tmp);
		ret = FALSE;
		goto out;
	}

skip_checks:

	/* get default filename */
	if (lock->priv->filename == NULL)
		lock->priv->filename = zif_config_get_string (lock->priv->config, "pidfile", &error_local);
	if (lock->priv->filename == NULL) {
		g_set_error (error, ZIF_LOCK_ERROR, ZIF_LOCK_ERROR_FAILED,
			     "lock file not set: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* remove file */
	retval = g_unlink (lock->priv->filename);
	if (retval != 0) {
		g_set_error (error, ZIF_LOCK_ERROR, ZIF_LOCK_ERROR_FAILED,
			     "cannot remove %s, cannot unlock", lock->priv->filename);
		ret = FALSE;
		goto out;
	}

	/* success */
	ret = TRUE;
out:
	return ret;
}

/**
 * zif_lock_finalize:
 **/
static void
zif_lock_finalize (GObject *object)
{
	ZifLock *lock;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_LOCK (object));
	lock = ZIF_LOCK (object);

	/* unlock if we hold the lock */
	if (lock->priv->self_locked)
		zif_lock_set_unlocked (lock, NULL);

	g_free (lock->priv->filename);
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
	lock->priv->self_locked = FALSE;
	lock->priv->config = zif_config_new ();
	lock->priv->filename = NULL;
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

