INCLUDES             = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I/usr/include

noinst_PROGRAMS      = yaml2db

yaml2db_SOURCES      = yaml2db.c

yaml2db_LDADD       = $(top_builddir)/colibri/libcolibri.la \
			$(top_builddir)/../yaml/src/libyaml.la
