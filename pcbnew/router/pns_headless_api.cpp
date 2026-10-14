/*
 * Headless Push-and-Shove routing bridge for external (scripting) access.
 *
 * Installed in pcbnew/router/ (next to pns_router.cpp / pns_kicad_iface.cpp);
 * built into the pnsrouter static library via PCBNEW_PNS_SRCS.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * License: GPLv3+ (matches the surrounding PNS sources)
 */

#include "pns_headless_api.h"

#include <limits>

#include <board.h>
#include <board_design_settings.h>
#include <connectivity/connectivity_data.h>
#include <footprint.h>
#include <netinfo.h>
#include <pad.h>
#include <padstack.h>
#include <pcb_track.h>

#include <base_units.h>
#include <geometry/geometry_utils.h>

#include "pns_item.h"
#include "pns_itemset.h"
#include "pns_kicad_iface.h"
#include "pns_routing_settings.h"
#include "pns_line.h"
#include "pns_node.h"
#include "pns_placement_algo.h"
#include "pns_router.h"
#include "pns_segment.h"
#include "pns_solid.h"
#include "pns_sizes_settings.h"
#include "pns_via.h"

using namespace std::placeholders;

// Pad/track coords are clamped away from the integer extremes the way
// PNS::TOOL_BASE does (pns_tool_base.cpp:42, COORDS_PADDING).
static const unsigned int COORDS_PADDING = pcbIUScale.mmToIU( 20 );

static VECTOR2I clampCoord( const VECTOR2I& aP )
{
    return GetClampedCoords( aP, COORDS_PADDING );
}


PNS_BRIDGE& PNS_BRIDGE::Instance()
{
    static PNS_BRIDGE theBridge;
    return theBridge;
}


PNS_BRIDGE::PNS_BRIDGE()
{
}


PNS_BRIDGE::~PNS_BRIDGE()
{
    Detach();
}


bool PNS_BRIDGE::Attach( BOARD* aBoard )
{
    if( !aBoard )
    {
        m_lastError = wxT( "Attach: null board" );
        return false;
    }

    if( m_board == aBoard && m_router && m_iface )
        return true;    // already attached to this board

    Detach();

    m_board = aBoard;

    // Force shove mode: the whole point of this API is push-and-shove.
    m_settings = std::make_unique<ROUTING_SETTINGS>( nullptr, "tools.pns_headless" );
    m_settings->SetMode( PNS_MODE::RM_Shove );
    m_settings->SetAllowDRCViolations( m_allowDrcViolations );

    // Constructing a PNS::ROUTER also publishes it as the process-wide
    // singleton (pns_router.cpp ROUTER::ROUTER() sets 'theRouter'), which PNS
    // internals use via ROUTER::GetInstance(); fine as long as there is no
    // concurrent GUI router in this process.
    m_iface = new PNS_HEADLESS_IFACE();
    m_iface->SetBoard( aBoard );

    m_router = new PNS::ROUTER();
    m_router->SetInterface( m_iface );
    m_router->SetMode( PNS::PNS_MODE_ROUTE_SINGLE );
    m_router->LoadSettings( m_settings.get() );
    m_router->ClearWorld();
    m_router->SyncWorld();      // creates PNS_PCBNEW_RULE_RESOLVER (pns_kicad_iface.cpp:2500)

    m_lastError = wxEmptyString;
    return true;
}


void PNS_BRIDGE::Detach()
{
    if( m_router )
    {
        if( m_router->RoutingInProgress() )
            m_router->StopRouting();

        // Delete the router first: the NODE dtor needs the rule resolver that
        // the iface owns (see the comment in pns_tool_base.cpp:~TOOL_BASE).
        delete m_router;
        m_router = nullptr;
    }

    delete m_iface;
    m_iface = nullptr;

    m_settings.reset();
    m_board = nullptr;
}


void PNS_BRIDGE::ResyncWorld()
{
    if( !m_router )
        return;

    if( m_router->RoutingInProgress() )
        m_router->StopRouting();

    m_router->SyncWorld();
}


void PNS_BRIDGE::SetAllowDrcViolations( bool aAllow )
{
    m_allowDrcViolations = aAllow;

    if( m_settings )
        m_settings->SetAllowDRCViolations( aAllow );
}


void PNS_BRIDGE::SetAllowPartialRoute( bool aAllow )
{
    m_allowPartial = aAllow;
}


void PNS_BRIDGE::SetTrackWidth( int aWidthIU )
{
    m_trackWidth = aWidthIU;
}


void PNS_BRIDGE::SetViaDiameter( int aDiameterIU )
{
    m_viaDiameter = aDiameterIU;
}


void PNS_BRIDGE::SetViaDrill( int aDrillIU )
{
    m_viaDrill = aDrillIU;
}


void PNS_BRIDGE::SetItemSearchRadius( int aRadiusIU )
{
    m_searchRadius = aRadiusIU;
}


bool PNS_BRIDGE::Route( NETINFO_ITEM* aNet,
                        const VECTOR2I& aFrom, const VECTOR2I& aTo,
                        PCB_LAYER_ID aLayer,
                        const std::vector<VECTOR2I>& aWaypoints,
                        bool aAllowVia,
                        std::vector<BOARD_CONNECTED_ITEM*>* aOutItems )
{
    m_lastError = wxEmptyString;

    if( !m_router || !m_iface )
    {
        m_lastError = wxT( "Route: not attached to a board" );
        return false;
    }

    if( !::IsCopperLayer( aLayer ) )
    {
        m_lastError = wxT( "Route: non-copper layer" );
        return false;
    }

    if( m_iface->GetPNSLayerFromBoardLayer( aLayer ) < 0 )
    {
        m_lastError = wxT( "Route: layer outside the board's copper stack" );
        return false;
    }

    // An unspecified net is inferred from the netted copper item nearest the
    // start point (the same lookup routeLeg() uses to seed the placer).
    if( !aNet )
    {
        const int  pnsLayer = m_iface->GetPNSLayerFromBoardLayer( aLayer );
        const VECTOR2I fromClamped = clampCoord( aFrom );
        PNS::ITEM* seed = findItemNear( nullptr, fromClamped, pnsLayer, m_searchRadius );

        if( !seed )
            seed = findItemNear( nullptr, fromClamped, pnsLayer, m_searchRadius * 3 );

        if( !seed || !seed->Net() )
        {
            m_lastError = wxT( "Route: no net given and no netted copper item found "
                               "near the start point" );
            return false;
        }

        aNet = static_cast<NETINFO_ITEM*>( seed->Net() );
    }

    if( m_router->RoutingInProgress() )
        m_router->StopRouting();

    // aAllowVia is reserved: in this single-layer entry point the PNS engine
    // never drops vias on its own (shoving stays on the routing layer).  Vias
    // come into play only at layer changes, see routeLeg()'s aViaAtEnd.
    wxUnusedVar( aAllowVia );

    std::vector<VECTOR2I> pts;
    pts.reserve( aWaypoints.size() + 2 );
    pts.push_back( clampCoord( aFrom ) );

    for( const VECTOR2I& wp : aWaypoints )
        pts.push_back( clampCoord( wp ) );

    pts.push_back( clampCoord( aTo ) );

    std::vector<BOARD_CONNECTED_ITEM*> created;
    m_iface->SetCaptureList( &created );

    bool ok = true;

    for( size_t i = 0; i + 1 < pts.size(); ++i )
    {
        if( pts[i] == pts[i + 1] )
            continue;   // zero-length leg

        if( !routeLeg( aNet, pts[i], pts[i + 1], aLayer, false ) )
        {
            ok = false;
            break;
        }
    }

    m_iface->SetCaptureList( nullptr );

    if( ok && aOutItems )
        *aOutItems = created;

    return ok;
}


bool PNS_BRIDGE::RouteNet( const wxString& aNetName,
                           const VECTOR2I& aFrom, const VECTOR2I& aTo,
                           PCB_LAYER_ID aLayer,
                           const std::vector<VECTOR2I>& aWaypoints,
                           bool aAllowVia,
                           std::vector<BOARD_CONNECTED_ITEM*>* aOutItems )
{
    if( !m_board )
    {
        m_lastError = wxT( "RouteNet: not attached" );
        return false;
    }

    NETINFO_ITEM* net = m_board->FindNet( aNetName );

    if( !net )
    {
        m_lastError = wxString::Format( wxT( "RouteNet: net '%s' not found on board" ),
                                        aNetName );
        return false;
    }

    return Route( net, aFrom, aTo, aLayer, aWaypoints, aAllowVia, aOutItems );
}


PNS::ITEM* PNS_BRIDGE::findItemNear( PNS::NET_HANDLE aNet, const VECTOR2I& aWhere,
                                     int aPnsLayer, int aSlopRadius )
{
    PNS::ITEM_SET candidates = m_router->QueryHoverItems( aWhere, aSlopRadius );

    PNS::ITEM* best = nullptr;
    long long  bestScore = std::numeric_limits<long long>::max();

    for( PNS::ITEM* item : candidates.Items() )
    {
        if( item->Kind() == PNS::ITEM::LINE_T )
            continue;   // the routing head, not a board item

        if( !item->IsRoutable() )
            continue;

        if( !item->Layers().Overlaps( aPnsLayer ) )
            continue;

        if( aNet && item->Net() != aNet )
            continue;

        VECTOR2I anchor;
        int      kindBias = 2;  // prefer pads, then vias, then track endpoints

        switch( item->Kind() )
        {
        case PNS::ITEM::SOLID_T:
            anchor = static_cast<PNS::SOLID*>( item )->Pos();
            kindBias = 0;
            break;

        case PNS::ITEM::VIA_T:
            anchor = static_cast<PNS::VIA*>( item )->Pos();
            kindBias = 1;
            break;

        case PNS::ITEM::SEGMENT_T:
        {
            PNS::SEGMENT* seg = static_cast<PNS::SEGMENT*>( item );
            anchor = ( ( aWhere - seg->Anchor( 0 ) ).SquaredEuclideanNorm()
                       <= ( aWhere - seg->Anchor( 1 ) ).SquaredEuclideanNorm() )
                     ? seg->Anchor( 0 ) : seg->Anchor( 1 );
            break;
        }

        default:
            continue;
        }

        long long score = ( long long ) kindBias * 1000000000LL
                          + ( aWhere - anchor ).EuclideanNorm();

        if( score < bestScore )
        {
            bestScore = score;
            best = item;
        }
    }

    return best;
}


bool PNS_BRIDGE::routeLeg( PNS::NET_HANDLE aNet, const VECTOR2I& aFrom, const VECTOR2I& aTo,
                           PCB_LAYER_ID aLayer, bool aViaAtEnd )
{
    const int pnsLayer = m_iface->GetPNSLayerFromBoardLayer( aLayer );

    // 1. Find a same-net item near the leg start; the router needs it to pick
    //    up the net, inherited widths and posture (LINE_PLACER::Start).
    PNS::ITEM* startItem = findItemNear( aNet, aFrom, pnsLayer, m_searchRadius );

    if( !startItem )
        startItem = findItemNear( aNet, aFrom, pnsLayer, m_searchRadius * 3 );

    if( !startItem )
    {
        m_lastError = wxT( "no item of the requested net found near the start point" );
        return false;
    }

    // 2. Import track/via sizes from the board/netclass rules for this net.
    PNS::SIZES_SETTINGS sizes( m_router->Sizes() );

    m_iface->SetStartLayerFromPCBNew( aLayer );
    m_iface->ImportSizes( sizes, startItem, nullptr, VECTOR2D( aFrom.x, aFrom.y ) );

    if( m_trackWidth > 0 )
        sizes.SetTrackWidth( m_trackWidth );

    if( m_viaDiameter > 0 )
        sizes.SetViaDiameter( m_viaDiameter );

    if( m_viaDrill > 0 )
        sizes.SetViaDrill( m_viaDrill );

    m_router->UpdateSizes( sizes );

    // 3. Start the placer.
    if( !m_router->StartRouting( aFrom, startItem, pnsLayer ) )
    {
        m_lastError = wxT( "StartRouting failed: " ) + m_router->FailureReason();
        return false;
    }

    // 4. Route to the leg end.  The end item is optional (FixRoute is called
    //    with forceFinish = true), but passing it helps the placer terminate.
    PNS::ITEM* endItem = findItemNear( aNet, aTo, pnsLayer, m_searchRadius );

    m_router->Move( aTo, endItem );

    if( aViaAtEnd )
    {
        // Deposit a via at the leg end so the next leg can start on the other
        // layer; mirrors the GUI 'V' toggle during interactive routing.
        m_router->ToggleViaPlacement();
        m_router->Move( aTo, endItem );
    }

    // 5. Verify the head actually reached the target before committing.
    PNS::PLACEMENT_ALGO* placer = m_router->Placer();

    if( !placer || placer->CurrentEnd() != aTo )
    {
        if( !m_allowPartial )
        {
            m_router->StopRouting();    // discard; nothing is committed
            m_lastError = wxT( "router could not reach the target point "
                               "(obstacle, or shove failed); use SetAllowPartialRoute() "
                               "to keep partial results" );
            return false;
        }
    }

    // 6. Force-finish and commit: FixRoute() moves the fixed segments into the
    //    placer's last node; CommitRouting() hands them to the iface, which
    //    writes PCB_TRACK/PCB_VIA objects onto the BOARD (AddItem override).
    if( !m_router->FixRoute( aTo, endItem, true /* aForceFinish */, true /* aForceCommit */ ) )
    {
        m_router->StopRouting();
        m_lastError = wxT( "FixRoute failed: " ) + m_router->FailureReason();
        return false;
    }

    m_router->CommitRouting();

    // Created PCB_TRACK/PCB_VIA objects were appended to the caller's capture
    // list by PNS_HEADLESS_IFACE::AddItem() during CommitRouting().
    return true;
}


// ---------------------------------------------------------------------------
// PNS_HEADLESS_IFACE
// ---------------------------------------------------------------------------

void PNS_HEADLESS_IFACE::SetView( KIGFX::VIEW* aView )
{
    // Refuse: a view would pull ROUTER_PREVIEW_ITEM and GAL code into a
    // headless session.  m_view stays null and every Display*/Hide* override
    // below is a no-op.
    wxUnusedVar( aView );
}


void PNS_HEADLESS_IFACE::SetHostTool( PCB_TOOL_BASE* aTool )
{
    // Refuse: no TOOL_MANAGER exists headless.  m_tool/m_commit stay null;
    // the overrides below never touch them.
    wxUnusedVar( aTool );
}


void PNS_HEADLESS_IFACE::EraseView()
{
    // No preview items and no hidden board items headless.
    m_hiddenItems.clear();
}


void PNS_HEADLESS_IFACE::DisplayItem( const PNS::ITEM* aItem, int aClearance,
                                      bool aEdit, int aFlags )
{
    // The base PNS_KICAD_IFACE::DisplayItem dereferences m_tool
    // (pns_kicad_iface.cpp:2547) and m_view; both are null here.
    wxUnusedVar( aItem );
    wxUnusedVar( aClearance );
    wxUnusedVar( aEdit );
    wxUnusedVar( aFlags );
}


void PNS_HEADLESS_IFACE::DisplayPathLine( const SHAPE_LINE_CHAIN& aLine, int aImportance )
{
    wxUnusedVar( aLine );
    wxUnusedVar( aImportance );
}


void PNS_HEADLESS_IFACE::DisplayRatline( const SHAPE_LINE_CHAIN& aRatline, PNS::NET_HANDLE aNet )
{
    // Called on every Move() via LINE_PLACER::updateLeadingRatLine()
    // (pns_line_placer.cpp:1497); the base implementation dereferences m_view
    // (pns_kicad_iface.cpp:2612).
    wxUnusedVar( aRatline );
    wxUnusedVar( aNet );
}


void PNS_HEADLESS_IFACE::HideItem( PNS::ITEM* aItem )
{
    // The base implementation dereferences m_view (pns_kicad_iface.cpp:2659).
    wxUnusedVar( aItem );
}


void PNS_HEADLESS_IFACE::AddItem( PNS::ITEM* aItem )
{
    BOARD_CONNECTED_ITEM* boardItem = createBoardItem( aItem );

    if( !boardItem )
        return;

    aItem->SetParent( boardItem );
    boardItem->ClearFlags();

    // BOARD::Add() keeps CONNECTIVITY_DATA in sync by itself
    // (pcbnew/board.cpp, BOARD::Add(): m_connectivity->Add( aBoardItem )).
    GetBoard()->Add( boardItem, ADD_MODE::APPEND );

    if( m_capture )
        m_capture->push_back( boardItem );
}


void PNS_HEADLESS_IFACE::RemoveItem( PNS::ITEM* aItem )
{
    BOARD_ITEM* parent = aItem->Parent();

    if( !parent )
        return;

    // Pad solids are tracked via m_fpOffsets in Commit(), never removed here.
    if( aItem->OfKind( PNS::ITEM::SOLID_T ) && parent->Type() == PCB_PAD_T )
        return;

    GetBoard()->Remove( parent );   // also updates connectivity
}


void PNS_HEADLESS_IFACE::UpdateItem( PNS::ITEM* aItem )
{
    // Geometry-only mirror of PNS_KICAD_IFACE::modifyBoardItem() minus the
    // BOARD_COMMIT bookkeeping (no undo history in a headless session).
    BOARD_ITEM* boardItem = aItem->Parent();

    if( !boardItem )
        return;

    switch( aItem->Kind() )
    {
    case PNS::ITEM::ARC_T:
    {
        PNS::ARC*        arc = static_cast<PNS::ARC*>( aItem );
        PCB_ARC*         arcBoard = static_cast<PCB_ARC*>( boardItem );
        const SHAPE_ARC* arcShape = static_cast<const SHAPE_ARC*>( arc->Shape( -1 ) );

        arcBoard->SetStart( VECTOR2I( arcShape->GetP0() ) );
        arcBoard->SetEnd( VECTOR2I( arcShape->GetP1() ) );
        arcBoard->SetMid( VECTOR2I( arcShape->GetArcMid() ) );
        arcBoard->SetWidth( arc->Width() );
        break;
    }

    case PNS::ITEM::SEGMENT_T:
    {
        PNS::SEGMENT* seg = static_cast<PNS::SEGMENT*>( aItem );
        PCB_TRACK*    track = static_cast<PCB_TRACK*>( boardItem );
        const SEG&    s = seg->Seg();

        track->SetStart( VECTOR2I( s.A.x, s.A.y ) );
        track->SetEnd( VECTOR2I( s.B.x, s.B.y ) );
        track->SetWidth( seg->Width() );
        break;
    }

    case PNS::ITEM::VIA_T:
    {
        PCB_VIA*  viaBoard = static_cast<PCB_VIA*>( boardItem );
        PNS::VIA* via = static_cast<PNS::VIA*>( aItem );

        viaBoard->SetPosition( VECTOR2I( via->Pos().x, via->Pos().y ) );
        viaBoard->SetWidth( PADSTACK::TEMP_ALL_LAYERS, via->Diameter( 0 ) );
        viaBoard->SetDrill( via->Drill() );
        viaBoard->SetNet( static_cast<NETINFO_ITEM*>( via->Net() ) );
        viaBoard->SetViaType( via->ViaType() ); // MUST be before SetLayerPair()
        viaBoard->SetLayerPair( GetBoardLayerFromPNSLayer( via->Layers().Start() ),
                                GetBoardLayerFromPNSLayer( via->Layers().End() ) );
        break;
    }

    default:
        break;
    }

    // Keep connectivity in sync for moved geometry.
    GetBoard()->GetConnectivity()->Update( boardItem );
}


void PNS_HEADLESS_IFACE::Commit()
{
    // Apply pad offsets recorded while shoving around pads; the GUI does this
    // through BOARD_COMMIT (PNS_KICAD_IFACE::Commit), headless we move the
    // footprint directly.
    for( const auto& [ pad, fpOffset ] : m_fpOffsets )
    {
        VECTOR2I offset = fpOffset.p_new - fpOffset.p_old;

        if( FOOTPRINT* footprint = pad->GetParentFootprint() )
            footprint->Move( offset );
        else
            pad->Move( offset );
    }

    m_fpOffsets.clear();
    m_independentPads.clear();
    m_hiddenItems.clear();
}
