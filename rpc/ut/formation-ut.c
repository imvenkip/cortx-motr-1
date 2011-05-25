#include "rpc/formation.h"
#include "stob/ut/io_fop.h"
#include <stdlib.h>

/*
   *** Current scenario ***
   1. The rpc core component is not ready completely. So this UT tries to
      simulate the things which are not available.
   2. The RPC Formation component 

   *** Requirements for UT of formation component. ***
   1. Simulate the end user access pattern.[C2_RPC_FORM_ACCESS_PATERN]
   2. Simulate minimalistic meta data traffic.[C2_RPC_FORM_MD_TRAFFIC]
   3. Simulate sufficient IO traffic to stress all corner cases.
      Ensure multiple requests on same files, multiple IO requests
      on same/different files in same groups so as to stress all
      corner cases. [C2_RPC_FORM_IO_TRAFFIC]
   4. Handle all memory management.[C2_RPC_FORM_MEM_MGMT]
   5. Write maximum asserts to check behavior of component.[C2_RPC_FORM_ASSERTS]
   6. Display statistics on rpc formation.[C2_RPC_FORM_STATS]

   *** A concise design for UT of RPC Formation. ***
   1. Consider a flat hierarchy of a number of files.
   2. These files will be created first.
   3. A certain set of files will be picked up for IO.
   4. All files will be put in some group and there will be sufficient
      multiple IO requests on same files.
   5. Create FOPs for these requests (metadata/IO), assign
 */

uint64_t			c2_rpc_max_message_size;
uint64_t			c2_rpc_max_fragments_size;
uint64_t			c2_rpc_max_rpcs_in_flight;

static int			 nthreads = 8;
static struct c2_thread		 form_ut_threads;

static int			 nfops = 256;
static c2_fop			*form_fops;

static int			 nrpcgroups = 16;
static struct c2_rpc_group	*form_groups;

static int			 nfiles = 64;
static c2_fop_fid		*form_fids;

static void form_ut_thread_init(int a)
{
#ifndef __KERNEL__
	printf("Thread id %d initialized.\n", form_ut_threads[a].t_h.h_id);
#endif
	/* Call its function. */
}

struct c2_fop_fid *form_get_fid(int i)
{
	C2_ASSERT(i < nfiles);
	return form_fids[i];
}

struct c2_rpc_group *form_get_rpcgroup(int i)
{
	C2_ASSERT(i < nrpcgroups);
	return form_groups[i];
}

struct c2_fop *form_create_file_create_fop()
{
	int 				 i = 0;
	struct c2_fop			*fop = NULL;
	struct c2_fop_file_create	*create_fop = NULL;

	fop = c2_fop_alloc(&c2_fop_file_create_fopt, NULL);
	if (fop == NULL) {
		printf("Failed to allocate struct c2_fop.\n");
		return NULL;
	}
	create_fop = c2_fop_data(fop);
	i = (rand()) % nfiles;
	create_fop->fcr_fid = form_get_fid(i);
	return fop;
}

struct c2_fop_io_vec *form_get_new_iovec(int i)
{
	struct c2_fop_io_vec		*iovec = NULL;

	C2_ASSERT(i < nfiles);
	C2_ALLOC_PTR(iovec);
	if (iovec == NULL) {
		printf("Failed to allocate memory for struct c2_fop_io_vec.\n");
		return NULL;
	}
	for () {
	}
}

struct c2_fop *form_create_write_fop()
{
	int				 i = 0;
	struct c2_fop			*fop = NULL;
	struct c2_fop_cob_writev	*write_fop = NULL;

	fop = c2_fop_alloc(&c2_fop_cob_writev_fopt, NULL);
	if (fop == NULL) {
		printf("Failed to allocate struct c2_fop.\n");
		return NULL;
	}
	write_fop = c2_fop_data(fop);
	i = (rand()) % nfiles;
	write_fop->fwr_fid = form_get_fid(i);
	write_fop->fwr_iovec = form_get_new_iovec(i);;
}

struct c2_fop *form_create_write_fop()
{
	int				 i = 0;
	struct c2_fop			*fop = NULL;
	struct c2_fop_cob_readv		*read_fop = NULL;

	fop = c2_fop_alloc(&c2_fop_cob_readv_fopt, NULL);
	if (fop == NULL) {
		printf("Failed to allocate struct c2_fop.\n");
		return NULL;
	}
}

int main(int argc, char **argv)
{
	int		result = 0;
	int		grp_no = 0;
	int		file_no = 0;

	result = io_fop_init();
	C2_ASSERT(result == 0);

	/*
	 1. Initialize the thresholds like max_message_size, max_fragements
	    and max_rpcs_in_flight.*/

	/* Lustre limits the rpc size(actually the number of pages in rpc)
	   by the MTU(Max Transferrable Unit) of LNET which is defined
	   to be 1M. !! Not sure of this is right !! */
	c2_rpc_max_message_size = 1024*1024;
	/* We start with a default value of 8. The max value in Lustre, is
	   limited to 32. */
	c2_rpc_max_rpcs_in_flight = 8;
	c2_rpc_max_fragements_size = ;
	
	/* 2. Create a pool of threads so this UT can be made multi-threaded.*/
	C2_SET_ARR0(&form_ut_threads);
	for (i = 0; i < nthreads; i++) {
		result = C2_THREAD_INIT(&form_ut_threads[i], int, NULL,
				&form_ut_thread_init, i);
		C2_ASSERT(result == 0);
	}
	/* 3. Create a number of meta-data and IO FOPs. For IO, decide the 
	    number of files to operate upon. Decide how to assign items to 
	    rpc groups and have multiple IO requests within or across groups.*/
	/* Init the fid structures and rpc groups. */
	C2_ALLOC_ARR(form_fids, nfiles);
	if (form_fids == NULL) {
		printf("Failed to allocate memory for array of struct \
				c2_fop_file_fid.\n");
		return -1;
	}

	C2_ALLOC_ARR(form_groups, nrpcgroups);
	if (form_groups == NULL) {
		printf("Failed to allocate memory for array of struct \
				c2_rpc_group.\n");
		return -1;
	}

	for (f = 0; f < nfops; f++) {
		/* Total random data. */
		/* 1. Create a FOP of type "create". */

	}
	/* 4. Populate the associated rpc items.
	 5. Assign priority and timeout for all rpc items. The thumb rule
	    for this is - meta data FOPs should have higher priority and
	    shorted timeout while IO FOPs can have lower priority than
	    meta data FOPs and relatively larger timeouts.
	 6. Assign a thread each time from the thread pool to do the 
	    rpc submission. This will give ample opportunity to test the
	    formation algorithm in multi threaded environment.
	 7. Simulate necessary behavior of grouping component.
	 8. This will trigger execution of formation algorithm. 
	 9. Grab output produced by formation algorithm and analyze the
	    statistics.
	 */
	for (i = 0; i < nthreads; i++) {
		c2_thread_join(&form_ut_threads[i]);
		c2_thread_fini(&form_ut_threads[i]);
	}
	return 0;
}

