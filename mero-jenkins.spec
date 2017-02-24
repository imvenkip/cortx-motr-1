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
%define raw_kernel_ver %(basename "%{KVER}")
%else
%define with_linux %( test -n "$kernel_src" && echo "--with-linux=$kernel_src" )

# raw kernel version.
%define raw_kernel_ver %(
			if test -n "$kernel_src"; then
				basename "$kernel_src"
			else
				uname -r
			fi
			)
%endif

%define kernel_ver %( echo %{raw_kernel_ver} | tr - _ )
%define kernel_ver_requires %( echo %{raw_kernel_ver} | sed -e 's/\.x86_64$//' )

%bcond_with ut
%bcond_with cassandra

# configure options
%define  configure_release_opts     --enable-release --with-trace-kbuf-size=256 --with-trace-ubuf-size=64
%define  configure_ut_opts          --enable-dev-mode --disable-altogether-mode
%if %{with cassandra}
%define  configure_cassandra_opts   --with-cassandra
%endif

%if %{with ut}
%define  configure_opts  %{configure_ut_opts} %{?configure_cassandra_opts}
%else
%define  configure_opts  %{configure_release_opts} %{?configure_cassandra_opts}
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
BuildRequires:  perl-File-Slurp
BuildRequires:  kernel-devel = %{kernel_ver_requires}
BuildRequires:  lustre-client-devel
BuildRequires:  libuuid-devel
BuildRequires:  binutils-devel
BuildRequires:  perl-autodie
BuildRequires:  systemd-devel
BuildRequires:  python-ply
%if %{with cassandra}
BuildRequires:  libcassandra
BuildRequires:  libuv
%endif

Requires:       kernel = %{kernel_ver_requires}
Requires:       kmod-lustre-client
Requires:       lustre-client
Requires:       libyaml
Requires:       genders
Requires:       sysvinit-tools
Requires:       attr
Requires:       perl
Requires:       perl-YAML-LibYAML
Requires:       perl-DateTime
Requires:       perl-File-Which
Requires:       perl-List-MoreUtils
Requires:       perl-autodie
Requires:       perl-Try-Tiny
Requires:       perl-Sereal
Requires:       perl-MCE
Requires:       ruby
Requires:       facter
Requires:       rubygem-net-ssh

%description
Mero filesystem runtime environment and servers.

%package devel
Summary: Mero include headers
Group: Development/Kernel
Provides: %{name}-devel = %{version}-%{release}
Requires: %{name}-libs = %{version}-%{release}
Requires: binutils-devel
Requires: libyaml-devel
Requires: libaio-devel
Requires: libuuid-devel
Requires: systemd-devel
Requires: glibc-headers
Requires: kernel-devel = %{kernel_ver_requires}
Requires: lustre-client-devel

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
%configure %{with_linux} %{configure_opts} GIT_REV_ID_FULL=%{_xyr_svn_version}
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
%if %{with ut}

make DESTDIR=%{buildroot} install-tests

find %{buildroot} -name m0mero.ko -o -name m0ut.ko -o -name m0loop-ut.ko \
    -o -name m0gf.ko | sed -e 's#^%{buildroot}##' > tests-ut.files

find %{buildroot} -name m0ut -o -name m0ut-isolated -o -name m0ub -o -name m0run \
    -o -name 'm0kut*' -o -name 'libmero*.so*' -o -name 'libgf_complete*.so*' |
    sed -e 's#^%{buildroot}##' >> tests-ut.files

sort -o tests-ut.files tests-ut.files
find %{buildroot} -type f | sed -e 's#^%{buildroot}##' | sort |
    comm -13 tests-ut.files - | sed -e 's#^#%{buildroot}#' > tests-ut.exclude
xargs -a tests-ut.exclude rm -rv

%else

make DESTDIR=%{buildroot} install
find %{buildroot} -name 'm0ff2c' | sed -e 's#^%{buildroot}##' > devel.files
find %{buildroot} -name 'libmero-xcode-ff2c*.so*' | sed -e 's#^%{buildroot}##' >> devel.files
find %{buildroot} -name '*.la' | sed -e 's#^%{buildroot}##' >> devel.files
find %{buildroot}%{_includedir} | sed -e 's#^%{buildroot}##' >> devel.files
find %{buildroot}%{_libdir} -name mero.pc | sed -e 's#^%{buildroot}##' >> devel.files
mkdir -p %{buildroot}%{_localstatedir}/mero

%endif # with ut

# Remove depmod output - it is regenerated during %post
if [ -e %{buildroot}/lib/modules/%{raw_kernel_ver}/modules.dep ]; then
	rm %{buildroot}/lib/modules/%{raw_kernel_ver}/modules.*
fi


%files
%if !%{with ut}
%doc AUTHORS README NEWS ChangeLog COPYING
%{_bindir}/*
%{_sbindir}/*
%{_libdir}/*
%{_libexecdir}/mero/*
%{_exec_prefix}/lib/*
%{_datadir}/*
%{_mandir}/*
%{_localstatedir}/mero
/lib/modules/*/kernel/fs/mero/*
%exclude /lib/modules/*/kernel/fs/clovis/st/linux_kernel/clovis_st_kmod.ko
%config  %{_sysconfdir}/*
%exclude %{_bindir}/m0kut*
%exclude %{_bindir}/m0ut*
%exclude %{_bindir}/m0ub
%exclude %{_bindir}/m0ff2c
%exclude %{_libdir}/*.la
%exclude %{_libdir}/libmero-ut*
%exclude %{_libdir}/libmero-xcode-ff2c*
%exclude %{_libdir}/pkgconfig/mero.pc
%exclude %{_includedir}
%exclude /lib/modules/*/kernel/fs/net/*
%exclude /lib/modules/*/kernel/fs/rpc/*
%exclude /lib/modules/*/kernel/fs/ut/*
%endif # with ut

%if %{with ut}
%files tests-ut -f tests-ut.files
%else
%files devel -f devel.files
%endif

%post
/sbin/depmod -a
systemctl daemon-reload

if [ -e /tmp/mero_no_trace_logs ] ; then
    /bin/sed -i -r -e "s/(MERO_TRACED_KMOD=)yes/\1no/" /etc/sysconfig/mero
    /bin/sed -i -r -e "s/(MERO_TRACED_M0D=)yes/\1no/" /etc/sysconfig/mero
fi

%postun
/sbin/depmod -a
systemctl daemon-reload

%if %{with ut}

%post tests-ut
/sbin/depmod -a

%postun tests-ut
/sbin/depmod -a

%endif # with ut

# always build debuginfo package
#%%debug_package

# Remove source code from debuginfo package.
%define __debug_install_post \
  %{_rpmconfigdir}/find-debuginfo.sh %{?_missing_build_ids_terminate_build:--strict-build-id} %{?_find_debuginfo_opts} "%{_builddir}/%{?buildsubdir}"; \
  rm -rf "${RPM_BUILD_ROOT}/usr/src/debug"; \
  mkdir -p "${RPM_BUILD_ROOT}/usr/src/debug/%{name}-%{version}"; \
%{nil}
