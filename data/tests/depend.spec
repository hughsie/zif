Summary: Depend package
Name: depend
Version: 0.1
Release: 1%{?dist}
URL: http://people.freedesktop.org/~hughsient/releases/
Source0: README
License: GPLv2+
BuildArch: noarch
Requires: test
Requires: Test(Interface)

%description
This is a depend package.

%install
install -Dp %{SOURCE0} $RPM_BUILD_ROOT/%{_datadir}/%{name}-%{version}/README

%files
%defattr(-,root,root)
%doc %{_datadir}/%{name}-%{version}/README

%changelog
* Mon May 17 2010 Richard Hughes <richard@hughsie.com> - 0.1-1
- Initial version

