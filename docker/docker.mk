IMAGE_NAME_PREFIX := m5stack_matter
ESP_MATTER_VERSION := v1.2_idf_v5.2.1
IMAGE_BASE_VERSION := $(ESP_MATTER_VERSION)
IMAGE_NAME := $(IMAGE_NAME_PREFIX):$(IMAGE_BASE_VERSION)

DOCKER := $(shell which docker)

.PHONY: check_docker
check_docker:
	@if ! $(DOCKER) image inspect $(IMAGE_NAME) > /dev/null; then \
		echo "Image $(IMAGE_NAME) does not exist"; \
		exit 1; \
	fi