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

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <dlfcn.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#include <mysql/mysql.h>
#include <mysql/mysqld_error.h>

#include "opensmskummeeplugin.h"
#include "sql_queries.h"

/** =========================================================================
 * Called when new data comes in
 * This adds data to the FIFO
 */
static void
add_to_head(plugin_data_t *data, data_entry_t *entry)
{
	pthread_mutex_lock(&(data->lock));

	entry->next = data->head;
	entry->prev = NULL;

	if (data->head)
		data->head->prev = entry;

	data->head = entry;

	if (data->tail == NULL) {
		data->tail = entry;
	}
	pthread_mutex_unlock(&(data->lock));
}

/** =========================================================================
 * Remove and return the tail of the data.
 */
static data_entry_t *
pull_from_tail(plugin_data_t *data)
{
	data_entry_t *rc = NULL;
	pthread_mutex_lock(&(data->lock));
	if (!data->tail)
		goto out;

	if (data->tail->prev)
		data->tail->prev->next = NULL;

	rc = data->tail;
	data->tail = data->tail->prev;
	rc->prev = NULL;

	/* check if last element */
	if (rc == data->head)
		data->head = NULL;

out:
	pthread_mutex_unlock(&(data->lock));
	return (rc);
}

inline void
thread_signal(plugin_data_t *plugin_data)
{
	pthread_mutex_lock(&(plugin_data->sig_lock));
	pthread_cond_signal(&(plugin_data->signal));
	pthread_mutex_unlock(&(plugin_data->sig_lock));
}

/** =========================================================================
 * This is the main worker thread.  It looks for information comming in the
 * FIFO and submits the data to the database.  This ensures that the PerfMgr
 * (and therefore OpenSM threads) will not be delayed by DB activity.
 */
void *
db_write_thread(void *pd)
{
	plugin_data_t *plugin_data = (plugin_data_t *)pd;
	data_entry_t  *data = NULL;

	plugin_log(plugin_data->osmlog, OSM_LOG_INFO,
			"In DB write thread\n");

	plugin_data->exit_flag = 0;
	if (sql_setup_db_conn(plugin_data)) {
		thread_signal(plugin_data);
		return (NULL);
	}

	thread_signal(plugin_data);

	while (1) {
		if (plugin_data->exit_flag) {
			return (NULL);
		}

		if (!plugin_data->tail) {
			struct timespec ts;
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += plugin_data->thread_sleep_s;
			pthread_mutex_lock(&(plugin_data->sig_lock));
			pthread_cond_timedwait(&(plugin_data->signal), &(plugin_data->sig_lock), &ts);
			pthread_mutex_unlock(&(plugin_data->sig_lock));
			continue;
		}

		data = pull_from_tail(plugin_data);

		if (!data) {
			plugin_log(plugin_data->osmlog, OSM_LOG_ERROR,
				"plugin_data->tail != NULL but pull returned NULL?");
			continue;
		}

		switch(data->type)
		{
			case OSM_EVENT_ID_PORT_ERRORS:
				sql_add_port_errors(plugin_data, &(data->data.pe_event));
				break;
			case OSM_EVENT_ID_PORT_DATA_COUNTERS:
				sql_add_data_counters(plugin_data, &(data->data.dc_event));
				break;
			case OSM_EVENT_ID_PORT_SELECT:
				sql_add_port_select(plugin_data, &(data->data.ps_event));
				break;
			case OSM_EVENT_ID_TRAP:
				break;
			case OSM_EVENT_ID_MAX: break;
		}

		free(data);
	}
}

/** =========================================================================
 */
static inline int
construct_plugin_data(plugin_data_t *data)
{
	FILE *fp = NULL;
	char  line[1024];
	char *last = NULL;
	char *key = NULL;
	char *val = NULL;

	if (pthread_mutex_init(&(data->lock), NULL)
			|| pthread_mutex_init(&(data->sig_lock), NULL)
			|| pthread_cond_init(&(data->signal), NULL)) {
		return (0);
	}

	data->db_user = DEF_DATABASE_USER;
	data->db_name = DEF_DATABASE_NAME;
	data->clear_db_on_load = 0;
	data->thread_sleep_s = DEF_THREAD_SLEEP;

	snprintf(line, 1023, "%s/%s", CONF_FILE_PREFIX, DATABASE_CONF);
	if (!(fp = fopen(line, "r"))) {
		return (0);
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
		if (!strcmp("CLEAR_DB_ON_LOAD", key) && val) {
			data->clear_db_on_load = strcmp("FALSE", val);
		}
		if (!strcmp("THREAD_SLEEP_S", key) && val) {
			data->thread_sleep_s = abs((int)(strtol(val, NULL, 0)));
		}
	}
	fclose(fp);

	return (1);
}

static void
free_plugin_data(plugin_data_t *data)
{
	free(data->db_user);
	free(data->db_name);
	free(data->db_password);
	free(data);
}

/** =========================================================================
 */
static void *
create(struct osm_opensm *osm)
{
	struct timespec  ts;
	plugin_data_t   *plugin_data = NULL;
	pthread_attr_t   th_attr;
	int              rc = 0;

	if (!(plugin_data = malloc(sizeof(*plugin_data))))
		return (NULL);

	plugin_data->osmlog = &(osm->log);

	if (!construct_plugin_data(plugin_data)) {
		plugin_log(plugin_data->osmlog, OSM_LOG_ERROR, "Failed to read %s\n", DATABASE_CONF);
		free(plugin_data);
		return (NULL);
	}

	if (pthread_attr_init(&(th_attr))) {
		free_plugin_data(plugin_data);
		return (NULL);
	}
	pthread_mutex_lock(&(plugin_data->sig_lock));
	if (pthread_create(&(plugin_data->thread), &(th_attr),
              		db_write_thread, (void *)plugin_data)) {
		plugin_log(plugin_data->osmlog, OSM_LOG_INFO, "Failed to create DB write thread\n");
		pthread_attr_destroy(&(th_attr));
		free_plugin_data(plugin_data);
		return (NULL);
	}
	pthread_attr_destroy(&(th_attr));

	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += 2; /* give 2 sec to start up */
	rc = pthread_cond_timedwait(&(plugin_data->signal), &(plugin_data->sig_lock), &ts);
	pthread_mutex_unlock(&(plugin_data->sig_lock));
	if (rc == ETIMEDOUT) {
		plugin_log(plugin_data->osmlog, OSM_LOG_ERROR, "DB write thread failed to initialize\n");
		pthread_join(plugin_data->thread, NULL);
		free_plugin_data(plugin_data);
		return (NULL);
	}

	plugin_log(plugin_data->osmlog, OSM_LOG_INFO, "DB write thread started\n");

	return ((void *)plugin_data);
}

/** =========================================================================
 */
static void
delete(void *data)
{
	plugin_data_t *plugin_data = (plugin_data_t *)data;

	plugin_log(plugin_data->osmlog, OSM_LOG_INFO, "destroy called cleaning up...\n");

	plugin_data->exit_flag = 1;
	thread_signal(plugin_data);
	plugin_log(plugin_data->osmlog, OSM_LOG_INFO, "Stopping DB write thread...\n");
	pthread_join(plugin_data->thread, NULL);
	plugin_log(plugin_data->osmlog, OSM_LOG_INFO, "Closing DB connection...\n");
	mysql_close(&(plugin_data->mysql));
	free_plugin_data(plugin_data);
}

/** =========================================================================
 * All this function does is copy the data into our FIFO for later processing
 * by the worker thread.
 */
static void report(void *data, osm_epi_event_id_t event_id, void *event_data)
{
	plugin_data_t *plugin_data = (plugin_data_t *)data;
	data_entry_t *entry = (data_entry_t *)malloc(sizeof *entry);

	if (!entry) {
		plugin_log(plugin_data->osmlog, OSM_LOG_ERROR,
			"Reporting event \"%d\" : malloc failed : %s\n",
				event_id, strerror(errno));
		return;
	}

	entry->type = event_id;

	switch (event_id) {
	case OSM_EVENT_ID_PORT_ERRORS:
		memcpy (&(entry->data.pe_event), event_data, sizeof(entry->data.pe_event));
		break;
	case OSM_EVENT_ID_PORT_DATA_COUNTERS:
		memcpy (&(entry->data.dc_event), event_data, sizeof(entry->data.dc_event));
		break;
	case OSM_EVENT_ID_PORT_SELECT:
		memcpy (&(entry->data.ps_event), event_data, sizeof(entry->data.ps_event));
		break;
	case OSM_EVENT_ID_TRAP:
		memcpy (&(entry->data.trap_event), event_data, sizeof(entry->data.trap_event));
		break;
	case OSM_EVENT_ID_MAX:
	default:
		plugin_log(plugin_data->osmlog, OSM_LOG_ERROR,
			"Unknown event reported to plugin\n");
		free(entry);
		return;
	}

	add_to_head(data, entry);
}

/** =========================================================================
 * Define the object symbol for loading
 */
osm_event_plugin_t osm_event_plugin = {
      osm_version:OSM_VERSION,
      create:create,
      delete:delete,
      report:report
};
