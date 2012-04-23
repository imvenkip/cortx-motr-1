noinst_LTLIBRARIES                = libcolibri-xcode-ff2c.la
libcolibri_xcode_ff2c_la_SOURCES  = lex.c lex.h parser.c parser.h sem.h sem.c gen.h gen.c

noinst_PROGRAMS                   = ff2c
ff2c_SOURCES                      = ff2c.c
ff2c_LDADD                        = $(top_builddir)/xcode/ff2c/libcolibri-xcode-ff2c.la

INCLUDES                          = -I. -I$(top_srcdir) -I$(top_srcdir)/include

