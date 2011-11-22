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
 * Original author: Subhash Arya <subhash_arya@xyratex.com>,
 * Original creation date: 11/4/2011
 */
/**
   @addtogroup rpc_layer_core

   This file is a centralized (temporary) location for opcodes.
   An opcode is an rpc item type attribute which uniquely identifies an rpc
   item type.
   @see rpc/rpccore.h
   @see rpc/rpc_base.h

   @{
 */

enum C2_RPC_OPCODES {
	/** Network rpc opcodes */
	C2_NET_TEST_OPCODE = 1,

	/** ADDB rpc opcodes */
	C2_ADDB_RECORD_REQUEST_OPCODE = 2,
	C2_ADDB_REPLY_OPCODE = 3,

	/** Bulk emulation rpc opcodes */
	C2_BULK_SUNRPC_MSG_OPCODE = 4,
	C2_BULK_SUNRPC_GET_OPCODE = 5,
	C2_BULK_SUNRPC_PUT_OPCODE= 6,
	C2_BULK_SUNRPC_MSG_REPLY_OPCODE = 7,
	C2_BULK_SUNRPC_GET_REPLY_OPCODE = 8,
	C2_BULK_SUNRPC_PUT_REPLY_OPCODE= 9,

	/** Colibri set up rpc opcodes */
	C2_CS_DS1_REQ_OPCODE = 10,
        C2_CS_DS1_REP_OPCODE = 11,
        C2_CS_DS2_REQ_OPCODE = 12,
        C2_CS_DS2_REP_OPCODE = 13,

	/** Console rpc opcodes */
	C2_CONS_FOP_DISK_OPCODE = 14,
	C2_CONS_FOP_DEVICE_OPCODE = 15,
	C2_CONS_FOP_REPLY_OPCODE = 16,
	C2_CONS_TEST = 17,

	/** Fol rpc opcodes */
	C2_FOL_ANCHOR_TYPE_OPCODE = 18,
	C2_FOL_UT_OPCODE = 19,

	/** Fop iterator rpc opcodes */
	C2_FOP_ITERATOR_TEST_OPCODE = 20,

	/** I/O service rpc opcodes */
	C2_IOSERVICE_READV_OPCODE = 21,
	C2_IOSERVICE_WRITEV_OPCODE = 22,
	C2_IOSERVICE_READV_REP_OPCODE = 23,
	C2_IOSERVICE_WRITEV_REP_OPCODE = 24,

	/** Request handler rpc opcodes */
	C2_REQH_ERROR_REPLY_OPCODE  = 25,
	C2_REQH_UT_CREATE_REQ_OPCODE = 26,
	C2_REQH_UT_WRITE_REQ_OPCODE = 27,
	C2_REQH_UT_READ_REQ_OPCODE = 28,
	C2_REQH_UT_CREATE_REPLY_OPCODE = 29,
	C2_REQH_UT_WRITE_REPLY_OPCODE = 30,
	C2_REQH_UT_READ_REPLY_OPCODE = 31,

	/** RPC module opcodes */
	C2_RPC_PING_OPCODE = 32,
	C2_RPC_PING_REPLY_OPCODE = 33,
	C2_RPC_CONN_ESTABLISH_OPCODE = 34,
        C2_RPC_CONN_TERMINATE_OPCODE = 35,
        C2_RPC_SESSION_ESTABLISH_OPCODE = 36,
        C2_RPC_SESSION_TERMINATE_OPCODE = 37,
        C2_RPC_CONN_ESTABLISH_REP_OPCODE = 38,
        C2_RPC_CONN_TERMINATE_REP_OPCODE = 39,
        C2_RPC_SESSION_ESTABLISH_REP_OPCODE = 40,
        C2_RPC_SESSION_TERMINATE_REP_OPCODE = 41,
        C2_RPC_NOOP_OPCODE = 42,
	C2_RPC_ONWIRE_UT_OPCODE = 43,

	/** Stob rpc opcodes */
	C2_STOB_UT_WRITE_OPCODE = 44,
	C2_STOB_UT_READ_OPCODE = 45,
	C2_STOB_UT_CREATE_OPCODE = 46,
	C2_STOB_UT_QUIT_OPCODE = 47,
	C2_STOB_UT_WRITE_REPLY_OPCODE = 48,
	C2_STOB_UT_READ_REPLY_OPCODE = 49,
	C2_STOB_UT_CREATE_REPLY_OPCODE = 50,

	/** Xcode rpc opcodes */
	C2_XCODE_UT_OPCODE = 51,
};
/** @} endgroup rpc_layer_core */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

