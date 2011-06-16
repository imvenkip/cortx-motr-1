#include <stdio.h>
#include <stdlib.h>
#include "rpc/rpccore.h" 
#include "rpc/session.h"
#include "lib/list.h"
#include "lib/mutex.h"
#include "lib/rwlock.h"
#include "lib/refs.h"
#include "lib/errno.h"
#include "lib/cdefs.h"
#include "lib/memory.h"

/* Implementation routines of rpc-grouping interfaces. Lots of stubs currently
and some of them will be implemented during RPC integration task 
Some of the TBD items :
(a) Proper locking interfaces
(b) Ref count handling
(c) Handling of endpoints - currently using c2_net_endpoint
(d) Timeouts - currently using c2_time
(e) Integration of callbacks and other interfaces with RPC-formation
(f) Asserts and error handling.
*/

#ifdef RPC_GRP_DEBUG
uint32_t generate_rand_timeout()
{
        int rand_num;
        seed_val = seed_val + (unsigned int)time(NULL);
        srand(seed_val);
        rand_num = rand();
        return (uint32_t)(rand_num % 20);
}

uint32_t generate_rand_endpoint()
{
        uint32_t endpoint;
        endpoint = (generate_rand_timeout() % NO_OF_ENDPOINTS);
        if(endpoint)
                return endpoint;
        return NO_OF_ENDPOINTS;
}
#endif

struct c2_net_endpoint *c2_get_item_endpoint(struct c2_update_stream *us)
{
	/*TBD once proper definition of endpoint is available */
#ifdef RPC_GRP_DEBUG
	struct c2_net_endpoint *endpoint;
	C2_ALLOC_PTR(endpoint);
	if(us) {
		/*Submit request for a group, currently assuming they would
		be sent to the same endpoint ( some random value currently ) */
		endpoint->endpoint_val = 32;	
		
	}
	else
		endpoint->endpoint_val = generate_rand_endpoint();
	return endpoint;
#endif	
	return NULL;
}

/*
  Place holder. Dont know if this would be necessary.
Will be implemented once the RPC core module is ready

static struct c2_rpc_machine* c2_get_rpc_machine( )
{
	return NULL;
}
*/

/* TBD once endpoint structures are well defined, will return true when endpoints are equal */
static bool c2_rpc_are_endpoints_same(struct c2_net_endpoint *endpoint1, 
				      struct c2_net_endpoint *endpoint2)
{
#ifdef RPC_GRP_DEBUG
	return(endpoint1->endpoint_val == endpoint2->endpoint_val);
#endif
/*	memcmp(endpoint1, endpoint2) ?? */
	return true;
}

/* Return the head formation list for a specific endpoint
    TBD : Locking interfaces */ 
static struct c2_rpc_formation_list *c2_get_form_list(struct c2_rpcmachine 
						*rpc_machine, struct
						c2_net_endpoint *endpoint)
{
 	struct c2_rpc_processing 	*rpc_processing;
	struct c2_rpc_formation_list 	*rpc_formation_list, *return_val;	

	return_val = NULL;
	rpc_processing = &rpc_machine->cr_processing;
	/*Iterate through the various lists and check if formation list
	  for the endpoint exists */
	/* Lock the crp_formation_list */
	c2_list_for_each_entry(&rpc_processing->crp_formation_lists,
                               rpc_formation_list,
                               struct c2_rpc_formation_list,
                               re_linkage) {
		if(c2_rpc_are_endpoints_same(rpc_formation_list->endpoint,
					 endpoint)) {
			return_val = rpc_formation_list;
			break;
		}
	}
	/*Unlock the crp_formation_list*/
	return return_val;
}

static int c2_rpc_change_item_param(struct c2_rpc_item *item, 
				    enum c2_rpc_item_priority prio,
			             const c2_time_t *deadline)
{
	/* TBD, might need to invoke a function from rpc formation */
	return 0;
}

static int c2_rpc_item_submitted(struct c2_rpc_item *item)
{
	/*TBD, Notify formation that the item has been submitted 
	  to the items cache */
	return 0;
}
static bool is_more_urgent(const c2_time_t t1, const c2_time_t t2)
{
	return(t1 < t2);
}

/** Create new formation cache for an endpoint if it doesnt exist */
static struct c2_rpc_formation_list *c2_create_new_formation_list(struct c2_rpcmachine 
				    *rpc_machine, struct c2_net_endpoint *endpoint)
{
	struct c2_rpc_formation_list *rpc_formation_list;
	struct c2_rpc_processing     *rpc_processing_context;
	
	rpc_processing_context = &rpc_machine->cr_processing;
	C2_ALLOC_PTR(rpc_formation_list);
	rpc_formation_list->endpoint = endpoint;
	c2_list_init(&rpc_formation_list->re_items);
	c2_mutex_init(&rpc_formation_list->re_guard);
	c2_list_add(&rpc_processing_context->crp_formation_lists,
                         &rpc_formation_list->re_linkage);
	return rpc_formation_list;
}
/** Insert an rpc item into a rpc item list sorted by deadlines.
    Currently insertion sort is used, but this will be replaced
    by a more efficient sorting algorithm/data-structure in the future.*/
static int c2_rpc_insert_item_form_list(struct c2_rpc_formation_list
					*rpc_formation_list,
					struct c2_rpc_item *new_item)
{
	struct c2_rpc_item 	*crt_item;
	/*Acquire the lock on rpc_formation_list */
	c2_mutex_lock(&rpc_formation_list->re_guard);
	c2_list_for_each_entry(&rpc_formation_list->re_items, crt_item,
                               struct c2_rpc_item, ri_linkage) {
		if(is_more_urgent(new_item->ri_deadline, crt_item->ri_deadline))
			break;
	}
	c2_list_add_before(&crt_item->ri_linkage, &new_item->ri_linkage);
	/*Release the lock */
	c2_mutex_unlock(&rpc_formation_list->re_guard);
	return 0;
}
	
int c2_rpc_submit(struct c2_service_id          *srvid,
                  struct c2_update_stream       *us,
                  struct c2_rpc_item            *item,
                  enum c2_rpc_item_priority     prio,
                  const c2_time_t               *deadline)
{
	struct c2_net_endpoint 		*endpoint;
	struct c2_rpc_formation_list    *rpc_formation_list;	
	struct c2_rpcmachine		*rpc_machine;
	struct c2_mutex			*guard;

	/*Placeholder for lock/mutex on rpc item, probably not needed */
	item->ri_state = RPC_ITEM_IN_USE;
	c2_ref_get(&item->ri_ref);
	item->ri_prio = prio;
	item->ri_deadline = *deadline;
	/* Placeholder for unlock */

	/*Check if the request is for modifying the fields in the rpc
	  item or for submitting a new item */
	if(c2_list_link_is_in(&item->ri_linkage))
	{
		return(c2_rpc_change_item_param(item, prio, deadline));
	}
	
	endpoint = c2_get_item_endpoint(us);
#ifdef RPC_GRP_DEBUG
	//printf("\nEndpoint for item : %d", endpoint->endpoint_val);
#endif
	/* Get the rpc processing context, need to confirm if this is 
	   correct */
	rpc_machine = item->ri_mach;
	guard = &rpc_machine->cr_processing.crp_guard;
	/* Get the items cache for that endpoint, create one if it 
	   doesn,t exists */
	c2_mutex_lock(guard);
	rpc_formation_list = c2_get_form_list(rpc_machine, endpoint);
	if(rpc_formation_list == NULL)
	{
		rpc_formation_list = c2_create_new_formation_list(rpc_machine, endpoint);		
	}
	c2_mutex_unlock(guard);
	/*Insert the item to the items_cache for the endpoint */
	c2_rpc_insert_item_form_list(rpc_formation_list, item);
	/*Notify the formation component that an item has been added */
	item->ri_state = RPC_ITEM_SUBMITTED;
	return(c2_rpc_item_submitted(item));
}

int c2_rpc_reply_submit(struct c2_rpc_item	*request,
			struct c2_rpc_item	*reply,
			struct c2_db_tx		*tx)
{
	printf("reply_submit: %p %p\n", request, reply);
	return 0;
}
int c2_rpc_cancel(struct c2_rpc_item *item)
{
	return 0;
}

int c2_rpc_group_open(struct c2_rpcmachine *machine,
		      struct c2_rpc_group **group)
{
	struct c2_rpc_group 	*new_rpc_group;
	
	C2_ALLOC_PTR(new_rpc_group);
	/*Initialize all the members of the group */
	new_rpc_group->rg_mach = machine;
	c2_list_init(&new_rpc_group->rg_items);
	c2_mutex_init(&new_rpc_group->rg_guard);
	c2_chan_init(&new_rpc_group->rg_chan);	
	*group = new_rpc_group;
	return 0;
}



int c2_rpc_group_close(struct c2_rpcmachine *machine, struct c2_rpc_group *group)
{
	return 0;
}

/** Insert items of the same group in the list. This list is sorted based on priorities in 
ascending order */
static void c2_rpc_add_group_items_list(struct c2_rpc_group *group,
					struct c2_rpc_item *new_item,
					enum  c2_rpc_item_priority prio)
{
	struct c2_rpc_item *crt_item;

	 c2_list_for_each_entry(&group->rg_items, crt_item,
                               struct c2_rpc_item, ri_group_linkage) {
		if(prio > crt_item->ri_prio)
			break;
	}
	c2_list_add_before(&crt_item->ri_group_linkage, &new_item->ri_group_linkage);
}

int c2_rpc_group_submit(struct c2_rpc_group *group,
			struct c2_rpc_item *item,
			struct c2_update_stream *us,
			enum c2_rpc_item_priority prio,
			 const c2_time_t *deadline)
{
	
	struct c2_service_id          *srvid = NULL;

	c2_mutex_lock(&group->rg_guard);
	group->rg_expected++;
	item->ri_group = group;
	/* Add the item to the rg_items list which will be sorted
	   based in priorities */
	c2_rpc_add_group_items_list(group, item, prio);

	c2_rpc_submit(srvid, us, item, prio, deadline);
	c2_mutex_unlock(&group->rg_guard);
	return 0;
}

