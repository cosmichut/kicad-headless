/*
 * pybind11 exposure draft for PNS_BRIDGE  --  [需应用, experimental] draft.
 *
 * CONTEXT: the current master tree has removed pcbnew/swig entirely; the
 * blessed python story is the external kicad-python package talking to the
 * IPC API (protobuf) served by pcbnew's api_handler_pcb.cpp.  Until PNS
 * routing has an IPC API surface, this module is the fastest way to drive
 * the engine headless from python.
 *
 * BUILD (summary; full notes in CMAKE_HOOKS.md):
 *   - compile this file against pybind11 and link the same object sets as
 *     pcbnew_kiface_objects + pnsrouter + common + gal + core (Windows/MSVC).
 *   - produces "pns_headless.pyd"; import alongside a built KiCad tree:
 *
 * PYTHON USAGE:
 *   import pns_headless
 *   board = pns_headless.LoadBoard("board.kicad_pcb")   # or reuse kicad APIs
 *   br = pns_headless.Bridge()
 *   br.Attach( board )
 *   br.SetTrackWidthMM( 0.2 )
 *   ok = br.RouteMM( "EN6_MCU", 5.78, 18.1, 11.2, 26.95, "F.Cu", True )
 *   print( br.LastError() )
 *   br.Detach()
 */

#include <cmath>
#include <stdexcept>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <board.h>
#include <pcb_load_and_save.h>   // LoadBoard() declaration location varies; see note

#include "pns_headless_api.h"

namespace py = pybind11;

static BOARD* LoadBoardWrapper( const std::string& aPath )
{
    // NOTE: pull the real loader from the tree you build against; on master
    // this is PLUGINS IO_MERGER based loading (pcbnew/io_merger/... or
    // pcbnew/files.cpp LoadBoard).  Kept minimal in the draft.
    return ::LoadBoard( wxString::FromUTF8( aPath.c_str() ) );
}


static PCB_LAYER_ID LayerFromName( BOARD* aBoard, const std::string& aName )
{
    wxString name = wxString::FromUTF8( aName.c_str() );
    return aBoard->GetLayerID( name );
}


PYBIND11_MODULE( pns_headless, m )
{
    m.doc() = "Headless KiCad Push-and-Shove routing bridge (draft)";

    m.def( "LoadBoard", &LoadBoardWrapper, py::return_value_policy::reference,
           "Load a .kicad_pcb file (caller keeps ownership via the bridge)" );

    py::class_<PNS_BRIDGE, std::unique_ptr<PNS_BRIDGE, py::nodelete>>(
            m, "Bridge" )
        .def_static( "Instance", &PNS_BRIDGE::Instance,
                     py::return_value_policy::reference )
        .def( "Attach", &PNS_BRIDGE::Attach )
        .def( "Detach", &PNS_BRIDGE::Detach )
        .def( "ResyncWorld", &PNS_BRIDGE::ResyncWorld )
        .def( "IsAttached", &PNS_BRIDGE::IsAttached )
        .def( "Route",
              []( PNS_BRIDGE& self, const std::string& aNet,
                  double aFromXMm, double aFromYMm,
                  double aToXMm, double aToYMm,
                  const std::string& aLayer, bool aAllowVia,
                  const std::vector<double>& aWaypointsMM )
              {
                  BOARD* board = nullptr; // the bridge holds the board
                  if( !self.IsAttached() )
                      throw std::runtime_error( "not attached" );

                  const int iu = 1000000; // pcbIUScale.IU_PER_MM
                  auto toIU = [&]( double v ) { return (int) llround( v * iu ); };

                  // Resolve the layer name through the attached board
                  // (PNS_BRIDGE::GetBoard()).
                  board = self.GetBoard();
                  PCB_LAYER_ID layer = LayerFromName( board, aLayer );

                  std::vector<VECTOR2I> waypoints;

                  for( size_t i = 0; i + 1 < aWaypointsMM.size(); i += 2 )
                      waypoints.emplace_back( toIU( aWaypointsMM[i] ),
                                              toIU( aWaypointsMM[i + 1] ) );

                  return self.RouteNet( wxString::FromUTF8( aNet.c_str() ),
                                        VECTOR2I( toIU( aFromXMm ), toIU( aFromYMm ) ),
                                        VECTOR2I( toIU( aToXMm ), toIU( aToYMm ) ),
                                        layer, waypoints, aAllowVia, nullptr );
              },
              py::arg( "net" ), py::arg( "from_x_mm" ), py::arg( "from_y_mm" ),
              py::arg( "to_x_mm" ), py::arg( "to_y_mm" ),
              py::arg( "layer" ), py::arg( "allow_via" ) = true,
              py::arg( "waypoints_mm" ) = std::vector<double>() )
        .def( "SetAllowDrcViolations", &PNS_BRIDGE::SetAllowDrcViolations )
        .def( "SetAllowPartialRoute", &PNS_BRIDGE::SetAllowPartialRoute )
        .def( "SetTrackWidthMM",
              []( PNS_BRIDGE& self, double aMm )
              {
                  self.SetTrackWidth( (int) llround( aMm * 1000000.0 ) );
              } )
        .def( "SetViaDiameterMM",
              []( PNS_BRIDGE& self, double aMm )
              {
                  self.SetViaDiameter( (int) llround( aMm * 1000000.0 ) );
              } )
        .def( "SetViaDrillMM",
              []( PNS_BRIDGE& self, double aMm )
              {
                  self.SetViaDrill( (int) llround( aMm * 1000000.0 ) );
              } )
        .def( "SetItemSearchRadiusMM",
              []( PNS_BRIDGE& self, double aMm )
              {
                  self.SetItemSearchRadius( (int) llround( aMm * 1000000.0 ) );
              } )
        .def( "LastError",
              []( PNS_BRIDGE& self )
              {
                  return std::string( self.LastError().utf8_str().data() );
              } );
}
