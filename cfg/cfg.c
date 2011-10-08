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
 * Original author: Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 09/27/2011
 */

/**
   @page DLD-conf.schema DLD for configuration schema

   - @ref DLD-ovw

   <hr>
   @section DLD-ovw Overview

   This DLD contains the data structures and routines to organize the
   configuration information of Colibri, to access these information.
   These configuration information is stored in database, and is populated
   to clients and servers who are using these information to take the whole
   Colibri system up. These information is cached on clients and servers.
   These information is treated as resources, which are protected by locks.
   So configuration may be invalidated and re-acquired (that is update)
   by users when resources are revoked.

   The configuration schema is to provide a way to store, load, and update
   these informations. How to maintain the relations in-between these data
   strucctures is done by its upper layer.
 */

#include "cfg/cfg.h"

/**
   @addtogroup conf.schema
   @{
*/

struct c2_cfg_env cfg_env;

int  c2_cfg_env_init(struct c2_cfg_env *env)
{
	/*
	   Init the database environment, tables, transaction.
           Init the mutex lock and others.
	*/
	return 0;
}

void c2_cfg_env_fini(struct c2_cfg_env *env, bool commit)
{
	/*
	  Commit or abort the database transaction. Close the database tables
	  and the environment.
	  Fini the mutex lock and others.
	*/
}

int c2_cfg_device_insert(struct c2_cfg_env *env, struct c2_cfg_device *device)
{

	return 0;
}

int c2_cfg_device_update(struct c2_cfg_env *env, struct c2_cfg_device *device)
{

	return 0;
}

int c2_cfg_device_delete(struct c2_cfg_env *env, struct c2_cfg_device *device)
{

	return 0;
}

int c2_cfg_device_list(struct c2_cfg_env *env, const struct c2_cfg_device *last,
		       struct c2_cfg_device *results, const int len)
{
	/*
	   According to the @last device, init the db iterator, scan the table,
	   fill the @results array to maximal length of @len. By the end, return
	   the number of devices filled. If the returned count is less than the
	   array length or is zero, the end is reached.
	 */

	return 0;
}

int c2_cfg_fs_insert(struct c2_cfg_env *env, struct c2_cfg_filesystem *fs)
{

	return 0;
}

int c2_cfg_fs_update(struct c2_cfg_env *env, struct c2_cfg_filesystem *fs)
{

	return 0;
}

int c2_cfg_fs_delete(struct c2_cfg_env *env, struct c2_cfg_filesystem *fs)
{

	return 0;
}

int c2_cfg_fs_list(struct c2_cfg_env *env, const struct c2_cfg_filesystem *last,
		   struct c2_cfg_filesystem *results, const int len)
{
	return 0;
}

int c2_cfg_nic_insert(struct c2_cfg_env *env, struct c2_cfg_nic *nic)
{

	return 0;
}

int c2_cfg_nic_update(struct c2_cfg_env *env, struct c2_cfg_nic *nic)
{

	return 0;
}

int c2_cfg_nic_delete(struct c2_cfg_env *env, struct c2_cfg_nic *nic)
{

	return 0;
}

int c2_cfg_nic_list(struct c2_cfg_env *env, const struct c2_cfg_nic *last,
		    struct c2_cfg_nic *results, const int len)
{
	return 0;
}
int c2_cfg_node_insert(struct c2_cfg_env *env, struct c2_cfg_node *node)
{

	return 0;
}

int c2_cfg_node_update(struct c2_cfg_env *env, struct c2_cfg_node *node)
{

	return 0;
}

int c2_cfg_node_delete(struct c2_cfg_env *env, struct c2_cfg_node *node)
{

	return 0;
}

int c2_cfg_node_list(struct c2_cfg_env *env, const struct c2_cfg_node *last,
		     struct c2_cfg_node *results, const int len)
{
	return 0;
}

int c2_cfg_partition_insert(struct c2_cfg_env *env, struct c2_cfg_partition *p)
{

	return 0;
}

int c2_cfg_partition_update(struct c2_cfg_env *env, struct c2_cfg_partition *p)
{

	return 0;
}

int c2_cfg_partition_delete(struct c2_cfg_env *env, struct c2_cfg_partition *p)
{

	return 0;
}

int c2_cfg_partition_list(struct c2_cfg_env *env,
			  const struct c2_cfg_partition *last,
		          struct c2_cfg_partition *results, const int len)
{
	return 0;
}
int c2_cfg_param_insert(struct c2_cfg_env *env, struct c2_cfg_param *p)
{

	return 0;
}

int c2_cfg_param_update(struct c2_cfg_env *env, struct c2_cfg_param *p)
{

	return 0;
}

int c2_cfg_param_delete(struct c2_cfg_env *env, struct c2_cfg_param *p)
{

	return 0;
}

int c2_cfg_param_list(struct c2_cfg_env *env, const struct c2_cfg_param *last,
		      struct c2_cfg_param *results, const int len)
{
	return 0;
}

int c2_cfg_pool_insert(struct c2_cfg_env *env, struct c2_cfg_pool *p)
{

	return 0;
}

int c2_cfg_pool_update(struct c2_cfg_env *env, struct c2_cfg_pool *p)
{

	return 0;
}

int c2_cfg_pool_delete(struct c2_cfg_env *env, struct c2_cfg_pool *p)
{

	return 0;
}

int c2_cfg_pool_list(struct c2_cfg_env *env, const struct c2_cfg_pool *last,
		     struct c2_cfg_pool *results, const int len)
{
	return 0;
}

int c2_cfg_profile_insert(struct c2_cfg_env *env, struct c2_cfg_profile *p)
{

	return 0;
}

int c2_cfg_profile_update(struct c2_cfg_env *env, struct c2_cfg_profile *p)
{

	return 0;
}

int c2_cfg_profile_delete(struct c2_cfg_env *env, struct c2_cfg_profile *p)
{

	return 0;
}

int c2_cfg_profile_list(struct c2_cfg_env *env,
			const struct c2_cfg_profile *last,
			struct c2_cfg_profile *results, const int len)
{
	return 0;
}

int c2_cfg_service_insert(struct c2_cfg_env *env, struct c2_cfg_service *s)
{
	return 0;
}

int c2_cfg_service_update(struct c2_cfg_env *env, struct c2_cfg_service *s)
{
	return 0;
}

int c2_cfg_service_delete(struct c2_cfg_env *env, struct c2_cfg_service *s)
{
	return 0;
}

int c2_cfg_service_list(struct c2_cfg_env *env,
			const struct c2_cfg_service *last,
			struct c2_cfg_service *results, const int len)
{
	return 0;
}



/** @} end-of-conf.schema */


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
