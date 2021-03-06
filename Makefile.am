ACLOCAL_AMFLAGS = -I m4

SUBDIRS = 						\
	etc						\
	data						\
	libzif						\
	docs						\
	man						\
	po						\
	examples					\
	tools

security-check:
	flawfinder `find -name "*.c"`

snapshot:
	$(MAKE) dist distdir=$(PACKAGE)-$(VERSION)-`date +"%Y%m%d"`

coverity:
	$(MAKE) clean;					\
	cov-build --dir cov-int $(MAKE);		\
	tar czvf zif-coverity.tgz cov-int;		\
	set COVERITY_EMAIL=`cat ~/.coverity | grep EMAIL | cut -f2`; \
	set COVERITY_PASSWORD=`cat ~/.coverity | grep PASSWORD_zif | cut -f2`; \
	curl --form file=@zif-coverity.tgz		\
	     --form project=zif				\
	     --form password=$(COVERITY_PASSWORD)	\
	     --form email=$(COVERITY_EMAIL)		\
	     http://scan5.coverity.com/cgi-bin/upload.py

DISTCLEANFILES =					\
	zif-*.tar.gz

MAINTAINERCLEANFILES =					\
	*~						\
	compile						\
	configure					\
	ChangeLog					\
	gtk-doc.make					\
	INSTALL						\
	install-sh					\
	missing						\
	mkinstalldirs					\
	config.guess					\
	ltmain.sh					\
	config.sub					\
	depcomp						\
	Makefile.in					\
	config.h.*					\
	aclocal.m4

EXTRA_DIST =						\
	COPYING						\
	MAINTAINERS					\
	AUTHORS						\
	INSTALL						\
	README						\
	HACKING						\
        TODO						\
	NEWS						\
        autogen.sh					\
	config.h

DISTCHECK_CONFIGURE_FLAGS = 				\
	--disable-tests					\
	--enable-gtk-doc

distclean-local:
	if test $(srdcir) = .; then :; else \
		rm -f ChangeLog; \
	fi

ChangeLog:
	@echo Creating $@
	@if test -d "$(srcdir)/.git"; then \
	  (GIT_DIR=$(top_srcdir)/.git ./missing --run git log ZIF_0_0_X.. --stat -M -C --name-status  --date=short --no-color) | fmt --split-only > $@.tmp \
	  && mv -f $@.tmp $@ \
	  || ($(RM) $@.tmp; \
	      echo Failed to generate ChangeLog, your ChangeLog may be outdated >&2; \
	      (test -f $@ || echo git-log is required to generate this file >> $@)); \
	else \
	  test -f $@ || \
	  (echo A git checkout and git-log is required to generate ChangeLog >&2 && \
	  echo A git checkout and git-log is required to generate this file >> $@); \
	fi

.PHONY: ChangeLog

uninstall-am:
	$(RM) -R $(DESTDIR)$(localstatedir)/lib/zif

-include $(top_srcdir)/git.mk
