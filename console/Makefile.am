consoledir                      = $(includedir)/colibri/console
console_HEADERS                 = console.h console_fom.h console_fop.h      \
				  console_it.h console_mesg.h console_xc.h   \
				  console_yaml.h

INCLUDES			= -iquote . -iquote $(top_srcdir) -iquote    \
			          $(top_srcdir)/include

console_U_SOURCES		= console_xc.h console_xc.c

$(console_U_SOURCES): console_xc.ff \
	$(top_builddir)/xcode/ff2c/ff2c
	$(top_builddir)/xcode/ff2c/ff2c $<

noinst_LTLIBRARIES		= libcolibri-console.la
libcolibri_console_la_SOURCES	= $(console_U_SOURCES) console_fop.c \
				  console_fop.h console_fom.c        \
				  console_fom.h console_yaml.h       \
				  console_yaml.c console_it.h        \
				  console_it.c console_mesg.h        \
				  console_mesg.c console.h console.c

EXTRA_DIST			= console_xc.ff

MOSTLYCLEANFILES		= $(console_U_SOURCES)
