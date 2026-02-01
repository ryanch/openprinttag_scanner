# Objective

Store and retrieve filament details to NFC stickers on spools.
When print jobs complete, subtract used filament from the spool.
Display filament details on the screen when scanned.

# Features






Connect to PrusaLink API


# Components

nfc_manager
- init

printer_manager


prusalink_fetch_strategy

lcd_screen_manager





# Specs:
* Python Example: https://github.com/prusa3d/OpenPrintTag/blob/main/utils/rec_update.py
* NFC Scanner: https://github.com/adafruit/Adafruit-PN532?utm_source=platformio&utm_medium=piohome
* PrusaLink API Docs: https://hexdocs.pm/prusa_link/PrusaLink.Api.html


## Possible code for polling for printer status:
```
import requests
import time

PRINTER_IP = "192.168.1.XXX"
API_KEY = "YOUR_API_KEY"
HEADERS = {"X-Api-Key": API_KEY}

def get_status():
    response = requests.get(f"http://{PRINTER_IP}/api/v1/status", headers=HEADERS)
    return response.json() if response.status_status == 200 else None

def get_file_metadata(storage, path):
    # This retrieves the parsed G-code info, including filament length/weight
    url = f"http://{PRINTER_IP}/api/v1/files{storage}/{path}"
    response = requests.get(url, headers=HEADERS)
    return response.json()

print("Monitoring printer...")
last_state = None

while True:
    data = get_status()
    if data:
        current_state = data['printer']['state']
        
        if current_state == "PRINTING" and last_state != "PRINTING":
            print(f"Job started: {data['job']['file']['path']}")
            
        elif current_state == "FINISHED" and last_state == "PRINTING":
            print("Job complete! Fetching details...")
            # Retrieve metadata for the file that just finished
            file_info = get_file_metadata(data['job']['file']['storage'], data['job']['file']['path'])
            
            # Filament usage is usually in 'filament_m' or 'filament_g' in the metadata
            usage = file_info.get('filament_m', 'Unknown')
            print(f"Filament used: {usage} meters")
            
        last_state = current_state
        
    time.sleep(10) # Poll every 10 seconds
```


# Prompts

I am starting a new esp32 project.

I want you to update src/main.c so that on startup it sets up the 1602LCD module with the LiquidCrystal_I2C using these pins:
LCD SDA - pin 18
LCD SCL - pin 29

update so that after setting up the LiquidCrystal_I2C, it displays the message "hello world".

next, I want you setup the "Adafruit PN532" module with these pins for SPI function:
PN532_SCK  - pin 14
PN532_MISO - pin 27
PN532_MOSI - pin 26
PN532_SS  - pin 25 
PN532_IRQ  - pin 33
PN532_RESET  - pin 32

This is an example on how to setup the PN532:
https://raw.githubusercontent.com/adafruit/Adafruit-PN532/refs/heads/master/examples/ntag2xx_read/ntag2xx_read.ino

for now, setup PN532 so that if I scan a NFC tag, it shows on the LCD screen "Scanned"

Note, ignore the specs dir and lib dir for now. 