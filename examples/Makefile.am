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

EXTRA_DIST =						\
	zif-resolve-newest.py

ZIF_LIBS =						\
	$(GLIB_LIBS)					\
	$(top_builddir)/libzif/libzif.la

noinst_PROGRAMS =					\
	zif-rhn						\
	zif-file-requires				\
	zif-list-repos					\
	zif-list-installed

zif_file_requires_SOURCES =				\
	zif-file-requires.c
zif_file_requires_LDADD =				\
	$(ZIF_LIBS)
zif_file_requires_CFLAGS =				\
	$(WARNINGFLAGS_C)

zif_list_installed_SOURCES =				\
	zif-list-installed.c
zif_list_installed_LDADD =				\
	$(ZIF_LIBS)
zif_list_installed_CFLAGS =				\
	$(WARNINGFLAGS_C)

zif_list_repos_SOURCES =				\
	zif-list-repos.c
zif_list_repos_LDADD =					\
	$(ZIF_LIBS)
zif_list_repos_CFLAGS =					\
	$(WARNINGFLAGS_C)

zif_rhn_SOURCES =					\
	zif-rhn.c
zif_rhn_LDADD =						\
	$(ZIF_LIBS)
zif_rhn_CFLAGS =					\
	$(WARNINGFLAGS_C)

clean-local:
	rm -f *~

CLEANFILES = $(BUILT_SOURCES)

DISTCLEANFILES =

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in


-include $(top_srcdir)/git.mk
