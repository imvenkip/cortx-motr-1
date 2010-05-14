/* -*- C -*- */

#ifndef __COLIBRI_STOB_LINUX_H__
#define __COLIBRI_STOB_LINUX_H__

/**
   @defgroup stoblinux Storage object based on Linux specific file system
   interfaces.

   @see stob
   @{
 */

#include "stob.h"

struct c2_stob_linux {
	struct c2_stob sl_stob;
	int            sl_fd;
	const char    *sl_path;
};

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
