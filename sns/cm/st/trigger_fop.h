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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 09/11/2011
 */

#pragma once

#ifndef __MERO_SNS_CM_ST_TRIGGER_FOP_H__
#define __MERO_SNS_CM_ST_TRIGGER_FOP_H__

#include "lib/types.h"
#include "xcode/xcode_attr.h"

/** Sequence of file sizes to be repaired. */
struct file_sizes {
	uint32_t  f_nr;
	uint64_t *f_size;
} M0_XCA_SEQUENCE;

/**
 * Simplistic implementation of sns repair trigger fop for testing purposes
 * only.
 */
struct trigger_fop {
	uint64_t          fdata;
	uint64_t          N;
	uint64_t          K;
	uint64_t          P;
	struct file_sizes fsize;
	uint32_t          op;
} M0_XCA_RECORD;

struct trigger_rep_fop {
	uint32_t rc;
} M0_XCA_RECORD;

int m0_sns_repair_trigger_fop_init(void);
void m0_sns_repair_trigger_fop_fini(void);

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
