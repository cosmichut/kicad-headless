/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "pcb_route_command.h"
#include <cli/exit_codes.h>
#include <jobs/job_pcb_route.h>
#include <string_utils.h>
#include <wx/crt.h>
#include <wx/tokenzr.h>

#define ARG_NET "--net"
#define ARG_FROM_X "--from-x"
#define ARG_FROM_Y "--from-y"
#define ARG_TO_X "--to-x"
#define ARG_TO_Y "--to-y"
#define ARG_LAYER "--layer"
#define ARG_WAYPOINTS "--waypoints"
#define ARG_ALLOW_VIA "--allow-via"
#define ARG_CHAIN "--chain"

// Coordinates arrive as strings so that negative values wrapped in single
// quotes by kicad_cli.cpp's preprocessArgs survive the scan-free parse.
static bool parseCoordArg( const std::string& aValue, double& aCoord )
{
    wxString value = From_UTF8( aValue.c_str() );
    value.Trim( true ).Trim( false );
    value.Replace( wxT( "'" ), wxT( "" ) );

    return value.ToDouble( &aCoord );
}

CLI::PCB_ROUTE_COMMAND::PCB_ROUTE_COMMAND() : COMMAND( "route" )
{
    addCommonArgs( true, false, IO_TYPE::FILE, IO_TYPE::FILE );

    m_argParser.add_description(
            UTF8STDSTR( _( "Route a push-and-shove track between two points and save the board.\n"
                           "All coordinates are in millimetres (board Y axis grows downwards)" ) ) );

    m_argParser.add_argument( ARG_NET )
            .default_value( std::string( "" ) )
            .help( UTF8STDSTR( _( "Net to route; if omitted, it is inferred from the copper "
                                  "item nearest the start point" ) ) )
            .metavar( "NET_NAME" );

    m_argParser.add_argument( ARG_FROM_X )
            .default_value( std::string( "0" ) )
            .help( UTF8STDSTR( _( "Start point X in mm (use --from-x=-1.5 for negative "
                                  "values); required unless --chain is given" ) ) )
            .metavar( "MM" );

    m_argParser.add_argument( ARG_FROM_Y )
            .default_value( std::string( "0" ) )
            .help( UTF8STDSTR( _( "Start point Y in mm (use --from-y=-1.5 for negative "
                                  "values); required unless --chain is given" ) ) )
            .metavar( "MM" );

    m_argParser.add_argument( ARG_TO_X )
            .default_value( std::string( "0" ) )
            .help( UTF8STDSTR( _( "End point X in mm (use --to-x=-1.5 for negative values); "
                                  "required unless --chain is given" ) ) )
            .metavar( "MM" );

    m_argParser.add_argument( ARG_TO_Y )
            .default_value( std::string( "0" ) )
            .help( UTF8STDSTR( _( "End point Y in mm (use --to-y=-1.5 for negative values); "
                                  "required unless --chain is given" ) ) )
            .metavar( "MM" );

    m_argParser.add_argument( ARG_LAYER )
            .default_value( std::string( "F.Cu" ) )
            .help( UTF8STDSTR( _( "Copper layer to route on, e.g. F.Cu, B.Cu, In1.Cu" ) ) )
            .metavar( "LAYER" );

    m_argParser.add_argument( ARG_WAYPOINTS )
            .default_value( std::string( "" ) )
            .help( UTF8STDSTR( _( "Comma-separated intermediate points, x1,y1,x2,y2,..." ) ) )
            .metavar( "COORDS" );

    m_argParser.add_argument( ARG_ALLOW_VIA )
            .help( UTF8STDSTR( _( "Allow the router to insert vias (reserved; the route stays "
                                  "on a single layer in this version)" ) ) )
            .flag();

    m_argParser.add_argument( ARG_CHAIN )
            .default_value( std::string( "" ) )
            .help( UTF8STDSTR( _( "Multi-leg chain: x,y,LAYER;x,y,LAYER;... (layer changes "
                                  "drop vias)" ) ) )
            .metavar( "CHAIN_PARAMS" );
}


int CLI::PCB_ROUTE_COMMAND::doPerform( KIWAY& aKiway )
{
    std::unique_ptr<JOB_PCB_ROUTE> routeJob = std::make_unique<JOB_PCB_ROUTE>();

    routeJob->m_filename = m_argInput;
    routeJob->m_net = From_UTF8( m_argParser.get<std::string>( ARG_NET ).c_str() );
    routeJob->m_layer = From_UTF8( m_argParser.get<std::string>( ARG_LAYER ).c_str() );

    if( !parseCoordArg( m_argParser.get<std::string>( ARG_FROM_X ), routeJob->m_fromX )
            || !parseCoordArg( m_argParser.get<std::string>( ARG_FROM_Y ), routeJob->m_fromY )
            || !parseCoordArg( m_argParser.get<std::string>( ARG_TO_X ), routeJob->m_toX )
            || !parseCoordArg( m_argParser.get<std::string>( ARG_TO_Y ), routeJob->m_toY ) )
    {
        wxFprintf( stderr, _( "Invalid coordinate value(s); expected numbers in mm\n" ) );
        return EXIT_CODES::ERR_ARGS;
    }

    routeJob->m_allowVia = m_argParser.get<bool>( ARG_ALLOW_VIA );

    const bool chainGiven = m_argParser.is_used( ARG_CHAIN );
    const bool endpointsGiven = m_argParser.is_used( ARG_FROM_X ) || m_argParser.is_used( ARG_FROM_Y )
                                || m_argParser.is_used( ARG_TO_X ) || m_argParser.is_used( ARG_TO_Y );

    if( chainGiven && endpointsGiven )
    {
        wxFprintf( stderr, _( "--chain and --from-x/--from-y/--to-x/--to-y are mutually "
                              "exclusive\n" ) );
        return EXIT_CODES::ERR_ARGS;
    }

    if( !chainGiven && !endpointsGiven )
    {
        wxFprintf( stderr, _( "Either --chain or --from-x/--from-y/--to-x/--to-y is required\n" ) );
        return EXIT_CODES::ERR_ARGS;
    }

    if( chainGiven )
    {
        routeJob->m_chain = From_UTF8( m_argParser.get<std::string>( ARG_CHAIN ).c_str() );

        // Strip the single quotes preprocessArgs (kicad_cli.cpp) wraps negative
        // coordinate values in.
        routeJob->m_chain.Replace( wxT( "'" ), wxT( "" ) );
    }

    wxString waypoints = From_UTF8( m_argParser.get<std::string>( ARG_WAYPOINTS ).c_str() );

    // Strip the single quotes preprocessArgs (kicad_cli.cpp) wraps negative
    // coordinate values in.
    waypoints.Replace( wxT( "'" ), wxT( "" ) );

    if( !waypoints.IsEmpty() )
    {
        wxStringTokenizer    tokenizer( waypoints, "," );
        std::vector<double>  coords;

        while( tokenizer.HasMoreTokens() )
        {
            wxString token = tokenizer.GetNextToken();
            token.Trim( true ).Trim( false );

            double coord = 0.0;

            if( !token.ToDouble( &coord ) )
            {
                wxFprintf( stderr, _( "Invalid waypoint coordinate '%s'\n" ), token );
                return EXIT_CODES::ERR_ARGS;
            }

            coords.push_back( coord );
        }

        if( coords.size() % 2 != 0 )
        {
            wxFprintf( stderr, _( "Waypoints must come in x,y pairs\n" ) );
            return EXIT_CODES::ERR_ARGS;
        }

        for( size_t ii = 0; ii + 1 < coords.size(); ii += 2 )
            routeJob->m_waypoints.emplace_back( coords[ii], coords[ii + 1] );
    }

    if( routeJob->m_filename.IsEmpty() )
    {
        wxFprintf( stderr, _( "Input board file is required\n" ) );
        return EXIT_CODES::ERR_ARGS;
    }

    if( !wxFile::Exists( routeJob->m_filename ) )
    {
        wxFprintf( stderr, _( "Board file does not exist or is not accessible\n" ) );
        return EXIT_CODES::ERR_INVALID_INPUT_FILE;
    }

    return aKiway.ProcessJob( KIWAY::FACE_PCB, routeJob.get() );
}
