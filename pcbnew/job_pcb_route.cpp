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

#include <jobs/job_pcb_route.h>
#include <cli/exit_codes.h>

#include <base_units.h>
#include <board.h>
#include <board_loader.h>
#include <i18n_utility.h>
#include <layer_ids.h>
#include <netinfo.h>
#include <pcb_track.h>
#include <reporter.h>

#include <utility>

#include <wx/crt.h>
#include <wx/filename.h>
#include <wx/tokenzr.h>

#include "pcbnew_jobs_handler.h"
#include <router/pns_headless_api.h>


int PCBNEW_JOBS_HANDLER::JobRoute( JOB* aJob )
{
    JOB_PCB_ROUTE* routeJob = dynamic_cast<JOB_PCB_ROUTE*>( aJob );

    if( routeJob == nullptr )
        return CLI::EXIT_CODES::ERR_UNKNOWN;

    BOARD* brd = getBoard( routeJob->m_filename );

    if( !brd )
        return CLI::EXIT_CODES::ERR_INVALID_INPUT_FILE;

    // Resolve the copper layer (default: front copper).
    PCB_LAYER_ID layer = F_Cu;

    if( !routeJob->m_layer.IsEmpty() )
    {
        layer = brd->GetLayerID( routeJob->m_layer );

        if( layer == UNDEFINED_LAYER || !IsCopperLayer( layer ) )
        {
            m_reporter->Report(
                    wxString::Format( _( "Unknown or non-copper layer '%s'\n" ),
                                      routeJob->m_layer ),
                    RPT_SEVERITY_ERROR );
            return CLI::EXIT_CODES::ERR_ARGS;
        }
    }

    // Resolve the net (empty: let the bridge infer it from the start point).
    NETINFO_ITEM* net = nullptr;

    if( !routeJob->m_net.IsEmpty() )
    {
        net = brd->FindNet( routeJob->m_net );

        if( !net )
        {
            m_reporter->Report(
                    wxString::Format( _( "Net '%s' not found on board\n" ),
                                      routeJob->m_net ),
                    RPT_SEVERITY_ERROR );
            return CLI::EXIT_CODES::ERR_ARGS;
        }
    }

    // --chain form: "x1,y1,LAYER1;x2,y2,LAYER2;..." routed as consecutive
    // legs by PNS_BRIDGE::RouteChain(); a layer change between consecutive
    // points drops a via at that point.  Mutually exclusive with the
    // single-point endpoints (enforced in the CLI; a raw job carrying both
    // routes the chain and ignores the endpoints).
    std::vector<std::pair<VECTOR2I, PCB_LAYER_ID>> chain;

    if( !routeJob->m_chain.IsEmpty() )
    {
        wxStringTokenizer chainTokenizer( routeJob->m_chain, ";" );

        while( chainTokenizer.HasMoreTokens() )
        {
            wxString pointStr = chainTokenizer.GetNextToken();
            pointStr.Trim( true ).Trim( false );

            if( pointStr.IsEmpty() )
                continue;   // tolerate a trailing ';'

            wxStringTokenizer     coordTokenizer( pointStr, "," );
            std::vector<wxString> parts;

            while( coordTokenizer.HasMoreTokens() )
            {
                wxString part = coordTokenizer.GetNextToken();
                part.Trim( true ).Trim( false );
                parts.push_back( part );
            }

            if( parts.size() != 3 )
            {
                m_reporter->Report(
                        wxString::Format( _( "Malformed chain point '%s': expected x,y,LAYER\n" ),
                                          pointStr ),
                        RPT_SEVERITY_ERROR );
                return CLI::EXIT_CODES::ERR_ARGS;
            }

            double x = 0.0;
            double y = 0.0;

            if( !parts[0].ToDouble( &x ) || !parts[1].ToDouble( &y ) )
            {
                m_reporter->Report(
                        wxString::Format( _( "Invalid chain coordinate in '%s'\n" ), pointStr ),
                        RPT_SEVERITY_ERROR );
                return CLI::EXIT_CODES::ERR_ARGS;
            }

            PCB_LAYER_ID pointLayer = brd->GetLayerID( parts[2] );

            if( pointLayer == UNDEFINED_LAYER || !IsCopperLayer( pointLayer ) )
            {
                m_reporter->Report(
                        wxString::Format( _( "Unknown or non-copper layer '%s'\n" ), parts[2] ),
                        RPT_SEVERITY_ERROR );
                return CLI::EXIT_CODES::ERR_ARGS;
            }

            chain.emplace_back( VECTOR2I( pcbIUScale.mmToIU( x ), pcbIUScale.mmToIU( y ) ),
                                pointLayer );
        }

        if( chain.size() < 2 )
        {
            m_reporter->Report( _( "Chain needs at least two points\n" ), RPT_SEVERITY_ERROR );
            return CLI::EXIT_CODES::ERR_ARGS;
        }
    }

    PNS_BRIDGE& bridge = PNS_BRIDGE::Instance();

    if( !bridge.Attach( brd ) )
    {
        m_reporter->Report( bridge.LastError() + wxS( "\n" ), RPT_SEVERITY_ERROR );
        return CLI::EXIT_CODES::ERR_UNKNOWN;
    }

    std::vector<BOARD_CONNECTED_ITEM*> created;
    bool ok = false;

    if( chain.empty() )
    {
        VECTOR2I              from( pcbIUScale.mmToIU( routeJob->m_fromX ),
                                    pcbIUScale.mmToIU( routeJob->m_fromY ) );
        VECTOR2I              to( pcbIUScale.mmToIU( routeJob->m_toX ),
                                  pcbIUScale.mmToIU( routeJob->m_toY ) );
        std::vector<VECTOR2I> waypoints;

        for( const VECTOR2D& wp : routeJob->m_waypoints )
            waypoints.emplace_back( pcbIUScale.mmToIU( wp.x ), pcbIUScale.mmToIU( wp.y ) );

        ok = bridge.Route( net, from, to, layer, waypoints, routeJob->m_allowVia, &created );
    }
    else
    {
        ok = bridge.RouteChain( net, chain, &created );
    }

    if( !ok )
    {
        m_reporter->Report( _( "Routing failed: " ) + bridge.LastError() + wxS( "\n" ),
                            RPT_SEVERITY_ERROR );
        bridge.Detach();
        return CLI::EXIT_CODES::ERR_UNKNOWN;
    }

    bridge.Detach();

    int tracks = 0;
    int vias = 0;

    for( BOARD_CONNECTED_ITEM* item : created )
    {
        if( dynamic_cast<PCB_VIA*>( item ) )
            ++vias;
        else
            ++tracks;
    }

    wxString boardFileName = brd->GetFileName();

    // Single-point: the one routing layer; chain: first..last chain layer.
    wxString layerDesc = chain.empty()
                            ? brd->GetLayerName( layer )
                            : brd->GetLayerName( chain.front().second ) + wxS( ".." )
                                      + brd->GetLayerName( chain.back().second );

    if( !BOARD_LOADER::SaveBoard( boardFileName, *brd ) )
    {
        m_reporter->Report(
                wxString::Format( _( "Routing succeeded but saving '%s' failed\n" ),
                                  boardFileName ),
                RPT_SEVERITY_ERROR );
        return CLI::EXIT_CODES::ERR_UNKNOWN;
    }

    m_reporter->Report( wxString::Format(
            _( "Routed net '%s' on %s: %d track segment(s), %d via(s); board saved to '%s'\n" ),
            routeJob->m_net.IsEmpty() ? wxString( wxS( "(auto)" ) ) : routeJob->m_net,
            layerDesc,
            tracks,
            vias,
            boardFileName ),
            RPT_SEVERITY_INFO );

    return CLI::EXIT_CODES::SUCCESS;
}
