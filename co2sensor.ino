#include "Adafruit_EPD.h"
#include <Adafruit_GFX.h>  // Core graphics library
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include "Lato.h"
#include "pf.h"
#include "FreeSansBold12pt7b.h"
#include "sunrise_i2c.h"
#include <Adafruit_TinyUSB.h> // for Serial


using namespace Adafruit_LittleFS_Namespace;

// Adafruit E-Ink Friend pins
#define EPD_EN 2           // FIXME currently unused
#define DC 0
#define ECS -1
#define SRCS -1
#define RST 1              // can set to -1 and share with microcontroller Reset!
#define EPD_BUSY -1        // can set to -1 to not use a pin (will wait a fixed delay)
#define EPD_SPI &SPI       // primary SPI
#define EPD_SETTLE_MS 1000 // Additional time to wait for the display uptate to complete
#define WIDTH 400
#define HEIGHT 300
#define M 5                // Margin
#define LEGEND_WIDTH 35
#define VGRIDLINE_STEP 36  // gridline every 36 pixels (=5*36 minutes or 3 hours)
#define BATTERY_WIDTH 20
#define BATTERY_HEIGHT 10

// Senseair Sunrise pins
#define CO_EN 3       // GPIO for EN-pin
#define RDY 9         // GPIO for nRDY-pin
#define STABILIZATION_MS 35

extern uint8_t powerDownData[];

// Display
Adafruit_SSD1681 display(HEIGHT, WIDTH, DC, RST, ECS, SRCS, EPD_BUSY, EPD_SPI);

// Sensor
sunrise sunrise;

void setup() {
  pinMode(LED_RED, OUTPUT);
  digitalWrite(LED_RED, LOW);

  Serial.begin(115200);
  while ( !Serial ) delay(10);   // for nrf52840 with native usb
  Serial.println("Booting!");

  pinMode(RDY, INPUT);           // Initialize pin to check if measurement is ready (nRDY-pin)
  pinMode(CO_EN, OUTPUT);        // Initialize pin for enabling sensor
  pinMode(EPD_EN, OUTPUT);       // Initialize pin for enabling display
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);


  InternalFS.begin();

  // Calibrare?
  if (0) {
    delay(10000);
    calibrate();    
    writeSensorStateToFlash();
    Serial.print("Calibration complete.");
    while(1) { delay(1000); }
  }
}

void readSensorStateFromFlash() {
  Serial.print("Reading sensor state from flash... ");

  File file = InternalFS.open("/sensor.bin");
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

  File file = InternalFS.open("/sensor.bin", FILE_O_WRITE);
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

  File file = InternalFS.open("/values.bin", FILE_O_WRITE);
  if (!file) {
    Serial.println("- failed to open /values.bin for appending");
    return;
  }

  // Seek to the end of the file (for a poor man's append)
  file.seek(file.size());
  
  file.write(chopped);
  file.close();
}

size_t getStoredMeasurementCount() {
  File file = InternalFS.open("/values.bin");
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
  int16_t batt_pixels = (batt - 300) / 20; // should be in 0-17 range
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
  File file = InternalFS.open("/values.bin");
  if (!file) {
    Serial.println("- failed to open /values.bin for reading");
    return;
  }

  // Read last [up to] 360 measurements from file and draw them
  int16_t ppm;
  int32_t pos = file.size() - (WIDTH - LEGEND_WIDTH - M);
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
  digitalWrite(EPD_EN, HIGH);     
  display.begin();
  Serial.println("Display ready");
  display.setRotation(1);
  display.clearBuffer();
  display.setFont(&FreeSansBold12pt7b);
  display.setCursor(0, 0);
  display.setTextColor(EPD_BLACK);
  display.setTextWrap(true);
  display.print(text);
  display.display(true);
  digitalWrite(EPD_EN, LOW);     
}

void calibrate() {
    Serial.print("Configuring sensor");
    digitalWrite(CO_EN, HIGH);     
    delay(STABILIZATION_MS);
    sunrise.initSunrise();

    Serial.print("Error status: ");
    Serial.println(sunrise.readErrorStatus(), BIN);
    delay(1000);

    Serial.print("Set number of samples: ");
    Serial.println(sunrise.setNbrSamples(8) ? "OK" : "FAILED");

    Serial.print("Set measurement mode: ");
    Serial.println(sunrise.setMeasurementMode(SINGLE) ? "OK" : "FAILED");

    Serial.print("Disable ABC: ");
    Serial.println(sunrise.setABCPeriod(0) ? "OK" : "FAILED");

    Serial.print("Disable ABC: ");
    Serial.println(sunrise.setABCPeriod(0) ? "OK" : "FAILED");

    Serial.print("Reset sensor: ");
    Serial.println(sunrise.resetSensor() ? "OK" : "FAILED");

    Serial.print("Error status: ");
    Serial.println(sunrise.readErrorStatus(), BIN);
    delay(1000);

    // Serial.print("Clear calibration status: ");
    // Serial.println(sunrise.clearCalibrationStatus() ? "OK" : "FAILED");

    // Serial.print("Write calibration target (420ppm): ");
    // Serial.println(sunrise.writeCalibrationTarget(420) ? "OK" : "FAILED");

    // Serial.print("Perform calibration: ");
    // Serial.println(sunrise.setCalibrationCommand(TARGET_CALIBRATION) ? "OK" : "FAILED");

    // Serial.print("Calibration status: ");
    // Serial.println(sunrise.readCalibrationStatus());

    // String buffer = "";
    // for (int i=0; i < 5; i++) {
    //   uint16_t co2 = measure();
    //   Serial.printf("CO2 : %dppm\n", co2);
    //   buffer += co2;
    //   buffer += "\n";
    //   delay(100);
    // }

    // showText(buffer);
    // display.display();
    // delay(EPD_SETTLE_MS);
}

void loop() {
  Serial.println("************************* WAKING UP ******************************");
  digitalWrite(LED_GREEN, LOW);
  readSensorStateFromFlash();

  size_t numStoredMeasurements = getStoredMeasurementCount();
  Serial.printf("Have %d stored measurements\n", numStoredMeasurements);

  digitalWrite(LED_BLUE, LOW);
  uint16_t co2 = measure();
  digitalWrite(LED_BLUE, HIGH);
  Serial.printf("CO2 : %dppm\n", co2);
  writeMeasurementToFlash(co2);

  Serial.print("Sensor state: ");
  uint8_t count = 0;
  while (count < 24) {
    Serial.print(powerDownData[count++], HEX);
    Serial.print(" ");
  }
  Serial.println("");
  writeSensorStateToFlash();

  digitalWrite(EPD_EN, HIGH);     
  updateDisplay(co2, 0);
  delay(EPD_SETTLE_MS);
  digitalWrite(EPD_EN, LOW);     

  digitalWrite(LED_GREEN, HIGH);
  Serial.println("************************* GOING TO SLEEP ******************************");
  delay(20000);  // 20s
}
