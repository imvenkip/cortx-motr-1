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
 *
 * <i> Note on naming convention: For operation xyz, the fop is named 
 * as c2_fop_xyz, its corresponding reply fop is named as c2_fop_xyz_rep
 * and fom is named as c2_fom_xyz. For each fom type, its corresponding
 * create, state and fini methods are named as c2_fom_xyz_create, 
 * c2_fom_xyz_state, c2_fom_xyz_fini respectively </i>
 *
 *  @{
 */

#include "fop/fop.h"
#include "fop/fop_format.h"
#include "io_fops.h"
#include "stob/stob.h"
#ifndef __KERNEL__
#include "io_fops_u.h"
/**
 * Function to map given fid to corresponding Component object id(in turn,
 * storage object id).
 * Currently, this mapping is identity. But it is subject to 
 * change as per the future requirements.
 */
void c2_fid2stob_map(struct c2_fid *in, struct c2_stob_id *out);
#endif

/**
 * Find out the respective FOM type object (c2_fom_type)
 * from the given opcode.
 * This opcode is obtained from the FOP type (c2_fop_type->ft_code) 
 */
struct c2_fom_type* c2_fom_type_map(c2_fop_type_code_t code);

/** 
 * The various phases for writev FOM. 
 * Not used as of now. Will be used once the 
 * complete FOM and reqh infrastructure is in place.
 */
enum c2_fom_cob_writev_phases{
	FOPH_COB_WRITE
};

/**
 * Object encompassing FOM for cob write
 * operation and necessary context data
 */
struct c2_fom_cob_rwv {
	/** Generic c2_fom object. */
        struct c2_fom                    fcrw_gen;
	/** FOP associated with this FOM. */
        struct c2_fop			*fcrw_fop;
	/** Reply FOP associated with request FOP above. */
	struct c2_fop			*fcrw_rep_fop;
	/** Stob object on which this FOM is acting. */
        struct c2_stob		        *fcrw_stob;
	/** Stob IO packet for the operation. */
        struct c2_stob_io		*fcrw_st_io;
	/** FOP ctx sent by the network service. */
	struct c2_fop_ctx		*fcrw_fop_ctx;
	/** FOL object to make transactions of update operations. */
	struct c2_fol			*fcrw_fol;
	/** Stob domain in which this FOM is operating. */
	struct c2_stob_domain		*fcrw_domain;
};

/**
 * <b> State Transition function for "write IO" operation
 *     that executes on data server. </b>
 *  - Submit the write IO request to the corresponding cob.
 *  - Send reply FOP to client.
 */
int c2_fom_cob_writev_state(struct c2_fom *fom); 

/** 
 * The various phases for readv FOM. 
 * Not used as of now. Will be used once the 
 * complete FOM and reqh infrastructure is in place.
 */
enum c2_fom_cob_readv_phases {
	FOPH_COB_READ
};

/**
 * <b> State Transition function for "read IO" operation
 *     that executes on data server. </b>
 *  - Submit the read IO request to the corresponding cob.
 *  - Send reply FOP to client.
 */
int c2_fom_cob_readv_state(struct c2_fom *fom); 

/** Finish method of read FOM object */
void c2_fom_cob_rwv_fini(struct c2_fom *fom);

/** FOM type specific functions for readv FOP. */
static const struct c2_fom_type_ops cob_readv_type_ops = {
	.fto_create = NULL,
};

/** FOM type specific functions for writev FOP. */
static const struct c2_fom_type_ops cob_writev_type_ops = {
	.fto_create = NULL,
};

extern struct c2_fom_type c2_fom_cob_readv_mopt;
extern struct c2_fom_type c2_fom_cob_writev_mopt;
extern struct c2_fom_type *fom_types[];

#ifndef __KERNEL__
/**
 * A dummy request handler API to handle incoming FOPs.
 * Actual reqh will be used in future.
 */
int c2_dummy_req_handler(struct c2_service *s, struct c2_fop *fop,
			 void *cookie, struct c2_fol *fol, 
			 struct c2_stob_domain *dom);
#endif


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

