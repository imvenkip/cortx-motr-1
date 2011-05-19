/* -*- C -*- */

#ifndef __COLIBRI_LIB_USER_SPACE_THREAD_H__
#define __COLIBRI_LIB_USER_SPACE_THREAD_H__

#include <sys/types.h>
#include <pthread.h>
#include <signal.h>

/**
   @addtogroup thread Thread

   <b>User space c2_thread implementation</b>

   User space implementation is straight-forwardly based on POSIX thread
   interface.

   @see c2_thread

   @{
 */

struct c2_thread_handle {
	pthread_t h_id;
};

enum {
	C2_THREAD_NAME_LEN = 16
};

int  c2_threads_init(void);
void c2_threads_fini(void);


/** @} end of thread group */

/* __COLIBRI_LIB_USER_SPACE_THREAD_H__ */
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
