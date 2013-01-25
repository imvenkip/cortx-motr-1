/* -*- c -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
#ifndef __MERO_CONF_PRELOAD_H__
#define __MERO_CONF_PRELOAD_H__

struct m0_confx;

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
 * string -- to m0_confc_init() via `local_conf' parameter.
 *
 * When confc API is used by a kernel module, configuration string is
 * provided via mount(8) option.
 *
 * <!---------------------------------------------------------------->
 * @subsection conf-fspec-preload-string-format Format
 *
 * The format of configuration string corresponds to the format of
 * string argument of m0_xcode_read() function.
 *
 * The acceptable TAGs are enumerated in struct m0_confx_u.
 *
 * The order of fields within an object descriptor should correspond
 * to their order in the corresponding m0_confx_{profile..partition}
 * structure.
 *
 * Object relations are expressed via object ids.  Directory objects
 * (m0_conf_dir) are not included in a configuration string --- they
 * are created dynamically by a configuration module.
 *
 * E.g., the following configuration string
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
 * describes two m0_confx_objs:
 *
 * @code
 * struct m0_confx_obj a = {
 *         .o_id = M0_BUF_INITS("prof"),
 *         .o_conf = {
 *                 .u_type = M0_CO_PROFILE,
 *                 .u.u_profile = {
 *                         .xp_filesystem = M0_BUF_INITS("fs")
 *                 }
 *         }
 * };
 *
 * struct m0_confx_obj b = {
 *         .o_id = M0_BUF_INITS("fs"),
 *         .o_conf = {
 *                 .u_type = M0_CO_FILESYSTEM,
 *                 .u.u_filesystem = {
 *                         .xp_rootfid = {
 *                                 .f_container = 11,
 *                                 .f_key = 22
 *                         },
 *                         .xp_params = {
 *                                 .an_count = 4,
 *                                 .an_elems = {
 *                                         M0_BUF_INITS("pool_width=3"),
 *                                         M0_BUF_INITS("nr_data_units=1"),
 *                                         M0_BUF_INITS("nr_parity_units=1"),
 *                                         M0_BUF_INITS("unit_size=4096")
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
 * See examples of configuration strings in conf/ut/conf-str.txt and
 * m0t1fs/linux_kernel/st/st.
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
 * Decodes configuration string.
 *
 * If the call succeeds, the user is responsible for freeing allocated
 * memory with m0_confx_free():
 * @code
 *         m0_confx_free(*out);
 * @endcode
 */
M0_INTERNAL int m0_confstr_parse(const char *s, struct m0_confx **out);

/** Frees the memory, dynamically allocated by m0_confstr_parse(). */
M0_INTERNAL void m0_confx_free(struct m0_confx *enc);

/** @} conf_dfspec_preload */
#endif /* __MERO_CONF_PRELOAD_H__ */
