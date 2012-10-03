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
 * Original author: Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 10/13/2011
 */

#pragma once

#ifndef __COLIBRI_DUMMY_INIT_FINI_H__
#define __COLIBRI_DUMMY_INIT_FINI_H__

int c2_trace_init(void);
void c2_trace_fini(void);

int c2_memory_init(void);
void c2_memory_fini(void);

int c2_threads_init(void);
void c2_threads_fini(void);

int c2_db_init(void);
void c2_db_fini(void);

int c2_linux_stobs_init(void);
void c2_linux_stobs_fini(void);

int c2_ad_stobs_init(void);
void c2_ad_stobs_fini(void);

int sim_global_init(void);
void sim_global_fini(void);

int c2_reqhs_init(void);
void c2_reqhs_fini(void);

int c2_timers_init(void);
void c2_timers_fini(void);

#endif /* __COLIBRI_DUMMY_INIT_FINI_H__ */
