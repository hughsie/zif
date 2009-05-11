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

#if !defined (__DUM_H_INSIDE__) && !defined (DUM_COMPILATION)
#error "Only <dum.h> can be included directly."
#endif

#ifndef __DUM_REPOS_H
#define __DUM_REPOS_H

#include <glib-object.h>
#include "dum-store-remote.h"

G_BEGIN_DECLS

#define DUM_TYPE_REPOS		(dum_repos_get_type ())
#define DUM_REPOS(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), DUM_TYPE_REPOS, DumRepos))
#define DUM_REPOS_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), DUM_TYPE_REPOS, DumReposClass))
#define DUM_IS_REPOS(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), DUM_TYPE_REPOS))
#define DUM_IS_REPOS_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), DUM_TYPE_REPOS))
#define DUM_REPOS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), DUM_TYPE_REPOS, DumReposClass))

typedef struct DumReposPrivate DumReposPrivate;

typedef struct
{
	GObject			 parent;
	DumReposPrivate		*priv;
} DumRepos;

typedef struct
{
	GObjectClass	parent_class;
} DumReposClass;

GType		 dum_repos_get_type		(void) G_GNUC_CONST;
DumRepos	*dum_repos_new			(void);
gboolean	 dum_repos_set_repos_dir	(DumRepos	*repos,
						 const gchar	*repos_dir,
						 GError		**error);
gboolean	 dum_repos_load			(DumRepos	*repos,
						 GError		**error);
GPtrArray	*dum_repos_get_stores		(DumRepos	*repos,
						 GError		**error);
GPtrArray	*dum_repos_get_stores_enabled	(DumRepos	*repos,
						 GError		**error);
DumStoreRemote	*dum_repos_get_store		(DumRepos	*repos,
						 const gchar	*id,
						 GError		**error);
G_END_DECLS

#endif /* __DUM_REPOS_H */

