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
#include "net/net.h"
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

/**
 * fom phase table to hold function pointers, which execute
 * various fom phases.
 */
static struct c2_fom_phase_table fp_table[FOPH_NR];

/**
 * function to initialize fom phase table.
 */
void set_fom_phase_table(void);
void c2_fom_wait(struct c2_fom *fom, struct c2_chan *chan);

/**
 * macro definition to set a fom phase in fom phase table.
 */
#define INIT_PHASE(curr_phase, act, np) \
	fp_table[curr_phase].action = act; \
	fp_table[curr_phase].next_phase = np; \

/** 
 * Function to initialize request handler and fom domain
 * @param reqh -> c2_reqh structure pointer.
 * @param rom -> c2_rpc_machine structure pointer.
 * @param dtm -> c2_dtm structure pointer.
 * @param dom -> c2_stob_domain strutcure pointer.
 * @param fol -> c2_fol structure pointer.
 * @param serv -> c2_service structure pointer.
 * @retval int -> returns 0, on success.
 * 		  returns -1, on failure.
 * @pre assumes reqh, dom, fol, serv not null.
 */
int  c2_reqh_init(struct c2_reqh *reqh,
		struct c2_rpcmachine *rpc, struct c2_dtm *dtm,
		struct c2_stob_domain *dom, struct c2_fol *fol,
		struct c2_service *serv)
{
	int result;
	size_t nr;

	nr = 0;	
	if ((reqh == NULL) || (dom == NULL) ||
		(fol == NULL) || (serv == NULL))
		return -EINVAL;

	set_fom_phase_table();
	reqh->rh_rpc = rpc;
	reqh->rh_dtm = dtm;
	reqh->rh_dom = dom;
	reqh->rh_fol = fol;
	reqh->rh_serv = serv;

	reqh->rh_fom_dom = c2_alloc(sizeof *reqh->rh_fom_dom);
	if (reqh->rh_fom_dom == NULL)
		return -ENOMEM;
	result = c2_fom_domain_init(reqh->rh_fom_dom, nr);
	return result;
}

/**
 * Request handler clean up function.
 * @param reqh -> c2_reqh structure pointer.
 * @pre assumes reqh not null.
 * @pre assumes fom domain in reqh is allocated and initialized.
 */
void c2_reqh_fini(struct c2_reqh *reqh)
{
	C2_PRE(reqh != NULL);
	C2_PRE(reqh->rh_fom_dom);
	c2_fom_domain_fini(reqh->rh_fom_dom);
}

/**
 * Function to accept fop and create corresponding fom
 * and submit it for further processing.
 * @param reqh -> c2_reqh structure pointer.
 * @param fom -> c2_fom sturcture pointer.
 * @param cookie -> void pointer to hold some pointer address.
 * @pre assumes reqh is not null.
 */
void c2_reqh_fop_handle(struct c2_reqh *reqh, struct c2_fop *fop, void *cookie)
{
	struct c2_fom *fom = NULL;
        struct c2_fop_ctx	*ctx;
        int			result;
	size_t			iloc;
	
	C2_ASSERT(reqh != NULL);
	C2_ASSERT(fop != NULL);

	/* initialize database */
	ctx = c2_alloc(sizeof *ctx);
	ctx->fc_cookie = cookie;
	ctx->ft_service = reqh->rh_serv;

	/* Initialize fom for fop processing */
	result = fop->f_type->ft_ops->fto_fom_init(fop, &fom);
	if (fom == NULL)
		return;

	fom->fo_fop_ctx = ctx;
	fom->fo_fol = reqh->rh_fol;
	fom->fo_domain = reqh->rh_dom;
	c2_fom_init(fom);

	/* locate fom's home locality */
	iloc = fom->fo_ops->fo_home_locality(reqh->rh_fom_dom, fom);
	fom->fo_loc = &reqh->rh_fom_dom->fd_localities[iloc];

	/* submit fom for further processing */
	c2_fom_queue(fom);
}

void c2_reqh_fop_sortkey_get(struct c2_reqh *reqh, struct c2_fop *fop,
				struct c2_fop_sortkey *key)
{
}

/**
 * Funtion to handle init phase of fom.
 * @param fom -> c2_fom structure pointer.
 * @pre assumes fom is not null.
 * @retval int -> returns 0, on success.
 *		  returns -1, on failure.
 */
int c2_fom_phase_init(struct c2_fom *fom)
{
	if (fom == NULL) {
		fom->fo_phase = FOPH_FAILED;
		return -EINVAL;
	}
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Function to authenticate fop.
 * @param fom -> c2_fom structure pointer.
 * @pre assumes fom is not null.
 * @retval int -> returns 0, on success.
 *		  returns -1, on failure.
 */
int c2_fom_authen(struct c2_fom *fom)
{
	if (fom == NULL) {
		fom->fo_phase = FOPH_FAILED;
		return -EINVAL;
	}
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Function invoked after a fom resumes execution
 * post wait, in authentication phase.
 * @param fom -> c2_fom structure pointer.
 * @pre assumes fom is not null.
 * @retval int -> returns 0, on success.
 *		  returns -1, on failure.
 */
int c2_fom_authen_wait(struct c2_fom *fom)
{
	if (fom == NULL) {
		fom->fo_phase = FOPH_FAILED;
		return -EINVAL;
	}
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	fo_wait = false;
	return 0;
}

/**
 * Function to identify local resources
 * required for fop execution.
 * @param fom -> c2_fom structure pointer.
 * @pre assumes fom is not null.
 * @retval int -> returns 0, on success.
 *		  returns -1, on failure.
 */
int c2_fom_loc_resource(struct c2_fom *fom)
{
	if (fom == NULL) {
		fom->fo_phase = FOPH_FAILED;
		return -EINVAL;
	}
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Funtion invoked after a fom resumes execution
 * post wait, while checking for local resources.
 * @param fom -> c2_fom structure pointer.
 * @pre assumes fom is not null.
 * @retval int -> returns 0, on success.
 *		  returns -1, on failure.
 */
int c2_fom_loc_resource_wait(struct c2_fom *fom)
{
	if (fom == NULL) {
		fom->fo_phase = FOPH_FAILED;
		return -EINVAL;
	}
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	fo_wait = false;
	return 0;
}

/**
 * Function to identify distributed resources
 * required for fop execution.
 * @param fom -> c2_fom structure pointer.
 * @pre assumes fom is not null.
 * @retval int -> returns 0, on success.
 *		  returns -1, on failure.
 */
int c2_fom_dist_resource(struct c2_fom *fom)
{
	if (fom == NULL) {
		fom->fo_phase = FOPH_FAILED;
		return -EINVAL;
	}
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Funtion invoked after a fom resumes execution
 * post wait, while checking for distributed resources.
 * @param fom -> c2_fom structure pointer.
 * @pre assumes fom is not null.
 * @retval int -> returns 0, on success.
 *		  returns -1, on failure.
 */
int c2_fom_dist_resource_wait(struct c2_fom *fom)
{
	if (fom == NULL) {
		fom->fo_phase = FOPH_FAILED;
		return -EINVAL;
	}
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	fo_wait = false;
	return 0;
}

/**
 * Function to locate and load file system objects, 
 * required for fop execution.
 * @param fom -> c2_fom structure pointer.
 * @pre assumes fom is not null.
 * @retval int -> returns 0, on success.
 *		  returns -1, on failure.
 */
int c2_fom_obj_check(struct c2_fom *fom)
{
	if (fom == NULL) {
		fom->fo_phase = FOPH_FAILED;
		return -EINVAL;
	}
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Funtion invoked after a fom resumes execution
 * post wait, while locating file system objects.
 * @param fom -> c2_fom structure pointer.
 * @pre assumes fom is not null.
 * @retval int -> returns 0, on success.
 *		  returns -1, on failure.
 */
int c2_fom_obj_check_wait(struct c2_fom *fom)
{
	if (fom == NULL) {
		fom->fo_phase = FOPH_FAILED;
		return -EINVAL;
	}
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	fo_wait = false;
	return 0;
}

/**
 * Function to authorise fop in fom
 * @param fom -> c2_fom structure pointer.
 * @pre assumes fom is not null.
 * @retval int -> returns 0, on success.
 *		  returns -1, on failure.
 */
int c2_fom_auth(struct c2_fom *fom)
{
	if (fom == NULL) {
		fom->fo_phase = FOPH_FAILED;
		return -EINVAL;
	}
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Funtion invoked after a fom resumes execution
 * post wait in fop authorisation phase.
 * @param fom -> c2_fom structure pointer.
 * @pre assumes fom is not null.
 * @retval int -> returns 0, on success.
 *		  returns -1, on failure.
 */
int c2_fom_auth_wait(struct c2_fom *fom)
{
	if (fom == NULL) {
		fom->fo_phase = FOPH_FAILED;
		return -EINVAL;
	}
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	fo_wait = false;
	return 0;
}

/**
 * Funtion to create local transactional context.
 * @param fom -> c2_fom structure pointer.
 * @pre assumes fom is not null.
 * @retval int -> returns 0, on success.
 *		  returns -1, on failure.
 */
int c2_create_loc_ctx(struct c2_fom *fom)
{	
	if (fom == NULL) {
		fom->fo_phase = FOPH_FAILED;
		return -EINVAL;
	}
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
 * post wait while creating local transactional context. 
 * @param fom -> c2_fom structure pointer.
 * @pre assumes fom is not null.
 * @retval int -> returns 0, on success.
 *		  returns -1, on failure.
 */
int c2_create_loc_ctx_wait(struct c2_fom *fom)
{
	if (fom == NULL) {
		fom->fo_phase = FOPH_FAILED;
		return -EINVAL;
	}
	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	fo_wait = false;
	fom->fo_phase = FOPH_DONE;
	fom->fo_ops->fo_fini(fom);
	return 0;
}

/**
 * Function to handle generic operations of fom
 * like authentication, authorisation, acquiring resources, &tc
 * @param fom -> c2_fom structure pointer.
 * @pre assumes fom is not null.
 * @retval int -> returns 0, on success.
 *		  returns -1, on failure.
 */
int c2_fom_state_generic(struct c2_fom *fom)
{
	C2_PRE(fom != NULL);
	int rc = 0;
	bool stop = false;
	while (!stop) {
		rc = fp_table[fom->fo_phase].action(fom);
		if (rc || fom->fo_phase == FOPH_DONE ||
			fo_wait || fom->fo_phase == FOPH_EXEC)
			stop = true;
	}

	return rc;
}

/**
 * Transition table holding function pointers
 * to generic fom phases and next phase to transition into.
 * current phase is used as the offset in this table.
 */
void set_fom_phase_table(void)
{
	INIT_PHASE(FOPH_INIT, c2_fom_phase_init, FOPH_AUTHENTICATE)
	INIT_PHASE(FOPH_AUTHENTICATE, c2_fom_authen, FOPH_RESOURCE_LOCAL)
	INIT_PHASE(FOPH_AUTHENTICATE_WAIT, c2_fom_authen_wait, FOPH_RESOURCE_LOCAL)
	INIT_PHASE(FOPH_RESOURCE_LOCAL, c2_fom_loc_resource, FOPH_RESOURCE_DISTRIBUTED)
	INIT_PHASE(FOPH_RESOURCE_LOCAL_WAIT, c2_fom_loc_resource_wait, FOPH_RESOURCE_DISTRIBUTED)
	INIT_PHASE(FOPH_RESOURCE_DISTRIBUTED, c2_fom_dist_resource, FOPH_OBJECT_CHECK)
	INIT_PHASE(FOPH_RESOURCE_DISTRIBUTED_WAIT, c2_fom_dist_resource_wait, FOPH_OBJECT_CHECK)
	INIT_PHASE(FOPH_OBJECT_CHECK, c2_fom_obj_check, FOPH_AUTHORISATION)
	INIT_PHASE(FOPH_OBJECT_CHECK_WAIT, c2_fom_obj_check_wait, FOPH_AUTHORISATION)
	INIT_PHASE(FOPH_AUTHORISATION, c2_fom_auth, FOPH_EXEC)
	INIT_PHASE(FOPH_AUTHORISATION_WAIT, c2_fom_auth_wait, FOPH_EXEC)
	INIT_PHASE(FOPH_TXN_CONTEXT, c2_create_loc_ctx, FOPH_DONE)
	INIT_PHASE(FOPH_TXN_CONTEXT_WAIT, c2_create_loc_ctx_wait, FOPH_DONE)
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
