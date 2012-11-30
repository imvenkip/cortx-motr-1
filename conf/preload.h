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
 * The format of configuration string corresponds to the format used
 * by the second argument of c2_xcode_read() function.
 *
 * The acceptable values of TAGs are enumerated in struct confx_u.
 *
 * Fields of an object have to be described in the order of their
 * appearance in the corresponding confx_* structure.
 *
 * Object relations are expressed via object ids.  Directory objects
 * (c2_conf_dir) are not mentioned in a configuration string --- they
 * will be created by dynamically by a configuration module.
 *
 * c2_conf_parse() translates configuration string into an array of
 * confx_objects.
 *
 * E.g., configuration string
 *
@verbatim
[2:
 ("prof", {1| ("fs")}),
 ("fs", {2|
         ((11, 22),
          [4: "pool_width=3", "nr_data_units=1", "nr_parity_units=1",
              "unit_size=4096"],
          [0])})]
@endverbatim
 *
 * describes two confx_objects:
 *
 * @code
 * struct confx_object a = {
 *         .o_id = C2_BUF_INITS("prof"),
 *         .o_conf = {
 *                 .u_type = C2_CO_PROFILE,
 *                 .u.u_profile = {
 *                         .xp_filesystem = C2_BUF_INITS("fs")
 *                 }
 *         }
 * };
 *
 * struct confx_object b = {
 *         .o_id = C2_BUF_INITS("fs"),
 *         .o_conf = {
 *                 .u_type = C2_CO_FILESYSTEM,
 *                 .u.u_filesystem = {
 *                         .xp_rootfid = {
 *                                 .f_container = 11,
 *                                 .f_key = 22
 *                         },
 *                         .xp_params = {
 *                                 .an_count = 4,
 *                                 .an_elems = {
 *                                         C2_BUF_INITS("pool_width=3"),
 *                                         C2_BUF_INITS("nr_data_units=1"),
 *                                         C2_BUF_INITS("nr_parity_units=1"),
 *                                         C2_BUF_INITS("unit_size=4096")
 *                                 }
 *                         },
 *                         .xp_services = { .ab_count = 0, .ab_elems = NULL }
 *                 }
 *         }
 * };
 * @endcode
 *
 * <!---------------------------------------------------------------->
 * @subsection conf-fspec-preload-string-examples Examples
 *
 * @todo XXX TODO Add more examples.
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
C2_INTERNAL int c2_conf_parse(const char *src, struct confx_object *dest,
			      size_t n);

/**
 * Frees the memory, dynamically allocated by c2_conf_parse().
 *
 * @param xobjs  Array of confx_objects.
 * @param n      Number of elements in `xobjs'.
 * @param deep   Whether to free c2_buf fields.
 *
 * @see c2_conf_parse()
 */
C2_INTERNAL void c2_confx_fini(struct confx_object *xobjs, size_t n);

/**
 * Counts confx_objects encoded in a string.
 *
 * @param[in] src  Configuration string (see @ref conf-fspec-preload-string).
 *
 * @returns >= 0  The number of confx_objects found.
 * @returns  < 0  Error code.
 */
C2_INTERNAL size_t c2_confx_obj_nr(const char *src);

/** @} conf_dfspec_preload */
#endif /* __COLIBRI_CONF_PRELOAD_H__ */
