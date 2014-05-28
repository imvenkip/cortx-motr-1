/* -*- c -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 19-Dec-2012
 */
#pragma once
#ifndef __MERO_CONF_SCHEMA_H__
#define __MERO_CONF_SCHEMA_H__

/* import */
#include "lib/types.h"
#include "fid/fid.h" /** import m0_fid */

/**
   @page DLD_conf_schema DLD for configuration schema

   This page contains the internal on-disk data structures for Mero
   configuration information.

   - @ref conf_schema "DLD for Configuration Schema"
 */

/**
   @defgroup conf_schema Configuration Schema
   @brief DLD of configuration schema

   This file defines the interfaces and data structures to store and
   access the Mero configuration information in database. Mero
   configuration information is used to describe how a Mero file system
   is organized by storage, nodes, devices, containers, services, etc.

   These data structures are used for on-disk purpose.

   <hr>
   @section DLD-ovw Overview

   This DLD contains the data structures and routines to organize the
   configuration information of Mero, to access these information.
   These configuration information is stored in database, and is populated
   to clients and servers who are using these information to take the whole
   Mero system up. These information is cached on clients and servers.
   These information is treated as resources, which are protected by locks.
   So configuration may be invalidated and re-acquired (that is update)
   by users when resources are revoked.

   The configuration schema is to provide a way to store, load, and update
   these informations. How to maintain the relations in-between these data
   strucctures is done by its upper layer.

   @see HLD of configuration schema <a>https://docs.google.com/a/xyratex.com/document/d/1JmsVBV8B4R-FrrYyJC_kX2ibzC1F-yTHEdrm3-FLQYk/edit?hl=en_US</a>
   @{
*/

/**
   state bits for node, device, nic, etc. These bits can be OR'd and tested.
*/
enum m0_cfg_state_bit {
	/** set if Online, and clear if Offline */
	M0_CFG_STATE_ONLINE  = 1 << 0,

	/** set if Good, and clear if Failed    */
	M0_CFG_STATE_GOOD    = 1 << 1
};

/**
   Property flag bits for node, device, nic, etc. These bits can be OR'd and tested.
*/
enum m0_cfg_flag_bit {
	/** set if real machine, and clear if virtual machine */
	M0_CFG_FLAG_REAL          = 1 << 0,

	/** set if Little-endian CPU, and clear if Big-endian CPU */
	M0_CFG_FLAG_LITTLE_ENDIAN = 1 << 1,

	/** set if a disk/device is removable */
	M0_CFG_FLAG_REMOVABLE     = 1 << 2,
};


/**
   Network interface types.
*/
enum m0_cfg_nic_type {
	/** Ethernet, 10Mb */
	M0_CFG_NIC_ETHER10 = 1,

	/** Ethernet, 100Mb */
	M0_CFG_NIC_ETHER100,

	/** Ethernet, 1000Mb */
	M0_CFG_NIC_ETHER1000,

	/** Ethernet, 10gb */
	M0_CFG_NIC_ETHER10GB,

	/** Infini/Band */
	M0_CFG_NIC_INFINIBAND
};

/**
   Mero device interface types.
*/
enum m0_cfg_storage_device_interface_type {
	M0_CFG_DEVICE_INTERFACE_ATA = 1,  /**< ATA     */
	M0_CFG_DEVICE_INTERFACE_SATA,     /**< SATA    */
	M0_CFG_DEVICE_INTERFACE_SCSI,     /**< SCSI    */
	M0_CFG_DEVICE_INTERFACE_SATA2,    /**< SATA II */
	M0_CFG_DEVICE_INTERFACE_SCSI2,    /**< SCSI II */
	M0_CFG_DEVICE_INTERFACE_SAS,      /**< SAS     */
	M0_CFG_DEVICE_INTERFACE_SAS2      /**< SAS II  */
};


/**
   Mero device media types.
*/
enum m0_cfg_storage_device_media_type {
	/** spin disk       */
	M0_CFG_DEVICE_MEDIA_DISK = 1,

	/** SSD or flash memory */
	M0_CFG_DEVICE_MEDIA_SSD,

	/** tape            */
	M0_CFG_DEVICE_MEDIA_TAPE,

	/** read-only memory, like CD */
	M0_CFG_DEVICE_MEDIA_ROM
};

enum {
	/** maximum number of params */
	M0_CFG_PARAM_LEN = 128
};

/** Type of Mero service. */
enum m0_conf_service_type {
	M0_CST_MDS = 1, /*< Meta-data service. */
	M0_CST_IOS,     /*< IO/data service. */
	M0_CST_MGS,     /*< Management service (confd?). */
	M0_CST_RMS,     /*< Resource management service. */
	M0_CST_SS,      /*< Stats service */
	M0_CST_HA       /*< HA service */
};

/**
   @} conf_schema end group
*/

#endif /* __MERO_CONF_SCHEMA_H__ */
