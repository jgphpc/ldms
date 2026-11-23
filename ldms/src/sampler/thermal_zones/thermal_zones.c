/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2026 CSCS - Swiss National Supercomputing Centre
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of the copyright holder nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <linux/limits.h>
#include "../sampler_base.h"
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"

#define SAMP "thermal_zones"
#define ZONE_PREFIX "thermal_zone"
#define ZONE_TYPE_SZ 32
#define MAX_TRIP_POINTS 16

/*
 * One Linux thermal zone, i.e. /sys/class/thermal/thermal_zone<N>. The
 * zones are discovered once, when the 'config' action runs, and each
 * zone is represented by a record in the 'zone_list' list metric.
 */
struct zone {
	int zone_no;		/* N in thermal_zoneN */
	char type[ZONE_TYPE_SZ]; /* content of thermal_zoneN/type */
	char *temp_path;	/* thermal_zoneN/temp */
	FILE *temp_fp;
	float temp;		/* thermal_zoneN/temp in C */
	float trip_temp;	/* highest trip point temp in C, -1 if none */
	ldms_mval_t zone_v;	/* this zone's record value */
};

struct ldms_metric_template_s zone_template[] = {
	{ "zone_no", LDMS_MDESC_F_DATA, LDMS_V_U32, "", 1 },
	{ "type", LDMS_MDESC_F_DATA, LDMS_V_CHAR_ARRAY, "", ZONE_TYPE_SZ },
	{ "temp", LDMS_MDESC_F_DATA, LDMS_V_F32, "C", 1 },
	{ "trip_temp", LDMS_MDESC_F_DATA, LDMS_V_F32, "C", 1 },
	{}
};
static int zone_metric_ids[sizeof(zone_template) / sizeof(zone_template[0])];
#define ZONE_NO_IDX	0
#define TYPE_IDX	1
#define TEMP_IDX	2
#define TRIP_TEMP_IDX	3

static struct zone *zones;
static int zone_count;
static int zone_list_idx;
static int zone_rec_idx;

static ldms_set_t set;
static ovis_log_t mylog;
static base_data_t base;

static int read_line(const char *path, char *buf, size_t buf_sz)
{
	FILE *fp;
	size_t rc;
	char *nl;

	fp = fopen(path, "r");
	if (!fp)
		return -1;
	rc = fread(buf, 1, buf_sz - 1, fp);
	fclose(fp);
	if (rc == 0)
		return -1;
	buf[rc] = '\0';
	nl = strchr(buf, '\n');
	if (nl)
		*nl = '\0';
	return 0;
}

static int read_long(const char *path, long *out)
{
	char buf[64];

	if (read_line(path, buf, sizeof(buf)))
		return -1;
	*out = strtol(buf, NULL, 0);
	return 0;
}

/* Accept names of the form thermal_zone<N> */
static int zone_filter(const struct dirent *d)
{
	const char *p = d->d_name;

	if (strncmp(p, ZONE_PREFIX, sizeof(ZONE_PREFIX) - 1))
		return 0;
	p += sizeof(ZONE_PREFIX) - 1;
	if (!*p)
		return 0;
	for (; *p != '\0'; p++) {
		if (!isdigit(*p))
			return 0;
	}
	return 1;
}

/* Sort zones numerically: thermal_zone2 before thermal_zone10 */
static int zone_sort(const struct dirent **a, const struct dirent **b)
{
	int na = atoi((*a)->d_name + sizeof(ZONE_PREFIX) - 1);
	int nb = atoi((*b)->d_name + sizeof(ZONE_PREFIX) - 1);
	return na - nb;
}

static int discover_zones(void)
{
	struct dirent **namelist = NULL;
	char path[PATH_MAX];
	int n, i, j;
	int rc = 0;
	long v;

	n = scandir("/sys/class/thermal", &namelist, zone_filter, zone_sort);
	if (n < 0)
		return errno;
	if (n == 0) {
		rc = ENOENT;
		goto out;
	}

	zones = calloc(n, sizeof(*zones));
	if (!zones) {
		rc = ENOMEM;
		goto out;
	}
	zone_count = n;

	for (i = 0; i < n; i++) {
		struct zone *z = &zones[i];

		z->zone_no = atoi(namelist[i]->d_name + sizeof(ZONE_PREFIX) - 1);
		z->trip_temp = -1.0;

		snprintf(path, sizeof(path), "/sys/class/thermal/%s/temp",
			 namelist[i]->d_name);
		z->temp_path = strdup(path);

		snprintf(path, sizeof(path), "/sys/class/thermal/%s/type",
			 namelist[i]->d_name);
		if (read_line(path, z->type, sizeof(z->type)))
			snprintf(z->type, sizeof(z->type), "unknown");

		/* The highest trip point temperature, if the zone has any. */
		for (j = 0; j < MAX_TRIP_POINTS; j++) {
			snprintf(path, sizeof(path),
				 "/sys/class/thermal/%s/trip_point_%d_temp",
				 namelist[i]->d_name, j);
			if (read_long(path, &v))
				break;
			if (v / 1000.0 > z->trip_temp)
				z->trip_temp = v / 1000.0;
		}
	}
 out:
	if (namelist) {
		for (i = 0; i < n; i++)
			free(namelist[i]);
		free(namelist);
	}
	return rc;
}

static int create_metric_set(base_data_t base)
{
	int rc;
	ldms_schema_t schema;
	int i;

	rc = discover_zones();
	if (rc) {
		ovis_log(mylog, OVIS_LERROR,
			 "No thermal zones found under /sys/class/thermal, errno=%d.\n",
			 rc);
		goto err;
	}

	/* Create a metric set of the required size */
	schema = base_schema_new(base);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR,
		       "The schema '%s' could not be created, errno=%d.\n",
		       base->schema_name, errno);
		rc = EINVAL;
		goto err;
	}

	ldms_record_t zone_def =
		ldms_record_from_template("zone_temps",
					  zone_template, zone_metric_ids);

	/* Add record definition into the schema */
	zone_rec_idx = ldms_schema_record_add(schema, zone_def);
	if (zone_rec_idx < 0) {
		rc = -zone_rec_idx;
		goto err;
	}

	/* Add a list of thermal zones */
	zone_list_idx = ldms_schema_metric_list_add(schema, "zone_list", NULL,
			zone_count * ldms_record_heap_size_get(zone_def));
	if (zone_list_idx < 0) {
		rc = -zone_list_idx;
		goto err;
	}

	set = base_set_new(base);
	if (!set) {
		ovis_log(mylog, OVIS_LERROR, "Error %d creating the metric set.\n", errno);
		rc = errno;
		goto err;
	}

	/* Populate the list with a record for each thermal zone */
	ldms_mval_t zone_list_v;
	zone_list_v = ldms_metric_get(set, zone_list_idx);
	for (i = 0; i < zone_count; i++) {
		struct zone *z = &zones[i];
		long v;

		if (!read_long(z->temp_path, &v))
			z->temp = v / 1000.0;

		z->zone_v = ldms_record_alloc(set, zone_rec_idx);
		ldms_record_set_u32(z->zone_v, ZONE_NO_IDX, z->zone_no);
		ldms_record_array_set_str(z->zone_v, TYPE_IDX, z->type);
		ldms_record_set_float(z->zone_v, TEMP_IDX, z->temp);
		ldms_record_set_float(z->zone_v, TRIP_TEMP_IDX, z->trip_temp);
		rc = ldms_list_append_record(set, zone_list_v, z->zone_v);
		if (rc) {
			ovis_log(mylog, OVIS_LERROR,
				 "Failed to append the record for thermal_zone%d.\n",
				 z->zone_no);
			goto err;
		}
	}
 err:
	return rc;
}


static const char *usage(ldmsd_plug_handle_t handle)
{
	return "config name=" SAMP BASE_CONFIG_USAGE;
}

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc;

	if (set) {
		ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
		return EINVAL;
	}

	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, mylog);
	if (!base) {
		rc = EINVAL;
		goto err;
	}

	rc = create_metric_set(base);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "failed to create a metric set.\n");
		goto err;
	}

	return 0;

 err:
	base_del(base);
	base = NULL;
	return rc;

}

static int sample(ldmsd_plug_handle_t handle)
{
	int i;

	if (!set){
		ovis_log(mylog, OVIS_LDEBUG, "plugin not initialized\n");
		return EINVAL;
	}

	base_sample_begin(base);

	/* Update the zone temperatures */
	for (i = 0; i < zone_count; i++) {
		struct zone *z = &zones[i];
		char fbuf[64];
		size_t rc;

		if (!z->temp_fp)
			z->temp_fp = fopen(z->temp_path, "r");
		else
			fseek(z->temp_fp, 0, SEEK_SET);
		if (!z->temp_fp)
			continue;
		rc = fread(fbuf, 1, sizeof(fbuf) - 1, z->temp_fp);
		if (rc == 0)
			continue;
		fbuf[rc] = '\0';
		z->temp = strtol(fbuf, NULL, 0) / 1000.0;
		ldms_record_set_float(z->zone_v, TEMP_IDX, z->temp);
	}

	base_sample_end(base);
	return 0;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);
	set = NULL;

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	int i;

	/* Clean up the zones */
	for (i = 0; i < zone_count; i++) {
		free(zones[i].temp_path);
		if (zones[i].temp_fp)
			fclose(zones[i].temp_fp);
	}
	free(zones);
	zones = NULL;
	zone_count = 0;
	if (base)
		base_del(base);
	base = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.type = LDMSD_PLUGIN_SAMPLER,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};
