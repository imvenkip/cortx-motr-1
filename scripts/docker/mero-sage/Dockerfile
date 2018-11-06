ARG  CENTOS_RELEASE=7.3.1611
ARG  LUSTRE_VERSION=2.9.0
FROM seagate/lustre-client:${CENTOS_RELEASE}-${LUSTRE_VERSION}

RUN yum -y remove 'kernel*'
RUN yum -y install kernel-3.10.0-514.16.1.el7
RUN rm lustre-client-devel-*.rpm
RUN yumdownloader python34-libs \
    && rpm -ihv --nodeps python34-libs*.rpm \
    && rm -f python34-libs*.rpm

COPY mero.spec .
COPY kmod-lustre-client-*.rpm ./
COPY lustre-client-*.rpm ./
COPY lustre-client-devel-*.rpm ./

RUN yum -y install {kmod-,}lustre-client-*.rpm

RUN kernel_src=$(ls -1rd /lib/modules/*/build | head -n1) \
    yum-builddep -y mero.spec \
    && rm -f mero.spec

RUN sed -i -e 's/Core/SAGE/' /etc/redhat-release
