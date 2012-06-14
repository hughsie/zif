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
	GMutex			 mutex;
	ZifConfig		*config;
	GPtrArray		*item_array;
};

typedef struct {
	gpointer		 owner;
	guint			 id;
	guint			 refcount;
	ZifLockMode		 mode;
	ZifLockType		 type;
} ZifLockItem;

enum {
	SIGNAL_STATE_CHANGED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

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
	if (lock_type == ZIF_LOCK_TYPE_RPMDB)
		return "rpmdb";
	if (lock_type == ZIF_LOCK_TYPE_REPO)
		return "repo";
	if (lock_type == ZIF_LOCK_TYPE_METADATA)
		return "metadata";
	if (lock_type == ZIF_LOCK_TYPE_GROUPS)
		return "groups";
	if (lock_type == ZIF_LOCK_TYPE_RELEASE)
		return "release";
	if (lock_type == ZIF_LOCK_TYPE_CONFIG)
		return "config";
	if (lock_type == ZIF_LOCK_TYPE_HISTORY)
		return "history";
	return "unknown";
}

/**
 * zif_lock_get_item_by_type_mode:
 **/
static ZifLockItem *
zif_lock_get_item_by_type_mode (ZifLock *lock,
				ZifLockType type,
				ZifLockMode mode)
{
	ZifLockItem *item;
	guint i;

	/* search for the item that matches type */
	for (i = 0; i < lock->priv->item_array->len; i++) {
		item = g_ptr_array_index (lock->priv->item_array, i);
		if (item->type == type && item->mode == mode)
			return item;
	}
	return NULL;
}

/**
 * zif_lock_get_item_by_id:
 **/
static ZifLockItem *
zif_lock_get_item_by_id (ZifLock *lock, guint id)
{
	ZifLockItem *item;
	guint i;

	/* search for the item that matches the ID */
	for (i = 0; i < lock->priv->item_array->len; i++) {
		item = g_ptr_array_index (lock->priv->item_array, i);
		if (item->id == id)
			return item;
	}
	return NULL;
}

/**
 * zif_lock_create_item:
 **/
static ZifLockItem *
zif_lock_create_item (ZifLock *lock, ZifLockType type, ZifLockMode mode)
{
	static guint id = 1;
	ZifLockItem *item;

	item = g_new0 (ZifLockItem, 1);
	item->id = id++;
	item->type = type;
	item->owner = g_thread_self ();
	item->refcount = 1;
	item->mode = mode;
	g_ptr_array_add (lock->priv->item_array, item);
	return item;
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
 * zif_lock_get_state:
 * @lock: A #ZifLock
 *
 * Gets a bitfield of what locks have been taken
 *
 * Return value: A bitfield.
 *
 * Since: 0.3.0
 **/
guint
zif_lock_get_state (ZifLock *lock)
{
	guint bitfield = 0;
	guint i;
	ZifLockItem *item;

	g_return_val_if_fail (ZIF_IS_LOCK (lock), FALSE);

	for (i = 0; i < lock->priv->item_array->len; i++) {
		item = g_ptr_array_index (lock->priv->item_array, i);
		bitfield += 1 << item->type;
	}
	return bitfield;
}

/**
 * zif_lock_emit_state:
 **/
static void
zif_lock_emit_state (ZifLock *lock)
{
	guint bitfield = 0;
	bitfield = zif_lock_get_state (lock);
	g_signal_emit (lock, signals [SIGNAL_STATE_CHANGED], 0, bitfield);
}

/**
 * zif_lock_take:
 * @lock: A #ZifLock
 * @type: A #ZifLockType, e.g. %ZIF_LOCK_TYPE_RPMDB
 * @mode: A #ZifLockMode, e.g. %ZIF_LOCK_MODE_PROCESS
 * @error: A #GError, or %NULL
 *
 * Tries to take a lock for the packaging system.
 *
 * Return value: A lock ID greater than 0, or 0 for an error.
 *
 * Since: 0.3.1
 **/
guint
zif_lock_take (ZifLock *lock,
	       ZifLockType type,
	       ZifLockMode mode,
	       GError **error)
{
	gboolean ret;
	gchar *cmdline = NULL;
	gchar *filename = NULL;
	gchar *pid_filename = NULL;
	gchar *pid_text = NULL;
	GError *error_local = NULL;
	guint id = 0;
	guint pid;
	ZifLockItem *item;

	g_return_val_if_fail (ZIF_IS_LOCK (lock), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* lock other threads */
	g_mutex_lock (&lock->priv->mutex);

	/* find the lock type, and ensure we find a process lock for
	 * a thread lock */
	item = zif_lock_get_item_by_type_mode (lock, type, mode);
	if (item == NULL && mode == ZIF_LOCK_MODE_THREAD) {
		item = zif_lock_get_item_by_type_mode (lock,
						       type,
						       ZIF_LOCK_MODE_PROCESS);
	}

	/* create a lock file for process locks */
	if (item == NULL && mode == ZIF_LOCK_MODE_PROCESS) {

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

	/* create new lock */
	if (item == NULL) {
		item = zif_lock_create_item (lock, type, mode);
		id = item->id;
		zif_lock_emit_state (lock);
		goto out;
	}

	/* we're trying to lock something that's already locked
	 * in another thread */
	if (item->owner != g_thread_self ()) {
		g_set_error (error,
			     ZIF_LOCK_ERROR,
			     ZIF_LOCK_ERROR_FAILED,
			     "failed to obtain lock '%s' already taken by thread %p",
			     zif_lock_type_to_string (type),
			     item->owner);
		goto out;
	}

	/* increment ref count */
	item->refcount++;

	/* emit the new locking bitfield */
	zif_lock_emit_state (lock);

	/* success */
	id = item->id;
out:
	/* unlock other threads */
	g_mutex_unlock (&lock->priv->mutex);

	g_free (pid_text);
	g_free (pid_filename);
	g_free (filename);
	g_free (cmdline);
	return id;
}

/**
 * zif_lock_release:
 * @lock: A #ZifLock
 * @id: A lock ID, as given by zif_lock_take()
 * @error: A #GError, or %NULL
 *
 * Tries to release a lock for the packaging system.
 *
 * Return value: %TRUE if we locked, else %FALSE and the error is set
 *
 * Since: 0.3.1
 **/
gboolean
zif_lock_release (ZifLock *lock, guint id, GError **error)
{
	gboolean ret = FALSE;
	gchar *filename = NULL;
	GError *error_local = NULL;
	GFile *file = NULL;
	ZifLockItem *item;

	g_return_val_if_fail (ZIF_IS_LOCK (lock), FALSE);
	g_return_val_if_fail (id != 0, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* lock other threads */
	g_mutex_lock (&lock->priv->mutex);

	/* never took */
	item = zif_lock_get_item_by_id (lock, id);
	if (item == NULL) {
		g_set_error (error,
			     ZIF_LOCK_ERROR,
			     ZIF_LOCK_ERROR_NOT_LOCKED,
			     "Lock was never taken with id %i", id);
		goto out;
	}

	/* not the same thread */
	if (item->owner != g_thread_self ()) {
		g_set_error (error,
			     ZIF_LOCK_ERROR,
			     ZIF_LOCK_ERROR_NOT_LOCKED,
			     "Lock %s was not taken by this thread",
			     zif_lock_type_to_string (item->type));
		goto out;
	}

	/* idecrement ref count */
	item->refcount--;

	/* delete file for process locks */
	if (item->refcount == 0 &&
	    item->mode == ZIF_LOCK_MODE_PROCESS) {

		/* get the lock filename */
		filename = zif_lock_get_filename_for_type (lock,
							   item->type,
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

	/* no thread now owns this lock */
	if (item->refcount == 0)
		g_ptr_array_remove (lock->priv->item_array, item);

	/* emit the new locking bitfield */
	zif_lock_emit_state (lock);

	/* success */
	ret = TRUE;
out:
	/* unlock other threads */
	g_mutex_unlock (&lock->priv->mutex);
	if (file != NULL)
		g_object_unref (file);
	g_free (filename);
	return ret;
}

/**
 * zif_lock_release_noerror:
 * @lock: A #ZifLock
 * @id: A lock ID, as given by zif_lock_take()
 *
 * Tries to release a lock for the packaging system. This method
 * should not be used lightly as no error will be returned.
 *
 * Since: 0.3.1
 **/
void
zif_lock_release_noerror (ZifLock *lock, guint id)
{
	gboolean ret;
	GError *error = NULL;
	ret = zif_lock_release (lock, id, &error);
	if (!ret) {
		g_warning ("Handled locally: %s", error->message);
		g_error_free (error);
	}
}

/**
 * zif_lock_finalize:
 **/
static void
zif_lock_finalize (GObject *object)
{
	guint i;
	ZifLock *lock;
	ZifLockItem *item;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_LOCK (object));
	lock = ZIF_LOCK (object);

	/* unlock if we hold the lock */
	for (i = 0; i < lock->priv->item_array->len; i++) {
		item = g_ptr_array_index (lock->priv->item_array, i);
		if (item->refcount > 0) {
			g_warning ("held lock %s at shutdown",
				   zif_lock_type_to_string (item->type));
			zif_lock_release (lock, item->type, NULL);
		}
	}

	g_object_unref (lock->priv->config);
	g_ptr_array_unref (lock->priv->item_array);

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

	signals [SIGNAL_STATE_CHANGED] =
		g_signal_new ("state-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ZifLockClass, state_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

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
	lock->priv->item_array = g_ptr_array_new_with_free_func (g_free);
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

