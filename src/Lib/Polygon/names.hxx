// names.hxx -- process shapefiles names
//
// Written by Curtis Olson, started February 1999.
//
// Copyright (C) 1999  Curtis L. Olson  - curt@flightgear.org
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
// $Id$
 

#ifndef _NAMES_HXX
#define _NAMES_HXX


#include <simgear/compiler.h>

#include STL_STRING

FG_USING_STD(string);


// Posible shape file types.  Note the order of these is important and
// defines the priority of these shapes if they should intersect.  The
// smaller the number, the higher the priority.
enum AreaType {
    SomeSortOfArea    = 0,
    HoleArea          = 1,
    LakeArea          = 2,
    DryLakeArea       = 3,
    IntLakeArea       = 4,
    ReservoirArea     = 5,
    IntReservoirArea  = 6,
    StreamArea        = 7,
    CanalArea         = 8,
    GlacierArea       = 9,
    OceanArea         = 10,
    UrbanArea         = 11,
    MarshArea         = 12,
    DefaultArea       = 13,
    VoidArea          = 9997,
    NullArea          = 9998,
    UnknownArea       = 9999
};


// return area type from text name
AreaType get_area_type( string area );

// return text form of area name
string get_area_name( AreaType area );


#endif // _NAMES_HXX


