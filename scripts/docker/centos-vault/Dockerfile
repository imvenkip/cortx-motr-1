ARG  CENTOS_RELEASE=latest
FROM centos:${CENTOS_RELEASE}

# ARG vars declared before FROM aren't visible in the rest of the file unless
# they're re-declared
ARG CENTOS_RELEASE

# disable all repos except for the specific release we're targeting
RUN yum-config-manager --disable '*'
COPY C${CENTOS_RELEASE}.repo /etc/yum.repos.d/

# upgrade all
RUN yum --assumeyes upgrade

# enable EPEL repo
RUN yum --assumeyes install epel-release

# install common tools
RUN yum --assumeyes install \
        file \
        git \
        rpm-build \
        which

# set locale
RUN localedef -i en_US -f UTF-8 en_US.UTF-8
ENV LANG=en_US.UTF-8 \
    LC_ALL=en_US.UTF-8

WORKDIR /root
