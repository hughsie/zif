INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	$(PACKAGEKIT_CFLAGS)				\
	-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE	\
	-DBINDIR=\"$(bindir)\"			 	\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DVERSION="\"$(VERSION)\"" 			\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"		\
	-DPK_DATA=\"$(zifgdatadir)\"			\
	-DPK_DB_DIR=\""$(PK_DB_DIR)"\" 			\
	-DEGG_LOG_FILE=\""$(PK_LOG_DIR)/zif"\"		\
	-DEGG_VERBOSE="\"PK_VERBOSE\""			\
	-DEGG_LOGGING="\"PK_LOGGING\""			\
	-DEGG_CONSOLE="\"PK_CONSOLE\""			\
	-I$(top_srcdir)/libzif

ZIF_LIBS =						\
	$(top_builddir)/libzif/libzif.la

bin_PROGRAMS =						\
	zif

zif_SOURCES =						\
	egg-debug.c					\
	egg-debug.h					\
	pk-progress-bar.c				\
	pk-progress-bar.h				\
	egg-debug.h					\
	zif-main.c

zif_LDADD =						\
	$(GLIB_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
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

