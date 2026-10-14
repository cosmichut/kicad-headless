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
#include <jobs/job_registry.h>
#include <i18n_utility.h>

JOB_PCB_ROUTE::JOB_PCB_ROUTE() :
        JOB( "route", false ),
        m_filename(),
        m_net(),
        m_fromX( 0.0 ),
        m_fromY( 0.0 ),
        m_toX( 0.0 ),
        m_toY( 0.0 ),
        m_layer( wxS( "F.Cu" ) ),
        m_waypoints(),
        m_allowVia( false )
{
}


wxString JOB_PCB_ROUTE::GetDefaultDescription() const
{
    return _( "Route a track between two points" );
}


wxString JOB_PCB_ROUTE::GetSettingsDialogTitle() const
{
    return _( "Route Job Settings" );
}


REGISTER_JOB( pcb_route, _HKI( "PCB: Route a track between two points" ), KIWAY::FACE_PCB,
              JOB_PCB_ROUTE );
