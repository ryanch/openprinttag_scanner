#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <Adafruit_PN532.h>

// LCD I2C pins
#define LCD_SDA 23
#define LCD_SCL 22

// PN532 SPI pins (software SPI)
#define PN532_SCK   14
#define PN532_MISO  27
#define PN532_MOSI  26
#define PN532_SS    25
#define PN532_IRQ   33
#define PN532_RESET 32

// LCD at I2C address 0x27 (common for 1602), 16 chars, 2 lines
LiquidCrystal_I2C lcd(0x27, 16, 2);

// PN532 using software SPI
Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);

void setup() {
  delay(500);
  Serial.begin(9600);
  delay(1000);
  Serial.println("=== Starting setup ===");

  // Initialize I2C with custom pins for LCD
  Wire.begin(LCD_SDA, LCD_SCL);
  Serial.println("I2C initialized");

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("hello world");
  Serial.println("LCD initialized");

  // Initialize PN532
  Serial.println("Starting PN532...");
  nfc.begin();
  Serial.println("PN532 begin() done");
  uint32_t versiondata = nfc.getFirmwareVersion();
  Serial.print("Version data: ");
  Serial.println(versiondata, HEX);
  if (!versiondata) {
    lcd.setCursor(0, 1);
    lcd.print("PN532 not found");
    Serial.println("PN532 not found - halting");
    while (1) { delay(1000); } // halt
  }

  // Configure PN532 to read RFID tags
  nfc.SAMConfig();
}

void loop() {
  uint8_t uid[7];
  uint8_t uidLength;

  // Wait for an NFC tag (blocking with 100ms timeout)
  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Scanned");
    delay(1000);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("hello world");
  }
}
