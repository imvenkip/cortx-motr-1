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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 08/03/2011
 */

#ifndef __COLIBRI_CONSOLE_H__
#define __COLIBRI_CONSOLE_H__

#include "lib/time.h"	 /* c2_time_t */
#include "net/net.h"	 /* c2_net_* */
#include "rpc/session.h" /* c2_rpc_session */
#include "rpc/rpc2.h" /* c2_rpcmachine */
#include "db/db.h"	 /* c2_dbenv */
#include "cob/cob.h"	 /* c2_cob_domain */
#include "rpc/session.h" /* c2_rpc_conn */
#include "reqh/reqh.h"	 /* c2_reqh */

/**
   @defgroup console Console

   Build a standalone utility that

   - connects to a specified service.
   - constructs a fop of a specified fop type and with specified
     values of fields and sends it to the service.
   - waits fop reply.
   - outputs fop reply to the user.

   The console utility can send a DEVICE_FAILURE fop to a server. Server-side
   processing for fops of this type consists of calling a single stub function.
   Real implementation will be supplied by the middleware.cm-setup task.

   @{
*/

extern bool verbose;

extern uint32_t timeout;

/**
 * @enum
 * @brief Default port values.
 */
enum {
	CLIENT_PORT = 23123,
        SERVER_PORT = 23125,
};

/**
 * @enum
 * @brief Default address length.
 */
enum {
        ADDR_LEN = 36,
};

/**
 * @enum
 * @brief Default RID.
 */
enum {
        RID = 1,
};

/**
 * @enum
 * @brief Default number of slots.
 */
enum {
        NR_SLOTS = 1,
};

/**
 * @enum
 * @brief Default time(seconds) to wait.
 */
enum {
	TIME_TO_WAIT = 10
};


/**
 * @brief Max number o items in flight for rpc
 */
enum {
	MAX_RPCS_IN_FLIGHT = 1
};

/**
 * @struct c2_console
 * @brief Console has all info required to process message and
 *	  to send it over rpc transport.
 */
struct c2_console {
        /** Transport structure */
        struct c2_net_xprt        *cons_xprt;
        /** Network domain */
        struct c2_net_domain       cons_ndom;
        /** Local host name */
        const char                *cons_lhost;
        /** Local port */
        int                        cons_lport;
	/** Dotted quad for local host */
	char			   cons_laddr[ADDR_LEN];
        /** Remote host name */
        const char                *cons_rhost;
        /** remote port */
        int                        cons_rport;
	/** Dotted quad for remote host */
	char			   cons_raddr[ADDR_LEN];
        /** Remote end point */
        struct c2_net_end_point   *cons_rendp;
	/** DB for rpc machine */
        struct c2_dbenv		   cons_db;
        /** DB name */
        const char		  *cons_db_name;
        /** RPC connection */
        struct c2_rpc_conn         cons_rconn;
        /** rpc machine */
        struct c2_rpcmachine       cons_rpc_mach;
	/** Transfer machine needed for creating remote end point */
	struct c2_net_transfer_mc *cons_trans_mc;
        /** cob domain */
        struct c2_cob_domain       cons_cob_domain;
        /** cob domain id */
        struct c2_cob_domain_id    cons_cob_dom_id;
        /** rpc session */
        struct c2_rpc_session      cons_rpc_session;
	/** Request handler */
	struct c2_reqh		   cons_reqh;
	/** number of slots */
        int			   cons_nr_slots;
	/** Console server/client ID */
        uint32_t		   cons_rid;
	/** No of items in flight for rpc */
	uint64_t		   cons_items_in_flight;
};

/**
 * @brief Helper function to initialize context for rpc client
 *
 * @param cons context information for rpc connection.
 *
 * @return 0 success, -errno failure.
 */
int c2_cons_rpc_client_init(struct c2_console *cons);

/**
 * @brief Helper function to initialize context for rpc server.
 *
 * @param cons context information for rpc connection.
 *
 * @return 0 success, -errno failure.
 */
int c2_cons_rpc_server_init(struct c2_console *cons);

/**
 * @brief helper function to fni rpc conext for client.
 */
void c2_cons_rpc_client_fini(struct c2_console *cons);

/**
 * @brief helper function to fni rpc conext for client.
 */
void c2_cons_rpc_server_fini(struct c2_console *cons);

/**
 * @brief Creates the RPC connection and establishes session
 *        with provided server.
 *
 * @param cons Console object ref.
 *
 * @return 0 success, -errno failure.
 */
int c2_cons_rpc_client_connect(struct c2_console *cons);

/**
 * @brief Closes the RPC session and connection.
 *
 * @param cons Console object ref.
 *
 * @return 0 success, -errno failure.
 */
int c2_cons_rpc_client_disconnect(struct c2_console *cons);
/**
 * @brief Helper function to set timeout value.
 *
 * @param t1
 */
c2_time_t c2_cons_timeout_construct(uint32_t timeout_secs);

/** @} end of console group */

/* __COLIBRI_CONSOLE_H__ */
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

