/* -*- C -*- */

#ifndef __COLIBRI_SM_H__
#define __COLIBRI_SM_H__

#include "lib/adt.h"
#include "lib/c2list.h"
#include "lib/queue.h"
#include "dtm/dtm.h"

/**
   @defgroup sm State machine
   @{
*/

/** Macroscopic state machine state. */
typedef uint64_t c2_sm_state_t;

enum c2_sm_res {
	SR_DONE,
	SR_AGAIN,
	SR_WAIT
};

struct c2_sm;
struct c2_sm_event;

struct c2_sm_ops {
	c2_sm_state_t  (*so_state_get)(struct c2_sm *sm);
	enum c2_sm_res (*so_event_try)(struct c2_sm *sm, 
				       struct c2_sm_event *ev);
};

/**
   state machine

   Abstract state machine. Possibly persistent, possibly replicated.
*/
struct c2_sm {
	struct c2_queue sm_incoming;
};

int  c2_sm_init(struct c2_sm *mach);
void c2_sm_fini(struct c2_sm *mach);

c2_sm_state_t c2_sm_state_get  (struct c2_sm *mach);
int           c2_sm_state_set  (struct c2_sm *mach, c2_sm_state_t state);
int           c2_sm_state_wait (struct c2_sm *mach);
int           c2_sm_state_until(struct c2_sm *mach, c2_sm_state_t state);

/**
   an event causing state transition.
*/
struct c2_sm_event {
	struct c2_sm        *se_mach;
	/*struct c2_chan_link  se_wait;*/
	struct c2_list_link  se_wait;
	struct c2_queue_link se_linkage;
};

int  c2_sm_event_init(struct c2_sm_event *ev, struct c2_sm *mach);
void c2_sm_event_fini(struct c2_sm_event *ev);

enum c2_sm_res c2_sm_event_try  (struct c2_sm_event *sm);
int            c2_sm_event_wait (struct c2_sm_event *sm);
int            c2_sm_event_apply(struct c2_sm_event *sm);
int            c2_sm_event_queue(struct c2_sm_event *sm);

struct c2_persistent_sm_ops;

/** persistent state machine */
struct c2_persistent_sm {
	struct c2_sm                       ps_sm;
	const struct c2_persistent_sm_ops *ps_ops;
	struct c2_dtm                     *ps_dtm;
};

struct c2_persistent_sm_ops {
	int (*pso_recover)(struct c2_persistent_sm *pmach);
};

int  c2_persistent_sm_register  (struct c2_persistent_sm *pmach,
				 struct c2_dtm *dtm, 
				 const struct c2_persistent_sm_ops *ops);
void c2_persistent_sm_unregister(struct c2_persistent_sm *pmach);

/** @} end of sm group */

/* __COLIBRI_SM_H__ */
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
