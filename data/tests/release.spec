Summary: Release package
Name: release
Version: 16
Release: 1%{?dist}
URL: http://people.freedesktop.org/~hughsient/releases/
License: GPLv2+
BuildArch: noarch
Provides: redhat-release

%description
This is a release package.

%install

%files
%defattr(-,root,root)

%changelog
* Tue Sep 20 2011 Richard Hughes <richard@hughsie.com> - 16-1
- Initial version

