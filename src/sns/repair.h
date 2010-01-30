/* -*- C -*- */

#ifndef __SNS_REPAIR_H__
#define __SNS_REPAIR_H__

/**
   @page snsrepair SNS repair detailed level design specification.

   @section Overview

   @section Definitions

   @section repairfuncspec Functional specification

       @ref poolmachine
 */

/**
   @defgroup poolmachine Pool machine
   @{
*/

/**
   pool machine

   Data structure representing replicated pool state machine.
*/
struct poolmachine {
};

int  poolmachine_init(struct poolmachine *pm);
void poolmachine_fini(struct poolmachine *pm);

/** @} */

/* __SNS_REPAIR_H__ */
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
