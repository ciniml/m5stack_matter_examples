.PHONY: all build clean

all: build

include ./docker.mk

build:
	@if $(DOCKER) image inspect $(IMAGE_NAME) > /dev/null; then \
		echo "Image $(IMAGE_NAME) already exists"; \
		exit 0; \
	fi && \
	$(DOCKER) build -t $(IMAGE_NAME) . --build-arg ESP_MATTER_VERSION=$(ESP_MATTER_VERSION)

clean:
	-$(DOCKER) image rm $(IMAGE_NAME)