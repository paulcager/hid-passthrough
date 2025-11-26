# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a Raspberry Pi Pico firmware project that implements a USB HID passthrough device. It acts as a
man-in-the-middle between a USB HID device (e.g., keyboard) and a host PC, allowing inspection and modification of HID
reports.

## Build System

### Prerequisites

- Pico SDK installed at `~/pico/pico-sdk` (or set `PICO_SDK_PATH` environment variable)
- CMake 3.13 or higher
- ARM cross-compiler toolchain for RP2040

### Building

```bash
cd build
cmake ..
make -j$(nproc)
```

The output binary will be `build/hid_pass.uf2` which can be flashed to the Pico by copying to the USB mass storage
device.

### Clean Build

```bash
cd build
rm -rf *
cmake ..
make -j$(nproc)
```

## Architecture

### Dual-Core Design

The project uses both cores of the RP2040:

- **Core 0**: Runs the TinyUSB device stack (`tud_task()`) to present as a USB device to the host PC
- **Core 1**: Runs the TinyUSB host stack with PIO-USB (`tuh_task()`) to communicate with the physical USB HID device

### USB Stack Configuration

- **Device Mode (RHPORT0)**: Uses native RP2040 USB hardware to present as HID + CDC device to host PC
- **Host Mode (RHPORT1)**: Uses PIO-USB (GPIO-based USB implementation) to act as USB host for physical HID device
- **GPIO Pin**: USB host D+ on GPIO2 (D- is automatically GPIO3)

### TinyUSB Direct Usage

This project uses TinyUSB directly (not through pico_stdlib), which means:

- Standard `pico_enable_stdio_usb()` is disabled (set to 0 in CMakeLists.txt)
- USB stdio is provided by `cdc_stdio_lib` instead, which bridges TinyUSB CDC to pico stdio
- All USB descriptors are manually defined in `main.c`
- Must call `tusb_init()` before `cdc_stdio_lib_init()` for proper initialization order

### USB Descriptors

The device presents as a composite device with:

1. **CDC Interface** (interface 0): For debug serial output via `printf()`
2. **HID Interface** (interface 2): For forwarding HID reports from physical device to host PC

Device class is set to MISC/IAD to support the composite CDC+HID configuration.

### Initialization Order

Critical sequence in `main()`:

1. `board_init()` - Initialize Pico board
2. `tusb_init()` - Initialize TinyUSB device stack
3. `cdc_stdio_lib_init()` - Initialize CDC stdio bridge
4. `stdio_init_all()` - Initialize standard I/O
5. `multicore_launch_core1()` - Launch USB host on Core 1
6. Main loop runs `tud_task()` for device stack

### Key Dependencies

- **pio_usb**: Git submodule providing GPIO-based USB host implementation
- **cdc_stdio_lib**: Git submodule providing CDC stdio bridge for TinyUSB compatibility
- **Pico SDK**: Located at `~/pico/pico-sdk`

## Configuration Files

### tusb_config.h

Defines TinyUSB configuration:

- Enables both device and host stacks
- Configures 1 HID device interface and 1 CDC device interface
- Supports up to 4 HID host interfaces (for connecting multiple HID devices)
- Sets buffer sizes for endpoints

### CMakeLists.txt

- Links against both `tinyusb_device` and `tinyusb_host`
- Includes `pico_pio_usb` for GPIO-based USB host
- Includes `cdc_stdio_lib` for USB CDC stdio support
- UART stdio is enabled on UART0 (default pins GP0/GP1)

## HID Passthrough Flow

1. Physical HID device connects to Pico GPIO2/3 (USB host port)
2. `tuh_hid_mount_cb()` called when device is detected
3. `tuh_hid_report_received_cb()` receives HID reports from physical device
4. Reports are forwarded via `tud_hid_report()` to host PC
5. Host PC can send reports back via `tud_hid_set_report_cb()` (e.g., keyboard LEDs)
6. All reports are logged via `printf()` to CDC serial port

## Debug Output

Debug output via `printf()` appears on:

- UART0 (GP0/GP1) - always enabled
- USB CDC serial port - available after enumeration

Use a serial monitor (115200 baud for UART) or USB CDC terminal to view debug messages.
