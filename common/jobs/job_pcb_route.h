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

#ifndef JOB_PCB_ROUTE_H
#define JOB_PCB_ROUTE_H

#include <kicommon.h>
#include <math/vector2d.h>
#include <vector>
#include "job.h"

class KICOMMON_API JOB_PCB_ROUTE : public JOB
{
public:
    JOB_PCB_ROUTE();

    wxString GetDefaultDescription() const override;
    wxString GetSettingsDialogTitle() const override;

    /// Board file to load and route.  The routed result is saved back to it.
    wxString m_filename;

    /// Net to route.  Empty means "infer from the copper item nearest the start point".
    wxString m_net;

    /// Route endpoints, in mm, board coordinates (Y grows downwards).
    double m_fromX;
    double m_fromY;
    double m_toX;
    double m_toY;

    /// Copper layer name (e.g. "F.Cu").  Empty defaults to the front copper layer.
    wxString m_layer;

    /// Intermediate points, in mm, all on m_layer.
    std::vector<VECTOR2D> m_waypoints;

    /// Reserved: allow the router to change layers (via insertion) in future
    /// multi-layer revisions of the route entry point.
    bool m_allowVia;
};

#endif // JOB_PCB_ROUTE_H
