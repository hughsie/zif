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

#ifndef __DUM_REPO_MD_H
#define __DUM_REPO_MD_H

#include <glib-object.h>

#include "dum-repo-md.h"

G_BEGIN_DECLS

#define DUM_TYPE_REPO_MD		(dum_repo_md_get_type ())
#define DUM_REPO_MD(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), DUM_TYPE_REPO_MD, DumRepoMd))
#define DUM_REPO_MD_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), DUM_TYPE_REPO_MD, DumRepoMdClass))
#define DUM_IS_REPO_MD(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), DUM_TYPE_REPO_MD))
#define DUM_IS_REPO_MD_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), DUM_TYPE_REPO_MD))
#define DUM_REPO_MD_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), DUM_TYPE_REPO_MD, DumRepoMdClass))

typedef struct DumRepoMdPrivate DumRepoMdPrivate;

typedef struct
{
	GObject			 parent;
	DumRepoMdPrivate	*priv;
} DumRepoMd;

typedef struct
{
	GObjectClass	parent_class;
	/* vtable */
	gboolean	 (*load)		(DumRepoMd		*md,
						 GError			**error);
} DumRepoMdClass;

typedef enum {
	DUM_REPO_MD_TYPE_PRIMARY,
	DUM_REPO_MD_TYPE_FILELISTS,
	DUM_REPO_MD_TYPE_OTHER,
	DUM_REPO_MD_TYPE_COMPS,
	DUM_REPO_MD_TYPE_UNKNOWN
} DumRepoMdType;

typedef struct {
	guint		 timestamp;
	gchar		*location;
	gchar		*checksum;
	gchar		*checksum_open;
	GChecksumType	 checksum_type;
} DumRepoMdInfoData;

GType		 dum_repo_md_get_type		(void) G_GNUC_CONST;
DumRepoMd	*dum_repo_md_new		(void);

/* setters */
gboolean	 dum_repo_md_set_mdtype		(DumRepoMd	*md,
						 DumRepoMdType	 type);
gboolean	 dum_repo_md_set_id		(DumRepoMd	*md,
						 const gchar	*id);
gboolean	 dum_repo_md_set_cache_dir	(DumRepoMd	*md,
						 const gchar	*cache_dir);
gboolean	 dum_repo_md_set_baseurl	(DumRepoMd	*md,
						 const gchar	*baseurl);
gboolean	 dum_repo_md_set_base_filename	(DumRepoMd	*md,
						 const gchar	*base_filename);
gboolean	 dum_repo_md_set_info_data	(DumRepoMd	*md,
						 const DumRepoMdInfoData *info_data);

/* getters */
DumRepoMdType	 dum_repo_md_get_mdtype		(DumRepoMd	*md);
const gchar	*dum_repo_md_get_id		(DumRepoMd	*md);

const gchar	*dum_repo_md_get_local_path	(DumRepoMd	*md);
const gchar	*dum_repo_md_get_remote_uri	(DumRepoMd	*md);
const gchar	*dum_repo_md_get_filename	(DumRepoMd	*md);
const gchar	*dum_repo_md_get_filename_raw	(DumRepoMd	*md);
const DumRepoMdInfoData *dum_repo_md_get_info_data (DumRepoMd	*md);

/* actions */
gboolean	 dum_repo_md_load		(DumRepoMd	*md,
						 GError		**error);
gboolean	 dum_repo_md_check		(DumRepoMd	*md,
						 GError		**error);
gboolean	 dum_repo_md_refresh		(DumRepoMd	*md,
						 GError		**error);
guint		 dum_repo_md_get_age		(DumRepoMd	*md,
						 GError		**error);
void		 dum_repo_md_print		(DumRepoMd	*md);
const gchar	*dum_repo_md_type_to_text	(DumRepoMdType	 type);

G_END_DECLS

#endif /* __DUM_REPO_MD_H */

