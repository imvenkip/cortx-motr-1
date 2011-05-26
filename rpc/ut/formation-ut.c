#include "rpc/formation.h"
#include "rpc/rpccore.h"
#include "stob/ut/io_fop.h"
#include "colibri/init.h"
#include "lib/cdefs.h"
#include "lib/memory.h"

/* Some random deadline values for testing purpose only */
#define MIN_NONIO_DEADLINE	0 		// 0 ms
#define MAX_NONIO_DEADLINE	10000000	// 10 ms 
#define MIN_IO_DEADLINE		10000000  	// 10 ms
#define MAX_IO_DEADLINE		100000000 	// 100 ms

/* Some random priority values for testing purpose only */
#define MIN_NONIO_PRIO		0
#define MAX_NONIO_PRIO		5
#define MIN_IO_PRIO		6
#define MAX_IO_PRIO 		10

/* Array of groups */
#define MAX_NONIO_GRPS		2
struct c2_rpc_group		*rgroup_nonio[MAX_NONIO_GRPS];
#define MAX_IO_GRPS		5
struct c2_rpc_group		*rgroup_io[MAX_IO_GRPS];

/**
  Alloc and initialize the global array of groups used for UT
 */
int c2_rpc_form_groups_alloc(void)
{
	int		i = 0;
	printf("Inside c2_rpc_form_groups_alloc \n");

	for(i = 0; i < MAX_NONIO_GRPS; i++) {
		rgroup_nonio[i] = c2_alloc(sizeof(struct c2_rpc_group));
		if(rgroup_nonio[i] == NULL) {
			return -1;
		}
		c2_list_init(&rgroup_nonio[i]->rg_items);
		c2_mutex_init(&rgroup_nonio[i]->rg_guard);
		rgroup_nonio[i]->rg_expected = 0;
		rgroup_nonio[i]->nr_residual = 0;
	}
	for(i = 0; i < MAX_IO_GRPS; i++) {
		rgroup_io[i] = c2_alloc(sizeof(struct c2_rpc_group));
		if(rgroup_io[i] == NULL) {
			return -1;
		}
		c2_list_init(&rgroup_io[i]->rg_items);
		c2_mutex_init(&rgroup_io[i]->rg_guard);
		rgroup_io[i]->rg_expected = 0;
		rgroup_io[i]->nr_residual = 0;
	}
	return 0;
}

/**
  Deallocate the global array of groups used in UT
 */
int c2_rpc_form_groups_free(void)
{
	int		i = 0;
	printf("Inside c2_rpc_form_groups_free \n");

	for(i = 0; i < MAX_NONIO_GRPS; i++) {
		c2_list_fini(&rgroup_nonio[i]->rg_items);
		c2_mutex_fini(&rgroup_nonio[i]->rg_guard);
		c2_free(rgroup_nonio[i]);
	}
	for(i = 0; i < MAX_IO_GRPS; i++) {
		c2_list_fini(&rgroup_io[i]->rg_items);
		c2_mutex_fini(&rgroup_io[i]->rg_guard);
		c2_free(rgroup_io[i]);
	}
	return 0;
}

/**
  Alloc and initialize the items cache
 */
int c2_rpc_form_item_cache_init(void)
{
	printf("Inside c2_rpc_form_item_cache_init \n");

	items_cache = c2_alloc(sizeof(struct c2_rpc_form_items_cache));
	if(items_cache == NULL){
		return -1;
	}
	c2_mutex_init(&items_cache->ic_mutex);
	c2_list_init(&items_cache->ic_cache_list);
	return 0;
}

/**
  Deallocate the items cache
 */
void c2_rpc_form_item_cache_fini(void)
{
	printf("Inside c2_rpc_form_item_cache_fini \n");
	c2_mutex_fini(&items_cache->ic_mutex);
	c2_list_fini(&items_cache->ic_cache_list);
	c2_free(items_cache);
}

/**
  Assign a group to a given RPC item
 */
int c2_rpc_form_item_assign_group(struct c2_rpc_item *item,
		struct c2_rpc_group *grp)
{
	printf("Inside c2_rpc_form_item_assign_group \n");
	C2_PRE(item !=NULL);

	item->ri_group = grp;
	return 0;
}

/**
  Assign a deadline to a given RPC item
 */
int c2_rpc_form_item_assign_deadline(struct c2_rpc_item *item,
		c2_time_t deadline)
{
	printf("Inside c2_rpc_form_item_assign_deadline \n");
	C2_PRE(item !=NULL);

	item->ri_deadline = deadline;
	return 0;
}

/**
  Assign a priority to a given RPC item
 */
int c2_rpc_form_item_assign_prio(struct c2_rpc_item *item, const int prio)
{
	printf("Inside c2_rpc_form_item_assign_prio \n");
	C2_PRE(item !=NULL);

	item->ri_prio = prio;
	return 0;
}

/**
  Insert an rpc item to the global items cache such that it is sorted
  according to timeout
  */
int c2_rpc_form_item_add_to_cache(struct c2_rpc_item *item)
{
	struct c2_rpc_item	*rpc_item;
	struct c2_rpc_item	*rpc_item_next;
	bool			 item_inserted = false;

	printf("Inside c2_rpc_form_add_rpc_to_cache \n");
	C2_PRE(item != NULL);

	c2_mutex_lock(&items_cache->ic_mutex);
	c2_list_for_each_entry_safe(&items_cache->ic_cache_list, 
			rpc_item, rpc_item_next,
			struct c2_rpc_item, ri_linkage){
		if (item->ri_deadline < rpc_item->ri_deadline) {
			c2_list_add_before(&rpc_item->ri_linkage, &item->ri_linkage);
			item_inserted = true;
			break;
		}

	}
	if(!item_inserted) {
		c2_list_add_after(&rpc_item->ri_linkage, &item->ri_linkage);
	}
	item->ri_state = RPC_ITEM_SUBMITTED;
	c2_mutex_unlock(&items_cache->ic_mutex);

	return 0;
}

/**
  Populate the rpc item parameters specific to IO FOPs
 */
int c2_rpc_form_item_io_populate_param(struct c2_rpc_item *item)
{
	int		prio;
	c2_time_t	deadline;

	printf("Inside c2_rpc_form_item_io_populate_param \n");
	C2_PRE(item != NULL);

	prio = rand() % MAX_IO_PRIO + MIN_IO_PRIO;
	c2_rpc_form_item_assign_prio(item, prio);
	deadline = rand() % (MAX_IO_DEADLINE-1) + MIN_IO_DEADLINE;
	c2_rpc_form_item_assign_deadline(item, deadline);

	return 0;
}

/**
  Populate the rpc item parameters specific to Non-IO FOPs
 */
int c2_rpc_form_item_nonio_populate_param(struct c2_rpc_item *item)
{
	int		prio;
	c2_time_t	deadline;

	printf("Inside c2_rpc_form_item_nonio_populate_param \n");
	C2_PRE(item != NULL);

	prio = rand() % MAX_NONIO_PRIO + MIN_NONIO_PRIO;
	c2_rpc_form_item_assign_prio(item, prio);
	deadline = rand() % MAX_NONIO_DEADLINE + MIN_NONIO_DEADLINE;
	c2_rpc_form_item_assign_deadline(item, deadline);

	item->ri_group = NULL;

	return 0;
}

/**
  Populate the rpc item parameters based on the FOP type
 */
int c2_rpc_form_item_populate_param(struct c2_rpc_item *item)
{
	bool 		io_req = false;
	int		res = 0;

	printf("Inside c2_rpc_form_item_populate_param \n");
	C2_PRE(item != NULL);
	
	io_req = c2_rpc_item_is_io_req(item);
	if(io_req) {
		res = c2_rpc_form_item_io_populate_param(item);
		C2_ASSERT(res!=0);
	}
	else {
		res = c2_rpc_form_item_nonio_populate_param(item);
		C2_ASSERT(res!=0);
	}
	return 0;
}

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

void c2_rpc_test_single_item_addition()
{
	int			 result = 0;
	struct c2_rpc_item	*item;

	item = c2_alloc(sizeof(struct c2_rpc_item));

	/*
	 1. Initialize the threasholds like max_message_size, max_fragements
	    and max_rpcs_in_flight.
	 2. Create a pool of threads so this UT can be made multi-threaded.
	 3. Create a number of meta-data and IO FOPs. For IO, decide the 
	    number of files to opeate upon. Decide how to assign items to 
	    rpc groups and have multiple IO requests within or across groups.
	 4. Populate the associated rpc items.
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

	result = c2_rpc_form_init();
	C2_ASSERT(result == 0);

	result = c2_rpc_form_item_cache_init();
	C2_ASSERT(result == 0);

	/**
	  XXX Call FOP creation and population routines here. Extract rpc item 
	  from the fop.
	 */

	result = c2_rpc_form_item_nonio_populate_param(item);	
	C2_ASSERT(result == 0);

	result = c2_rpc_form_item_add_to_cache(item);
	C2_ASSERT(result == 0);

	result = c2_rpc_form_extevt_rpcitem_added_in_cache(item);
	C2_ASSERT(result == 0);

	result = c2_rpc_form_init();
	C2_ASSERT(result == 0);

}

int main(int argc, char **argv)
{

	/*
	 1. Initialize the threasholds like max_message_size, max_fragements
	    and max_rpcs_in_flight.
	 2. Create a pool of threads so this UT can be made multi-threaded.
	 3. Create a number of meta-data and IO FOPs. For IO, decide the 
	    number of files to opeate upon. Decide how to assign items to 
	    rpc groups and have multiple IO requests within or across groups.
	 4. Populate the associated rpc items.
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


	c2_rpc_test_single_item_addition();

	return 0;
}
