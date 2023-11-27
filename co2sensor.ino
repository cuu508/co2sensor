#include "Adafruit_EPD.h"
#include <Adafruit_GFX.h>  // Core graphics library
#include "FS.h"
#include "SPIFFS.h"
#include "Lato.h"
#include "FreeSansBold12pt7b.h"
#include "sunrise_i2c.h"
#include <Wire.h>

// #define CONFIGURE_SENSOR 1
#define FORMAT_SPIFFS_IF_FAILED true

// Adafruit E-Ink Friend pins
#define DC 21
#define ECS 3
#define SRCS 7
#define RST 20             // can set to -1 and share with microcontroller Reset!
#define EPD_BUSY -1        // can set to -1 to not use a pin (will wait a fixed delay)
#define EPD_SPI &SPI       // primary SPI
#define EPD_SETTLE_MS 1500 // Additional time to wait for the display uptate to complete

// Senseair Sunrise pins
#define CO_EN 2      // GPIO for EN-pin
#define RDY 10        // GPIO for nRDY-pin
#define STABILIZATION_MS 35

// TPL5110
#define DONE 1

// Battery
#define BATT_LEVEL 0

extern uint8_t powerDownData[];

// Display
Adafruit_SSD1608 display(200, 200, DC, RST, ECS, SRCS, EPD_BUSY, EPD_SPI);

// Sensor
sunrise sunrise;

void setup() {
  pinMode(DONE, OUTPUT);        // Initialize pin for turning off power
  digitalWrite(DONE, LOW);     

  Serial.begin(115200);
  Serial.println("Booting!");

  pinMode(BATT_LEVEL, INPUT);   // Initialize pin for reading battery level
  pinMode(RDY, INPUT);           // Initialize pin to check if measurement is ready (nRDY-pin)

  pinMode(CO_EN, OUTPUT);        // Initialize pin for enabling sensor

#ifdef CONFIGURE_SENSOR
  Serial.print("Configuring sensor");
  digitalWrite(CO_EN, HIGH);     
  delay(STABILIZATION_MS);
  sunrise.initSunrise();

  Serial.print("Error status: ");
  Serial.println(sunrise.readErrorStatus(), BIN);
  delay(50);

  Serial.print("Set number of samples: ");
  if (sunrise.setNbrSamples(8)) {
    Serial.println("SUCCESS");
  } else {
    Serial.println("FAILED");
  }

  Serial.print("Set measurement mode: ");
  if (sunrise.setMeasurementMode(SINGLE)) {
    Serial.println("SUCCESS");
  } else {
    Serial.println("FAILED");
  }

  Serial.print("Reset sensor: ");
  if (sunrise.resetSensor()) {
    Serial.println("SUCCESS");
  } else {
    Serial.println("FAILED");
  }
#endif

  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println("SPIFFS Mount Failed");
    while (1) { delay(1000); };
  }

  Serial.printf("Total bytes on flash: %d\n", SPIFFS.totalBytes());
  Serial.printf("Used bytes on flash: %d\n", SPIFFS.usedBytes()); 
}

void readSensorStateFromFlash() {
  Serial.print("Reading sensor state from flash... ");

  File file = SPIFFS.open("/sensor.bin");
  if (!file) {
    Serial.println("- failed to open /sensor.bin for reading");
    return;
  }

  size_t numRead = file.read(powerDownData, 24);
  Serial.printf("- read %d bytes.\n", numRead);
  file.close();
}

void writeSensorStateToFlash() {
  Serial.print("Writing sensor state to flash... ");

  File file = SPIFFS.open("/sensor.bin", FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open /sensor.bin for writing");
    return;
  }
  if (file.write(powerDownData, 24)) {
    Serial.println("- done.");
  } else {
    Serial.println("- failed.");
  }
  file.close();
}

void writeMeasurementToFlash(uint16_t data) {
  // divide by 8
  uint8_t chopped = data >= 2048 ? 255 : data >> 3; 

  File file = SPIFFS.open("/values.bin", FILE_APPEND);
  if (!file) {
    Serial.println("- failed to open /values.bin for appending");
    return;
  }
  
  file.write(chopped);
  file.close();
}

size_t getStoredMeasurementCount() {
  File file = SPIFFS.open("/values.bin");
  if (!file) {
    return 0;
  }

  size_t result = file.size();
  file.close();
  return result;
}

uint16_t measure() {
  digitalWrite(CO_EN, HIGH);     
  delay(STABILIZATION_MS);

  sunrise.initSunrise();
  uint16_t co2 = sunrise.getSingleReading(CO2_FILTERED_COMPENSATED, RDY);  

  digitalWrite(CO_EN, LOW);
  return co2;
}

void updateDisplay(uint16_t co2, int batt) {
  char buffer[4];

  display.begin();
  Serial.println("Display ready");
  display.setRotation(1);

  // Initialize buffer
  display.clearBuffer();
  display.fillScreen(EPD_WHITE);
  display.setTextWrap(false);
  display.setTextColor(EPD_BLACK);

  // Draw battery level
  itoa(batt, buffer, 10);  
  display.setFont(&FreeSansBold12pt7b);
  display.setCursor(0, 20);  
  display.print(buffer);  

  // Draw current measurement
  itoa(co2, buffer, 10);
  display.setFont(&Lato_Black40pt7b);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(buffer, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(100 - w / 2 - 5, 100 + h / 2);
  display.print(buffer);
  
  // // Draw historic graph  
  // File file = SPIFFS.open("/values.bin");
  // if (!file) {
  //   Serial.println("- failed to open /values.bin for appending");
  //   return;
  // }

  // uint32_t pos = file.size() - 200;
  // file.seek(pos > 0 ? pos : 0);
  // x1 = 0;
  // while (file.available()) {
  //   y1 = 200 - file.read() / 2;
  //   // FIXME draw dot on x1, y1
  // }
  // file.close();

  display.display();
}

void loop() {
  Serial.println("************************* WAKING UP ******************************");
  digitalWrite(DONE, LOW);     
  readSensorStateFromFlash();

  size_t numStoredMeasurements = getStoredMeasurementCount();
  Serial.printf("Have %d stored measurements\n", numStoredMeasurements);
  // With 5 minute wakeups, every 12th measurement marks one hour
  if (numStoredMeasurements % 12 == 0) {
    sunrise.incrementABCTime();
  }

  uint16_t co2 = measure();
  writeMeasurementToFlash(co2);
  Serial.printf("CO2 : %dppm\n", co2);

  Serial.print("Sensor state: ");
  uint8_t count = 0;
  while (count < 24) {
    Serial.print(powerDownData[count++], HEX);
    Serial.print(" ");
  }
  Serial.println("");
  writeSensorStateToFlash();

  int batt = analogRead(BATT_LEVEL);
  Serial.printf("Batt : %d\n", batt);
  updateDisplay(co2, batt);
  delay(EPD_SETTLE_MS);

  Serial.println("************************* GOING TO SLEEP ******************************");
  digitalWrite(DONE, HIGH);
  delay(10000);  // 10s
}