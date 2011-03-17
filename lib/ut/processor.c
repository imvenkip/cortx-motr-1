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

#define	INVALID_NUMAID		0xffff

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

struct psummary g_test_cpus_summary = {
	.kmaxstr = "32\n",
	.possstr = "0-1,7-8\n",
	.presentstr = "0-1,7\n",
	.onlnstr = "0-1\n"
};

struct pinfo g_test_cpus[] = {
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
	{ .numaid = INVALID_NUMAID },
	{ .numaid = INVALID_NUMAID },
	{ .numaid = INVALID_NUMAID },
	{ .numaid = INVALID_NUMAID },
	{ .numaid = INVALID_NUMAID },
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

char *g_processor_info_dirp;

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
	C2_UT_ASSERT(c2_processor_is_initialized() == false);
}

static void ub_init2(int i)
{
	c2_processors_init();
	C2_UT_ASSERT(c2_processor_is_initialized() == true);
	c2_processors_fini();
}

static void ub_init3(int i)
{
	c2_processors_init();
	c2_processors_fini();
	C2_UT_ASSERT(c2_processor_is_initialized() == false);
}

static void maptostr(struct c2_bitmap *map, char **buf)
{
	unsigned int i, from_idx, to_idx;
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
	bool	val;
	struct c2_bitmap	map;
	c2_processor_nr_t	num;

	c2_processors_init();
	num = c2_processor_nr_max();
	c2_bitmap_init(&map, num);

	c2_processors_online(&map);

	id = c2_processor_getcpu();
	if (id != -1) {
		val = c2_bitmap_get(&map, id);
		C2_UT_ASSERT(val == true);
	}
	c2_processors_fini();
}

static void verify_map(int mapid)
{
	char			buf[BUF_SZ],
				result[BUF_SZ],
				*expect,
				*map_file,
				filename[PATH_MAX];
	int			rc;
	FILE 			*fp;
	struct c2_bitmap	map;
	c2_processor_nr_t	num;

	c2_processors_init();
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
			C2_UT_ASSERT(1);
			break;
	};

	expect = &buf[0];
	maptostr(&map, &expect);

	sprintf(filename, "%s/%s", g_processor_info_dirp, map_file);
	fp = fopen(filename, "r");
	fgets(result, BUF_SZ-1, fp);
	C2_UT_ASSERT(fp != NULL);
	fclose(fp);

	rc = strncmp(result, expect, strlen(expect));
	C2_UT_ASSERT( rc == 0);

	c2_processors_fini();
}

static void verify_max_processors()
{
	FILE			*fp;
	char			filename[PATH_MAX];
	c2_processor_nr_t	num,
				result;

	sprintf(filename, "%s/%s", g_processor_info_dirp, MAX_PROCESSOR_FILE);
	fp = fopen(filename, "r");
	fscanf(fp, "%u", &result);
	C2_UT_ASSERT(fp != NULL);
	fclose(fp);

	c2_processors_init();
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
	FILE	*fp;

	unsigned int	coreid, physid, mixedid, l1_sz, lvl, l2_sz;
	struct stat	statbuf;

	C2_UT_ASSERT(pd->pd_id == id);
	C2_UT_ASSERT(pd->pd_pipeline == id);

	sprintf(filename, "%s/"NUMA_FILE1, g_processor_info_dirp,
					   id, pd->pd_numa_node);
	rc1 = stat(filename, &statbuf);
	C2_UT_ASSERT(rc1 == 0);
	if (rc1 != 0) {
		sprintf(filename, "%s/"NUMA_FILE2, g_processor_info_dirp,
						   pd->pd_numa_node, id);
		rc2 = stat(filename, &statbuf);
		C2_UT_ASSERT(rc2 == 0);
	}
	C2_UT_ASSERT(rc1 == 0 || rc2 == 0 || pd->pd_numa_node == 0);

	sprintf(filename, "%s/"COREID_FILE, g_processor_info_dirp, id);
	fp = fopen(filename, "r");
	C2_UT_ASSERT(fp != NULL);
	fscanf(fp, "%u", &coreid);
	fclose(fp);

	sprintf(filename, "%s/"PHYSID_FILE, g_processor_info_dirp, id);
	fp = fopen(filename, "r");
	C2_UT_ASSERT(fp != NULL);
	fscanf(fp, "%u", &physid);
	fclose(fp);

	mixedid = physid << 16 | coreid;
	C2_UT_ASSERT(pd->pd_l1 == id || pd->pd_l1 == mixedid);
	C2_UT_ASSERT(pd->pd_l2 == id || pd->pd_l2 == mixedid ||
		     pd->pd_l2 == physid);
	

	sprintf(filename, "%s/"L1SZ_FILE, g_processor_info_dirp, id);
	fp = fopen(filename, "r");
	C2_UT_ASSERT(fp != NULL);
	fscanf(fp, "%uK", &l1_sz);
	fclose(fp);

	l1_sz *= 1024;
	C2_UT_ASSERT(pd->pd_l1_sz == l1_sz);

	sprintf(filename, "%s/"C1_LVL_FILE, g_processor_info_dirp, id);
	fp = fopen(filename, "r");
	C2_UT_ASSERT(fp != NULL);
	fscanf(fp, "%u", &lvl);
	fclose(fp);
	if (lvl == 1) {
		sprintf(filename, "%s/"L2SZ_FILE2, g_processor_info_dirp, id);
	} else {
		sprintf(filename, "%s/"L2SZ_FILE1, g_processor_info_dirp, id);
	}
	fp = fopen(filename, "r");
	C2_UT_ASSERT(fp != NULL);
	fscanf(fp, "%uK", &l2_sz);
	fclose(fp);

	l2_sz *= 1024;
	C2_UT_ASSERT(pd->pd_l2_sz == l2_sz);
}

static void verify_processors()
{
	c2_processor_nr_t	i, num;
	bool			val;

	struct c2_bitmap		onln_map;
	struct c2_processor_descr	pd;

	c2_processors_init();
	num = c2_processor_nr_max();
	c2_bitmap_init(&onln_map, num);
	c2_processors_online(&onln_map);

	for (i=0; i < num; i++) {
		val = c2_bitmap_get(&onln_map, i);
		if (val == true) {
			c2_processor_describe(i, &pd);
			verify_a_processor(i, &pd);
		}
	}
	c2_processors_fini();
}

static void populate_test_dataset1(void)
{
	char	filename[PATH_MAX];
	FILE 	*fp;

	unsigned int i, cpu;

	sprintf(filename, "mkdir -p %s/cpu", g_processor_info_dirp);
	system(filename);

	sprintf(filename, "%s/"MAX_PROCESSOR_FILE, g_processor_info_dirp);
	fp = fopen(filename, "w");
	C2_UT_ASSERT(fp != NULL);
	fputs(g_test_cpus_summary.kmaxstr, fp);
	fclose(fp);
	
	sprintf(filename, "%s/"POSS_PROCESSOR_FILE, g_processor_info_dirp);
	fp = fopen(filename, "w");
	C2_UT_ASSERT(fp != NULL);
	fputs(g_test_cpus_summary.possstr, fp);
	fclose(fp);

	sprintf(filename, "%s/"AVAIL_PROCESSOR_FILE, g_processor_info_dirp);
	fp = fopen(filename, "w");
	C2_UT_ASSERT(fp != NULL);
	fputs(g_test_cpus_summary.presentstr, fp);
	fclose(fp);

	sprintf(filename, "%s/"ONLN_PROCESSOR_FILE, g_processor_info_dirp);
	fp = fopen(filename, "w");
	C2_UT_ASSERT(fp != NULL);
	fputs(g_test_cpus_summary.onlnstr, fp);
	fclose(fp);

	cpu = sizeof(g_test_cpus)/sizeof(struct pinfo);

	for (i=0; i < cpu; i++) {
		if (g_test_cpus[i].numaid == INVALID_NUMAID) {
			continue;
		}
		sprintf(filename, "mkdir -p %s/cpu/cpu%u/topology",
				  g_processor_info_dirp, i);
		system(filename);
		sprintf(filename, "mkdir -p %s/cpu/cpu%u/cache/index0",
				  g_processor_info_dirp, i);
		system(filename);
		sprintf(filename, "mkdir -p %s/cpu/cpu%u/cache/index1",
				  g_processor_info_dirp, i);
		system(filename);
		sprintf(filename, "mkdir -p %s/cpu/cpu%u/cache/index2",
				  g_processor_info_dirp, i);
		system(filename);

		sprintf(filename, "%s/"NUMA_FILE1, g_processor_info_dirp, i,
						   g_test_cpus[i].numaid);
		fp = fopen(filename, "w");
		C2_UT_ASSERT(fp != NULL);
		fclose(fp);

		sprintf(filename, "%s/"COREID_FILE, g_processor_info_dirp, i);
		fp = fopen(filename, "w");
		C2_UT_ASSERT(fp != NULL);
		fprintf(fp, "%u\n", g_test_cpus[i].coreid);
		fclose(fp);

		sprintf(filename, "%s/"PHYSID_FILE, g_processor_info_dirp, i);
		fp = fopen(filename, "w");
		C2_UT_ASSERT(fp != NULL);
		fprintf(fp, "%u\n", g_test_cpus[i].physid);
		fclose(fp);

		sprintf(filename, "%s/"C0_LVL_FILE, g_processor_info_dirp, i);
		fp = fopen(filename, "w");
		C2_UT_ASSERT(fp != NULL);
		fprintf(fp, "%u\n", g_test_cpus[i].c0lvl);
		fclose(fp);

		sprintf(filename, "%s/"C1_LVL_FILE, g_processor_info_dirp, i);
		fp = fopen(filename, "w");
		C2_UT_ASSERT(fp != NULL);
		fprintf(fp, "%u\n", g_test_cpus[i].c1lvl);
		fclose(fp);

		sprintf(filename, "%s/"C2_LVL_FILE, g_processor_info_dirp, i);
		fp = fopen(filename, "w");
		C2_UT_ASSERT(fp != NULL);
		fprintf(fp, "%u\n", g_test_cpus[i].c2lvl);
		fclose(fp);

		sprintf(filename, "%s/"L1SZ_FILE, g_processor_info_dirp, i);
		fp = fopen(filename, "w");
		C2_UT_ASSERT(fp != NULL);
		fprintf(fp, "%s", g_test_cpus[i].c0szstr);
		fclose(fp);

		sprintf(filename, "%s/"L2SZ_FILE1, g_processor_info_dirp, i);
		fp = fopen(filename, "w");
		C2_UT_ASSERT(fp != NULL);
		fprintf(fp, "%s", g_test_cpus[i].c1szstr);
		fclose(fp);

		sprintf(filename, "%s/"L2SZ_FILE2, g_processor_info_dirp, i);
		fp = fopen(filename, "w");
		C2_UT_ASSERT(fp != NULL);
		fprintf(fp, "%s", g_test_cpus[i].c2szstr);
		fclose(fp);

		sprintf(filename, "%s/"C0_SHMAP_FILE, g_processor_info_dirp, i);
		fp = fopen(filename, "w");
		C2_UT_ASSERT(fp != NULL);
		fprintf(fp, "%s", g_test_cpus[i].c0sharedmapstr);
		fclose(fp);

		sprintf(filename, "%s/"C1_SHMAP_FILE, g_processor_info_dirp, i);
		fp = fopen(filename, "w");
		C2_UT_ASSERT(fp != NULL);
		fprintf(fp, "%s", g_test_cpus[i].c1sharedmapstr);
		fclose(fp);

		sprintf(filename, "%s/"C2_SHMAP_FILE, g_processor_info_dirp, i);
		fp = fopen(filename, "w");
		C2_UT_ASSERT(fp != NULL);
		fprintf(fp, "%s", g_test_cpus[i].c2sharedmapstr);
		fclose(fp);

	}/* for - populate test data for all CPUs */

}

void test_processor(void)
{

	ub_init1(0);
	ub_init2(0);
	ub_init3(0);

	g_processor_info_dirp = SYSFS_PATH;
	verify_max_processors();
	verify_map(POSS_MAP);
	verify_map(AVAIL_MAP);
	verify_map(ONLN_MAP);
	verify_processors();
	verify_getcpu();

	g_processor_info_dirp = TEST1_SYSFS_PATH;
	setenv("C2_PROCESSORS_INFO_DIR", TEST1_SYSFS_PATH, 1);
	populate_test_dataset1();
	verify_max_processors();
	verify_map(POSS_MAP);
	verify_map(AVAIL_MAP);
	verify_map(ONLN_MAP);
	verify_processors();
	verify_getcpu();

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
