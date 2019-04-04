ARG  CENTOS_RELEASE_EOS=7.5.1804
FROM centos:${CENTOS_RELEASE_EOS}

# ARG vars declared before FROM aren't visible in the rest of the file unless
# they're re-declared
ARG CENTOS_RELEASE_EOS

# disable all repos except for the specific release we're targeting
RUN yum-config-manager --disable '*'
COPY C${CENTOS_RELEASE_EOS}.repo /etc/yum.repos.d/

# match EOS kernel
RUN yum -y remove 'kernel*'
RUN yum -y install kernel-3.10.0-862.el7 \
                   kernel-devel-3.10.0-862.el7

# enable EPEL repo
RUN yum --assumeyes install epel-release

# install common tools
RUN yum --assumeyes install \
        createrepo \
        file \
        git \
        jq \
        rpm-build \
        which

# set locale
RUN localedef -i en_US -f UTF-8 en_US.UTF-8
ENV LANG=en_US.UTF-8 \
    LC_ALL=en_US.UTF-8

WORKDIR /root
