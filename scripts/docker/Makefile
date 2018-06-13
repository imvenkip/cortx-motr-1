.PHONY: latest
latest: centos-image lustre-client-image mero-devel-image

CENTOS_RELEASE  := latest
LUSTRE_VERSION  := 2.11.0
NAMESPACE       := seagate
DOCKER          := docker


.PHONY: 7.3 7.4 \
        centos-image \
        centos-vault-image \
        clean \
        lustre-client-image \
        mero-devel-image \
        push

7.4: CENTOS_RELEASE := 7.4.1708
7.4: LUSTRE_VERSION := 2.11.0
7.4: DOCKER_OPTS += --build-arg CENTOS_RELEASE=$(CENTOS_RELEASE) \
                    --build-arg LUSTRE_VERSION=$(LUSTRE_VERSION)
7.4: centos-vault-image lustre-client-image mero-devel-image
	$(DOCKER) tag $(NAMESPACE)/mero-devel:$(CENTOS_RELEASE) $(NAMESPACE)/mero-devel:$(basename $(CENTOS_RELEASE))

7.3: CENTOS_RELEASE := 7.3.1611
7.3: LUSTRE_VERSION := 2.9.0
7.3: DOCKER_OPTS += --build-arg CENTOS_RELEASE=$(CENTOS_RELEASE) \
                    --build-arg LUSTRE_VERSION=$(LUSTRE_VERSION)
7.3: centos-vault-image lustre-client-image mero-devel-image
	$(DOCKER) tag $(NAMESPACE)/mero-devel:$(CENTOS_RELEASE) $(NAMESPACE)/mero-devel:$(basename $(CENTOS_RELEASE))


INAME = $(@:%-image=%)

centos-vault-image:
	cd $(INAME) \
	&& $(DOCKER) build . -t $(NAMESPACE)/centos:$(CENTOS_RELEASE) $(DOCKER_OPTS)

centos-image:
	cd $(INAME) \
	&& $(DOCKER) build . -t $(NAMESPACE)/$(INAME) $(DOCKER_OPTS)

# Using `tar` as a workaround for symlink de-reference to pass them to `docker build`
lustre-client-image: TAG = $(CENTOS_RELEASE)-$(LUSTRE_VERSION)
mero-devel-image:    TAG = $(CENTOS_RELEASE)
lustre-client-image mero-devel-image:
	cd $(INAME) \
	&& tar -ch . \
	   | $(DOCKER) build -t $(NAMESPACE)/$(INAME):$(TAG) $(DOCKER_OPTS) -

push: name := [lm]*
push: tag  := *
push:
	@for img in $$(docker images --filter=reference='$(NAMESPACE)/$(name):$(tag)' \
				    --format '{{.Repository}}:{{.Tag}}' \
			| sort) ; \
	do \
		echo "---> $$img" ; \
		$(DOCKER) push $$img ; \
	done

clean: name := [clm]*
clean: tag  := *
clean:
	@for img in $$(docker images --filter=reference='$(NAMESPACE)/$(name):$(tag)' \
				    --format '{{.Repository}}:{{.Tag}}') ; \
	do \
		echo "---> $$img" ; \
		$(DOCKER) rmi $$img ; \
	done

help:
	@echo 'Build targets:'
	@echo '  latest          - build images based on the latest CentOS snapshot,'
	@echo '                    and latest supported Lustre version, this is the'
	@echo '                    default, same as just `make`'
	@echo '  7.4             - build images based on CentOS 7.4.1708 snapshot and'
	@echo '                    Lustre 2.11.0'
	@echo '  7.3             - build images based on CentOS 7.3.1611 snapshot and'
	@echo '                    Lustre 2.9.0'
	@echo ''
	@echo 'Publish targets:'
	@echo "  push            - upload local $(NAMESPACE)/* images to docker hub repository"
	@echo ''
	@echo 'Clean targets:'
	@echo "  clean           - remove local $(NAMESPACE)/* images"
