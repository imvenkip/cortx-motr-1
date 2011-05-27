#noinst_LTLIBRARIES   = libformation-ut.la
#libformation_ut_la_SOURCES   = formation-ut.c 

noinst_PROGRAMS	= formation

io_fop_init.c io_u.h io_u.c:
	ln -sf $(top_builddir)/stob/ut/io_fop_init.c io_fop_init.c
	ln -sf $(top_builddir)/stob/ut/io_u.h io_u.h
	ln -sf $(top_builddir)/stob/ut/io_u.c io_u.c

formation_SOURCES   = io_u.h io_u.c io_fop_init.c formation-ut-infra1.c
formation_LDFLAGS   = -pthread
formation_LDADD     = $(top_builddir)/colibri/libcolibri.la

INCLUDES             = -I. -I$(top_srcdir) -I$(top_srcdir)/include

clean-local:
	rm -f io_fop_init.c \
		rm -f io_u.*

