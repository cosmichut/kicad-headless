/*
 * SWIG exposure draft for PNS_BRIDGE  --  [需应用] patch draft, not applied.
 *
 * TARGET: KiCad 9.x trees that still carry pcbnew/swig/ (the current master
 * tree this draft was authored against has REMOVED SWIG entirely; for master
 * use pybind11_pns_headless_draft.cpp / the IPC API instead).
 *
 * INSTALL (KiCad 9.x):
 *   1. Copy pns_headless_api.{h,cpp} into pcbnew/router/.
 *   2. Add "router/pns_headless_api.cpp" to PCBNEW_PNS_SRCS in
 *      pcbnew/router/CMakeLists.txt.
 *   3. Copy this file to pcbnew/swig/pns_headless.i.
 *   4. In pcbnew/swig/pcbnew.i, after the board includes
 *      (%include board.i / board_item.h ...), add:
 *          %include "pns_headless.i"
 *   5. Rebuild the pcbnew target; import pcbnew; see usage below.
 *
 * PYTHON USAGE:
 *   import pcbnew
 *   board = pcbnew.LoadBoard("board.kicad_pcb")
 *   br = pcbnew.PNS_BRIDGE_PY.Instance()
 *   br.Attach(board)
 *   br.SetTrackWidthMM(0.2)
 *   ok = br.RouteMM( "EN6_MCU", (5.78, 18.1), (11.2, 26.95),
 *                    pcbnew.F_Cu, True, [] )        # no intermediate waypoints
 *   if not ok:
 *       print( br.LastError() )
 *   br.Detach()
 */

%include <std_vector.i>
%include <std_string.i>

// std::vector<BOARD_CONNECTED_ITEM*> -> python list of board items
namespace std {
    %template( VecBoardConnectedItem ) vector<BOARD_CONNECTED_ITEM*>;
};

%inline %{

#include "router/pns_headless_api.h"

/**
 * Thin wrapper kept separate from PNS_BRIDGE so the SWIG layer stays
 * value-type simple: all mm-based convenience lives here, the C++ core
 * stays in internal units.
 */
class PNS_BRIDGE_PY
{
public:
    static PNS_BRIDGE_PY& Instance()
    {
        static PNS_BRIDGE_PY s_inst;
        return s_inst;
    }

    bool Attach( BOARD* aBoard )
    {
        return PNS_BRIDGE::Instance().Attach( aBoard );
    }

    void Detach()
    {
        PNS_BRIDGE::Instance().Detach();
    }

    void ResyncWorld()
    {
        PNS_BRIDGE::Instance().ResyncWorld();
    }

    /**
     * Route in millimetres.  aWaypoints is a flat list of mm pairs:
     * [x1, y1, x2, y2, ...].  aLayer is a pcbnew layer constant
     * (pcbnew.F_Cu, pcbnew.B_Cu, ...).  Returns true on success.
     */
    bool RouteMM( const std::string& aNetName,
                  std::vector<double> aFromMM, std::vector<double> aToMM,
                  PCB_LAYER_ID aLayer, bool aAllowVia,
                  std::vector<double> aWaypointsMM )
    {
        if( aFromMM.size() < 2 || aToMM.size() < 2 || aWaypointsMM.size() % 2 != 0 )
            return false;

        auto toIU = []( double aMm ) { return pcbIUScale.mmToIU( aMm ); };

        VECTOR2I from( toIU( aFromMM[0] ), toIU( aFromMM[1] ) );
        VECTOR2I to  ( toIU( aToMM[0] ),   toIU( aToMM[1] )   );

        std::vector<VECTOR2I> waypoints;

        for( size_t i = 0; i + 1 < aWaypointsMM.size(); i += 2 )
            waypoints.emplace_back( toIU( aWaypointsMM[i] ), toIU( aWaypointsMM[i + 1] ) );

        m_lastItems.clear();

        return PNS_BRIDGE::Instance().RouteNet(
                wxString::FromUTF8( aNetName.c_str() ),
                from, to, aLayer, waypoints, aAllowVia, &m_lastItems );
    }

    /// Board items created by the last successful RouteMM().
    std::vector<BOARD_CONNECTED_ITEM*> LastRouteItems()
    {
        return m_lastItems;
    }

    // ---- settings ----
    void SetAllowDrcViolations( bool aAllow )
    {
        PNS_BRIDGE::Instance().SetAllowDrcViolations( aAllow );
    }

    void SetAllowPartialRoute( bool aAllow )
    {
        PNS_BRIDGE::Instance().SetAllowPartialRoute( aAllow );
    }

    void SetTrackWidthMM( double aMm )
    {
        PNS_BRIDGE::Instance().SetTrackWidth( pcbIUScale.mmToIU( aMm ) );
    }

    void SetViaDiameterMM( double aMm )
    {
        PNS_BRIDGE::Instance().SetViaDiameter( pcbIUScale.mmToIU( aMm ) );
    }

    void SetViaDrillMM( double aMm )
    {
        PNS_BRIDGE::Instance().SetViaDrill( pcbIUScale.mmToIU( aMm ) );
    }

    void SetItemSearchRadiusMM( double aMm )
    {
        PNS_BRIDGE::Instance().SetItemSearchRadius( pcbIUScale.mmToIU( aMm ) );
    }

    std::string LastError()
    {
        wxString err = PNS_BRIDGE::Instance().LastError();
        return std::string( err.utf8_str().data() );
    }

private:
    PNS_BRIDGE_PY() {}
    std::vector<BOARD_CONNECTED_ITEM*> m_lastItems;
};

%}
