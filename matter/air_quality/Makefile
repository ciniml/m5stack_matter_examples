.PHONY: all build clean flash monitor
all: build

include ../../docker/docker.mk

PORT ?= /dev/ttyACM0

MATTER_FACTORY_DATA ?= ../common/mfg_manifest/20202020_3841.bin
MATTER_FACTORY_DATA_DIR ?= $(dir $(MATTER_FACTORY_DATA))

REPOSITORY_ROOT := $(shell cd ../../ && pwd)

$(info REPOSITORY_ROOT=$(REPOSITORY_ROOT))

build: check_docker build/config
	mkdir -p .cache
	$(DOCKER) run -it --rm -u `id -u`:`id -g` -v $(REPOSITORY_ROOT):$(REPOSITORY_ROOT) -v $(PWD)/.cache:/.cache -w $(REPOSITORY_ROOT)/matter/air_quality $(IMAGE_NAME) bash -c "idf.py build"

build/config:
	mkdir -p .cache
	$(DOCKER) run -it --rm -u `id -u`:`id -g` -v $(REPOSITORY_ROOT):$(REPOSITORY_ROOT) -v $(PWD)/.cache:/.cache -w $(REPOSITORY_ROOT)/matter/air_quality $(IMAGE_NAME) bash -c "idf.py set-target esp32s3"

clean:
	-@$(RM) -r dependencies.lock .cache build managed_components sdkconfig

menuconfig:
	$(DOCKER) run -it --rm --device=$(PORT):$(PORT):rw -v $(REPOSITORY_ROOT):$(REPOSITORY_ROOT) -v $(PWD)/.cache:/.cache -w $(REPOSITORY_ROOT)/matter/air_quality $(IMAGE_NAME) idf.py menuconfig

flash:
	$(DOCKER) run -it --rm --device=$(PORT):$(PORT):rw -v $(REPOSITORY_ROOT):$(REPOSITORY_ROOT) -v $(PWD)/.cache:/.cache -w $(REPOSITORY_ROOT)/matter/air_quality $(IMAGE_NAME) idf.py flash --port $(PORT)

flash-matter-factory:
	$(DOCKER) run -it --rm --device=$(PORT):$(PORT):rw -v $(abspath $(MATTER_FACTORY_DATA_DIR)):/work -w /work $(IMAGE_NAME) esptool.py -p $(PORT) write_flash 0x10000 $(notdir $(MATTER_FACTORY_DATA))

erase-matter:
	$(DOCKER) run -it --rm --device=$(PORT):$(PORT):rw -v $(abspath $(MATTER_FACTORY_DATA_DIR)):/work -w /work $(IMAGE_NAME) bash -c "esptool.py -p $(PORT) erase_region 0x10000 0xc000"

erase:
	$(DOCKER) run -it --rm --device=$(PORT):$(PORT):rw -v $(abspath $(MATTER_FACTORY_DATA_DIR)):/work -w /work $(IMAGE_NAME) bash -c "esptool.py -p $(PORT) erase_flash"

monitor:
	$(DOCKER) run -it --rm --device=$(PORT):$(PORT):rw -v $(REPOSITORY_ROOT):$(REPOSITORY_ROOT) -v $(PWD)/.cache:/.cache -w $(REPOSITORY_ROOT)/matter/air_quality $(IMAGE_NAME) idf.py monitor --port $(PORT)
