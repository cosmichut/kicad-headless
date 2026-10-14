/*
 * Headless Push-and-Shove routing bridge for external (scripting) access.
 *
 * Exposes the KiCad PNS router to non-GUI callers: given a BOARD, a net and a
 * from/to point pair (plus optional waypoints), drives ROUTER/LINE_PLACER/SHOVE
 * and writes the resulting PCB_TRACK/PCB_VIA objects back onto the BOARD.
 *
 * Installed in pcbnew/router/ (next to pns_router.cpp / pns_kicad_iface.cpp);
 * built into the pnsrouter static library via PCBNEW_PNS_SRCS.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * License: GPLv3+ (matches the surrounding PNS sources)
 */

#ifndef PNS_HEADLESS_API_H
#define PNS_HEADLESS_API_H

#include <vector>
#include <memory>

#include <layer_ids.h>
#include <math/vector2d.h>

#include "pns_kicad_iface.h"
#include "pns_routing_settings.h"

class BOARD;
class BOARD_CONNECTED_ITEM;
class NETINFO_ITEM;
class wxString;

namespace PNS
{
class ROUTER;
class ROUTER_IFACE;
class ITEM;
}

class PNS_HEADLESS_IFACE;

/**
 * PNS_BRIDGE
 *
 * Singleton façade around PNS::ROUTER for headless (no KIGFX::VIEW, no
 * TOOL_MANAGER) routing sessions.  Attach() a BOARD once, then call Route()
 * any number of times; each successful leg is committed directly onto the
 * BOARD via BOARD::Add() (which keeps CONNECTIVITY_DATA in sync, see
 * pcbnew/board.cpp BOARD::Add()).
 */
class PNS_BRIDGE
{
public:
    static PNS_BRIDGE& Instance();
    ~PNS_BRIDGE();

    /**
     * Bind the bridge to a board: creates a PNS_HEADLESS_IFACE, a PNS::ROUTER,
     * loads headless ROUTING_SETTINGS (RM_Shove) and syncs the PNS world.
     * Idempotent: attaching the same board again is a no-op; attaching a
     * different board tears the old session down first.
     */
    bool Attach( BOARD* aBoard );

    /// Tear the session down.  Safe to call when not attached.
    void Detach();

    bool IsAttached() const { return m_router != nullptr && m_board != nullptr; }
    BOARD* GetBoard() const { return m_board; }

    /// Re-import the whole board into the PNS world after external edits
    /// (footprint moves, zone fills, ...) done between Route() calls.
    void ResyncWorld();

    /**
     * Route from aFrom to aTo on aLayer, optionally through aWaypoints
     * (board internal units).  All points are on the same layer in this
     * entry point; aAllowVia currently gates via insertion when the API is
     * extended to per-waypoint layers (see RouteViaLayers()).
     *
     * @param aNet net to route (determines start/end snapping and track net).
     *             May be null, in which case the net is inferred from the
     *             netted copper item nearest aFrom (fails if there is none).
     * @param aOutItems optional; receives the BOARD_CONNECTED_ITEMs created
     *                  by this call (already added to the BOARD).
     * @return true when the complete route was committed to the BOARD.
     */
    bool Route( NETINFO_ITEM* aNet,
                const VECTOR2I& aFrom, const VECTOR2I& aTo,
                PCB_LAYER_ID aLayer,
                const std::vector<VECTOR2I>& aWaypoints,
                bool aAllowVia,
                std::vector<BOARD_CONNECTED_ITEM*>* aOutItems = nullptr );

    /// best-effort helper for scripting layers
    bool RouteNet( const wxString& aNetName,
                   const VECTOR2I& aFrom, const VECTOR2I& aTo,
                   PCB_LAYER_ID aLayer,
                   const std::vector<VECTOR2I>& aWaypoints,
                   bool aAllowVia,
                   std::vector<BOARD_CONNECTED_ITEM*>* aOutItems = nullptr );

    // ---- tuning knobs (must be set before Route(), not mid-route) ----
    void SetAllowDrcViolations( bool aAllow );
    void SetAllowPartialRoute( bool aAllow );   ///< commit what was routed when the far end is unreachable
    void SetTrackWidth( int aWidthIU );         ///< 0 = derive from board/netclass rules (default)
    void SetViaDiameter( int aDiameterIU );     ///< 0 = derive from board/netclass rules (default)
    void SetViaDrill( int aDrillIU );           ///< 0 = derive from board/netclass rules (default)
    void SetItemSearchRadius( int aRadiusIU );  ///< snap radius for start/end item lookup (default 1 mm)

    /// Human readable reason for the last failure (empty after success).
    const wxString& LastError() const { return m_lastError; }

private:
    PNS_BRIDGE();

    bool routeLeg( PNS::NET_HANDLE aNet, const VECTOR2I& aFrom, const VECTOR2I& aTo,
                   PCB_LAYER_ID aLayer, bool aViaAtEnd );

    PNS::ITEM* findItemNear( PNS::NET_HANDLE aNet, const VECTOR2I& aWhere,
                             int aPnsLayer, int aSlopRadius );

private:
    BOARD*               m_board = nullptr;
    PNS::ROUTER*         m_router = nullptr;
    PNS_HEADLESS_IFACE*  m_iface = nullptr;
    std::unique_ptr<ROUTING_SETTINGS> m_settings;

    bool m_allowDrcViolations = false;
    bool m_allowPartial = false;
    int  m_trackWidth = 0;
    int  m_viaDiameter = 0;
    int  m_viaDrill = 0;
    int  m_searchRadius = 1000000;  // 1 mm in internal units

    wxString m_lastError;
};

/**
 * PNS_HEADLESS_IFACE
 *
 * A PNS_KICAD_IFACE subclass that (a) replaces every VIEW/tool-backed method
 * with a headless-safe stub and (b) writes router items straight onto the
 * BOARD instead of through BOARD_COMMIT (which requires a TOOL_MANAGER).
 */
class PNS_HEADLESS_IFACE : public PNS_KICAD_IFACE
{
public:
    PNS_HEADLESS_IFACE() : PNS_KICAD_IFACE() {}

    // Never attach a view or host tool headless; keep them null.
    void SetView( KIGFX::VIEW* aView );
    void SetHostTool( PCB_TOOL_BASE* aTool );

    // --- view-facing methods: no-ops (KIGFX::VIEW is null) ---
    void EraseView() override;
    bool IsAnyLayerVisible( const PNS_LAYER_RANGE& aLayer ) const override { return true; }
    bool IsItemVisible( const PNS::ITEM* aItem ) const override { return true; }
    void DisplayItem( const PNS::ITEM* aItem, int aClearance, bool aEdit = false,
                      int aFlags = 0 ) override;
    void DisplayPathLine( const SHAPE_LINE_CHAIN& aLine, int aImportance ) override;
    void DisplayRatline( const SHAPE_LINE_CHAIN& aRatline, PNS::NET_HANDLE aNet ) override;
    void HideItem( PNS::ITEM* aItem ) override;
    EDA_UNITS GetUnits() const override { return EDA_UNITS::MM; }

    // --- board write-back without BOARD_COMMIT/TOOL_MANAGER ---
    void AddItem( PNS::ITEM* aItem ) override;
    void RemoveItem( PNS::ITEM* aItem ) override;
    void UpdateItem( PNS::ITEM* aItem ) override;
    void Commit() override;

    /// When non-null, every BOARD_CONNECTED_ITEM created here is recorded.
    void SetCaptureList( std::vector<BOARD_CONNECTED_ITEM*>* aList ) { m_capture = aList; }

private:
    std::vector<BOARD_CONNECTED_ITEM*>* m_capture = nullptr;
};

#endif // PNS_HEADLESS_API_H
