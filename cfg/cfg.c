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
