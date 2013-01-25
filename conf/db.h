/* -*- C -*- */
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
 * Original author: Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>
 * Original creation date: 19-Sep-2012
 */
#pragma once
#ifndef __MERO_CONF_DB_H__
#define __MERO_CONF_DB_H__

struct m0_confx;
struct m0_confx_obj;

/**
 * Creates configuration database, populating it with provided
 * configuration data.
 *
 * @pre  conf->cx_nr > 0
 */
M0_INTERNAL int m0_confdb_create(const char *dbpath,
				 const struct m0_confx *conf);

/**
 * Creates m0_confx and populates it with data read from a
 * configuration database.
 *
 * If the call succeeds, the user is responsible for freeing allocated
 * memory with m0_confx_free(*out).
 */
M0_INTERNAL int m0_confdb_read(const char *dbpath, struct m0_confx **out);

#endif /* __MERO_CONF_DB_H__ */
