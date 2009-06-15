/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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

#if !defined (__ZIF_H_INSIDE__) && !defined (ZIF_COMPILATION)
#error "Only <zif.h> can be included directly."
#endif

#ifndef __ZIF_REPO_MD_MASTER_H
#define __ZIF_REPO_MD_MASTER_H

#include <glib-object.h>

#include "zif-repo-md.h"

G_BEGIN_DECLS

#define ZIF_TYPE_REPO_MD_MASTER		(zif_repo_md_master_get_type ())
#define ZIF_REPO_MD_MASTER(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_REPO_MD_MASTER, ZifRepoMdMaster))
#define ZIF_REPO_MD_MASTER_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_REPO_MD_MASTER, ZifRepoMdMasterClass))
#define ZIF_IS_REPO_MD_MASTER(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_REPO_MD_MASTER))
#define ZIF_IS_REPO_MD_MASTER_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_REPO_MD_MASTER))
#define ZIF_REPO_MD_MASTER_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_REPO_MD_MASTER, ZifRepoMdMasterClass))

typedef struct _ZifRepoMdMaster		ZifRepoMdMaster;
typedef struct _ZifRepoMdMasterPrivate	ZifRepoMdMasterPrivate;
typedef struct _ZifRepoMdMasterClass	ZifRepoMdMasterClass;

struct _ZifRepoMdMaster
{
	ZifRepoMd		 parent;
	ZifRepoMdMasterPrivate	*priv;
};

struct _ZifRepoMdMasterClass
{
	ZifRepoMdClass		 parent_class;
};

GType		 zif_repo_md_master_get_type		(void);
ZifRepoMdMaster	*zif_repo_md_master_new			(void);
const ZifRepoMdInfoData *zif_repo_md_master_get_info	(ZifRepoMdMaster	*md,
							 ZifRepoMdType		 type,
							 GError			**error);

G_END_DECLS

#endif /* __ZIF_REPO_MD_MASTER_H */

