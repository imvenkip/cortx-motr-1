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
 * Original author: Andriy Tkachuk <Andriy_Tkachuk@xyratex.com>
 * Original creation date: 01/30/2012
 */

#pragma once

#ifndef __MERO_LIB_USERSP_TRACE_H__
#define __MERO_LIB_USERSP_TRACE_H__

#include <stdio.h>  /* FILE */

#include "lib/types.h"  /* pid_t */

/**
   @defgroup trace Tracing.

   User-space specific declarations.

 */

extern pid_t m0_pid;

M0_INTERNAL int m0_trace_parse(FILE *trace_file, FILE *output_file,
			       bool yaml_stream_mode, bool header_only,
			       const char *m0mero_ko_path);

M0_INTERNAL void m0_trace_set_mmapped_buffer(bool val);

/** @} end of trace group */
#endif /* __MERO_LIB_USERSP_TRACE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
