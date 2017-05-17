#include <dummy.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiAP.h>
#include <ESP8266WiFiGeneric.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WiFiScan.h>
#include <ESP8266WiFiSTA.h>
#include <ESP8266WiFiType.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFiServer.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include "Wire.h"
#include "SendEmail.h"
#include "FS.h"

/////////////////// Globals and #defines ///////////////////

// File name to look for in file system
#define DEFAULT_CONFIG_FILE_NAME "/config.ini"

// Arbitrary sizes for char buffers. char[] is required for wifi libaries so its used instead of String
#define SSID_MAX_LENGTH 30
#define WIFI_PASSWORD_MAX_LENGTH 30

// Configuration file string definitions
#define CONFIG_WIFI_SSID "wifi_ssid"
#define CONFIG_WIFI_PASSWORD "wifi_password"
#define CONFIG_DEVICE_NAME "device_name"
#define CONFIG_SMTP_SERVER "smtp_server"
#define CONFIG_SMTP_ACCOUNT "smtp_account"
#define CONFIG_SMTP_PASSWORD "smtp_password"
#define CONFIG_NOTIFICATION_EMAIL "notification_email"
#define CONFIG_NOTIFICATION_CO2_PPM "notification_co2_ppm"
#define CONFIG_NOTIFICATION_FREQUENCY_MINUTES "notification_frequency_minutes"
#define CONFIG_START_HOUR "start_hour"
#define CONFIG_STOP_HOUR "stop_hour"

// Configuration variables
char wifiSSID[SSID_MAX_LENGTH];
char wifiPassword[WIFI_PASSWORD_MAX_LENGTH];
String deviceName;
String smtpServer;
String smtpAccount;
String smtpPassword;
String notificationEmail;
int notificationCo2PPM;
int notificationFrequencyMinutes = 0;
int startHour;
int stopHour;

// Configuration Button pin to enter into configuration mode
int configButtonPin = 0;

// Status RGB LED pins
int ledPinRed = 4;
int ledPinBlue = 12;
int ledPinGreen = 13;

// Sensor mosfet pin used to turn sensor on/off when going out/in sleep mode
int pfetGatePin = 5;

// Configuration web server
ESP8266WebServer server(80);

// I2C pin and device definitions
#define AIR_SENSOR_I2C_ADDRESS 0x5A
#define AIR_SENSOR_DATA_LENGTH 9
const int SDAPin = 2;
const int SCLPin = 14;

// System state variables
bool inConfigState = false;
bool inWarmUpState = false;
bool inSuccessfulSensorState = false;
bool inThresholdExceededState = false;
bool inWifiConnectedState = false;

// Last measured air quality values
int lastCo2Read = 0;
int lastTvocRead = 0;

// Next MS we are allowed to send another notification email
unsigned long emailTimer = 0;

// We dont want to count multiple config button state transitions as multiple button presses
unsigned long debounceTimer = 0;
#define BUTTON_DEBOUNCE_PERIOD_MS 500

// How often in MS to poll the air quality sensor
unsigned long sensorTimer = 0;
#define SENSOR_TIMER_PERIOD_MS 500

int currentGMTHour = 0;
int currentGMTMinute = 0;

unsigned long millisUntilSleep;

/////////////////// setup() and loop() ///////////////////

void setup()
{
  // Setup I2C pins. NOTE: Clock stretching is REQUIRED for the AMS iAQ module. it uses it extensively
  Wire.begin(SDAPin, SCLPin);
  Wire.setClock(100000);
  Wire.setClockStretchLimit(15000);

  // Setup GPIO for state LED and config button
  pinMode(configButtonPin, INPUT_PULLUP);
  pinMode(ledPinRed, OUTPUT);
  pinMode(ledPinBlue, OUTPUT);
  pinMode(ledPinGreen, OUTPUT);

  // Keep the iAQ sensor on by default
  pinMode(pfetGatePin, OUTPUT);
  powerOnSensor();

  // Turn RGB LED On
  analogWrite(ledPinRed, 1);
  analogWrite(ledPinBlue, 1);
  analogWrite(ledPinGreen, 1);

  // Setup Debug port
  Serial.begin(115200);

  // Start File System, attempt to retrieve settings
  SPIFFS.begin();
  if (SPIFFS.exists(DEFAULT_CONFIG_FILE_NAME))
  {
    Serial.print("Using Config File: ");
    Serial.println(DEFAULT_CONFIG_FILE_NAME);
    File configFile = SPIFFS.open(DEFAULT_CONFIG_FILE_NAME, "r");

    Serial.println("Using Settings: ");
    while (configFile.available())
    {
      String settingLine = configFile.readStringUntil('\n');
      parseSettingLine(settingLine);
    }
  }
  else
  {
    Serial.print("Settings file doesnt exist, creating empty file and entering config mode.");
    File configFile = SPIFFS.open(DEFAULT_CONFIG_FILE_NAME, "w");
    configFile.close();
    enterConfigState();
    return;
  }

  // This will connect to wifi and allow us to query a timestamp
  enterNormalState();

  // Get current time, determine if we should be sleeping or not
  int i = 0;
  while (!getNetworkTime() && i < 10)
  {
    i++;
  }

  // TODO: Handle 24 hour operating mode

  // If we are supposed to be running, find out when we should sleep
  if (inOperatingTimespan(currentGMTHour))
  {
    int minutesLeftToRun = minutesUntilEndOfOperatingTimespan(currentGMTHour, currentGMTMinute);
    Serial.print("In operating window for minutes: ");
    Serial.println(minutesLeftToRun);

    millisUntilSleep = minutesLeftToRun * 60 * 1000; // TODO: overflow?
  }
  // If we are supposed to be sleeping, find out when we should wake
  else
  {
    int minutesLeftToSleep = minutesUntilStartOfOperatingTimespan(currentGMTHour, currentGMTMinute);
    Serial.print("Out of operating window. Sleeping for minutes: ");
    Serial.println(minutesLeftToSleep);

    // Sleep for 1 hour max. I dont think the ESP8266 is that accurate when sleeping that long
    if (minutesLeftToSleep > 60)
      minutesLeftToSleep = 60;

    powerOffSensor();
    ESP.deepSleep(minutesLeftToSleep * 60 * 1000000);
  }
}

void loop()
{
  // Do simple button debouncing
  readButtonState();

  // Update status LED with system state
  updateLEDState();

  // If we are in the config state, then run the settings website
  if (inConfigState)
    server.handleClient();
  else if (WiFi.status() == WL_CONNECTION_LOST)
    enterNormalState();

  // Query sensor every so often
  if (millis() - sensorTimer >= SENSOR_TIMER_PERIOD_MS)
  {
    sensorTimer = millis();
    querySensor();
  }

  // Check if we should sleep
  if (millis() >= millisUntilSleep)
  {
    // Update timestamp
    int i = 0;
    while (!getNetworkTime() && i < 10)
    {
      i++;
    }
    int minutesToSleep = minutesUntilStartOfOperatingTimespan(currentGMTHour, currentGMTMinute);
    if (minutesToSleep > 60)
      minutesToSleep = 60;

    Serial.print("Time to sleep: ");
    Serial.println(minutesToSleep);
    powerOffSensor();
    ESP.deepSleep(minutesToSleep * 60 * 1000000);
  }
}

/////////////////// State functions ///////////////////

// Shut down webserver, connect to SSID from settings
void enterNormalState()
{
  inConfigState = false;
  updateLEDState(); // Give feedback immediately
  WiFi.softAPdisconnect(true);

  Serial.print("Attempting to connect to wifi with: ");
  Serial.println(wifiSSID);
  WiFi.begin(wifiSSID, wifiPassword);
  // Attempt to connect to the wifi for a limited time. Blink the LED to give feedback.
  // If we fail, we can at least re-enter config mode and change the SSID/Password and try again
  int i = 0;
  for (i = 0; (i < 40) && (WiFi.status() != WL_CONNECTED); i++)
  {
    delay(500);
    if (i % 2)
    {
      analogWrite(ledPinRed, 1);
      analogWrite(ledPinBlue, 1);
      analogWrite(ledPinGreen, 1);
    }
    else
    {
      analogWrite(ledPinRed, 1023);
      analogWrite(ledPinBlue, 1023);
      analogWrite(ledPinGreen, 1023);
    }
    Serial.print(".");
  }
  Serial.println("WiFi connected ");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  inWifiConnectedState = true;
}

// Disconnect from wifi, start configuration webserver
void enterConfigState()
{
  inConfigState = true;
  updateLEDState(); // Give feedback immediately
  WiFi.disconnect();
  inWifiConnectedState = false;
  bool apStartSuccess = WiFi.softAP("ScottsESP", "anniepassword");
  if (apStartSuccess)
  {
    Serial.print("Webserver start success: ");
    Serial.println(WiFi.softAPIP());
    server.on("/", handleRoot);
    server.on("/settings", handleSettings);
    server.on("/reading", handleLastReading);
    server.begin();
  }
  else
    Serial.println("Webserver start failure.");
}

// Update LED based on state
// Blue if in config mode
// Red if sensor threshold is exceeded
// Yellow if sensor is still warming up
// Green if connected to wifi and sensor is giving valid reads
void updateLEDState()
{
  if (inConfigState) // Blue
  {
    analogWrite(ledPinRed, 1023);
    analogWrite(ledPinBlue, 1);
    analogWrite(ledPinGreen, 1023);
  }
  else if (inThresholdExceededState) // Red
  {
    analogWrite(ledPinRed, 1);
    analogWrite(ledPinBlue, 1023);
    analogWrite(ledPinGreen, 1023);
  }
  else if (inWarmUpState) // Yellow
  {
    analogWrite(ledPinRed, 1);
    analogWrite(ledPinBlue, 1023);
    analogWrite(ledPinGreen, 1);
  }
  else if (inSuccessfulSensorState && inWifiConnectedState) // Green
  {
    analogWrite(ledPinRed, 1023);
    analogWrite(ledPinBlue, 1023);
    analogWrite(ledPinGreen, 1);
  }
}

void readButtonState()
{
  // ESP8266 is active low
  if (debounceTimer == 0 && digitalRead(configButtonPin) == LOW)
  {
    // Button has been pressed, change to the opposite state
    if (inConfigState)
      enterNormalState();
    else
      enterConfigState();

    Serial.print("Config mode changed: ");
    Serial.println(inConfigState);

    debounceTimer = millis() + BUTTON_DEBOUNCE_PERIOD_MS;
  }

  // Has the debounce counter expired? If so, then reset it and allow another button press
  if (millis() > debounceTimer)
    debounceTimer = 0;
}
/////////////////// Helper functions ///////////////////

bool sendNotificationEmail(String body)
{
  if ((millis() < emailTimer) || inConfigState)
    return false;

  SendEmail e(smtpServer, 465, smtpAccount, smtpPassword, 5000, true);

  String adjustedSmtpAccount = smtpAccount;
  String adjustedNotificationEmail = notificationEmail;

  // Gmail requires <> around to/from addresses
  if (smtpAccount.endsWith("@gmail.com"))
  {
    adjustedSmtpAccount = "<" + smtpAccount + ">";
    adjustedNotificationEmail = "<" + notificationEmail + ">";
  }

  bool result = e.send(adjustedSmtpAccount, adjustedNotificationEmail, deviceName, body);
  Serial.print("Email result " + result);
  Serial.println(result);

  emailTimer = millis() + (notificationFrequencyMinutes * 60 * 1000);

  return result;
}

// Get Air Quality data from the iAQ sensor
void querySensor()
{
  // See datasheet for details on data format, etc
  Wire.requestFrom(AIR_SENSOR_I2C_ADDRESS, AIR_SENSOR_DATA_LENGTH);
  unsigned char co2MSB = Wire.read();
  unsigned char co2LSB = Wire.read();

  unsigned char statusByte = Wire.read();

  // TODO: Do I care about these values?
  unsigned char resistanceByte3 = Wire.read();
  unsigned char resistanceByte4 = Wire.read();
  unsigned char resistanceByte5 = Wire.read();
  unsigned char resistanceByte6 = Wire.read();

  unsigned char tvocMSB = Wire.read();
  unsigned char tvocLSB = Wire.read();

  if (statusByte == 0x00)
  {
    inWarmUpState = false;
    inSuccessfulSensorState = true;

    unsigned int co2PPM = (co2MSB * 256) + co2LSB;
    unsigned int tvocPPB = (tvocMSB * 256) + tvocLSB;
    Serial.print("CO2 PPM: ");
    Serial.print(co2PPM);
    Serial.print(" TVOC PPB: ");
    Serial.println(tvocPPB);

    lastCo2Read = co2PPM;
    lastTvocRead = tvocPPB;

    if (co2PPM > notificationCo2PPM)
    {
      inThresholdExceededState = true;
      updateLEDState();

      String notificationText = "CO2 threshold hit: ";
      notificationText.concat(co2PPM);
      notificationText.concat(" TVOC ppm: ");
      notificationText.concat(tvocPPB);

      sendNotificationEmail(notificationText);
    }
    else
      inThresholdExceededState = false;
  }
  else
  {
    Serial.print("Sensor Not ready: 0x");
    inWarmUpState = true;
    inSuccessfulSensorState = false;
    Serial.println(statusByte, HEX);
  }
}

void powerOnSensor(void)
{
  digitalWrite(pfetGatePin, HIGH);
}

void powerOffSensor(void)
{
  digitalWrite(pfetGatePin, LOW);
}
