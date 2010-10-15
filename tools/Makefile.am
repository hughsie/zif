INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
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
	$(top_builddir)/libzif/libzif.la

bin_PROGRAMS =						\
	zif

zif_SOURCES =						\
	zif-progress-bar.c				\
	zif-progress-bar.h				\
	egg-debug.h					\
	zif-main.c

zif_LDADD =						\
	$(GLIB_LIBS)					\
	$(ZIF_LIBS)

zif_CFLAGS =						\
	$(WARNINGFLAGS_C)

clean-local:
	rm -f *~

CLEANFILES = $(BUILT_SOURCES)

DISTCLEANFILES =

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in

