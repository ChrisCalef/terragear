// construct.cxx -- Class to manage the primary data used in the
//                  construction process
//
// Written by Curtis Olson, started May 1999.
//
// Copyright (C) 1999  Curtis L. Olson  - http://www.flightgear.org/~curt
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
//
// $Id: construct.cxx,v 1.4 2004-11-19 22:25:49 curt Exp $

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <boost/foreach.hpp>

#include <simgear/compiler.h>
#include <simgear/constants.h>
#include <simgear/math/sg_geodesy.hxx>
#include <simgear/math/SGGeometry.hxx>
#include <simgear/misc/sg_dir.hxx>
#include <simgear/misc/texcoord.hxx>
#include <simgear/io/sg_binobj.hxx>
#include <simgear/structure/exception.hxx>
#include <simgear/debug/logstream.hxx>

#include <Geometry/poly_support.hxx>
#include <Geometry/poly_extra.hxx>

#include <Match/match.hxx>
#include <Osgb36/osgb36.hxx>

#include "construct.hxx"
#include "usgs.hxx"

using std::string;

double gSnap = 0.00000001;      // approx 1 mm

static const double cover_size = 1.0 / 120.0;
static const double half_cover_size = cover_size * 0.5;

#define USE_CLIPPER     (0)

// If we don't offset land use squares by some amount, then we can get
// land use square boundaries coinciding with tile boundaries.
//
// This can foul up our edge matching scheme because a subsequently
// generated adjacent tile might be forced to have edge nodes not
// found in the first tile and not on the shared edge.  This can lead
// to gaps.  If we put skirts around everything that might hide the
// problem.
static const double quarter_cover_size = cover_size * 0.25;


// For debug:
void*     ds_id = NULL;        // If we are going to build shapefiles
void*     l_id  = NULL;        // datasource and layer IDs
char      ds_name[128];        // need to clean this up and add cmdline options
char      layer_name[128];
char      feature_name[128];


// Constructor
TGConstruct::TGConstruct():
        useUKGrid(false),
        writeSharedEdges(true),
        useOwnSharedEdges(false)
{ }


// Destructor
TGConstruct::~TGConstruct() { 
    array.close();

    // land class polygons
    polys_in.clear();
    polys_clipped.clear();

    // All Nodes
    nodes.clear();
}

// STEP 1
// Load elevation data from an Array file (a regular grid of elevation data) 
// and return list of fitted nodes.
void TGConstruct::LoadElevationArray( void ) {
    string base = bucket.gen_base_path();
    int i;

    for ( i = 0; i < (int)load_dirs.size(); ++i ) {
        string array_path = get_work_base() + "/" + load_dirs[i] + "/" + base + "/" + bucket.gen_index_str();

        if ( array.open(array_path) ) {
            break;
        } else {
            SG_LOG(SG_GENERAL, SG_ALERT, "Failed to open Array file " << array_path);
        }
    }

    array.parse( bucket );
    array.remove_voids( );

    point_list corner_list = array.get_corner_list();
    for (unsigned int i=0; i<corner_list.size(); i++) {
        nodes.unique_add(corner_list[i]);
    }

    point_list fit_list = array.get_fitted_list();
    for (unsigned int i=0; i<fit_list.size(); i++) {
        nodes.unique_add(fit_list[i]);
    }
}

// Add a polygon to the clipper.
void TGConstruct::add_poly( int area, const TGPolygon &poly, string material ) {
    TGSuperPoly sp;

    if ( area < TG_MAX_AREA_TYPES ) {
        sp.set_poly( poly );
        sp.set_material( material );

        polys_in.superpolys[area].push_back(sp);
    } else {
        SG_LOG( SG_CLIPPER, SG_ALERT, "Polygon type out of range = " << area);
        exit(-1);
    }
}

// Load a polygon definition file.
bool TGConstruct::load_poly(const string& path) {
    bool poly3d = false;
    string first_line;
    string poly_name;
    AreaType poly_type;
    int contours, count, i, j;
    int hole_flag;
    double startx, starty, startz, x, y, z, lastx, lasty, lastz;

    sg_gzifstream in( path );

    if ( !in ) {
        SG_LOG( SG_CLIPPER, SG_ALERT, "Cannot open file: " << path );
        exit(-1);
    }

    TGPolygon poly;

    Point3D p;
    // (this could break things, why is it here) in >> skipcomment;
    while ( !in.eof() ) {
        in >> first_line;
        if ( first_line == "#2D" ) {
            poly3d = false;
            in >> poly_name;
        } else if ( first_line == "#3D" ) {
            poly3d = true;
            in >> poly_name;
        } else {
            // support old format (default to 2d)
            poly3d = false;
            poly_name = first_line;
        }
        poly_type = get_area_type( poly_name );
        in >> contours;

        SG_LOG( SG_CLIPPER, SG_INFO, "Loading " << path << ":" << poly_name << "-" << poly_type << " contours = " << contours );

        poly.erase();

        for ( i = 0; i < contours; ++i ) {
            in >> count;

            if ( count < 3 ) {
                SG_LOG( SG_CLIPPER, SG_ALERT, "Polygon with less than 3 data points." );
                exit(-1);
            }

            in >> hole_flag;

            in >> startx;
            in >> starty;
            if ( poly3d ) {
                in >> startz;
            } else {
                startz = -9999.0;
            }
            
            p = Point3D(startx+nudge, starty+nudge, startz);
            p.snap( gSnap );
            poly.add_node( i, p );

            if ( poly3d ) {
                nodes.unique_add_fixed_elevation( p );
            } else {
                nodes.unique_add( p );
            }

            for ( j = 1; j < count - 1; ++j ) {
                in >> x;
                in >> y;
                if ( poly3d ) {
                    in >> z;
                } else {
                    z = -9999.0;
                }
                p = Point3D( x+nudge, y+nudge, z );
                p.snap( gSnap );
                poly.add_node( i, p );
                if ( poly3d ) {
                    nodes.unique_add_fixed_elevation( p );
                } else {
                    nodes.unique_add( p );
                }
            }

            in >> lastx;
            in >> lasty;
            if ( poly3d ) {
                in >> lastz;
            } else {
                lastz = -9999.0;
            }

            if ( (fabs(startx - lastx) < SG_EPSILON) && 
                 (fabs(starty - lasty) < SG_EPSILON) && 
                 (fabs(startz - lastz) < SG_EPSILON) ) {
                // last point same as first, discard
            } else {
                p = Point3D( lastx+nudge, lasty+nudge, lastz );
                p.snap( gSnap );
                poly.add_node( i, p );
                if ( poly3d ) {
                    nodes.unique_add_fixed_elevation( p );
                } else {
                    nodes.unique_add( p );
                }
            }
        }

        poly = remove_dups( poly );
	
        int area = (int)poly_type;
        string material = get_area_name( area );
        
        add_poly(area, poly, material);

        in >> skipcomment;
    }

    return true;
}


// Load a polygon definition file containing osgb36 Eastings and Northings
// and convert them to WGS84 Latitude and Longitude
bool TGConstruct::load_osgb36_poly(const string& path) {
    string poly_name;
    AreaType poly_type;
    int contours, count, i, j;
    int hole_flag;
    double startx, starty, x, y, lastx, lasty;

    SG_LOG( SG_CLIPPER, SG_INFO, "Loading " << path << " ..." );

    sg_gzifstream in( path );

    if ( !in ) {
        SG_LOG( SG_CLIPPER, SG_ALERT, "Cannot open file: " << path );
        exit(-1);
    }

    TGPolygon poly;

    Point3D p;
    Point3D OSRef;
    in >> skipcomment;
    while ( !in.eof() ) {
        in >> poly_name;
        SG_LOG( SG_CLIPPER, SG_INFO, "poly name = " << poly_name);
        poly_type = get_area_type( poly_name );
        SG_LOG( SG_CLIPPER, SG_INFO, "poly type (int) = " << (int)poly_type);
        in >> contours;
        SG_LOG( SG_CLIPPER, SG_INFO, "num contours = " << contours);

        poly.erase();

        for ( i = 0; i < contours; ++i ) {
            in >> count;

            if ( count < 3 ) {
                SG_LOG( SG_CLIPPER, SG_ALERT, "Polygon with less than 3 data points." );
                exit(-1);
            }

            in >> hole_flag;

            in >> startx;
            in >> starty;
            OSRef = Point3D(startx, starty, -9999.0);

            //Convert from OSGB36 Eastings/Northings to WGS84 Lat/Lon
            //Note that startx and starty themselves must not be altered since we compare them with unaltered lastx and lasty later
            p = OSGB36ToWGS84(OSRef);
	    
            poly.add_node( i, p );
            nodes.unique_add( p );

            SG_LOG( SG_CLIPPER, SG_BULK, "0 = " << startx << ", " << starty );

            for ( j = 1; j < count - 1; ++j ) {
                in >> x;
                in >> y;
                OSRef = Point3D( x, y, -9999.0 );
                p = OSGB36ToWGS84(OSRef);
		
                poly.add_node( i, p );
                nodes.unique_add( p );
                SG_LOG( SG_CLIPPER, SG_BULK, j << " = " << x << ", " << y );
            }

            in >> lastx;
            in >> lasty;

            if ( (fabs(startx - lastx) < SG_EPSILON) && 
                 (fabs(starty - lasty) < SG_EPSILON) ) {
                // last point same as first, discard
            } else {
                OSRef = Point3D( lastx, lasty, -9999.0 );
                p = OSGB36ToWGS84(OSRef);
		
                poly.add_node( i, p );
                nodes.unique_add( p );
                SG_LOG( SG_CLIPPER, SG_BULK, count - 1 << " = " << lastx << ", " << lasty );
            }
    	}

        int area = (int)poly_type;
        string material = get_area_name( area );
        
        add_poly(area, poly, material);
    
        in >> skipcomment;
    }

    return true;
}

// load all 2d polygons from the specified load disk directories and
// clip against each other to resolve any overlaps
int TGConstruct::LoadLandclassPolys( void ) {
    int i;

    string base = bucket.gen_base_path();
    string poly_path;
    int count = 0;

    for ( int i = 0; i < TG_MAX_AREA_TYPES; ++i ) {
        polys_in.superpolys[i].clear();
    }

    // load 2D polygons from all directories provided
    for ( i = 0; i < (int)load_dirs.size(); ++i ) {
        poly_path = get_work_base() + "/" + load_dirs[i] + '/' + base;
        SG_LOG(SG_GENERAL, SG_ALERT, "poly_path = " << poly_path);

        string tile_str = bucket.gen_index_str();
        simgear::Dir d(poly_path);
        if (!d.exists()) {
            SG_LOG(SG_GENERAL, SG_ALERT, "directory not found:" << poly_path);
            continue;
        }
    
        simgear::PathList files = d.children(simgear::Dir::TYPE_FILE);
        BOOST_FOREACH(const SGPath& p, files) {
            if (p.file_base() != tile_str) {
                continue;
            }
        
            string lext = p.complete_lower_extension();
            if ((lext == "arr") || (lext == "arr.gz") || (lext == "btg.gz") ||
                (lext == "fit") || (lext == "fit.gz") || (lext == "ind")) 
            {
                // skipped!
            } else if (lext == "osgb36") {
                SG_LOG(SG_GENERAL, SG_ALERT, "Loading osgb36 poly definition file");
                load_osgb36_poly( p.str() );
                ++count;
            } else {
                load_poly( p.str() );
                ++count;
            }
        } // of directory file children
	
        SG_LOG(SG_GENERAL, SG_ALERT, "  loaded " << count << " total polys");
    }

    return count;
}

// Add a polygon to a list, merging if possible.
//
// Merge a polygon with an existing one if possible, append a new one
// otherwise; this function is used by actual_load_landcover, below,
// to reduce the number of separate polygons.
void TGConstruct::add_to_polys ( TGPolygon &accum, const TGPolygon &poly) {
    if ( accum.contours() > 0 ) {
#if USE_CLIPPER
        accum = tgPolygonUnionClipper( accum, poly );
#else
        accum = tgPolygonUnion( accum, poly );
#endif
    } else {
        accum = poly;
    }
}

// make the area specified area, look up the land cover type, and add
// it to polys
void TGConstruct::make_area( const LandCover &cover, TGPolygon *polys,
                       double x1, double y1, double x2, double y2,
                       double half_dx, double half_dy )
{
    const double fudge = 0.0001;  // (0.0001 degrees =~ 10 meters)

    AreaType area = get_landcover_type( cover,
                                   x1 + half_dx, y1 + half_dy,
                                   x2 - x1, y2 - y1 );

    if ( area != get_default_area_type() ) {
        // Create a square polygon and merge it into the list.
        TGPolygon poly;
        poly.erase();
        poly.add_node(0, Point3D(x1 - fudge, y1 - fudge, 0.0));
        poly.add_node(0, Point3D(x1 - fudge, y2 + fudge, 0.0));
        poly.add_node(0, Point3D(x2 + fudge, y2 + fudge, 0.0));
        poly.add_node(0, Point3D(x2 + fudge, y1 - fudge, 0.0));

        if ( measure_roughness( poly ) < 1.0 ) {
            add_to_polys(polys[area], poly);
        }
    }
}

// Come up with a "rough" metric for the roughness of the terrain
// coverted by a polygon
double TGConstruct::measure_roughness( TGPolygon &poly ) {
    int i;
    unsigned int j;

    // find the elevation range
    double max_z = -9999.0;
    double min_z = 9999.0;

    for ( i = 0; i < poly.contours(); ++i ) {
        point_list points = poly.get_contour( i );
        for ( j = 0; j < points.size(); ++j ) {
            double z;
            z = array.altitude_from_grid( points[j].x() * 3600.0, 
                                          points[j].y() * 3600.0 );
            if ( z < -9000 ) {
                z = array.closest_nonvoid_elev( points[j].x() * 3600.0, 
                                                points[j].y() * 3600.0 );
            }

            if ( z < min_z ) {
                min_z = z;
            }
            if ( z > max_z ) {
                max_z = z;
            }
        }
    }

    double diff = max_z - min_z;

    // 50m difference in polygon elevation range yields a roughness
    // metric of 1.0.  Less than 1.0 is relatively flat.  More than
    // 1.0 is relatively rough.

    SG_LOG(SG_GENERAL, SG_ALERT, "roughness = " << diff / 50.0 );

    return diff / 50.0;
}

AreaType TGConstruct::get_landcover_type (const LandCover &cover, double xpos, double ypos, double dx, double dy)
{
    // Look up the land cover for the square
    int cover_value = cover.getValue(xpos, ypos);
    AreaType area = translateUSGSCover(cover_value);

    if ( area != get_default_area_type() ) {
        // Non-default area is fine.
        return area;
    } else {
        // If we're stuck with the default area, try to borrow from a
        // neighbour.
        for (double x = xpos - dx; x <= xpos + dx; x += dx) {
            for (double y = ypos - dy; y < ypos + dx; y += dy) {
                if (x != xpos || y != ypos) {
                    cover_value = cover.getValue(x, y);
                    area = translateUSGSCover(cover_value);
                    if (area != get_default_area_type() ) {
                        return area;
                    }
                }
            }
        }
    }

    // OK, give up and return default
    return get_default_area_type();
}


// Generate polygons from land-cover raster.  Horizontally- or
// vertically-adjacent polygons will be merged automatically.
int TGConstruct::load_landcover()
{
    int count = 0;

    try {

        LandCover cover(get_cover());
        TGPolygon polys[TG_MAX_AREA_TYPES];
        TGPolygon poly;		// working polygon

        // Get the lower left (SW) corner of the tile
        double base_lon = bucket.get_center_lon()
            - 0.5 * bucket.get_width()
            - quarter_cover_size;
        double base_lat = bucket.get_center_lat()
            - 0.5 * bucket.get_height()
            - quarter_cover_size;

        SG_LOG(SG_GENERAL, SG_ALERT, "raster land cover: tile at " << base_lon << ',' << base_lat);
    
        double max_lon = bucket.get_center_lon()
            + 0.5 * bucket.get_width();
        double max_lat = bucket.get_center_lat()
            + 0.5 * bucket.get_height();

        SG_LOG(SG_GENERAL, SG_ALERT, "raster land cover: extends to " << max_lon << ',' << max_lat);

        double x1 = base_lon;
        double y1 = base_lat;
        double x2 = x1 + cover_size;
        double y2 = y1 + cover_size;

        while ( x1 < max_lon ) {
            while ( y1 < max_lat ) {
                make_area( cover, polys, x1, y1, x2, y2, half_cover_size, half_cover_size );
                
                y1 = y2;
                y2 += cover_size;
            }

            x1 = x2;
            x2 += cover_size;
            y1 = base_lat;
            y2 = y1 + cover_size;
        }

        // Now that we're finished looking up land cover, we have a list
        // of lists of polygons, one (possibly-empty) list for each area
        // type.  Add the remaining polygons to the clipper.
        for ( int i = 0; i < TG_MAX_AREA_TYPES; i++ ) {
            if ( polys[i].contours() ) {
                add_poly( i, polys[i], get_area_name((AreaType)i ));
                count++;
            }
        }
    } catch ( string e ) {
        SG_LOG(SG_GENERAL, SG_ALERT, "Died with exception: " << e);
        exit(-1);
    }

    // Return the number of polygons actually read.
    return count;
}

void TGConstruct::FixTJunctions( void ) {
    int before, after;

    // traverse each poly, and add intermediate nodes
    for ( unsigned int i = 0; i < TG_MAX_AREA_TYPES; ++i ) {
        for( unsigned int j = 0; j < polys_clipped.superpolys[i].size(); ++j ) {
            TGPolygon current = polys_clipped.superpolys[i][j].get_poly();

            before  = current.total_size();
            current = add_tgnodes_to_poly( current, &nodes );
            after   = current.total_size();

            if (before != after) {
               SG_LOG( SG_CLIPPER, SG_INFO, "Fixed T-Junctions in " << get_area_name( (AreaType)i ) << ":" << j+1 << " of " << (int)polys_clipped.superpolys[i].size() << " nodes increased from " << before << " to " << after );   
            }

            /* Save it back */
            polys_clipped.superpolys[i][j].set_poly( current );
        }
    }
}

void TGConstruct::calc_gc_course_dist( const Point3D& start, const Point3D& dest, 
                                       double *course, double *dist )
{
    SGGeoc gs = start.toSGGeoc();
    SGGeoc gd = dest.toSGGeoc();
    *course = SGGeoc::courseRad(gs, gd);
    *dist = SGGeoc::distanceM(gs, gd);
}

// calculate spherical distance between two points (lon, lat specified
// in degrees, result returned in meters)
double TGConstruct::distanceSphere( const Point3D p1, const Point3D p2 ) {
    Point3D r1( p1.x() * SG_DEGREES_TO_RADIANS,
                p1.y() * SG_DEGREES_TO_RADIANS,
                p1.z() );
    Point3D r2( p2.x() * SG_DEGREES_TO_RADIANS,
                p2.y() * SG_DEGREES_TO_RADIANS,
                p2.z() );

    double course, dist_m;
    calc_gc_course_dist( r1, r2, &course, &dist_m );

    return dist_m;
}

// fix the elevations of the geodetic nodes
// This should be done in the nodes class itself, except for the need for the triangle type
// hopefully, this will get better when we have the area lookup via superpoly...
void TGConstruct::CalcElevations( void )
{
    //TGPolygon tri_poly;
    TGPolyNodes tri_nodes;
    double e1, e2, e3, min;
    int    n1, n2, n3;
    Point3D p;

    SG_LOG(SG_GENERAL, SG_ALERT, "fixing node heights");

    for (int i = 0; i < (int)nodes.size(); ++i) {
        TGNode node = nodes.get_node( i );
        Point3D pos = node.GetPosition();

        if ( !node.GetFixedPosition() ) {
            // set elevation as interpolated point from DEM data.
            nodes.SetElevation( i, array.altitude_from_grid(pos.x() * 3600.0, pos.y() * 3600.0) );
        }
    }

    // now flatten some stuff
    for (int i = 0; i < TG_MAX_AREA_TYPES; i++) {
        if ( is_lake_area( (AreaType)i ) ) {
            for (int j = 0; j < (int)polys_clipped.superpolys[i].size(); ++j ) {
                SG_LOG( SG_CLIPPER, SG_INFO, "Flattening " << get_area_name( (AreaType)i ) << ":" << j+1 << " of " << (int)polys_clipped.superpolys[i].size() );   

                tri_nodes = polys_clipped.superpolys[i][j].get_tri_idxs();

                for (int k=0; k< tri_nodes.contours(); k++) {
                    if (tri_nodes.contour_size(k) != 3) {
                        SG_LOG(SG_GENERAL, SG_ALERT, "triangle doesnt have 3 nodes" << tri_nodes.contour_size(k) );
                        exit(0);                        
                    }

                    n1 = tri_nodes.get_pt( k, 0 );
                    e1 = nodes.get_node(n1).GetPosition().z();
                    n2 = tri_nodes.get_pt( k, 1 );
                    e2 = nodes.get_node(n2).GetPosition().z();
                    n3 = tri_nodes.get_pt( k, 2 );
                    e3 = nodes.get_node(n3).GetPosition().z();

                    min = e1;
                    if ( e2 < min ) { min = e2; }                  
                    if ( e3 < min ) { min = e3; }                  

                    nodes.SetElevation( n1, min );
                    nodes.SetElevation( n2, min );
                    nodes.SetElevation( n3, min );
                }
            }
        }

        if ( is_stream_area( (AreaType)i ) ) {
            for (int j = 0; j < (int)polys_clipped.superpolys[i].size(); ++j ) {
                SG_LOG( SG_CLIPPER, SG_INFO, "Flattening " << get_area_name( (AreaType)i ) << ":" << j+1 << " of " << (int)polys_clipped.superpolys[i].size() );   

                tri_nodes = polys_clipped.superpolys[i][j].get_tri_idxs();

                for (int k=0; k< tri_nodes.contours(); k++) {
                    if (tri_nodes.contour_size(k) != 3) {
                        SG_LOG(SG_GENERAL, SG_ALERT, "triangle doesnt have 3 nodes" << tri_nodes.contour_size(k) );
                        exit(0);                        
                    }

                    point_list raw_nodes = nodes.get_geod_nodes();

                    n1 = tri_nodes.get_pt( k, 0 );
                    e1 = nodes.get_node(n1).GetPosition().z();
                    n2 = tri_nodes.get_pt( k, 1 );
                    e2 = nodes.get_node(n2).GetPosition().z();
                    n3 = tri_nodes.get_pt( k, 2 );
                    e3 = nodes.get_node(n3).GetPosition().z();

                    min = e1;
                    p   = raw_nodes[n1];

                    if ( e2 < min ) { min = e2; p = raw_nodes[n2]; }                  
                    if ( e3 < min ) { min = e3; p = raw_nodes[n3]; }                  

                    double d1 = distanceSphere( p, raw_nodes[n1] ); 
                    double d2 = distanceSphere( p, raw_nodes[n2] ); 
                    double d3 = distanceSphere( p, raw_nodes[n3] ); 

                    double max1 = d1 * 0.20 + min;
                    double max2 = d2 * 0.20 + min;
                    double max3 = d3 * 0.20 + min;

                    if ( max1 < e1 ) { nodes.SetElevation( n1, max1 ); }
                    if ( max2 < e2 ) { nodes.SetElevation( n2, max2 ); }
                    if ( max3 < e3 ) { nodes.SetElevation( n3, max3 ); }
                }
            }
        }

        if ( is_road_area( (AreaType)i ) ) {
            for (int j = 0; j < (int)polys_clipped.superpolys[i].size(); ++j ) {
                SG_LOG( SG_CLIPPER, SG_INFO, "Flattening " << get_area_name( (AreaType)i ) << ":" << j+1 << " of " << (int)polys_clipped.superpolys[i].size() );   

                tri_nodes = polys_clipped.superpolys[i][j].get_tri_idxs();

                for (int k=0; k< tri_nodes.contours(); k++) {
                    if (tri_nodes.contour_size(k) != 3) {
                        SG_LOG(SG_GENERAL, SG_ALERT, "triangle doesnt have 3 nodes" << tri_nodes.contour_size(k) );
                        exit(0);                        
                    }

                    point_list raw_nodes = nodes.get_geod_nodes();

                    n1 = tri_nodes.get_pt( k, 0 );
                    e1 = nodes.get_node(n1).GetPosition().z();
                    n2 = tri_nodes.get_pt( k, 1 );
                    e2 = nodes.get_node(n2).GetPosition().z();
                    n3 = tri_nodes.get_pt( k, 2 );
                    e3 = nodes.get_node(n3).GetPosition().z();

                    min = e1;
                    p   = raw_nodes[n1];

                    if ( e2 < min ) { min = e2; p = raw_nodes[n2]; }                  
                    if ( e3 < min ) { min = e3; p = raw_nodes[n3]; }                  

                    double d1 = distanceSphere( p, raw_nodes[n1] ); 
                    double d2 = distanceSphere( p, raw_nodes[n2] ); 
                    double d3 = distanceSphere( p, raw_nodes[n3] ); 

                    double max1 = d1 * 0.30 + min;
                    double max2 = d2 * 0.30 + min;
                    double max3 = d3 * 0.30 + min;

                    if ( max1 < e1 ) { nodes.SetElevation( n1, max1 ); }
                    if ( max2 < e2 ) { nodes.SetElevation( n2, max2 ); }
                    if ( max3 < e3 ) { nodes.SetElevation( n3, max3 ); }
                }
            }
        }

        if ( is_ocean_area( (AreaType)i ) ) {
            for (int j = 0; j < (int)polys_clipped.superpolys[i].size(); ++j ) {

                tri_nodes = polys_clipped.superpolys[i][j].get_tri_idxs();

                SG_LOG( SG_CLIPPER, SG_INFO, "Flattening " << get_area_name( (AreaType)i ) << ":" << j+1 << " of " << (int)polys_clipped.superpolys[i].size() );   

                for (int k=0; k< tri_nodes.contours(); k++) {
                    if (tri_nodes.contour_size(k) != 3) {
                        SG_LOG(SG_GENERAL, SG_ALERT, "triangle doesnt have 3 nodes" << tri_nodes.contour_size(k) );
                        exit(0);                        
                    }

                    n1 = tri_nodes.get_pt( k, 0 );
                    n2 = tri_nodes.get_pt( k, 1 );
                    n3 = tri_nodes.get_pt( k, 2 );

                    nodes.SetElevation( n1, 0.0 );
                    nodes.SetElevation( n2, 0.0 );
                    nodes.SetElevation( n3, 0.0 );
                }
            }
        }
    }
}

TGPolygon TGConstruct::area_tex_coords( const TGPolygon& tri )
{
    TGPolygon result;
    result.erase();

    // lots of conversion needed to use simgear API - perhaps we need a new simgear API?        
    for (int c=0; c<tri.contours(); c++)
    {
        // get the points, and calculate the elevations
        point_list nodes = tri.get_contour(c);
        std::vector< SGGeod > conv_geods;
        point_list tex_coords;

        for (int i = 0; i < (int)nodes.size(); ++i ) 
        {
            SGGeod conv_geod = SGGeod::fromDegM( nodes[i].x(), nodes[i].y(), nodes[i].z() );
        	SG_LOG(SG_GENERAL, SG_DEBUG, "geod pt = " << nodes[i] );
           	conv_geods.push_back( conv_geod );
        }
        
        // now calculate texture coordinates
        // generate identity interger list...  
        std::vector< int > node_idx;
        for (int i = 0; i < (int)conv_geods.size(); i++) {
            node_idx.push_back(i);
        }

        std::vector< SGVec2f > tp_list = sgCalcTexCoords( bucket, conv_geods, node_idx );
        // generate a contour of texture coordinates from the tp list
        for (int i = 0; i < (int)tp_list.size(); i++)
        {
            tex_coords.push_back( Point3D::fromSGVec2( tp_list[i] ) );
        }
        result.add_contour( tex_coords, 0 );
    }

    return result;
}

TGPolygon TGConstruct::linear_tex_coords( const TGPolygon& tri, const TGTexParams& tp )
{
    TGPolygon result;
    int i, j;

    result.erase();

    Point3D ref = tp.get_ref();
    double width = tp.get_width();
    double length = tp.get_length();
    double heading = tp.get_heading();
    double minu = tp.get_minu();
    double maxu = tp.get_maxu();
    double minv = tp.get_minv();
    double maxv = tp.get_maxv();
    SG_LOG( SG_GENERAL, SG_DEBUG, "section ref = " << ref );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  width   = " << width );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  length  = " << length );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  heading = " << heading );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  minv    = " << minv );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  maxv    = " << maxv );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  heading = " << heading );

    Point3D p, t;
    double x, y, tx, ty;

    for ( i = 0; i < tri.contours(); ++i ) 
    {
    	for ( j = 0; j < tri.contour_size( i ); ++j ) 
        {
    	    p = tri.get_pt( i, j );
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "tex coords for contour " << i << "point " << j << ": " << p );

    	    //
    	    // 1. Calculate distance and bearing from the center of
    	    // the feature
    	    //

    	    // given alt, lat1, lon1, lat2, lon2, calculate starting
    	    // and ending az1, az2 and distance (s).  Lat, lon, and
    	    // azimuth are in degrees.  distance in meters
    	    double az1, az2, dist;
    	    geo_inverse_wgs_84( 0, ref.y(), ref.x(), p.y(), p.x(),
    				&az1, &az2, &dist );
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "basic course from ref = " << az2);

    	    //
    	    // 2. Rotate this back into a coordinate system where Y
    	    // runs the length of the runway and X runs crossways.
    	    //

    	    double course = az2 - heading;
    	    while ( course < -360 ) { course += 360; }
    	    while ( course > 360 ) { course -= 360; }
    	    SG_LOG( SG_GENERAL, SG_DEBUG,
                        "  course = " << course << "  dist = " << dist );

    	    //
    	    // 3. Convert from polar to cartesian coordinates
    	    //

    	    x = sin( course * SGD_DEGREES_TO_RADIANS ) * dist;
    	    y = cos( course * SGD_DEGREES_TO_RADIANS ) * dist;
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "  x = " << x << " y = " << y);

    	    //
    	    // 4. Map x, y point into texture coordinates
    	    //
    	    double tmp;

            tmp = x / width;
            tx = tmp * (maxu - minu) + minu;

            if ( tx < -1.0 )  { tx = -1.0; }
            if ( tx > 1.0 ) { tx = 1.0; }

    	    SG_LOG(SG_GENERAL, SG_DEBUG, "  (" << tx << ")");

            ty = (y/length) + minv;
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "  (" << ty << ")");

    	    t = Point3D( tx, ty, 0 );
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "  (" << tx << ", " << ty << ")");

    	    result.add_node( i, t );
    	}
    }

    return result;
}

// collect custom objects and move to scenery area
void TGConstruct::AddCustomObjects( void ) {
    // Create/open the output .stg file for writing
    SGPath dest_d(get_output_base().c_str());
    dest_d.append(bucket.gen_base_path().c_str());
    string dest_dir = dest_d.str_native();
    SGPath dest_i(dest_d);
    dest_i.append(bucket.gen_index_str());
    dest_i.concat(".stg");
    string dest_ind = dest_i.str_native();

    FILE *fp;
    if ( (fp = fopen( dest_ind.c_str(), "w" )) == NULL ) {
        SG_LOG( SG_GENERAL, SG_ALERT, "ERROR: opening " << dest_ind << " for writing!" );
        exit(-1);
    }

    // Start with the default custom object which is the base terrain
    fprintf(fp, "OBJECT_BASE %s.btg\n", bucket.gen_index_str().c_str());

    char line[2048];             // big enough?
    char token[256];
    char name[256];

    for ( int i = 0; i < (int)load_dirs.size(); ++i ) {
        SGPath base(get_work_base().c_str());
        base.append(load_dirs[i]);
        base.append( bucket.gen_base_path() );
        SGPath index(base);
        index.append( bucket.gen_index_str() );
        index.concat(".ind");
        string index_file = index.str_native();
        SG_LOG(SG_GENERAL, SG_ALERT, "collecting custom objects from " << index_file);

        sg_gzifstream in( index_file );

        if ( ! in.is_open() ) {
            //cout << "No custom objects" << endl;
        } else {
            while ( ! in.eof() ) {
                SG_LOG( SG_GENERAL, SG_INFO, "Collecting custom objects from " << index_file );
                in.getline(line, 2048);
                SG_LOG( SG_GENERAL, SG_INFO, "line = " << line );

                int result = sscanf( line, "%s %s", token, name );
                SG_LOG( SG_GENERAL, SG_INFO, "scanf scanned " << result << " tokens" );

                if ( result > 0 ) {
                    SG_LOG( SG_GENERAL, SG_INFO, "token = " << token << " name = " << name );

                    if ( strcmp( token, "OBJECT" ) == 0 ) {
                        base.append(name);
                        base.concat(".gz");
                        string basecom = base.str_native();
#ifdef _MSC_VER
                        string command = "copy " + basecom + " " + dest_dir;
#else
                        string command = "cp " + basecom + " " + dest_dir;
#endif
                        SG_LOG( SG_GENERAL, SG_INFO, "running " << command );
                        system( command.c_str() );

                        fprintf(fp, "OBJECT %s\n", name);
                    } else {
                        fprintf(fp, "%s\n", line);
                    }
                }
            }
        }
    }

    fclose(fp);
}

// Attempt to merge slivers into a list of polygons.
//
// For each sliver contour, see if a union with another polygon yields
// a polygon with no increased contours (i.e. the sliver is adjacent
// and can be merged.)  If so, replace the clipped polygon with the
// new polygon that has the sliver merged in.
void TGConstruct::merge_slivers( TGPolyList& clipped,  poly_list& slivers_list ) {
    TGPolygon poly, result, slivers, sliver;
    point_list contour;
    int original_contours, result_contours;
    bool done;
    int area, i, j, k;

    for ( i = 0; i < (int)slivers_list.size(); i++ ) {
        slivers = slivers_list[i];

        for ( j = 0; j < slivers.contours(); ++j ) {
            // cout << "Merging sliver = " << i << endl;

            // make the sliver polygon
            contour = slivers.get_contour( j );
            sliver.erase();
            sliver.add_contour( contour, 0 );
            done = false;

            for ( area = 0; area < TG_MAX_AREA_TYPES && !done; ++area ) {
                if ( is_hole_area( area ) ) {
                	// don't merge a non-hole sliver in with a hole
                	continue;
                }

                // cout << "  testing area = " << area << " with " 
                //      << clipped.polys[area].size() << " polys" << endl;
                for ( k = 0; k < (int)clipped.superpolys[area].size() && !done; ++k ) {
                    // cout << "  polygon = " << j << endl;
                    poly = clipped.superpolys[area][k].get_poly();
                    original_contours = poly.contours();
#if USE_CLIPPER
                    result = tgPolygonUnionClipper( poly, sliver );
#else
                    result = tgPolygonUnion( poly, sliver );
#endif
                    result_contours = result.contours();

                    if ( original_contours == result_contours ) {
                        // cout << "    FOUND a poly to merge the sliver with" << endl;
                        clipped.superpolys[area][k].set_poly( result );
                        done = true;
                    }
                }
            }

           if ( !done ) {
                // cout << "no suitable polys found for sliver merge" << endl;
            }
        }
    }
}

bool TGConstruct::ClipLandclassPolys( void ) {
    TGPolygon accum, clipped, tmp;
    TGPolygon remains;
    poly_list slivers;
    int i, j;
    Point3D p;
    point2d min, max;

    // Get clip bounds
    min.x = bucket.get_center_lon() - 0.5 * bucket.get_width();
    min.y = bucket.get_center_lat() - 0.5 * bucket.get_height();
    max.x = bucket.get_center_lon() + 0.5 * bucket.get_width();
    max.y = bucket.get_center_lat() + 0.5 * bucket.get_height();

    accum.erase();

    // set up clipping tile : and remember to add the nodes!
    polys_in.safety_base.erase();

    p = Point3D(min.x, min.y, -9999.0);
    polys_in.safety_base.add_node( 0, p );
    nodes.unique_add( p );

    p = Point3D(max.x, min.y, -9999.0);
    polys_in.safety_base.add_node( 0, p );
    nodes.unique_add( p );

    p = Point3D(max.x, max.y, -9999.0);
    polys_in.safety_base.add_node( 0, p );
    nodes.unique_add( p );

    p = Point3D(min.x, max.y, -9999.0);
    polys_in.safety_base.add_node( 0, p );
    nodes.unique_add( p );

    // set up land mask, we clip most things to this since it is our
    // best representation of land vs. ocean.  If we have other less
    // accurate data that spills out into the ocean, we want to just
    // clip it.
    // also set up a mask for all water and islands
    TGPolygon land_mask, water_mask, island_mask;
    land_mask.erase();
    water_mask.erase();
    island_mask.erase();
    for ( i = 0; i < TG_MAX_AREA_TYPES; i++ ) {
        if ( is_landmass_area( i ) && !ignoreLandmass ) {
            for ( unsigned int j = 0; j < polys_in.superpolys[i].size(); ++j ) {
#if USE_CLIPPER
                land_mask = tgPolygonUnionClipper( land_mask, polys_in.superpolys[i][j].get_poly() );
#else
                land_mask = tgPolygonUnion( land_mask, polys_in.superpolys[i][j].get_poly() );
#endif

            }
        } else if ( is_water_area( i ) ) {
            for (unsigned int j = 0; j < polys_in.superpolys[i].size(); j++) {
#if USE_CLIPPER
                water_mask = tgPolygonUnionClipper( water_mask, polys_in.superpolys[i][j].get_poly() );
#else
                water_mask = tgPolygonUnion( water_mask, polys_in.superpolys[i][j].get_poly() );
#endif
            }
        } else if ( is_island_area( i ) ) {
            for (unsigned int j = 0; j < polys_in.superpolys[i].size(); j++) {
#if USE_CLIPPER
                island_mask = tgPolygonUnionClipper( island_mask, polys_in.superpolys[i][j].get_poly() );
#else
                island_mask = tgPolygonUnion( island_mask, polys_in.superpolys[i][j].get_poly() );
#endif
            }
        }
    }

    // process polygons in priority order
    for ( i = 0; i < TG_MAX_AREA_TYPES; ++i ) {
        for( j = 0; j < (int)polys_in.superpolys[i].size(); ++j ) {
            TGPolygon current = polys_in.superpolys[i][j].get_poly();

            SG_LOG( SG_CLIPPER, SG_INFO, "Clipping " << get_area_name( (AreaType)i ) << ":" << j+1 << " of " << (int)polys_in.superpolys[i].size() );                       

            tmp = current;

            // if not a hole, clip the area to the land_mask
            if ( !ignoreLandmass && !is_hole_area( i ) ) {
#if USE_CLIPPER
                tmp = tgPolygonIntClipper( tmp, land_mask );
#else
                tmp = tgPolygonInt( tmp, land_mask );
#endif
            }

            // if a water area, cut out potential islands
            if ( is_water_area( i ) ) {
                // clip against island mask
#if USE_CLIPPER
                tmp = tgPolygonDiffClipper( tmp, island_mask );
#else
                tmp = tgPolygonDiff( tmp, island_mask );
#endif
            }

#if 0
            if ( (i == 3) && (j == 137) ) {
                SG_LOG( SG_CLIPPER, SG_INFO, "Error Poly\n" << tmp );     

                sprintf( layer_name, "preclip" );
                l_id = tgShapefileOpenLayer( ds_id, layer_name );
                sprintf( feature_name, "preclip" );
                tgShapefileCreateFeature( ds_id, l_id, tmp, feature_name );
            }
#endif

#if USE_CLIPPER
            clipped = tgPolygonDiffClipper( tmp, accum );
#else
            clipped = tgPolygonDiff( tmp, accum );
#endif
            // only add to output list if the clip left us with a polygon
            if ( clipped.contours() > 0 ) {
                // move slivers from clipped polygon to slivers polygon
                tgPolygonFindSlivers( clipped, slivers );

                // merge any slivers with previously clipped
                // neighboring polygons
                //if ( slivers.contours() > 0 ) {
                //    merge_slivers(polys_clipped, slivers);
                //}

                // add the sliverless result polygon to the clipped polys list
                if ( clipped.contours() > 0  ) {
                    TGSuperPoly sp;
                    string material = get_area_name( (AreaType)i );

                    sp.set_material( material );
                    sp.set_poly( clipped );
                    polys_clipped.superpolys[i].push_back( sp );
                }
	        }

#if USE_CLIPPER
            accum   = tgPolygonUnionClipper( tmp, accum );
#else
            accum   = tgPolygonUnion( tmp, accum );
#endif
        }
    }

    // Now, merge any slivers with clipped polys
    merge_slivers(polys_clipped, slivers);

    // finally, what ever is left over goes to ocean

    // clip to accum against original base tile
    // remains = new gpc_polygon;
    // remains->num_contours = 0;
    // remains->contour = NULL;
#if USE_CLIPPER
    remains = tgPolygonDiffClipper( polys_in.safety_base, accum );
#else
    remains = tgPolygonDiff( polys_in.safety_base, accum );
#endif

    if ( remains.contours() > 0 ) {
        // cout << "remains contours = " << remains.contours() << endl;
        // move slivers from remains polygon to slivers polygon
        tgPolygonFindSlivers( remains, slivers );
        // cout << "  After sliver move:" << endl;
        // cout << "    remains = " << remains.contours() << endl;
        // cout << "    slivers = " << slivers.contours() << endl;

        // merge any slivers with previously clipped
        // neighboring polygons
        if ( slivers.size() > 0 ) {
            merge_slivers(polys_clipped, slivers);
        }

        if ( remains.contours() > 0 ) {
            TGSuperPoly sp;
            string material = get_area_name(get_sliver_target_area_type());

            sp.set_material( material );
            sp.set_poly( remains );

            polys_clipped.superpolys[(int)get_sliver_target_area_type()].push_back(sp);
        }
    }

    // Once clipping is complete, make sure any newly added intersection nodes 
    // are added to the tgnodes
    for (int i = 0; i < TG_MAX_AREA_TYPES; i++) {
        for (int j = 0; j < (int)polys_clipped.superpolys[i].size(); ++j ) {
            TGPolygon poly = polys_clipped.superpolys[i][j].get_poly();

            SG_LOG( SG_CLIPPER, SG_INFO, "Collecting nodes for " << get_area_name( (AreaType)i ) << ":" << j+1 << " of " << (int)polys_clipped.superpolys[i].size() );                       

            for (int k=0; k< poly.contours(); k++) {
                for (int l = 0; l < poly.contour_size(k); l++) {
                    // ensure we have all nodes...
                    nodes.unique_add( poly.get_pt( k, l ) );
                }
            } 
        }
    }

    return true;
}

bool TGNodesSortByLon( const TGNode& n1, const TGNode& n2 )
{
    return ( n1.GetPosition().x() < n2.GetPosition().x() );
}

// TODO : Add to TGNodes class
#if 0

static void dump_lat_nodes( TGConstruct& c, double lat ) {
    node_list all_nodes = c.get_nodes()->get_node_list();
    node_list sorted_nodes;
    for (unsigned int i=0; i<all_nodes.size(); i++) {
        if ( fabs( all_nodes[i].GetPosition().y() - lat ) < 0.0000001 ) {
            sorted_nodes.push_back( all_nodes[i] );
        }
    }

    sort( sorted_nodes.begin(), sorted_nodes.end(), TGNodesSortByLon );

    for (unsigned int i=0; i<sorted_nodes.size(); i++) {
        string fixed;
        
        if ( sorted_nodes[i].GetFixedPosition() ) {
            fixed = " z is fixed elevation ";
        } else {
            fixed = " z is interpolated elevation ";
        }

        SG_LOG(SG_GENERAL, SG_ALERT, "Point[" << i << "] is " << sorted_nodes[i].GetPosition() << fixed );
    }
}
#endif

// This function populates the Superpoly tri_idx polygon.
// This polygon is a mirror of tris, except the verticies are 
// indexes into the node array (cast as unsigned long)
void TGConstruct::LookupNodesPerVertex( void )
{
    SG_LOG(SG_GENERAL, SG_ALERT, "LookupNodexPerVertex");

    // for each node, traverse all the triangles - and create face lists
    for ( unsigned int area = 0; area < TG_MAX_AREA_TYPES; ++area ) {

        for( unsigned int p = 0; p < polys_clipped.superpolys[area].size(); ++p ) {
            TGPolygon   tris = polys_clipped.superpolys[area][p].get_tris();
            TGPolyNodes tri_nodes;
            int idx;

            for (int tri=0; tri < tris.contours(); tri++) {

                for (int vertex = 0; vertex < tris.contour_size(tri); vertex++) {
                    idx = nodes.find( tris.get_pt( tri, vertex ) );
                    if (idx >= 0) {
                        tri_nodes.add_node( tri, idx );
                    } else {
                        exit(0);
                    }
                }
            }
            polys_clipped.superpolys[area][p].set_tri_idxs(tri_nodes);            
        }
    }
}

void TGConstruct::LookupFacesPerNode( void )
{
    SG_LOG(SG_GENERAL, SG_ALERT, "LookupFacesPerNode");

    // Add each face that includes a node to the node's face list
    for ( unsigned int area = 0; area < TG_MAX_AREA_TYPES; ++area ) {
        for( unsigned int p = 0; p < polys_clipped.superpolys[area].size(); ++p ) {
            TGPolygon tris = polys_clipped.superpolys[area][p].get_tris();

            for (int tri=0; tri < tris.contours(); tri++) {
                for (int sub = 0; sub < tris.contour_size(tri); sub++) {
                    int n = nodes.find( tris.get_pt( tri, sub ) );
                    nodes.AddFace( n, area, p, tri );
                }
            }
        }
    }
}

double TGConstruct::calc_tri_area( int_list& triangle_nodes ) {
    Point3D p1 = nodes.get_node( triangle_nodes[0] ).GetPosition();
    Point3D p2 = nodes.get_node( triangle_nodes[1] ).GetPosition();
    Point3D p3 = nodes.get_node( triangle_nodes[2] ).GetPosition();

    return triangle_area( p1, p2, p3 );
}

void TGConstruct::calc_normals( point_list& wgs84_nodes, TGSuperPoly& sp ) {
    // for each face in the superpoly, calculate a face normal
    SGVec3d v1, v2, normal;
    TGPolyNodes tri_nodes = sp.get_tri_idxs();
    int_list    face_nodes;
    double_list face_areas;
    point_list  face_normals;
    double      area;

    face_normals.clear();
    face_areas.clear();

    for (int i=0; i<tri_nodes.contours(); i++) {
        face_nodes = tri_nodes.get_contour(i);

        Point3D p1 = wgs84_nodes[ face_nodes[0] ];
        Point3D p2 = wgs84_nodes[ face_nodes[1] ];
        Point3D p3 = wgs84_nodes[ face_nodes[2] ];

        area  = calc_tri_area( face_nodes );

        // do some sanity checking.  With the introduction of landuse
        // areas, we can get some long skinny triangles that blow up our
        // "normal" calculations here.  Let's check for really small
        // triangle areas and check if one dimension of the triangle
        // coordinates is nearly coincident.  If so, assign the "default"
        // normal of straight up.

        bool degenerate = false;
        const double area_eps = 1.0e-12;
        if ( area < area_eps ) {
            degenerate = true;
        }

        if ( fabs(p1.x() - p2.x()) < SG_EPSILON && fabs(p1.x() - p3.x()) < SG_EPSILON ) {
            degenerate = true;
        }
        if ( fabs(p1.y() - p2.y()) < SG_EPSILON && fabs(p1.y() - p3.y()) < SG_EPSILON ) {
            degenerate = true;
        }
        if ( fabs(p1.z() - p2.z()) < SG_EPSILON && fabs(p1.z() - p3.z()) < SG_EPSILON ) {
            degenerate = true;
        }

        if ( degenerate ) {
            normal = normalize(SGVec3d(p1.x(), p1.y(), p1.z()));
        } else {
        	v1[0] = p2.x() - p1.x();
        	v1[1] = p2.y() - p1.y();
        	v1[2] = p2.z() - p1.z();
        	v2[0] = p3.x() - p1.x();
        	v2[1] = p3.y() - p1.y();
        	v2[2] = p3.z() - p1.z();
        	normal = normalize(cross(v1, v2));
        }
        
        face_normals.push_back( Point3D::fromSGVec3( normal ) );
        face_areas.push_back( area );
    }

    sp.set_face_normals( face_normals );
    sp.set_face_areas( face_areas );
}

void TGConstruct::CalcFaceNormals( void )
{
    // traverse the superpols, and calc normals for each tri within
    point_list wgs84_nodes = nodes.get_wgs84_nodes_as_Point3d();

    for (int i = 0; i < TG_MAX_AREA_TYPES; i++) {
        for (int j = 0; j < (int)polys_clipped.superpolys[i].size(); ++j ) {
            SG_LOG( SG_CLIPPER, SG_INFO, "Calculating face normals for " << get_area_name( (AreaType)i ) << ":" << j+1 << " of " << (int)polys_in.superpolys[i].size() );                       

            calc_normals( wgs84_nodes, polys_clipped.superpolys[i][j] );
        }
    }
}

void TGConstruct::CalcPointNormals( void )
{
    // traverse triangle structure building the face normal table
    SG_LOG(SG_GENERAL, SG_ALERT, "Calculating point normals");

    Point3D normal;
    point_list wgs84_nodes = nodes.get_wgs84_nodes_as_Point3d();

    for ( unsigned int i = 0; i<nodes.size(); i++ ) {
        TGNode node       = nodes.get_node( i );
        TGFaceList faces  = node.GetFaces();
        double total_area = 0.0;

        Point3D average( 0.0 );

        // for each triangle that shares this node
        for ( unsigned int j = 0; j < faces.size(); ++j ) {
            unsigned int at   = faces[j].area;
            unsigned int poly = faces[j].poly;
            unsigned int tri  = faces[j].tri;
            int_list     face_nodes;
            double       face_area;

            normal     = polys_clipped.superpolys[at][poly].get_face_normal( tri );
            face_nodes = polys_clipped.superpolys[at][poly].get_tri_idxs().get_contour( tri ) ;
            face_area  = polys_clipped.superpolys[at][poly].get_face_area( tri );

            normal *= face_area;	// scale normal weight relative to area
            total_area += face_area;
            average += normal;
        }
        average /= total_area;

        nodes.SetNormal( i, average );
    }
}

void TGConstruct::LoadSharedEdgeData( void )
{
    match.load_neighbor_shared( bucket, work_base );
    if ( useOwnSharedEdges ) {
        match.load_missing_shared( bucket, work_base );
    }
    match.add_shared_nodes( this );
}

void TGConstruct::SaveSharedEdgeData( void )
{
    match.split_tile( bucket, this );
    SG_LOG(SG_GENERAL, SG_ALERT, "Tile Split");

    if ( writeSharedEdges ) {
        SG_LOG(SG_GENERAL, SG_ALERT, "write shared edges");

        match.write_shared( bucket, work_base );
    }
}

void TGConstruct::TesselatePolys( void )
{
    // tesselate the polygons and prepair them for final output
    point_list extra = nodes.get_geod_nodes();
    for (int i = 0; i < TG_MAX_AREA_TYPES; i++) {
        for (int j = 0; j < (int)polys_clipped.superpolys[i].size(); ++j ) {
            TGPolygon poly = polys_clipped.superpolys[i][j].get_poly();

           SG_LOG( SG_CLIPPER, SG_INFO, "Tesselating " << get_area_name( (AreaType)i ) << "(" << i << ") :" << j+1 << " of " << (int)polys_clipped.superpolys[i].size() << " : flag = " << polys_clipped.superpolys[i][j].get_flag());                       

#if 0
            if ( (i == 3) && (j == 137) ) {
                SG_LOG( SG_CLIPPER, SG_INFO, "Error Poly\n" << poly );     


                sprintf( layer_name, "pretesselate" );
                l_id = tgShapefileOpenLayer( ds_id, layer_name );
                sprintf( feature_name, "pretesselate" );
                tgShapefileCreateFeature( ds_id, l_id, poly, feature_name );

                // close befoe the crash
                tgShapefileCloseDatasource( ds_id );
            }
#endif

            TGPolygon tri = polygon_tesselate_alt_with_extra( poly, extra, false );

            // ensure all added nodes are accounted for
            for (int k=0; k< tri.contours(); k++) {
                for (int l = 0; l < tri.contour_size(k); l++) {
                    // ensure we have all nodes...
                    nodes.unique_add( tri.get_pt( k, l ) );
                }
            } 

            // Save the triangulation
            polys_clipped.superpolys[i][j].set_tris( tri );
        }
    }
}

void TGConstruct::WriteBtgFile( void )
{
    TGTriNodes normals, texcoords;
    normals.clear();
    texcoords.clear();

    group_list pts_v; pts_v.clear();
    group_list pts_n; pts_n.clear();
    string_list pt_materials; pt_materials.clear();

    group_list tris_v; tris_v.clear();
    group_list tris_n; tris_n.clear();
    group_list tris_tc; tris_tc.clear();
    string_list tri_materials; tri_materials.clear();

    group_list strips_v; strips_v.clear();
    group_list strips_n; strips_n.clear();
    group_list strips_tc; strips_tc.clear();
    string_list strip_materials; strip_materials.clear();

    int index;
    int_list pt_v, tri_v, strip_v;
    int_list pt_n, tri_n, strip_n;
    int_list tri_tc, strip_tc;

    for (int i = 0; i < TG_MAX_AREA_TYPES; i++) {
        // only tesselate non holes
        if ( !is_hole_area( i ) ) {
            for (int j = 0; j < (int)polys_clipped.superpolys[i].size(); ++j ) {
                SG_LOG( SG_CLIPPER, SG_INFO, "Ouput nodes for " << get_area_name( (AreaType)i ) << ":" << j+1 << " of " << (int)polys_clipped.superpolys[i].size() );                       

            	TGPolyNodes tri_nodes = polys_clipped.superpolys[i][j].get_tri_idxs();
            	TGPolygon   tri_txs   = polys_clipped.superpolys[i][j].get_texcoords();
            	string      material  = polys_clipped.superpolys[i][j].get_material();

            	for (int k = 0; k < tri_nodes.contours(); ++k) 
                {
        	        tri_v.clear();
        	        tri_n.clear();
        	        tri_tc.clear();
        	        for (int l = 0; l < tri_nodes.contour_size(k); ++l) 
                    {
                        index = tri_nodes.get_pt( k, l );
            		    tri_v.push_back( index );
    
                        // add the node's normal
                		index = normals.unique_add( nodes.GetNormal( index ) );
                		tri_n.push_back( index );
    
                		Point3D tc = tri_txs.get_pt( k, l );
                		index = texcoords.unique_add( tc );
                		tri_tc.push_back( index );
            	    }
            	    tris_v.push_back( tri_v );
        	        tris_n.push_back( tri_n );
        	        tris_tc.push_back( tri_tc );
        	        tri_materials.push_back( material );
        	    }
            }
        }
    }
    
    std::vector< SGVec3d > wgs84_nodes = nodes.get_wgs84_nodes_as_SGVec3d();
    SGSphered d;

    for (int i = 0; i < (int)wgs84_nodes.size(); ++i) 
    {
        d.expandBy(wgs84_nodes[ i ]);
    }
    
    SGVec3d gbs_center = d.getCenter();
    double gbs_radius = d.getRadius();
    SG_LOG(SG_GENERAL, SG_DEBUG, "gbs center = " << gbs_center);
    SG_LOG(SG_GENERAL, SG_DEBUG, "Done with wgs84 node mapping");
    SG_LOG(SG_GENERAL, SG_DEBUG, "  center = " << gbs_center << " radius = " << gbs_radius );

    // null structures
    group_list fans_v; fans_v.clear();
    group_list fans_n; fans_n.clear();
    group_list fans_tc; fans_tc.clear();
    string_list fan_materials; fan_materials.clear();

    string base = get_output_base();
    string binname = bucket.gen_index_str();
    binname += ".btg";
    string txtname = bucket.gen_index_str();
    txtname += ".txt";
    
    std::vector< SGVec3f > normals_3f;
    for (int i=0; i < (int)normals.get_node_list().size(); i++ ) 
    {
        Point3D node = normals.get_node_list()[i];
        normals_3f.push_back( node.toSGVec3f() );
    }

    std::vector< SGVec2f > texcoords_2f;
    for (int i=0; i < (int)texcoords.get_node_list().size(); i++ ) 
    {
        Point3D node = texcoords.get_node_list()[i];
        texcoords_2f.push_back( node.toSGVec2f() );
    }

    SGBinObject obj;
    
    obj.set_gbs_center( gbs_center );
    obj.set_gbs_radius( gbs_radius );
    obj.set_wgs84_nodes( wgs84_nodes );
    obj.set_normals( normals_3f );
    obj.set_texcoords( texcoords_2f );
    obj.set_pts_v( pts_v );
    obj.set_pts_n( pts_n );
    obj.set_pt_materials( pt_materials );
    obj.set_tris_v( tris_v );
    obj.set_tris_n( tris_n );
    obj.set_tris_tc( tris_tc ); 
    obj.set_tri_materials( tri_materials );
    obj.set_strips_v( strips_v );
    obj.set_strips_n( strips_n );
    obj.set_strips_tc( strips_tc ); 
    obj.set_strip_materials( strip_materials );
    obj.set_fans_v( fans_v );
    obj.set_fans_n( fans_n );
    obj.set_fans_tc( fans_tc );
    obj.set_fan_materials( fan_materials );
    
    bool result;
    result = obj.write_bin( base, binname, bucket );
    if ( !result ) 
    {
        throw sg_exception("error writing file. :-(");
    }
    result = obj.write_ascii( base, txtname, bucket );
    if ( !result ) 
    {
        throw sg_exception("error writing file. :-(");
    }
}

void TGConstruct::clean_clipped_polys() {
    int i, j;

    // Clean the polys
    for ( i = 0; i < TG_MAX_AREA_TYPES; ++i ) {
        for( j = 0; j < (int)polys_clipped.superpolys[i].size(); ++j ) {
            TGPolygon poly = polys_clipped.superpolys[i][j].get_poly();
            SG_LOG( SG_CLIPPER, SG_INFO, "Cleaning poly " << get_area_name( (AreaType)i ) << ":" << j+1 << " of " << polys_clipped.superpolys[i].size() );

            if ( i == 3 ) {
                poly = remove_cycles( poly );
                poly = remove_dups( poly );
                poly = remove_bad_contours( poly );
                poly = tgPolygonSimplify( poly );
                poly = remove_tiny_contours( poly );
                poly = remove_spikes( poly );
                poly = remove_dups( poly );
                poly = remove_bad_contours( poly );
                poly = remove_tiny_contours( poly );
            } else {
                poly = snap(poly, gSnap);
                poly = remove_dups( poly );
                poly = remove_bad_contours( poly );
            }

            polys_clipped.superpolys[i][j].set_poly( poly );
        }
    }
}

void TGConstruct::CalcTextureCoordinates( void )
{
    for (int i = 0; i < TG_MAX_AREA_TYPES; i++) {
        for (int j = 0; j < (int)polys_clipped.superpolys[i].size(); ++j ) {
            TGPolygon poly = polys_clipped.superpolys[i][j].get_poly();

            SG_LOG( SG_CLIPPER, SG_INFO, "Texturing " << get_area_name( (AreaType)i ) << "(" << i << ") :" << j+1 << " of " << (int)polys_clipped.superpolys[i].size() << " : flag = " << polys_clipped.superpolys[i][j].get_flag());                       

            TGPolygon tri = polys_clipped.superpolys[i][j].get_tris();
            TGPolygon tc;

            if ( polys_clipped.superpolys[i][j].get_flag() == "textured" ) {
                // SG_LOG(SG_GENERAL, SG_DEBUG, "USE TEXTURE PARAMS for tex coord calculations" );
                // tc = linear_tex_coords( tri, clipped_polys.texparams[i][j] );
                tc = area_tex_coords( tri );
            } else {
                // SG_LOG(SG_GENERAL, SG_DEBUG, "USE SIMGEAR for tex coord calculations" );
                tc = area_tex_coords( tri );
            }
      	    polys_clipped.superpolys[i][j].set_texcoords( tc );
        }
    }
}

// master construction routine
// TODO : Split each step into its own function, and move 
//        into seperate files by major functionality
//        loading, clipping, tesselating, normals, and output
//        Also, we are still calculating some thing more than one 
//        (like face area - need to move this into superpoly )
void TGConstruct::ConstructBucket( SGBucket b ) {

    sprintf(ds_name, "./construct_debug_%ld", b.gen_index() );
    ds_id = tgShapefileOpenDatasource( ds_name );

    bucket = b;

    SG_LOG(SG_GENERAL, SG_ALERT, "Construct tile, bucket = " << bucket );

    // STEP 1) 
    // Load grid of elevation data (Array)
    LoadElevationArray();
    
    // STEP 2) 
    // Clip 2D polygons against one another
    if ( LoadLandclassPolys() == 0 ) {
        // don't build the tile if there is no 2d data ... it *must*
        // be ocean and the sim can build the tile on the fly.
        return;
    }

    // STEP 3)
    // Load the land use polygons if the --cover option was specified
    if ( get_cover().size() > 0 ) {
        load_landcover();
    }

    // STEP 4)
    // Clip the Landclass polygons    
    ClipLandclassPolys(); 

    // STEP 5)
    // Merge in Shared data (just add the nodes to the nodelist)
    // When this step is complete, some nodes will have normals (from shared tiles)
    // and some will not
    LoadSharedEdgeData();

    // STEP 6) 
    // Fix T-Junctions by finding nodes that lie close to polygon edges, and
    // inserting them into the edge
    FixTJunctions();

    // TODO : Needs to be part of clipping 
    // just before union : If we need to clean again after fixing tjunctions, make 
    // sure we don't alter the shape
    clean_clipped_polys();

    // STEP 7)
    // Generate triangles - we can't generate the node-face lookup table
    // until all polys are tesselated, as extra nodes can still be generated
    TesselatePolys();

    // STEP 8) 
    // Generate triangle vertex coordinates to node index lists
    // NOTE: After this point, no new nodes can be added
    LookupNodesPerVertex();

    // STEP 9) 
    // Interpolate elevations, and flatten stuff 
    CalcElevations();
    
    // STEP 10) 
    // Generate face_connected list - 
    LookupFacesPerNode();

    // STEP 11)
    // Calculate Face Normals
    CalcFaceNormals();

    // STEP 12)
    // Calculate Point Normals
    CalcPointNormals();

#if 0
    if ( c.get_cover().size() > 0 ) {
        // Now for all the remaining "default" land cover polygons, assign
        // each one it's proper type from the land use/land cover
        // database.
        fix_land_cover_assignments( c );
    }
#endif

    // STEP 13)
    // Calculate Texture Coordinates
    CalcTextureCoordinates();

    // STEP 14)
    // Write out the shared edge data
    SaveSharedEdgeData();

    // STEP 15)
    // Generate the btg file
    WriteBtgFile();

    // STEP 16) 
    // Write Custom objects to .stg file 
    AddCustomObjects();
}
