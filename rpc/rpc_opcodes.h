/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
   @addtogroup rpc

   This file is a centralized (temporary) location for opcodes.
   An opcode is an rpc item type attribute which uniquely identifies an rpc
   item type.
   @see rpc/rpccore.h

   @{
 */

#pragma once

#ifndef __COLIBRI_RPC_CORE_OPCODES_H__
#define __COLIBRI_RPC_CORE_OPCODES_H__

enum C2_RPC_OPCODES {
	/** ADDB */
	C2_ADDB_REPLY_OPCODE                = 1,
	C2_ADDB_RECORD_REQUEST_OPCODE       = 2,

	/** Colibri setup rpc */
	C2_CS_DS1_REQ_OPCODE                = 3,
	C2_CS_DS1_REP_OPCODE                = 4,
	C2_CS_DS2_REQ_OPCODE                = 5,
	C2_CS_DS2_REP_OPCODE                = 6,

	/** Console rpc */
	C2_CONS_FOP_DEVICE_OPCODE           = 7,
	C2_CONS_FOP_REPLY_OPCODE            = 8,
	C2_CONS_TEST                        = 9,

	/** Fol rpc */
	C2_FOL_ANCHOR_TYPE_OPCODE           = 10,
	C2_FOL_UT_OPCODE                    = 11,

	/** Fop iterator rpc */
	C2_FOP_ITERATOR_TEST_OPCODE         = 12,

	/** Request handler rpc */
	C2_REQH_ERROR_REPLY_OPCODE	    = 13,

	/** Stob IO rpc */
	C2_STOB_IO_CREATE_REQ_OPCODE        = 14,
	C2_STOB_IO_WRITE_REQ_OPCODE         = 15,
	C2_STOB_IO_READ_REQ_OPCODE          = 16,
	C2_STOB_IO_CREATE_REPLY_OPCODE      = 17,
	C2_STOB_IO_WRITE_REPLY_OPCODE       = 18,
	C2_STOB_IO_READ_REPLY_OPCODE        = 19,
	C2_STOB_UT_WRITE_OPCODE             = 20,
	C2_STOB_UT_READ_OPCODE              = 21,
	C2_STOB_UT_CREATE_OPCODE            = 22,
	C2_STOB_UT_WRITE_REPLY_OPCODE       = 23,
	C2_STOB_UT_READ_REPLY_OPCODE        = 24,
	C2_STOB_UT_CREATE_REPLY_OPCODE      = 25,
	C2_STOB_UT_QUIT_OPCODE              = 26,

	/** RPC module */
	C2_RPC_PING_OPCODE                  = 27,
	C2_RPC_PING_REPLY_OPCODE            = 28,
	C2_RPC_CONN_ESTABLISH_OPCODE        = 29,
	C2_RPC_CONN_TERMINATE_OPCODE        = 30,
	C2_RPC_SESSION_ESTABLISH_OPCODE     = 31,
	C2_RPC_SESSION_TERMINATE_OPCODE     = 32,
	C2_RPC_CONN_ESTABLISH_REP_OPCODE    = 33,
	C2_RPC_CONN_TERMINATE_REP_OPCODE    = 34,
	C2_RPC_SESSION_ESTABLISH_REP_OPCODE = 35,
	C2_RPC_SESSION_TERMINATE_REP_OPCODE = 36,
	C2_RPC_NOOP_OPCODE                  = 37,
	C2_RPC_ONWIRE_UT_OPCODE             = 38,

	/** Network rpc */
	C2_NET_TEST_OPCODE                  = 39,

	/** I/O service read & write */
	C2_IOSERVICE_READV_OPCODE           = 40,
	C2_IOSERVICE_WRITEV_OPCODE          = 41,
	C2_IOSERVICE_READV_REP_OPCODE       = 42,
	C2_IOSERVICE_WRITEV_REP_OPCODE      = 43,
	/** I/O service cob creation and deletion */
	C2_IOSERVICE_COB_CREATE_OPCODE      = 44,
	C2_IOSERVICE_COB_DELETE_OPCODE      = 45,
	C2_IOSERVICE_COB_OP_REPLY_OPCODE    = 46,
	C2_IOSERVICE_FV_NOTIFICATION_OPCODE = 47,

	/** Xcode rpc */
	C2_XCODE_UT_OPCODE                  = 48,

	/** FOP module */
	C2_FOP_RDWR_OPCODE                  = 49,
	C2_FOP_RDWR_REPLY_OPCODE            = 50,

	/** Configuration rpc */
	C2_CONF_FETCH_OPCODE                = 51,
	C2_CONF_FETCH_RESP_OPCODE           = 52,
	C2_CONF_UPDATE_OPCODE               = 53,
	C2_CONF_UPDATE_RESP_OPCODE          = 54,

        /* Mdservice fops */
        C2_MDSERVICE_CREATE_OPCODE          = 64,
        C2_MDSERVICE_LINK_OPCODE            = 65,
        C2_MDSERVICE_UNLINK_OPCODE          = 66,
        C2_MDSERVICE_RENAME_OPCODE          = 67,
        C2_MDSERVICE_OPEN_OPCODE            = 68,
        C2_MDSERVICE_CLOSE_OPCODE           = 69,
        C2_MDSERVICE_SETATTR_OPCODE         = 70,
        C2_MDSERVICE_GETATTR_OPCODE         = 71,
        C2_MDSERVICE_READDIR_OPCODE         = 72,
        C2_MDSERVICE_CREATE_REP_OPCODE      = 73,
        C2_MDSERVICE_LINK_REP_OPCODE        = 74,
        C2_MDSERVICE_UNLINK_REP_OPCODE      = 75,
        C2_MDSERVICE_RENAME_REP_OPCODE      = 76,
        C2_MDSERVICE_OPEN_REP_OPCODE        = 77,
        C2_MDSERVICE_CLOSE_REP_OPCODE       = 78,
        C2_MDSERVICE_SETATTR_REP_OPCODE     = 79,
        C2_MDSERVICE_GETATTR_REP_OPCODE     = 80,
        C2_MDSERVICE_READDIR_REP_OPCODE     = 81,

	C2_SNS_REPAIR_TRIGGER_OPCODE        = 82,
	C2_SNS_REPAIR_TRIGGER_REP_OPCODE    = 83
};
/** @} endgroup rpc_layer_core */

#endif /* __COLIBRI_RPC_CORE_OPCODES_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

