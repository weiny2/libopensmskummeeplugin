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

#ifndef _OPENSMSKUMMEE_PLUGIN_H_
#define _OPENSMSKUMMEE_PLUGIN_H_

#include <opensm/osm_event_plugin.h>
#include <opensm/osm_version.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_log.h>
#include "opensmskummeeconf.h"

/** =========================================================================
 * Keep entries until the worker thread comes around to process them.
 */
typedef struct _data_entry {
	struct _data_entry *next;
	struct _data_entry *prev;
	osm_epi_event_id_t  type;
	union {
		osm_epi_pe_event_t   pe_event;
		osm_epi_dc_event_t   dc_event;
		osm_epi_ps_event_t   ps_event;
		ib_mad_notice_attr_t trap_event;
	} data;
} data_entry_t;

/** =========================================================================
 * Main plugin data structure
 */
typedef struct _plugin_data {
	osm_log_t       *osmlog;
	/* MYSQL */
	char            *db_user;
	char            *db_name;
	char            *db_password;
	MYSQL            mysql;
	MYSQL           *conn;
	/* Thread */
	pthread_t        thread;
	int              exit_flag;
	pthread_cond_t   signal;
	pthread_mutex_t  sig_lock;
	int              thread_sleep_s;
	/* DB Data */
	pthread_mutex_t  lock;
	int              clear_db_on_load;
	data_entry_t    *head;
	data_entry_t    *tail;
} plugin_data_t;

/** =========================================================================
 * Define an error function which indicate where this error is comming from
 */
#define plugin_log(log, level, fmt, args...) \
	OSM_LOG(log, level, "skummee plugin: " fmt, ## args);

#endif /* _OPENSMSKUMMEE_PLUGIN_H_ */

