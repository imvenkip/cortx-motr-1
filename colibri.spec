Name:    colibri
Version: 0.1
Release: 1
Summary: Colibri filesystem and development libraries
Group: System Environment/Base
License: Xyratex
Source: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}

%description
Colibri filesystem and development libraries.

%prep
%setup  -q -n %{name}-%{version}

%build
%configure --program-prefix=%{?_program_prefix:%{_program_prefix}}
make 

%install
rm -rf $RPM_BUILD_ROOT
DESTDIR="$RPM_BUILD_ROOT" make install

%files
%defattr(-,root,root)
%doc AUTHORS README NEWS ChangeLog COPYING INSTALL doc
%{_includedir}/*
%{_libdir}/libcolibri*
