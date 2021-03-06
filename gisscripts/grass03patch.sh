#!/bin/sh
#
# Written by Martin Spott
#
# Copyright (C) 2010  Martin Spott - Martin (at) flightgear (dot) org
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#

# Create reasonable symlinks pointing to this script to return proper
# $MODE-values for the "case" clause, like 'grass03patch.sh_shp'
MODE=`basename ${0} | cut -f 2 -d \_`
#
#PATCHMAP=clc00_nl
PATCHMAP=clc00

case ${MODE} in
	shp)
	    MLIST=`g.mlist type=vect pattern="c[0-9][0-9][0-9]_int" fs=,`
	;;
	ldb)
	    MLIST=`g.mlist type=vect pattern="cs_*_int" fs=,`
	;;
esac

v.patch input=${MLIST} output=${PATCHMAP}_patched

# EOF
