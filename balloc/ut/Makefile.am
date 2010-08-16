INCLUDES         = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I@DBSRC@/build_unix

noinst_PROGRAMS  = format balloc dump_free_extent dump_group_desc dump_super_block

format_SOURCES   = format.c
format_LDFLAGS   =
format_LDADD     = $(top_builddir)/colibri/libcolibri.la

balloc_SOURCES   = balloc.c
balloc_LDFLAGS   =
balloc_LDADD     = $(top_builddir)/colibri/libcolibri.la

dump_free_extent_SOURCES   = dump_free_extent.c
dump_free_extent_LDFLAGS   =
dump_free_extent_LDADD     = $(top_builddir)/colibri/libcolibri.la

dump_group_desc_SOURCES   = dump_group_desc.c
dump_group_desc_LDFLAGS   =
dump_group_desc_LDADD     = $(top_builddir)/colibri/libcolibri.la

dump_super_block_SOURCES   = dump_super_block.c
dump_super_block_LDFLAGS   =
dump_super_block_LDADD     = $(top_builddir)/colibri/libcolibri.la
