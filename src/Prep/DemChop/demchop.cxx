// demchop.cxx -- chop up a dem file into it's corresponding pieces and stuff
//                them into the workspace directory
//
// Written by Curtis Olson, started March 1999.
//
// Copyright (C) 1997  Curtis L. Olson  - http://www.flightgear.org/~curt
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
// $Id: demchop.cxx,v 1.13 2004-11-19 22:25:51 curt Exp $

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <simgear/compiler.h>

#include <iostream>
#include <string>

#include <simgear/bucket/newbucket.hxx>
#include <simgear/debug/logstream.hxx>
#include <simgear/misc/sg_path.hxx>

#include <DEM/dem.hxx>

#include "point2d.hxx"

using std::endl;
using std::cout;
using std::string;


int main(int argc, char **argv) {
    sglog().setLogLevels( SG_ALL, SG_WARN );

    if ( argc != 3 ) {
	SG_LOG( SG_GENERAL, SG_ALERT, 
		"Usage " << argv[0] << " <dem_file> <work_dir>" );
	exit(-1);
    }

    string dem_name = argv[1];
    string work_dir = argv[2];

    SGPath sgp( work_dir );
    sgp.append( "dummy" );
    sgp.create_dir( 0755 );

    TGDem dem(dem_name);
    dem.parse();
    dem.close();

    point2d min, max;
    min.x = dem.get_originx() / 3600.0 + SG_HALF_BUCKET_SPAN;
    min.y = dem.get_originy() / 3600.0 + SG_HALF_BUCKET_SPAN;
    SGBucket b_min( min.x, min.y );

    max.x = (dem.get_originx() + dem.get_cols() * dem.get_col_step()) / 3600.0 
	- SG_HALF_BUCKET_SPAN;
    max.y = (dem.get_originy() + dem.get_rows() * dem.get_row_step()) / 3600.0 
	- SG_HALF_BUCKET_SPAN;
    SGBucket b_max( max.x, max.y );

    if ( b_min == b_max ) {
	dem.write_area( work_dir, b_min, true );
    } else {
	SGBucket b_cur;
	int dx, dy, i, j;

	sgBucketDiff(b_min, b_max, &dx, &dy);
	cout << "DEM file spans tile boundaries" << endl;
	cout << "  dx = " << dx << "  dy = " << dy << endl;

	if ( (dx > 20) || (dy > 20) ) {
	    cout << "somethings really wrong!!!!" << endl;
	    exit(-1);
	}

	for ( j = 0; j <= dy; j++ ) {
	    for ( i = 0; i <= dx; i++ ) {
		b_cur = sgBucketOffset(min.x, min.y, i, j);
		dem.write_area( work_dir, b_cur, true );
	    }
	}
    }

    return 0;
}


