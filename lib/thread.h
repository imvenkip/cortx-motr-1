/* -*- C -*- */

#ifndef __COLIBRI_LIB_THREAD_H__
#define __COLIBRI_LIB_THREAD_H__

#include <sys/types.h>
#include <pthread.h>
#include <signal.h>

#include "cdefs.h"

/**
   @defgroup thread Thread
   @{
 */

struct c2_thread {
	/** POSIX thread identifier for now. */
	pthread_t  t_id;
	void     (*t_func)(void *);
	void      *t_arg;
};

#define C2_THREAD_INIT(thread, TYPE, func, arg)		\
({							\
	typeof(func) __func = (func);			\
	typeof(arg)  __arg  = (arg);			\
	TYPE         __dummy;				\
	(void)(__func == (void (*)(TYPE))NULL);		\
	(void)(&__arg  == &__dummy);			\
	c2_thread_init(thread,				\
		       (void (*)(void *))__func,	\
		       (void *)(unsigned long)__arg);	\
})

#define LAMBDA(T, ...) ({ T __lambda __VA_ARGS__; &__lambda; })

int  c2_thread_init(struct c2_thread *q, void (*func)(void *), void *arg);
int  c2_thread_kill(struct c2_thread *q, int signal);
void c2_thread_fini(struct c2_thread *q);
int  c2_thread_join(struct c2_thread *q);

int  c2_threads_init(void);
void c2_threads_fini(void);


/** @} end of thread group */

/* __COLIBRI_LIB_THREAD_H__ */
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
