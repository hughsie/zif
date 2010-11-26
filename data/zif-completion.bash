#
#  bash completion support for Zif's console commands.
#
#  Copyright (C) 2008 - 2010 James Bowes <jbowes@repl.ca>
#  Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
#  02110-1301  USA


__zif_commandlist="
    clean
    download
    find-package
    get-categories
    get-depends
    get-details
    get-files
    get-groups
    get-packages
    get-updates
    get-upgrades
    get-config-value
    help
    install
    local-install
    manifest-check
    manifest-dump
    refresh-cache
    remove
    repo-disable
    repo-enable
    repo-list
    resolve
    search-category
    search-details
    search-file
    search-group
    search-name
    update
    update-details
    upgrade
    what-conflicts
    what-obsoletes
    what-provides
    "

__zifcomp ()
{
	local all c s=$'\n' IFS=' '$'\t'$'\n'
	local cur="${COMP_WORDS[COMP_CWORD]}"
	if [ $# -gt 2 ]; then
		cur="$3"
	fi
	for c in $1; do
		case "$c$4" in
		*.)    all="$all$c$4$s" ;;
		*)     all="$all$c$4 $s" ;;
		esac
	done
	IFS=$s
	COMPREPLY=($(compgen -P "$2" -W "$all" -- "$cur"))
	return
}

_zif ()
{
	local i c=1 command

	while [ $c -lt $COMP_CWORD ]; do
		i="${COMP_WORDS[c]}"
		case "$i" in
		--version|--help|--verbose|--nowait|-v|-n|-h|-?) ;;
		*) command="$i"; break ;;
		esac
		c=$((++c))
	done

    if [ $c -eq $COMP_CWORD -a -z "$command" ]; then
		case "${COMP_WORDS[COMP_CWORD]}" in
		--*=*) COMPREPLY=() ;;
		--*)   __zifcomp "
			--version
			--offline
			--verbose
            --help
            --config
            --proxy
            --background
            --age
            --skip-broken
            --assume-yes
			"
			;;
        -*) __zifcomp "
            -v
            -o
            -h
            -c
            -b
            -p
            -a
            -s
            -y
            -?
            "
            ;;
		*)     __zifcomp "$__zif_commandlist" ;;
		esac
		return
	fi

	case "$command" in
	*)           COMPREPLY=() ;;
	esac
}

complete -o default -o nospace -F _zif zif
