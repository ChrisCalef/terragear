// main.cxx -- main loop
//
// Written by Curtis Olson, started March 1998.
//
// Copyright (C) 1998  Curtis L. Olson  - curt@me.umn.edu
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
//


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <simgear/compiler.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include <list>
#include <stdio.h>
#include <string.h>
#include STL_STRING

#include <simgear/constants.h>
#include <simgear/bucket/newbucket.hxx>
#include <simgear/debug/logstream.hxx>
#include <simgear/misc/fgstream.hxx>

#include <Polygon/index.hxx>

#include "build.hxx"
#include "convex_hull.hxx"


#if 0
// write out airport data
void write_airport( long int p_index, const FGPolygon& hull_list, FGBucket b, 
		    const string& root, const bool cut_and_keep ) {
    char tile_name[256], poly_index[256];

    string base = b.gen_base_path();
    string path = root + "/" + base;
    string command = "mkdir -p " + path;
    system( command.c_str() );

    long int b_index = b.gen_index();
    sprintf(tile_name, "%ld", b_index);
    string aptfile = path + "/" + tile_name;

    sprintf( poly_index, "%ld", p_index );
    aptfile += ".";
    aptfile += poly_index;
    cout << "apt file = " << aptfile << endl;

    FILE *fd;
    if ( (fd = fopen(aptfile.c_str(), "a")) == NULL ) {
        cout << "Cannot open file: " << aptfile << endl;
        exit(-1);
    }

    // polygon type
    if ( cut_and_keep ) {
	fprintf( fd, "AirportKeep\n" );
    } else {
	fprintf( fd, "AirportIgnore\n" );
    }

    // number of contours
    fprintf( fd, "1\n" );

    // size of first contour
    fprintf( fd, "%d\n", hull_list.contour_size(0) );

    // hole flag
    fprintf( fd, "%d\n", 0 );  // not a hole

    // write contour (polygon) points
    for ( int i = 0; i < hull_list.contour_size(0); ++i ) {
	fprintf( fd, "%.7f %.7f\n", 
		 hull_list.get_pt(0,i).x(),
		 hull_list.get_pt(0,i).y() );
    }

    fclose(fd);
}


// process and airport + runway list
void process_airport( string airport, list < string > & runways_list,
		      const string& root ) {
    FGPolygon rwy_list, hull;
    point_list apt_list;

    // parse main airport information
    int elev;

    cout << airport << endl;
    string apt_type = airport.substr(0, 1);
    string apt_code = airport.substr(2, 4); my_chomp(apt_code);
    string apt_lat = airport.substr(7, 10);
    string apt_lon = airport.substr(18, 11);
    string apt_elev = airport.substr(30, 5);
    sscanf( apt_elev.c_str(), "%d", &elev );
    string apt_use = airport.substr(36, 1);
    string apt_twr = airport.substr(37, 1);
    string apt_bldg = airport.substr(38, 1);
    string apt_name = airport.substr(40);

    /*
    cout << "  type = " << apt_type << endl;
    cout << "  code = " << apt_code << endl;
    cout << "  lat  = " << apt_lat << endl;
    cout << "  lon  = " << apt_lon << endl;
    cout << "  elev = " << apt_elev << " " << elev << endl;
    cout << "  use  = " << apt_use << endl;
    cout << "  twr  = " << apt_twr << endl;
    cout << "  bldg = " << apt_bldg << endl;
    cout << "  name = " << apt_name << endl;
    */

    // Ignore any seaplane bases
    if ( apt_type == "S" ) {
	return;
    }

    // parse runways and generate the vertex list
    string rwy_str;
    double lon, lat, hdg;
    int len, width;

    list < string >::iterator last_runway = runways_list.end();
    for ( list < string >::iterator current_runway = runways_list.begin();
	  current_runway != last_runway ; ++current_runway ) {
	rwy_str = (*current_runway);

	cout << rwy_str << endl;
	string rwy_no = rwy_str.substr(2, 4);
	string rwy_lat = rwy_str.substr(6, 10);
	sscanf( rwy_lat.c_str(), "%lf", &lat);
	string rwy_lon = rwy_str.substr(17, 11);
	sscanf( rwy_lon.c_str(), "%lf", &lon);
	string rwy_hdg = rwy_str.substr(29, 7);
	sscanf( rwy_hdg.c_str(), "%lf", &hdg);
	string rwy_len = rwy_str.substr(36, 7);
	sscanf( rwy_len.c_str(), "%d", &len);
	string rwy_width = rwy_str.substr(43, 4);
	sscanf( rwy_width.c_str(), "%d", &width);
	string rwy_sfc = rwy_str.substr(47, 4);
	string rwy_end1 = rwy_str.substr(52, 8);
	string rwy_end2 = rwy_str.substr(61, 8);

	/*
	cout << "  no    = " << rwy_no << endl;
	cout << "  lat   = " << rwy_lat << " " << lat << endl;
	cout << "  lon   = " << rwy_lon << " " << lon << endl;
	cout << "  hdg   = " << rwy_hdg << " " << hdg << endl;
	cout << "  len   = " << rwy_len << " " << len << endl;
	cout << "  width = " << rwy_width << " " << width << endl;
	cout << "  sfc   = " << rwy_sfc << endl;
	cout << "  end1  = " << rwy_end1 << endl;
	cout << "  end2  = " << rwy_end2 << endl;
	*/

	rwy_list = gen_runway_area( lon, lat, hdg * DEG_TO_RAD, 
				    (double)len * FEET_TO_METER,
				    (double)width * FEET_TO_METER );

	// add rwy_list to apt_list
	for (int i = 0; i < (int)rwy_list.contour_size(0); ++i ) {
	    apt_list.push_back( rwy_list.get_pt(0,i) );
	}
    }

    if ( apt_list.size() == 0 ) {
	cout << "no runway points generated" << endl;
	return;
    }

    // printf("Runway points in degrees\n");
    // current = apt_list.begin();
    // last = apt_list.end();
    // for ( ; current != last; ++current ) {
    //   printf( "%.5f %.5f\n", current->lon, current->lat );
    // }
    // printf("\n");

    // generate convex hull
    hull = convex_hull(apt_list);

    // get next polygon index
    long int index = poly_index_next();

    // find average center, min, and max point of convex hull
    Point3D min(200.0), max(-200.0);
    double sum_x, sum_y;
    int count = hull.contour_size(0);
    sum_x = sum_y = 0.0;
    for ( int i = 0; i < count; ++i ) {
	// printf("return = %.6f %.6f\n", (*current).x, (*current).y);
	Point3D p = hull.get_pt( 0, i );
	sum_x += p.x();
	sum_y += p.y();

	if ( p.x() < min.x() ) { min.setx( p.x() ); }
	if ( p.y() < min.y() ) { min.sety( p.y() ); }
	if ( p.x() > max.x() ) { max.setx( p.x() ); }
	if ( p.y() > max.y() ) { max.sety( p.y() ); }
    }
    Point3D average( sum_x / count, sum_y / count, 0 );

    // find buckets for center, min, and max points of convex hull.
    // note to self: self, you should think about checking for runways
    // that span the data line
    FGBucket b(average.x(), average.y());
    FGBucket b_min(min.x(), min.y());
    FGBucket b_max(max.x(), max.y());
    cout << "Bucket center = " << b << endl;
    cout << "Bucket min = " << b_min << endl;
    cout << "Bucket max = " << b_max << endl;
    
    if ( b_min == b_max ) {
	write_airport( index, hull, b, root, true );
    } else {
	FGBucket b_cur;
	int dx, dy, i, j;

	fgBucketDiff(b_min, b_max, &dx, &dy);
	cout << "airport spans tile boundaries" << endl;
	cout << "  dx = " << dx << "  dy = " << dy << endl;

	if ( (dx > 2) || (dy > 2) ) {
	    cout << "somethings really wrong!!!!" << endl;
	    exit(-1);
	}

	for ( j = 0; j <= dy; j++ ) {
	    for ( i = 0; i <= dx; i++ ) {
		b_cur = fgBucketOffset(min.x(), min.y(), i, j);
		if ( b_cur == b ) {
		    write_airport( index, hull, b_cur, root, true );
		} else {
		    write_airport( index, hull, b_cur, root, true );
		}
	    }
	}
	// string answer; cin >> answer;
    }
}
#endif


// reads the apt_full file and extracts and processes the individual
// airport records
int main( int argc, char **argv ) {
    string_list runways_list;
    string airport, last_airport;
    string line;
    char tmp[256];

    fglog().setLogLevels( FG_ALL, FG_DEBUG );

    if ( argc != 3 ) {
	FG_LOG( FG_GENERAL, FG_ALERT, 
		"Usage " << argv[0] << " <apt_file> <work_dir>" );
	exit(-1);
    }

    // make work directory
    string work_dir = argv[2];
    string command = "mkdir -p " + work_dir;
    system( command.c_str() );

    // initialize persistant polygon counter
    string counter_file = work_dir + "/poly_counter";
    poly_index_init( counter_file );

    fg_gzifstream in( argv[1] );
    if ( !in ) {
        FG_LOG( FG_GENERAL, FG_ALERT, "Cannot open file: " << argv[1] );
        exit(-1);
    }

    // throw away the first line
    in.getline(tmp, 256);

    last_airport = "";

    while ( ! in.eof() ) {
	in.getline(tmp, 256);
	line = tmp;
	// cout << line << endl;

	if ( line.length() == 0 ) {
	    // empty, skip
	} else if ( line[0] == '#' ) {
	    // comment, skip
	} else if ( (line[0] == 'A') || (line[0] == 'S') ) {
	    // start of airport record
	    airport = line;

	    if ( last_airport.length() ) {
		// process previous record
		// process_airport(last_airport, runways_list, argv[2]);
		build_airport(last_airport, runways_list, argv[2]);
	    }

	    // clear runway list for start of next airport
	    runways_list.clear();

	    last_airport = airport;
	} else if ( line[0] == 'R' ) {
	    // runway entry
	    runways_list.push_back(line);
	} else if ( line == "[End]" ) {
	    // end of file
	    break;
	} else {
	    FG_LOG( FG_GENERAL, FG_ALERT, 
		    "Unknown line in file" << endl << line );
	    exit(-1);
	}
    }

    if ( last_airport.length() ) {
	// process previous record
	// process_airport(last_airport, runways_list, argv[2]);
	build_airport(last_airport, runways_list, argv[2]);
    }

    return 0;
}


