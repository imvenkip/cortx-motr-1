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
#ifndef __COLIBRI_CONF_XCODE_H__
#define __COLIBRI_CONF_XCODE_H__

#include "lib/buf.h"
struct confx_object;

/**
 * Serialized representation of a configuration object
 */
struct c2_conf_xcode_pair {
	struct c2_buf xp_key; /** Representation of an object id */
	struct c2_buf xp_val; /** Representation of an object fields */
};

/**
 * Serializes obj into out_kv.xp_key and out_kv.xp_val used to be written as a
 * key and value into configuration db.
 *
 * @note User is responsible to c2_free out_kv.xp_val, when it's no more needed.
 *
 * @note xcode interfaces don't allow to make obj to be
 * const struct confx_object *
 *
 * @param[in]  obj serialized configuration object
 * @param[out] out_kv serialized configuration buffers, obj is xcoded into
 *
 * @returns = 0  Success.
 * @returns < 0  Error code.
 */
int c2_confx_encode(struct confx_object *obj,
		    struct c2_conf_xcode_pair *out_kv);

/**
 * Deserializes serialized representation of a configuration object
 *
 * @note User is responsible to free out_obj array with c2_confx_fini()
 *
 * @note xcode interfaces don't allow to make kv to be
 * const struct c2_conf_xcode_pair *
 *
 * @param[in]  kv serialized representation of configuration object
 * @param[out] out_obj deserialized configuration object
 *
 * @returns = 0  Success.
 * @returns < 0  Error code.
 */
int c2_confx_decode(struct c2_conf_xcode_pair *kv,
		    struct confx_object *out_obj);

/**
 * Creates configuration db, from the given configuration objects array
 *
 * @param[in]  db_name a path, where db has to be stored
 * @param[in]  obj an array of configuration objects
 * @param[in]  obj_nr a number of initialized objects in obj
 *
 * @returns = 0  Success.
 * @returns < 0  Error code.
 */
int c2_confx_db_create(const char *db_name,
		       struct confx_object *obj, size_t obj_nr);

/**
 * Reads configuration db, into the given configuration objects array
 *
 * @param[in]     db_name a path, where db has is stored
 * @param[inout]  obj an array of configuration objects
 *
 * @returns => 0  Success, the number of read configuration objects
 * @returns <  0  Error code.
 *
 * @note User has to c2_free obj, and call c2_xcode_free for all obj[i]
 */
int c2_confx_db_read(const char *db_name, struct confx_object **obj);

/** xcode type initializer defined in onwire.h */
int c2_confx_types_init(void);

/** xcode type finalizer defined in onwire.h */
void c2_confx_types_fini(void);


#endif /* __COLIBRI_CONF_XCODE_H__ */
