/* Air Sensor Everywhere_v.1
   Small Sensors Group - University of Massachusetts Amherst
   Environmental Health Science - Lab of Richard Peltier
   Implemented by: Filimon Kiros and Benjamin Lazan

   Arduino UNO with Adafruit Sdfat Data Logger Sheild

   Wiring and wire colors

   Digital Pins
   0  - reserved for RX
   1  - reserved for TX
   2  -
   3  - Temp/Humidity - green
   4  - Shinyei PM2.5 -purple
   5  -
   6  -
   7  -
   8  -
   9  -
   10 - reserved for SD * only on old Rev A adafruit loggers
   11 - reserved for SD * only on old Rev A adafruit loggers
   12 - reserved for SD * only on old Rev A adafruit loggers
   13 - reserved for SD * (LED power)

   Analog Pins
   AO - CO - yellow
   A1 - O3 - green
   A2 -
   A3 -
   A5 - reserved for RTC
   A6 - reserved for RTC


   Ground Pins
   CO - brown
   O3 - black
   PM sensor - white
   Temp/Rh - black

   Power Pins
   CO - 5V -red
   O3 - 5V -orange
   PM sensor - 5V - purple
   Temp/Humid - 3.3V - red


   Notes:

   Shinyei Wiring:
                     Front of Unit (10K Ohm resisitor connecting GND to Pin 1)
                     This makes P2 more sensitive (REF: https://indiaairquality.com/2016/07/24/circuit-diagram-for-the-42r/)
                     Take reading from P2 channel only.

    pin    1       2       3      4       5
    use    empty   P1      5V      P2      GND
    color                  purple  grey    white

   Real Time Clock:
     To calibrate an Adafruit Sdfat logger RTC, one must
     install a battery on the Sdfat shield, Download the
     Adafruit RTClib library, and run the PCF8523 example
     sketch before loading this air sensor software

*/
#include <Wire.h>
#include "LowPower.h"
#include <SPI.h>
#include "DHT.h"
#include "FreeStack.h"
#include "SdFat.h"
#include "RTClib.h"
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>

//------------------------------------------------------------
//    Configuration Constants
//------------------------------------------------------------

//
// So you want to change a component?
//
// Then the corresponding component ID number below may be changed,
// and the config_date value should be updated in the format YYYY-MM-DD
// This new configuration will be recorded into the database.
//

const char CONFIG_DATE[12] = "${a}";
const int SENSOR_ID =           ${b};
const int ENCLOSURE_ID =        ${c};
const int ARDUINO_ID =          ${d};
const int DATASHIELD_ID =       ${e};
const int SDCARD_ID =           ${m};
const int SHINYEI_ID =          ${j};
const int O3_SENSOR_ID =        ${f};
const int CO_SENSOR_ID =        ${h};
const int DHT22_ID =            ${l};
const int BATTERY_ID =          ${n};

// logging options
#define LOG_INTERVAL 60000
#define LOG_TO_SERIAL true

// sleep and wake settings in milliseconds
// e.g.
//  30min = 1800000
//  1hr = 3600000
//  2hrs = 7200000
#define WAKE_DURATION    3600000
#define SLEEP_DURATION   1800000

// calculate number of sleep cycles based on 8 second sleep intervals
// you shouldn't need to touch this
#define SLEEP_CYCLES   SLEEP_DURATION / 8000

// pin to switch transistor for power down everything entering sleep state
#define transistor_pin 2

RTC_PCF8523 RTC;

// Set SD pin
#define chipSelect 10

#define ledPin 3

SdFat sd;
SdFile logfile;

// DHT-22 Temp and Humidity
#define DHTPIN 6
DHT dht(DHTPIN, DHT22);

//
// Gas sensor settings
//
enum Gases { CO, O3 };

// Analog read pins
#define CO_PIN 1
#define O3_PIN 0

// Shenyei PM variables
#define PM_P2_PIN 7
unsigned long duration;
unsigned long starttime;
unsigned long sampletime_ms = 300000;   //300sec or 5min
unsigned long lowpulseoccupancy = 0;
float ratio = 0;
float concentration = 0;
float PM25count;
float PM25conc;

uint32_t timer = millis();
uint32_t lastSleepCycle = millis();

void setup() {

#if LOG_TO_SERIAL
  Serial.begin(9600);
  while (!Serial);
#endif

  Wire.begin();

  if (!RTC.begin()) {
    logfile.println("RTC failed");
#if LOG_TO_SERIAL
    Serial.println("RTC failed");
#endif
  }

  pinMode(ledPin, OUTPUT);
  pinMode(chipSelect, OUTPUT);
  pinMode(transistor_pin, OUTPUT);
  // see if the card is present and can be initialized:
  if (!sd.begin(chipSelect)) {
#if LOG_TO_SERIAL
    Serial.println("Card init. failed!");
#endif
    fatalBlink();
  }

  // this creates file with the format 2017_1_23_20h32m_SENSOR001.txt
  // time on file is set when arduino starts up
  DateTime now = RTC.now();
  int month = now.month();
  int day = now.day();
  int year = now.year();
  int hour = now.hour();
  int minute = now.minute();
  char filename[35] = "";
  sprintf(filename, "SENSOR%.3d_%.4d-%.2d-%.2d_%.2d%.2d.txt",
          SENSOR_ID, year % 10000, month % 100, day % 100, hour % 100, minute % 100);

  // open logfile for writing, or throw error
  if (!logfile.open(filename, O_RDWR | O_CREAT | O_AT_END)) {
    sd.errorHalt("opening file for write failed");
    fatalBlink();
  }

#if LOG_TO_SERIAL
  Serial.print("Writing to ");
  Serial.println(filename);
#endif
  // flashing LED indicated success in writing to sd file
  for (int i = 0; i < 5; i++) {
    digitalWrite(ledPin, HIGH);
    delay(200);
    digitalWrite(ledPin, LOW);
    delay(200);
  }

  dht.begin();

  pinMode(PM_P2_PIN, INPUT);
  starttime = millis();

  //
  // File headers
  //
  // NOTE: Please don't change the file headers if you intend to load the
  // datas into the database using the database program. It relies on them
  // to stay juuuust as they are. -Ben

  logfile.print(F("# "));
  logfile.println(filename);
  logfile.println(F("# "));

  // Sensor configuration header
  // This information is read into the database to store the current
  // sensor component configuration.
  logfile.println(F("# Sensor Configuration:"));
  logfile.print(F("# config_date: "));
  logfile.println(CONFIG_DATE);
  logfile.print(F("# sensor_id: "));
  logfile.println(SENSOR_ID);
  logfile.print(F("# enclosure_id: "));
  logfile.println(ENCLOSURE_ID);
  logfile.print(F("# arduino_id: "));
  logfile.println(ARDUINO_ID);
  logfile.print(F("# datashield_id: "));
  logfile.println(DATASHIELD_ID);
  logfile.print(F("# sdcard_id: "));
  logfile.println(SDCARD_ID);
  logfile.print(F("# shinyei_id: "));
  logfile.println(SHINYEI_ID);
  logfile.print(F("# o3_sensor_id: "));
  logfile.println(O3_SENSOR_ID);
  logfile.print(F("# co_sensor_id: "));
  logfile.println(CO_SENSOR_ID);
  logfile.print(F("# dht22_id: "));
  logfile.println(DHT22_ID);
  logfile.println(F("# If you swap parts record the new component ID and new configuration date in the file SENSORHERE.ino"));
  logfile.println(F("# TEMP(degC),HUMID(%),PM2.5_ug/m3,PM2.5_#/0.01ft3,CO_PPM,CO_V,O3_PPB,O3_V, YYYY-MM-DD HH:MM:SS"));

#if LOG_TO_SERIAL
  Serial.print(F("# "));
  Serial.println(filename);
  Serial.println(F("# "));
  Serial.println(F("# Sensor Configuration:"));
  Serial.print(F("# config_date: "));
  Serial.println(CONFIG_DATE);
  Serial.print(F("# sensor_id: "));
  Serial.println(SENSOR_ID);
  Serial.print(F("# enclosure_id: "));
  Serial.println(ENCLOSURE_ID);
  Serial.print(F("# arduino_id: "));
  Serial.println(ARDUINO_ID);
  Serial.print(F("# datashield_id: "));
  Serial.println(DATASHIELD_ID);
  Serial.print(F("# sdcard_id: "));
  Serial.println(SDCARD_ID);
  Serial.print(F("# shinyei_id: "));
  Serial.println(SHINYEI_ID);
  Serial.print(F("# o3_sensor_id: "));
  Serial.println(O3_SENSOR_ID);
  Serial.print(F("# co_sensor_id: "));
  Serial.println(CO_SENSOR_ID);
  Serial.print(F("# dht22_id: "));
  Serial.println(DHT22_ID);
  Serial.println(F("# If you swap parts record the new component ID and new configuration date in the file SENSORHERE.ino"));
  Serial.println(F("# TEMP(degC),HUMID(%),PM2.5_ug/m3,PM2.5_#/0.01ft3,CO_PPM,CO_V,O3_PPB,O3_V,YYYY-MM-DD HH:MM:SS"));
#endif
}

void loop() 
{
 // Wake cycle
  logSensorReadings();
  
  // Sleep cycle
  if (millis() - lastSleepCycle > WAKE_DURATION)
  {
    digitalWrite(transistor_pin, LOW);
    for (int i = 0; i < SLEEP_CYCLES; i++)
    {
      LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
      // keep battery alive if 96 seconds have elapsed
      if ((i % 12) == 0)
      {
        digitalWrite(transistor_pin, HIGH);
        delay(20);
        digitalWrite(transistor_pin, LOW);
      }
    }
    digitalWrite(transistor_pin, HIGH);
    lastSleepCycle = millis();
  }
}

void logSensorReadings() {

  if (timer > millis())  timer = millis();
  // run Shinyei PM calculations
  calculatePM();

  if (millis() - timer > LOG_INTERVAL) {
    timer = millis();

    DateTime now = RTC.now();
    int year = now.year();
    int month = now.month();
    int day = now.day();
    int hour = now.hour();
    int minute = now.minute();
    int second = now.second();
    // print temperature and humidity
    // LETS CONVERT TO THE ADAFRUIT lib
    float temp = dht.readTemperature();
    float humid = dht.readHumidity();
    // PM25conc      we have these two as global variables
    // PM25count
    float coPPM = calculateGas(CO);
    float coVolt = readVoltage(CO_PIN);
    float o3PPM = calculateGas(O3);
    float o3Volt = readVoltage(O3_PIN);

    char datetime[25] = "";
    sprintf(datetime, "%.4d-%.2d-%.2d %.2d:%.2d:%.2d", year, month, day, hour, minute, second);

    // Not an elegant solution, but an extensible one
    logfile.print(temp);
    logfile.print(", ");
    logfile.print(humid);
    logfile.print(", ");
    logfile.print(PM25conc);
    logfile.print(", ");
    logfile.print(PM25count);
    logfile.print(", ");
    logfile.print(coPPM);
    logfile.print(", ");
    logfile.print(coVolt);
    logfile.print(", ");
    logfile.print(o3PPM);
    logfile.print(", ");
    logfile.print(o3Volt);
    logfile.print(", ");
    logfile.println(datetime);

#if LOG_TO_SERIAL
    Serial.print(temp);
    Serial.print(", ");
    Serial.print(humid);
    Serial.print(", ");
    Serial.print(PM25conc);
    Serial.print(", ");
    Serial.print(PM25count);
    Serial.print(", ");
    Serial.print(coPPM);
    Serial.print(", ");
    Serial.print(coVolt);
    Serial.print(", ");
    Serial.print(o3PPM);
    Serial.print(", ");
    Serial.print(o3Volt);
    Serial.print(", ");
    Serial.println(datetime);
#endif

    // write to sd card
    logfile.flush();
  }
}


//
// readvoltage
//
// Converts from the ADC analogRead value (0-1023)
// and maps it to a 0-5 value.
//
float readVoltage(int PIN) {
  return (analogRead(PIN) * (5.0 / 1023));
}


//
// calculateGas()
//
// caculate gas performs ppm/ppb conversions on raw data
// input: gasVoltage - integer analogRead from MICS sensor
//
// To add a gas you must add the gas to the enum Gases variable
// in the gas settings section, you must add a case to the switch
// statement below which returns a result, it is good practice to add a #define statement
// to assign the analog pin number to a variable which can be easily
// changed if needed. e.g.
//                  #define NO2_PIN 9
//
float calculateGas(int gas) {
  float result;
  float voltage;             // v has significant decimals
  switch (gas) {
    float x;
    case CO:
      x = readVoltage(CO_PIN);
      result = ${i};
      if (result < 0)
        result = 0;
      return result;
      break;

    case O3:
      x = readVoltage(O3_PIN);
      result = ${g};
      if (result < 0)
        result = 0;
      return result;
      break;

  }
}

//
// calculatePM
//
// This function runs in the main loop every millisecond,
// concentration and particle count are only calculated when
// sampletime_ms has elapsed, sampletime_ms can be adjusted in the
// PM variables section towards the top of this file
//
void calculatePM() {
  duration = pulseIn(PM_P2_PIN, LOW);
  lowpulseoccupancy = lowpulseoccupancy + duration;

  if ((millis() - starttime) > sampletime_ms) {
    ratio = lowpulseoccupancy / (sampletime_ms * 10.0);
    PM25count = 1.1 * pow(ratio, 3) - 3.8 * pow(ratio, 2) + 520 * ratio + 0.62;
    // PM2.5 count (#/0.01ft3) to mass concentration (ug/m3) conversion
    float x = PM25count;
    PM25conc = ${k};                 // Shinyie_4 Equation (power function)
    lowpulseoccupancy = 0;
    starttime = millis();
  }
}

//
// fatalBlink
//
// Produces the blinky light of death.
//
void fatalBlink() {
  while (1) {
    delay(1000);
    for (int i = 0; i < 10; i++) {
      digitalWrite(ledPin, HIGH);
      delay(50);
      digitalWrite(ledPin, LOW);
      delay(50);
    }
  }
}
