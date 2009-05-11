INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	$(PACKAGEKIT_CFLAGS)				\
	-DBINDIR=\"$(bindir)\"			 	\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DVERSION="\"$(VERSION)\"" 			\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"		\
	-DPK_DATA=\"$(dumgdatadir)\"			\
	-DPK_DB_DIR=\""$(PK_DB_DIR)"\" 			\
	-DEGG_LOG_FILE=\""$(PK_LOG_DIR)/dum"\"		\
	-DEGG_VERBOSE="\"PK_VERBOSE\""			\
	-DEGG_LOGGING="\"PK_LOGGING\""			\
	-DEGG_CONSOLE="\"PK_CONSOLE\""			\
	-I$(top_srcdir)/libdum

DUM_LIBS =						\
	$(top_builddir)/libdum/libdum.la

bin_PROGRAMS =						\
	dum

dum_SOURCES =						\
	egg-debug.c					\
	egg-debug.h					\
	dum-main.c

dum_LDADD =						\
	$(GLIB_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(DUM_LIBS)

dum_CFLAGS =						\
	$(WARNINGFLAGS_C)

clean-local:
	rm -f *~

CLEANFILES = $(BUILT_SOURCES)

DISTCLEANFILES =

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in

