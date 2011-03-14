/* -*- C -*- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <linux/limits.h>

#include "lib/processor.h"
#include "lib/list.h"

/**
   @addtogroup Processor

   This file includes additional data structures and function for caching
   processors data - for user-mode programs.

   This file will also implement Linux user-mode processors interfaces.

   @section TestCases

   @subsection Configuration
   Get sysfs file system data for following configurations:
      - Single CPU
      - SMP
      - SMP with multicore CPUs
      - NUMA
      - Configurations with Intel, AMD processors
      - Developer VMs
   @subsection Procedure
   - Point the directory for sysfs file system data for the above
     configurations using environment variable 'C2_PROCESSORS_INFO_DIR'.
   - Create expected reults file.
   -  Run unit test and compare the processor info structure with expected
      results.
   @{

 */
#define MAX_LINE_LEN	256

#define C2_PROCESSORS_INFO_ENV		"C2_PROCESSORS_INFO_DIR"

#define C2_PROCESSORS_SYSFS_DIR		"/sys/devices/system"
#define C2_PROCESSORS_CPU_DIR		"cpu/"
#define C2_PROCESSORS_NODE_DIR		"node/"

#define C2_PROCESSORS_MAX_FILE		"cpu/kernel_max"
#define C2_PROCESSORS_POSS_FILE		"cpu/possible"
#define C2_PROCESSORS_PRESENT_FILE	"cpu/present"
#define C2_PROCESSORS_ONLINE_FILE	"cpu/online"

#define C2_PROCESSORS_CACHE1_LEVEL_FILE	"cache/index0/level"
#define C2_PROCESSORS_CACHE2_LEVEL_FILE	"cache/index1/level"
#define C2_PROCESSORS_CACHE3_LEVEL_FILE	"cache/index2/level"

#define C2_PROCESSORS_CACHE1_SHCPUMAP_FILE	"cache/index0/shared_cpu_map"
#define C2_PROCESSORS_CACHE2_SHCPUMAP_FILE	"cache/index1/shared_cpu_map"
#define C2_PROCESSORS_CACHE3_SHCPUMAP_FILE	"cache/index2/shared_cpu_map"

#define C2_PROCESSORS_CACHE1_SIZE_FILE	"cache/index0/size"
#define C2_PROCESSORS_CACHE2_SIZE_FILE	"cache/index1/size"
#define C2_PROCESSORS_CACHE3_SIZE_FILE	"cache/index2/size"

#define C2_PROCESSORS_COREID_FILE	"topology/core_id"
#define C2_PROCESSORS_PHYSID_FILE	"topology/physical_package_id"

#define C2_PROCESSORS_L1		1
#define C2_PROCESSORS_L2		2

#define C2_PROCESSORS_CPU_DIR_PREFIX	"cpu/cpu"

#define C2_PROCESSORS_NODE_STR		"node"
#define C2_PROCESSORS_CPU_STR		"cpu"

#define C2_PROCESSORS_RANGE_SET_SEPARATOR	","
#define C2_PROCESSORS_RANGE_SEPARATOR		'-'

/**
   System wide summary of all the processors. This is the head node.
   It contains various processor statistics and a linked list of
   processor info (struct c2_processor_descr).

   @see lib/processor.h
 */
struct c2_processor_sys_summary {
	/** Head of the list for processor info */
	struct c2_list	pss_head;

	/** Number of possible processors that can be attached to this OS.
          This means the maximum number of processors that OS can handle.  */
	c2_processor_nr_t pss_max;

	/** bitmap of possible processors on this node */
	struct c2_bitmap pss_poss_map;

	/** bitmap of processors that are present on this node */
	struct c2_bitmap pss_avail_map;

	/** bitmap of online processors on this node */
	struct c2_bitmap pss_online_map;

};

/**
   A node in the linked list describing processor properties. It
   encapsulates 'struct c2_processor_descr'.
   @see lib/processor.h
 */
struct c2_processor_node {
	/** Linking structure for node */
	struct c2_list_link	pn_link;

	/** Processor descritor strcture */
	struct c2_processor_descr pn_info;
};

/**
	Global Variables.
 */
static struct c2_processor_sys_summary g_sys_cpus;
static bool g_c2_processor_init=false;

/**
   This function converts a bitmap string into a bitmap of c2_bitmap type.

   @param map -> c2 bitmap structure that will store the bitmap. Memory
                 for this parameter should be allocated before calling.
   @param mapstr -> bitmap string

   @see c2_processors_set_avail_map()
   @see c2_processors_set_online_map()
   @see c2_processors_set_poss_map()

 */
static void c2_processor_set_map(struct c2_bitmap *map, const char *mapstr)
{
	uint32_t from_id=0, to_id=0, id;
	char *str, *rangeset, *range;

	C2_ASSERT(map != NULL);
	C2_ASSERT(mapstr != NULL);

	/*
	 * strtok() modifies string. Hence create a copy.
	 */
	str = strdup(mapstr);
	C2_ASSERT(str != NULL);

	/*
	 * Tokenize the string to separate cpu ranges.
	 * For example, the string looks like 0-3,7,8-12
	 */
	rangeset = strtok(str, C2_PROCESSORS_RANGE_SET_SEPARATOR);
	while (rangeset != NULL) {
		/*
		 * Parse from and to indices within the range.
		 */
		range = strchr(rangeset, C2_PROCESSORS_RANGE_SEPARATOR);
		if (range) {
			sscanf(rangeset, "%u-%u", &from_id, &to_id);
		} else {
			sscanf(rangeset, "%u", &from_id);
			to_id = from_id;
		}
		/*
		 * Set the bitmap for the given range.
		 */
		C2_ASSERT(to_id <= map->b_nr);
		for (id=from_id; id <= to_id; id++) {
			c2_bitmap_set(map, id, 1);
		}
		rangeset = strtok(NULL, C2_PROCESSORS_RANGE_SET_SEPARATOR);

	}/* while - string is parsed */
	
	/*
	 * Free memory allocated by strdup()
	 */
	free(str);
}

/**
   Read "cpu/present" file under sysfs. Read the present cpu string and
   convert it into a bitmap.

   @pre Assumes the directory has been changed to approriate CPU
        info dir.
   @see c2_processors_set_avail_map()
   @see c2_processors_set_online_map()
   @see c2_processors_set_poss_map()
   @see c2_processor_set_map()

 */
static void c2_processor_set_avail_map()
{
	int	rc;
	char	buf[MAX_LINE_LEN+1];
	FILE	*fp;

	/*
	 * Open present/available CPUs file
	 */
	fp = fopen(C2_PROCESSORS_PRESENT_FILE, "r");
	C2_ASSERT(fp != NULL);

        fgets(buf, MAX_LINE_LEN, fp);

	rc = c2_bitmap_init(&g_sys_cpus.pss_avail_map, g_sys_cpus.pss_max);
	C2_ASSERT(rc == 0);

	/*
	 * Parse the present/available CPUs string and set the bitmap.
	 */
	c2_processor_set_map(&g_sys_cpus.pss_avail_map, buf);

	fclose(fp);
}

/**
   Read "cpu/online" file under sysfs. Read the online cpu string and
   convert it into a bitmap.

   @pre Assumes the directory has been changed to approriate CPU
        info dir.
   @see c2_processors_set_avail_map()
   @see c2_processors_set_online_map()
   @see c2_processors_set_poss_map()
   @see c2_processor_set_map()

 */
static void c2_processor_set_online_map()
{
	int	rc;
	char	buf[MAX_LINE_LEN+1];
	FILE	*fp;

	/*
	 * Open online CPUs file
	 */
	fp = fopen(C2_PROCESSORS_ONLINE_FILE, "r");
	C2_ASSERT(fp != NULL);

        fgets(buf, MAX_LINE_LEN, fp);

	rc = c2_bitmap_init(&g_sys_cpus.pss_online_map, g_sys_cpus.pss_max);
	C2_ASSERT(rc == 0);

	/*
	 * Parse the online CPUs string and set the bitmap.
	 */
	c2_processor_set_map(&g_sys_cpus.pss_online_map, buf);

	fclose(fp);
}

/**
   Read "cpu/possible" file under sysfs. Read the possible cpu string and
   convert it into a bitmap.

   @pre Assumes the directory has been changed to approriate CPU
        info dir.
   @see c2_processors_set_avail_map()
   @see c2_processors_set_online_map()
   @see c2_processors_set_poss_map()
   @see c2_processor_set_map()

 */
static void c2_processor_set_poss_map()
{
	int	rc;
	char	buf[MAX_LINE_LEN+1];
	FILE	*fp;

	/*
	 * Open possible CPUs file
	 */
	fp = fopen(C2_PROCESSORS_POSS_FILE, "r");
	C2_ASSERT(fp != NULL);

        fgets(buf, MAX_LINE_LEN, fp);

	rc = c2_bitmap_init(&g_sys_cpus.pss_poss_map, g_sys_cpus.pss_max);
	C2_ASSERT(rc == 0);

	/*
	 * Parse the possible CPUs string and set the bitmap.
	 */
	c2_processor_set_map(&g_sys_cpus.pss_poss_map, buf);

	fclose(fp);
}

/**
   Read "cpu/kernel_max" file under sysfs. Read the kernel_max cpu string and
   convert it into a number.

   @pre Assumes the directory has been changed to approriate CPU
        info dir.
   @see c2_processors_set_avail_map()
   @see c2_processors_set_online_map()
   @see c2_processors_set_poss_map()
   @see c2_processor_set_map()

 */
static void c2_processor_set_maxsz()
{
	FILE	*fp;

	fp = fopen(C2_PROCESSORS_MAX_FILE, "r");
	C2_ASSERT(fp != NULL);

	fscanf(fp, "%u\n", &g_sys_cpus.pss_max);

	fclose(fp);

}

/**
   Fetch NUMA node id for a given processor.

   @param id -> id of the processor for which information is requested.

   @retval -> id of the NUMA node to which the processor belongs. If the
              machine is not configured as NUMA, returns 0.

   @pre Assumes the directory has been changed to approriate CPU
        info dir.
 */
static uint32_t c2_processor_get_numanodeid(c2_processor_nr_t id)
{
	uint32_t numa_node_id=0, gotid=0;
	int rc;

	char	dirname[PATH_MAX];

	DIR	*dirp;
	struct stat statbuf;
	struct dirent *fname;

	sprintf(dirname, C2_PROCESSORS_CPU_DIR_PREFIX"%u", id);
	dirp = opendir(dirname);

	/*
	 * Find node id under .../cpu/cpu<id>/node<id>
	 */
	while ((fname = readdir(dirp)) != NULL) {
		if (fname->d_name[0] == 'n'
		    && !strncmp(fname->d_name, C2_PROCESSORS_NODE_STR,
			      strlen(C2_PROCESSORS_NODE_STR))) {
			sscanf(fname->d_name,
			       C2_PROCESSORS_NODE_STR"%u", &numa_node_id);
			gotid = 1;
			break;
		}
	}/* while - entire cpuX dir is scanned */

	closedir(dirp);

	if (gotid) {
		goto out;
	}

	/*
	 * If nodeid file is not found in previous search, look for cpuX file
	 * under node/nod<id>/cpu<id>
	 */
	dirp = opendir(C2_PROCESSORS_NODE_DIR);
	while ((fname = readdir(dirp)) != NULL) {
		if (fname->d_name[0] == 'n'
		    && !strncmp(fname->d_name, C2_PROCESSORS_NODE_STR,
			      strlen(C2_PROCESSORS_NODE_STR))) {
			sprintf(dirname, "%s%s/%s%u",
				C2_PROCESSORS_NODE_DIR, fname->d_name,
				C2_PROCESSORS_CPU_STR, id);
			rc = stat(dirname, &statbuf);
			if (rc == 0) {
				sscanf(fname->d_name,
				       C2_PROCESSORS_NODE_STR"%u",
				       &numa_node_id);
				break;
			}
		}
	}/* while - entire node dir is scanned */
	closedir(dirp);

	/*
	 * Note : If no numa node id is found, it's assumed to be 0.
	 */
out:
	return numa_node_id;
}

/**
   Read "cpu/cpu<id>/toplology/core_id" file under sysfs. Read the core id
   string convert it into a number.

   @pre Assumes the directory has been changed to approriate CPU
        info dir.
   @param id -> id of the processor for which information is requested.

   @retval "core" id for a given processor
 */
static uint32_t c2_processor_get_coreid(c2_processor_nr_t id)
{
	uint32_t coreid;
	char filebuf[PATH_MAX];
	FILE *fp;

	sprintf(filebuf, "%s%u/%s", C2_PROCESSORS_CPU_DIR_PREFIX, id,
				   C2_PROCESSORS_COREID_FILE);
	fp = fopen(filebuf, "r");
	C2_ASSERT(fp != NULL);
	fscanf(fp,"%u",&coreid);
	fclose(fp);

	return coreid;
}

/**
   Read "cpu/cpu<id>/toplology/physical_package_id" file under sysfs. Read
   the physical package id string convert it into a number.

   @pre Assumes the directory has been changed to approriate CPU
        info dir.
   @param id -> id of the processor for which information is requested.

   @retval "core" id for a given processor
 */
static uint32_t c2_processor_get_physid(c2_processor_nr_t id)
{
	uint32_t physid;
	char	filebuf[PATH_MAX];
	FILE *fp;

	sprintf(filebuf, "%s%u/%s", C2_PROCESSORS_CPU_DIR_PREFIX, id,
				   C2_PROCESSORS_PHYSID_FILE);
	fp = fopen(filebuf, "r");
	C2_ASSERT(fp != NULL);
	fscanf(fp,"%u",&physid);
	fclose(fp);

	return physid;
}

/**
   Read string bitmap and check if the other CPUs share the resource.

   @param string_map -> a string representing a bitmap

   @retval true if the processor shares the map
   @retval false if the processor does not the map
 */
static bool c2_processor_is_cache_shared(const char *mapstr)
{
	char *ptr, *str;

	uint32_t bit, num, i, shared_cpus=0;

	/*
	 * strtok() modifies string. Hence create a copy.
	 */
	str = strdup(mapstr);

	/*
	 * The string is a bitmap. Each byte string is separated by ",".
	 */
	ptr = strtok(str, C2_PROCESSORS_RANGE_SET_SEPARATOR);
	while (ptr) {
		/*
		 * Convert the "bytes" string into a number.
		 */
		sscanf(ptr,"%x",&num);
		bit = 1;
		for (i=0; i<8; i++) {
			if (num & bit) { /* Check if bit is set */
				shared_cpus++;
			}
			bit <<= 1;
		}
		/*
		 * If we have already found more than one cpu, don't
		 * scan further.
		 */
		if (shared_cpus > 1) { /* Is cache shared? */
			break;
		}
		ptr = strtok(NULL, C2_PROCESSORS_RANGE_SET_SEPARATOR);

	}/* while - entire string is parsed */

	C2_ASSERT(shared_cpus >= 1);

	/*
	 * Free memory allocated by strdup()
	 */
	free(str);

	return (shared_cpus > 1) ? true : false;
}

/**
   Fetch L1 cache size for a given processor.

   @param id -> id of the processor for which information is requested.

   @retval size of L1 cache for the given processor.

   @pre Assumes the directory has been changed to approriate CPU
        info dir.
 */
static size_t c2_processor_get_l1_size(c2_processor_nr_t id)
{
	uint32_t lvl, sz;
	size_t	size;
	char	filename[PATH_MAX];
	FILE	*fp;

	sprintf(filename, "%s%u/%s", C2_PROCESSORS_CPU_DIR_PREFIX, id,
				   C2_PROCESSORS_CACHE1_LEVEL_FILE);
	fp = fopen(filename, "r");
	C2_ASSERT(fp != NULL);
	fscanf(fp,"%u",&lvl);
	fclose(fp);

	C2_ASSERT(lvl == C2_PROCESSORS_L1);

	/*
	 * Set path to appropriate cache size file
	 */
	sprintf(filename, "%s%u/%s", C2_PROCESSORS_CPU_DIR_PREFIX, id,
				   C2_PROCESSORS_CACHE1_SIZE_FILE);

	/*
	 * Get the size string. It's in format 32K, 6144K etc.
	 */
	fp = fopen(filename, "r");
	C2_ASSERT(fp != NULL);
	fscanf(fp,"%uK", &sz);
	fclose(fp);

	size = (size_t) (sz * 1024);

	return size;
}

/**
   Fetch L2 cache size for a given processor.

   @param id -> id of the processor for which information is requested.

   @retval size of L2 cache for the given processor.

   @pre Assumes the directory has been changed to approriate CPU
        info dir.
 */
static size_t c2_processor_get_l2_size(c2_processor_nr_t id)
{
	uint32_t lvl, sz, l3_cache_present;
	size_t	size;
	char	filename[PATH_MAX];
	FILE	*fp;

	sprintf(filename, "%s%u/%s", C2_PROCESSORS_CPU_DIR_PREFIX, id,
				   C2_PROCESSORS_CACHE3_LEVEL_FILE);
	fp = fopen(filename, "r");
	C2_ASSERT(fp != NULL);
	fscanf(fp,"%u",&lvl);
	fclose(fp);

	if (lvl != C2_PROCESSORS_L2) { /* If L2 level is not found */
		sprintf(filename, "%s%u/%s", C2_PROCESSORS_CPU_DIR_PREFIX, id,
					   C2_PROCESSORS_CACHE2_LEVEL_FILE);
		fp = fopen(filename, "r");
		C2_ASSERT(fp != NULL);
		fscanf(fp,"%u",&lvl);
		fclose(fp);

		C2_ASSERT(lvl == C2_PROCESSORS_L2);
		l3_cache_present = 1;
		/*
		 * Set path to appropriate cache size file
		 */
		sprintf(filename, "%s%u/%s", C2_PROCESSORS_CPU_DIR_PREFIX, id,
					   C2_PROCESSORS_CACHE2_SIZE_FILE);
	} else {
		/*
		 * Set path to appropriate cache size file
		 */
		sprintf(filename, "%s%u/%s", C2_PROCESSORS_CPU_DIR_PREFIX, id,
					   C2_PROCESSORS_CACHE3_SIZE_FILE);
	}

	/*
	 * Get the size string. It's in format 32K, 6144K etc.
	 */
	fp = fopen(filename, "r");
	C2_ASSERT(fp != NULL);
	fscanf(fp,"%uK", &sz);
	fclose(fp);

	size = (size_t)(sz * 1024);

	return size;
}

/**
   Fetch L1 cache id for a given processor.

   @param id -> id of the processor for which information is requested.

   @retval id of L1 cache for the given processor.

   @pre Assumes the directory has been changed to approriate CPU
        info dir.
 */
static uint32_t c2_processor_get_l1_cacheid(c2_processor_nr_t id)
{
	uint32_t lvl, coreid, physid, l1_id=id;
	bool	is_shared;
	char	filename[PATH_MAX], buf[MAX_LINE_LEN+1];
	FILE	*fp;

	sprintf(filename, "%s%u/%s", C2_PROCESSORS_CPU_DIR_PREFIX, id,
				   C2_PROCESSORS_CACHE1_LEVEL_FILE);
	fp = fopen(filename, "r");
	C2_ASSERT(fp != NULL);
	fscanf(fp,"%u",&lvl);
	fclose(fp);

	C2_ASSERT(lvl == C2_PROCESSORS_L1);
	/*
	 * Set path to appropriate shared cpu file name
	 */
	sprintf(filename, "%s%u/%s", C2_PROCESSORS_CPU_DIR_PREFIX, id,
			  C2_PROCESSORS_CACHE1_SHCPUMAP_FILE);

	/*
	 * Get the shared cpu map string.
	 */
	fp = fopen(filename, "r");
	C2_ASSERT(fp != NULL);
        fgets(buf, MAX_LINE_LEN, fp);
	fclose(fp);

	/*
	 * Scan the map string to find how many bits are set in the string
	 * If more than one bit is set, then cache is shared.
	 */
	is_shared = c2_processor_is_cache_shared(buf);
	if (is_shared == true) { /* L1 cache is shared */
		physid = c2_processor_get_physid(id);
		coreid = c2_processor_get_coreid(id);	
		l1_id = physid << 16 | coreid;
	}

	return l1_id;
}

/**
   Fetch L2 cache id for a given processor.

   @param id -> id of the processor for which information is requested.

   @retval id of L2 cache for the given processor.

   @pre Assumes the directory has been changed to approriate CPU
        info dir.
 */
static uint32_t c2_processor_get_l2_cacheid(c2_processor_nr_t id)
{
	uint32_t lvl, l2_id=id, coreid, physid;
	bool	is_shared, l3_cache_present=false;
	char	filename[PATH_MAX], buf[MAX_LINE_LEN+1];
	FILE	*fp;

	sprintf(filename, "%s%u/%s", C2_PROCESSORS_CPU_DIR_PREFIX, id,
				   C2_PROCESSORS_CACHE3_LEVEL_FILE);
	fp = fopen(filename, "r");
	C2_ASSERT(fp != NULL);
	fscanf(fp,"%u",&lvl);
	fclose(fp);

	if (lvl != C2_PROCESSORS_L2) {
		sprintf(filename, "%s%u/%s", C2_PROCESSORS_CPU_DIR_PREFIX, id,
					   C2_PROCESSORS_CACHE2_LEVEL_FILE);
		fp = fopen(filename, "r");
		C2_ASSERT(fp != NULL);
		fscanf(fp,"%u",&lvl);
		fclose(fp);

		C2_ASSERT(lvl == C2_PROCESSORS_L2);
		l3_cache_present = true;
		/*
		 * Set path to appropriate shared cpu file name
		 */
		sprintf(filename, "%s%u/%s", C2_PROCESSORS_CPU_DIR_PREFIX, id,
					   C2_PROCESSORS_CACHE2_SHCPUMAP_FILE);
	} else {
		/*
		 * Set path to appropriate shared cpu file name
		 */
		sprintf(filename, "%s%u/%s", C2_PROCESSORS_CPU_DIR_PREFIX, id,
					   C2_PROCESSORS_CACHE3_SHCPUMAP_FILE);
	}

	/*
	 * Get the shared cpu map string.
	 */
	fp = fopen(filename, "r");
	C2_ASSERT(fp != NULL);
        fgets(buf, MAX_LINE_LEN, fp);
	fclose(fp);

	/*
	 * Scan the map string to find how many bits are set in the string
	 */
	is_shared = c2_processor_is_cache_shared(buf);
	if (is_shared == true) { /* L2 cache is shared */
		physid = c2_processor_get_physid(id);

		if (l3_cache_present == true) { /* L3 cache is present */
			coreid = c2_processor_get_coreid(id);	
			l2_id = physid << 16 | coreid;
		} else {
			l2_id = physid;
		}
	}

	return l2_id;
}

/**
   Fetch pipeline id for a given processor.
   Curently pipeline id is same as processor id.

   @param id -> id of the processor for which information is requested.

   @retval id of pipeline for the given processor.

 */
static inline uint32_t c2_processor_get_pipelineid(c2_processor_nr_t id)
{
	return id;
}

/**
   Collect all the information needed to describe a single processor.
   This function will scan,parse directories and files under "sysfs".
   This data is cached.

   This function will be called from c2_processors_getsummary().

   @param sysfs_dir -> Directory underwhich the information should be searched
   @param id -> id of the processor for which information is requested.
   @param pn -> A linked list node containing processor information.

   @pre Memory to 'pn' must be allocated by the calling function
   @post pn structure will be filled with processor information

   @see c2_processors_getsummary()
 */
static void c2_processor_getinfo(c2_processor_nr_t id,
                          	 struct c2_processor_node *pn)
{
	C2_ASSERT(pn != NULL);

	pn->pn_info.pd_id = id;
	pn->pn_info.pd_numa_node = c2_processor_get_numanodeid(id);
	pn->pn_info.pd_l1 = c2_processor_get_l1_cacheid(id);
	pn->pn_info.pd_l1_sz = c2_processor_get_l1_size(id);
	pn->pn_info.pd_l2 = c2_processor_get_l2_cacheid(id);
	pn->pn_info.pd_l2_sz = c2_processor_get_l2_size(id);
	pn->pn_info.pd_pipeline = c2_processor_get_pipelineid(id);
}

/**
   Parse "sysfs" (/sys/devices/system) directory to fetch the summary of 
   processors on this system.

   To facilitate testing, this function will fetch the directory string from
   environment variable C2_PROCESSORS_INFO_DIR. This environment variable
   should be used only for unit testing.
   Under normal operation, a default value of "sysfs" directory is used.

   @pre  C2_PROCESSORS_INFO_DIR/default directory must exist.
   @post A global variable of type c2_processor_sys_summary will be filled in

   @see lib/processor.h 
   @see void c2_processors_init()

 */
static void c2_processors_getsummary()
{
	int rc;
	bool present;
	uint32_t cpuid;
	char *dirp, *str, cwd[PATH_MAX];
	struct c2_processor_node *pinfo;

	dirp = getenv(C2_PROCESSORS_INFO_ENV);
	if (dirp == NULL) {
		dirp = C2_PROCESSORS_SYSFS_DIR;
	}

	/*
	 * Obtain current working directory.
	 */
	str = getcwd(cwd, sizeof(cwd) - 1);
	C2_ASSERT(str != NULL);

	/*
	 * Change directory to desired "sysfs" directory.
	 * Subsequent functions will use file names "relative" to syfs dir.
	 * Subsequent function will work in the context of "sysfs" directory.
	 */
	rc = chdir(dirp);
	C2_ASSERT(rc == 0);

	/*
	 * Now get the summary of max/possible/avail/online CPUs
	 * All these functions will collect summary in 'g_sys_cpus'.
	 */
	c2_processor_set_maxsz();
	c2_processor_set_poss_map();
	c2_processor_set_avail_map();
	c2_processor_set_online_map();

	c2_list_init(&g_sys_cpus.pss_head);

	/*
	 * Using present/available CPU mask get details of each processor.
	 */
	for (cpuid=0; cpuid < g_sys_cpus.pss_avail_map.b_nr; cpuid++) {
		present = c2_bitmap_get(&g_sys_cpus.pss_avail_map, cpuid);
		if (present == true) {
			pinfo = (struct c2_processor_node *)
				calloc (1, sizeof(struct c2_processor_node));
			c2_processor_getinfo(cpuid, pinfo);
			c2_list_add(&g_sys_cpus.pss_head, &pinfo->pn_link);

		} /* if - processor is present on the system */

	}/* for - scan all the available processors */

	/*
	 * Change back to previous working dir
	 */
	rc = chdir(cwd);
	C2_ASSERT(rc == 0);
}

/**
   Copy c2_bitmap.

   @param src -> Source bitmap.
   @param dst -> Destination bitmap
 */
static void c2_processors_copy_c2bitmap(const struct c2_bitmap *src,
			       struct c2_bitmap *dst)
{
	size_t	i;
	bool	val;

	C2_ASSERT(dst->b_nr >= src->b_nr);
	C2_ASSERT(dst->b_nr <= g_sys_cpus.pss_max);

	for (i=0; i < src->b_nr; i++) {
		val = c2_bitmap_get(src, i);
		c2_bitmap_set(dst, i, val);

	}/* for - scan the source bitmap */

}

/* ---- Processor Interface Implementation ---- */

/**
   Initialize processors interface. This will allow the interface
   to cache/populate the data, if necessary. The data is cached for
   user mode. The data may not be cached for kernel mode as kernel already
   has the data.

   The calling function should not assume hot-plug CPU facility.
   If the underlying OS supports the hot-plug CPU facility, the calling
   program will have to re-initalize the interface (at least in user-mode)
   after registering for platform specific CPU change notification.

   To re-initalize the interface, c2_processors_fini() must be called first,
   before initializing it again.

   @post Interface initialized.

   Concurrency: The interface should not be initialized twice or simultaneously.
                It's not MT-safe and can be called only once. It can be
                called again after calling c2_processors_fini().
 */
void c2_processors_init()
{
	c2_processors_getsummary();
	g_c2_processor_init = true;
}

/**
   Close the processors interface. This function will destroy any cached data.
   After calling this interface no meaningful data should be assumed.

   Concurrency: Not MT-safe. Assumes no threads are using processor interface.
 */
void c2_processors_fini()
{
	struct c2_list_link *node;

	c2_bitmap_fini(&g_sys_cpus.pss_poss_map, false);
	c2_bitmap_fini(&g_sys_cpus.pss_avail_map, false);
	c2_bitmap_fini(&g_sys_cpus.pss_online_map, false);
	g_sys_cpus.pss_max = 0;

	/*
	 * Remove all the processor nodes.
	 */
	c2_list_for_each(&g_sys_cpus.pss_head, node) {
		c2_list_del(node);
		free(node);
	}
	c2_list_fini(&g_sys_cpus.pss_head);
	g_c2_processor_init = false;
}

/**
   Query if processors interface is initialized.
   @retval true if the interface is initialized
   @retval false if the interface is not initalized.
 */
bool c2_processor_is_initalized(void)
{
	return g_c2_processor_init;
}

/**
   Maximum processors this system can handle.

 */
c2_processor_nr_t c2_processor_nr_max(void)
{
	return g_sys_cpus.pss_max;
}

/**
   Return the bitmap of possible processors.

   @pre map->b_nr >= c2_processor_nr_max()
   @pre c2_processors_init() must be called before calling this function.
   @pre The calling function should allocated memory for 'map' and initialize
        it
 */
void c2_processors_possible(struct c2_bitmap *map)
{
	c2_processors_copy_c2bitmap(&g_sys_cpus.pss_poss_map, map);
}

/**
   Return the bitmap of available processors.

   @pre map->b_nr >= c2_processor_nr_max()
   @pre c2_processors_init() must be called before calling this function.
   @pre The calling function should have allocated memory for 'map' and
        initialize it
 */
void c2_processors_available(struct c2_bitmap *map)
{
	c2_processors_copy_c2bitmap(&g_sys_cpus.pss_avail_map, map);
}

/**
   Return the bitmap of online processors.


   @pre map->b_nr >= c2_processor_nr_max()
   @pre c2_processors_init() must be called before calling this function.
   @pre The calling function should have allocated memory for 'map' and
        initialize it
 */
void c2_processors_online(struct c2_bitmap *map)
{
	c2_processors_copy_c2bitmap(&g_sys_cpus.pss_online_map, map);
}

/**
   Obtain information on the processor with a given id.
   @param id -> id of the processor for which information is requested.
   @param pd -> processor descripto structure. Memory for this should be
                allocated by the calling function. Interface does not allocate
                memory.

   @retval 0 if a matching processor is found
   @retval -EINVAL if id does not match with any of the processors.

   @pre  Memory must be allocated for pd. Interface donot allocated memory.
   @pre c2_processors_init() must be called before calling this function.
   @post d->pd_id == id or none

   Concurrency: This is read only data. Interface by itself does not do
                any locking. When used in kernel-mode, the interface may
                call some functions that may use some kind of locks.
 */
int c2_processor_describe(c2_processor_nr_t id,
			  struct c2_processor_descr *pd)
{
	int	rc=-EINVAL;
	struct c2_processor_node *pinfo;
	struct c2_list_link *node;

	C2_ASSERT(pd != NULL);

	c2_list_for_each(&g_sys_cpus.pss_head, node) {
		pinfo = (struct c2_processor_node *)node;
		if (pinfo->pn_info.pd_id == id) {
			*pd = pinfo->pn_info;
			rc = 0;
			break;
		}/* if - matching CPU id found */

	}/* for - iterate over all the processor nodes */

	return rc;
}

/**
   Return the id of the processor on which the calling thread is running.
   If the call is not supported return -1.

   @retval logical processor id (as supplied by the system) on which the
           calling thread is running, if the call is uspported.
           It will return -1, if this call is not supported.

   @note At this point in time this call requires glibc 2.6. Until we
         standardize on tools, this call cannot be implented as that may
         break the build.
 */
int c2_processor_getcpu(void)
{
	return -1;
}

/** @} end of processor group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

