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

#ifndef __MERO_RPC_CORE_OPCODES_H__
#define __MERO_RPC_CORE_OPCODES_H__

enum M0_RPC_OPCODES {
	/** Mero setup rpc */
	M0_CS_DS1_REQ_OPCODE                = 3,
	M0_CS_DS1_REP_OPCODE                = 4,
	M0_CS_DS2_REQ_OPCODE                = 5,
	M0_CS_DS2_REP_OPCODE                = 6,

	/** Console rpc */
	M0_CONS_FOP_DEVICE_OPCODE           = 7,
	M0_CONS_FOP_REPLY_OPCODE            = 8,
	M0_CONS_TEST                        = 9,

	/** Fol rpc */
	M0_FOL_ANCHOR_TYPE_OPCODE           = 10,
	M0_FOL_UT_OPCODE                    = 11,

	/** Fop iterator rpc */
	M0_FOP_ITERATOR_TEST_OPCODE         = 12,

	/** Request handler rpc */
	M0_REQH_ERROR_REPLY_OPCODE          = 13,
	M0_REQH_UT_DUMMY_OPCODE             = 14,

	/** Stob IO rpc */
	M0_STOB_IO_CREATE_REQ_OPCODE        = 15,
	M0_STOB_IO_WRITE_REQ_OPCODE         = 16,
	M0_STOB_IO_READ_REQ_OPCODE          = 17,
	M0_STOB_IO_CREATE_REPLY_OPCODE      = 18,
	M0_STOB_IO_WRITE_REPLY_OPCODE       = 19,
	M0_STOB_IO_READ_REPLY_OPCODE        = 20,
	M0_STOB_UT_WRITE_OPCODE             = 21,
	M0_STOB_UT_READ_OPCODE              = 22,
	M0_STOB_UT_CREATE_OPCODE            = 23,
	M0_STOB_UT_WRITE_REPLY_OPCODE       = 24,
	M0_STOB_UT_READ_REPLY_OPCODE        = 25,
	M0_STOB_UT_CREATE_REPLY_OPCODE      = 26,
	M0_STOB_UT_QUIT_OPCODE              = 27,

	/** RPC module */
	M0_RPC_PING_OPCODE                  = 28,
	M0_RPC_PING_REPLY_OPCODE            = 29,
	M0_RPC_NOOP_OPCODE                  = 30,
	M0_RPC_ONWIRE_UT_OPCODE             = 31,

	M0_RPC_CONN_ESTABLISH_OPCODE        = 32,
	M0_RPC_CONN_ESTABLISH_REP_OPCODE    = 33,

	M0_RPC_SESSION_ESTABLISH_OPCODE     = 34,
	M0_RPC_SESSION_ESTABLISH_REP_OPCODE = 35,

	M0_RPC_SESSION_TERMINATE_OPCODE     = 36,
	M0_RPC_SESSION_TERMINATE_REP_OPCODE = 37,

	M0_RPC_CONN_TERMINATE_OPCODE        = 38,
	M0_RPC_CONN_TERMINATE_REP_OPCODE    = 39,

	/** Network rpc */
	M0_NET_TEST_OPCODE                  = 40,

	/** I/O service read & write */
	M0_IOSERVICE_READV_OPCODE           = 41,
	M0_IOSERVICE_WRITEV_OPCODE          = 42,
	M0_IOSERVICE_READV_REP_OPCODE       = 43,
	M0_IOSERVICE_WRITEV_REP_OPCODE      = 44,
	/** I/O service cob creation, deletion, truncation */
	M0_IOSERVICE_COB_CREATE_OPCODE      = 45,
	M0_IOSERVICE_COB_DELETE_OPCODE      = 46,
	M0_IOSERVICE_COB_TRUNCATE_OPCODE    = 47,
	M0_IOSERVICE_COB_OP_REPLY_OPCODE    = 48,
	M0_IOSERVICE_FV_NOTIFICATION_OPCODE = 49,

	/** Xcode rpc */
	M0_XCODE_UT_OPCODE                  = 50,

	/** FOP module */
	M0_FOP_RDWR_OPCODE                  = 51,
	M0_FOP_RDWR_REPLY_OPCODE            = 52,

	/** Configuration rpc */
	M0_CONF_FETCH_OPCODE                = 53,
	M0_CONF_FETCH_RESP_OPCODE           = 54,
	M0_CONF_UPDATE_OPCODE               = 55,
	M0_CONF_UPDATE_RESP_OPCODE          = 56,

	/* Mdservice fops */
	M0_MDSERVICE_CREATE_OPCODE          = 57,
	M0_MDSERVICE_LOOKUP_OPCODE          = 58,
	M0_MDSERVICE_LINK_OPCODE            = 59,
	M0_MDSERVICE_UNLINK_OPCODE          = 60,
	M0_MDSERVICE_RENAME_OPCODE          = 61,
	M0_MDSERVICE_OPEN_OPCODE            = 62,
	M0_MDSERVICE_CLOSE_OPCODE           = 63,
	M0_MDSERVICE_SETATTR_OPCODE         = 64,
	M0_MDSERVICE_GETATTR_OPCODE         = 65,
	M0_MDSERVICE_SETXATTR_OPCODE        = 66,
	M0_MDSERVICE_GETXATTR_OPCODE        = 67,
	M0_MDSERVICE_DELXATTR_OPCODE        = 68,
	M0_MDSERVICE_LISTXATTR_OPCODE       = 69,
	M0_MDSERVICE_STATFS_OPCODE          = 70,
	M0_MDSERVICE_READDIR_OPCODE         = 71,
	M0_MDSERVICE_CREATE_REP_OPCODE      = 72,
	M0_MDSERVICE_LOOKUP_REP_OPCODE      = 73,
	M0_MDSERVICE_LINK_REP_OPCODE        = 74,
	M0_MDSERVICE_UNLINK_REP_OPCODE      = 75,
	M0_MDSERVICE_RENAME_REP_OPCODE      = 76,
	M0_MDSERVICE_OPEN_REP_OPCODE        = 77,
	M0_MDSERVICE_CLOSE_REP_OPCODE       = 78,
	M0_MDSERVICE_SETATTR_REP_OPCODE     = 79,
	M0_MDSERVICE_GETATTR_REP_OPCODE     = 80,
	M0_MDSERVICE_STATFS_REP_OPCODE      = 81,
	M0_MDSERVICE_READDIR_REP_OPCODE     = 82,
	M0_MDSERVICE_SETXATTR_REP_OPCODE    = 83,
	M0_MDSERVICE_GETXATTR_REP_OPCODE    = 84,
	M0_MDSERVICE_DELXATTR_REP_OPCODE    = 85,
	M0_MDSERVICE_LISTXATTR_REP_OPCODE   = 86,

	/* RPC UT */
	M0_RPC_ARROW_OPCODE                 = 87,

	/** Resource manager opcodes */
	M0_RM_FOP_BORROW                    = 88,
	M0_RM_FOP_BORROW_REPLY              = 89,
	M0_RM_FOP_REVOKE                    = 90,
	M0_RM_FOP_REVOKE_REPLY              = 91,
	M0_RM_FOP_CANCEL                    = 92,

	/* SNS copy packet. */
	M0_SNS_CM_REPAIR_CP_OPCODE          = 93,
	M0_SNS_CM_REPAIR_CP_REP_OPCODE      = 94,
	M0_SNS_CM_REBALANCE_CP_OPCODE       = 95,
	M0_SNS_CM_REBALANCE_CP_REP_OPCODE   = 96,
	M0_SNS_REPAIR_TRIGGER_OPCODE        = 97,
	M0_SNS_REPAIR_TRIGGER_REP_OPCODE    = 98,
	M0_SNS_REBALANCE_TRIGGER_OPCODE     = 99,
	M0_SNS_REBALANCE_TRIGGER_REP_OPCODE = 100,
	/* SNS sliding window update fop. */
	M0_SNS_CM_REPAIR_SW_FOP_OPCODE      = 101,
	M0_SNS_CM_REBALANCE_SW_FOP_OPCODE   = 102,

	/* RPC UB */
	M0_RPC_UB_REQ_OPCODE                = 103,
	M0_RPC_UB_RESP_OPCODE               = 104,

	/* Pool */
	M0_POOLMACHINE_QUERY_OPCODE         = 105,
	M0_POOLMACHINE_QUERY_REP_OPCODE     = 106,
	M0_POOLMACHINE_SET_OPCODE           = 107,
	M0_POOLMACHINE_SET_REP_OPCODE       = 108,

	/* Stats fops */
	M0_STATS_UPDATE_FOP_OPCODE          = 109,
	M0_STATS_QUERY_FOP_OPCODE           = 110,
	M0_STATS_QUERY_REP_FOP_OPCODE       = 111,

	/* DTM */
	M0_DTM_NOTIFICATION_OPCODE          = 112,
	M0_DTM_UP_OPCODE                    = 113,

	/* High Availability opcodes */
	M0_HA_NOTE_GET_OPCODE               = 114,
	M0_HA_NOTE_GET_REP_OPCODE           = 115,
	M0_HA_NOTE_SET_OPCODE               = 116,
	M0_HA_OLD_ENTRYPOINT_REQ_OPCODE     = 1117,
	M0_HA_OLD_ENTRYPOINT_REP_OPCODE     = 1118,

	/* fsync fops */
	M0_FSYNC_MDS_OPCODE                 = 119,
	M0_FSYNC_MDS_REP_OPCODE             = 120,
	M0_FSYNC_IOS_OPCODE                 = 121,
	M0_FSYNC_IOS_REP_OPCODE             = 122,

	/* cob getattr & reply */
	M0_IOSERVICE_COB_GETATTR_OPCODE     = 128,
	M0_IOSERVICE_COB_GETATTR_REP_OPCODE = 129,
	/* cob setattr & reply */
	M0_IOSERVICE_COB_SETATTR_OPCODE     = 130,
	M0_IOSERVICE_COB_SETATTR_REP_OPCODE = 131,

	/** Spiel opcodes */
	M0_SPIEL_CONF_FILE_OPCODE           = 138,
	M0_SPIEL_CONF_FILE_REP_OPCODE       = 139,
	M0_SPIEL_CONF_FLIP_OPCODE           = 140,
	M0_SPIEL_CONF_FLIP_REP_OPCODE       = 141,

	/** ADDB2 */
	M0_ADDB_FOP_OPCODE                  = 150,

	/* reqh/ut/reqh_fop_allow_ut.c */
	M0_REQH_UT_ALLOW_OPCODE             = 151,

	/* SNS repair/rebalance quiesce */
	M0_SNS_REPAIR_QUIESCE_OPCODE        = 152,
	M0_SNS_REPAIR_QUIESCE_REP_OPCODE    = 153,
	M0_SNS_REBALANCE_QUIESCE_OPCODE     = 154,
	M0_SNS_REBALANCE_QUIESCE_REP_OPCODE = 155,

	/* SNS repair/rebalance status query */
	M0_SNS_REPAIR_STATUS_OPCODE         = 156,
	M0_SNS_REPAIR_STATUS_REP_OPCODE     = 157,
	M0_SNS_REBALANCE_STATUS_OPCODE      = 158,
	M0_SNS_REBALANCE_STATUS_REP_OPCODE  = 159,
	M0_SNS_REPAIR_ABORT_OPCODE          = 160,
	M0_SNS_REPAIR_ABORT_REP_OPCODE      = 161,

	/** SSS Service fops */
	M0_SSS_SVC_REQ_OPCODE               = 200,
	M0_SSS_SVC_REP_OPCODE               = 201,

	/** SSS process fops */
	M0_SSS_PROCESS_REQ_OPCODE           = 202,
	M0_SSS_PROCESS_REP_OPCODE           = 203,
	M0_SSS_PROCESS_SVC_LIST_REP_OPCODE  = 204,

	/** SSS device fops */
	M0_SSS_DEVICE_REQ_OPCODE            = 205,
	M0_SSS_DEVICE_REP_OPCODE            = 206,

	/** HA link opcodes */
	M0_HA_LINK_MSG_REQ                  = 207,
	M0_HA_LINK_MSG_REP                  = 208,

	/** CAS fops. */
	M0_CAS_GET_FOP_OPCODE               = 230,
	M0_CAS_PUT_FOP_OPCODE               = 231,
	M0_CAS_DEL_FOP_OPCODE               = 232,
	M0_CAS_CUR_FOP_OPCODE               = 233,
	M0_CAS_REP_FOP_OPCODE               = 234,

	/*
	 * Identifiers below are for fop-less foms, not fops.
	 */
	M0_FOM_OPCODE_START                 = 1024,
	M0_BE_TX_GROUP_OPCODE               = 1025,
	M0_CM_UT_OPCODE                     = 1026, /* CP, PUMP, SW_UPDATE, STORE */
	M0_CM_REBALANCE_OPCODE              = 1030, /* CP, PUMP, SW_UPDATE, STORE */
	M0_CM_REPAIR_OPCODE                 = 1034, /* CP, PUMP, SW_UPDATE, STORE */
	M0_CM_UT_SENDER_OPCODE              = 1038, /* CP, PUMP, SW_UPDATE, STORE */
	M0_UB_FOM_OPCODE                    = 1042,
	M0_UT_RDWR_OPCODE                   = 1043,
	M0_UT_STATS_OPCODE                  = 1044,
	M0_UT_IOS_OPCODE                    = 1045,
	M0_RPC_LINK_CONN_OPCODE             = 1046,
	M0_RPC_LINK_DISC_OPCODE             = 1047,
	M0_HA_LINK_OUTGOING_OPCODE          = 1048,

	M0_OPCODES_NR                       = 2048
};

/** @} endgroup rpc_layer_core */
#endif /* __MERO_RPC_CORE_OPCODES_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
