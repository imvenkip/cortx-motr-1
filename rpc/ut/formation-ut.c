#include "rpc/formation.h"
#include "rpc/rpccore.h"
#include "stob/ut/io_fop.h"
#include "colibri/init.h"
#include "lib/cdefs.h"
#include "lib/memory.h"

struct c2_rpc_form_items_cache *input_cache;

/**
  Alloc and initialize the items cache
 */
struct c2_rpc_form_items_cache *c2_rpc_form_item_cache_create(void)
{
	input_cache = c2_alloc(sizeof(struct c2_rpc_form_items_cache));
	if(input_cache == NULL){
		return NULL;
	}
	c2_mutex_init(&input_cache->ic_mutex);
	c2_list_init(&input_cache->ic_cache_list);
	return input_cache;
}

/**
  Insert a rpc item to the cache based on sorted deadline value
 */
int c2_rpc_form_item_cache_insert(struct c2_rpc_form_items_cache *cache,
		struct c2_rpc_item *item)
{
	C2_PRE(cache != NULL);	
	C2_PRE(item !=NULL);
	return 0;
}

/**
  Assign a group to a given RPC item
 */
int c2_rpc_form_item_assign_group(struct c2_rpc_item *item,
		struct c2_rpc_group *grp)
{
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
	C2_PRE(item !=NULL);

	item->ri_deadline = deadline;
	return 0;
}

/**
  Assign a priority to a given RPC item
 */
int c2_rpc_form_item_assign_prio(struct c2_rpc_item *item, const int prio)
{
	C2_PRE(item !=NULL);

	item->ri_prio = prio;
	return 0;
}

/**
  Insert an rpc item to the global input cache such that it is sorted
  according to timeout
  */
int c2_rpc_form_add_rpc_to_cache(struct c2_rpc_item *item)
{
	struct c2_rpc_item	*rpc_item;
	struct c2_rpc_item	*rpc_item_next;
	bool			 item_inserted = false;
	C2_PRE(item != NULL);

	c2_mutex_lock(&input_cache->ic_mutex);
	c2_list_for_each_entry_safe(&input_cache->ic_cache_list, 
			rpc_item, rpc_item_next,
			struct c2_rpc_item, ri_linkage){
		if (item->ri_deadline < rpc_item->ri_deadline) {
			c2_list_add_before(&rpc_item->ri_linkage, &item->ri_linkage);
			item_inserted = true;
			break;
		}

	}
	if(!item_inserted) {
		c2_list_add(&input_cache->ic_cache_list, &item->ri_linkage);
	}
	c2_mutex_unlock(&input_cache->ic_mutex);

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
int main(int argc, char **argv)
{
	int result = 0;

	result = c2_init();
	C2_ASSERT(result == 0);

	result = io_fop_init();
	C2_ASSERT(result == 0);

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
	return 0;
}

