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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 08/19/2010
 */

#ifndef __COLIBRI_UTILS_COMMON_H__
#define __COLIBRI_UTILS_COMMON_H__

int  unit_start(const char *sandbox);
void unit_end(const char *sandbox, bool keep_sandbox);

struct c2_list;

/**
 * Splits input string, which should be in format of suite[:test][,suite[:test]],
 * into c2_test_suite_entry tokens and adds them into a linked list.
 */
int  parse_test_list(char *str, struct c2_list *list);
void free_test_list(struct c2_list *list);

/**
 * Parses fault point definitions from command line argument and enables them.
 *
 * The input string should be in format:
 *
 * func:tag:type[:integer[:integer]][,func:tag:type[:integer[:integer]]]
 */
int enable_fault_point(const char *str);

/**
 * Parses fault point definitions from a yaml file and enables them.
 *
 * Each FP is described by a yaml mapping with the following keys:
 *
 *   func  - a name of the target function, which contains fault point
 *   tag   - a fault point tag
 *   type  - a fault point type, possible values are: always, oneshot, random,
 *           off_n_on_m
 *   p     - data for 'random' fault point
 *   n     - data for 'off_n_on_m' fault point
 *   m     - data for 'off_n_on_m' fault point
 *
 * An example of yaml file:
 *
 * @verbatim
 * ---
 *
 * - func: test_func1
 *   tag:  test_tag1
 *   type: random
 *   p:    50
 *
 * - func: test_func2
 *   tag:  test_tag2
 *   type: oneshot
 *
 * # yaml mappings could be specified in a short form as well
 * - { func: test_func3, tag:  test_tag3, type: off_n_on_m, n: 3, m: 1 }
 *
 * @endverbatim
 */
int enable_fault_points_from_file(const char *file_name);

/* __COLIBRI_UTILS_COMMON_H__ */
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
