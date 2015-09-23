/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Igor Vartanov <igor.vartanov@seagate.com>
 * Original creation date: 17-Aug-2015
 */

#pragma once

#ifndef __MERO_SPIEL_CMD_INTERNAL_H__
#define __MERO_SPIEL_CMD_INTERNAL_H__

/**
 * @addtogroup spiel-api-fspec-intr
 * @{
 */

#define SPIEL_CONF_OBJ_FIND(confc, profile, fid, conf_obj, filter, ...) \
	_spiel_conf_obj_find(confc, profile, fid, filter,               \
			     M0_COUNT_PARAMS(__VA_ARGS__) + 1,          \
			     (const struct m0_fid []){                  \
			     __VA_ARGS__, M0_FID0 },                    \
			     conf_obj)

#define SPIEL_CONF_DIR_ITERATE(confc, profile, ctx, iter_cb, fs_test_cb, ...) \
	_spiel_conf_dir_iterate(confc, profile, ctx, iter_cb, fs_test_cb,     \
				M0_COUNT_PARAMS(__VA_ARGS__) + 1,       \
				(const struct m0_fid []){               \
				__VA_ARGS__, M0_FID0 })


enum {
	SPIEL_MAX_RPCS_IN_FLIGHT = 1,
	SPIEL_CONN_TIMEOUT       = 5, /* seconds */
};

#define SPIEL_DEVICE_FORMAT_TIMEOUT   m0_time_from_now(10*60, 0)

struct spiel_string_entry {
	char            *sse_string;
	struct m0_tlink  sse_link;
	uint64_t         sse_magic;
};

/****************************************************/
/*                    Filesystem                    */
/****************************************************/

/** Descriptor for an item to be polled about stats */
struct _stats_item {
	struct m0_fid   i_fid;           /**< respective conf object's fid    */
	struct m0_tlink i_link;          /**< link in _fs_stats_ctx::fx_items */
	uint64_t        i_magic;
};

/** filesystem stats collection context */
struct _fs_stats_ctx {
	/**
	 * The most recent retcode. Normally it must be zero during all the
	 * stats collection. And when it becomes non-zero, the stats collection
	 * is interrupted, and the retcode is conveyed to client.
	 */
	int              fx_rc;
	struct m0_spiel *fx_spl;         /**< spiel instance      */
	m0_bcount_t      fx_free;        /**< free space          */
	m0_bcount_t      fx_total;       /**< total space         */
	struct m0_tl     fx_items;       /**< _stats_item list    */
	struct m0_fid    fx_fid;         /**< filesystem fid      */
	/** stats item type to be enlisted */
	const struct m0_conf_obj_type *fx_type;
};

#define SPIEL_LOGLVL M0_DEBUG

/** @} */

#endif /* __MERO_SPIEL_CMD_INTERNAL_H__*/

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
