/* -*- C -*- */

#ifndef __COLIBRI_NRS_NRS_H__
#define __COLIBRI_NRS_NRS_H__

/**
   @defgroup nrs Network Request Scheduler
   @{
 */

/* import */
struct c2_fop;
struct c2_reqh;

struct c2_nrs {
	struct c2_reqh *n_reqh;
};

int  c2_nrs_init(struct c2_nrs *nrs, struct c2_reqh *reqh);
void c2_nrs_fini(struct c2_nrs *nrs);

void c2_nrs_enqueue(struct c2_nrs *nrs, struct c2_fop *fop);

/** @} end of nrs group */

/* __COLIBRI_NRS_NRS_H__ */
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
