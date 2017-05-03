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

#define DEFAULT_CONFIG_FILE_NAME "/config.ini"

// Arbitrary sizes for char buffers. char[] is required for wifi libaries so its used instead of String
#define SSID_MAX_LENGTH 30
#define WIFI_PASSWORD_MAX_LENGTH 30

#define CONFIG_WIFI_SSID "wifi_ssid"
#define CONFIG_WIFI_PASSWORD "wifi_password"
#define CONFIG_DEVICE_NAME "device_name"
#define CONFIG_SMTP_SERVER "smtp_server"
#define CONFIG_SMTP_ACCOUNT "smtp_account"
#define CONFIG_SMTP_PASSWORD "smtp_password"
#define CONFIG_NOTIFICATION_EMAIL "notification_email"
#define CONFIG_NOTIFICATION_CO2_PPM "notification_co2_ppm"
#define CONFIG_NOTIFICATION_FREQUENCY_MINUTES "notification_frequency_minutes"

// Configuration variables
char wifiSSID[SSID_MAX_LENGTH];
char wifiPassword[WIFI_PASSWORD_MAX_LENGTH];
String deviceName;
String smtpServer;
String smtpAccount;
String smtpPassword;
String notificationEmail;
int notificationCo2PPM;
int notificationFrequencyMinutes;

// Configuration Button and status LED pins
int configButtonPin = 5;

int ledPinRed = 4;
int ledPinBlue = 12;
int ledPinGreen = 13;

// Config web server
ESP8266WebServer server(80);

// I2C pin and device definitions
#define AIR_SENSOR_I2C_ADDRESS 0x5A
#define AIR_SENSOR_DATA_LENGTH 9
const int SDAPin = 2;
const int SCLPin = 14;

// State variables
bool inConfigState = false;
bool inWarmUpState = false;
bool inSuccessfulSensorState = false;
bool inThresholdExceededState = false;
bool inWifiConnectedState = false;

int lastCo2Read = 0;
int lastTvocRead = 0;

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

  // Turn RGB LED Off
  analogWrite(ledPinRed, 1023);
  analogWrite(ledPinBlue, 1023);
  analogWrite(ledPinGreen, 1023);

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

  enterNormalState();
}

bool parseSettingLine(String settingLine)
{
  int indexOfFieldSeparator = settingLine.indexOf("=");
  String settingString = settingLine.substring(indexOfFieldSeparator + 1, settingLine.length() - 1);
  Serial.println(settingString);

  if (settingLine.startsWith(CONFIG_WIFI_SSID))
    settingString.toCharArray(wifiSSID, SSID_MAX_LENGTH);
  else if (settingLine.startsWith(CONFIG_WIFI_PASSWORD))
    settingString.toCharArray(wifiPassword, WIFI_PASSWORD_MAX_LENGTH);
  else if (settingLine.startsWith(CONFIG_DEVICE_NAME))
    deviceName = settingString;
  else if (settingLine.startsWith(CONFIG_SMTP_SERVER))
    smtpServer = settingString;
  else if (settingLine.startsWith(CONFIG_SMTP_ACCOUNT))
    smtpAccount = settingString;
  else if (settingLine.startsWith(CONFIG_SMTP_PASSWORD))
    smtpPassword = settingString;
  else if (settingLine.startsWith(CONFIG_NOTIFICATION_EMAIL))
    notificationEmail = settingString;
  else if (settingLine.startsWith(CONFIG_NOTIFICATION_CO2_PPM))
    notificationCo2PPM = settingString.toInt();
  else if (settingLine.startsWith(CONFIG_NOTIFICATION_FREQUENCY_MINUTES))
    notificationFrequencyMinutes = settingString.toInt();
  else
  {
    Serial.print("Unknown Setting: ");
    Serial.println(settingLine);
  }
}

#define EMAIL_NOTIFICATION_TIMESPAN (10 * 60 * 1000) // Only send at most once every X mins
unsigned long emailTimer = 0;

bool sendNotificationEmail(String body)
{
  if ((millis() < emailTimer) || inConfigState)
    return false;

  SendEmail e(smtpServer, 465, smtpAccount, smtpPassword, 5000, true);

  String adjustedSmtpAccount = smtpAccount;
  String adjustedNotificationEmail = notificationEmail;

  // Gmail requires <> around to/from addresses
  if (smtpAccount.endsWith("@gmail.com"))
    adjustedSmtpAccount = "<" + smtpAccount + ">";

  if (notificationEmail.endsWith("@gmail.com"))
    adjustedNotificationEmail = "<" + notificationEmail + ">";

  bool result = e.send(adjustedSmtpAccount, adjustedNotificationEmail, deviceName, body);
  Serial.print("Email result " + result);
  Serial.println(result);

  emailTimer = millis() + EMAIL_NOTIFICATION_TIMESPAN;

  return result;
}

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

// We dont want to count multiple config button state transitions as multiple button presses
int debounceTimer = 0;
#define BUTTON_DEBOUNCE_TIME_MS 1000

void readButtonState()
{
  // ESP8266 is active low
  if (debounceTimer == 0 && digitalRead(configButtonPin) == LOW)
  {
    // Button has been pressed, change states
    if (inConfigState)
      enterNormalState();
    else
      enterConfigState();

    Serial.print("Config mode changed: ");
    Serial.println(inConfigState);
    debounceTimer = millis() + BUTTON_DEBOUNCE_TIME_MS;
  }

  // Has the counter expired? If so, then reset it
  if (millis() > debounceTimer)
    debounceTimer = 0;
}

// TODO: Update LED based on state
// Blue if in config mode
// Red if sensor threshold is exceeded
// Yellow if sensor is still warming up
// Green if connected to wifi and sensor is completed warming up
void updateLEDState()
{
  if (inConfigState)
  {
    analogWrite(ledPinRed, 1023); // Blue
    analogWrite(ledPinBlue, 1);
    analogWrite(ledPinGreen, 1023);
  }
  else if (inThresholdExceededState)
  {
    analogWrite(ledPinRed, 1); // Red
    analogWrite(ledPinBlue, 1023);
    analogWrite(ledPinGreen, 1023);
  }
  else if (inWarmUpState)
  {
    analogWrite(ledPinRed, 1); // Yellow
    analogWrite(ledPinBlue, 1023);
    analogWrite(ledPinGreen, 1);
  }
  else if (inSuccessfulSensorState && inWifiConnectedState)
  {
    analogWrite(ledPinRed, 1023); // Green
    analogWrite(ledPinBlue, 1023);
    analogWrite(ledPinGreen, 1);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
unsigned long sensorTimer = 0;
int sensorTimerPeriod = 500; // How often in MS to poll the air quality sensor

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
  if (millis() - sensorTimer >= sensorTimerPeriod)
  {
    sensorTimer = millis();
    querySensor();
  }
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
    server.on("/reading",handleLastReading);
    server.begin();
  }
  else
    Serial.println("Webserver start failure.");
}

// Shut down webserver, connect to SSID from settings
void enterNormalState()
{
  inConfigState = false;
  updateLEDState(); // Give feedback immediately
  WiFi.softAPdisconnect(true);

  Serial.print("Attempting to connect to wifi with: ");
  Serial.println(wifiSSID);
  WiFi.begin(wifiSSID, wifiPassword);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected ");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  inWifiConnectedState = true;
}

// Display main login page
void handleRoot() {
  String content = "<html><body><form action='settings' method='POST'>Please enter settings:<br>";
  content += "Wifi: <input type='text' name='wifissid' value ="; 
  content += wifiSSID;
  content += "><br>";
  content += "Wifi Password: <input type='text' name='wifipassword' value =";
  content += wifiPassword;
  content += "><br>";
  content += "Device Name: <input type='text' name='devicename' value =";
  content += deviceName;
  content += "><br>";
  content += "SMTP server: <input type='text' name='smtpserver' value =";
  content += smtpServer;
  content += "><br>";
  content += "SMTP account: <input type='text' name='smtpaccount' value=";
  content += smtpAccount;
  content += "><br>";
  content += "SMTP password: <input type='text' name='smtppassword' value =";
  content += smtpPassword;
  content += "><br>";
  content += "Notification Email address: <input type='text' name='notificationemail' value =";
  content += notificationEmail;
  content += "><br>";
  content += "Notification CO2 Threshold (PPM): <input type='text' name='notificationco2ppm' value=";
  content += notificationCo2PPM;
  content += "><br>";
  content += "Notification Frequency in minutes: <input type='text' name='notificationfrequencyminutes' value=";
  content += notificationFrequencyMinutes;
  content += "><br>";
  content += "<input type='submit' value='Submit'></form><br>";
  server.send(200, "text/html", content);
}

void handleLastReading()
{
  String content = "<html><body>Last Measurements:<br>";
  content += "CO2 Equivalent (PPM):";
  content += lastCo2Read;
  content += "<br>TVOC (PPB): ";
  content += lastTvocRead;
  content += "</body></html>";
  server.send(200, "text/html", content);
}

void handleSettings()
{
  Serial.println("Got Settings");
  String ssid = server.arg("wifissid");
  String password = server.arg("wifipassword");

  // Parse settings
  deviceName = server.arg("devicename");
  smtpServer = server.arg("smtpserver");
  smtpAccount = server.arg("smtpaccount");
  smtpPassword = server.arg("smtppassword");
  notificationEmail = server.arg("notificationemail");
  notificationCo2PPM = server.arg("notificationco2ppm").toInt();
  notificationFrequencyMinutes = server.arg("notificationfrequencyminutes").toInt();
  ssid.toCharArray(wifiSSID, SSID_MAX_LENGTH);
  password.toCharArray(wifiPassword, WIFI_PASSWORD_MAX_LENGTH);

  //TODO: Fix this horrible string concatentation hack
  String settingsString;
  settingsString += CONFIG_WIFI_SSID;
  settingsString += "=";
  settingsString += wifiSSID;
  settingsString += "\r\n";

  settingsString += CONFIG_WIFI_PASSWORD;
  settingsString += "=";
  settingsString += wifiPassword;
  settingsString += "\r\n";

  settingsString += CONFIG_DEVICE_NAME;
  settingsString += "=";
  settingsString += deviceName;
  settingsString += "\r\n";

  settingsString += CONFIG_SMTP_SERVER;
  settingsString += "=";
  settingsString += smtpServer;
  settingsString += "\r\n";

  settingsString += CONFIG_SMTP_ACCOUNT;
  settingsString += "=";
  settingsString += smtpAccount;
  settingsString += "\r\n";

  settingsString += CONFIG_SMTP_PASSWORD;
  settingsString += "=";
  settingsString += smtpPassword;
  settingsString += "\r\n";

  settingsString += CONFIG_NOTIFICATION_EMAIL;
  settingsString += "=";
  settingsString += notificationEmail;
  settingsString += "\r\n";

  settingsString += CONFIG_NOTIFICATION_CO2_PPM;
  settingsString += "=";
  settingsString += notificationCo2PPM;
  settingsString += "\r\n";

  settingsString += CONFIG_NOTIFICATION_FREQUENCY_MINUTES;
  settingsString += "=";
  settingsString += notificationFrequencyMinutes;
  settingsString += "\r\n";

  Serial.println(settingsString);

  File configFile = SPIFFS.open(DEFAULT_CONFIG_FILE_NAME, "w+");
  configFile.print(settingsString);
  configFile.close();

  server.send(200, "text/html", "<html><body><h1>Thanks for the settings!</h1><br><a href='/'>Enter Settings Again</a></body></html>");
}


