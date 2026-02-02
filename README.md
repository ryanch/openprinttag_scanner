
# OpenPrintTag Scanner

## Overview
This project is an ESP32-based NFC scanner and writer designed to work with 3D printer filament spools that follow the [OpenPrintTag](https://openprinttag.org) standard. It allows you to tap a filament spool to the scanner to select it in your printer's management software (e.g., PrusaLink), and automatically deducts the filament used after a print.

The device can be configured via a web interface served over Bluetooth Low Energy (BLE), allowing you to set up WiFi and printer credentials without recompiling the firmware.

## How to configure
1.  On first boot, or when WiFi is not configured, the device will start a BLE service.
2.  Connect to the device named "OpenPrintTag" from your computer or phone.
3.  Once connected, you can access a web interface to:
    *   Configure WiFi SSID and password.
    *   Set the IP address and API key for your PrusaLink-compatible printer.

## Functionality
*   **NFC Tag Reading/Writing:** Reads and writes data to NFC tags (NTAG213/215/216) formatted according to the OpenPrintTag specification.
*   **Printer Integration:** Communicates with PrusaLink and OctoPrint to:
    *   Notify the printer when a spool has been selected.
    *   Automatically subtract filament weight from the tag after a print is completed.
*   **LCD Display:** Shows the current status of the device, including WiFi connection, NFC scans, and printer status.
*   **Bluetooth Configuration:** Provides a web-based UI over BLE for easy setup.
*   **Extensible Printer Strategy:** The `IPrinterLinkStrategy` interface allows for adding support for other printer control software in the future.

# Hardware Setup

## Hardware Needed
*   NFC Reader/Writer: [Adafruit PN532](https://a.co/d/1DlqSIC)
*   LCD Screen: [16x2 I2C LCD](https://a.co/d/dryhwvd) (only 1 needed)
*   ESP32: [ESP32 DevKitC V4](https://a.co/d/gW3zBIJ) (only 1 needed)

## Hardware Configuration
Connect the components to the ESP32 as follows:

**16x2 I2C LCD:**
*   **GND:** GND
*   **VCC:** 5V
*   **SDA:** GPIO 23
*   **SCL:** GPIO 22

**Adafruit PN532 NFC Reader (SPI configuration):**
*   **VCC:** 5V
*   **GND:** GND
*   **SCK:** GPIO 14
*   **MISO:** GPIO 27
*   **MOSI:** GPIO 26
*   **SS (CS):** GPIO 25

# Specs Referenced:
*   Python Example: https://github.com/prusa3d/OpenPrintTag/blob/main/utils/rec_update.py
*   NFC Scanner: https://github.com/adafruit/Adafruit-PN532?utm_source=platformio&utm_medium=piohome
*   PrusaLink API Docs: https://hexdocs.pm/prusa_link/PrusaLink.Api.html
