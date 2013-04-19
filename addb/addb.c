/* -*- C -*- */
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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 *                  Huang Hua <hua_huang@xyratex.com>
 * Original creation date: 06/19/2010
 * Rewritten by:    Carl Braganza <carl_braganza@xyratex.com>
 *                  Dave Cohrs <dave_cohrs@xyratex.com>
 * Rewrite date: 08/14/2012
 */

/**
   <!-- 8/14/2012 -->
   @page ADDB-DLD ADDB Detailed Design
   - @ref ADDB-DLD-ovw
   - @ref ADDB-DLD-def
   - @ref ADDB-DLD-req
   - @ref ADDB-DLD-depends
   - @ref ADDB-DLD-highlights
   - @subpage ADDB-DLD-fspec "Functional Specification" <!-- Note @subpage -->
   - @ref ADDB-DLD-lspec
     - @ref ADDB-DLD-lspec-comps
     - @ref ADDB-DLD-lspec-meta
     - @ref ADDB-DLD-lspec-mc
     - @ref ADDB-DLD-lspec-mc-confs
     - @ref ADDB-DLD-lspec-mc-global
     - @ref ADDB-DLD-lspec-state
     - @ref ADDB-DLD-lspec-thread
     - @ref ADDB-DLD-lspec-numa
   - @ref ADDB-DLD-conformance
   - @ref ADDB-DLD-ut
   - @ref ADDB-DLD-st
   - @ref ADDB-DLD-O
   - @ref ADDB-DLD-ref
   - @ref ADDB-DLD-impl-plan

   Additional design details are found in component DLDs:
   - @subpage ADDB-DLD-CTXOBJ "Context Object Management"
   - @subpage ADDB-DLD-EVMGR  "Event Manager Detailed Design"
   - @subpage ADDB-DLD-RSINK  "RPC Sink Detailed Design"
   - @subpage ADDB-DLD-DSINK  "Stob Sink and Repository Detailed Design"
   - @subpage ADDB-DLD-RETRV  "Record Retrieval Detailed Design"
   - @subpage ADDB-DLD-SVC    "Service"
   - @subpage ADDB-DLD-CNTR   "Counter Detailed Design"

   @see @ref addb "Analysis and Diagnostics Data-Base API",
   @ref addb_pvt "ADDB Internal Interfaces"

   <hr>
   @section ADDB-DLD-ovw Overview
   This design describes the common ADDB data structures, external interfaces,
   and sub-component breakdown.

   <hr>
   @section ADDB-DLD-def Definitions

   See @ref ADDB-DLD-ref-HLD "[0]" for definitions that apply to this
   design.  In addition, the following terms are defined:

   - @b ADDB-Machine A software abstraction encapsulating a set of
   interfaces. See @ref ADDB-DLD-lspec-mc for more detail.
   - @b Event-Manager A component of an ADDB machine that provides its posting
   interfaces.
        - @b Caching-Event-Manager  A variant of this component that
	supports posting in awkward contexts.
	- @b Passthrough-Event-Manager A variant of this component that is
	used in non-awkward contexts.
   - @b Record-Sink A component of an ADDB machine that processes posted
   ADDB records.
        - @b RPC-Sink A variant of this component that is used to send ADDB
	records to remote ADDB services.
	- @b Stob-Sink A variant of this component that is used to save ADDB
	records persistently.

   <hr>
   @section ADDB-DLD-req Requirements

   The following requirements are fully described in
   @ref ADDB-DLD-ref-HLD "[0]":
   - @b r.addb.filtering.context.initialization
   - @b r.addb.filtering.context.deletion
   - @b r.addb.filtering.context.hierarchy
   - @b r.addb.filtering.context.identifier
   - @b r.addb.filtering.context.identifier.container
   - @b r.addb.filtering.context.identifier.conveyance
   - @b r.addb.filtering.context.identifier.import
   - @b r.addb.filtering.context.identifier.node
   - @b r.addb.filtering.context-type.registration
   - @b r.addb.filtering.context-type.identifier.node
   - @b r.addb.grouping.distributed
   - @b r.addb.monitoring.exception-logging
   - @b r.addb.post.compile-time-validation
   - @b r.addb.post.decoupled-from-storage-location
   - @b r.addb.post.multiple-context-paths
   - @b r.addb.post.non-blocking.awkward-contexts
   - @b r.addb.post.non-blocking.minimum-context-switching
   - @b r.addb.post.reentrant
   - @b r.addb.record.definition.base-type-refinement
   - @b r.addb.record.definition.compile-time-validation
   - @b r.addb.record.definition.declarative
   - @b r.addb.record.definition.meta-data
   - @b r.addb.record.definition.meta-data.fields
   - @b r.addb.record.definition.registration
   - @b r.addb.record.definition.standard-exceptions
   - @b r.addb.record.type.counter.support-interfaces
   - @b r.addb.retention.location.accessible
   - @b r.addb.retention.storage.context
   - @b r.addb.retention.storage.context-api
   - @b r.addb.retention.storage.record-api
   - @b r.addb.retention.storage.stob
   - @b r.addb.retention.storage.dump-cli

   <hr>
   @section ADDB-DLD-depends Dependencies

   - The @ref rpc-layer-core-dld "RPC layer core" module has to be extended to
   provide the mechanisms needed to send ADDB records to an ADDB service.
   Details are found in @ref ADDB-DLD-RPCSINK-depends "RPC Sink Dependencies".
   - The @ref reqh module must be extended to create an appropriate ADDB
   machine for use in a service.
@code
   struct m0_reqh {
        ...
	struct m0_addb_mc        rh_addb_mc;
   };
@endcode
   The first request handler ADDB machine initialized must also be used to
   configure the global ADDB machine, ::m0_addb_gmc.
   - The @ref timer module must be extended to provide the m0_is_awkward()
   interface.
   - The @ref m0t1fs module has to be extended to support the node
   identifier kernel module parameter.
   - The @ref conf module for an ADDB service object definition
   including internal stob definition parameters.
   - The @ref m0d module must support configuration parameters for
   the stob used by ADDB until such time that the @ref conf interfaces are
   available.
   - The @ref stob "Storage objects" module is used to store data persistently.
   - The ADDB service is dependent on the definitions of the
   @ref reqhservice "Request handler service" module.

   <hr>
   @section ADDB-DLD-highlights Design Highlights

   This design closely follows that described in @ref ADDB-DLD-ref-HLD "[0]"
   with slight deviations:
   - The ADDB service is not considered part of the ADDB
   machine, but instead uses the ADDB machine configured in the request handler.
   - No context manager component is necessary in an ADDB machine.

   An ADDB counter tracks sample history between consecutive post intervals.
   The counter is automatically reset each time it is posted.

   <hr>
   @section ADDB-DLD-lspec Logical Specification

   - @ref ADDB-DLD-lspec-comps
   - @ref ADDB-DLD-lspec-meta
   - @ref ADDB-DLD-lspec-mc
   - @ref ADDB-DLD-lspec-mc-confs
   - @ref ADDB-DLD-lspec-mc-global
   - @ref ADDB-DLD-lspec-state
   - @ref ADDB-DLD-lspec-thread
   - @ref ADDB-DLD-lspec-numa

   @subsection ADDB-DLD-lspec-comps Component Overview

   The ADDB subsystem is composed of the following sub-components:
   - @b Meta-Data This consists of support for record type definition and
   registration, and context type definition and registration.
   - <b>ADDB machine</b> This provides the operational framework through which
   ADDB context records are posted and persisted.  It contains a
   number of sub-components, as described in @ref ADDB-DLD-lspec-mc.
   - @b Service This handles the delivery of ADDB records from the RPC
   subsystem and routes them to the record-sequence sink interface.
   - @b Retrieval-API This provides the programmatic interface through
   which ADDB records are fetched.  Separate interfaces exist for context
   records and for event records; in addition a "raw" interface is provided
   to fetch any type of record.
   - @b Retrieval-CLI This provides a dump of the contents of the ADDB
   repository.
   - @b Counters This provides support to operate on counters, essentially
   implementing the advertised statistical operations.
   - @b Initialization This provides the ADDB subsystem's initialization
   logic, including establishment of the global node and container contexts,
   and a global default ADDB machine.

   @subsection ADDB-DLD-lspec-meta Meta-Data Management
   ADDB meta-data includes record type and context type objects.  All consumers
   of the ADDB APIs, be they Mero modules or external modules, need to work
   with meta-data objects. See @ref ADDB-DLD-ref-HLD "[0]" for more details.

   There are four operations involved with meta-data objects:
   - Declaration
   - Definition
   - Registration
   - Location

   The first two operations, declaration and definition, are handled by the
   macro interfaces.  The same macros are invoked to declare and to define; in
   the latter case, "predicate" macros must be defined to change the expansion
   of the macro from declaration to definition.  See @ref ADDB-DLD-fspec-sub
   "Subroutines and Macros" for details.

   The support for meta-data object definition is obvious; the need for
   declaration comes from the fact that pointers to these meta-data objects are
   required in other APIs.  Another point to note is that the name of the
   meta-data object is constructed by the definition macros from a base name
   provided by the invoker; separate macros are provided to construct this name
   from the base name to properly reference the meta-data objects.

   All meta-data objects must be registered before use.  This is provided to
   support external (non-Mero) modules that wish to use the ADDB system.
   During registration the invoker assigned identifier in the meta-data object
   is checked for uniqueness against previously registered meta-data objects of
   the same type.  Meta-data objects also carry a string description; this is
   not checked for uniqueness during registration.

   ADDB records refer to their associated meta-data objects by their assigned
   numeric identifiers.  Access to the meta-data object itself is required
   during record retrieval post-processing, in order to analyse the records.
   This requires support to locate the meta-data objects given their identifier.
   To support this, the meta-data management component maintains hash tables for
   each meta-data type over its numeric identifier space.  Meta-data objects are
   chained in sorted lists anchored in each hash bucket.

   The justification for use of a hashing algorithm is that quick lookup is
   essential to the record retrieval process.  It also helps during validation,
   though validation by itself does not make a sufficiently strong case to
   warrant hashing.  A very simple "modulo-some-prime" hashing algorithm is
   used.  In the worst case all objects hash to the same bucket and lookup
   de-generates to linear list traversal, which is the only supported
   option in Mero today.

   @subsection ADDB-DLD-lspec-mc The ADDB Machine Framework

   The ADDB machine provides the operational framework through which ADDB
   records are posted and persisted.  It provides a uniform and transferable way
   to post ADDB records in any execution environment by decoupling the posting
   front end interfaces from the internal processing logic which is constrained
   by the execution environment.

   The machine encapsulates a set of interfaces:
   -# Event posting
   -# Cross-machine record copy
   -# ADDB record sink
   -# ADDB record-sequence sink

   The first interface is used by the externally visible event posting
   APIs.  The other interfaces are internal.

   The interfaces are implemented by a set of three "plug-in" sub-components
   that must be configured when the machine is initialized:
   - @b Event-Manager This handles record posting.  There are two variants
   of this component:
       - @b Passthrough-Event-Manager This provides the record posting interface
       of the ADDB machine.  Records are directed to the Record-Sink interface.
       - @b Caching-Event-Manager This provides the record posting and
       cross-machine copying interfaces of the ADDB machine.  Records are saved
       in an in-memory cache.
   - @b Record-Sink This processes posted records internally.  There are two
   variants of this component:
       - @ref ADDB-DLD-RPCSINK-lspec "RPC Sink" This provides the Record-Sink
       interface of the ADDB machine and handles client side interaction with
       RPC.
       - @ref ADDB-DLD-DSINK-lspec "Stob Sink" This provides the Record-Sink
       and record-sequence sink interfaces of the ADDB machine.  It saves data
       to @ref stob.

   These are illustrated in the following UML diagram, along with the interfaces
   they advertise. @image html "../../addb/addbmc.png" "ADDB Machine Objects"
   <!-- PNG image width is 800 -->

   The constructors of the machine components are represented by the following
   subroutines:
   - m0_addb_mc_configure_cache_evmgr()
   - m0_addb_mc_configure_stob_sink()
   - m0_addb_mc_configure_pt_evmgr()
   - m0_addb_mc_configure_rpc_sink()

   ADDB machine components are reference counted for reasons described in
   @ref ADDB-DLD-lspec-mc-global.

   @subsection ADDB-DLD-lspec-mc-confs ADDB Machine Configurations

   The choice of sub-components configured affect the semantics of the ADDB
   machine. The following configurations are supported:
   - <b>Transient store configuration</b> This configuration contains a
   Passthrough-Event-Manager and an RPC-Sink component.
   See @ref ADDB-DLD-fspec-uc-TSMC "Configure a Transient Store Machine".
   - <b>Persistent store configuration</b> This configuration contains a
   Passthrough-Event-Manager and a Stob-Sink.
   Such a configuration is required by the ADDB service.
   See @ref ADDB-DLD-fspec-uc-PSMC "Configure a Persistent Store Machine".
   - <b>Awkward context configuration</b> This configuration contains a
   Caching-Event-Manager component only, to support posting in awkward contexts.
   The cache has to be periodically flushed to another ADDB machine
   configured either for transient or persistent store.
   See @ref ADDB-DLD-fspec-uc-AWKMC "Configure an Awkward Context Machine".

   There are some constraints in the order of configuring the components: a
   Passthrough-Event-Manager requires a Record-Sink to be configured first.
   See <a href="https://docs.google.com/a/xyratex.com/document/d/
1XCynkCGwJcWQD5Yn61u20j4gccIgaGMBJlpsW7JJfqc/edit#heading=h.ssf38m5lco3k">
Proposed ADDB machine</a>
   in @ref ADDB-DLD-ref-HLD "[0]" for more details on these configurations.

   It is expected that applications will configure ADDB machines to
   address their specific execution needs, such as operation in awkward
   contexts or minimizing the possibility of a thread context switch:

   - In file system or standalone commands, a Transient Store configuration
   would be the norm.  Posts made to the ADDB machine are sent to a remote
   server for persistence.

   - In server processes it is expected that posted ADDB records be persisted
   locally instead of being sent across the network, which means that an ADDB
   machine configured with a Stob-Sink must be used.  Each request handler will
   create a private Persistent Store ADDB machine configuration for its use and
   for the use of the FOMs it animates, to minimize context switching.  The ADDB
   service, which saves remotely created ADDB records to a stob, will use its
   request handler's ADDB machine.

   It is strongly recommended that "infrastructure" modules which expose
   interfaces that operate on the stack of their invoker, provide a way for the
   invoking application to make its current choice of ADDB machine available for
   use in these interfaces.  This way the infrastructure module can post ADDB
   records safely without regard to the invocation context.  Care should be
   taken, however, that if context objects are to be created, this is only done
   in clearly non-awkward contexts.

   @subsection ADDB-DLD-lspec-mc-global The Global ADDB Machine

   The ADDB subsystem defines a globally visible ADDB machine,
   ::m0_addb_gmc, to be used where no special ADDB machine is available.
   The global machine is initialized during module initialization, but is
   not configured. It is left to the higher level application to configure
   it in a suitable manner:

   - In file system and standalone clients, the global machine is really all
   that is required.  It should be configured with a Transient Store
   configuration.  No other ADDB machine is necessary except to handle
   awkward contexts.
   See the @ref ADDB-DLD-fspec-uc-TSMC "Configure a Transient Store Machine"
   recipe and assume that @c mc in the example is replaced by ::m0_addb_gmc.

   - In Mero servers, given the expectation that all ADDB records posted
   are saved locally, the global machine must be configured with a
   Persistent Store configuration.

   The server case is a bit more involved. Consider:
   - Stob-Sink components are expensive in that they consume physical
   storage and require configuration information.
   - We know that there is at least one request handler in a Mero server
   process with its own private Persistent Store configuration ADDB machine.
   - Almost all activity in a server process is associated with a request
   handler, be it message delivery from the RPC/network stack, or execution
   of FOMs, to I/O stob activity.  All the modules involved should be using
   the request handler ADDB machine for normal ADDB record posts, as per
   the recommendations of the previous section.
   - Any awkward-context posts associated with a request handler, such
   as those from timer or AST routines, are handled through dedicated
   caching ADDB machines that are subsequently flushed to the main
   request handler ADDB machine.
   - Any ADDB operations from non-request handler related threads are likely
   to only involve Exception records.

   It makes sense then to re-direct use of the global ADDB machine to the ADDB
   machine of one of the request handlers. The small context switch cost that
   this incurs is justified by the low probability of expected use.

   Support is provided to duplicate the configuration of an existing ADDB
   machine in another machine, with the m0_addb_mc_dup() subroutine.  The
   request handler configuration logic should check if the global machine has
   been configured, and if not configure it from its own machine.
   This is illustrated by the following pseudo-code, the initial part of which
   will be recognized as following the
   @ref ADDB-DLD-fspec-uc-PSMC "Configure a Persistent Store Machine" recipe:
@code
     m0_addb_mc_init(&reqh->rh_addb_mc);
     rc = m0_addb_mc_configure_pt_evmgr(&reqh->rh_addb_mc);
     rc = m0_addb_mc_configure_stob_sink(&reqh->rh_addb_mc, addb_stob,
           cctx->cc_addb_stob_segment_size);
     if (!m0_addb_mc_is_fully_configured(&m0_addb_gmc))
           m0_addb_mc_dup(&reqh->rh_addb_mc, &m0_addb_gmc);
@endcode

   Reference counts are maintained for each ADDB machine component. Internally,
   the duplication operation merely increments the reference counts of the
   configured components of the source machine and then copies their pointers
   to the machine being configured.  Finalization of an ADDB machine's
   internal components is triggered by the finalization of the ADDB machine;
   the reference count of each individual component is decremented, and only
   when it goes to 0 will that component actually be finalized.

   Although the global ADDB machine is not configuered on startup, it may be
   used to initialize context objects that are children of the global context
   objects. The remaining processing of such context objects will be completed
   only when the global ADDB machine gets configured, as described in
   @ref ADDB-DLD-CTXOBJ-gi "Context Initialization with the Global Machine"

   The global ADDB machine is finalized when the ADDB module is finalized.
   In a server process, the request handler ADDB machines would have already
   been finalized by that time.

   @subsection ADDB-DLD-lspec-state State Specification

   The global ADDB machine is created in an initialized but unconfigured state.
   It may be safely used to initialize context objects from module
   initialization code - the processing of the context definition records will
   be completed only when the machine is configured.  An unconfigured ADDB
   machine may also be used to post ADDB records, but these records will be
   silently dropped.

   @todo State specification will be added to the design as part of subsequent
   ADDB sub-component designs.

   @subsection ADDB-DLD-lspec-thread Threading and Concurrency Model

   - Context object initialization with an ADDB subsystem supplied global
   context object as the parent will be serialized implicitly when accessing the
   parent object's @a ac_cntr by means of the internal ::addb_mutex.  External
   serialization is assumed to be used if the parent object is not an ADDB
   subsystem supplied context object.

   - ADDB machine configuration is assumed to be externally serialized.
   Reference counts are maintained in components to support duplication
   of ADDB machine configuration.

   - Meta-data management routines are to be used in non-awkward contexts
   only and serialize themselves with a static mutex.  Once defined and
   registered, meta-data objects can be used in any context.

   - The Caching-Event-Manager is pre-configured with a limited cache and does
   not allocate memory.  It uses spin-locks in the kernel and atomic
   compare-and-swap instructions in user space to serialize access to its cache
   in the posting and cross-machine event copying interfaces.
   As such, the "awkward context" ADDB machine can be used to post ADDB
   records in any execution environment.

   - The Passthrough-Event-Manager directly invokes the services of the
   Record-Sink and performs no caching nor serialization.

   - The "request handler" ADDB machine configuration is intended to be
   set up by a request handler and used only within its scope.
   Essentially, as it usually operates only within the request handler's
   global lock, all normal usage of its ADDB machine is serialized.
   However, because one instance of such an ADDB machine may be established
   as the global default ADDB machine, the internal Stob-Sink maintains a
   mutex for serialization through its Record-Sink interface.

   - The Transient Store ADDB machine configuration is used in non-awkward,
   non-server environments such as in the Mero file system.  All
   serialization takes place with an internal mutex in the RPC-Sink component of
   the machine.

   - Access to counters is serialized by the application: this includes both
   the update and the periodic ADDB posts of counter objects.

   @subsection ADDB-DLD-lspec-numa NUMA optimizations

   The "request handler" ADDB machine is specifically optimized to operate
   within the execution environment of a request handler.

   <hr>
   @section ADDB-DLD-conformance Conformance

   - @b i.addb.filtering.context.initialization The M0_ADDB_CTX_INIT() API
   is provided.
   - @b i.addb.filtering.context.deletion The m0_addb_ctx_fini() API
   is provided.
   - @b i.addb.filtering.context.hierarchy The M0_ADDB_CTX_INIT() API
   allows the specification of a parent context.
   - @b i.addb.filtering.context.identifier The m0_addb_ctx structure stores
   the context ID as a sequence of unsigned 64 bit integers.
   - @b i.addb.filtering.context.identifier.container The ::m0_addb_proc_ctx
   object provides the software container context.  The context type is either
   @ref m0_addb_ct_kmod with identifier ::M0_ADDB_CTXID_KMOD, or is
   @ref m0_addb_ct_process with identifier ::M0_ADDB_CTXID_PROCESS.
   - @b i.addb.filtering.context.identifier.conveyance The xcode-based
   data structures in @ref addb_otw provide the conveyance mechanism.
   - @b i.addb.filtering.context.identifier.import The m0_addb_ctx_import()
   and m0_addb_ctx_export() APIs are provided.
   - @b i.addb.filtering.context.identifier.node The ::m0_addb_node_ctx
   object provides the node context.
   - @b i.addb.filtering.context-type.registration The
   m0_addb_ctx_type_register() API is provided.
   - @b i.addb.filtering.context-type.identifier.node This is specified by
   the context type pairs @ref m0_addb_ct_node_lo with identifier
   ::M0_ADDB_CTXID_NODE_LO and @ref m0_addb_ct_node_hi with identifier
   ::M0_ADDB_CTXID_NODE_HI.
   - @b i.addb.grouping.distributed This will be specified as part of
   task addb.machine.rpc-sink.
   - @b i.addb.monitoring.exception-logging m0__addb_post() will record
   exceptions in the system logging facility.
   - @b i.addb.post.compile-time-validation The M0_ADDB_POST() macro validates
   the event parameters at compile time.
   - @b i.addb.post.decoupled-from-storage-location The single m0__addb_post()
   API is used for posting all events, regardless of the configured Record-Sink.
   - @b i.addb.post.multiple-context-paths The various posting macros such
   as M0_ADDB_POST() take a context vector parameter.
   - @b i.addb.post.non-blocking.awkward-contexts The Caching-Event-Manager
   is provided for posting events in awkward contexts.
   - @b i.addb.post.non-blocking.minimum-context-switching Each request
   handler must configure its own ADDB machine, and this machine
   is tracked in the m0_reqh::rh_addb_mc field.  One such machine shares
   resources with the ::m0_addb_gmc, but this machine should be rarely used.
   - @b i.addb.post.reentrant The Event-Manager, as specified in
   @ref ADDB-DLD-lspec-mc, is decoupled from the configured Record-Sink.
   - @b i.addb.record.definition.base-type-refinement The record type
   definition macros are based on the ::m0_addb_base_rec_type types.
   - @b i.addb.record.definition.compile-time-validation The m0_addb_rec_type
   is used to track the information required for compile-time validation during
   record posting.
   - @b i.addb.record.definition.declarative The macros M0_ADDB_RT_DP(),
   M0_ADDB_RT_EX(), M0_ADDB_RT_CNTR(), and M0_ADDB_RT_SEQ() provide the
   required declarative mechanism.
   - @b i.addb.record.definition.meta-data The m0_addb_rec_type objects
   are available both to posting and retrieval interfaces.
   - @b i.addb.record.definition.meta-data.fields The m0_addb_rec_type::art_rf
   vector provides the required field descriptions.
   - @b i.addb.record.definition.registration The m0_addb_rec_type_register()
   API is provided.
   - @b i.addb.record.definition.standard-exceptions The standard exceptions
   are covered in @ref ADDB-DLD-EVMGR-std.
   - @b i.addb.record.type.counter.support-interfaces The
   m0_addb_counter_update() API is provided.
   - @b i.addb.retention.location.accessible The repository is accessible
   because existing stob implementations are based on Linux files and paths.
   - @b i.addb.retention.storage.context Context creation records have their
   own ::m0_addb_base_rec_type and can be referenced by the context identifier.
   - @b i.addb.retention.storage.context-api The m0_addb_cursor_init() API
   provides a cursor to sequentially retrieve only ADDB context information.
   - @b i.addb.retention.storage.record-api The m0_addb_cursor_init() API
   provides a cursor to sequentially retrieve only non-context ADDB records.
   - @b i.addb.retention.storage.stob The Stob-Sink uses a stob as
   the ADDB repository.
   - @b i.addb.retention.storage.dump-cli This will be specified as part
   of task addb.api.retrieval.

   @todo i.addb.grouping.distributed

   @todo i.addb.retention.storage.dump-cli

   <hr>
   @section ADDB-DLD-ut Unit Tests

   @subsection ADDB-DLD-ut-md ADDB meta-data unit tests
   - Verify that the UT context type and record type initialization macros work.
   - Verify the pre-defined context types and record types.
   - Stress concurrent registration of context type and record type.
   - Verify the reserved range of record types.
   - Validate the hash tables.

   @subsection ADDB-DLD-ut-context ADDB context object unit tests
   The context unit test will fake the record sink interface an ADDB machine
   to examine the generated context definition records.
   - Test that the global node context gets created with the UUID provisioned
   in the kernel.  There will be some variance between the user and kernel
   space tests.
   - Test that the global process context gets created.
   - Test that the global context object definition records are created
   correctly.
   - Test that the M0_ADDB_CTX_INIT() macro works.
   - Test that the export of context identifiers works.
   - Test that a context object can be created by import.
   - Test that the logic to periodically repost the global context object
   definition records works.

   @subsection ADDB-DLD-ut-evmgr ADDB event manager unit tests

   The following tests cover the parts of the event manager that are common
   to both caching and passthrough event managers.

   @test M0_ADDB_POST() of ADDB exceptions only result in a call to the defined
   m0_addb_mc_evmgr::evm_log() function.  Note that only a system test can
   verify the implementation of a real logging function.

   @test M0_ADDB_POST() results in correct calls to
   m0_addb_mc_evmgr::evm_rec_alloc() and m0_addb_mc_evmgr::evm_post().
   Included in this test is that the m0_addb_rec is correctly created.

   @test M0_ADDB_POST_CNTR() results in correct calls to
   m0_addb_mc_evmgr::evm_rec_alloc() and m0_addb_mc_evmgr::evm_post().
   Included in this test is that the m0_addb_rec is correctly created.

   @test M0_ADDB_POST_SEQ() results in correct calls to
   m0_addb_mc_evmgr::evm_rec_alloc() and m0_addb_mc_evmgr::evm_post().
   Included in this test is that the m0_addb_rec is correctly created.

   The following tests cover passthrough event managers.

   @test m0_addb_mc_configure_pt_evmgr() correctly sets the fields of the
   m0_addb_mc and addb_pt_evmgr.

   @test m0_addb_mc_fini() correctly causes the event manager to be released.

   @todo Unit tests relating to posting via a caching event manager will be
   added in task addb.machine.event-mgr.awkward.

   @todo ADDB RPC sink related unit tests will be added in task
   addb.machine.rpc-sink.

   @subsection ADDB-DLD-ut-dsink Stob Sink Unit Tests

   @see @ref ADDB-DLD-DSINK-ut "Stob Sink and Repository Unit Tests"

   @subsection ADDB-DLD-ut-retrieval Repository Retrieval Unit Tests

   @see @ref ADDB-DLD-RETRV-ut "Repository Retrieval Unit Tests"

   @subsection ADDB-DLD-ut-counter ADDB counter unit tests

   @test Successful updates of a counter without histogram updates all fields
   correctly.

   @test Successful updates of a counter with histogram updates all fields
   correctly.

   @test An update that would cause overflow does not update any fields of
   the counter.

   @test Post correctly initializes the m0_addb_post_data for a counter,
   resets the counter statistics and updates the sequence number.

   @test Post correctly initializes the m0_addb_post_data for a counter,
   both in the case of a counter with histogram and without.

   @todo Serialization during the posting of counters will be tested along with
   other serialization and posting tests in the event manager unit tests.

   @todo ADDB initialization related unit tests will be added in task
   addb.api.init.

   <hr>
   @section ADDB-DLD-st System Tests
   @todo System tests will be added to the design as part of subsequent
   ADDB sub-component designs.

   <hr>
   @section ADDB-DLD-O Analysis

   Meta-data objects are tracked via hash tables that use a very simple
   "modulo-some-prime" hashing algorithm on their numeric identifier.  In the
   worst case all objects hash to the same bucket and lookup de-generates to
   linear list traversal, which is what is the only supported option in Mero
   today.

   <hr>
   @section ADDB-DLD-ref References
   - @anchor ADDB-DLD-ref-HLD [0] <a href="https://docs.google.com/a
/xyratex.com/document/d/1XCynkCGwJcWQD5Yn61u20j4gccIgaGMBJlpsW7JJfqc/view">
HLD of the ADDB collection mechanism in Mero M0</a>

   <hr>
   @section ADDB-DLD-impl-plan Implementation Plan

   The previous implementation of ADDB has to be completely replaced.
   See <a href="https://docs.google.com/a/xyratex.com/document/d/
1ECPWbVq4460vZzAGW8EcNlBl8qiluaDNt3THs6jhU7g/view">M0 ADDB Implementation
Plan</a> for details.

 */


#include <stdarg.h>

/*
 * Include our header file in definition mode before any other Mero header,
 * so as to guarantee that this is the very first inclusion and hence the
 * predicate macros act as expected.
 */
#undef M0_ADDB_CT_CREATE_DEFINITION
#define M0_ADDB_CT_CREATE_DEFINITION
#undef M0_ADDB_RT_CREATE_DEFINITION
#define M0_ADDB_RT_CREATE_DEFINITION
#include "addb/addb_pvt.h"
#ifndef __KERNEL__
#include "m0t1fs/m0t1fs_addb.h"
#endif

#include "mero/magic.h"
#include "lib/arith.h"  /* max_check */
#include "lib/errno.h"  /* errno */
#include "lib/memory.h" /*m0_alloc/m0_free */
#include "lib/misc.h"
#include "lib/rwlock.h"
#include "lib/time.h"
#ifndef __KERNEL__
#include "fop/fom_generic.h"
#endif
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_ADDB
#include "lib/trace.h"  /* M0_LOG() */

#include "addb/addb_wire_xc.h"
#include "addb/addb_fops_xc.h"

struct m0_uint128 m0_node_uuid; /* globally visible uuid */

/**
   @addtogroup addb_pvt
   @{
 */

/**
   Private mutex used within the ADDB subsystem to serialize access to global
   objects.
 */
static struct m0_mutex addb_mutex;

/**
   Time that the ADDB subsystem was initialized.
 */
static m0_time_t addb_init_time;

/**
   Initialize the node UUID.
 */
static int addb_node_uuid_init(void)
{
	char buf[M0_UUID_STRLEN + 1];
	struct m0_uint128 *uuid = &m0_node_uuid;
	int rc;

	uuid->u_lo = uuid->u_hi = 0;
	rc = addb_node_uuid_string_get(buf);
	if (rc < 0) {
		M0_LOG(M0_ERROR, "Unable to fetch node UUID string");
		return rc;
	}
	rc = m0_uuid_parse(buf, uuid);
	if (rc < 0) {
		M0_LOG(M0_ERROR, "Unable to parse node UUID string");
		return rc;
	}
	M0_LOG(M0_NOTICE, "Node uuid: %08x-%04x-%04x-%04x-%012lx",
	       (unsigned)(uuid->u_hi >> 32),
	       (unsigned)(uuid->u_hi >> 16) & 0xffff,
	       (unsigned)uuid->u_hi & 0xffff,
	       (unsigned)(uuid->u_lo >> 48),
	       (long unsigned)uuid->u_lo & 0xffffffffffff);
	return 0;
}

/** @} addb_pvt */

/* Include subordinate C files to control external symbols */
#include "addb/addb_ct.c"
#include "addb/addb_fops_xc.c"
#include "addb/addb_rt.c"
#include "addb/addb_rec.c"
#include "addb/addb_ctxobj.c"
#include "addb/addb_mc.c"
#include "addb/addb_evmgr.c"
#include "addb/addb_counter.c"
#include "addb/addb_ts.c"
#include "addb/addb_rpcsink.c"
#ifdef __KERNEL__
#include "addb/linux_kernel/kctx.c"
#else
#include "addb/user_space/addb_svc.c"
#include "addb/user_space/addb_fom.c"
#include "addb/user_space/addb_pfom.c"
#include "addb/user_space/uctx.c"
#include "addb/user_space/addb_stobsink.c"
#include "addb/user_space/addb_retrieval.c"
#endif
#include "addb/addb_fops.c"

#ifndef __KERNEL__
static void addb_register_kernel_ctx_and_rec_types(void)
{
	m0_addb_ctx_type_register(&m0_addb_ct_m0t1fs_mod);
	m0_addb_ctx_type_register(&m0_addb_ct_m0t1fs_mountp);
	m0_addb_ctx_type_register(&m0_addb_ct_m0t1fs_op_read);
	m0_addb_ctx_type_register(&m0_addb_ct_m0t1fs_op_write);
}
#endif

M0_INTERNAL int m0_addb_init(void)
{
	int rc;

	addb_init_time = m0_time_now();
	rc = addb_node_uuid_init();
	if (rc != 0)
		return rc;
	m0_mutex_init(&addb_mutex);
	m0_xc_addb_wire_init();
	addb_ct_init();
	addb_rt_init();
	addb_ctx_init();

	m0_addb_mc_init(&m0_addb_gmc);

#ifndef __KERNEL__
	addb_register_kernel_ctx_and_rec_types();
#endif
	return 0;
}

M0_INTERNAL void m0_addb_fini(void)
{
	if (m0_addb_mc_is_initialized(&m0_addb_gmc))
		m0_addb_mc_fini(&m0_addb_gmc);

	addb_ctx_fini();
	addb_rt_fini();
	addb_ct_fini();
	m0_xc_addb_wire_fini();
	m0_mutex_fini(&addb_mutex);
}

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
