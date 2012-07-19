# This file contains build rules for *_xc.[hc] files, which are automatically
# generated from corresponding *.h files.

# *_xc.[ch] files contain generated xcode data structures for the given C
# structures from *.h files. For more information about xcode please refer to
# xcode/xcode.h and xcode/gccxml2xcode documentation.

# gccxml doesn't like -Werror so we need to remove it from CFLAGS

%_xc.h %_xc.c: %.h
	$(GCCXML) $(filter-out -Werror,$(CFLAGS)) -fxml=$(<:.h=.gccxml) $<
	$(top_srcdir)/xcode/gccxml2xcode -i $(<:.h=.gccxml)

.PHONY: clean-xc
clean-xc:
	rm -f *.gccxml *_xc.[ch]

