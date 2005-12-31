// poly_support.cxx -- additional supporting routines for the TGPolygon class
//                     specific to the object building process.
//
// Written by Curtis Olson, started October 1999.
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
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
// $Id$


#include <float.h>
#include <stdio.h>

#include <simgear/compiler.h>
#include <simgear/constants.h>
#include <simgear/math/point3d.hxx>
#include <simgear/math/sg_types.hxx>
#include <simgear/debug/logstream.hxx>

#include <Polygon/polygon.hxx>
#include <Triangulate/trieles.hxx>

#define REAL double
extern "C" {
#include <TriangleJRS/triangle.h>
}
#include <TriangleJRS/tri_support.h>

#include "contour_tree.hxx"
#include "poly_support.hxx"
#include "trinodes.hxx"
#include "trisegs.hxx"

SG_USING_STD(cout);
SG_USING_STD(endl);

// Given a line segment specified by two endpoints p1 and p2, return
// the slope of the line.
static double slope( const Point3D& p0, const Point3D& p1 ) {
    if ( fabs(p0.x() - p1.x()) > SG_EPSILON ) {
	return (p0.y() - p1.y()) / (p0.x() - p1.x());
    } else {
	// return 1.0e+999; // really big number
	return DBL_MAX;
    }
}


// Given a line segment specified by two endpoints p1 and p2, return
// the y value of a point on the line that intersects with the
// verticle line through x.  Return true if an intersection is found,
// false otherwise.
static bool intersects( Point3D p0, Point3D p1, double x, Point3D *result ) {
    // sort the end points
    if ( p0.x() > p1.x() ) {
	Point3D tmp = p0;
	p0 = p1;
	p1 = tmp;
    }
    
    if ( (x < p0.x()) || (x > p1.x()) ) {
	// out of range of line segment, bail right away
	return false;
    }

    // equation of a line through (x0,y0) and (x1,y1):
    // 
    //     y = y1 + (x - x1) * (y0 - y1) / (x0 - x1)

    double y;

    if ( fabs(p0.x() - p1.x()) > SG_EPSILON ) {
	y = p1.y() + (x - p1.x()) * (p0.y() - p1.y()) / (p0.x() - p1.x());
    } else {
	return false;
    }
    result->setx(x);
    result->sety(y);

    if ( p0.y() <= p1.y() ) {
	if ( (p0.y() <= y) && (y <= p1.y()) ) {
	    return true;
	}
    } else {
 	if ( (p0.y() >= y) && (y >= p1.y()) ) {
	    return true;
	}
    }

    return false;
}


// calculate some "arbitrary" point inside the specified contour for
// assigning attribute areas
Point3D calc_point_inside_old( const TGPolygon& p, const int contour, 
			       const TGTriNodes& trinodes ) {
    Point3D tmp, min, ln, p1, p2, p3, m, result, inside_pt;
    int min_node_index = 0;
    int min_index = 0;
    int p1_index = 0;
    int p2_index = 0;
    int ln_index = 0;
    int i;

    // 1. find a point on the specified contour, min, with smallest y

    // min.y() starts greater than the biggest possible lat (degrees)
    min.sety( 100.0 );

    point_list c = p.get_contour(contour);
    point_list_iterator current, last;
    current = c.begin();
    last = c.end();

    for ( i = 0; i < p.contour_size( contour ); ++i ) {
	tmp = p.get_pt( contour, i );
	if ( tmp.y() < min.y() ) {
	    min = tmp;
	    min_index = trinodes.find( min );
	    min_node_index = i;

	    // cout << "min index = " << *current 
	    //      << " value = " << min_y << endl;
	} else {
	    // cout << "  index = " << *current << endl;
	}
    }

    //cout << "min node index = " << min_node_index << endl;
    //cout << "min index = " << min_index
	 //<< " value = " << trinodes.get_node( min_index ) 
	 //<< " == " << min << endl;

    // 2. take midpoint, m, of min with neighbor having lowest
    // fabs(slope)

    if ( min_node_index == 0 ) {
	p1 = c[1];
	p2 = c[c.size() - 1];
    } else if ( min_node_index == (int)(c.size()) - 1 ) {
	p1 = c[0];
	p2 = c[c.size() - 2];
    } else {
	p1 = c[min_node_index - 1];
	p2 = c[min_node_index + 1];
    }
    p1_index = trinodes.find( p1 );
    p2_index = trinodes.find( p2 );

    double s1 = fabs( slope(min, p1) );
    double s2 = fabs( slope(min, p2) );
    if ( s1 < s2  ) {
	ln_index = p1_index;
	ln = p1;
    } else {
	ln_index = p2_index;
	ln = p2;
    }

    TGTriSeg base_leg( min_index, ln_index, 0 );

    m.setx( (min.x() + ln.x()) / 2.0 );
    m.sety( (min.y() + ln.y()) / 2.0 );
    //cout << "low mid point = " << m << endl;

    // 3. intersect vertical line through m and all other segments of
    // all other contours of this polygon.  save point, p3, with
    // smallest y > m.y

    p3.sety(100);
    
    for ( i = 0; i < (int)p.contours(); ++i ) {
	//cout << "contour = " << i << " size = " << p.contour_size( i ) << endl;
	for ( int j = 0; j < (int)(p.contour_size( i ) - 1); ++j ) {
	    // cout << "  p1 = " << poly[i][j] << " p2 = " 
	    //      << poly[i][j+1] << endl;
	    p1 = p.get_pt( i, j );
	    p2 = p.get_pt( i, j+1 );
	    p1_index = trinodes.find( p1 );
	    p2_index = trinodes.find( p2 );
	
	    if ( intersects(p1, p2, m.x(), &result) ) {
		//cout << "intersection = " << result << endl;
		if ( ( result.y() < p3.y() ) &&
		     ( result.y() > m.y() ) &&
		     !( base_leg == TGTriSeg(p1_index, p2_index, 0) ) ) {
		    p3 = result;
		}
	    }
	}
	// cout << "  p1 = " << poly[i][0] << " p2 = " 
	//      << poly[i][poly[i].size() - 1] << endl;
	p1 = p.get_pt( i, 0 );
	p2 = p.get_pt( i, p.contour_size( i ) - 1 );
	p1_index = trinodes.find( p1 );
	p2_index = trinodes.find( p2 );
	if ( intersects(p1, p2, m.x(), &result) ) {
	    //cout << "intersection = " << result << endl;
	    if ( ( result.y() < p3.y() ) &&
		 ( result.y() > m.y() ) &&
		 !( base_leg == TGTriSeg(p1_index, p2_index, 0) ) ) {
		p3 = result;
	    }
	}
    }
    if ( p3.y() < 100 ) {
	//cout << "low intersection of other segment = " << p3 << endl;
	inside_pt = Point3D( (m.x() + p3.x()) / 2.0,
			     (m.y() + p3.y()) / 2.0,
			     0.0 );
    } else {
	cout << "Error:  Failed to find a point inside :-(" << endl;
	inside_pt = p3;
    }

    // 4. take midpoint of p2 && m as an arbitrary point inside polygon

    //cout << "inside point = " << inside_pt << endl;

    return inside_pt;
}


// basic triangulation of a polygon with out adding points or
// splitting edges, this should triangulate around interior holes.
void polygon_tesselate( const TGPolygon &p,
			triele_list &elelist,
			point_list &out_pts )
{
    struct triangulateio in, out, vorout;
    int i;

    // make sure all elements of these structs point to "NULL"
    zero_triangulateio( &in );
    zero_triangulateio( &out );
    zero_triangulateio( &vorout );
    
    int counter, start, end;

    // list of points
    double max_x = p.get_pt(0,0).x();

    int total_pts = 0;
    for ( i = 0; i < p.contours(); ++i ) {
	total_pts += p.contour_size( i );
    }

    in.numberofpoints = total_pts;
    in.pointlist = (REAL *) malloc(in.numberofpoints * 2 * sizeof(REAL));

    counter = 0;
    for ( i = 0; i < p.contours(); ++i ) {
	point_list contour = p.get_contour( i );
	for ( int j = 0; j < (int)contour.size(); ++j ) {
	    in.pointlist[2*counter] = contour[j].x();
	    in.pointlist[2*counter + 1] = contour[j].y();
	    if ( contour[j].x() > max_x ) {
		    max_x = contour[j].x();
	    }
	    ++counter;
	}
    }

    in.numberofpointattributes = 1;
    in.pointattributelist = (REAL *) malloc(in.numberofpoints *
					    in.numberofpointattributes *
					    sizeof(REAL));
    counter = 0;
    for ( i = 0; i < p.contours(); ++i ) {
	point_list contour = p.get_contour( i );
	for ( int j = 0; j < (int)contour.size(); ++j ) {
	    in.pointattributelist[counter] = contour[j].z();
	    ++counter;
	}
    }

    // in.pointmarkerlist = (int *) malloc(in.numberofpoints * sizeof(int));
    // for ( i = 0; i < in.numberofpoints; ++i) {
    //    in.pointmarkerlist[i] = 0;
    // }
    in.pointmarkerlist = NULL;

    // segment list
    in.numberofsegments = in.numberofpoints;
    in.segmentlist = (int *) malloc(in.numberofsegments * 2 * sizeof(int));
    counter = 0;
    start = 0;
    end = -1;

    for ( i = 0; i < p.contours(); ++i ) {
	point_list contour = p.get_contour( i );
	start = end + 1;
	end = start + contour.size() - 1;
	for ( int j = 0; j < (int)contour.size() - 1; ++j ) {
	    in.segmentlist[counter++] = j + start;
	    in.segmentlist[counter++] = j + start + 1;
	}
	in.segmentlist[counter++] = end;
	in.segmentlist[counter++] = start;
    }

    in.segmentmarkerlist = (int *) malloc(in.numberofsegments * sizeof(int));
    for ( i = 0; i < in.numberofsegments; ++i ) {
       in.segmentmarkerlist[i] = 0;
    }

    // hole list
    in.numberofholes = 1;
    for ( i = 0; i < p.contours(); ++i ) {
	if ( p.get_hole_flag( i ) ) {
	    ++in.numberofholes;
	}
    }
    in.holelist = (REAL *) malloc(in.numberofholes * 2 * sizeof(REAL));
    // outside of polygon
    counter = 0;
    in.holelist[counter++] = max_x + 1.0;
    in.holelist[counter++] = 0.0;

    for ( i = 0; i < (int)p.contours(); ++i ) {
	if ( p.get_hole_flag( i ) ) {
	    in.holelist[counter++] = p.get_point_inside(i).x();
	    in.holelist[counter++] = p.get_point_inside(i).y();
	}
    }

    // region list
    in.numberofregions = 0;
    in.regionlist = NULL;

    // no triangle list
    in.numberoftriangles = 0;
    in.numberofcorners = 0;
    in.numberoftriangleattributes = 0;
    in.trianglelist = NULL;
    in.triangleattributelist = NULL;
    in.trianglearealist = NULL;
    in.neighborlist = NULL;

    // no edge list
    in.numberofedges = 0;
    in.edgelist = NULL;
    in.edgemarkerlist = NULL;
    in.normlist = NULL;

    // dump the results to screen
    // print_tri_data( &in );

    // TEMPORARY
    // write_tri_data(&in);
    /* cout << "Press return to continue:";
    char junk;
    cin >> junk; */

    // Triangulate the points.  Switches are chosen to read and write
    // a PSLG (p), number everything from zero (z), and produce an
    // edge list (e), and a triangle neighbor list (n).
    // no new points on boundary (Y), no internal segment
    // splitting (YY), no quality refinement (q)
    // Quite (Q)

    string tri_options;
    tri_options = "pzYYenQ";	// add multiple "V" entries for verbosity
    //cout << "Triangulation with options = " << tri_options << endl;

    triangulate( (char *)tri_options.c_str(), &in, &out, &vorout );

    // TEMPORARY
    // write_tri_data(&out);

    // now copy the results back into the corresponding TGTriangle
    // structures

    // triangles
    elelist.clear();
    int n1, n2, n3;
    double attribute;
    for ( i = 0; i < out.numberoftriangles; ++i ) {
	n1 = out.trianglelist[i * 3];
	n2 = out.trianglelist[i * 3 + 1];
	n3 = out.trianglelist[i * 3 + 2];
	if ( out.numberoftriangleattributes > 0 ) {
	    attribute = out.triangleattributelist[i];
	} else {
	    attribute = 0.0;
	}
	// cout << "triangle = " << n1 << " " << n2 << " " << n3 << endl;

	elelist.push_back( TGTriEle( n1, n2, n3, attribute ) );
    }

    // output points
    out_pts.clear();
    double x, y, z;
    for ( i = 0; i < out.numberofpoints; ++i ) {
	x = out.pointlist[i * 2    ];
	y = out.pointlist[i * 2 + 1];
	z = out.pointattributelist[i];
	out_pts.push_back( Point3D(x, y, z) );
    }
   
    // free mem allocated to the "Triangle" structures
    free(in.pointlist);
    free(in.pointattributelist);
    free(in.pointmarkerlist);
    free(in.regionlist);
    free(out.pointlist);
    free(out.pointattributelist);
    free(out.pointmarkerlist);
    free(out.trianglelist);
    free(out.triangleattributelist);
    // free(out.trianglearealist);
    free(out.neighborlist);
    free(out.segmentlist);
    free(out.segmentmarkerlist);
    free(out.edgelist);
    free(out.edgemarkerlist);
    free(vorout.pointlist);
    free(vorout.pointattributelist);
    free(vorout.edgelist);
    free(vorout.normlist);
}


// Alternate basic triangulation of a polygon with out adding points
// or splitting edges and without regard for holes.  Returns a polygon
// with one contour per tesselated triangle.  This is mostly just a
// wrapper for the polygon_tesselate() function.  Note, this routine
// will modify the points_inside list for your polygon.

TGPolygon polygon_tesselate_alt( TGPolygon &p ) {
    TGPolygon result;
    result.erase();
    int i;

    // Bail right away if polygon is empty
    if ( p.contours() == 0 ) {
	return result;
    }

    // 1.  Robustly find a point inside each contour that is not
    //     inside any other contour
    calc_points_inside( p );
    for ( i = 0; i < p.contours(); ++i ) {
	//cout << "final point inside =" << p.get_point_inside( i )
	//     << endl;
    }

    // 2.  Do a final triangulation of the entire polygon
    triele_list trieles;
    point_list nodes;
    polygon_tesselate( p, trieles, nodes );

    // 3.  Convert the tesselated output to a list of tringles.
    //     basically a polygon with a contour for every triangle
    for ( i = 0; i < (int)trieles.size(); ++i ) {
	TGTriEle t = trieles[i];
	Point3D p1 = nodes[ t.get_n1() ];
	Point3D p2 = nodes[ t.get_n2() ];
	Point3D p3 = nodes[ t.get_n3() ];
	result.add_node( i, p1 );
	result.add_node( i, p2 );
	result.add_node( i, p3 );
    }

    return result;
}


// basic triangulation of a contour with out adding points or
// splitting edges but cuts out any of the specified holes
static void contour_tesselate( TGContourNode *node, const TGPolygon &p,
			       const TGPolygon &hole_polys,
			       const point_list &hole_pts,
			       triele_list &elelist,
			       point_list &out_pts )
{
    struct triangulateio in, out, vorout;
    int i;

    // make sure all elements of these structs point to "NULL"
    zero_triangulateio( &in );
    zero_triangulateio( &out );
    zero_triangulateio( &vorout );
    
    int counter, start, end;

    // list of points
    int contour_num = node->get_contour_num();
    // cout << "Tesselating contour = " << contour_num << endl;
    point_list contour = p.get_contour( contour_num );

    double max_x = contour[0].x();

    int total_pts = contour.size();
    // cout << "contour = " << contour_num << " nodes = " << total_pts << endl;

#if 0
    // testing ... don't enable this if not testing
    if ( contour_num != 0 || total_pts != 28 ) {
	out_pts.push_back( Point3D(0, 0, 0) );
	out_pts.push_back( Point3D(0, 1, 0) );
	out_pts.push_back( Point3D(1, 1, 0) );
	elelist.push_back( TGTriEle( 0, 1, 2, 0.0 ) );
	return;
    }
#endif

    for ( i = 0; i < hole_polys.contours(); ++i ) {
	total_pts += hole_polys.contour_size( i );
    }

    in.numberofpoints = total_pts;
    // cout << "total points = " << total_pts << endl;
    in.pointlist = (REAL *) malloc(in.numberofpoints * 2 * sizeof(REAL));

    counter = 0;
    for ( i = 0; i < (int)contour.size(); ++i ) {
	in.pointlist[2*counter] = contour[i].x();
	in.pointlist[2*counter + 1] = contour[i].y();
	if ( contour[i].x() > max_x ) {
	    max_x = contour[i].x();
	}
	++counter;
    }

    for ( i = 0; i < hole_polys.contours(); ++i ) {
	point_list hole_contour = hole_polys.get_contour( i );
	for ( int j = 0; j < (int)hole_contour.size(); ++j ) {
	    in.pointlist[2*counter] = hole_contour[j].x();
	    in.pointlist[2*counter + 1] = hole_contour[j].y();
	    if ( hole_contour[j].x() > max_x ) {
		    max_x = hole_contour[j].x();
	    }
	    ++counter;
	}
    }

    in.numberofpointattributes = 1;
    in.pointattributelist = (REAL *) malloc(in.numberofpoints *
					    in.numberofpointattributes *
					    sizeof(REAL));
    counter = 0;
    for ( i = 0; i < (int)contour.size(); ++i ) {
	in.pointattributelist[counter] = contour[i].z();
	++counter;
    }

    for ( i = 0; i < hole_polys.contours(); ++i ) {
	point_list hole_contour = hole_polys.get_contour( i );
	for ( int j = 0; j < (int)hole_contour.size(); ++j ) {
	    in.pointattributelist[counter] = hole_contour[j].z();
	    ++counter;
	}
    }

    // in.pointmarkerlist = (int *) malloc(in.numberofpoints * sizeof(int));
    // for ( i = 0; i < in.numberofpoints; ++i) {
    //     in.pointmarkerlist[i] = 0;
    // }
    in.pointmarkerlist = NULL;

    // segment list
    in.numberofsegments = in.numberofpoints;
    in.segmentlist = (int *) malloc(in.numberofsegments * 2 * sizeof(int));
    in.segmentmarkerlist = (int *) malloc(in.numberofsegments * sizeof(int));
    counter = 0;
    start = 0;
    end = contour.size() - 1;
    for ( i = 0; i < end; ++i ) {
	in.segmentlist[counter++] = i;
	in.segmentlist[counter++] = i + 1;
	in.segmentmarkerlist[i] = 0;
    }
    in.segmentlist[counter++] = end;
    in.segmentlist[counter++] = start;
    in.segmentmarkerlist[contour.size() - 1] = 0;

    for ( i = 0; i < hole_polys.contours(); ++i ) {
	point_list hole_contour = hole_polys.get_contour( i );
	start = end + 1;
	end = start + hole_contour.size() - 1;
	for ( int j = 0; j < (int)hole_contour.size() - 1; ++j ) {
	    in.segmentlist[counter++] = j + start;
	    in.segmentlist[counter++] = j + start + 1;
	}
	in.segmentlist[counter++] = end;
	in.segmentlist[counter++] = start;
    }

    for ( i = 0; i < in.numberofsegments; ++i ) {
	in.segmentmarkerlist[i] = 0;
    }

    // hole list
    in.numberofholes = hole_pts.size() + 1;
    in.holelist = (REAL *) malloc(in.numberofholes * 2 * sizeof(REAL));
    // outside of polygon
    counter = 0;
    in.holelist[counter++] = max_x + 1.0;
    in.holelist[counter++] = 0.0;

    for ( i = 0; i < (int)hole_pts.size(); ++i ) {
	in.holelist[counter++] = hole_pts[i].x();
	in.holelist[counter++] = hole_pts[i].y();
    }
    // region list
    in.numberofregions = 0;
    in.regionlist = NULL;

    // no triangle list
    in.numberoftriangles = 0;
    in.numberofcorners = 0;
    in.numberoftriangleattributes = 0;
    in.trianglelist = NULL;
    in.triangleattributelist = NULL;
    in.trianglearealist = NULL;
    in.neighborlist = NULL;

    // no edge list
    in.numberofedges = 0;
    in.edgelist = NULL;
    in.edgemarkerlist = NULL;
    in.normlist = NULL;

    // dump the results to screen
    // print_tri_data( &in );

    // TEMPORARY
    // write_tri_data(&in);
    /* cout << "Press return to continue:";
    char junk;
    cin >> junk; */

    // Triangulate the points.  Switches are chosen to read and write
    // a PSLG (p), number everything from zero (z), and produce an
    // edge list (e), and a triangle neighbor list (n).
    // no new points on boundary (Y), no internal segment
    // splitting (YY), no quality refinement (q)
    // Quite (Q)

    string tri_options;
    tri_options = "pzYYenQ";	// add multiple "V" entries for verbosity
    //cout << "Triangulation with options = " << tri_options << endl;

    triangulate( (char *)tri_options.c_str(), &in, &out, &vorout );

    // TEMPORARY
    // write_tri_data(&out);

    // now copy the results back into the corresponding TGTriangle
    // structures

    // triangles
    elelist.clear();
    int n1, n2, n3;
    double attribute;
    for ( i = 0; i < out.numberoftriangles; ++i ) {
	n1 = out.trianglelist[i * 3];
	n2 = out.trianglelist[i * 3 + 1];
	n3 = out.trianglelist[i * 3 + 2];
	if ( out.numberoftriangleattributes > 0 ) {
	    attribute = out.triangleattributelist[i];
	} else {
	    attribute = 0.0;
	}
	// cout << "triangle = " << n1 << " " << n2 << " " << n3 << endl;

	elelist.push_back( TGTriEle( n1, n2, n3, attribute ) );
    }

    // output points
    out_pts.clear();
    double x, y, z;
    for ( i = 0; i < out.numberofpoints; ++i ) {
	x = out.pointlist[i * 2    ];
	y = out.pointlist[i * 2 + 1];
	z = out.pointattributelist[i];
	out_pts.push_back( Point3D(x, y, z) );
    }
   
    // free mem allocated to the "Triangle" structures
    free(in.pointlist);
    free(in.pointattributelist);
    free(in.pointmarkerlist);
    free(in.regionlist);
    free(out.pointlist);
    free(out.pointattributelist);
    free(out.pointmarkerlist);
    free(out.trianglelist);
    free(out.triangleattributelist);
    // free(out.trianglearealist);
    free(out.neighborlist);
    free(out.segmentlist);
    free(out.segmentmarkerlist);
    free(out.edgelist);
    free(out.edgemarkerlist);
    free(vorout.pointlist);
    free(vorout.pointattributelist);
    free(vorout.edgelist);
    free(vorout.normlist);
}


#if 0
// depricated code, also note the method for finding the center point
// of a triangle is depricated and weights the 3 vertices unevenly.

// Find a point inside the polygon without regard for holes
static Point3D point_inside_hole( point_list contour ) {

    triele_list elelist; = contour_tesselate( contour );
    if ( elelist.size() <= 0 ) {
	cout << "Error polygon triangulated to zero triangles!" << endl;
	exit(-1);
    }

    TGTriEle t = elelist[0];
    Point3D p1 = contour[ t.get_n1() ];
    Point3D p2 = contour[ t.get_n2() ];
    Point3D p3 = contour[ t.get_n3() ];

    Point3D m1 = ( p1 + p2 ) / 2;
    Point3D m2 = ( p1 + p3 ) / 2;

    Point3D center = ( m1 + m2 ) / 2;

    return center;
}
#endif


// Find a point inside a specific polygon contour taking holes into
// consideration
static Point3D point_inside_contour( TGContourNode *node, const TGPolygon &p ) {
    int contour_num;
    int i;

    TGPolygon hole_polys;
    hole_polys.erase();

    point_list hole_pts;
    hole_pts.clear();

    // build list of hole points
    // cout << "contour has " << node->get_num_kids() << " kids" << endl;
    for ( i = 0; i < node->get_num_kids(); ++i ) {
        if ( node->get_kid( i ) != NULL ) {
	    contour_num = node->get_kid(i)->get_contour_num();
	    // cout << "  child = " << contour_num << endl;
	    hole_pts.push_back( p.get_point_inside( contour_num ) );
	    point_list contour = p.get_contour( contour_num );
	    hole_polys.add_contour( contour, 1 );
        }
    }

    triele_list elelist;
    point_list out_pts;
    // cout << "before contour tesselate" << endl;
    contour_tesselate( node, p, hole_polys, hole_pts, elelist, out_pts );
    if ( elelist.size() <= 0 ) {
	cout << "Error polygon triangulated to zero triangles!" << endl;
	return Point3D( -200, -200, 0 );
	// exit(-1);
    }
    // cout << "after contour tesselate" << endl;

    // find the largest triangle in the group
    double max_area = 0.0;
    int biggest = 0;
    for ( i = 0; i < (int)elelist.size(); ++i ) {
	TGTriEle t = elelist[i];
	Point3D p1 = out_pts[ t.get_n1() ];
	Point3D p2 = out_pts[ t.get_n2() ];
	Point3D p3 = out_pts[ t.get_n3() ];
	double area = triangle_area( p1, p2, p3 );
	if ( area > max_area ) {
	    max_area = area;
	    biggest = i;
	}
    }

    // find center point of largest triangle
    //cout << "biggest = " << biggest + 1 << " out of " << elelist.size() << endl;
    TGTriEle t = elelist[biggest];
    contour_num = node->get_contour_num();
    Point3D p1 = out_pts[ t.get_n1() ];
    Point3D p2 = out_pts[ t.get_n2() ];
    Point3D p3 = out_pts[ t.get_n3() ];
    //cout << "hole tri = " << p1 << endl << "  " << p2 << endl << "  " << p3 << endl;

    // new
    Point3D center = ( p1 + p2 + p3 ) / 3;

    return center;

}


// recurse the contour tree and build up the point inside list for
// each contour/hole
static void calc_point_inside( TGContourNode *node, TGPolygon &p ) {
    int contour_num = node->get_contour_num();
    // cout << "starting calc_point_inside() with contour = " << contour_num
    //      << endl;

    for ( int i = 0; i < node->get_num_kids(); ++i ) {
	if ( node->get_kid( i ) != NULL ) {
	    calc_point_inside( node->get_kid( i ), p );
	}
    }

    if ( contour_num >= 0 ) {
	Point3D pi = point_inside_contour( node, p );
	// cout << endl << "point inside(" << contour_num << ") = " << pi
	//      << endl << endl;
	p.set_point_inside( contour_num, pi );
    }
}


static void print_contour_tree( TGContourNode *node, string indent ) {
    cout << indent << node->get_contour_num() << endl;

    indent += "  ";
    for ( int i = 0; i < node->get_num_kids(); ++i ) {
	if ( node->get_kid( i ) != NULL ) {
	    print_contour_tree( node->get_kid( i ), indent );
	}
    }
}


// Build the contour "is inside of" tree
static void build_contour_tree( TGContourNode *node,
				const TGPolygon &p,
				int_list &avail )
{
    //cout << "working on contour = " << node->get_contour_num() << endl;
    //cout << "  total contours = " << p.contours() << endl;
    TGContourNode *tmp;
    int i;

    // see if we are building on a hole or not
    bool flag;
    if ( node->get_contour_num() >= 0 ) {
	flag = (bool)p.get_hole_flag( node->get_contour_num() );
    } else {
	flag = true;
    }
    //cout << "  hole flag = " << flag << endl;

    // add all remaining hole/non-hole contours as children of the
    // current node if they are inside of it.
    for ( i = 0; i < p.contours(); ++i ) {
	//cout << "  testing contour = " << i << endl;
	if ( p.get_hole_flag( i ) != flag ) {
	    // only holes can be children of non-holes and visa versa
	    if ( avail[i] ) {
		// must still be an available contour
		int cur_contour = node->get_contour_num();
		if ( (cur_contour < 0 ) || p.is_inside( i, cur_contour ) ) {
		    // must be inside the parent (or if the parent is
		    // the root, add all available non-holes.
		    //cout << "  adding contour = " << i << endl;
		    avail[i] = 0;
		    tmp = new TGContourNode( i );
		    node->add_kid( tmp );
		} else {
		    //cout << "  not inside" << endl;
		}
	    } else {
		//cout << "  not available" << endl;
	    }
	} else {
	    //cout << "  wrong hole/non-hole type" << endl;
	}
    }

    // if any of the children are inside of another child, remove the
    // inside one
    //cout << "node now has num kids = " << node->get_num_kids() << endl;

    for ( i = 0; i < node->get_num_kids(); ++i ) {
	for ( int j = 0; j < node->get_num_kids(); ++j ) {
	    // cout << "working on kid " << i << ", " << j << endl;
	    if ( i != j ) {
		if ( (node->get_kid(i) != NULL)&&(node->get_kid(j) != NULL) ) {
		    int A = node->get_kid( i )->get_contour_num();
		    int B = node->get_kid( j )->get_contour_num();
		    if ( p.is_inside( A, B ) ) {
			// p.write_contour( i, "a" );
			// p.write_contour( j, "b" );
			// exit(-1);
		        // need to remove contour j from the kid list
		        avail[ node->get_kid( i ) -> get_contour_num() ] = 1;
		        node->remove_kid( i );
		        //cout << "removing contour " << A 
                        //     << " which is inside of contour " << B << endl;
			continue;
		    }
		} else {
		    // one of these kids is already NULL, skip
		}
	    } else {
		// doesn't make sense to check if a contour is inside itself
	    }
	}
    }

    // for each child, extend the contour tree
    for ( i = 0; i < node->get_num_kids(); ++i ) {
	tmp = node->get_kid( i );
	if ( tmp != NULL ) {
	    build_contour_tree( tmp, p, avail );
	}
    }
}


// calculate some "arbitrary" point inside each of the polygons contours
void calc_points_inside( TGPolygon& p ) {
    // first build the contour tree

    // make a list of all still available contours (all of the for
    // starters)
    int_list avail;
    for ( int i = 0; i < p.contours(); ++i ) {
	avail.push_back( 1 );
    }

    // create and initialize the root node
    TGContourNode *ct = new TGContourNode( -1 );

    // recursively build the tree
    // cout << "building contour tree" << endl;
    build_contour_tree( ct, p, avail );
    // print_contour_tree( ct, "" );

    // recurse the tree and build up the point inside list for each
    // contour/hole
    // cout << " calc_point_inside()\n";
    calc_point_inside( ct, p );
}


// remove duplicate nodes in a polygon should they exist.  Returns the
// fixed polygon
TGPolygon remove_dups( const TGPolygon &poly ) {
    TGPolygon result;
    point_list contour, new_contour;
    result.erase();

    TGPolygon tmp = poly;
    for ( int i = 0; i < tmp.contours(); ++i ) {
	contour = poly.get_contour( i );
	// cout << "testing contour " << i << "  size = " << contour.size() 
	//      << "  hole = " << poly.get_hole_flag( i ) << endl;
	bool have_dups = true;
	while ( have_dups ) {
	    have_dups = false;
	    new_contour.clear();
	    Point3D last = contour[ contour.size() - 1 ];
	    for ( int j = 0; j < (int)contour.size(); ++j ) {
		// cout << "  " << i << " " << j << endl;
		Point3D cur = contour[j];
		if ( cur == last ) {
		    have_dups = true;
		    // cout << "skipping a duplicate point" << endl;
		} else {
		    new_contour.push_back( cur );
		    last = cur;
		}
	    }
	    contour = new_contour;
	}

	// cout << "  final size = " << contour.size() << endl;

	if ( contour.size() ) {
	    int flag = poly.get_hole_flag( i );
	    result.add_contour( contour, flag );
	} else {
	    // too small an area ... add a token point to the contour
	    // to keep other things happy, but this "bad" contour will
	    // get nuked later
	    result.add_node( i, poly.get_pt( i, 0 ) );
	}
    }

    return result;
}


static inline double
snap (double value, double grid_size)
{
				// I have no idea if this really works.
  double factor = 1.0 / grid_size;
  return double(int(value * factor)) / factor;
}

static inline Point3D
snap (const Point3D &p, double grid_size)
{
  Point3D result;
  result.setx(snap(p.x(), grid_size));
  result.sety(snap(p.y(), grid_size));
  result.setz(snap(p.z(), grid_size));
  //cout << result << endl;
  return result;
}

// snap all points in a polygon to the given grid size.
TGPolygon snap (const TGPolygon &poly, double grid_size)
{
  TGPolygon result;
  for (int contour = 0; contour < poly.contours(); contour++) {
    for (int i = 0; i < poly.contour_size(contour); i++) {
      result.add_node(contour, snap(poly.get_pt(contour, i), grid_size));
    }
    result.set_hole_flag(contour, poly.get_hole_flag(contour));
  }
  return result;
}


// static const double tgAirportEpsilon = SG_EPSILON / 10.0;
static const double tgAirportEpsilon = SG_EPSILON;


// Find a point in the given node list that lies between start and
// end, return true if something found, false if nothing found.
bool find_intermediate_node( const Point3D& start, const Point3D& end,
			     const point_list& nodes, Point3D *result )
{
    bool found_node = false;
    double m, m1, b, b1, y_err, x_err, y_err_min, x_err_min;

    Point3D p0 = start;
    Point3D p1 = end;

    // cout << "  find_intermediate_nodes() " << p0 << " <=> " << p1 << endl;

    double xdist = fabs(p0.x() - p1.x());
    double ydist = fabs(p0.y() - p1.y());
    // cout << "xdist = " << xdist << "  ydist = " << ydist << endl;
    x_err_min = xdist + 1.0;
    y_err_min = ydist + 1.0;

    if ( xdist > ydist ) {
	// cout << "use y = mx + b" << endl;

	// sort these in a sensible order
	Point3D p_min, p_max;
	if ( p0.x() < p1.x() ) {
	    p_min = p0;
	    p_max = p1;
	} else {
	    p_min = p1;
	    p_max = p0;
	}

	m = (p_min.y() - p_max.y()) / (p_min.x() - p_max.x());
	b = p_max.y() - m * p_max.x();

	// cout << "m = " << m << " b = " << b << endl;

	for ( int i = 0; i < (int)nodes.size(); ++i ) {
	    // cout << i << endl;
	    Point3D current = nodes[i];

	    if ( (current.x() > (p_min.x() + SG_EPSILON)) 
		 && (current.x() < (p_max.x() - SG_EPSILON)) ) {

		// printf( "found a potential candidate %.7f %.7f %.7f\n",
		//         current.x(), current.y(), current.z() );

		y_err = fabs(current.y() - (m * current.x() + b));
		// cout << "y_err = " << y_err << endl;

		if ( y_err < tgAirportEpsilon ) {
		    // cout << "FOUND EXTRA SEGMENT NODE (Y)" << endl;
		    // cout << p_min << " < " << current << " < "
		    //      << p_max << endl;
		    found_node = true;
		    if ( y_err < y_err_min ) {
			*result = current;
			y_err_min = y_err;
		    }
		}
	    }
	}
    } else {
	// cout << "use x = m1 * y + b1" << endl;

	// sort these in a sensible order
	Point3D p_min, p_max;
	if ( p0.y() < p1.y() ) {
	    p_min = p0;
	    p_max = p1;
	} else {
	    p_min = p1;
	    p_max = p0;
	}

	m1 = (p_min.x() - p_max.x()) / (p_min.y() - p_max.y());
	b1 = p_max.x() - m1 * p_max.y();

	// cout << "  m1 = " << m1 << " b1 = " << b1 << endl;
	// printf( "  m = %.8f  b = %.8f\n", 1/m1, -b1/m1);

	// cout << "  should = 0 = "
	//      << fabs(p_min.x() - (m1 * p_min.y() + b1)) << endl;
	// cout << "  should = 0 = "
	//      << fabs(p_max.x() - (m1 * p_max.y() + b1)) << endl;

	for ( int i = 0; i < (int)nodes.size(); ++i ) {
	    Point3D current = nodes[i];

	    if ( (current.y() > (p_min.y() + SG_EPSILON)) 
		 && (current.y() < (p_max.y() - SG_EPSILON)) ) {
		
		// printf( "found a potential candidate %.7f %.7f %.7f\n",
		//         current.x(), current.y(), current.z() );

		x_err = fabs(current.x() - (m1 * current.y() + b1));
		// cout << "x_err = " << x_err << endl;

		// if ( temp ) {
		// cout << "  (" << counter << ") x_err = " << x_err << endl;
		// }

		if ( x_err < tgAirportEpsilon ) {
		    // cout << "FOUND EXTRA SEGMENT NODE (X)" << endl;
		    // cout << p_min << " < " << current << " < "
		    //      << p_max << endl;
		    found_node = true;
		    if ( x_err < x_err_min ) {
			*result = current;
			x_err_min = x_err;
		    }
		}
	    }
	}
    }

    return found_node;
}


// Attempt to reduce degeneracies where a subsequent point of a
// polygon lies *on* a previous line segment.  These artifacts are
// occasionally introduced by the gpc polygon clipper.
static point_list reduce_contour_degeneracy( const point_list& contour ) {
    point_list result = contour;

    Point3D p0, p1, bad_node;
    bool done = false;

    while ( !done ) {
	// traverse the contour until we find the first bad node or
	// hit the end of the contour
        // cout << "   ... reduce_degeneracy(): not done ... " << endl;
	bool bad = false;

	int i = 0;
	int j = 0;

	// look for first stray intermediate nodes
	while ( i < (int)result.size() - 1 && !bad ) {
	    p0 = result[i];
	    p1 = result[i+1];
	
	    bad = find_intermediate_node( p0, p1, result, &bad_node );
	    // if ( bad ) { cout << "bad in n-1 nodes" << endl; }
	    ++i;
	}
	if ( !bad ) {
	    // do the end/start connecting segment
	    p0 = result[result.size() - 1];
	    p1 = result[0];

	    bad = find_intermediate_node( p0, p1, result, &bad_node );
	    // if ( bad ) { cout << "bad in 0 to n segment" << endl; }
	}

	// CLO: look for later nodes that match earlier segment end points
	// (i.e. a big cycle.)  There's no good automatic fix to this
	// so just drop the problematic point and live with our
	// contour changing shape.
	//
	// WARNING: FIXME??? By changing the shape of a polygon at
	// this point we are introducing some overlap which could show
	// up as flickering/depth-buffer fighting at run time.  The
	// better fix would be to split up the two regions into
	// separate contours, but I just don't have the energy to
	// think though that right now.
	if ( !bad ) {
	    i = 0;
	    while ( i < (int)result.size() - 1 && !bad ) {
	        j = i + 1;
		while ( j < (int)result.size() && !bad ) {
		    if ( result[i] == result[j] ) {
		        bad = true;
			bad_node = result[j];
			// cout << "size = " << result.size() << " i = "
			//      << i << " j = " << j << " result[i] = "
			//      << result[i] << " result[j] = " << result[j]
			//      << endl;
		    }
		    j++;
		}
		i++;
	    }
	}

	if ( bad ) {
	    // remove bad node from contour.  But only remove one node.  If
	    // the 'badness' is caused by coincident adjacent nodes, we don't
	    // want to remove both of them, just one (either will do.)
  	    cout << "found a bad node = " << bad_node << endl;
	    point_list tmp; tmp.clear();
	    bool found_one = false;
	    for ( int j = 0; j < (int)result.size(); ++j ) {
		if ( result[j] == bad_node && !found_one) {
		    // skip
		    found_one = true;
		} else {
		    tmp.push_back( result[j] );
		}
	    }
	    result = tmp;
	} else { 
	    done = true;
	}
    }

    return result;
}

// Search each segment of each contour for degenerate points (i.e. out
// of order points that lie coincident on other segments
TGPolygon reduce_degeneracy( const TGPolygon& poly ) {
    TGPolygon result;

    for ( int i = 0; i < poly.contours(); ++i ) {
        // cout << "reduce_degeneracy() contour = " << i << endl;
	point_list contour = poly.get_contour(i);
	contour = reduce_contour_degeneracy( contour );
	result.add_contour( contour, poly.get_hole_flag(i) );

	// maintain original hole flag setting
	// result.set_hole_flag( i, poly.get_hole_flag( i ) );
    }

    return result;
}

// find short cycles in a contour and snip them out.
static point_list remove_small_cycles( const point_list& contour ) {
    point_list result;
    result.clear();

    unsigned int i = 0;
    while ( i < contour.size() ) {
        result.push_back( contour[i] );
        for ( unsigned int j = i + 1; j < contour.size(); ++j ) {
            if ( contour[i] == contour[j] && i + 4 > j ) {
                cout << "detected a small cycle: i = "
                     << i << " j = " << j << endl;
                for ( unsigned int k = i; k <= j; ++k ) {
                    cout << "  " << contour[k] << endl;
                }
                i = j;
            }
        }
        ++i;
    }

    return result;
}


// Occasionally the outline of the clipped polygon can take a side
// track, then double back on return to the start of the side track
// branch and continue normally.  Attempt to detect and clear this
// extraneous nonsense.
TGPolygon remove_cycles( const TGPolygon& poly ) {
    TGPolygon result;
    // cout << "remove cycles: " << poly << endl;
    for ( int i = 0; i < poly.contours(); ++i ) {
	point_list contour = poly.get_contour(i);
	contour = remove_small_cycles( contour );
	result.add_contour( contour, poly.get_hole_flag(i) );
    }

    return result;
}


// remove any degenerate contours
TGPolygon remove_bad_contours( const TGPolygon &poly ) {
    TGPolygon result;
    result.erase();

    for ( int i = 0; i < poly.contours(); ++i ) {
	point_list contour = poly.get_contour( i );
	if ( contour.size() >= 3 ) {
	    // good
	    int flag = poly.get_hole_flag( i );
	    result.add_contour( contour, flag );
	} else {
	    //cout << "tossing a bad contour" << endl;
	}
    }

    return result;
}


