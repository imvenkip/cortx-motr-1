#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib/errno.h"
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
 * Generic variable to signify wait.
 */
static bool fo_wait;

int c2_fom_phase_init(struct c2_fom *fom);
int c2_fom_authen(struct c2_fom *fom);
int c2_fom_authen_wait(struct c2_fom *fom);
int c2_fom_loc_resource(struct c2_fom *fom);
int c2_fom_loc_resource_wait(struct c2_fom *fom);
int c2_fom_dist_resource(struct c2_fom *fom);
int c2_fom_dist_resource_wait(struct c2_fom *fom);
int c2_fom_obj_check(struct c2_fom *fom);
int c2_fom_obj_check_wait(struct c2_fom *fom);
int c2_fom_auth(struct c2_fom *fom);
int c2_fom_auth_wait(struct c2_fom *fom);
int c2_create_loc_ctx(struct c2_fom *fom);
int c2_create_loc_ctx_wait(struct c2_fom *fom);
void c2_fom_wait(struct c2_fom *fom, struct c2_chan *chan);

/**
 * Transition table holding function pointers
 * to generic fom phases and next phase to into.
 * current phase is used as the offset in this table.
 */
static struct c2_fom_phase_table fp_table [] = {
		{ .action = c2_fom_phase_init,
		  .next_phase = FOPH_AUTHENTICATE },
		{
		  .action = c2_fom_authen,
		  .next_phase = FOPH_RESOURCE_LOCAL },
		{
		  .action = c2_fom_authen_wait,
		  .next_phase = FOPH_RESOURCE_LOCAL },
		{
		  .action = c2_fom_loc_resource,
		  .next_phase = FOPH_RESOURCE_DISTRIBUTED },
		{
		  .action = c2_fom_loc_resource_wait,
		  .next_phase = FOPH_RESOURCE_DISTRIBUTED },
		{
		  .action = c2_fom_dist_resource,
		  .next_phase = FOPH_OBJECT_CHECK },
		{
		  .action = c2_fom_dist_resource_wait,
		  .next_phase = FOPH_OBJECT_CHECK },
		{
		  .action = c2_fom_obj_check,
		  .next_phase = FOPH_AUTHORISATION },
		{
		  .action = c2_fom_obj_check_wait,
		  .next_phase = FOPH_AUTHORISATION },
		{
		  .action = c2_fom_auth,
		  .next_phase = FOPH_EXEC },
		{
		  .action = c2_fom_auth_wait,
		  .next_phase = FOPH_EXEC },
		{
		  .action = c2_create_loc_ctx,
		  .next_phase = FOPH_DONE },
		{
		  .action = c2_create_loc_ctx_wait,
		  .next_phase = FOPH_DONE} };
				        		
/** 
 * Function to initialize request handler and fom domain
 * success : returns 0
 * failure : returns negative value
 */
int  c2_reqh_init(struct c2_reqh *reqh,
		struct c2_rpcmachine *rpc, struct c2_dtm *dtm,
		struct c2_stob_domain *dom, struct c2_fol *fol, 
		struct c2_service *serv)
{
	int result = 0;
	size_t nr = 0;

	if ((reqh == NULL) || (dom == NULL) ||
		(fol == NULL) || (serv == NULL))
		return -EINVAL;

	reqh->rh_rpc = rpc;
	reqh->rh_dtm = dtm;
	reqh->rh_dom = dom;
	reqh->rh_fol = fol;
	reqh->rh_serv = serv;

	result = c2_fom_domain_init(&reqh->rh_fom_dom, nr);
	if (result)
		printf("REQH: c2_fom_domain_init failed with result %d\n", result);

	return result;
}

/**
 * Request handler clean up function.
 */
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
	struct c2_fom *fom = NULL;
        struct c2_fop_ctx	*ctx;
        int			result = 0;
	
	C2_ASSERT(reqh != NULL);
	C2_ASSERT(fop != NULL);

	/* initialize database */
	ctx = c2_alloc(sizeof *ctx);
	ctx->fc_cookie = cookie;
        ctx->ft_service = reqh->rh_serv;

	/* Initialize fom for fop processing */
	result = fop->f_type->ft_ops->fto_fom_init(fop, &fom);
	C2_ASSERT(fom != NULL);

	fom->fo_fop_ctx = ctx;
	fom->fo_fol = reqh->rh_fol;
	fom->fo_stdom = reqh->rh_dom;
	c2_fom_init(fom);

	/* find locality and submit fom for further processing */
	printf("REQH: fom enqueued for execution\n");
	c2_fom_queue(reqh->rh_fom_dom, fom);
}

void c2_reqh_fop_sortkey_get(struct c2_reqh *reqh, struct c2_fop *fop,
				struct c2_fop_sortkey *key)
{
}

/**
 * Funtion to handle init phase of fom.
 */
int c2_fom_phase_init(struct c2_fom *fom)
{
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Function to authenticate fop.
 * success : returns 0
 * failure : returns negative value
 */
int c2_fom_authen(struct c2_fom *fom)
{
	C2_ASSERT(fom != NULL);
	fom->fo_phase = FOPH_AUTHENTICATE_WAIT;
	fo_wait = true;
	return 0;
}

/**
 * Function invoked after a fom resumes execution
 * post wait, in authentication phase.
 * success : returns 0
 * failure : returns negative value
 */
int c2_fom_authen_wait(struct c2_fom *fom)
{
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	fo_wait = false;
	return 0;
}

/**
 * Function to identify local resources
 * required for fop execution.
 * success : returns 0
 * failure : returns negative value
 */
int c2_fom_loc_resource(struct c2_fom *fom)
{
	C2_ASSERT(fom != NULL);
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Funtion invoked after a fom resumes execution
 * post wait, while checking for local resources.
 * success : returns 0
 * failure : returns negative value
 */
int c2_fom_loc_resource_wait(struct c2_fom *fom)
{
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	fo_wait = false;
	return 0;
}

/**
 * Function to identify distributed resources
 * required for fop execution.
 * success : returns 0
 * failure : returns negative value
 */
int c2_fom_dist_resource(struct c2_fom *fom)
{
	C2_ASSERT(fom != NULL);
	fom->fo_phase = FOPH_RESOURCE_DISTRIBUTED_WAIT;
	fo_wait = true;
	return 0;
}

/**
 * Funtion invoked after a fom resumes execution
 * post wait, while checking for distributed resources.
 * success : returns 0
 * failure : returns negative value
 */
int c2_fom_dist_resource_wait(struct c2_fom *fom)
{
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	fo_wait = false;
	return 0;
}

/**
 * Function to locate and load file system objects, 
 * required for fop execution.
 * success : returns 0
 * failure : returns negative value
 */
int c2_fom_obj_check(struct c2_fom *fom)
{
	C2_ASSERT(fom != NULL);
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Funtion invoked after a fom resumes execution
 * post wait, while locating file system objects.
 * success : returns 0
 * failure : returns negative value
 */
int c2_fom_obj_check_wait(struct c2_fom *fom)
{
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	fo_wait = false;
	return 0;
}

/**
 * Function to authorise fop in fom
 * success : returns 0
 * failure : returns negative value
 */
int c2_fom_auth(struct c2_fom *fom)
{
	C2_ASSERT(fom != NULL);
	fom->fo_phase = FOPH_AUTHORISATION_WAIT;
	fo_wait = true;
	return 0;
}

/**
 * Funtion invoked after a fom resumes execution
 * post wait in fop authorisation phase.
 * success : returns 0
 * failure : returns negative value
 */
int c2_fom_auth_wait(struct c2_fom *fom)
{
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	fo_wait = false;
	return 0;
}

/**
 * Funtion to create local transactional context.
 * success : returns 0
 * failure : returns negative value.
 */
int c2_create_loc_ctx(struct c2_fom *fom)
{	
  	C2_ASSERT(fom != NULL);
	int rc = 0;
	rc = c2_fop_fol_rec_add(fom->fo_fop, fom->fo_fol, 
				&fom->fo_tx.tx_dbtx);
	if (rc)
		fom->fo_phase = FOPH_FAILED;
	else
		fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return rc;
}

/**
 * Funtion to invoke after a fom resumes execution
 * post wait while creating local transactional 
 * context.
 * success : returns 0
 * failure : returns negative value.
 */
int c2_create_loc_ctx_wait(struct c2_fom *fom)
{
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	fo_wait = false;
	fom->fo_phase = FOPH_DONE;
	fom->fo_ops->fo_fini(fom);
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
	bool stop = false;
	while (!stop) {
		rc = fp_table[fom->fo_phase].action(fom);
		if (rc || (fom->fo_phase == FOPH_DONE) ||
			fo_wait || (fom->fo_phase == FOPH_EXEC))
			stop = true;
	}
	if (fo_wait) {
		fo_wait = false;
 		c2_fom_wait(fom, &fom->chan_gen_wait);
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
