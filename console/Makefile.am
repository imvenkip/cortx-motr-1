consoledir                      = $(includedir)/colibri/console
console_HEADERS                 = console.h console_fom.h console_fop.h console_it.h \
                                  console_mesg.h console_u.h console_yaml.h

INCLUDES			= -I. -I$(top_srcdir) -I$(top_srcdir)/include

console_U_SOURCES		= console_u.h console_u.c

$(console_U_SOURCES):	console.ff \
			$(top_builddir)/fop/rt/libc2rt.la \
			$(top_builddir)/fop/fop2c
			$(top_builddir)/fop/fop2c -u $<

noinst_LTLIBRARIES		= libcolibri-console.la
libcolibri_console_la_SOURCES	= $(console_U_SOURCES) console.ff \
				  console_fop.c console_fop.h \
				  console_fom.c console_fom.h \
				  console_yaml.h console_yaml.c \
				  console_it.h console_it.c \
				  console_mesg.h console_mesg.c \
				  console.h console.c

EXTRA_DIST			= console.ff main.c

MOSTLYCLEANFILES		= $(console_U_SOURCES)
