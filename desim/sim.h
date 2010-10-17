/*
 * Copyright 2009 ClusterStor.
 *
 * Nikita Danilov.
 */

/*
 * Discreet (sic.) Event Simulator (desim).
 *
 * Main data types and entry points declarations.
 */

#ifndef SIM_H
#define SIM_H

#include <stdarg.h>

#if defined(__APPLE__)
/* for ucontext: 
   http://lists.apple.com/archives/darwin-dev/2008/Jan/msg00229.html */
#define _XOPEN_SOURCE
#endif

#include <stdlib.h>
#include <ucontext.h>

#include "lib/list.h"
#include "desim/cnt.h"

struct sim;
struct sim_callout;
struct sim_thread;

typedef unsigned long long sim_time_t;
typedef int  sim_call_t(struct sim_callout *);
typedef void sim_func_t(struct sim *, struct sim_thread *, void *);

/*
 * A call-out (alias timer, alias event) is a representation of an event in
 * simulation. A call-out is allocated to simulate some event that is to happen
 * in the simulation "future". Call-outs are inserted into a per-simulation
 * queue sorted by the logical simulation time. In the fullness of logical time,
 * call-out is "executed", meaning that its ->sc_call() function is
 * called. Execution of call-out might allocate new call-outs advancing the
 * state of simulation.
 */
struct sim_callout {
	/* logical time for which the call-out is scheduled */
	sim_time_t         sc_time;
	/* call-back function to be invoked when the logical time is ripe. If
	 * call-back function returns true (non-0), main simulation loop frees
	 * the call-out structure (this is suitable for one-shot call-outs),
	 * otherwise it is up to the call-out creator to clean up. */
	sim_call_t         *sc_call;
	/* a datum, opaque for generic simulation code, attached to the
	 * call-out. This field is for private use by call-back function */
	void               *sc_datum;
	/* linkage into a logical time list sim::ss_future */
	struct c2_list_link sc_linkage;
	/* simulation run this call-out is an event in */
	struct sim         *sc_sim;
};

/*
 * State of a simulation.
 *
 * This is the "root" data-structure used by the simulation loop (sim_run()) to
 * drive simulation.
 */
struct sim {
	/* current logical time. The precise meaning of this field is up to a
	 * particular simulation model. Standard modules in net.[ch],
	 * storage.[ch], etc. assume this field to be a nanosecond-precision
	 * time. */
	sim_time_t          ss_bolt;
	/*
	 * A "logical time queue". A list of call-outs (struct sim_callout)
	 * linked through their sim_callout::sc_linkage and ordered by the
	 * sim_callout::sc_time field. This list represents of future events,
	 * still to be executed by the simulation loop.
	 */
	struct c2_list      ss_future;
};

/*
 * A thread in a simulated world. 
 *
 * Conceptually, a thread can always be replaced by a collection of call-outs. A
 * thread is advantageous and natural to use when there is a lot of state to be
 * maintained between call-outs and this state cannot be readily stored in some
 * data-structure hanging off sim_callout::sc_datum. A thread uses native C
 * language stack to store the state.
 *
 * Threads interact with the simulation by sleeping (sim_sleep()), waiting on
 * channels (struct sim_chan) and posting call-outs.
 *
 * Current thread uses ucontext functions to implement lightweight and simple
 * user-level threads. Blocking system call made from a sim_thread blocks the
 * whole simulation.
 *
 * With threads there are two modes in which simulation can be at the moment:
 *
 *     "thread mode", when one of the threads is running. In this mode the code
 *     is executed on the thread's stack;
 *
 *     "scheduler mode", when no thread is running and simulation state is
 *     advanced by main simulation loop running on a stack where sim_run() was
 *     called, executing call-outs.
 *
 * Note that this distinction is similar to "process" vs. "interrupt" modes of a
 * traditional UNIX kernel.
 *
 * A switch from thread to scheduler mode occurs when a thread is suspended
 * (e.g., to wait on a channel) or exits. A switch in the opposite direction
 * happens when a thread is resumed (e.g., woken up or starts for the first
 * time).
 *
 */
struct sim_thread {
	/* simulation instance this thread is a part of */
	struct sim         *st_sim;
	/* allocated native stack, allocated in sim_thread_init() */
	void               *st_stack;
	/* size of allocated stack */
	unsigned            st_size;
	/* platform-independent structure holding thread machine state
	 * (registers and signals mask usually) */
	ucontext_t          st_ctx;
	/* channel waiting */
	/* linkage into a sim_chan::ch_threads list */
	struct c2_list_link st_block;
	/* time when a thread was parked onto a channel. See sim_chan_wait()
	   comments. */
	sim_time_t          st_blocked;
	/* pre-allocated callout to wake the thread. Used by sim_sleep() and
	   sim_chan_{signal,broadcast}(). */
	struct sim_callout  st_wake;
};

/*
 * Synchronization channel.
 *
 * A channel is conceptually similar to a POSIX condition variable, except that
 * it is simpler due to single-threaded nature of simulation.
 *
 * A thread can sleep (wait) on a channel by calling sim_chan_wait(). Threads
 * waiting on a channel are woken up one by one by calls to sim_chan_signal() or
 * all together by a call to sim_chan_broadcast().
 */
struct sim_chan {
	/* list of threads waiting on a channel */
	struct c2_list      ch_threads;
	/* statistical counter measuring for how long threads are sleeping on
	 * this channel */
	struct cnt          ch_cnt_sleep;
};

void sim_init(struct sim *state);
void sim_fini(struct sim *state);
void sim_run (struct sim *state);

void sim_timer_add   (struct sim *state, sim_time_t delta,
		      sim_call_t *cfunc, void *datum);
void sim_timer_rearm (struct sim_callout *call, sim_time_t delta,
		      sim_call_t *cfunc, void *datum);
void *sim_alloc(size_t size);
void  sim_free (void *ptr);

void sim_chan_init(struct sim_chan *chan, char *format, ...)
	__attribute__((format(printf, 2, 3)));
void sim_chan_fini(struct sim_chan *chan);
void sim_chan_wait(struct sim_chan *chan, struct sim_thread *thread);
void sim_chan_signal(struct sim_chan *chan);
void sim_chan_broadcast(struct sim_chan *chan);

void sim_thread_init(struct sim *state, struct sim_thread *thread,
		     unsigned stacksize, sim_func_t func, void *arg);
void sim_thread_fini(struct sim_thread *thread);
void sim_thread_exit(struct sim_thread *thread);

void sim_sleep(struct sim_thread *thread, sim_time_t nap);
struct sim_thread *sim_thread_current(void);

/* get a pseudo-random number in the interval [a, b] */
unsigned long long sim_rnd(unsigned long long a, unsigned long long b);

void sim_name_set(char **name, const char *format, ...)
	__attribute__((format(printf, 2, 3)));

void sim_name_vaset(char **name, const char *format, va_list valist);

enum sim_log_level {
	SLL_WARN,
	SLL_INFO,
	SLL_TRACE,
	SLL_DEBUG
};

extern enum sim_log_level sim_log_level;

void sim_log(struct sim *s, enum sim_log_level level, const char *format, ...)
	__attribute__((format(printf, 3, 4)));

int  sim_global_init(void);
void sim_global_fini(void);

#endif /* SIM_H */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
