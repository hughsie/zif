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

#if !defined (__ZIF_H_INSIDE__) && !defined (ZIF_COMPILATION)
#error "Only <zif.h> can be included directly."
#endif

#ifndef __ZIF_STATE_H
#define __ZIF_STATE_H

#include <glib-object.h>
#include <gio/gio.h>

#include "zif-lock.h"

G_BEGIN_DECLS

#define ZIF_TYPE_STATE		(zif_state_get_type ())
#define ZIF_STATE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_STATE, ZifState))
#define ZIF_STATE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_STATE, ZifStateClass))
#define ZIF_IS_STATE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_STATE))
#define ZIF_IS_STATE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_STATE))
#define ZIF_STATE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_STATE, ZifStateClass))
#define ZIF_STATE_ERROR		(zif_state_error_quark ())

typedef struct _ZifState	ZifState;
typedef struct _ZifStatePrivate	ZifStatePrivate;
typedef struct _ZifStateClass	ZifStateClass;

struct _ZifState
{
	GObject			 parent;
	ZifStatePrivate	*priv;
};

typedef enum {
	ZIF_STATE_ACTION_DOWNLOADING,
	ZIF_STATE_ACTION_CHECKING,
	ZIF_STATE_ACTION_LOADING_REPOS,
	ZIF_STATE_ACTION_DECOMPRESSING,
	ZIF_STATE_ACTION_DEPSOLVING_CONFLICTS,
	ZIF_STATE_ACTION_DEPSOLVING_INSTALL,
	ZIF_STATE_ACTION_DEPSOLVING_REMOVE,
	ZIF_STATE_ACTION_DEPSOLVING_UPDATE,
	ZIF_STATE_ACTION_PREPARING,
	ZIF_STATE_ACTION_INSTALLING,
	ZIF_STATE_ACTION_REMOVING,
	ZIF_STATE_ACTION_UPDATING,
	ZIF_STATE_ACTION_CLEANING,
	ZIF_STATE_ACTION_TEST_COMMIT,
	ZIF_STATE_ACTION_LOADING_RPMDB,		/* Since: 0.2.4 */
	ZIF_STATE_ACTION_CHECKING_UPDATES,	/* Since: 0.2.4 */
	ZIF_STATE_ACTION_UNKNOWN
} ZifStateAction;

struct _ZifStateClass
{
	GObjectClass	 parent_class;
	/* Signals */
	void		(* percentage_changed)		(ZifState	*state,
							 guint		 value);
	void		(* subpercentage_changed)	(ZifState	*state,
							 guint		 value);
	void		(* allow_cancel_changed)	(ZifState	*state,
							 gboolean	 allow_cancel);
	void		(* action_changed)		(ZifState	*state,
							 ZifStateAction	 action,
							 const gchar	*action_hint);
	void		(* package_progress_changed)	(ZifState	*state,
							 const gchar	*package_id,
							 ZifStateAction	 action,
							 guint		 percentage);
	/* Padding for future expansion */
	void (*_zif_reserved1) (void);
	void (*_zif_reserved2) (void);
	void (*_zif_reserved3) (void);
	void (*_zif_reserved4) (void);
};

typedef enum {
	ZIF_STATE_ERROR_CANCELLED,
	ZIF_STATE_ERROR_INVALID,
	ZIF_STATE_ERROR_LAST
} ZifStateError;

#define zif_state_done(state, error)			zif_state_done_real(state, error, G_STRLOC)
#define zif_state_finished(state, error)		zif_state_finished_real(state, error, G_STRLOC)
#define zif_state_set_number_steps(state, steps)	zif_state_set_number_steps_real(state, steps, G_STRLOC)
#define zif_state_set_steps(state, error, value, args...)	zif_state_set_steps_real(state, error, G_STRLOC, value, ## args)

typedef gboolean (*ZifStateErrorHandlerCb)		(const GError		*error,
							 gpointer		 user_data);
typedef gboolean (*ZifStateLockHandlerCb)		(ZifState		*state,
							 ZifLock		*lock,
							 ZifLockType		 lock_type,
							 GError			**error,
							 gpointer		 user_data);

GType		 zif_state_get_type			(void);
GQuark		 zif_state_error_quark			(void);
ZifState	*zif_state_new				(void);
ZifState	*zif_state_get_child			(ZifState		*state);

/* percentage changed */
void		 zif_state_set_report_progress		(ZifState		*state,
							 gboolean		 report_progress);
gboolean	 zif_state_set_number_steps_real	(ZifState		*state,
							 guint			 steps,
							 const gchar		*strloc);
gboolean	 zif_state_set_steps_real		(ZifState		*state,
							 GError			**error,
							 const gchar		*strloc,
							 gint			 value, ...);
gboolean	 zif_state_set_percentage		(ZifState		*state,
							 guint			 percentage);
void		 zif_state_set_package_progress		(ZifState		*state,
							 const gchar		*package_id,
							 ZifStateAction		 action,
							 guint			 percentage);
guint		 zif_state_get_percentage		(ZifState		*state);
gboolean	 zif_state_action_start			(ZifState		*state,
							 ZifStateAction		 action,
							 const gchar		*action_hint);
gboolean	 zif_state_action_stop			(ZifState		*state);
const gchar	*zif_state_action_to_string		(ZifStateAction		 action);
ZifStateAction	 zif_state_get_action			(ZifState		*state);
const gchar	*zif_state_get_action_hint		(ZifState		*state);
gboolean	 zif_state_check			(ZifState		*state,
							 GError			 **error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 zif_state_done_real			(ZifState		*state,
							 GError			 **error,
							 const gchar		*strloc)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 zif_state_finished_real		(ZifState		*state,
							 GError			 **error,
							 const gchar		*strloc)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 zif_state_reset			(ZifState		*state);
gboolean	 zif_state_valid			(ZifState		*state);
void		 zif_state_set_enable_profile		(ZifState		*state,
							 gboolean		 enable_profile);

/* cancellation */
GCancellable	*zif_state_get_cancellable		(ZifState		*state);
void		 zif_state_set_cancellable		(ZifState		*state,
							 GCancellable		*cancellable);
gboolean	 zif_state_get_allow_cancel		(ZifState		*state);
void		 zif_state_set_allow_cancel		(ZifState		*state,
							 gboolean		 allow_cancel);
guint64		 zif_state_get_speed			(ZifState		*state);
void		 zif_state_set_speed			(ZifState		*state,
							 guint64		 speed);

/* error handling */
void		 zif_state_set_error_handler		(ZifState		*state,
							 ZifStateErrorHandlerCb	 error_handler_cb,
							 gpointer		 user_data);
gboolean	 zif_state_error_handler		(ZifState		*state,
							 const GError		*error);

/* lock handling */
void		 zif_state_set_lock_handler		(ZifState		*state,
							 ZifStateLockHandlerCb	 lock_handler_cb,
							 gpointer		 user_data);
gboolean	 zif_state_take_lock			(ZifState		*state,
							 ZifLockType		 lock_type,
							 ZifLockMode		 lock_mode,
							 GError			**error);

G_END_DECLS

#endif /* __ZIF_STATE_H */

