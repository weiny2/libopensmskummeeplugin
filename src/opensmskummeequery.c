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

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#if HAVE_LIBGENDERS
/* If we have libgenders we can enable some additional options. */
#include <genders.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>

#include <mysql/mysql.h>
#include <mysql/mysqld_error.h>

#include <complib/cl_nodenamemap.h>

#define _GNU_SOURCE
#include <getopt.h>

#include "opensmskummeeconf.h"

static char *node_name_map_file = NULL;
static nn_map_t *node_name_map = NULL;

typedef struct _mysql_connection {
	char            *db_user;
	char            *db_name;
	char            *db_password;
	MYSQL            mysql;
	MYSQL           *conn;
} mysql_connection_t;

static char *argv0;
static int show_queries = 0;
static int skummee_output = 0;

#if HAVE_LIBGENDERS
#define IB_SWITCH_TOTAL_NODE_NAME "Total_IB_Switch_Errors"
static int sum_fabric_errors = 0;
typedef struct {
	uint64_t SymbolErrors;
	uint64_t LinkRecovers;
	uint64_t LinkDowned;
	uint64_t RcvErrors;
	uint64_t RcvRemotePhysErrors;
	uint64_t RcvSwitchRelayErrors;
	uint64_t XmtDiscards;
	uint64_t XmtConstraintErrors;
	uint64_t RcvConstraintErrors;
	uint64_t LinkIntegrityErrors;
	uint64_t ExcBufOverrunErrors;
	uint64_t VL15Dropped;
} switch_error_sum_t;
#endif

/** =========================================================================
 * Define MYSQL query macros and functions to make code more readable.
 */
#define MYSQL_QUERY_FAIL() \
	fprintf(stderr, "MYSQL Query failed : %s\n", query_str);

#define QUERY_STR_LEN (1024)
static char query_str[QUERY_STR_LEN];
int QUERY(mysql_connection_t *conn, const char *fmt, ...)
{
	int rc = 0;
	va_list args;
	va_start(args, fmt);
	vsnprintf(query_str, QUERY_STR_LEN, fmt, args);
	if (show_queries) {
		printf("QUERY: %s\n", query_str);
	}
	if ((rc = mysql_query(conn->conn,query_str))) {
		MYSQL_QUERY_FAIL();
	}
	va_end(args);
	return (rc);
}

#define COL_TO_UINT64(num) \
	row[num] ? strtoull(row[num], NULL, 0) : 0

/**
 * overwrites characters in name
 */
static char *
clean_node_name(char *name)
{
	int i = 0;
	for (i = 0; name[i] != '\0'; i++) {
		char c = name[i];

		if (isspace(c))
			name[i] = '_';

		switch (c) {
			case ',':
			case '"':
			case '=':
			case '&':
			case '`':
			case ';':
			case '\'':
			case '\\':
				name[i] = '_';
		}
	}
	return (name);
}

static uint64_t
get_guid_for_name(nn_map_t * map, char *name)
{
	cl_map_item_t *map_item = NULL;

	for (map_item = cl_qmap_head(map);
	     map_item != cl_qmap_end(map);
	     map_item = cl_qmap_next(map_item)) {
		name_map_item_t *item = (name_map_item_t *)map_item;
		char *iname = item->name;
		if (skummee_output) {
			char tmp[256];
			strncpy(tmp, item->name, 256);
			clean_node_name(tmp);
			iname = tmp;
		}
		if (strcmp(iname, name) == 0)
			return (item->guid);
	}

	return (0);
}

static int
query_status(mysql_connection_t *conn)
{
	int rc = 0;
	unsigned int  num_fields;
	unsigned int  num_rows, i;
	uint64_t      errors = 0;
	MYSQL_RES    *res;
	MYSQL_ROW     row;
#if HAVE_LIBGENDERS
	genders_t genders_handle;
	char        **nodes = NULL;
	int           num_nodes = 0;
	switch_error_sum_t switch_error_sum;

	memset(&switch_error_sum, 0, (sizeof switch_error_sum));
#endif

	/* issue the query join for any errors */
	if (QUERY(conn,
			"select pe.SymbolErrors, pe.LinkRecovers,"
			" pe.LinkDowned, pe.RcvErrors, pe.RcvRemotePhysErrors,"
			" pe.RcvSwitchRelayErrors, pe.XmtDiscards,"
			" pe.XmtConstraintErrors, pe.RcvConstraintErrors,"
			" pe.LinkIntegrityErrors, pe.ExcBufOverrunErrors,"
			" pe.VL15Dropped, n.name, pe.port, n.guid"
			" from port_errors as pe,nodes as n where n.guid=pe.guid;")) {
		fprintf(stderr, "Failed to query node errors\n");
		return (1);
	}

	res = mysql_store_result(conn->conn);

	if ((num_fields = mysql_num_fields(res)) != 15) {
		fprintf(stderr, "%s:%d Failed to query status %d != 14\n",
			__FUNCTION__, __LINE__, num_fields);
		rc = 1;
		goto free_res;
	}

	num_rows = mysql_num_rows(res);

#if HAVE_LIBGENDERS
	genders_handle = genders_handle_create();
	if (!genders_handle || genders_load_data(genders_handle, NULL)) {
		fprintf(stderr, "Genders load failed: %s\n", genders_handle ?
			genders_errormsg(genders_handle)
			: "handle creation failed");
		goto genders_open_fail;
	}
#endif

	/* The basic algo is to sum up all the errors and report them together */
	for (i = 0; i < num_rows; i++) {
		char node_desc[65];
		char *node_name = NULL;

		row = mysql_fetch_row(res);

		/* add up all the errors reported. */
		errors = 0;
		errors += COL_TO_UINT64(0);
		errors += COL_TO_UINT64(1);
		errors += COL_TO_UINT64(2);
		errors += COL_TO_UINT64(3);
		errors += COL_TO_UINT64(4);
		errors += COL_TO_UINT64(5);
		errors += COL_TO_UINT64(6);
		errors += COL_TO_UINT64(7);
		errors += COL_TO_UINT64(8);
		errors += COL_TO_UINT64(9);
		errors += COL_TO_UINT64(10);
		errors += COL_TO_UINT64(11);

#if HAVE_LIBGENDERS
		if (sum_fabric_errors && !genders_isnode(genders_handle, row[12])) {
			switch_error_sum.SymbolErrors         += COL_TO_UINT64(0);
			switch_error_sum.LinkRecovers         += COL_TO_UINT64(1);
			switch_error_sum.LinkDowned           += COL_TO_UINT64(2);
			switch_error_sum.RcvErrors            += COL_TO_UINT64(3);
			switch_error_sum.RcvRemotePhysErrors  += COL_TO_UINT64(4);
			switch_error_sum.RcvSwitchRelayErrors += COL_TO_UINT64(5);
			switch_error_sum.XmtDiscards          += COL_TO_UINT64(6);
			switch_error_sum.XmtConstraintErrors  += COL_TO_UINT64(7);
			switch_error_sum.RcvConstraintErrors  += COL_TO_UINT64(8);
			switch_error_sum.LinkIntegrityErrors  += COL_TO_UINT64(9);
			switch_error_sum.ExcBufOverrunErrors  += COL_TO_UINT64(10);
			switch_error_sum.VL15Dropped          += COL_TO_UINT64(11);
		}
#endif

		strncpy(node_desc, row[12], 64);
		node_name = remap_node_name(node_name_map, COL_TO_UINT64(14), node_desc);
		clean_node_name(node_name);
		printf("%s,ibport%s-error_sum,%"PRIuLEAST64"\n",
			node_name, row[13], errors);
		free(node_name);
	}

#if HAVE_LIBGENDERS
	if (sum_fabric_errors) {
		printf("%s,SymbolErrors_sum,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.SymbolErrors);
		printf("%s,SymbolErrors_sum_counter,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.SymbolErrors);

		printf("%s,LinkRecovers_sum,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.LinkRecovers);
		printf("%s,LinkRecovers_sum_counter,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.LinkRecovers);

		printf("%s,LinkDowned_sum,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.LinkDowned);
		printf("%s,LinkDowned_sum_counter,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.LinkDowned);

		printf("%s,RcvErrors_sum,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.RcvErrors);
		printf("%s,RcvErrors_sum_counter,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.RcvErrors);

		printf("%s,RcvRemotePhysErrors_sum,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.RcvRemotePhysErrors);
		printf("%s,RcvRemotePhysErrors_sum_counter,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.RcvRemotePhysErrors);

		printf("%s,RcvSwitchRelayErrors_sum,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.RcvSwitchRelayErrors);
		printf("%s,RcvSwitchRelayErrors_sum_counter,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.RcvSwitchRelayErrors);

		printf("%s,XmtDiscards_sum,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.XmtDiscards);
		printf("%s,XmtDiscards_sum_counter,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.XmtDiscards);

		printf("%s,XmtConstraintErrors_sum,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.XmtConstraintErrors);
		printf("%s,XmtConstraintErrors_sum_counter,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.XmtConstraintErrors);

		printf("%s,RcvConstraintErrors_sum,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.RcvConstraintErrors);
		printf("%s,RcvConstraintErrors_sum_counter,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.RcvConstraintErrors);

		printf("%s,LinkIntegrityErrors_sum,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.LinkIntegrityErrors);
		printf("%s,LinkIntegrityErrors_sum_counter,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.LinkIntegrityErrors);

		printf("%s,ExcBufOverrunErrors_sum,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.ExcBufOverrunErrors);
		printf("%s,ExcBufOverrunErrors_sum_counter,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.ExcBufOverrunErrors);

		printf("%s,VL15Dropped_sum,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.VL15Dropped);
		printf("%s,VL15Dropped_sum_counter,%"PRIuLEAST64"\n",
			IB_SWITCH_TOTAL_NODE_NAME,
			switch_error_sum.VL15Dropped);
	}
genders_open_fail:
	genders_handle_destroy(genders_handle);
#endif

free_res:
	mysql_free_result(res);
	return (rc);
}

/** =========================================================================
 * These will print a row from the "report_*" functions.
 * If you change those queries you will have to change these functions.
 */
#define print_report_res(name, row) \
	printf("Details for \"%s\" 0x%" PRIxLEAST64 " port %s\n" \
		"     Errors\n" \
		"         SymbolErrors         : %" PRIuLEAST64 "\n" \
		"         LinkRecovers         : %" PRIuLEAST64 "\n" \
		"         LinkDowned           : %" PRIuLEAST64 "\n" \
		"         RcvErrors            : %" PRIuLEAST64 "\n" \
		"         RcvRemotePhysErrors  : %" PRIuLEAST64 "\n" \
		"         RcvSwitchRelayErrors : %" PRIuLEAST64 "\n" \
		"         XmtDiscards          : %" PRIuLEAST64 "\n" \
		"         XmtConstraintErrors  : %" PRIuLEAST64 "\n" \
		"         RcvConstraintErrors  : %" PRIuLEAST64 "\n" \
		"         LinkIntegrityErrors  : %" PRIuLEAST64 "\n" \
		"         ExcBufOverrunErrors  : %" PRIuLEAST64 "\n" \
		"         VL15Dropped          : %" PRIuLEAST64 "\n" \
		"     Data Counters\n" \
		"         XmtData              : %" PRIuLEAST64 "\n" \
		"         RcvData              : %" PRIuLEAST64 "\n" \
		"         XmtPkts              : %" PRIuLEAST64 "\n" \
		"         RcvPkts              : %" PRIuLEAST64 "\n" \
		"         UnicastXmitPkts      : %" PRIuLEAST64 "\n" \
		"         UnicastRcvPkts       : %" PRIuLEAST64 "\n" \
		"         MulticastXmitPkts    : %" PRIuLEAST64 "\n" \
		"         MulticastRcvPkts     : %" PRIuLEAST64 "\n" \
		, \
		name, \
		COL_TO_UINT64(14), \
		row[13], \
		COL_TO_UINT64(0), \
		COL_TO_UINT64(1), \
		COL_TO_UINT64(2), \
		COL_TO_UINT64(3), \
		COL_TO_UINT64(4), \
		COL_TO_UINT64(5), \
		COL_TO_UINT64(6), \
		COL_TO_UINT64(7), \
		COL_TO_UINT64(8), \
		COL_TO_UINT64(9), \
		COL_TO_UINT64(10), \
		COL_TO_UINT64(11), \
		COL_TO_UINT64(15), \
		COL_TO_UINT64(16), \
		COL_TO_UINT64(17), \
		COL_TO_UINT64(18), \
		COL_TO_UINT64(19), \
		COL_TO_UINT64(20), \
		COL_TO_UINT64(21), \
		COL_TO_UINT64(22))

#define print_report_line_res(node_name, row) \
	printf("%s,port%s-SymbolErrors,%" PRIuLEAST64 "\n" \
		"%s,port%s-LinkRecovers,%" PRIuLEAST64 "\n" \
		"%s,port%s-LinkDowned,%" PRIuLEAST64 "\n" \
		"%s,port%s-RcvErrors,%" PRIuLEAST64 "\n" \
		"%s,port%s-RcvRemotePhysErrors,%" PRIuLEAST64 "\n" \
		"%s,port%s-RcvSwitchRelayErrors,%" PRIuLEAST64 "\n" \
		"%s,port%s-XmtDiscards,%" PRIuLEAST64 "\n" \
		"%s,port%s-XmtConstraintErrors,%" PRIuLEAST64 "\n" \
		"%s,port%s-RcvConstraintErrors,%" PRIuLEAST64 "\n" \
		"%s,port%s-LinkIntegrityErrors,%" PRIuLEAST64 "\n" \
		"%s,port%s-ExcBufOverrunErrors,%" PRIuLEAST64 "\n" \
		"%s,port%s-VL15Dropped,%" PRIuLEAST64 "\n" \
		"%s,port%s-XmtData,%" PRIuLEAST64 "\n" \
		"%s,port%s-RcvData,%" PRIuLEAST64 "\n" \
		"%s,port%s-XmtPkts,%" PRIuLEAST64 "\n" \
		"%s,port%s-RcvPkts,%" PRIuLEAST64 "\n" \
		"%s,port%s-UnicastXmitPkts,%" PRIuLEAST64 "\n" \
		"%s,port%s-UnicastRcvPkts,%" PRIuLEAST64 "\n" \
		"%s,port%s-MulticastXmitPkts,%" PRIuLEAST64 "\n" \
		"%s,port%s-MulticastRcvPkts,%" PRIuLEAST64 "\n" \
		, \
		node_name, row[13], COL_TO_UINT64(0), \
		node_name, row[13], COL_TO_UINT64(1), \
		node_name, row[13], COL_TO_UINT64(2), \
		node_name, row[13], COL_TO_UINT64(3), \
		node_name, row[13], COL_TO_UINT64(4), \
		node_name, row[13], COL_TO_UINT64(5), \
		node_name, row[13], COL_TO_UINT64(6), \
		node_name, row[13], COL_TO_UINT64(7), \
		node_name, row[13], COL_TO_UINT64(8), \
		node_name, row[13], COL_TO_UINT64(9), \
		node_name, row[13], COL_TO_UINT64(10), \
		node_name, row[13], COL_TO_UINT64(11), \
		node_name, row[13], COL_TO_UINT64(15), \
		node_name, row[13], COL_TO_UINT64(16), \
		node_name, row[13], COL_TO_UINT64(17), \
		node_name, row[13], COL_TO_UINT64(18), \
		node_name, row[13], COL_TO_UINT64(19), \
		node_name, row[13], COL_TO_UINT64(20), \
		node_name, row[13], COL_TO_UINT64(21), \
		node_name, row[13], COL_TO_UINT64(22))

/** =========================================================================
 * Port <= 0 indicates "all" ports
 */
static int
report(mysql_connection_t *conn, char *node, int port)
{
	int rc = 0;
	unsigned int  num_fields;
	unsigned int  num_rows, i;
	MYSQL_RES    *res;
	MYSQL_ROW     row;
#define CLAUSE_SIZE (256)
	char          node_clause[CLAUSE_SIZE];
	char          port_clause[CLAUSE_SIZE];

	if (node) {
		/* check to see if the node is specified by name or guid */
		uint64_t guid = strtoull(node, NULL, 0);
		if (guid == 0)
			guid = get_guid_for_name(node_name_map, node);
		if (guid == 0) {
			snprintf(node_clause, CLAUSE_SIZE,
				" and n.name=\"%s\"", node);
		} else {
			snprintf(node_clause, CLAUSE_SIZE,
				" and n.guid=%"PRIuLEAST64, guid);
		}
	} else {
		node_clause[0] = '\0';
	}
	if (port > 0) {
		snprintf(port_clause, CLAUSE_SIZE, " and pe.port=%d", port);
	} else {
		port_clause[0] = '\0';
	}

	if (QUERY(conn,
			"select pe.SymbolErrors, pe.LinkRecovers,"
			" pe.LinkDowned, pe.RcvErrors, pe.RcvRemotePhysErrors,"
			" pe.RcvSwitchRelayErrors, pe.XmtDiscards,"
			" pe.XmtConstraintErrors, pe.RcvConstraintErrors,"
			" pe.LinkIntegrityErrors, pe.ExcBufOverrunErrors,"
			" pe.VL15Dropped, n.name, pe.port, n.guid,"
			" pd.xmit_data, pd.rcv_data, pd.xmit_pkts, pd.rcv_pkts,"
			" pd.unicast_xmit_pkts, pd.unicast_rcv_pkts,"
			" pd.multicast_xmit_pkts, pd.multicast_rcv_pkts, "
			" pd.port "
			" from "
			" port_errors as pe,nodes as n,port_data_counters as pd "
			" where"
			" n.guid=pe.guid and n.guid=pd.guid and pe.port=pd.port %s %s;",
			node_clause,
			port_clause)) {
		fprintf(stderr, "Failed to query node errors");
		if (node) {
			fprintf(stderr, " for \"%s\"", node);
		} else {
			fprintf(stderr, "\n");
		}
		return (1);
	}

	res = mysql_store_result(conn->conn);

	if ((num_fields = mysql_num_fields(res)) != 24) {
		fprintf(stderr, "%s:%d Failed to query node errors %d != 24\n",
			__FUNCTION__, __LINE__, num_fields);
		rc = 1;
		goto free_res;
	}

	num_rows = mysql_num_rows(res);

	if (num_rows == 0) {
		if (node) {
			if (port <= 0) {
				fprintf(stderr, "Node \"%s\" (port %s) not found in DB\n",
					node, "\"all\"");
			} else {
				fprintf(stderr, "Node \"%s\" (port %d) not found in DB\n",
					node, port);
			}
		} else {
			fprintf(stderr, "Failed to find any nodes in DB\n");
		}
		rc = 1;
		goto free_res;
	}

	for (i = 0; i < num_rows; i++) {
		char node_desc[65];
		char *node_name = NULL;

		row = mysql_fetch_row(res);

		strncpy(node_desc, row[12], 64);
		node_name = remap_node_name(node_name_map, COL_TO_UINT64(14), node_desc);
		if (skummee_output) {
			clean_node_name(node_name);
			print_report_line_res(node_name, row);
		} else
			print_report_res(node_name, row);
		free(node_name);
	}

free_res:
	mysql_free_result(res);
	return (rc);
}

/** =========================================================================
 * This will print a row from the "report_error_*" functions.
 * If you change those queries you will have to change this function.
 */
#define print_report_error_res(name, row, total) \
	printf("Details for \"%s\" 0x%" PRIxLEAST64 " port %s\n" \
		"     Total unsupressed errors : %" PRIuLEAST64 "\n" \
		"         SymbolErrors         : %" PRIuLEAST64 "\n" \
		"         LinkRecovers         : %" PRIuLEAST64 "\n" \
		"         LinkDowned           : %" PRIuLEAST64 "\n" \
		"         RcvErrors            : %" PRIuLEAST64 "\n" \
		"         RcvRemotePhysErrors  : %" PRIuLEAST64 "\n" \
		"         RcvSwitchRelayErrors : %" PRIuLEAST64 "\n" \
		"         XmtDiscards          : %" PRIuLEAST64 "\n" \
		"         XmtConstraintErrors  : %" PRIuLEAST64 "\n" \
		"         RcvConstraintErrors  : %" PRIuLEAST64 "\n" \
		"         LinkIntegrityErrors  : %" PRIuLEAST64 "\n" \
		"         ExcBufOverrunErrors  : %" PRIuLEAST64 "\n" \
		"         VL15Dropped          : %" PRIuLEAST64 "\n" \
		, \
		name, \
		COL_TO_UINT64(14), \
		row[13], \
		total, \
		COL_TO_UINT64(0), \
		COL_TO_UINT64(1), \
		COL_TO_UINT64(2), \
		COL_TO_UINT64(3), \
		COL_TO_UINT64(4), \
		COL_TO_UINT64(5), \
		COL_TO_UINT64(6), \
		COL_TO_UINT64(7), \
		COL_TO_UINT64(8), \
		COL_TO_UINT64(9), \
		COL_TO_UINT64(10), \
		COL_TO_UINT64(11))

/** =========================================================================
*/
typedef struct mask_val {
	char *str;
	struct mask_val *next;
} mask_val_t;
static mask_val_t *
parse_mask(char *mask_str)
{
	mask_val_t *ret = NULL;
	mask_val_t *cur = NULL;
	char       *tmp = NULL;
	if (!mask_str) {
		return (ret);
	}

	printf("Suppressing errors for: ");

	ret = malloc(sizeof(*ret));
	ret->str = strtok(mask_str, ",\n\0");
	ret->next = NULL;
	cur = ret;

	printf("%s ", cur->str);

	while ((tmp = strtok(NULL, ",\n\0"))) {
		cur->next = malloc(sizeof(*ret));
		cur->next->str = tmp;
		cur->next->next = NULL;
		cur = cur->next;
		printf("%s ", cur->str);
	}

	printf("\n");

	return (ret);
}

/** =========================================================================
*/
static uint64_t
check_mask(char *var_str, uint64_t var, mask_val_t *vals)
{
	mask_val_t *val = vals;
	while (val) {
		if (!strcmp(val->str, var_str)) {
			return (0);
		}
		val = val->next;
	}
	return (var);
}
#define CHECK_MASK(var, mask_val) check_mask(#var, var, mask_val);

/** =========================================================================
*/
static int
report_error(mysql_connection_t *conn, char *mask_str)
{
	int rc = 0;
	unsigned int  num_fields;
	unsigned int  num_rows, i;
	MYSQL_RES    *res;
	MYSQL_ROW     row;
	mask_val_t   *mask_vals = parse_mask(mask_str);

	if (QUERY(conn,
			"select pe.SymbolErrors, pe.LinkRecovers,"
			" pe.LinkDowned, pe.RcvErrors, pe.RcvRemotePhysErrors,"
			" pe.RcvSwitchRelayErrors, pe.XmtDiscards,"
			" pe.XmtConstraintErrors, pe.RcvConstraintErrors,"
			" pe.LinkIntegrityErrors, pe.ExcBufOverrunErrors,"
			" pe.VL15Dropped, n.name, pe.port, n.guid,"
			" pd.xmit_data, pd.rcv_data, pd.xmit_pkts, pd.rcv_pkts,"
			" pd.unicast_xmit_pkts, pd.unicast_rcv_pkts,"
			" pd.multicast_xmit_pkts, pd.multicast_rcv_pkts, "
			" pd.port "
			" from "
			" port_errors as pe,nodes as n,port_data_counters as pd "
			" where"
			" n.guid=pe.guid and n.guid=pd.guid and pe.port=pd.port;"
			)) {
		fprintf(stderr, "Failed to query node errors\n");
		return (1);
	}

	res = mysql_store_result(conn->conn);

	if ((num_fields = mysql_num_fields(res)) != 24) {
		fprintf(stderr, "%s:%d Failed to query node errors %d != 24\n",
			__FUNCTION__, __LINE__, num_fields);
		rc = 1;
		goto free_res;
	}

	num_rows = mysql_num_rows(res);

	if (num_rows == 0) {
		fprintf(stderr, "Failed to find any nodes in DB\n");
		rc = 1;
		goto free_res;
	}

	for (i = 0; i < num_rows; i++) {
		uint64_t SymbolErrors;
		uint64_t LinkRecovers;
		uint64_t LinkDowned;
		uint64_t RcvErrors;
		uint64_t RcvRemotePhysErrors;
		uint64_t RcvSwitchRelayErrors;
		uint64_t XmtDiscards;
		uint64_t XmtConstraintErrors;
		uint64_t RcvConstraintErrors;
		uint64_t LinkIntegrityErrors;
		uint64_t ExcBufOverrunErrors;
		uint64_t VL15Dropped;
		uint64_t total = 0;

		row = mysql_fetch_row(res);

		SymbolErrors = COL_TO_UINT64(0);
		total += CHECK_MASK(SymbolErrors, mask_vals);
		LinkRecovers = COL_TO_UINT64(1);
		total += CHECK_MASK(LinkRecovers, mask_vals);
		LinkDowned = COL_TO_UINT64(2);
		total += CHECK_MASK(LinkDowned, mask_vals);
		RcvErrors = COL_TO_UINT64(3);
		total += CHECK_MASK(RcvErrors, mask_vals);
		RcvRemotePhysErrors = COL_TO_UINT64(4);
		total += CHECK_MASK(RcvRemotePhysErrors, mask_vals);
		RcvSwitchRelayErrors = COL_TO_UINT64(5);
		total += CHECK_MASK(RcvSwitchRelayErrors, mask_vals);
		XmtDiscards = COL_TO_UINT64(6);
		total += CHECK_MASK(XmtDiscards, mask_vals);
		XmtConstraintErrors = COL_TO_UINT64(7);
		total += CHECK_MASK(XmtConstraintErrors, mask_vals);
		RcvConstraintErrors = COL_TO_UINT64(8);
		total += CHECK_MASK(RcvConstraintErrors, mask_vals);
		LinkIntegrityErrors = COL_TO_UINT64(9);
		total += CHECK_MASK(LinkIntegrityErrors, mask_vals);
		ExcBufOverrunErrors = COL_TO_UINT64(10);
		total += CHECK_MASK(ExcBufOverrunErrors, mask_vals);
		VL15Dropped = COL_TO_UINT64(11);
		total += CHECK_MASK(VL15Dropped, mask_vals);

		if (total > 0) {
			char node_desc[65];
			char *node_name = NULL;
			strncpy(node_desc, row[12], 64);
			node_name = remap_node_name(node_name_map, COL_TO_UINT64(14), node_desc);
			print_report_error_res(node_name, row, total);
			free(node_name);
		}
	}

free_res:
	mysql_free_result(res);
	return (rc);
}

/** =========================================================================
 */
static int
clear_node(mysql_connection_t *conn, char *node)
{
	if (QUERY(conn,
			"update port_errors as pe, nodes as n set "
			"pe.SymbolErrors=0,pe.LinkRecovers=0,pe.LinkDowned=0,pe.RcvErrors=0,"
			"pe.RcvRemotePhysErrors=0,pe.RcvSwitchRelayErrors=0,pe.XmtDiscards=0,"
			"pe.XmtConstraintErrors=0,pe.RcvConstraintErrors=0,pe.LinkIntegrityErrors=0,"
			"pe.ExcBufOverrunErrors=0,pe.VL15Dropped=0 "
			"where pe.guid=n.guid and n.name=\"%s\""
			";",
			node)) {
		fprintf(stderr, "Failed to clear error for node \"%s\"\n", node);
		return (1);
	}
	printf("Success: Errors cleared for \"%s\"\n", node);
	return (0);
}

/** =========================================================================
 */
static int
clear_all(mysql_connection_t *conn)
{
	if (QUERY(conn,
			"update port_errors as pe set "
			"pe.SymbolErrors=0,pe.LinkRecovers=0,pe.LinkDowned=0,pe.RcvErrors=0,"
			"pe.RcvRemotePhysErrors=0,pe.RcvSwitchRelayErrors=0,pe.XmtDiscards=0,"
			"pe.XmtConstraintErrors=0,pe.RcvConstraintErrors=0,pe.LinkIntegrityErrors=0,"
			"pe.ExcBufOverrunErrors=0,pe.VL15Dropped=0 "
			";")) {
		fprintf(stderr, "Failed to clear errors\n");
		return (1);
	}
	printf("Success: Errors cleared for all ports\n");
	return (0);
}

static void
sql_setup_db_conn(mysql_connection_t *conn)
{
	/* open DB connection */
	if (!mysql_init(&(conn->mysql))) {
		fprintf(stderr, "Failed to Connect to MySQL\n");
		exit (1);
	}
	if (!(conn->conn = mysql_real_connect(&(conn->mysql), NULL,
			conn->db_user, conn->db_password,
			conn->db_name, 0, NULL, 0))) {
		fprintf(stderr, "Failed to open database \"%s\" : %s\n",
			conn->db_name, mysql_error(&(conn->mysql)));
		mysql_close(&(conn->mysql));
		exit (1);
	}
}

/** =========================================================================
 */
static void
construct_mysql_connection(mysql_connection_t *data)
{
	FILE *fp = NULL;
	char  line[1024];
	char *last = NULL;
	char *key = NULL;
	char *val = NULL;

	data->db_user = DEF_DATABASE_USER;
	data->db_name = DEF_DATABASE_NAME;

	snprintf(line, 1023, "%s/%s", CONF_FILE_PREFIX, DATABASE_CONF);
	if (!(fp = fopen(line, "r"))) {
		fprintf(stderr, "Failed to open DB conf file : %s/%s\n",
			CONF_FILE_PREFIX, DATABASE_CONF);
		exit (1);
	}

	while (fgets(line, 1023, fp) != NULL) {
		/* get the first token */
		key = strtok_r(line, " \t\n", &last);
		if (!key)
			continue;
		val = strtok_r(NULL, " \t\n", &last);

		if (!strcmp("DB_USER", key) && val) {
			data->db_user = strdup(val);
		}
		if (!strcmp("DB_NAME", key) && val) {
			data->db_name = strdup(val);
		}
		if (!strcmp("DB_PASSWORD", key) && val) {
			data->db_password = strdup(val);
		}
	}
	fclose(fp);
}

/** =========================================================================
 */
static void
usage(void)
{
	fprintf(stderr, "Usage: %s [-hCsRe] [-c <node>] [-r <node>]\n"
	                "   Query libopensmskummeeplugin DB and print data to stdout\n"
	                "   By default print a sum of errors for each port in a SKUMMEE compatible format\n"
	                "\n"
			"Options\n"
	                "   -h this help message\n"
	                "   -C Clear all the error counters from the DB\n"
	                "   -c <node> Clear the error counters for node <node>\n"
	                "   -r <node> Report error details for node <node>\n"
	                "   -R Report error details for all nodes\n"
	                "   -S SKUMMEE output\n"
			"      change -R and -r output to be SKUMMEE compatible\n"
	                "   -e Report port counters which have errors\n"
	                "   -E <var1,var2,...> Report port counters which have errors\n"
	                "      ignorning the vars specified\n"
	                "   -p <port> limit error reporting to port <port>\n"
	                "   -s print the SQL queries as they are submitted\n"
	                "   --node-name-map <map_file> use the node name map specified\n"
		, argv0);
#if HAVE_LIBGENDERS
	fprintf(stderr, "   -f sum all \"none node\" errors into an IB_Fabric,error_sum,<#> entry\n");
#endif
}

/** =========================================================================
 */
enum {
	QUERY_ALL,
	CLEAR_ALL,
	CLEAR_NODE,
	REPORT_NODE,
	REPORT_ALL,
	REPORT_ERROR
} mode = QUERY_ALL;
int
main(int argc, char **argv)
{
	int                 rc = 0;
	int                 ch = 0;
	mysql_connection_t  conn;
	char               *node = NULL;
	char               *mask_str = NULL;
	int                 port = -1;
#if HAVE_LIBGENDERS
	static char const   str_opts[] = "hCc:sr:Rp:eE:Sf";
#else
	static char const   str_opts[] = "hCc:sr:Rp:eE:S";
#endif
	static const struct option long_opts [] = {
	   {"help", 0, 0, 'h'},
	   {"clear-all", 0, 0, 'C'},
	   {"clear", 1, 0, 'c'},
	   {"report", 1, 0, 'r'},
	   {"report-all", 0, 0, 'R'},
	   {"report-error", 0, 0, 'e'},
	   {"report-error-mask", 1, 0, 'E'},
	   {"port", 1, 0, 'p'},
	   {"show-queries", 0, 0, 's'},
	   {"node-name-map", 1, 0, 1},
	   {"line-mode", 0, 0, 'l'},
#if HAVE_LIBGENDERS
	   {"sum-fabric-errors", 0, 0, 'f'},
#endif
	   { }
	};

	argv0 = argv[0];

	while ((ch = getopt_long(argc, argv, str_opts, long_opts, NULL)) != -1) {
		switch (ch) {
		case 1:
			node_name_map_file = strdup(optarg);
			break;
		case 'C':
			printf("clearing all\n");
			mode = CLEAR_ALL;
			break;
		case 'c':
			printf("clearing node %s\n", optarg);
			node = strdup(optarg);
			mode = CLEAR_NODE;
			break;
		case 'r':
			node = strdup(optarg);
			mode = REPORT_NODE;
			break;
		case 'R':
			mode = REPORT_ALL;
			break;
		case 'e':
			mode = REPORT_ERROR;
			break;
		case 'E':
			mode = REPORT_ERROR;
			mask_str = strdup(optarg);
			break;
		case 'p':
			port = atoi(optarg);
			if (port <= 0) {
				fprintf(stderr,
					"Invalid port value specified : \"%d\"\n",
					port);
				usage();
				exit(1);
			}
			if (mode == QUERY_ALL) { mode = REPORT_ALL; }
			break;
		case 's':
			show_queries = 1;
			break;
		case 'S':
			skummee_output = 1;
			break;
#if HAVE_LIBGENDERS
		case 'f':
			sum_fabric_errors = 1;
			break;
#endif
		case 'h':
		default:
			usage();
			exit(0);
		}
	}

	node_name_map = open_node_name_map(node_name_map_file);
	
	/* work */
	construct_mysql_connection(&conn);
	sql_setup_db_conn(&conn);

	switch (mode) {
		case CLEAR_NODE:
			rc = clear_node(&conn, node);
			break;
		case REPORT_NODE:
			rc = report(&conn, node, port);
			break;
		case REPORT_ALL:
			rc = report(&conn, NULL, port);
			break;
		case REPORT_ERROR:
			rc = report_error(&conn, mask_str);
			break;
		case CLEAR_ALL:
			rc = clear_all(&conn);
			break;
		default:
		case QUERY_ALL:
			rc = query_status(&conn);
			break;
	}

	mysql_close(&(conn.mysql));
	close_node_name_map(node_name_map);

	return (rc);
}

