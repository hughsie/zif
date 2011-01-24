INCLUDES =						\
	$(GLIB_CFLAGS)					\
	-DBINDIR=\"$(bindir)\"			 	\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DVERSION="\"$(VERSION)\"" 			\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"		\
	-DZIF_DATA=\"$(pkggdatadir)\"			\
	-DG_LOG_DOMAIN=\"Zif\"				\
	-I$(top_srcdir)/libzif

ZIF_LIBS =						\
	$(GLIB_LIBS)					\
	$(top_builddir)/libzif/libzif.la

noinst_PROGRAMS =					\
	zif-file-requires

zif_file_requires_SOURCES =				\
	zif-file-requires.c
zif_file_requires_LDADD =				\
	$(ZIF_LIBS)
zif_file_requires_CFLAGS =				\
	$(WARNINGFLAGS_C)

clean-local:
	rm -f *~

CLEANFILES = $(BUILT_SOURCES)

DISTCLEANFILES =

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in

