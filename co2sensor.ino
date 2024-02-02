#include "Adafruit_EPD.h"
#include <Adafruit_GFX.h>  // Core graphics library
#include "FS.h"
#include "SPIFFS.h"
#include "Lato.h"
#include "pf.h"
#include "sunrise_i2c.h"
#include <Wire.h>

#define FORMAT_SPIFFS_IF_FAILED true

// Adafruit E-Ink Friend pins
#define DC 21
#define ECS 3
#define SRCS 7
#define RST 20             // can set to -1 and share with microcontroller Reset!
#define EPD_BUSY -1        // can set to -1 to not use a pin (will wait a fixed delay)
#define EPD_SPI &SPI       // primary SPI
// #define EPD_SETTLE_MS 1500 // Additional time to wait for the display uptate to complete
#define EPD_SETTLE_MS 1000 // Additional time to wait for the display uptate to complete
#define WIDTH 400
#define HEIGHT 300
#define M 5                // Margin
#define LEGEND_WIDTH 35
#define VGRIDLINE_STEP 36  // gridline every 36 pixels (=5*36 minutes or 3 hours)
#define BATTERY_WIDTH 20
#define BATTERY_HEIGHT 10


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
Adafruit_SSD1681 display(HEIGHT, WIDTH, DC, RST, ECS, SRCS, EPD_BUSY, EPD_SPI);

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

uint16_t ppm2y(uint16_t ppm) {
  return HEIGHT + 40 - (ppm >> 3);
}

void updateDisplay(uint16_t co2, int batt) {
  char buffer[4];

  display.begin();
  // For some reason the display does not work after the initialization,
  // but does after the second initialization.
  display.begin();
  Serial.println("Display ready");
  display.setRotation(1);

  // Initialize buffer
  display.clearBuffer();
  display.setTextWrap(false);
  display.setTextColor(EPD_BLACK);

  // Draw battery indicator:
  display.drawRect(M, M, BATTERY_WIDTH, BATTERY_HEIGHT, EPD_BLACK);
  // display.drawRect(M + 1, M + 1, BATTERY_WIDTH - 2, BATTERY_HEIGHT - 2, EPD_BLACK);
  display.drawRect(M + BATTERY_WIDTH, M + 3, 2, BATTERY_HEIGHT - 6, EPD_BLACK);
  int16_t batt_pixels = (batt - 900) / 20; // should be in 0-17 range
  if (batt_pixels > BATTERY_WIDTH - 4) {
    batt_pixels = BATTERY_WIDTH - 4;
  }
  display.fillRect(M + 2, M + 2, batt_pixels, BATTERY_HEIGHT - 4, EPD_BLACK);

  // Draw current measurement
  itoa(co2, buffer, 10);
  display.setFont(&Lato_Black50pt7b);
  int16_t x, y;
  uint16_t w, h;
  display.getTextBounds(buffer, 0, 0, &x, &y, &w, &h);
  display.setCursor(WIDTH - M - w, M + h);
  display.print(buffer);

  // Draw historic graph  
  File file = SPIFFS.open("/values.bin");
  if (!file) {
    Serial.println("- failed to open /values.bin for reading");
    return;
  }

  // Read last [up to] 360 measurements from file and draw them
  int16_t ppm;
  uint32_t pos = file.size() - (WIDTH - LEGEND_WIDTH - M);
  file.seek(pos > 0 ? pos : 0);
  x = LEGEND_WIDTH;
  while (file.available()) {
    ppm = file.read() << 3;
    display.drawLine(x, ppm2y(ppm), x, HEIGHT, EPD_BLACK);
    x++;
  }
  file.close();

  // Draw vertical grid lines every 36 pixels (=3 hours)
  x -= VGRIDLINE_STEP;
  while (x > LEGEND_WIDTH) {
    for (y = ppm2y(1900) + 1; y < HEIGHT; y += 2) {
      display.drawPixel(x, y, EPD_BLACK);
    }
    x -= VGRIDLINE_STEP;
  }

  // Draw horizontal grid lines
  display.setFont(&pf_tempesta_seven4pt7b);
  for (ppm = 1800; ppm >= 400; ppm -= 200) {
    y = ppm2y(ppm);
    itoa(ppm, buffer, 10);
    display.setCursor(M, y + 3);  
    display.print(buffer);
    for (x = LEGEND_WIDTH; x < WIDTH - M; x += 2) {
      display.drawPixel(x, y, EPD_BLACK);
    }
  }

  Serial.println("Displaying...");
  display.display(true);
  Serial.println("Displaying done.");
}

void showText(String text) {
  display.begin();
  display.begin();
  Serial.println("Display ready");
  display.setRotation(1);
  display.clearBuffer();
  // FIXME this font can only display digits!
  display.setFont(&pf_tempesta_seven4pt7b);
  display.setCursor(0, 0);
  display.setTextColor(EPD_BLACK);
  display.setTextWrap(true);
  display.print(text);
}

void calibrate() {
    Serial.print("Configuring sensor");
    digitalWrite(CO_EN, HIGH);     
    delay(STABILIZATION_MS);
    sunrise.initSunrise();

    // Serial.print("Error status: ");
    // Serial.println(sunrise.readErrorStatus(), BIN);
    // delay(1000);

    // Serial.print("Set number of samples: ");
    // Serial.println(sunrise.setNbrSamples(8) ? "OK" : "FAILED");

    // Serial.print("Set measurement mode: ");
    // Serial.println(sunrise.setMeasurementMode(SINGLE) ? "OK" : "FAILED");

    // Serial.print("Disable ABC: ");
    // Serial.println(sunrise.setABCPeriod(0) ? "OK" : "FAILED");

    // Serial.print("Disable ABC: ");
    // Serial.println(sunrise.setABCPeriod(0) ? "OK" : "FAILED");

    // Serial.print("Reset sensor: ");
    // Serial.println(sunrise.resetSensor() ? "OK" : "FAILED");

    Serial.print("Error status: ");
    Serial.println(sunrise.readErrorStatus(), BIN);
    delay(1000);

    Serial.print("Clear calibration status: ");
    Serial.println(sunrise.clearCalibrationStatus() ? "OK" : "FAILED");

    Serial.print("Write calibration target (420ppm): ");
    Serial.println(sunrise.writeCalibrationTarget(420) ? "OK" : "FAILED");

    Serial.print("Perform calibration: ");
    Serial.println(sunrise.setCalibrationCommand(TARGET_CALIBRATION) ? "OK" : "FAILED");

    Serial.print("Calibration status: ");
    Serial.println(sunrise.readCalibrationStatus());

    String buffer = "";
    for (int i=0; i < 5; i++) {
      uint16_t co2 = measure();
      Serial.printf("CO2 : %dppm\n", co2);
      buffer += co2;
      buffer += "\n";
      delay(100);
    }

    showText(buffer);
    display.display();
    delay(EPD_SETTLE_MS);
}

void loop() {
  Serial.println("************************* WAKING UP ******************************");
  digitalWrite(DONE, LOW);     
  readSensorStateFromFlash();

  int batt = analogRead(BATT_LEVEL);
  if (batt > 4000) {
    calibrate();    
    writeSensorStateToFlash();
    digitalWrite(DONE, HIGH);
    while(1) { delay(1000); }
  }


  size_t numStoredMeasurements = getStoredMeasurementCount();
  Serial.printf("Have %d stored measurements\n", numStoredMeasurements);

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

  Serial.printf("Batt : %d\n", batt);
  updateDisplay(co2, batt);
  delay(EPD_SETTLE_MS);

  Serial.println("************************* GOING TO SLEEP ******************************");
  digitalWrite(DONE, HIGH);
  delay(20000);  // 20s
}
