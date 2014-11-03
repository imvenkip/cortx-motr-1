#xyr build defines
# This section will be replaced during Jenkins builds.
%define _xyr_package_name     @PACKAGE@
%define _xyr_package_source   %{name}-%{version}.tar.gz
%define _xyr_package_version  @PACKAGE_VERSION@
%define _xyr_build_number     1_%{kernel_ver}@GIT_REV_ID_RPM@%{?dist}
%define _xyr_pkg_url          ssh://es-gerrit.xyus.xyratex.com:29418/mero
%define _xyr_svn_version      d7353edc
#xyr end defines

# optional configure options
%if %{?KVER:1}%{!?KVER:0}
%define with_linux "--with-linux=/usr/src/kernels/%{KVER}"
%define kernel_ver %(basename "%{KVER}" | tr - _)
%else
%define with_linux %( test -n "$kernel_src" && echo "--with-linux=$kernel_src" )

%define kernel_ver %(
                      if test -n "$kernel_src"; then
                          basename "$kernel_src" | tr - _
                      else
                          uname -r | tr - _
                      fi
                    )
%endif

%define kernel_ver_requires %( echo %{kernel_ver} | sed -e 's/\.x86_64$//' -e 's/_/-/g' )

%bcond_with ut

%if %{with ut}
%define  configure_opts  --enable-dev-mode --disable-altogether-mode
%else
%define  configure_opts  --enable-release
%endif

Name:           %{_xyr_package_name}
Version:        %{_xyr_package_version}
Release:        %{_xyr_build_number}
Summary:        Mero filesystem and development libraries
Group:          System Environment/Base
License:        Xyratex
Source:         %{_xyr_package_source}
Url:            %{_xyr_pkg_url}
BuildArch:      x86_64
ExcludeArch:    i686
Provides:       %{name}-libs = %{version}-%{release}
Provides:       %{name}-modules = %{version}-%{release}

BuildRequires:  automake
BuildRequires:  autoconf
BuildRequires:  libtool
BuildRequires:  make
BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  gccxml
BuildRequires:  glibc-headers
BuildRequires:  asciidoc
BuildRequires:  libyaml-devel
BuildRequires:  libaio-devel
BuildRequires:  perl
BuildRequires:  perl-XML-LibXML
BuildRequires:  perl-List-MoreUtils
BuildRequires:  perl-File-Find-Rule
BuildRequires:  perl-IO-All
BuildRequires:  kernel-devel = %{kernel_ver_requires}
BuildRequires:  lustre-devel
BuildRequires:  libuuid-devel

Requires:       kernel = %{kernel_ver_requires}
Requires:       lustre-modules
Requires:       libyaml
Requires:       genders
Requires:       sysvinit-tools
Requires:       perl
Requires:       perl-YAML-LibYAML
Requires:       perl-DateTime
Requires:       perl-File-Which
Requires:       perl-List-MoreUtils

%description
Mero filesystem runtime environment and servers.

%package devel
Summary: Mero include headers
Group: Development/Kernel
Provides: %{name}-devel = %{version}
Requires: %{name} = %{version}

%description devel
This package contains the headers required to build external
applications that use Mero libraries.

%if %{with ut}
%package tests-ut
Summary: Mero unit tests
Group: Development/Kernel
Conflicts: %{name}

%description tests-ut
This package contains Mero unit tests (for kernel and user space).
%endif # with ut

%prep
%setup -q

%build
bash ./autogen.sh
%configure %{with_linux} %{configure_opts}
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
%if %{with ut}

make DESTDIR=%{buildroot} install-tests

find %{buildroot} -name m0mero.ko -o -name m0ut.ko -o -name m0loop-ut.ko \
    -o -name galois.ko | sed -e 's#^%{buildroot}##' > tests-ut.files

find %{buildroot} -name m0ut -o -name m0ub -o -name m0run -o -name 'm0kut*' \
    -o -name 'libmero*.so*' -o -name 'libgalois*.so*' |
    sed -e 's#^%{buildroot}##' >> tests-ut.files

sort -o tests-ut.files tests-ut.files
find %{buildroot} -type f | sed -e 's#^%{buildroot}##' | sort |
    comm -13 tests-ut.files - | sed -e 's#^#%{buildroot}#' > tests-ut.exclude
xargs -a tests-ut.exclude rm -rv

%else

make DESTDIR=%{buildroot} install
find %{buildroot} -name '*.la' | sed -e 's#^%{buildroot}##' > devel.files
find %{buildroot}%{_includedir} | sed -e 's#^%{buildroot}##' >> devel.files
mkdir -p %{buildroot}%{_localstatedir}/mero

%endif # with ut

%files
%if !%{with ut}
%doc AUTHORS README NEWS ChangeLog COPYING
%{_bindir}/*
%{_sbindir}/*
%{_libdir}/*
%{_mandir}/*
%{_localstatedir}/mero
/lib/modules/*/kernel/fs/mero/*
%config  %{_sysconfdir}/*
%exclude %{_bindir}/m0kut*
%exclude %{_bindir}/m0ut
%exclude %{_bindir}/m0ub
%exclude %{_libdir}/*.la
%exclude %{_libdir}/libmero-ut*
%exclude %{_includedir}
%exclude /lib/modules/*/kernel/fs/m0t1fs/*
%exclude /lib/modules/*/kernel/fs/net/*
%exclude /lib/modules/*/kernel/fs/rpc/*
%exclude /lib/modules/*/kernel/fs/ut/*
%endif # with ut

%if %{with ut}
%files tests-ut -f tests-ut.files
%else
%files devel -f devel.files
%endif

%pre
if initctl list | grep -q 'mero' ; then
    status mero-client | grep -q 'start/running' && stop mero-client
    status mero        | grep -q 'start/running' && stop mero
    status mero-kernel | grep -q 'start/running' && stop mero-kernel
fi || true

%post
/sbin/depmod -a
/sbin/initctl reload-configuration
/bin/sed -i -e "s/<host>/$(hostname -s)/" /etc/mero/genders
/bin/sed -i -e "s/00000000-0000-0000-0000-000000000000/$(uuidgen)/" /etc/mero/genders

%preun
if initctl list | grep -q 'mero' ; then
    status mero-client | grep -q 'start/running' && stop mero-client
    status mero        | grep -q 'start/running' && stop mero
    status mero-kernel | grep -q 'start/running' && stop mero-kernel
fi || true

%postun
/sbin/depmod -a
/sbin/initctl reload-configuration

%if %{with ut}

%post tests-ut
/sbin/depmod -a

%postun tests-ut
/sbin/depmod -a

%endif # with ut
