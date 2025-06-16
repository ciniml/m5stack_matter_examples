# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

This project uses a Docker-based build system for ESP-IDF/esp-matter development. The Docker image is based on Espressif's official esp-matter image with NanoC6 support patches.

### Building Docker Image
```bash
cd docker
make build
```

### Common Build Commands

**For Matter devices (air_quality, sleepy_device):**
```bash
cd matter/<project_name>
make build          # Build firmware
make flash          # Flash firmware to device
make flash-matter-factory  # Flash Matter factory data
make monitor        # Open serial monitor
make clean          # Clean build artifacts
```

**For OpenThread projects (ot_br, ot_rcp):**
```bash
cd openthread/<project_name>
source esp-idf/export.sh
idf.py set-target esp32c6
idf.py build
idf.py flash -p /dev/ttyACM0
```

**For Matter controller:**
```bash
cd matter/controller
npm install
npm run build
npm start
```

## Project Architecture

### Matter Devices
- **air_quality/**: Air quality sensor device using SCD4x sensor and e-paper display (M5Stack CoreS3)
- **sleepy_device/**: Low-power Thread Matter device with sleep modes (NanoC6)

### OpenThread Infrastructure
- **ot_br/**: Thread Border Router firmware (can run on single NanoC6 with coexistence or dual NanoC6 setup)
- **ot_rcp/**: Thread RCP (Radio Co-Processor) firmware for dual NanoC6 Border Router setup

### Controller
- **controller/**: TypeScript/Node.js Matter controller using matter.js library

### Shared Components
- **components/M5GFX/**: M5Stack graphics library
- **components/M5Unified/**: M5Stack unified hardware abstraction
- **components/scd4x-idf/**: SCD4x sensor driver for ESP-IDF

## Hardware Targets

- **NanoC6**: Primary target for Thread/Matter devices
- **M5Stack CoreS3**: Target for air quality sensor with display
- **M5Stack devices**: Supported via M5Unified component

## Development Notes

- All firmware builds use the custom Docker image with NanoC6 patches
- Matter factory data is shared across projects in `matter/common/mfg_manifest/`
- Default serial port is `/dev/ttyACM0` (can be overridden with `PORT` environment variable)
- Projects use ESP-IDF's component system with managed_components for dependencies
- Thread Border Router supports both single-chip coexistence and dual-chip RCP configurations

## Configuration Files

- `sdkconfig.defaults*`: ESP-IDF configuration files for different targets
- `partitions.csv`: Flash partition tables
- `dependencies.lock`: Component dependency lock files
- `CMakeLists.txt`: Build configuration files

## Matter Commissioning

Default commissioning credentials:
- Manual Pairing Code: `34970012334`
- PIN Code: `20202020`
- Discriminator: `3841`
- QR Code available in `matter/common/mfg_manifest/`