#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>  /* mkdir */
#include <sys/types.h> /* mkdir */
#include <unistd.h>    /* sleep */

#include "lib/assert.h"
#include "lib/memory.h"
#include "stob/stob.h"
#include "fop/fop.h"
#include "fop/fom.h"
#include "fop/fop_iterator.h"
#include "dtm/dtm.h"

#include "reqh.h"

/**
   @addtogroup reqh
   @{
 */

/** 
 * Function to initialize request handler and fom domain
*/
int  c2_reqh_init(struct c2_reqh *reqh,
		  struct c2_rpcmachine *rpc, struct c2_dtm *dtm,
		  struct c2_stob_domain *dom, struct c2_fol *fol, struct c2_service *serv)
{
	C2_ASSERT(reqh != NULL);
	int result = 0;
	size_t nr = 0;

	reqh->rh_rpc = rpc;
	reqh->rh_dtm = dtm;
	reqh->rh_dom = dom;
	reqh->rh_fol = fol;
	reqh->rh_serv = serv;

	result = c2_fom_domain_init(&reqh->rh_fom_dom, nr);
	if (result && !reqh->rh_fom_dom) {
		printf("reqh: c2_fom_domain_init failed with result %d\n", result);
		return 1;
	}

	return result;
}

void c2_reqh_fini(struct c2_reqh *reqh)
{
	C2_ASSERT(reqh != NULL);
	C2_ASSERT(reqh->rh_fom_dom);
	c2_fom_domain_fini(reqh->rh_fom_dom);
        fclose((FILE*)c2_addb_store_stob);
}

/** 
 * Function to accept fop and create corresponding fom 
 * and submit it for further processing.
*/
void c2_reqh_fop_handle(struct c2_reqh *reqh, struct c2_fop *fop, void *cookie)
{
	C2_ASSERT(reqh != NULL);
	C2_ASSERT(fop != NULL);
	C2_ASSERT(cookie != NULL);

	struct c2_fom *fom = NULL;
        struct c2_fop_ctx       *ctx;
        int                     result = 0;
		
	/** initialize database **/
	ctx = c2_alloc(sizeof *ctx);
	ctx->fc_cookie = cookie;
        ctx->ft_service = reqh->rh_serv;

	/*
	 * Initialize fom for fop processing
	 */
	result = fop->f_type->ft_ops->fto_fom_init(fop, &fom);
	C2_ASSERT(fom != NULL);

	fom->fo_fop_ctx = ctx;
	fom->fo_fol = reqh->rh_fol;
	fom->fo_stdom = reqh->rh_dom;
	c2_fom_init(fom);
	C2_ASSERT(reqh->rh_fom_dom != NULL);

	/* 	 
	 * find locality and submit fom for further processing
	 */
	printf("reqh: fom enqueued for execution\n");		
	c2_fom_queue(reqh->rh_fom_dom, fom);
}

void c2_reqh_fop_sortkey_get(struct c2_reqh *reqh, struct c2_fop *fop,
			     struct c2_fop_sortkey *key)
{
}

/**
 * Function to authenticate fop.
 */
int c2_fom_authen(struct c2_fom *fom)
{
	C2_ASSERT(fom != NULL);
	fom->fo_phase = FOPH_AUTHENTICATE_WAIT;
	return 0;
}

/**
 * Function to identify local resources
 * required for fop execution.
 */
int c2_fom_loc_resource(struct c2_fom *fom)
{
	C2_ASSERT(fom != NULL);
	/*fom->fo_phase = FOPH_RESOURCE_LOCAL_WAIT;*/
	return 0;
}

/** 
 * Function to identify distributed resources
 * required for fop execution.
 */
int c2_fom_dist_resource(struct c2_fom *fom)
{
	C2_ASSERT(fom != NULL);
	fom->fo_phase = FOPH_RESOURCE_DISTRIBUTED_WAIT;
	return 0;
}

/**
 * Function to locate and load file
 * system object, required for fop
 * execution.
 */
int c2_fom_obj_check(struct c2_fom *fom)
{
	C2_ASSERT(fom != NULL);
	/*fom->fo_phase = FOPH_OBJECT_CHECK_WAIT;*/
	return 0;
}

/**
 * Function to authorise fop in fom
 */
int c2_fom_auth(struct c2_fom *fom)
{
	C2_ASSERT(fom != NULL);
	fom->fo_phase = FOPH_AUTHORISATION_WAIT;
	return 0;
}

/**
 * Funtion to create local transactional context.
 */
int c2_create_loc_ctx(struct c2_fom *fom)
{	
  	C2_ASSERT(fom != NULL);
	int rc = 0;
	rc = c2_fop_fol_rec_add(fom->fo_fop, fom->fo_fol, 
	                        &fom->fo_tx.tx_dbtx);
	return 0;				
}
/**
 * Function to handle generic operations of fom
 * like authentication, authorisation, acquiring resources, &tc
*/
int c2_fom_state_generic(struct c2_fom *fom)
{
	C2_ASSERT(fom != NULL);
	int rc = 0;
 			
	switch(fom->fo_phase) {
	case FOPH_INIT:
		/** Start with fop authentication **/
		fom->fo_phase = FOPH_AUTHENTICATE;
	 
		rc = c2_fom_authen(fom);
		if (fom->fo_phase == FOPH_AUTHENTICATE_WAIT) {
			/* Operation is blocking,
			 * register fom with the wait channel.
			 */
			printf("FOM: waiting for authentication..\n");
			c2_fom_block_at(fom, &fom->chan_gen_wait);
			break;
		} else
		if (fom->fo_phase == FOPH_FAILED)
			/* fop failed in generic phase, return the 
			 * error code in reply fop.
			 */
			 break;
	case FOPH_AUTHENTICATE:
		/* If done with authentication phase proceed with
		 * acquiring local resource information, change phase
		 * to FOPH_RESOURCE_LOCAL.
		 */
		fom->fo_phase = FOPH_RESOURCE_LOCAL;

		/* collect local resources information for this fom */
		rc = c2_fom_loc_resource(fom);
		if (fom->fo_phase == FOPH_RESOURCE_LOCAL_WAIT) {
			/* Operation is blocking,
			 * register fom with the wait channel.
			 */ 
			printf("FOM: waiting to acquire local resources\n");
			c2_fom_block_at(fom, &fom->chan_gen_wait);
			break;
		} else
		if (fom->fo_phase == FOPH_FAILED)
			/* fop failed in generic phase, return the 
			 * error code in reply fop.
			 */
			 break;
	case FOPH_RESOURCE_LOCAL:
		/* If done with acquiring local resource information, proceed with 
		 * acquiring distributed resource information, change phase to
		 * FOPH_RESOURCE_DISTRIBUTED. 
		 */
		fom->fo_phase = FOPH_RESOURCE_DISTRIBUTED;

		/* collect distributed resources information for this fom */
		rc = c2_fom_dist_resource(fom);
		if (fom->fo_phase == FOPH_RESOURCE_DISTRIBUTED_WAIT) {
			/* Operation is blocking,
			 * register fom with the wait channel.
			 */ 
			printf("FOM: waiting to acquire distributed resources \n");
			c2_fom_block_at(fom, &fom->chan_gen_wait);
			break;
		} else
		if (fom->fo_phase == FOPH_FAILED)
			/* fop failed in generic phase, return the 
			 * error code in reply fop.
			 */
			 break;
	case FOPH_RESOURCE_DISTRIBUTED:
		/* If done with acquiring distributed resource information, 
		 * proceed with locating effective file system objects, 
		 * and loading them, change phase to FOPH_OBJECT_CHECK.
		 */
		fom->fo_phase = FOPH_OBJECT_CHECK;

		/* Locate and load various effective file system objects */
		rc = c2_fom_obj_check(fom);
		if (fom->fo_phase == FOPH_OBJECT_CHECK_WAIT) {
			/* Operation is blocking,
			 * register fom with the wait channel.
			 */ 
			printf("FOM: waiting for object check\n");
			c2_fom_block_at(fom, &fom->chan_gen_wait);
			break;
		} else
		if (fom->fo_phase == FOPH_FAILED)
			/* fop failed in generic phase, return the 
			 * error code in reply fop.
			 */
			 break;
	case FOPH_OBJECT_CHECK:
		/* If done with object checking, proceed to fop authorisation,
		 * change phase to FOPH_AUTHORISATION.
		 */
		fom->fo_phase = FOPH_AUTHORISATION;
		
		/* Authorise fop */
		rc = c2_fom_auth(fom);
		if (fom->fo_phase == FOPH_AUTHORISATION_WAIT) {
			/* Operation is blocking,
			 * register fom with the wait channel.
			 */ 
			printf("FOM: waiting for authorisation\n");
			c2_fom_block_at(fom, &fom->chan_gen_wait);
			break;
		} else
		if (fom->fo_phase == FOPH_FAILED)
			/* fop failed in generic phase, return the 
			 * error code in reply fop.
			 */
			 break;
	case FOPH_AUTHORISATION:
		/* If done with fop authorisation, proceed to fop 
		 * execution, change phase to FOPH_EXEC.
		 */ 
		fom->fo_phase = FOPH_EXEC;
		break;
	case FOPH_TXN_CONTEXT:
		/* create local transactional context for this fom */
		rc = c2_create_loc_ctx(fom);
		if (fom->fo_phase == FOPH_TXN_CONTEXT_WAIT) {
			/* Operation is blocking,
			 * register fom with the wait channel.
			 */ 
			printf("FOM: waiting to create local transactional context\n");
			c2_fom_block_at(fom, &fom->chan_gen_wait);
		} else
			/* Done with creating local transactional context
			 * fop execution copleted
			 */
			fom->fo_phase = FOPH_DONE;
		break;
	default:
		printf("\n REQH: Invalid FOM Phase \n");
		fom->fo_phase = FOPH_FAILED;
		break;
	}//switch

	return rc;
}
/** @} endgroup reqh */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
