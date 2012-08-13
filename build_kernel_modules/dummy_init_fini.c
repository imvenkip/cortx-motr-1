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

#include <linux/module.h>

/*
  This file contains dummy init() and fini() routines for modules, that are
  not yet ported to kernel.

  Once the module compiles successfully for kernel mode, dummy routines from
  this file should be removed.
 */

#define DUMMY_IMPLEMENTATION \
	printk("dummy implementation of %s called\n", __FUNCTION__)

int c2_memory_init(void)
{
	DUMMY_IMPLEMENTATION;
	return 0;
}

void c2_memory_fini(void)
{

}

int c2_threads_init(void)
{
	DUMMY_IMPLEMENTATION;
	return 0;
}

void c2_threads_fini(void)
{

}

int c2_db_init(void)
{
	DUMMY_IMPLEMENTATION;
	return 0;
}

void c2_db_fini(void)
{

}

int c2_linux_stobs_init(void)
{
	DUMMY_IMPLEMENTATION;
	return 0;
}

void c2_linux_stobs_fini(void)
{

}

int c2_ad_stobs_init(void)
{
	DUMMY_IMPLEMENTATION;
	return 0;
}

void c2_ad_stobs_fini(void)
{

}

int sim_global_init(void)
{
	DUMMY_IMPLEMENTATION;
	return 0;
}

void sim_global_fini(void)
{

}

int c2_timers_init(void)
{
	DUMMY_IMPLEMENTATION;
	return 0;
}

void c2_timers_fini(void)
{

}

struct c2_reqh_service;
struct c2_reqh;
struct c2_reqh_service *c2_reqh_service_get(const char *service_name,
                                            struct c2_reqh *reqh)
{
	return NULL;
}
