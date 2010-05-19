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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <mysql/mysql.h>
#include <mysql/mysqld_error.h>

#include "sql_queries.h"

/** =========================================================================
 * Define MYSQL query macros and functions to make code more readable.
 */
#define MYSQL_QUERY_FAIL() \
	plugin_log(plugin_data->osmlog, OSM_LOG_ERROR, "MYSQL Query failed : %s\n", query_str);

#define QUERY_STR_LEN (1024)
static char query_str[QUERY_STR_LEN];
int QUERY(plugin_data_t *plugin_data, const char *fmt, ...)
{
	int rc = 0;
	va_list args;
	va_start(args, fmt);
	vsnprintf(query_str, QUERY_STR_LEN, fmt, args);
	if ((rc = mysql_query(plugin_data->conn,query_str))) {
		MYSQL_QUERY_FAIL();
	}
	va_end(args);
	return (rc);
}

#define COL_TO_UINT64(num) \
	row[num] ? strtoull(row[num], NULL, 0) : 0;

/** =========================================================================
 * Ensure the node described by the port is in the nodes table
 */
static int
sql_add_node(plugin_data_t *plugin_data, osm_epi_port_id_t *port_id)
{
	MYSQL_RES *res;
	int ret = 0;

	if ((ret = QUERY(plugin_data, "select * from nodes where guid=%"PRIuLEAST64,
			port_id->node_guid))) {
		return (ret);
	}

	res = mysql_store_result(plugin_data->conn);
	if (mysql_num_rows(res) != 1) {
		/* the node is not in the table add it */
		ret = QUERY(plugin_data,
			"insert into nodes set guid=%"PRIuLEAST64",name=\"%s\";",
			port_id->node_guid,
			port_id->node_name);
	}

	mysql_free_result(res);

	return (ret);
}

static inline void
insert_port_errors(plugin_data_t *plugin_data, osm_epi_pe_event_t *pe_event)
{
	QUERY(plugin_data,
		"insert into port_errors set guid=%"PRIuLEAST64",port=%d,"
		"SymbolErrors=%"PRIuLEAST64","
		"LinkRecovers=%"PRIuLEAST64","
		"LinkDowned=%"PRIuLEAST64","
		"RcvErrors=%"PRIuLEAST64","
		"RcvRemotePhysErrors=%"PRIuLEAST64","
		"RcvSwitchRelayErrors=%"PRIuLEAST64","
		"XmtDiscards=%"PRIuLEAST64","
		"XmtConstraintErrors=%"PRIuLEAST64","
		"RcvConstraintErrors=%"PRIuLEAST64","
		"LinkIntegrityErrors=%"PRIuLEAST64","
		"ExcBufOverrunErrors=%"PRIuLEAST64","
		"VL15Dropped=%"PRIuLEAST64","
		"time_diff_s=%"PRIuLEAST64""
		";",
		pe_event->port_id.node_guid,
		pe_event->port_id.port_num,
		pe_event->symbol_err_cnt,
		pe_event->link_err_recover,
		pe_event->link_downed,
		pe_event->rcv_err,
		pe_event->rcv_rem_phys_err,
		pe_event->rcv_switch_relay_err,
		pe_event->xmit_discards,
		pe_event->xmit_constraint_err,
		pe_event->rcv_constraint_err,
		pe_event->link_integrity,
		pe_event->buffer_overrun,
		pe_event->vl15_dropped,
		pe_event->time_diff_s
		);
}

static inline void
update_port_errors(plugin_data_t *plugin_data, osm_epi_pe_event_t *pe_event)
{
	QUERY(plugin_data,
		"update port_errors set "
		"SymbolErrors=%"PRIuLEAST64","
		"LinkRecovers=%"PRIuLEAST64","
		"LinkDowned=%"PRIuLEAST64","
		"RcvErrors=%"PRIuLEAST64","
		"RcvRemotePhysErrors=%"PRIuLEAST64","
		"RcvSwitchRelayErrors=%"PRIuLEAST64","
		"XmtDiscards=%"PRIuLEAST64","
		"XmtConstraintErrors=%"PRIuLEAST64","
		"RcvConstraintErrors=%"PRIuLEAST64","
		"LinkIntegrityErrors=%"PRIuLEAST64","
		"ExcBufOverrunErrors=%"PRIuLEAST64","
		"VL15Dropped=%"PRIuLEAST64","
		"time_diff_s=%"PRIuLEAST64" "
		"where guid=%"PRIuLEAST64" and port=%d"
		";",
		pe_event->symbol_err_cnt,
		pe_event->link_err_recover,
		pe_event->link_downed,
		pe_event->rcv_err,
		pe_event->rcv_rem_phys_err,
		pe_event->rcv_switch_relay_err,
		pe_event->xmit_discards,
		pe_event->xmit_constraint_err,
		pe_event->rcv_constraint_err,
		pe_event->link_integrity,
		pe_event->buffer_overrun,
		pe_event->vl15_dropped,
		pe_event->time_diff_s,
		pe_event->port_id.node_guid,
		pe_event->port_id.port_num
		);
}

void
sql_add_port_errors(plugin_data_t *plugin_data, osm_epi_pe_event_t *pe_event)
{
	unsigned int num_fields;
	MYSQL_RES *res;
	MYSQL_ROW  row;
	osm_epi_port_id_t *port_id = &(pe_event->port_id);

	if (sql_add_node(plugin_data, port_id))
		return;

	if (QUERY(plugin_data,
			"select SymbolErrors, LinkRecovers,"
			"       LinkDowned,RcvErrors, RcvRemotePhysErrors,"
			"       RcvSwitchRelayErrors, XmtDiscards,"
			"       XmtConstraintErrors, RcvConstraintErrors,"
			"       LinkIntegrityErrors, ExcBufOverrunErrors,"
			"       VL15Dropped"
			"       from port_errors where guid=%"PRIuLEAST64" and port=%d",
			port_id->node_guid, port_id->port_num)) {
		return;
	}

	res = mysql_store_result(plugin_data->conn);

	if (mysql_num_rows(res) == 0) {
		/* simple insert for the first time */
		insert_port_errors(plugin_data, pe_event);
	} else if (mysql_num_rows(res) == 1) {
		/* must retrieve previous counts to be added to the new counts. */

		if ((num_fields = mysql_num_fields(res)) != 12) {
			plugin_log(plugin_data->osmlog, OSM_LOG_ERROR,
				"Failed to query port_error table for guid %"PRIuLEAST64", port %d; col ret %d != 12\n",
				port_id->node_guid, port_id->port_num, num_fields);
			goto free_res;
		}
		row = mysql_fetch_row(res);

		pe_event->symbol_err_cnt       += COL_TO_UINT64(0);
		pe_event->link_err_recover     += COL_TO_UINT64(1);
		pe_event->link_downed          += COL_TO_UINT64(2);
		pe_event->rcv_err              += COL_TO_UINT64(3);
		pe_event->rcv_rem_phys_err     += COL_TO_UINT64(4);
		pe_event->rcv_switch_relay_err += COL_TO_UINT64(5);
		pe_event->xmit_discards        += COL_TO_UINT64(6);
		pe_event->xmit_constraint_err  += COL_TO_UINT64(7);
		pe_event->rcv_constraint_err   += COL_TO_UINT64(8);
		pe_event->link_integrity       += COL_TO_UINT64(9);
		pe_event->buffer_overrun       += COL_TO_UINT64(10);
		pe_event->vl15_dropped         += COL_TO_UINT64(11);

		update_port_errors(plugin_data, pe_event);
	} else {
		/* more than one result? */
		plugin_log(plugin_data->osmlog, OSM_LOG_ERROR,
			"%llu port_error rows returned for guid %"PRIuLEAST64", port %d\n",
			mysql_num_rows(res), port_id->node_guid, port_id->port_num);
	}

free_res:
	mysql_free_result(res);
}

static inline void
insert_port_data_counters(plugin_data_t *plugin_data, osm_epi_dc_event_t *dc_event)
{
	QUERY(plugin_data,
		"insert into port_data_counters set guid=%"PRIuLEAST64",port=%d,"
		"xmit_data=%"PRIuLEAST64","
		"rcv_data=%"PRIuLEAST64","
		"xmit_pkts=%"PRIuLEAST64","
		"rcv_pkts=%"PRIuLEAST64","
		"unicast_xmit_pkts=%"PRIuLEAST64","
		"unicast_rcv_pkts=%"PRIuLEAST64","
		"multicast_xmit_pkts=%"PRIuLEAST64","
		"multicast_rcv_pkts=%"PRIuLEAST64","
		"time_diff_s=%"PRIuLEAST64""
		";",
		dc_event->port_id.node_guid, dc_event->port_id.port_num,
		dc_event->xmit_data,
		dc_event->rcv_data,
		dc_event->xmit_pkts,
		dc_event->rcv_pkts,
		dc_event->unicast_xmit_pkts,
		dc_event->unicast_rcv_pkts,
		dc_event->multicast_xmit_pkts,
		dc_event->multicast_rcv_pkts,
		dc_event->time_diff_s
		);
}

static inline void
update_port_data_counters(plugin_data_t *plugin_data, osm_epi_dc_event_t *dc_event)
{
	QUERY(plugin_data,
		"update port_data_counters set "
		"xmit_data=%"PRIuLEAST64","
		"rcv_data=%"PRIuLEAST64","
		"xmit_pkts=%"PRIuLEAST64","
		"rcv_pkts=%"PRIuLEAST64","
		"unicast_xmit_pkts=%"PRIuLEAST64","
		"unicast_rcv_pkts=%"PRIuLEAST64","
		"multicast_xmit_pkts=%"PRIuLEAST64","
		"multicast_rcv_pkts=%"PRIuLEAST64","
		"time_diff_s=%"PRIuLEAST64" "
		"where guid=%"PRIuLEAST64" and port=%d"
		";",
		dc_event->xmit_data,
		dc_event->rcv_data,
		dc_event->xmit_pkts,
		dc_event->rcv_pkts,
		dc_event->unicast_xmit_pkts,
		dc_event->unicast_rcv_pkts,
		dc_event->multicast_xmit_pkts,
		dc_event->multicast_rcv_pkts,
		dc_event->time_diff_s,
		dc_event->port_id.node_guid, dc_event->port_id.port_num
		);
}

void
sql_add_data_counters(plugin_data_t *plugin_data, osm_epi_dc_event_t *dc_event)
{
	unsigned int num_fields;
	MYSQL_RES *res;
	MYSQL_ROW  row;
	osm_epi_port_id_t *port_id = &(dc_event->port_id);

	if (sql_add_node(plugin_data, port_id))
		return;

	if (QUERY(plugin_data,
			"select xmit_data,rcv_data,xmit_pkts,rcv_pkts,"
			"       unicast_xmit_pkts,unicast_rcv_pkts,multicast_xmit_pkts,multicast_rcv_pkts"
			"       from port_data_counters where guid=%"PRIuLEAST64" and port=%d",
			port_id->node_guid, port_id->port_num)) {
		return;
	}

	res = mysql_store_result(plugin_data->conn);

	if (mysql_num_rows(res) == 0) {
		/* simple insert for the first time */
		insert_port_data_counters(plugin_data, dc_event);
	} else if (mysql_num_rows(res) == 1) {
		/* must retrieve previous counts to be added to the new counts. */

		if ((num_fields = mysql_num_fields(res)) != 8) {
			plugin_log(plugin_data->osmlog, OSM_LOG_ERROR,
				"Failed to query port_data_counters table for guid %"PRIuLEAST64", port %d; col ret %d != 8\n",
				port_id->node_guid, port_id->port_num, num_fields);
			goto free_res;
		}
		row = mysql_fetch_row(res);

		dc_event->xmit_data           += COL_TO_UINT64(0);
		dc_event->rcv_data            += COL_TO_UINT64(1);
		dc_event->xmit_pkts           += COL_TO_UINT64(2);
		dc_event->rcv_pkts            += COL_TO_UINT64(3);
		dc_event->unicast_xmit_pkts   += COL_TO_UINT64(4);
		dc_event->unicast_rcv_pkts    += COL_TO_UINT64(5);
		dc_event->multicast_xmit_pkts += COL_TO_UINT64(6);
		dc_event->multicast_rcv_pkts  += COL_TO_UINT64(7);

		update_port_data_counters(plugin_data, dc_event);
	} else {
		/* more than one result? */
		plugin_log(plugin_data->osmlog, OSM_LOG_ERROR,
			"%llu port_data_counters rows returned for guid %"PRIuLEAST64", port %d\n",
			mysql_num_rows(res), port_id->node_guid, port_id->port_num);
	}

free_res:
	mysql_free_result(res);
}

static void
insert_port_select(plugin_data_t *plugin_data, osm_epi_ps_event_t *ps_event)
{
	QUERY(plugin_data,
		"insert into port_select_counters set guid=%"PRIuLEAST64",port=%d,"
		"xmit_wait=%"PRIuLEAST64","
		"time_diff_s=%"PRIuLEAST64""
		";",
		ps_event->port_id.node_guid,
		ps_event->port_id.port_num,
		ps_event->xmit_wait,
		ps_event->time_diff_s
		);
}

static void
update_port_select(plugin_data_t *plugin_data, osm_epi_ps_event_t *ps_event)
{
	QUERY(plugin_data,
		"update port_select_counters set "
		"xmit_wait=%"PRIuLEAST64","
		"time_diff_s=%"PRIuLEAST64" "
		"where guid=%"PRIuLEAST64",port=%d,"
		";",
		ps_event->xmit_wait,
		ps_event->time_diff_s,
		ps_event->port_id.node_guid,
		ps_event->port_id.port_num
		);
}

void
sql_add_port_select(plugin_data_t *plugin_data, osm_epi_ps_event_t *ps_event)
{
	unsigned int num_fields;
	MYSQL_RES *res;
	MYSQL_ROW  row;
	osm_epi_port_id_t *port_id = &(ps_event->port_id);

	if (sql_add_node(plugin_data, port_id))
		return;

	if (QUERY(plugin_data,
			"select xmit_wait"
			"       from port_select_counters where guid=%"PRIuLEAST64" and port=%d",
			port_id->node_guid, port_id->port_num)) {
		return;
	}

	res = mysql_store_result(plugin_data->conn);

	if (mysql_num_rows(res) == 0) {
		/* simple insert for the first time */
		insert_port_select(plugin_data, ps_event);
	} else if (mysql_num_rows(res) == 1) {
		/* must retrieve previous counts to be added to the new counts. */

		if ((num_fields = mysql_num_fields(res)) != 1) {
			plugin_log(plugin_data->osmlog, OSM_LOG_ERROR,
				"Failed to query port_select_counters table for guid %"PRIuLEAST64", port %d; col ret %d != 1\n",
				port_id->node_guid, port_id->port_num, num_fields);
			goto free_res;
		}
		row = mysql_fetch_row(res);

		ps_event->xmit_wait += COL_TO_UINT64(0);

		update_port_select(plugin_data, ps_event);
	} else {
		/* more than one result? */
		plugin_log(plugin_data->osmlog, OSM_LOG_ERROR,
			"%llu port_select_counters rows returned for guid %"PRIuLEAST64", port %d\n",
			mysql_num_rows(res), port_id->node_guid, port_id->port_num);
	}

free_res:
	mysql_free_result(res);
}

int
sql_setup_db_conn(plugin_data_t *plugin_data)
{
	/* open DB connection */
	if (!mysql_init(&(plugin_data->mysql))) {
		plugin_log(plugin_data->osmlog, OSM_LOG_ERROR, "Failed to Connect to MySQL\n");
		return (1);
	}
	if (!(plugin_data->conn = mysql_real_connect(&(plugin_data->mysql), NULL,
			plugin_data->db_user, plugin_data->db_password,
			plugin_data->db_name, 0, NULL, 0))) {
		plugin_log(plugin_data->osmlog, OSM_LOG_ERROR, "Failed to open database \"%s\" : %s\n",
			plugin_data->db_name, mysql_error(&(plugin_data->mysql)));
		mysql_close(&(plugin_data->mysql));
		return (1);
	}


	if (plugin_data->clear_db_on_load) {
		/* Clean up the DB */
		QUERY(plugin_data, "delete from nodes;");
		QUERY(plugin_data, "delete from port_errors;");
		QUERY(plugin_data, "delete from port_data_counters;");
		QUERY(plugin_data, "delete from port_select_counters;");
	}

	plugin_log(plugin_data->osmlog, OSM_LOG_INFO,
		"Opened %s as user %s\n", plugin_data->db_name, plugin_data->db_user);
	return (0);
}

