/* -*- C -*- */
/*
 * Copyright (c) 2017-2020 Seagate Technology LLC and/or its Affiliates
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * For any questions about this software or licensing,
 * please email opensource@seagate.com or cortx-questions@seagate.com.
 *
 */


#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_DIXCM
#include "lib/memory.h"
#include "lib/trace.h"

#include "dix/cm/cp.h"

/**
 * @addtogroup DIXCMCP
 * @{
 */

extern struct m0_fop_type m0_dix_repair_cpx_fopt;

/** @see m0_dix_cm_cp_send() */
M0_INTERNAL int m0_dix_cm_repair_cp_send(struct m0_cm_cp *cp)
{
	return m0_dix_cm_cp_send(cp, &m0_dix_repair_cpx_fopt);
}

/** @} DIXCMCP */

#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
