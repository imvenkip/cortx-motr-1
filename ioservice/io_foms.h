/* -*- C -*- */

#ifndef __COLIBRI_FOP_FOM_OPERATIONS_H__
#define __COLIBRI_FOP_FOM_OPERATIONS_H__

/**
 * @defgroup io_foms Fop State Machines for various FOPs
 *
 * <b>Fop state machine for IO operations </b>
 * @see fom
 * @ref https://docs.google.com/a/xyratex.com/Doc?docid=0AQaCw6YRYSVSZGZmMzV6NzJfMTNkOGNjZmdnYg&hl=en  
 *
 * FOP state machines for various IO operations like
 * @li COB Readv
 * @li COB Writev
 *
 * All operation specific code will be executed in a single phase
 * for now. It will be decomposed into more granular phases
 * when FOM and reqh infrastructure is in place. 
 * Possible decomposition of this single phase for all operations 
 * are also stated, but not used for now. In that case, for every 
 * phase, a corresponding function will be executed as a part of FOM
 *
 * <i> Note on naming convention: For operation xyz, the fop is named 
 * as c2_fop_xyz, its corresponding reply fop is named as c2_fop_xyz_rep
 * and fom is named as c2_fom_xyz. For each fom type, its corresponding
 * create, state and fini methods are named as c2_fom_xyz_create, 
 * c2_fom_xyz_state, c2_fom_xyz_fini respectively </i>
 *
 *  @{
 */

/**
 * Decomposed phases for "write cob" FOM 
 *
 * enum c2_fom_cob_writev_phases{
 *       FOPH_STOB_INIT_IO_REQUEST,
 *       FOPH_STOB_IO_LAUNCH,
 *       FOPH_STOB_DOMAIN_TRXN_COMPLETE,
 *       FOPH_SEND_REP_FOP,
 *       FOPH_EXIT
 * };
 */

#include <fop/fop.h>
#include <fop/fop_format.h>
#include "io_fops.h"

enum c2_fom_cob_writev_phases{
	FOPH_COB_WRITE
};

/**
 * Object encompassing FOM for cob write
 * operation and necessary context data
 */

struct c2_fom_cob_writev {
        struct c2_fom                   fmcw_gen;
        struct c2_fop			*fmcw_fop;
	struct c2_stob_domain		*fmcw_domain;
	struct c2_fop_ctx		*fmcw_fop_ctx;
        struct c2_stob		        *fmcw_stob;
        struct c2_stob_io		*fmcw_st_io;
	struct c2_fol			*fmcw_fol;
};

int c2_fom_cob_writev_state(struct c2_fom *fom); 
void c2_fom_cob_writev_fini(struct c2_fom *fom);
int c2_fom_cob_writev_create(struct c2_fom_type *t, struct c2_fop *fop, 
		struct c2_fom **out);
int c2_fom_cob_writev_ctx_populate(struct c2_fom *fom, 
		struct c2_stob_domain *d, struct c2_fop_ctx *fopctx,
		struct c2_fol *fol);

enum c2_fom_cob_readv_phases {
	FOPH_COB_READ
};

/**
 * Object encompassing FOM for cob create reply
 * operation and necessary context data
 */

struct c2_fom_cob_readv {
        struct c2_fom                   	fmcr_gen;
        struct c2_fop				*fmcr_fop;
	struct c2_stob_domain			*fmcr_domain;
	struct c2_fop_ctx			*fmcr_fop_ctx;
        struct c2_stob		                *fmcr_stob;
        struct c2_stob_io			*fmcr_st_io;
	struct c2_fol				*fmcr_fol;
};

int c2_fom_cob_readv_state(struct c2_fom *fom); 
void c2_fom_cob_readv_fini(struct c2_fom *fom);
int c2_fom_cob_readv_create(struct c2_fom_type *t, struct c2_fop *fop, 
		struct c2_fom **out);
int c2_fom_cob_readv_ctx_populate(struct c2_fom *fom, 
		struct c2_stob_domain *d, struct c2_fop_ctx *fopctx,
		struct c2_fol *fol);

static const struct c2_fom_type_ops cob_readv_type_ops = {
	.fto_create = c2_fom_cob_readv_create,
	.fto_populate = c2_fom_cob_readv_ctx_populate,
};

static const struct c2_fom_type_ops cob_writev_type_ops = {
	.fto_create = c2_fom_cob_writev_create,
	.fto_populate = c2_fom_cob_writev_ctx_populate,
};


extern struct c2_fom_type c2_fom_cob_readv_mopt;
extern struct c2_fom_type c2_fom_cob_writev_mopt;
extern struct c2_fom_type *fom_types[];
struct c2_fom_type* c2_fom_type_map(c2_fop_type_code_t code);


/** @} end of io_foms */

/* __COLIBRI_FOP_FOM_OPERATIONS_H__ */
#endif

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
                                           
