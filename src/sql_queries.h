/*
 * Copyright (C) 2007, Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * Written by Ira Weiny weiny2@llnl.gov
 * CODE-235483
 * 
 * This file is part of libopensmskummeeplugin
 * For details, see http://www.llnl.gov/linux/
 *              or  http://www.nongnu.org/osmskummeeplugin/
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * OUR NOTICE AND TERMS AND CONDITIONS OF THE GNU GENERAL PUBLIC LICENSE
 *
 * Our Preamble Notice
 *
 * A. This notice is required to be provided under our contract with the U.S.
 * Department of Energy (DOE). This work was produced at the Lawrence Livermore
 * National Laboratory under Contract No.  DE-AC52-07NA27344 with the DOE.
 *
 */

#ifndef _SQL_QUERIES_H_
#define _SQL_QUERIES_H_

#include "opensmskummeeplugin.h"

int  sql_setup_db_conn(plugin_data_t *plugin_data);
void sql_add_port_errors(plugin_data_t *plugin_data, osm_epi_pe_event_t *pe_event);
void sql_add_data_counters(plugin_data_t *plugin_data, osm_epi_dc_event_t *dc_event);
void sql_add_port_select(plugin_data_t *plugin_data, osm_epi_ps_event_t *ps_event);

#endif /* _SQL_QUERIES_H_ */

