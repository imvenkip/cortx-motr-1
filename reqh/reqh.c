#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

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
		  struct c2_stob_domain *dom, struct c2_dbenv *db_env, struct c2_fol *fol, struct c2_service *serv)
{

	if(reqh != NULL) {

		printf("\nMandar: In c2_reqh_init\n");
		
	        int result = 0;
        	size_t nr = 0;

		reqh->rh_rpc = rpc;
		reqh->rh_dtm = dtm;
		reqh->rh_dom = dom;
		reqh->rh_fol = fol;
		reqh->rh_serv = serv;
		reqh->rh_dbenv = db_env;

        	result = c2_fom_domain_init(&reqh->rh_fom_dom, nr);

        	if(!result && !reqh->rh_fom_dom)
            		return 1;

	}

	return 0;
}

void c2_reqh_fini(struct c2_reqh *reqh)
{
	C2_ASSERT(reqh != NULL);
	C2_ASSERT(reqh->rh_fom_dom);
	c2_fom_domain_fini(reqh->rh_fom_dom);	
	
}

/** 
 * Function to accept fop and create corresponding fom 
 * and submit it for further processing.
*/
void c2_reqh_fop_handle(struct c2_reqh *reqh, struct c2_fop *fop, void *cookie)
{
	struct c2_fom *fom = NULL;
        struct c2_fop_ctx       *ctx;
	struct c2_dtx	*tx;
        int                     result = 0;
		
	tx = c2_alloc(sizeof(struct c2_dtx));

	/** initialize database **/
        result = c2_db_tx_init(&tx->tx_dbtx, reqh->rh_dbenv, 0);

	ctx = c2_alloc(sizeof(struct c2_fop_ctx));
        ctx->ft_service = reqh->rh_serv;
        ctx->fc_cookie  = cookie;
	ctx->fc_fol = reqh->rh_fol;
	ctx->fc_domain = reqh->rh_dom;
       	ctx->fc_tx = tx;

	if(fop != NULL) {
	
		/*
		 * Initialize fom for fop processing
		 */
		result = fop->f_type->ft_ops->fto_fom_init(fop, &fom);
		C2_ASSERT(fom != NULL);

		if(fom != NULL) {
			c2_fom_init(fom);
		        fom->fo_fop_ctx = ctx;
			if(reqh->rh_fom_dom != NULL) {
				/* 
				 * find locality and submit fom for further processing
				 */		
				c2_fom_queue(reqh->rh_fom_dom, fom);
			}
		}	
	}	
	
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
	return 0;
}

/**
 * Function to identify local resources
 * required for fop execution.
 */
int c2_fom_loc_resource(struct c2_fom *fom)
{
	C2_ASSERT(fom != NULL);
	return 0;
}

/** 
 * Function to identify distributed resources
 * required for fop execution.
 */
int c2_fom_dist_resource(struct c2_fom *fom)
{
	C2_ASSERT(fom != NULL);
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
	return 0;
}

/**
 * Function to authorise fop in fom
 */
int c2_fom_auth(struct c2_fom *fom)
{
	C2_ASSERT(fom != NULL);
	return 0;
}

/**
 * Funtion to create local transactional context.
 */
int c2_create_loc_ctx(struct c2_fom *fom)
{	
  	C2_ASSERT(fom != NULL);
	int rc = 0;
	rc = c2_fop_fol_rec_add(fom->fo_fop, fom->fo_fop_ctx->fc_fol, 
	                        &fom->fo_fop_ctx->fc_tx->tx_dbtx);
	
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
		
	if(fom != NULL)
	{	
		switch(fom->fo_phase) {
			case FOPH_INIT:
			{	
                                /** Start with fop authentication **/
				fom->fo_phase = FOPH_AUTHENTICATE;
			 
				rc = c2_fom_authen(fom);
				if(fom->fo_phase == FOPH_AUTHENTICATE_WAIT) {
                                        /* Operation is blocking,
					 * register fom with the wait channel.
					 */ 
					c2_fom_block_at(fom, &fom->chan_gen_wait);
					break;
				}
				else
				if(fom->fo_phase == FOPH_FAILED)
					/* fop failed in generic phase, return the 
					 * error code in reply fop.
					 */
					 break;
			}
			case FOPH_AUTHENTICATE:
			{	
				/* If done with authentication phase proceed with
				 * acquiring local resource information, change phase
				 * to FOPH_RESOURCE_LOCAL.
				 */
				fom->fo_phase = FOPH_RESOURCE_LOCAL;

				/* collect local resources information for this fom */
				rc = c2_fom_loc_resource(fom);
				if(fom->fo_phase == FOPH_RESOURCE_LOCAL_WAIT) {
                                        /* Operation is blocking,
					 * register fom with the wait channel.
					 */ 
					c2_fom_block_at(fom, &fom->chan_gen_wait);
					break;
				}
				else
				if(fom->fo_phase == FOPH_FAILED)
					/* fop failed in generic phase, return the 
					 * error code in reply fop.
					 */
					 break;
									
			}
			case FOPH_RESOURCE_LOCAL:
			{	
				/* If done with acquiring local resource information, proceed with 
				 * acquiring distributed resource information, change phase to
				 * FOPH_RESOURCE_DISTRIBUTED. 
				 */
				fom->fo_phase = FOPH_RESOURCE_DISTRIBUTED;

				/* collect distributed resources information for this fom */
				rc = c2_fom_dist_resource(fom);
				if(fom->fo_phase == FOPH_RESOURCE_DISTRIBUTED_WAIT) {
                                        /* Operation is blocking,
					 * register fom with the wait channel.
					 */ 
					c2_fom_block_at(fom, &fom->chan_gen_wait);
					break;
				}
				else
				if(fom->fo_phase == FOPH_FAILED)
					/* fop failed in generic phase, return the 
					 * error code in reply fop.
					 */
					 break;
			}
			case FOPH_RESOURCE_DISTRIBUTED:
			{
				/* If done with acquiring distributed resource information, 
				 * proceed with locating effective file system objects, 
				 * and loading them, change phase to FOPH_OBJECT_CHECK.
				 */
				fom->fo_phase = FOPH_OBJECT_CHECK;

				/* Locate and load various effective file system objects */
				rc = c2_fom_obj_check(fom);
				if(fom->fo_phase == FOPH_OBJECT_CHECK_WAIT) {
                                        /* Operation is blocking,
					 * register fom with the wait channel.
					 */ 
					c2_fom_block_at(fom, &fom->chan_gen_wait);
					break;
				}
				else
				if(fom->fo_phase == FOPH_FAILED)
					/* fop failed in generic phase, return the 
					 * error code in reply fop.
					 */
					 break;
			}
			case FOPH_OBJECT_CHECK:
			{
                                /* If done with object checking, proceed to fop authorisation,
				 * change phase to FOPH_AUTHORISATION.
				 */
                                fom->fo_phase = FOPH_AUTHORISATION;
				
                                /* Authorise fop */
                                rc = c2_fom_auth(fom);
				if(fom->fo_phase == FOPH_AUTHORISATION_WAIT) {
                                        /* Operation is blocking,
					 * register fom with the wait channel.
					 */ 
                                        c2_fom_block_at(fom, &fom->chan_gen_wait);
                                        break;
                                }
				else
				if(fom->fo_phase == FOPH_FAILED)
					/* fop failed in generic phase, return the 
					 * error code in reply fop.
					 */
					 break;
			}
			case FOPH_AUTHORISATION:
			{
				/* If done with fop authorisation, create local transactional
				 * context, change phase to FOPH_TXN_CONTEXT.
				 */ 
		
                                fom->fo_phase = FOPH_TXN_CONTEXT;

                                /* create local transactional context for this fom */
				rc = c2_create_loc_ctx(fom);

                                if(fom->fo_phase == FOPH_TXN_CONTEXT_WAIT) {
                                        /* Operation is blocking,
					 * register fom with the wait channel.
					 */ 
                                        c2_fom_block_at(fom, &fom->chan_gen_wait);
                                        break;
                                }
				else
				if(fom->fo_phase == FOPH_FAILED)
					/* fop failed in generic phase, return the 
					 * error code in reply fop.
					 */
					 break;
			}
			case FOPH_TXN_CONTEXT:
			{
                                /* If local transaction context created, proceed with 
				 * fop execution.
				 */
                                fom->fo_phase = FOPH_EXEC;
				break;
			}
			
		}
	
	}
	
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
