// index.cxx -- routines to handle a unique/persistant integer polygon index
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
 
#include <simgear/compiler.h>

#include STL_IOSTREAM
#include STL_STRING

#include <stdio.h>

#include "index.hxx"

FG_USING_STD(cout);
FG_USING_STD(endl);


static long int poly_index;
static string poly_path;


// initialize the unique polygon index counter stored in path
bool poly_index_init( string path ) {
    poly_path = path;

    FILE *fp = fopen( poly_path.c_str(), "r" );

    if ( fp == NULL ) {
	cout << "Error cannot open " << poly_path << endl;
	poly_index = 0;
	return false;
    }

    fscanf( fp, "%ld", &poly_index );

    fclose( fp );

    return true;
}


// increment the persistant counter and return the next poly_index
long int poly_index_next() {
    ++poly_index;

    FILE *fp = fopen( poly_path.c_str(), "w" );

    if ( fp == NULL ) {
	cout << "Error cannot open " << poly_path << " for writing" << endl;
    }

    fprintf( fp, "%ld\n", poly_index );

    fclose( fp );

    return poly_index;
}


