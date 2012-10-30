/* -*- c -*- */
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 16-Mar-2012
 */
#pragma once
#ifndef __COLIBRI_CONF_PRELOAD_H__
#define __COLIBRI_CONF_PRELOAD_H__

#include "lib/types.h" /* size_t */

struct confx_object;

/**
 * @page conf-fspec-preload Pre-Loading of Configuration Cache
 *
 * - @ref conf-fspec-preload-string
 *   - @ref conf-fspec-preload-string-format
 *   - @ref conf-fspec-preload-string-examples
 * - @ref conf_dfspec_preload "Detailed Functional Specification"
 *
 * When configuration cache is created, it can be pre-loaded with
 * configuration data.  Cache pre-loading can be useful for testing,
 * boot-strapping, and manual control. One of use cases is a situation
 * when confc cannot or should not communicate with confd.
 *
 * <hr> <!------------------------------------------------------------>
 * @section conf-fspec-preload-string Configuration string
 *
 * The application pre-loads confc cache by passing textual
 * description of configuration objects -- so called configuration
 * string -- to c2_confc_init() via `conf_source' parameter. Note
 * that the value of this parameter should start with "local-conf:",
 * otherwise it will be treated as an end point address of confd.
 *
 * When confc API is used by a kernel module, configuration string is
 * provided as mount(8) option.
 *
 * <!---------------------------------------------------------------->
 * @subsection conf-fspec-preload-string-format Format
 *
 * Configuration string represents a set (array) of configuration
 * objects encoded using JSON [http://www.json.org/] format.
 *
 * First two members of a configuration object encoding are "type" and
 * "id". The set of remaining members depends on the type of object.
 *
 * Object relations are expressed via object ids. Directory objects
 * (c2_conf_dir) are not encoded.
 *
 * c2_conf_parse() translates configuration string into an array of
 * confx_objects.
 *
 * E.g., configuration string
 *
@verbatim
[ {"type":"profile", "id":"test", "filesystem":"c2t1fs"},
  {"type":"filesystem", "id":"c2t1fs", "rootfid":[11,22], "params":[50,60,70],
   "services":[]} ]
@endverbatim
 *
 * describes two confx_objects:
 *
 * @code
 * struct confx_object a = {
 *     .o_id = C2_BUF_INITS("test"),
 *     .o_conf = {
 *         .u_type = C2_CO_PROFILE,
 *         .u.u_profile = {
 *             .xp_filesystem = C2_BUF_INITS("c2t1fs")
 *         }
 *     }
 * };
 * struct confx_object b = {
 *     .o_id = C2_BUF_INITS("c2t1fs"),
 *     .o_conf = {
 *         .u_type = C2_CO_FILESYSTEM,
 *         .u.u_filesystem = {
 *             .xp_rootfid = { .f_container = 11, .f_key = 22 },
 *             .xp_params = { .an_count = 3, .an_elems = { 50, 60, 70 } },
 *             .xp_services = { .ab_count = 0, .ab_elems = NULL }
 *         }
 *     }
 * };
 * @endcode
 *
 * <!---------------------------------------------------------------->
 * @subsection conf-fspec-preload-string-examples Examples
 *
@verbatim
[ { "type":"profile", "id":"test-2", "filesystem":"c2t1fs" }
, { "type":"filesystem", "id":"c2t1fs", "rootfid":[11, 22], "params":[50,60,70],
    "services":["mds", "io"] }
, { "type":"service", "id":"mds", "filesystem":"c2t1fs", "svc_type":1,
    "endpoints":["addr0"], "node":"N" }
, { "type":"service", "id":"io", "filesystem":"c2t1fs", "svc_type":2,
    "endpoints":["addr1","addr2","addr3"], "node":"N" }
, { "type":"node", "id":"N", "services":["mds", "io"], "memsize":8000,
    "nr_cpu":2, "last_state":3, "flags":2, "pool_id":0, "nics":["nic0"],
    "sdevs":["sdev0"] }
, { "type":"nic", "id":"nic0", "iface_type":5, "mtu":8192, "speed":10000,
    "filename":"ib0", "last_state":3 }
, { "type":"sdev", "id":"sdev0", "iface":4, "media":1, "size":596000000000,
    "last_state":3, "flags":4, "partitions":["part0"] }
, { "type":"partition", "id":"part0", "start":0, "size":596000000000, "index":0,
    "pa_type":7, "filename":"sda1" } ]
@endverbatim
 *
 * @see @ref conf_dfspec_preload "Detailed Functional Specification"
 */

/**
 * @defgroup conf_dfspec_preload Pre-Loading of Configuration Cache
 * @brief Detailed Functional Specification.
 *
 * @see @ref conf, @ref conf-fspec-preload "Functional Specification"
 *
 * @{
 */

/**
 * Fills the array of confx_objects with configuration data, obtained
 * from a string.
 *
 * @param[in]  src   Configuration string (see @ref conf-fspec-preload-string).
 * @param[out] dest  Receiver of configuration.
 * @param      n     Number of elements in `dest'.
 *
 * @returns >= 0  The number of confx_objects found.
 * @returns  < 0  Error code.
 *
 * @note  c2_conf_parse() allocates additional memory for some
 *        confx_objects. The user is responsible for freeing this
 *        memory with c2_confx_fini().
 *
 * @pre   src does not start with "local-conf:"
 * @post  retval <= n
 *
 * @see c2_confx_fini()
 */
int c2_conf_parse(const char *src, struct confx_object *dest, size_t n);

/**
 * Frees the memory, dynamically allocated by c2_conf_parse().
 *
 * @param xobjs  Array of confx_objects.
 * @param n      Number of elements in `xobjs'.
 * @param deep   Whether to free c2_buf fields.
 *
 * @see c2_conf_parse()
 */
void c2_confx_fini(struct confx_object *xobjs, size_t n);

/**
 * Counts confx_objects obtained from a string.
 *
 * @param[in]  src   Configuration string (see @ref conf-fspec-preload-string).
 *
 * @returns	     The number of confx_objects found.
 */
size_t c2_confx_obj_nr(const char *src);

/** @} conf_dfspec_preload */
#endif /* __COLIBRI_CONF_PRELOAD_H__ */
