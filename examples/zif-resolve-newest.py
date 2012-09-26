#!/usr/bin/python

# Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
# Licensed under the GNU General Public License Version 2

from gi.repository import Zif

# use Zif to check if the install version is actually newer
state = Zif.State.new()
config = Zif.Config.new()
config.set_filename('/etc/zif/zif.conf')
local = Zif.StoreLocal.new()
packages = local.resolve(['gnome-power-manager'], state)

if len(packages) > 0:
    package = Zif.Package.array_get_newest(packages)
    print package.get_name()
    print package.get_version()
