/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Rajesh Bhalerao <Rajesh_Bhalerao@xyratex.com>
 * Original creation date: 03/17/2011
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/limits.h>
#include "lib/ut.h"
#include "lib/ub.h"
#include "lib/assert.h"
#include "lib/processor.h"

#define	SYSFS_PATH		"/sys/devices/system"
#define	TEST1_SYSFS_PATH	"./test1"
#define	TEST2_SYSFS_PATH	"./test2"
#define	TEST3_SYSFS_PATH	"./test3"
#define	TEST4_SYSFS_PATH	"./test4"
#define	TEST5_SYSFS_PATH	"./test5"
#define	TEST6_SYSFS_PATH	"./test6"
#define	TEST7_SYSFS_PATH	"./test7"

#define MAX_PROCESSOR_FILE	"cpu/kernel_max"
#define POSS_PROCESSOR_FILE	"cpu/possible"
#define AVAIL_PROCESSOR_FILE	"cpu/present"
#define ONLN_PROCESSOR_FILE	"cpu/online"

#define NUMA_FILE1		"cpu/cpu%u/node%u"
#define NUMA_FILE2		"node/node%u/cpu%u"

#define COREID_FILE		"cpu/cpu%u/topology/core_id"
#define PHYSID_FILE		"cpu/cpu%u/topology/physical_package_id"

#define L1SZ_FILE		"cpu/cpu%u/cache/index0/size"
#define L2SZ_FILE1		"cpu/cpu%u/cache/index1/size"
#define L2SZ_FILE2		"cpu/cpu%u/cache/index2/size"

#define C0_LVL_FILE		"cpu/cpu%u/cache/index0/level"
#define C1_LVL_FILE		"cpu/cpu%u/cache/index1/level"
#define C2_LVL_FILE		"cpu/cpu%u/cache/index2/level"

#define C0_SHMAP_FILE		"cpu/cpu%u/cache/index0/shared_cpu_map"
#define C1_SHMAP_FILE		"cpu/cpu%u/cache/index1/shared_cpu_map"
#define C2_SHMAP_FILE		"cpu/cpu%u/cache/index2/shared_cpu_map"

#define	BUF_SZ	512
#define	SMALL_STR_SZ	32
#define	LARGE_STR_SZ	128

struct psummary {
	char *kmaxstr;
	char *possstr;
	char *presentstr;
	char *onlnstr;
};

struct pinfo {
	uint32_t numaid;
	uint32_t physid;
	uint32_t coreid;
	uint32_t c0lvl;
	uint32_t c1lvl;
	uint32_t c2lvl;
	const char *c0szstr;
	const char *c1szstr;
	const char *c2szstr;
	const char *c0sharedmapstr;
	const char *c1sharedmapstr;
	const char *c2sharedmapstr;
};

struct psummary test1_cpus_summary = {
	.kmaxstr = "32\n",
	.possstr = "0-1,7-8\n",
	.presentstr = "0-1,7\n",
	.onlnstr = "0-1\n"
};

struct pinfo test1_cpus[] = {
	{
		.numaid = 1,
		.physid = 0,
		.coreid = 0,
		.c0lvl = 1,
		.c1lvl = 1,
		.c2lvl = 2,
		.c0szstr = "64K\n",
		.c1szstr = "64K\n",
		.c2szstr = "540K\n",
		.c0sharedmapstr = "00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000001\n",
		.c1sharedmapstr = "00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000001\n",
		.c2sharedmapstr = "00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000003\n"
	},
	{
		.numaid = 1,
		.physid = 0,
		.coreid = 1,
		.c0lvl = 1,
		.c1lvl = 1,
		.c2lvl = 2,
		.c0szstr = "64K\n",
		.c1szstr = "64K\n",
		.c2szstr = "540K\n",
		.c0sharedmapstr = "00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000001\n",
		.c1sharedmapstr = "00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000001\n",
		.c2sharedmapstr = "00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000003\n"
	},
	{ .numaid = C2_PROCESSORS_INVALID_ID },
	{ .numaid = C2_PROCESSORS_INVALID_ID },
	{ .numaid = C2_PROCESSORS_INVALID_ID },
	{ .numaid = C2_PROCESSORS_INVALID_ID },
	{ .numaid = C2_PROCESSORS_INVALID_ID },
	{
		.numaid = 1,
		.physid = 3,
		.coreid = 0,
		.c0lvl = 1,
		.c1lvl = 1,
		.c2lvl = 2,
		.c0szstr = "64K\n",
		.c1szstr = "64K\n",
		.c2szstr = "540K\n",
		.c0sharedmapstr = "00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000001\n",
		.c1sharedmapstr = "00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000001\n",
		.c2sharedmapstr = "00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000001\n"
	}

};

struct psummary test3_cpus_summary = {
	.kmaxstr = "32\n",
};

struct psummary test4_cpus_summary = {
	.kmaxstr = "32\n",
	.possstr = "0-1,7-8\n",
};

struct psummary test5_cpus_summary = {
	.kmaxstr = "32\n",
	.possstr = "0-1,7-8\n",
	.presentstr = "0-1,7\n",
};

struct psummary test6_cpus_summary = {
	.kmaxstr = "32\n",
	.possstr = "0-1,7-8\n",
	.presentstr = "0-1,7\n",
	.onlnstr = "0-1\n"
};

struct psummary test7_cpus_summary = {
	.kmaxstr = "32\n",
	.possstr = "0-2,7-8\n",
	.presentstr = "0-2,7\n",
	.onlnstr = "0-2\n"
};

struct pinfo test7_cpus[] = {
	{
		.numaid = 1,
		.physid = 0,
		.coreid = 0,
		.c0lvl = 1,
		.c1lvl = 1,
		.c2lvl = 2,
		.c0szstr = "64K\n",
		.c1szstr = "64K\n",
		.c2szstr = "540K\n",
		.c0sharedmapstr = "00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000001\n",
		.c1sharedmapstr = "00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000001\n",
		.c2sharedmapstr = "00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000003\n"
	},
	{
		.numaid = 1,
		.physid = 0,
		.coreid = 1,
		.c0lvl = 1,
		.c1lvl = 1,
		.c2lvl = 2,
		.c0szstr = "64K\n",
		.c1szstr = "64K\n",
		.c2szstr = "540K\n",
		.c0sharedmapstr = "00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000001\n",
		.c1sharedmapstr = "00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000001\n",
		.c2sharedmapstr = "00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000003\n"
	},
	{
		.numaid = 1,
		.physid = 0,
		.coreid = 0,
		.c0lvl = 1,
		.c1lvl = 1,
		.c2lvl = 2,
		.c0szstr = "",
	},
	{ .numaid = C2_PROCESSORS_INVALID_ID },
	{ .numaid = C2_PROCESSORS_INVALID_ID },
	{ .numaid = C2_PROCESSORS_INVALID_ID },
	{ .numaid = C2_PROCESSORS_INVALID_ID },
	{ .numaid = C2_PROCESSORS_INVALID_ID },
	{
		.numaid = 1,
		.physid = 3,
		.coreid = 0,
		.c0lvl = 1,
		.c1lvl = 1,
		.c2lvl = 2,
		.c0szstr = "64K\n",
		.c1szstr = "64K\n",
		.c2szstr = "540K\n",
		.c0sharedmapstr = "00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000001\n",
		.c1sharedmapstr = "00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000001\n",
		.c2sharedmapstr = "00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000001\n"
	}

};

enum {
	UB_ITER = 100000
};

enum {
	POSS_MAP=1,
	AVAIL_MAP=2,
	ONLN_MAP=3
};

char *processor_info_dirp;

static void ub_init(void)
{
	return;
}

static void ub_fini(void)
{
	return;
}

static void ub_init1(int i)
{
}

static void ub_init2(int i)
{
	int rc;

	c2_processors_fini();
	rc = c2_processors_init();
	C2_UT_ASSERT(rc == 0);
}

static void ub_init3(int i)
{
	int rc;

	c2_processors_fini();
	rc = c2_processors_init();
	C2_UT_ASSERT(rc == 0);
}

static uint32_t get_num_from_file(const char *file)
{
	int      rc;
	uint32_t num = 0;
	FILE	 *fp;

	fp = fopen(file, "r");
	if (fp == NULL) {
		return num;
	}
	rc = fscanf(fp, "%u", &num);
	C2_UT_ASSERT(rc != EOF);
	fclose(fp);

	return num;
}

static void maptostr(struct c2_bitmap *map, char **buf)
{
	uint32_t i,
		 from_idx,
		 to_idx;
	bool val;

	char *str=*buf;

	C2_UT_ASSERT(map != NULL && str != NULL);
	*str = '\0';

	for (i=0; i < map->b_nr; i++) {
		val = c2_bitmap_get(map, i);
		if (val == true) {
			if (*str != '\0') {
				strcat(str, ",");
			}
			from_idx = to_idx = i;
			while (val == true && i < map->b_nr) {
				i++;
				val = c2_bitmap_get(map, i);
			}
			to_idx = i - 1;
			if (from_idx == to_idx) {
				sprintf(str, "%s%u", str, from_idx);
			} else {
				sprintf(str, "%s%u-%u", str, from_idx, to_idx);
			}
		}
	}
}

static void verify_getcpu()
{
	int	id;
	int	rc;

	struct c2_bitmap	map;
	c2_processor_nr_t	num;

	rc = c2_processors_init();
	if (rc != 0) {
		return;
	}

	num = c2_processor_nr_max();
	c2_bitmap_init(&map, num);

	c2_processors_online(&map);

	id = c2_processor_getcpu();
	if (id != -1) {
		C2_UT_ASSERT(c2_bitmap_get(&map, id));
	}

	c2_bitmap_fini(&map);
	c2_processors_fini();
}

static void verify_map(int mapid)
{
	char			buf[BUF_SZ],
				result[BUF_SZ],
				*expect,
				*map_file = NULL,
				filename[PATH_MAX];
	char                    *fgets_rc;
	int			rc;
	FILE 			*fp;
	struct c2_bitmap	map;
	c2_processor_nr_t	num;

	rc = c2_processors_init();
	if (rc != 0) {
		return;
	}

	num = c2_processor_nr_max();
	c2_bitmap_init(&map, num);

	switch (mapid) {
		case POSS_MAP:
			map_file = POSS_PROCESSOR_FILE;
			c2_processors_possible(&map);
			break;
		case AVAIL_MAP:
			map_file = AVAIL_PROCESSOR_FILE;
			c2_processors_available(&map);
			break;
		case ONLN_MAP:
			map_file = ONLN_PROCESSOR_FILE;
			c2_processors_online(&map);
			break;
		default:
			C2_UT_ASSERT(0);
			break;
	};

	expect = &buf[0];
	maptostr(&map, &expect);

	sprintf(filename, "%s/%s", processor_info_dirp, map_file);
	fp = fopen(filename, "r");
	fgets_rc = fgets(result, BUF_SZ-1, fp);
	C2_UT_ASSERT(fgets_rc != NULL);
	fclose(fp);

	rc = strncmp(result, expect, strlen(expect));
	C2_UT_ASSERT(rc == 0);

	c2_bitmap_fini(&map);
	c2_processors_fini();
}

static void verify_max_processors()
{
	char			filename[PATH_MAX];
	int			rc;
	c2_processor_nr_t	num,
				result;

	rc = c2_processors_init();
	if (rc != 0) {
		return;
	}

	sprintf(filename, "%s/%s", processor_info_dirp, MAX_PROCESSOR_FILE);
	result = (c2_processor_nr_t) get_num_from_file(filename);

	num = c2_processor_nr_max();
	C2_UT_ASSERT(num == result);

	c2_processors_fini();

}

static void verify_a_processor(c2_processor_nr_t id,
			       struct c2_processor_descr *pd)
{
	int	rc1=0,
		rc2=0;
	char	filename[PATH_MAX];

	uint32_t	coreid,
			physid,
			mixedid,
			l1_sz,
			lvl,
			l2_sz;
	struct stat	statbuf;

	C2_UT_ASSERT(pd->pd_id == id);
	C2_UT_ASSERT(pd->pd_pipeline == id);

	sprintf(filename, "%s/"NUMA_FILE1, processor_info_dirp,
					   id, pd->pd_numa_node);
	rc1 = stat(filename, &statbuf);
	if (rc1 != 0) {
		sprintf(filename, "%s/"NUMA_FILE2, processor_info_dirp,
						   pd->pd_numa_node, id);
		rc2 = stat(filename, &statbuf);
		C2_UT_ASSERT(rc2 == 0);
	}
	C2_UT_ASSERT(rc1 == 0 || rc2 == 0 || pd->pd_numa_node == 0);

	sprintf(filename, "%s/"COREID_FILE, processor_info_dirp, id);
	coreid = get_num_from_file(filename);

	sprintf(filename, "%s/"PHYSID_FILE, processor_info_dirp, id);
	physid = get_num_from_file(filename);

	mixedid = physid << 16 | coreid;
	C2_UT_ASSERT(pd->pd_l1 == id || pd->pd_l1 == mixedid);
	C2_UT_ASSERT(pd->pd_l2 == id || pd->pd_l2 == mixedid ||
		     pd->pd_l2 == physid);

	sprintf(filename, "%s/"L1SZ_FILE, processor_info_dirp, id);
	l1_sz = get_num_from_file(filename);

	l1_sz *= 1024;
	C2_UT_ASSERT(pd->pd_l1_sz == l1_sz);

	sprintf(filename, "%s/"C1_LVL_FILE, processor_info_dirp, id);
	lvl = get_num_from_file(filename);
	if (lvl == 1) {
		sprintf(filename, "%s/"L2SZ_FILE2, processor_info_dirp, id);
	} else {
		sprintf(filename, "%s/"L2SZ_FILE1, processor_info_dirp, id);
	}

	l2_sz = get_num_from_file(filename);

	l2_sz *= 1024;
	C2_UT_ASSERT(pd->pd_l2_sz == l2_sz);
}

static void verify_processors()
{
	c2_processor_nr_t	i,
				num;
	bool			val;
	int			rc;

	struct c2_bitmap		onln_map;
	struct c2_processor_descr	pd;

	rc = c2_processors_init();
	if (rc != 0) {
		return;
	}

	num = c2_processor_nr_max();
	c2_bitmap_init(&onln_map, num);
	c2_processors_online(&onln_map);

	for (i=0; i < num; i++) {
		val = c2_bitmap_get(&onln_map, i);
		if (val == true) {
			rc = c2_processor_describe(i, &pd);
			if (rc == 0) {
				verify_a_processor(i, &pd);
			}
		}
	}

	c2_bitmap_fini(&onln_map);
	c2_processors_fini();
}

static void write_str_to_file(const char *file, const char *str)
{
	FILE 	*fp;

	fp = fopen(file, "w");
	if (fp == NULL) {
		return;
	}

	fputs(str, fp);
	fclose(fp);
}

static void write_num_to_file(const char *file, uint32_t num)
{
	FILE 	*fp;

	fp = fopen(file, "w");
	if (fp == NULL) {
		return;
	}
	fprintf(fp, "%u\n", num);
	fclose(fp);
}

static void populate_cpu_summary(struct psummary *sum)
{
	int     rc;
	char	filename[PATH_MAX];

	sprintf(filename, "mkdir -p %s/cpu", processor_info_dirp);
	rc = system(filename);
	C2_UT_ASSERT(rc != -1);

	if (sum->kmaxstr) {
		sprintf(filename, "%s/"MAX_PROCESSOR_FILE, processor_info_dirp);
		write_str_to_file(filename, sum->kmaxstr);
	}

	if (sum->possstr) {
		sprintf(filename, "%s/"POSS_PROCESSOR_FILE,
				  processor_info_dirp);
		write_str_to_file(filename, sum->possstr);
	}

	if (sum->presentstr) {
		sprintf(filename, "%s/"AVAIL_PROCESSOR_FILE,
				  processor_info_dirp);
		write_str_to_file(filename, sum->presentstr);
	}

	if (sum->onlnstr) {
		sprintf(filename, "%s/"ONLN_PROCESSOR_FILE,
				  processor_info_dirp);
		write_str_to_file(filename, sum->onlnstr);
	}
}

static void populate_cpus(struct pinfo cpus[], uint32_t sz)
{
	char	filename[PATH_MAX];
	FILE 	*fp;
	uint32_t i;
	int      rc;

	for (i=0; i < sz; i++) {
		if (cpus[i].numaid == C2_PROCESSORS_INVALID_ID) {
			continue;
		}
		sprintf(filename, "mkdir -p %s/cpu/cpu%u/topology",
				  processor_info_dirp, i);
		rc = system(filename);
		C2_UT_ASSERT(rc != -1);
		sprintf(filename, "mkdir -p %s/cpu/cpu%u/cache/index0",
				  processor_info_dirp, i);
		rc = system(filename);
		C2_UT_ASSERT(rc != -1);
		sprintf(filename, "mkdir -p %s/cpu/cpu%u/cache/index1",
				  processor_info_dirp, i);
		rc = system(filename);
		C2_UT_ASSERT(rc != -1);
		sprintf(filename, "mkdir -p %s/cpu/cpu%u/cache/index2",
				  processor_info_dirp, i);
		rc = system(filename);
		C2_UT_ASSERT(rc != -1);

		sprintf(filename, "%s/"NUMA_FILE1, processor_info_dirp, i,
						   cpus[i].numaid);
		fp = fopen(filename, "w");
		fclose(fp);

		sprintf(filename, "%s/"COREID_FILE, processor_info_dirp, i);
		write_num_to_file(filename, cpus[i].coreid);

		sprintf(filename, "%s/"PHYSID_FILE, processor_info_dirp, i);
		write_num_to_file(filename, cpus[i].physid);

		sprintf(filename, "%s/"C0_LVL_FILE, processor_info_dirp, i);
		write_num_to_file(filename, cpus[i].c0lvl);

		sprintf(filename, "%s/"C1_LVL_FILE, processor_info_dirp, i);
		write_num_to_file(filename, cpus[i].c1lvl);

		sprintf(filename, "%s/"C2_LVL_FILE, processor_info_dirp, i);
		write_num_to_file(filename, cpus[i].c2lvl);

		if (cpus[i].c0szstr) {
			sprintf(filename, "%s/"L1SZ_FILE,
					   processor_info_dirp, i);
			write_str_to_file(filename, cpus[i].c0szstr);
		}

		if (cpus[i].c1szstr) {
			sprintf(filename, "%s/"L2SZ_FILE1,
					  processor_info_dirp, i);
			write_str_to_file(filename, cpus[i].c1szstr);
		}

		if (cpus[i].c2szstr) {
			sprintf(filename, "%s/"L2SZ_FILE2,
					  processor_info_dirp, i);
			write_str_to_file(filename, cpus[i].c2szstr);
		}

		if (cpus[i].c0sharedmapstr) {
			sprintf(filename, "%s/"C0_SHMAP_FILE,
					  processor_info_dirp, i);
			write_str_to_file(filename, cpus[i].c0sharedmapstr);
		}

		if (cpus[i].c1sharedmapstr) {
			sprintf(filename, "%s/"C1_SHMAP_FILE,
					  processor_info_dirp, i);
			write_str_to_file(filename, cpus[i].c1sharedmapstr);
		}

		if (cpus[i].c2sharedmapstr) {
			sprintf(filename, "%s/"C2_SHMAP_FILE,
					  processor_info_dirp, i);
			write_str_to_file(filename, cpus[i].c2sharedmapstr);
		}

	}/* for - populate test data for all CPUs */
}

static void populate_test_dataset1(void)
{

	unsigned int cpu;
	struct psummary *sum = &test1_cpus_summary;

	populate_cpu_summary(sum);

	cpu = sizeof(test1_cpus)/sizeof(struct pinfo);
	populate_cpus(test1_cpus, cpu);

}

static void clean_test_dataset(void)
{
	char	cmd[PATH_MAX];
	int     rc;

	sprintf(cmd, "rm -rf %s", processor_info_dirp);
	rc = system(cmd);
	C2_UT_ASSERT(rc != -1);

}

static void populate_test_dataset2(void)
{
	char	cmd[PATH_MAX];
	int     rc;

	sprintf(cmd, "mkdir -p %s/cpu", processor_info_dirp);
	rc = system(cmd);
	C2_UT_ASSERT(rc != -1);

}

static void populate_test_dataset3(void)
{
	struct psummary *sum = &test3_cpus_summary;
	populate_cpu_summary(sum);

}

static void populate_test_dataset4(void)
{
	struct psummary *sum = &test4_cpus_summary;
	populate_cpu_summary(sum);
}

static void populate_test_dataset5(void)
{
	struct psummary *sum = &test5_cpus_summary;
	populate_cpu_summary(sum);
}

static void populate_test_dataset6(void)
{
	struct psummary *sum = &test6_cpus_summary;
	populate_cpu_summary(sum);
}

static void populate_test_dataset7(void)
{

	unsigned int cpu;
	struct psummary *sum = &test7_cpus_summary;

	populate_cpu_summary(sum);

	cpu = sizeof(test7_cpus)/sizeof(struct pinfo);
	populate_cpus(test7_cpus, cpu);

}

static void verify_init(void)
{
	int rc;

	rc = c2_processors_init();
	C2_UT_ASSERT(rc != 0);
}

static void verify_all_params()
{
	verify_max_processors();
	verify_map(POSS_MAP);
	verify_map(AVAIL_MAP);
	verify_map(ONLN_MAP);
	verify_processors();
	verify_getcpu();
}

void test_processor(void)
{
	ub_init1(0);
	ub_init2(0);
	ub_init3(0);
	c2_processors_fini(); /* clean normal data so we can load test data */

	processor_info_dirp = SYSFS_PATH;
	verify_all_params();

	processor_info_dirp = TEST1_SYSFS_PATH;
	setenv("C2_PROCESSORS_INFO_DIR", TEST1_SYSFS_PATH, 1);
	populate_test_dataset1();
	verify_all_params();
	clean_test_dataset();

	processor_info_dirp = TEST2_SYSFS_PATH;
	setenv("C2_PROCESSORS_INFO_DIR", TEST2_SYSFS_PATH, 1);
	populate_test_dataset2();
	verify_init();
	clean_test_dataset();

	processor_info_dirp = TEST3_SYSFS_PATH;
	setenv("C2_PROCESSORS_INFO_DIR", TEST3_SYSFS_PATH, 1);
	populate_test_dataset3();
	verify_init();
	clean_test_dataset();

	processor_info_dirp = TEST4_SYSFS_PATH;
	setenv("C2_PROCESSORS_INFO_DIR", TEST4_SYSFS_PATH, 1);
	populate_test_dataset4();
	verify_init();
	clean_test_dataset();

	processor_info_dirp = TEST5_SYSFS_PATH;
	setenv("C2_PROCESSORS_INFO_DIR", TEST5_SYSFS_PATH, 1);
	populate_test_dataset5();
	verify_init();
	clean_test_dataset();

	processor_info_dirp = TEST6_SYSFS_PATH;
	setenv("C2_PROCESSORS_INFO_DIR", TEST6_SYSFS_PATH, 1);
	populate_test_dataset6();
	verify_init();
	clean_test_dataset();

	processor_info_dirp = TEST7_SYSFS_PATH;
	setenv("C2_PROCESSORS_INFO_DIR", TEST7_SYSFS_PATH, 1);
	populate_test_dataset7();
	verify_all_params();
	clean_test_dataset();

	unsetenv("C2_PROCESSORS_INFO_DIR");
	C2_UT_ASSERT(c2_processors_init() == 0); /* restore normal data */
}

struct c2_ub_set c2_processor_ub = {
	.us_name = "processor-ub",
	.us_init = ub_init,
	.us_fini = ub_fini,
	.us_run  = {
		{ .ut_name = "Init1",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_init1 },
		{ .ut_name = "Init2",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_init2 },
		{ .ut_name = "Init3",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_init3 },
		{ .ut_name = NULL }
	}
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
