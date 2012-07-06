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

#define _BSD_SOURCE
#include <endian.h>

#include "lib/types.h"

uint16_t c2_byteorder_cpu_to_be16(uint16_t cpu_16bits)
{
	return htobe16(cpu_16bits);
}

uint16_t c2_byteorder_cpu_to_le16(uint16_t cpu_16bits)
{
	return htole16(cpu_16bits);
}

uint16_t c2_byteorder_be16_to_cpu(uint16_t big_endian_16bits)
{
	return be16toh(big_endian_16bits);
}

uint16_t c2_byteorder_le16_to_cpu(uint16_t little_endian_16bits)
{
	return le16toh(little_endian_16bits);
}

uint32_t c2_byteorder_cpu_to_be32(uint32_t cpu_32bits)
{
	return htobe32(cpu_32bits);
}

uint32_t c2_byteorder_cpu_to_le32(uint32_t cpu_32bits)
{
	return htole32(cpu_32bits);
}

uint32_t c2_byteorder_be32_to_cpu(uint32_t big_endian_32bits)
{
	return be32toh(big_endian_32bits);
}

uint32_t c2_byteorder_le32_to_cpu(uint32_t little_endian_32bits)
{
	return le32toh(little_endian_32bits);
}

uint64_t c2_byteorder_cpu_to_be64(uint64_t cpu_64bits)
{
	return htobe64(cpu_64bits);
}

uint64_t c2_byteorder_cpu_to_le64(uint64_t cpu_64bits)
{
	return htole64(cpu_64bits);
}

uint64_t c2_byteorder_be64_to_cpu(uint64_t big_endian_64bits)
{
	return be64toh(big_endian_64bits);
}

uint64_t c2_byteorder_le64_to_cpu(uint64_t little_endian_64bits)
{
	return le64toh(little_endian_64bits);
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
