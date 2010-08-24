/* -*- C -*- */

#ifndef __COLIBRI_STOB_LINUX_H__
#define __COLIBRI_STOB_LINUX_H__

/**
   @defgroup stoblinux Storage object based on Linux specific file system
   interfaces.

   @see stob
   @{
 */

extern struct c2_stob_type linux_stob_type;

int  linux_stobs_init(void);
void linux_stobs_fini(void);

/** @} end group stoblinux */

/* __COLIBRI_STOB_LINUX_H__ */
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
