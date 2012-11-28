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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 19-Sep-2012
 */
#pragma once
#ifndef __MERO_CONF_XCODE_H__
#define __MERO_CONF_XCODE_H__

#include "lib/buf.h"
struct confx_object;

/**
 * Serialized representation of a configuration object
 */
struct m0_conf_xcode_pair {
	struct m0_buf xp_key; /** Representation of an object id */
	struct m0_buf xp_val; /** Representation of an object fields */
};

/**
 * Serializes configuration object into xcode representation.
 *
 * Resulting m0_conf_xcode_pair can be stored in the configuration database.
 *
 * Notes:
 *  1. User is responsible for freeing dest->xp_val.
 *  2. xcode interfaces don't allow `src' to be const.
 */
M0_INTERNAL int
m0_confx_encode(struct confx_object *src, struct m0_conf_xcode_pair *dest);

/**
 * Deserializes on-wire representation of a configuration object.
 *
 * Notes:
 *  1. User is responsible for freeing dest array with m0_confx_fini().
 *  2. xcode interfaces don't allow `src' to be const.
 */
M0_INTERNAL int m0_confx_decode(struct m0_conf_xcode_pair *src,
				struct confx_object *dest);

/**
 * Creates configuration db, populating it with given configuration data.
 *
 * @param db_name  path to the database
 * @param obj      input array of configuration objects
 * @param obj_nr   the number of objects
 */
M0_INTERNAL int m0_confx_db_create(const char *db_name,
				   struct confx_object *obj, size_t obj_nr);

/**
 * Reads configuration db, into the given configuration objects array
 *
 * @param db_name  path to the database
 * @param obj      resulting array of configuration objects
 *
 * @returns => 0 The number of read configuration objects
 * @returns < 0  Error code
 *
 * @note User has to m0_free obj, and call m0_xcode_free for all obj[i]
 */
M0_INTERNAL int m0_confx_db_read(const char *db_name,
				 struct confx_object **obj);

/** xcode type initializer defined in onwire.h */
M0_INTERNAL int m0_confx_types_init(void);

/** xcode type finalizer defined in onwire.h */
M0_INTERNAL void m0_confx_types_fini(void);

#endif /* __MERO_CONF_XCODE_H__ */
