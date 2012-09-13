UT_SRCDIR=$(top_srcdir)/balloc/ut

noinst_LTLIBRARIES		= libballoc-ut.la
libballoc_ut_la_SOURCES		= balloc.c

noinst_PROGRAMS			= dump_super_block free dump_free_extent \
				  dump_group_desc format perf mperf

format_SOURCES			= format.c
format_LDFLAGS			=
format_LDADD			= $(top_builddir)/colibri/libcolibri.la

free_SOURCES			= free.c
free_LDFLAGS			=
free_LDADD			= $(top_builddir)/colibri/libcolibri.la

dump_free_extent_SOURCES	= dump_free_extent.c
dump_free_extent_LDFLAGS	=
dump_free_extent_LDADD		= $(top_builddir)/colibri/libcolibri.la

dump_group_desc_SOURCES		= dump_group_desc.c
dump_group_desc_LDFLAGS		=
dump_group_desc_LDADD		= $(top_builddir)/colibri/libcolibri.la

dump_super_block_SOURCES	= dump_super_block.c
dump_super_block_LDFLAGS	=
dump_super_block_LDADD		= $(top_builddir)/colibri/libcolibri.la

perf_SOURCES			= perf.c
perf_LDFLAGS			=
perf_LDADD			= $(top_builddir)/colibri/libcolibri.la

mperf_SOURCES			= mperf.c
mperf_LDFLAGS			=
mperf_LDADD			= $(top_builddir)/colibri/libcolibri.la
