.PHONY: all build clean flash monitor
all: build

include ../../docker/docker.mk

PORT ?= /dev/ttyACM0

MATTER_FACTORY_DATA ?= ../common/mfg_manifest/20202020_3841.bin
MATTER_FACTORY_DATA_DIR ?= $(dir $(MATTER_FACTORY_DATA))

build: check_docker
	mkdir -p .cache
	$(DOCKER) run -it --rm -u `id -u`:`id -g` -v $(shell cd ../../ && pwd):/work -v $(PWD)/.cache:/.cache -w /work/matter/air_quality $(IMAGE_NAME) bash -c "idf.py set-target esp32c6 && idf.py build"

clean:
	-@$(RM) .cache build managed_components sdkconfig

flash:
	$(DOCKER) run -it --rm --device=$(PORT):$(PORT):rw -v $(PWD):/work -w /work $(IMAGE_NAME) idf.py flash -p $(PORT)

flash-matter-factory:
	$(DOCKER) run -it --rm --device=$(PORT):$(PORT):rw -v $(abspath $(MATTER_FACTORY_DATA_DIR)):/work -w /work $(IMAGE_NAME) esptool.py -p $(PORT) write_flash 0x10000 $(notdir $(MATTER_FACTORY_DATA))

erase-matter:
	$(DOCKER) run -it --rm --device=$(PORT):$(PORT):rw -v $(abspath $(MATTER_FACTORY_DATA_DIR)):/work -w /work $(IMAGE_NAME) bash -c "esptool.py -p $(PORT) erase_region 0x10000 0xc000"

erase:
	$(DOCKER) run -it --rm --device=$(PORT):$(PORT):rw -v $(abspath $(MATTER_FACTORY_DATA_DIR)):/work -w /work $(IMAGE_NAME) bash -c "esptool.py -p $(PORT) erase_flash"

monitor:
	$(DOCKER) run -it --rm --device=$(PORT):$(PORT):rw -v $(PWD):/work -w /work $(IMAGE_NAME) idf.py monitor -p $(PORT)
