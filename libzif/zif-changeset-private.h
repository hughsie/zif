/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
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

#ifndef __ZIF_CHANGESET_PRIVATE_H
#define __ZIF_CHANGESET_PRIVATE_H

#include "zif-changeset.h"

G_BEGIN_DECLS

void			 zif_changeset_set_date		(ZifChangeset		*changeset,
							 guint64		 date);
void			 zif_changeset_set_author	(ZifChangeset		*changeset,
							 const gchar		*author);
void			 zif_changeset_set_description	(ZifChangeset		*changeset,
							 const gchar		*description);
void			 zif_changeset_set_version	(ZifChangeset		*changeset,
							 const gchar		*version);
gboolean		 zif_changeset_parse_header	(ZifChangeset		*changeset,
							 const gchar		*header,
							 GError			**error);

G_END_DECLS

#endif /* __ZIF_CHANGESET_PRIVATE_H */

