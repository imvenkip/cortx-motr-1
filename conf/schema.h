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

/*
 * XXX TODO: Delete `cfg/cfg.h' after moving necessary definitions here,
 * to conf/schema.h.
 */

/** Type of Mero service. */
enum m0_conf_service_type {
	M0_CST_MDS = 1, /*< Meta-data service. */
	M0_CST_IOS,     /*< IO/data service. */
	M0_CST_MGS,     /*< Management service (confd?). */
	M0_CST_DLM,     /*< DLM service. */
	M0_CST_SS       /*< Stats service */
};

#endif /* __MERO_CONF_SCHEMA_H__ */
