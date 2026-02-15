
# OpenPrintTag Scanner

## Overview
This project is an ESP32-based NFC scanner and writer designed to work with 3D printer filament spools that follow the [OpenPrintTag](https://openprinttag.org) standard. It allows you to tap a filament spool to the scanner to select it in your printer's management software (e.g., PrusaLink), and automatically deducts the filament used after a print.

The device can be configured via a web interface served over Bluetooth Low Energy (BLE), allowing you to set up WiFi and printer credentials without recompiling the firmware.

More details available on the printables website: https://www.printables.com/model/1581648-openprinttag-scanner 

## How to configure
1.  On first boot, or when WiFi is not configured, the device will start a BLE service.
2.  Connect to the device named "OpenPrintTag" from your computer or phone.
3.  Once connected, you can access a web interface at https://ryanch.github.io/openprinttag_scanner/ to:
    *   Configure WiFi SSID and password.
    *   Set the IP address and API key for your PrusaLink-compatible printer.

## Functionality
*   **NFC Tag Reading/Writing:** Reads and writes data to NFC tags formatted according to the OpenPrintTag specification.
*   **Printer Integration:** Communicates with PrusaLink and OctoPrint to:
    *   Notify the printer when a spool has been selected.
    *   Automatically subtract filament weight from the tag after a print is completed.
*   **LCD Display:** Shows the current status of the device, including WiFi connection, NFC scans, and printer status.
*   **Bluetooth Configuration:** Provides a web-based UI over BLE for easy setup.
*   **Extensible Printer Strategy:** The `IPrinterLinkStrategy` interface allows for adding support for other printer control software in the future.

# Hardware Setup

## Hardware Needed
*   NFC Reader/Writer: PN5180 NFC module (ISO 15693)
*   LCD Screen: [16x2 I2C LCD](https://a.co/d/dryhwvd) (only 1 needed)
*   ESP32: [ESP32 DevKitC V4](https://a.co/d/gW3zBIJ) (only 1 needed)

## Hardware Configuration
Connect the components to the ESP32 as follows:

**16x2 I2C LCD (I2C):**
*   **GND:** GND
*   **VCC:** 5V
*   **SDA:** GPIO 23
*   **SCL:** GPIO 22

**PN5180 NFC Module (SPI, right side of ESP32 top to bottom, skipping D12):**

| PN5180 Pin | ESP32 Pin | Direction | Notes |
|------------|-----------|-----------|-------|
| RST        | D13       | Output    | Hardware reset (active low) |
| *(skip)*   | *D12*     | *—*       | *Strapping pin, skip* |
| NSS        | D14       | Output    | SPI chip select (active low) |
| MOSI       | D27       | Output    | SPI data to PN5180 |
| MISO       | D26       | Input     | SPI data from PN5180 |
| SCK        | D25       | Output    | SPI clock |
| BUSY       | D33       | Input     | SPI flow control |
| GPIO       | D32       | Input     | Card detection (future use) |
| IRQ        | D35       | Input     | Interrupt, active HIGH (input-only pin, no pull-up) |
| AUX        | D34       | Input     | Auxiliary monitoring (input-only pin, no pull-up) |
| REQ        | —         | —         | **Not connected.** Only needed for PN5180 firmware updates. |
| VIN        | 5V        | Power     | |
| GND        | GND       | Power     | |

> **Note:** D35 and D34 are input-only pins on the ESP32 (no internal pull-up). D12 is a strapping pin and is skipped.

# Specs Referenced:
*   Python Example: https://github.com/prusa3d/OpenPrintTag/blob/main/utils/rec_update.py
*   PN5180 Datasheet: https://www.nxp.com/docs/en/data-sheet/PN5180A0xx-C3.pdf
*   PrusaLink API Docs: https://hexdocs.pm/prusa_link/PrusaLink.Api.html
*   https://openprinttag.org/generator/ 