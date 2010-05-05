/* -*- C -*- */

#ifndef __COLIBRI_REQH_REQH_H__
#define __COLIBRI_REQH_REQH_H__

#include <sm/sm.h>

/* import */
struct c2_fop;

/**
   @defgroup reqh Request handler

   @{
 */

struct c2_reqh;
struct c2_fop_sortkey;

struct c2_reqh {
};

struct c2_fop_sortkey {
};

int  c2_reqh_fop_handle     (struct c2_reqh *reqh, struct c2_fop *fop);
void c2_reqh_fop_sortkey_get(struct c2_reqh *reqh, struct c2_fop *fop,
			     struct c2_fop_sortkey *key);

/** @} endgroup reqh */

/* __COLIBRI_REQH_REQH_H__ */
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
