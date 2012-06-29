/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>,
 *			Mandar Sawant <Mandar_Sawant@xyratex.com>
 * Original creation date: 05/19/2010
 */

#ifndef __COLIBRI_REQH_REQH_H__
#define __COLIBRI_REQH_REQH_H__

#include "lib/tlist.h"
#include "lib/bob.h"

#include "sm/sm.h"
#include "fop/fom.h"

/**
   @defgroup reqh Request handler

   Request handler provides non-blocking infrastructure for fop execution.
   There typically is a single request handler instance per address space, once
   the request handler is initialised and ready to serve requests, it accepts
   a fop (file operation packet), iterpretes it by interacting with other sub
   systems and executes the desired file system operation.

   For every incoming fop, request handler creates a corresponding fom
   (file operation state machine), fop is executed in this fom context.
   For every fom, request handler performs some standard operations such as
   authentication, locating resources for its execution, authorisation of file
   operation by the user, &tc. Once all the standard phases are completed, the
   fop specific operation is executed.

   @see https://docs.google.com/a/xyratex.com/Doc?docid=0ATg1HFjUZcaZZGNkNXg4cXpfMjA2Zmc0N3I3Z2Y&hl=en_US
   @{
 */

struct c2_fol;
struct c2_fop;
struct c2_net_xprt;
struct c2_rpc_machine;

enum {
        REQH_KEY_MAX = 32
};

/**
   Request handler instance.
 */
struct c2_reqh {
	struct c2_dtm		*rh_dtm;
	/**
	   @todo for now simply use storage object domain. In the future, this
	   will be replaced with "stores".
	 */
	struct c2_stob_domain	*rh_stdom;

	/** Database environment for this request handler. */
	struct c2_dbenv         *rh_dbenv;

	/** Cob domain for this request handler. */
	struct c2_cob_domain    *rh_cob_domain;

	/** Fol pointer for this request handler. */
	struct c2_fol		*rh_fol;

	/** Fom domain for this request handler. */
	struct c2_fom_domain	 rh_fom_dom;

        /**
	    Services registered with this request handler.

	    @see c2_reqh_service::rs_linkage
	 */
        struct c2_tl             rh_services;

        /**
	    RPC machines running in this request handler
	    There is one rpc machine per request handler
	    end point.

	    @see c2_rpc_machine::rm_rh_linkage
	 */
        struct c2_tl             rh_rpc_machines;

	/** provides protected access to reqh members. */
	struct c2_rwlock         rh_rwlock;

	/**
	    True if request handler received a shutdown signal.
	    Request handler should not process any further requests
	    if this flag is set.
	 */
	bool                     rh_shutdown;

	struct c2_addb_ctx      *rh_addb;

	/**
	    Channel to wait on for reqh shutdown.
	 */
	struct c2_chan           rh_sd_signal;

	/**
	 * Request handler key data.
	 *
	 * @see c2_reqh_key_init()
	 */
        void                    *rh_key[REQH_KEY_MAX];
	/** Request handler magic. */
	uint64_t                 rh_magic;
};

/**
   Initialises request handler instance provided by the caller.

   @param reqh Request handler instance to be initialised
   @param stdom Storage object domain used for file io
   @param db Database environment for this request handler
   @param cdom Cob domain for this request handler
   @param fol File operation log to record fop execution

   @todo use iostores instead of c2_stob_domain and endpoints
	or c2_rpc_machine instead of c2_service

   @see c2_reqh

   @pre reqh != NULL && stdom != NULL && db != NULL &&
	cdom != NULL && fol != NULL

   @retval 0, if request handler is succesfully initilaised,
		-errno, in case of failure
 */
int  c2_reqh_init(struct c2_reqh *reqh, struct c2_dtm *dtm,
			struct c2_stob_domain *stdom, struct c2_dbenv *db,
			struct c2_cob_domain *cdom, struct c2_fol *fol);

bool c2_reqh_invariant(const struct c2_reqh *reqh);

/**
   Destructor for request handler, no fop will be further executed
   in the address space belonging to this request handler.

   @param reqh, request handler to be finalised

   @pre reqh != NULL
 */
void c2_reqh_fini(struct c2_reqh *reqh);

/**
   Submit fop for request handler processing.
   Request handler intialises fom corresponding to this fop, finds appropriate
   locality to execute this fom, and enqueues the fom into its runq.
   Fop processing results are reported by other means (ADDB, reply fops, error
   messages, etc.) so this function returns nothing.

   @param reqh, request handler processing the fop
   @param fop, fop to be executed

   @pre reqh != null
   @pre fop != null
 */
void c2_reqh_fop_handle(struct c2_reqh *reqh,  struct c2_fop *fop);

/**
   Standard fom state transition function.

   This function handles standard fom phases from enum c2_fom_phase.

   First do "standard actions":

   - authenticity checks: reqh verifies that protected state in the fop is
     authentic. Various bits of information in C2 are protected by cryptographic
     signatures made by a node that issued this information: object identifiers
     (including container identifiers and fids), capabilities, locks, layout
     identifiers, other resources identifiers, etc. reqh verifies authenticity
     of such information by fetching corresponding node keys, re-computing the
     signature locally and checking it with one in the fop;

   - resource limits: reqh estimates local resources (memory, cpu cycles,
     storage and network bandwidths) necessary for operation execution. The
     execution of operation is delayed if it would overload the server or
     exhaust resource quotas associated with operation source (client, group of
     clients, user, group of users, job, etc.);

   - resource usage and conflict resolution: reqh determines what distributed
     resources will be consumed by the operation execution and call resource
     management infrastructure to request the resources and deal with resource
     usage conflicts (by calling DLM if necessary);

   - object existence: reqh extracts identities of file system objects affected
     by the fop and requests appropriate stores to load object representations
     together with their basic attributes;

   - authorization control: reqh extracts the identity of a user (or users) on
     whose behalf the operation is executed. reqh then uses enterprise user data
     base to map user identities into internal form. Resulting internal user
     identifiers are matched against protection and authorization information
     stored in the file system objects (loaded on the previous step);

   - distributed transactions: for operations mutating file system state, reqh
     sets up local transaction context where the rest of the operation is
     executed.

   Once the standard actions are performed successfully, request handler
   delegates the rest of operation execution to the fom type specific state
   transition function.

   Fom execution proceeds as follows:

   @verbatim

	fop
	 |
	 v                fom->fo_state = FOS_READY
     c2_reqh_fop_handle()-------------->FOM
					 | fom->fo_state = FOS_RUNNING
					 v
				     FOPH_INIT
					 |
			failed		 v         fom->fo_state = FOS_WAITING
		     +<-----------FOPH_AUTHETICATE------------->+
		     |			 |           FOPH_AUTHENTICATE_WAIT
		     |			 v<---------------------+
		     +<----------FOPH_RESOURCE_LOCAL----------->+
		     |			 |           FOPH_RESOURCE_LOCAL_WAIT
		     |			 v<---------------------+
		     +<-------FOPH_RESOURCE_DISTRIBUTED-------->+
		     |			 |	  FOPH_RESOURCE_DISTRIBUTED_WAIT
		     |			 v<---------------------+
		     +<---------FOPH_OBJECT_CHECK-------------->+
		     |                   |              FOPH_OBJECT_CHECK
		     |		         v<---------------------+
		     +<---------FOPH_AUTHORISATION------------->+
		     |			 |            FOPH_AUTHORISATION
	             |	                 v<---------------------+
		     +<---------FOPH_TXN_CONTEXT--------------->+
		     |			 |            FOPH_TXN_CONTEXT_WAIT
		     |			 v<---------------------+
		     +<-------------FOPH_NR_+_1---------------->+
		     |			 |            FOPH_NR_+_1_WAIT
		     v			 v<---------------------+
		 FOPH_FAILED        FOPH_SUCCESS
		     |			 |
		     v			 v
	  +-----FOPH_TXN_ABORT    FOPH_TXN_COMMIT-------------->+
	  |	     |			 |            FOPH_TXN_COMMIT_WAIT
	  |	     |	    send reply	 v<---------------------+
	  |	     +----------->FOPH_QUEUE_REPLY------------->+
          |	     ^			 |            FOPH_QUEUE_REPLY_WAIT
	  v	     |			 v<---------------------+
   FOPH_TXN_ABORT_WAIT		     FOPH_FINISH ---> c2_fom_fini()

   @endverbatim

   If a generic phase handler function fails while executing a fom, then
   it just sets the c2_fom::fo_rc to the result of the operation and returns
   C2_FSO_WAIT.  c2_fom_state_generic() then sets the c2_fom::fo_phase to
   C2_FOPH_FAILED, logs an ADDB event, and returns, later the fom execution
   proceeds as mentioned in above diagram.

   If fom fails while executing fop specific operation, the c2_fom::fo_phase
   is set to C2_FOPH_FAILED already by the fop specific operation handler, and
   the c2_fom::fo_rc set to the result of the operation.

   @see c2_fom_phase
   @see c2_fom_state_outcome

   @param fom, fom under execution

   @retval C2_FSO_AGAIN, if fom operation is successful, transition to next
	   phase, C2_FSO_WAIT, if fom execution blocks and fom goes into
	   corresponding wait phase, or if fom execution is complete, i.e
	   success or failure

   @todo standard fom phases implementation, depends on the support routines for
	handling various standard operations on fop as mentioned above
 */
int c2_fom_state_generic(struct c2_fom *fom);

/**
   Waits on c2_reqh::rh_sd_signal using the given clink until
   until c2_fom_domain::fd_foms_nr is 0.

   @param reqh request handler to be shutdown
 */
void c2_reqh_shutdown_wait(struct c2_reqh *reqh);

/**
    Initializes global reqh objects like reqh fops and addb context,
    invoked from c2_init().
 */
int c2_reqhs_init(void);

/**
   Finalises global reqh objects, invoked from c2_fini().
*/
void c2_reqhs_fini(void);

/**
   Find a service instance for a given service-name within a give
   request handler instance.

   @param service_name Name of the service of interest
   @param reqh Request handler instance

   @retval serive instance pointer or NULL.
 */
struct c2_reqh_service *c2_reqh_service_get(const char *service_name,
						struct c2_reqh *reqh);

/** Descriptor for tlist of request handler services. */
C2_TL_DESCR_DECLARE(c2_reqh_svc, extern);
C2_TL_DECLARE(c2_reqh_svc, extern, struct c2_reqh_service);
C2_BOB_DECLARE(extern, c2_reqh_service);

/** Descriptor for tlist of rpc machines. */
C2_TL_DESCR_DECLARE(c2_reqh_rpc_mach, extern);
C2_TL_DECLARE(c2_reqh_rpc_mach, extern, struct c2_rpc_machine);
C2_BOB_DECLARE(extern, c2_rpc_machine);

/**
   @name reqhkey

   This infrastructure allows request handler to be a central repository of
   data, typically used by the request handler services.
   This not only allows services to efficiently share the data with other
   services running in the same request handler but also avoids duplicate
   implementation of similar kind of framework.

   Following interfaces are of interest to any request handler service intending
   to store and share its specific data with the corresponding request handler,
   - c2_cs_reqh_key_init()
     Returns a new request handler key to access the stored data.
     Same key should be used in-order to share the data among multiple request
     handler entities if necessary.
     @note Key cannot exceed beyond REQH_KEY_MAX range for the given request
           handler.
     @see cs_reqh_context::rc_key
     @see cs_reqh_context::rc_keymax

   - c2_cs_reqh_key_find()
     Locates and returns the data corresponding to the key in the request handler.
     The key is used to locate the data in cs_reqh_context::rc_key[]. If the data
     is NULL, then size amount of memory is allocated for the data and returned.
     @note As request handler itself does not have any knowledge about the
     purpose and usage of the allocated data, it is the responsibility of the
     caller to initialise the allocated data and verify the consistency of the
     same throughout its existence.

   - c2_cs_reqh_key_fini()
     Destroys the request handler resource accessed by the given key.
     @note This simply destroys the allocated data without formally looking
     into its contents. Thus the caller must properly finalise the data contents
     before invoking this function. Also if the data was shared between multiple
     services, the caller must make sure that there are no more reference on the
     same.

     Below pseudo code illustrates the usage of reqhkey interfaces mentioned
     above,

     @code

     struct foo {
	...
	bool is_initialised;
     };
     static bool     foo_key_is_initialised;
     static uint32_t foo_key;
     int bar_init()
     {
	struct foo *data;

	if (!foo_key_is_initialised)
		foo_key = c2_cs__init(); //get new reqh data key

	data = c2_cs_reqh_key_find(reqh, foo_key, sizeof *foo);
	if (!data->foo_is_initialised)
		foo_init(data);
	...
     }

     void bar_fini()
     {
	struct foo *data;

	data = c2_cs_reqh_key_find(reqh, foo_key, sizeof *foo);
	foo_fini(data);
	c2_cs_reqh_key_fini(reqh, foo_key);
     }
     @endcode

     For more details please refer to @ref c2_cobfid_map_get() and
     @ref c2_cobfid_map_put() interfaces in ioservice/cobfid_map.c.
 */
/** @{ reqhkey */

unsigned c2_reqh_key_init(void);
void    *c2_reqh_key_find(struct c2_reqh *reqh, unsigned key, c2_bcount_t size);
void     c2_reqh_key_fini(struct c2_reqh *reqh, unsigned key);

/** @} reqhkey */

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
