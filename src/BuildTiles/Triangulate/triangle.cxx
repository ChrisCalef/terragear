// triangle.cxx -- "Triangle" interface class
//
// Written by Curtis Olson, started March 1999.
//
// Copyright (C) 1999  Curtis L. Olson  - curt@flightgear.org
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

#include <simgear/compiler.h>

#include <Geometry/poly_support.hxx>
#include <Polygon/polygon.hxx>
#include <TriangleJRS/tri_support.h>

#include "triangle.hxx"

#if !defined (SG_HAVE_NATIVE_SGI_COMPILERS)
SG_USING_STD(cout);
SG_USING_STD(endl);
#endif


// Constructor
FGTriangle::FGTriangle( void ) {
}


// Destructor
FGTriangle::~FGTriangle( void ) {
}


// populate this class based on the specified gpc_polys list
int 
FGTriangle::build( const point_list& corner_list,
		   const point_list& fit_list, 
		   const FGPolyList& gpc_polys )
{
    int debug_counter = 0;
    int index;
    int i;

    in_nodes.clear();
    in_segs.clear();

    // Point3D junkp;
    // int junkc = 0;
    // FILE *junkfp;

    // traverse the dem corner and fit lists and gpc_polys building a
    // unified node list and converting the polygons so that they
    // reference the node list by index (starting at zero) rather than
    // listing the points explicitely

    // first the corners since these are important
    const_point_list_iterator f_current, f_last;
    f_current = corner_list.begin();
    f_last = corner_list.end();
    for ( ; f_current != f_last; ++f_current ) {
	index = in_nodes.unique_add( *f_current );
    }

    // next process the polygons
    FGPolygon gpc_poly;
    const_poly_list_iterator current, last;

    // process polygons in priority order
    cout << "prepairing node list and polygons" << endl;

    for ( i = 0; i < FG_MAX_AREA_TYPES; ++i ) {
	polylist[i].clear();

	cout << "area type = " << i << " polys = " << gpc_polys.polys[i].size() 
	     << endl;
	debug_counter = 0;
	current = gpc_polys.polys[i].begin();
	last = gpc_polys.polys[i].end();
	for ( ; current != last; ++current ) {
	    gpc_poly = *current;

	    cout << "processing a polygon, contours = " 
		 << gpc_poly.contours() << endl;

	    if (gpc_poly.contours() <= 0 ) {
		cout << "FATAL ERROR! no contours in this polygon" << endl;
		exit(-1);
	    }

	    int j;
	    for ( j = 0; j < gpc_poly.contours(); ++j ) {
		cout << "  processing contour = " << j << ", nodes = " 
		     << gpc_poly.contour_size( j ) << ", hole = "
		     << gpc_poly.get_hole_flag( j ) << endl;

		/*
		char junkn[256];
		sprintf(junkn, "c%d", j);
		gpc_poly.write_contour( j, junkn );
		*/

		for ( int k = 0; k < gpc_poly.contour_size( j ); k++ ) {
		    Point3D p = gpc_poly.get_pt( j, k );
		    index = in_nodes.unique_add( p );
		    // junkp = in_nodes.get_node( index );
		    // fprintf(junkfp, "%.4f %.4f\n", junkp.x(), junkp.y());
		    // cout << "  - " << index << endl;
		}
		// fprintf(junkfp, "%.4f %.4f\n", 
		//    gpc_poly->contour[j].vertex[0].x, 
		//    gpc_poly->contour[j].vertex[0].y);
		// fclose(junkfp);
	    }

	    /* if ( i == OceanArea ) {
		cout << "temporary exit point" << endl;
		exit(-1);
	    } */

	    // for each contour, calculate a point inside (but not
	    // also inside any interior contours

	    // new way

	    // try to make sure our polygons aren't goofy
#if 0
            // CLO 09/18/2001: if we snap polygons including holes
            // will this screw up the edge matching when objects are
            // inserted into their holes?
	    gpc_poly = snap(gpc_poly, 0.000001);
#endif
	    gpc_poly = remove_dups( gpc_poly );
	    gpc_poly = reduce_degeneracy( gpc_poly );
	    gpc_poly = remove_dups( gpc_poly );
	    gpc_poly = remove_bad_contours( gpc_poly );

	    cout << "after sanity checks, contours = " 
		 << gpc_poly.contours() << endl;

	    calc_points_inside( gpc_poly );

#if 0
	    // old way
	    Point3D inside_pt;
	    for ( j = 0; j < gpc_poly.contours(); ++j ) {
		inside_pt = calc_point_inside( gpc_poly, j, in_nodes );
		gpc_poly.set_point_inside( j, inside_pt );
	    }
#endif

	    polylist[i].push_back( gpc_poly );

#if 0
	    // temporary ... write out hole/polygon info for debugging
	    for ( j = 0; j < (int)gpc_poly.contours(); ++j ) {
		char pname[256];
		sprintf(pname, "poly%02d-%02d-%02d", i, debug_counter, j);
		cout << "writing to " << pname << endl;
		FILE *fp = fopen( pname, "w" );
		Point3D point;
		for ( int k = 0; k < gpc_poly.contour_size( j ); ++k ) {
		    point = gpc_poly.get_pt( j, k );
		    fprintf( fp, "%.6f %.6f\n", point.x(), point.y() );
		}
		point = gpc_poly.get_pt( j, 0 );
		fprintf( fp, "%.6f %.6f\n", point.x(), point.y() );
		fclose(fp);

		char hname[256];
		sprintf(hname, "hole%02d-%02d-%02d", i, debug_counter, j);
		FILE *fh = fopen( hname, "w" );
		point = gpc_poly.get_point_inside( j );
		fprintf( fh, "%.6f %.6f\n", point.x(), point.y() );
		fclose(fh);
	    }

	    // cout << "type a letter + enter to continue: ";
	    // string input;
	    // cin >> input;
#endif
		
	    ++debug_counter;
	}
    }

    // last, do the rest of the height nodes
    f_current = fit_list.begin();
    f_last = fit_list.end();
    for ( ; f_current != f_last; ++f_current ) {
	index = in_nodes.course_add( *f_current );
    }

    for ( i = 0; i < FG_MAX_AREA_TYPES; ++i ) {
	if ( polylist[i].size() ) {
	    cout << get_area_name((AreaType)i) << " = " 
		 << polylist[i].size() << endl;
	}
    }

    // traverse the polygon lists and build the segment (edge) list
    // that is used by the "Triangle" lib.

    cout << "building segment list" << endl;
    int i1, i2;
    Point3D p1, p2;
    point_list node_list = in_nodes.get_node_list();
    FGPolygon poly;

    for ( i = 0; i < FG_MAX_AREA_TYPES; ++i ) {
	cout << "area type = " << i << endl;
	poly_list_iterator tp_current, tp_last;
	tp_current = polylist[i].begin();
	tp_last = polylist[i].end();

	// process each polygon in list
	for ( ; tp_current != tp_last; ++tp_current ) {
	    poly = *tp_current;
	    cout << "  processing a polygon with contours = " 
		 << poly.contours() << endl;
	    for ( int j = 0; j < (int)poly.contours(); ++j) {
		for ( int k = 0; k < (int)(poly.contour_size(j) - 1); ++k ) {
		    p1 = poly.get_pt( j, k );
		    p2 = poly.get_pt( j, k + 1 );
		    i1 = in_nodes.find( p1 );
		    i2 = in_nodes.find( p2 );
		    // calc_line_params(i1, i2, &m, &b);
		    if ( i == (int)HoleArea ) {
			// mark as a boundary
			in_segs.unique_divide_and_add( node_list, 
						       FGTriSeg(i1, i2, 1) );
		    } else {
			// non boundary
			in_segs.unique_divide_and_add( node_list, 
						       FGTriSeg(i1, i2, 0) );
		    }
		}
		p1 = poly.get_pt( j, 0 );
		p2 = poly.get_pt( j, poly.contour_size(j) - 1 );
		i1 = in_nodes.find( p1 );
		i2 = in_nodes.find( p2 );
		// calc_line_params(i1, i2, &m, &b);
		if ( i == (int)HoleArea ) {
		    // mark as a boundary
		    in_segs.unique_divide_and_add( node_list, 
						   FGTriSeg(i1, i2, 1) );
		} else {
		    // non boundary
		    in_segs.unique_divide_and_add( node_list, 
						   FGTriSeg(i1, i2, 0) );
		}
	    }
	}
    }

    return 0;
}


// populate this class based on the specified gpc_polys list
int FGTriangle::rebuild( FGConstruct& c ) {
    in_nodes.clear();
    in_segs.clear();

    in_nodes = c.get_tri_nodes();
    in_segs = c.get_tri_segs();

    return 0;
}


#if 0
static void write_out_data(struct triangulateio *out) {
    int i, j;

    FILE *node = fopen("tile.node", "w");
    fprintf(node, "%d 2 %d 0\n", 
	    out->numberofpoints, out->numberofpointattributes);
    for ( i = 0; i < out->numberofpoints; ++i) {
	fprintf(node, "%d %.6f %.6f %.2f\n", 
		i, out->pointlist[2*i], out->pointlist[2*i + 1], 0.0);
    }
    fclose(node);

    FILE *ele = fopen("tile.ele", "w");
    fprintf(ele, "%d 3 0\n", out->numberoftriangles);
    for ( i = 0; i < out->numberoftriangles; ++i) {
        fprintf(ele, "%d ", i);
        for ( j = 0; j < out->numberofcorners; ++j) {
	    fprintf(ele, "%d ", out->trianglelist[i * out->numberofcorners + j]);
        }
        for ( j = 0; j < out->numberoftriangleattributes; ++j) {
	    fprintf(ele, "%.6f ", 
		    out->triangleattributelist[i 
					      * out->numberoftriangleattributes
					      + j]
		    );
        }
	fprintf(ele, "\n");
    }
    fclose(ele);

    FILE *fp = fopen("tile.poly", "w");
    fprintf(fp, "0 2 1 0\n");
    fprintf(fp, "%d 1\n", out->numberofsegments);
    for ( i = 0; i < out->numberofsegments; ++i) {
	fprintf(fp, "%d %d %d %d\n", 
		i, out->segmentlist[2*i], out->segmentlist[2*i + 1],
		out->segmentmarkerlist[i] );
    }
    fprintf(fp, "%d\n", out->numberofholes);
    for ( i = 0; i < out->numberofholes; ++i) {
	fprintf(fp, "%d %.6f %.6f\n", 
		i, out->holelist[2*i], out->holelist[2*i + 1]);
    }
    fprintf(fp, "%d\n", out->numberofregions);
    for ( i = 0; i < out->numberofregions; ++i) {
	fprintf(fp, "%d %.6f %.6f %.6f\n", 
		i, out->regionlist[4*i], out->regionlist[4*i + 1],
		out->regionlist[4*i + 2]);
    }
    fclose(fp);
}
#endif


// Front end triangulator for polygon list.  Allocates and builds up
// all the needed structures for the triangulator, runs it, copies the
// results, and frees all the data structures used by the
// triangulator.  "pass" can be 1 or 2.  1 = first pass which
// generates extra nodes for a better triangulation.  2 = second pass
// after split/reassem where we don't want any extra nodes generated.

int FGTriangle::run_triangulate( const string& angle, const int pass ) {
    FGPolygon poly;
    Point3D p;
    struct triangulateio in, out, vorout;
    int counter;
    int i, j;

    // point list
    point_list node_list = in_nodes.get_node_list();
    in.numberofpoints = node_list.size();
    in.pointlist = (REAL *) malloc(in.numberofpoints * 2 * sizeof(REAL));

    for ( i = 0; i < in.numberofpoints; ++i ) {
	in.pointlist[2*i] = node_list[i].x();
	in.pointlist[2*i + 1] = node_list[i].y();
    }

    in.numberofpointattributes = 1;
    in.pointattributelist = (REAL *) malloc(in.numberofpoints *
					    in.numberofpointattributes *
					    sizeof(REAL));
    for ( i = 0; i < in.numberofpoints * in.numberofpointattributes; ++i) {
	in.pointattributelist[i] = node_list[i].z();
    }

    in.pointmarkerlist = (int *) malloc(in.numberofpoints * sizeof(int));
    for ( i = 0; i < in.numberofpoints; ++i) {
	in.pointmarkerlist[i] = 0;
    }

    // triangle list
    in.numberoftriangles = 0;

    // segment list
    triseg_list seg_list = in_segs.get_seg_list();
    in.numberofsegments = seg_list.size();
    in.segmentlist = (int *) malloc(in.numberofsegments * 2 * sizeof(int));
    in.segmentmarkerlist = (int *) malloc(in.numberofsegments * sizeof(int));

    triseg_list_iterator s_current, s_last;
    s_current = seg_list.begin();
    s_last = seg_list.end();
    counter = 0;
    for ( ; s_current != s_last; ++s_current ) {
	in.segmentlist[counter++] = s_current->get_n1();
	in.segmentlist[counter++] = s_current->get_n2();
    }
    s_current = seg_list.begin();
    s_last = seg_list.end();
    counter = 0;
    for ( ; s_current != s_last; ++s_current ) {
	in.segmentmarkerlist[counter++] = s_current->get_boundary_marker();
    }

    // hole list (make holes for airport ignore areas)
    poly_list_iterator h_current, h_last;
    in.numberofholes = 0;
    h_current = polylist[(int)HoleArea].begin();
    h_last = polylist[(int)HoleArea].end();
    for ( ; h_current != h_last; ++h_current ) {
	poly = *h_current;
	for ( j = 0; j < poly.contours(); ++j ) {
            in.numberofholes++;
        }
    }

    in.holelist = (REAL *) malloc(in.numberofholes * 2 * sizeof(REAL));

    h_current = polylist[(int)HoleArea].begin();
    h_last = polylist[(int)HoleArea].end();
    counter = 0;
    for ( ; h_current != h_last; ++h_current ) {
	poly = *h_current;
	for ( j = 0; j < poly.contours(); ++j ) {
	    p = poly.get_point_inside( j );
	    in.holelist[counter++] = p.x();
	    in.holelist[counter++] = p.y();
	}
    }

    // region list
    in.numberofregions = 0;
    for ( i = 0; i < FG_MAX_AREA_TYPES; ++i ) {
	poly_list_iterator h_current, h_last;
	h_current = polylist[i].begin();
	h_last = polylist[i].end();
	for ( ; h_current != h_last; ++h_current ) {
	    poly = *h_current;
	    for ( j = 0; j < poly.contours(); ++j ) {
		if ( ! poly.get_hole_flag( j ) ) {
		    ++in.numberofregions;
		}
	    }
	}
    }

    in.regionlist = (REAL *) malloc(in.numberofregions * 4 * sizeof(REAL));
    counter = 0;
    for ( i = 0; i < FG_MAX_AREA_TYPES; ++i ) {
	poly_list_iterator h_current, h_last;
	h_current = polylist[(int)i].begin();
	h_last = polylist[(int)i].end();
	for ( ; h_current != h_last; ++h_current ) {
	    poly = *h_current;
	    for ( j = 0; j < poly.contours(); ++j ) {
		if ( ! poly.get_hole_flag( j ) ) {
		    p = poly.get_point_inside( j );
		    cout << "Region point = " << p << endl;
		    in.regionlist[counter++] = p.x();  // x coord
		    in.regionlist[counter++] = p.y();  // y coord
		    in.regionlist[counter++] = i;      // region attribute
		    in.regionlist[counter++] = -1.0;   // area constraint
		                                       // (unused)
		}
	    }
	}
    }

    // prep the output structures
    out.pointlist = (REAL *) NULL;        // Not needed if -N switch used.
    // Not needed if -N switch used or number of point attributes is zero:
    out.pointattributelist = (REAL *) NULL;
    out.pointmarkerlist = (int *) NULL;   // Not needed if -N or -B switch used.
    out.trianglelist = (int *) NULL;      // Not needed if -E switch used.
    // Not needed if -E switch used or number of triangle attributes is zero:
    out.triangleattributelist = (REAL *) NULL;
    out.neighborlist = (int *) NULL;      // Needed only if -n switch used.
    // Needed only if segments are output (-p or -c) and -P not used:
    out.segmentlist = (int *) NULL;
    // Needed only if segments are output (-p or -c) and -P and -B not used:
    out.segmentmarkerlist = (int *) NULL;
    out.edgelist = (int *) NULL;          // Needed only if -e switch used.
    out.edgemarkerlist = (int *) NULL;    // Needed if -e used and -B not used.
  
    vorout.pointlist = (REAL *) NULL;     // Needed only if -v switch used.
    // Needed only if -v switch used and number of attributes is not zero:
    vorout.pointattributelist = (REAL *) NULL;
    vorout.edgelist = (int *) NULL;       // Needed only if -v switch used.
    vorout.normlist = (REAL *) NULL;      // Needed only if -v switch used.
    
    // TEMPORARY
    write_tri_data(&in);

    // Triangulate the points.  Switches are chosen to read and write
    // a PSLG (p), preserve the convex hull (c), number everything
    // from zero (z), assign a regional attribute to each element (A),
    // and produce an edge list (e), and a triangle neighbor list (n).

    string tri_options;
    if ( pass == 1 ) {
	// use a quality value of 10 (q10) meaning no interior
	// triangle angles less than 10 degrees
	// tri_options = "pczAen";
	if ( angle == "0" ) {
	    tri_options = "pczAen";
	} else {
	    tri_options = "pczq" + angle + "Aen";
	}
	// // string tri_options = "pzAen";
	// // string tri_options = "pczq15S400Aen";
    } else if ( pass == 2 ) {
	// no new points on boundary (Y), no internal segment
	// splitting (YY), no quality refinement (q)
	tri_options = "pczYYAen";
    } else {
	cout << "unknown pass number = " << pass 
	     << " in FGTriangle::run_triangulate()" << endl;
	exit(-1);
    }
    cout << "Triangulation with options = " << tri_options << endl;

    triangulate( (char *)tri_options.c_str(), &in, &out, &vorout );

    // TEMPORARY
    // write_tri_data(&out);

    // now copy the results back into the corresponding FGTriangle
    // structures

    // nodes
    out_nodes.clear();
    for ( i = 0; i < out.numberofpoints; ++i ) {
	Point3D p( out.pointlist[2*i], out.pointlist[2*i + 1], 
		   out.pointattributelist[i] );
	// cout << "point = " << p << endl;
	out_nodes.simple_add( p );
    }

    // segments
    out_segs.clear();
    for ( i = 0; i < out.numberofsegments; ++i ) {
	out_segs.unique_add( FGTriSeg( out.segmentlist[2*i], 
				       out.segmentlist[2*i+1],
				       out.segmentmarkerlist[i] ) );
    }

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

	elelist.push_back( FGTriEle( n1, n2, n3, attribute ) );
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

    return 0;
}

