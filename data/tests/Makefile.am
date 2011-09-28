SUBDIRS = 							\
	transactions

EXTRA_DIST =							\
	compress.txt.bz2					\
	compress.txt.gz						\
	corrupt-repomd.repo.in					\
	data.txt						\
	test-0.1-1.fc13.noarch.rpm				\
	test.spec						\
	zif.conf						\
	mirrorlist.txt						\
	metalink.xml						\
	licenses.txt						\
	invalid.repo						\
	releases.txt						\
	yum-comps-groups.conf					\
	repos/fedora-updates.repo				\
	repos/fedora.repo					\
	fedora/comps-fedora.xml.gz				\
	fedora/filelists.sqlite.bz2				\
	fedora/filelists.xml.gz					\
	fedora/other.sqlite.bz2					\
	fedora/prestodelta.xml.gz				\
	fedora/primary.xml.gz					\
	${NULL}

CLEANFILES =	\
	fedora/primary.sqlite					\
	fedora/primary.xml					\
	fedora/other.sqlite					\
	fedora/filelists.sqlite					\
	fedora/filelists.xml					\
	fedora/comps-fedora.xml					\
	fedora/prestodelta.xml					\
	fedora/updateinfo.xml					\
	corrupt-repomd						\
	corrupt-repomd.repo					\
	${NULL}

corrupt-repomd.repo : corrupt-repomd.repo.in Makefile
	$(AM_V_GEN) $(SED) "s|\@srcdir\@|$(abs_srcdir)|" $< >$@

all : corrupt-repomd.repo

-include $(top_srcdir)/git.mk
