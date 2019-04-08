ARG  CENTOS_RELEASE_SAGE=7.3.1611
FROM centos:${CENTOS_RELEASE_SAGE}

# ARG vars declared before FROM aren't visible in the rest of the file unless
# they're re-declared
ARG CENTOS_RELEASE_SAGE

# disable all repos except for the specific release we're targeting
RUN yum-config-manager --disable '*'
COPY C${CENTOS_RELEASE_SAGE}.repo /etc/yum.repos.d/

# upgrade all
RUN yum --assumeyes upgrade

# match SAGE kernel
RUN yum -y remove 'kernel*'
RUN yum -y install kernel-3.10.0-514.16.1.el7

# enable EPEL repo
RUN yum --assumeyes install epel-release

# workaround broken CentOS 7.3 compatibility witn EPEL
RUN yumdownloader python36-libs \
    && rpm -ihv --nodeps python36-libs*.rpm \
    && rm -f python36-libs*.rpm

# install common tools
RUN yum --assumeyes install \
        createrepo \
        file \
        git \
        jq \
        rpm-build \
        which

# mark special release
RUN sed -i -e 's/Core/SAGE/' /etc/redhat-release

# set locale
RUN localedef -i en_US -f UTF-8 en_US.UTF-8
ENV LANG=en_US.UTF-8 \
    LC_ALL=en_US.UTF-8

WORKDIR /root
