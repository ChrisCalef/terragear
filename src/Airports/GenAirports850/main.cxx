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
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.


#include <string.h>
#include <iostream>

#include <simgear/debug/logstream.hxx>
#include <simgear/misc/sg_path.hxx>
#include <simgear/misc/sgstream.hxx>

#include <Polygon/index.hxx>
#include <Geometry/util.hxx>
#include <Geometry/poly_support.hxx>

#include "beznode.hxx"
#include "closedpoly.hxx"
#include "linearfeature.hxx"
#include "parser.hxx"

using namespace std;


// TODO : Modularize this function
// IDEAS 
// 1) Start / Stop MarkStyle
// 2) Start / Stop LightStyle
// 3) holes
// 4) CreatePavementSS(pavement type)
// 5) CreateMarkingSS(marking type)
// 6) CalcStripeOffsets should be AddMarkingVerticies, and take the 2 distances from pavement edge...
// SCRAP ALL IDEAS - Markings are encapsulated in LinearFeature class. 
// Start creates a new object
// End closes the object (and should add it to a list - either in the parser, or ClosedPoly)

// Display usage
static void usage( int argc, char **argv ) {
    SG_LOG(SG_GENERAL, SG_ALERT, 
	   "Usage " << argv[0] << " --input=<apt_file> "
	   << "--work=<work_dir> [ --start-id=abcd ] [ --restart-id=abcd ] [ --nudge=n ] "
	   << "[--min-lon=<deg>] [--max-lon=<deg>] [--min-lat=<deg>] [--max-lat=<deg>] "
	   << "[--clear-dem-path] [--dem-path=<path>] [--max-slope=<decimal>] "
       << "[ --airport=abcd ]  [--tile=<tile>] [--chunk=<chunk>] [--verbose] [--help]");
}


void setup_default_elevation_sources(string_list& elev_src) {
    elev_src.push_back( "SRTM2-Africa-3" );
    elev_src.push_back( "SRTM2-Australia-3" );
    elev_src.push_back( "SRTM2-Eurasia-3" );
    elev_src.push_back( "SRTM2-Islands-3" );
    elev_src.push_back( "SRTM2-North_America-3" );
    elev_src.push_back( "SRTM2-South_America-3" );
    elev_src.push_back( "DEM-USGS-3" );
    elev_src.push_back( "SRTM-1" );
    elev_src.push_back( "SRTM-3" );
    elev_src.push_back( "SRTM-30" );
}

// Display help and usage
static void help( int argc, char **argv, const string_list& elev_src ) {
    cout << "genapts generates airports for use in generating scenery for the FlightGear flight simulator.  \n";
    cout << "Airport, runway, and taxiway vector data and attributes are input, and generated 3D airports \n";
    cout << "are output for further processing by the TerraGear scenery creation tools.  \n";
    cout << "\n\n";
    cout << "The standard input file is apt.dat.gz which is found in $FG_ROOT/Airports.  \n";
    cout << "This file is periodically generated by Robin Peel, who maintains  \n";
    cout << "the airport database for both the X-Plane and FlightGear simulators.  \n";
    cout << "The format of this file is documented at  \n";
    cout << "http://data.x-plane.com/designers.html#Formats   \n";
    cout << "Any other input file corresponding to this format may be used as input to genapts.  \n";
    cout << "Input files may be gzipped or left as plain text as required.  \n";
    cout << "\n\n";
    cout << "Processing all the world's airports takes a *long* time.  To cut down processing time \n";
    cout << "when only some airports are required, you may refine the input selection either by airport \n";
    cout << "or by area.  By airport, either one airport can be specified using --airport=abcd, where abcd is \n";
    cout << "a valid airport code eg. --airport-id=KORD, or a starting airport can be specified using --start-id=abcd \n";
    cout << "where once again abcd is a valid airport code.  In this case, all airports in the file subsequent to the \n";
    cout << "start-id are done.  This is convienient when re-starting after a previous error.  \n";
    cout << "If you want to restart with the airport after a problam icao, use --restart-id=abcd, as this works the same as\n";
    cout << " with the exception that the airport abcd is skipped \n";
    cout << "\nAn input area may be specified by lat and lon extent using min and max lat and lon.  \n";
    cout << "Alternatively, you may specify a chunk (10 x 10 degrees) or tile (1 x 1 degree) using a string \n";
    cout << "such as eg. w080n40, e000s27.  \n";
    cout << "\nAn input file containing only a subset of the world's \n";
    cout << "airports may of course be used.\n";
    cout << "\n\n";
    cout << "It is necessary to generate the elevation data for the area of interest PRIOR TO GENERATING THE AIRPORTS.  \n";
    cout << "Failure to do this will result in airports being generated with an elevation of zero.  \n";
    cout << "The following subdirectories of the work-dir will be searched for elevation files:\n\n";
    
    string_list::const_iterator elev_src_it;
    for (elev_src_it = elev_src.begin(); elev_src_it != elev_src.end(); elev_src_it++) {
    	    cout << *elev_src_it << "\n";
    }
    cout << "\n";
    usage( argc, argv );
}

// TODO: where do these belong
int nudge = 10;
double slope_max = 0.2;
double gSnap = 0.00000001;      // approx 1 mm

int main(int argc, char **argv)
{
    float min_lon = -180;
    float max_lon = 180;
    float min_lat = -90;
    float max_lat = 90;
	long  position = 0;

    string_list elev_src;
    elev_src.clear();
    setup_default_elevation_sources(elev_src);

    // Set Normal logging
    sglog().setLogLevels( SG_GENERAL, SG_INFO );

    SG_LOG(SG_GENERAL, SG_INFO, "Run genapt");

    // parse arguments
    string work_dir = "";
    string input_file = "";
    string start_id = "";
    string restart_id = "";
    string airport_id = "";
    string last_apt_file = "./last_apt.txt";
    int    dump_rwy_poly  = -1;
    int    dump_taxi_poly = -1;
    int    dump_pvmt_poly = -1;
    int    dump_feat_poly = -1;
    int    dump_base_poly = -1;

    int arg_pos;

    for (arg_pos = 1; arg_pos < argc; arg_pos++) 
    {
        string arg = argv[arg_pos];
        if ( arg.find("--work=") == 0 ) 
        {
            work_dir = arg.substr(7);
    	} 
        else if ( arg.find("--input=") == 0 ) 
        {
	        input_file = arg.substr(8);
        } 
        else if ( arg.find("--terrain=") == 0 ) 
        {
            elev_src.push_back( arg.substr(10) );
     	} 
        else if ( arg.find("--start-id=") == 0 ) 
        {
    	    start_id = arg.substr(11);
     	} 
        else if ( arg.find("--restart-id=") == 0 ) 
        {
    	    restart_id = arg.substr(13);
     	} 
        else if ( arg.find("--nudge=") == 0 ) 
        {
    	    nudge = atoi( arg.substr(8).c_str() );
    	}
        else if ( arg.find("--snap=") == 0 ) 
        {
    	    gSnap = atof( arg.substr(7).c_str() );
    	} 
        else if ( arg.find("--last_apt_file=") == 0 ) 
        {
    	    last_apt_file = arg.substr(16);
     	} 
        else if ( arg.find("--min-lon=") == 0 ) 
        {
    	    min_lon = atof( arg.substr(10).c_str() );
    	} 
        else if ( arg.find("--max-lon=") == 0 ) 
        {
    	    max_lon = atof( arg.substr(10).c_str() );
    	} 
        else if ( arg.find("--min-lat=") == 0 ) 
        {
    	    min_lat = atof( arg.substr(10).c_str() );
    	} 
        else if ( arg.find("--max-lat=") == 0 ) 
        {
    	    max_lat = atof( arg.substr(10).c_str() );
        } 
        else if ( arg.find("--chunk=") == 0 ) 
        {
            tg::Rectangle rectangle = tg::parseChunk(arg.substr(8).c_str(), 10.0);
            min_lon = rectangle.getMin().x();
            min_lat = rectangle.getMin().y();
            max_lon = rectangle.getMax().x();
            max_lat = rectangle.getMax().y();
        } 
        else if ( arg.find("--tile=") == 0 ) 
        {
            tg::Rectangle rectangle = tg::parseTile(arg.substr(7).c_str());
            min_lon = rectangle.getMin().x();
            min_lat = rectangle.getMin().y();
            max_lon = rectangle.getMax().x();
            max_lat = rectangle.getMax().y();
    	} 
        else if ( arg.find("--airport=") == 0 ) 
        {
    	    airport_id = arg.substr(10).c_str();
    	} 
        else if ( arg == "--clear-dem-path" ) 
        {
    	    elev_src.clear();
    	} 
        else if ( arg.find("--dem-path=") == 0 ) 
        {
    	    elev_src.push_back( arg.substr(11) );
    	} 
        else if ( (arg.find("--verbose") == 0) || (arg.find("-v") == 0) ) 
        {
    	    sglog().setLogLevels( SG_GENERAL, SG_BULK );
    	} 
        else if ( (arg.find("--max-slope=") == 0) ) 
        {
    	    slope_max = atof( arg.substr(12).c_str() );
    	} 
        else if ( arg.find("--dump-rwy=") == 0 ) 
        {
    	    dump_rwy_poly = atoi( arg.substr(11).c_str() );
    	}
        else if ( arg.find("--dump-taxi=") == 0 ) 
        {
    	    dump_taxi_poly = atoi( arg.substr(12).c_str() );
    	} 
        else if ( arg.find("--dump-pvmt=") == 0 ) 
        {
    	    dump_pvmt_poly = atoi( arg.substr(12).c_str() );
    	} 
        else if ( arg.find("--dump-feat=") == 0 ) 
        {
    	    dump_feat_poly = atoi( arg.substr(12).c_str() );
    	} 
        else if ( arg.find("--dump-base=") == 0 ) 
        {
    	    dump_base_poly = atoi( arg.substr(12).c_str() );
    	} 
        else if ( (arg.find("--help") == 0) || (arg.find("-h") == 0) ) 
        {
    	    help( argc, argv, elev_src );
    	    exit(-1);
    	} 
        else 
        {
    	    usage( argc, argv );
    	    exit(-1);
    	}
    }

    SG_LOG(SG_GENERAL, SG_INFO, "Input file = " << input_file);
    SG_LOG(SG_GENERAL, SG_INFO, "Terrain sources = ");
    for ( unsigned int i = 0; i < elev_src.size(); ++i ) 
    {
        SG_LOG(SG_GENERAL, SG_INFO, "  " << work_dir << "/" << elev_src[i] );
    }
    SG_LOG(SG_GENERAL, SG_INFO, "Work directory = " << work_dir);
    SG_LOG(SG_GENERAL, SG_INFO, "Nudge = " << nudge);
    SG_LOG(SG_GENERAL, SG_INFO, "Longitude = " << min_lon << ':' << max_lon);
    SG_LOG(SG_GENERAL, SG_INFO, "Latitude = " << min_lat << ':' << max_lat);

    if (max_lon < min_lon || max_lat < min_lat ||
	    min_lat < -90 || max_lat > 90 ||
	    min_lon < -180 || max_lon > 180) 
    {
        SG_LOG(SG_GENERAL, SG_ALERT, "Bad longitude or latitude");
    	exit(1);
    }

    if ( work_dir == "" ) 
    {
    	SG_LOG( SG_GENERAL, SG_ALERT, "Error: no work directory specified." );
    	usage( argc, argv );
	    exit(-1);
    }

    if ( input_file == "" ) 
    {
    	SG_LOG( SG_GENERAL, SG_ALERT,  "Error: no input file." );
    	exit(-1);
    }

    // make work directory
    SG_LOG(SG_GENERAL, SG_INFO, "Creating AirportArea directory");

    string airportareadir=work_dir+"/AirportArea";
    SGPath sgp( airportareadir );
    sgp.append( "dummy" );
    sgp.create_dir( 0755 );
    
    string lastaptfile = work_dir+"/last_apt";

    // initialize persistant polygon counter
    string counter_file = airportareadir+"/poly_counter";
    poly_index_init( counter_file );

    // Initialize shapefile support (for debugging)
    tgShapefileInit();

    sg_gzifstream in( input_file );
    if ( !in.is_open() ) 
    {
        SG_LOG( SG_GENERAL, SG_ALERT, "Cannot open file: " << input_file );
        exit(-1);
    }

    // Create the parser...
    Parser* parser = new Parser(input_file, work_dir, elev_src);

    // Add any debug 
    parser->SetDebugPolys( dump_rwy_poly, dump_taxi_poly, dump_pvmt_poly, dump_feat_poly, dump_base_poly );

    // just one airport 
    if ( airport_id != "" )
    {
        // just find and add the one airport
        parser->AddAirport( airport_id );

        SG_LOG(SG_GENERAL, SG_INFO, "Finished Adding airport - now parse");
        
        // and start the parser
        parser->Parse( last_apt_file );
    }
    else if ( start_id != "" )
    {
        SG_LOG(SG_GENERAL, SG_INFO, "move forward to " << start_id );

        // scroll forward in datafile
        position = parser->FindAirport( start_id );

        // add remaining airports within boundary
        parser->AddAirports( position, min_lat, min_lon, max_lat, max_lon );

        // parse all the airports that were found
        parser->Parse( last_apt_file );
    }
    else if ( restart_id != "" )
    {
        SG_LOG(SG_GENERAL, SG_INFO, "move forward airport after " << restart_id );

        // scroll forward in datafile
        position = parser->FindAirport( restart_id );

        // add all remaining airports within boundary
        parser->AddAirports( position, min_lat, min_lon, max_lat, max_lon );

        // but remove the restart id - it's broken
        parser->RemoveAirport( restart_id );

        // parse all the airports that were found
        parser->Parse( last_apt_file );
    }
    else
    {
        // find all airports within given boundary
        parser->AddAirports( 0, min_lat, min_lon, max_lat, max_lon );

        // and parser them
        parser->Parse( last_apt_file );
    }

    delete parser;

    SG_LOG(SG_GENERAL, SG_INFO, "Done");
    exit(0);

    return 0;
}

