#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "io_fops.h"
#include "lib/errno.h"

int c2_io_fop_cob_rwv_fom_init(struct c2_fop *fop, struct c2_fom **m);

/**
 * readv FOP operation vector.
 */
struct c2_fop_type_ops c2_io_cob_readv_ops = {
	.fto_fom_init = c2_io_fop_cob_rwv_fom_init,
	.fto_get_io_fop = c2_io_fop_get_read_fop,
};

/**
 * writev FOP operation vector.
 */
struct c2_fop_type_ops c2_io_cob_writev_ops = {
	.fto_fom_init = c2_io_fop_cob_rwv_fom_init,
	.fto_get_io_fop = c2_io_fop_get_write_fop,
};

/**
 * Init function to initialize readv and writev reply FOMs.
 * Since there is no client side FOMs as of now, this is empty.
 */
static int c2_io_fop_cob_rwv_rep_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

/**
 * readv and writev reply FOP operation vector.
 */
struct c2_fop_type_ops c2_io_rwv_rep_ops = {
	.fto_fom_init = c2_io_fop_cob_rwv_rep_fom_init,
};


/**
  FOP ops function
  Function to create a new fop and embed the given read segment into it
 */
int c2_io_fop_get_read_fop(struct c2_fop *curr_fop, struct c2_fop *res_fop,
		void *ioseg)
{
	struct c2_fop_cob_readv		*read_fop_curr;
	struct c2_fop_cob_readv		*read_fop_res;
	struct c2_fop_segment_seq	*seg = NULL;

	C2_PRE(curr_fop != NULL);
	C2_PRE(res_fop != NULL);
	C2_PRE(seg != NULL);

	seg = (struct c2_fop_segment_seq*)ioseg;
	read_fop_curr = c2_fop_data(curr_fop);

	res_fop = c2_fop_alloc(&c2_fop_cob_readv_fopt, NULL);
	C2_ASSERT(res_fop != NULL);
	read_fop_res = c2_fop_data(res_fop);
	/* Assumption: Currently, the code is coalescing irrespective
	   of member's uid and gid. This might change in future.*/
	read_fop_res->frd_ioseg = seg;
	read_fop_res->frd_fid = read_fop_curr->frd_fid;
	read_fop_res->frd_uid = read_fop_curr->frd_uid;
	read_fop_res->frd_gid = read_fop_curr->frd_gid;
	read_fop_res->frd_nid = read_fop_curr->frd_nid;
	read_fop_res->frd_flags = read_fop_curr->frd_flags;

	return 0;
}

/**
  FOP ops function
  Function to create a new fop and embed the given write vec into it
 */
int c2_io_fop_get_write_fop(struct c2_fop *curr_fop, struct c2_fop *res_fop,
		void *iovec)
{
	struct c2_fop_cob_writev	*write_fop_curr;
	struct c2_fop_cob_writev	*write_fop_res;
	struct c2_fop_io_vec		*vec = NULL;

	C2_PRE(curr_fop != NULL);
	C2_PRE(res_fop != NULL);
	C2_PRE(vec != NULL);

	vec = (struct c2_fop_io_vec*)iovec;
	write_fop_curr = c2_fop_data(curr_fop);

	res_fop = c2_fop_alloc(&c2_fop_cob_writev_fopt, NULL);
	C2_ASSERT(res_fop != NULL);
	write_fop_res = c2_fop_data(res_fop);
	/* Assumption: Currently, the code is coalescing irrespective
	   of member's uid and gid. This might change in future.*/
	write_fop_res->fwr_iovec = vec;
	write_fop_res->fwr_fid =  write_fop_curr->fwr_fid;
	write_fop_res->fwr_uid =  write_fop_curr->fwr_uid;
	write_fop_res->fwr_gid =  write_fop_curr->fwr_gid;
	write_fop_res->fwr_nid =  write_fop_curr->fwr_nid;
	write_fop_res->fwr_flags =  write_fop_curr->fwr_flags;

	return 0;
}

/**
 * FOP definitions for readv and writev operations.
 */
C2_FOP_TYPE_DECLARE(c2_fop_cob_readv, "Read request", 
		    c2_io_service_readv_opcode, &c2_io_cob_readv_ops);
C2_FOP_TYPE_DECLARE(c2_fop_cob_writev, "Write request", 
		    c2_io_service_writev_opcode, &c2_io_cob_writev_ops);
/**
 * FOP definitions of readv and writev reply FOPs.
 */
C2_FOP_TYPE_DECLARE(c2_fop_cob_writev_rep, "Write reply", 
		    c2_io_service_writev_rep_opcode, &c2_io_rwv_rep_ops);
C2_FOP_TYPE_DECLARE(c2_fop_cob_readv_rep, "Read reply", 
		    c2_io_service_readv_rep_opcode, &c2_io_rwv_rep_ops);

#ifdef __KERNEL__

/** Placeholder API for c2t1fs build. */
int c2_io_fop_cob_rwv_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

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
