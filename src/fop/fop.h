/* -*- C -*- */

#ifndef __FOP_FOP_H__
#define __FOP_FOP_H__

/**
   @defgroup fop File operation packet
   @{
*/

typedef uint32_t foptype_code_t;

struct foptype {
	foptype_code_t ft_code;
	const char    *ft_name;
	c2_list        ft_linkage;

	int (*ft_incoming)(struct foptype *ftype, struct fop *fop);
};

struct fopdata;
struct fop {
	struct foptype *f_type;
	struct fopdate *f_data;
};

int  foptype_register  (struct foptype *ftype, struct rpcmachine *rpcm);
void foptype_unregister(struct foptype *ftype);

/** @} end of fop group */

/* __FOP_FOP_H__ */
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
