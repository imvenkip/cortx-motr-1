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
 * Original author: Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 02/08/2012
 */

#ifndef __KLNET_DRV_H__
#define __KLNET_DRV_H__

/**
   @page LNetDRVDLD-fspec LNet Transport Device Functional Specfication
   <i>Mandatory. This page describes the external interfaces of the
   component. The section has mandatory sub-divisions created using the Doxygen
   @@section command.  It is required that there be Table of Contents at the
   top of the page that illustrates the sectioning of the page.</i>

   - @ref LNetDRVDLD-fspec-ds
   - @ref LNetDRVDLD-fspec-sub
   - @ref LNetDRVDLD-fspec-cli
   - @ref LNetDRVDLD-fspec-usecases
   - @ref LNetDRVDLDDFS "Detailed Functional Specification" <!-- Note link -->

   The Functional Specification section of the DLD shall be placed in a
   separate Doxygen page, identified as a @@subpage of the main specification
   document through the table of contents in the main document page.  The
   purpose of this separation is to co-locate the Functional Specification in
   the same source file as the Detailed Functional Specification.

   A table of contents should be created for the major sections in this page,
   as illustrated above.  It should also contain references to other
   <b>external</b> Detailed Functional Specification sections, which even
   though may be present in the same source file, would not be visibly linked
   in the Doxygen output.

   @section LNetDRVDLD-fspec-ds Data Structures
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and <i>brief</i> description of the
   major externally visible data structures defined by this component.  No
   details of the data structure are required here, just the salient
   points.</i>

   For example:
<table border="0">
<tr><td>&nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp;</td><td>
   The @c nlx_sample_ds1 structure tracks the density of the
   electro-magnetic field with the following:
@code
struct nlx_sample_ds1 {
  ...
  int dsd_flux_density;
  ...
};
@endcode
   The value of this field is inversely proportional to the square of the
   number of lines of comments in the DLD.
</td></tr></table>
   Note the indentation above, accomplished by means of an HTML table
   is purely for visual effect in the Doxygen output of the style guide.
   A real DLD should not use such constructs.

   Simple lists can also suffice:
   - nlx_sample_ds1

   The section could also describe what use it makes of data structures
   described elsewhere.

   Note that data structures are defined in the
   @ref LNetDRVDLDDFS "Detailed Functional Specification"
   so <b>do not duplicate the definitions</b>!
   Do not describe internal data structures here either - they can be described
   in the @ref LNetDRVDLD-lspec "Logical Specification" if necessary.

   @section LNetDRVDLD-fspec-sub Subroutines
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and brief description of the
   externally visible programming interfaces.</i>

   Externally visible interfaces should be enumerated and categorized by
   function.  <b>Do not provide details.</b> They will be fully documented in
   the @ref LNetDRVDLDDFS "Detailed Functional Specification".
   Do not describe internal interfaces - they can be described in the
   @ref LNetDRVDLD-lspec "Logical Specification" if necessary.

   @subsection LNetDRVDLD-fspec-sub-cons Constructors and Destructors

   @subsection LNetDRVDLD-fspec-sub-acc Accessors and Invariants

   @subsection LNetDRVDLD-fspec-sub-opi Operational Interfaces
   - nlx_sample_sub1()

   @section LNetDRVDLD-fspec-cli Command Usage
   <i>Mandatory for command line programs.  Components that provide programs
   would provide a specification of the command line invocation arguments.  In
   addition, the format of any any structured file consumed or produced by the
   interface must be described in this section.</i>

   @section LNetDRVDLD-fspec-usecases Recipes
   <i>This section could briefly explain what sequence of interface calls or
   what program invocation flags are required to solve specific usage
   scenarios.  It would be very nice if these examples can be linked
   back to the HLD for the component.</i>

   Note the following references to the Detailed Functional Specification
   sections at the end of these Functional Specifications, created using the
   Doxygen @@see command:

   @see @ref LNetDRVDLDDFS "Sample Detailed Functional Specification"
 */

/**
   @defgroup LNetDRVDLDDFS LNet Transport Device
   @ingroup net
   @brief Detailed functional specification template.

   This page is part of the DLD style template.  Detailed functional
   specifications go into a module described by the Doxygen @@defgroup command.
   Note that you cannot use a hyphen (-) in the tag of a @@defgroup.

   Module documentation may spread across multiple source files.  Make sure
   that the @@addtogroup Doxygen command is used in the other files to merge
   their documentation into the main group.  When doing so, it is important to
   ensure that the material flows logically when read through Doxygen.

   You are not constrained to have only one module in the design.  If multiple
   modules are present you may use multiple @@defgroup commands to create
   individual documentation pages for each such module, though it is good idea
   to use separate header files for the additional modules.  In particular, it
   is a good idea to separate the internal detailed documentation from the
   external documentation in this header file.  Please make sure that the DLD
   and the modules cross-reference each other, as shown below.

   @see The @ref LNetDRVDLD "LNet Transport Device and Driver DLD" its
   @ref LNetDRVDLD-fspec "Functional Specification"
   and its @ref LNetDRVDLD-lspec-thread

   @{
*/

/** Initialise the C2 LNet Transport device. */
int nlx_dev_init(void);
/** Finalise the C2 LNet device. */
void nlx_dev_fini(void);

/**
   @}
*/

#endif /*  __KLNET_DRV_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
