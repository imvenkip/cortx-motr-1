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
 * Original author: Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 07/06/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <asm/byteorder.h>

uint16_t c2_byteorder_cpu_to_be16(uint16_t cpu_16bits)
{
	return cpu_to_be16(cpu_16bits);
}

uint16_t c2_byteorder_cpu_to_le16(uint16_t cpu_16bits)
{
	return cpu_to_le16(cpu_16bits);
}

uint16_t c2_byteorder_be16_to_cpu(uint16_t big_endian_16bits)
{
	return be16_to_cpu(big_endian_16bits);
}

uint16_t c2_byteorder_le16_to_cpu(uint16_t little_endian_16bits)
{
	return le16_to_cpu(little_endian_16bits);
}

uint32_t c2_byteorder_cpu_to_be32(uint32_t cpu_32bits)
{
	return cpu_to_be32(cpu_32bits);
}

uint32_t c2_byteorder_cpu_to_le32(uint32_t cpu_32bits)
{
	return cpu_to_le32(cpu_32bits);
}

uint32_t c2_byteorder_be32_to_cpu(uint32_t big_endian_32bits)
{
	return be32_to_cpu(big_endian_32bits);
}

uint32_t c2_byteorder_le32_to_cpu(uint32_t little_endian_32bits)
{
	return le32_to_cpu(little_endian_32bits);
}

uint64_t c2_byteorder_cpu_to_be64(uint64_t cpu_64bits)
{
	return cpu_to_be64(cpu_64bits);
}

uint64_t c2_byteorder_cpu_to_le64(uint64_t cpu_64bits)
{
	return cpu_to_le64(cpu_64bits);
}

uint64_t c2_byteorder_be64_to_cpu(uint64_t big_endian_64bits)
{
	return be64_to_cpu(big_endian_64bits);
}

uint64_t c2_byteorder_le64_to_cpu(uint64_t little_endian_64bits)
{
	return le64_to_cpu(little_endian_64bits);
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
