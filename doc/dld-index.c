/**

<!-- Note that the extra-long lines cannot be helped here -->

@page DLDIX Detailed Designs

Detailed designs, ordered alphabetically by title:

- @subpage ADDB-DLD "ADDB detailed design specification"  <!-- addb/addb.c -->
- @subpage ADDB-MON-INFRA-DLD "ADDB monitoring infrastructure  Detailed Design" <!-- addb/addb_monitor.c -->
- @subpage NetRQProvDLD "Auto-Provisioning of Receive Message Queue Buffers DLD" <!-- net/tm_provision.c -->
- @subpage CMDLD "Copy Machine DLD" <!-- cm/cm.c -->
- @subpage CPDLD "Copy Packet DLD"  <!-- cm/cp.c -->
- @subpage conf "DLD of configuration caching" <!-- conf/obj.c -->
- @subpage DLD_conf_schema "DLD for configuration schema" <!-- conf/schema.h -->
- @subpage dtm "Distributed transaction manager" <!-- dtm/dtm.h -->
- @subpage data_integrity "Data integrity using checksum" <!-- lib/checksum.h -->
- @subpage m0_long_lock-dld "FOM Long lock DLD" <!-- fop/fom_long_lock.h -->
- <i>I/O Related</i>
  - @subpage DLD-bulk-server "DLD of Bulk Server" <!-- ioservice/io_foms.c -->
  - @subpage io_calls_params_dld "DLD of I/O calls Parameters Server-side" <!-- ioservice/io_device.c -->
  - @subpage rmw_io_dld "DLD for read-modify-write IO requests" <!-- m0t1fs/linux_kernel/file_internal.h -->
  - @subpage io_bulk_client "IO bulk transfer Detailed Level Design" <!-- ioservice/io_fops.c -->
  - @subpage IOFOLDLD "IO FOL DLD" <!-- ioservice/io_fops.c -->
  - @subpage iosnsrepair "I/O with SNS and SNS repair" <!-- m0t1fs/linux_kernel/
file.c -->
  - @subpage SNSCMDLD "SNS copy machine DLD" <!-- sns/cm/cm.c -->
- @subpage Layout-DB "Layout DB DLD" <!-- layout/layout_db.c -->
- @subpage LNetDLD "LNet Transport DLD" <!-- net/lnet/lnet_main.c -->
- @subpage net-test "Mero Network Benchmark" <!-- net/test/main.c -->
- @subpage m0t1fs "M0T1FS detailed level design specification" <!-- m0t1fs/linux_kernel/m0t1fs.h -->
- @subpage rpc-layer-core-dld "RPC layer core DLD" <!-- rpc/rpc.h -->
- @subpage m0loop-dld "The new m0loop device driver DLD" <!-- m0t1fs/linux_kernel/m0loop.c -->
- @subpage DLD-stats-svc "Stats Service" <!-- addb/stats_srv.c -->

Detailed designs should use the <i>@subpage DLD "Mero DLD Template"
<!-- doc/dld-template.c --> </i> as a style guide.
Higher level designs can be found in the
<a href="https://docs.google.com/a/xyratex.com/#folders/
0B-6nd3Xo8hzBZGJlNjE2NTctZDA1OC00NTIyLWJhOGItM2U5MTc2MjJjODNm">
Google Drive Mero Design Folder</a>.

 */
