/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#if !defined (__DUM_H_INSIDE__) && !defined (DUM_COMPILATION)
#error "Only <dum.h> can be included directly."
#endif

#ifndef __DUM_DOWNLOAD_H
#define __DUM_DOWNLOAD_H

#include <glib-object.h>

G_BEGIN_DECLS

#define DUM_TYPE_DOWNLOAD		(dum_download_get_type ())
#define DUM_DOWNLOAD(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), DUM_TYPE_DOWNLOAD, DumDownload))
#define DUM_DOWNLOAD_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), DUM_TYPE_DOWNLOAD, DumDownloadClass))
#define DUM_IS_DOWNLOAD(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), DUM_TYPE_DOWNLOAD))
#define DUM_IS_DOWNLOAD_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), DUM_TYPE_DOWNLOAD))
#define DUM_DOWNLOAD_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), DUM_TYPE_DOWNLOAD, DumDownloadClass))

typedef struct DumDownloadPrivate DumDownloadPrivate;

typedef struct
{
	GObject				 parent;
	DumDownloadPrivate		*priv;
} DumDownload;

typedef struct
{
	GObjectClass	parent_class;
	/* Signals */
	void		(* percentage_changed)		(DumDownload	*download,
							 guint		 value);
	/* Padding for future expansion */
	void (*_dum_reserved1) (void);
	void (*_dum_reserved2) (void);
	void (*_dum_reserved3) (void);
	void (*_dum_reserved4) (void);
} DumDownloadClass;

GType		 dum_download_get_type			(void) G_GNUC_CONST;
DumDownload	*dum_download_new			(void);
gboolean	 dum_download_set_proxy			(DumDownload		*download,
							 const gchar		*http_proxy,
							 GError			**error);
gboolean	 dum_download_file			(DumDownload		*download,
							 const gchar		*uri,
							 const gchar		*filename,
							 GError			**error);
gboolean	 dum_download_cancel			(DumDownload		*download,
							 GError			**error);

G_END_DECLS

#endif /* __DUM_DOWNLOAD_H */

