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

// Configuration variables
char wifiSSID[SSID_MAX_LENGTH];
char wifiPassword[WIFI_PASSWORD_MAX_LENGTH];
String deviceName;
String smtpServer;
String smtpAccount;
String smtpPassword;
String notificationEmail;
int notificationCo2PPM;

// Configuration Button and status LED pins
int configButtonPin = 4;

int ledPinRed = 13;
int ledPinBlue = 12;
int ledPinGreen = 5;

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

bool notificationSent = false; // TODO: Make email notification better

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

  // Setup Debug port
  Serial.begin(115200);

  // Start File System, attempt to retrieve settings
  SPIFFS.begin();
  if (SPIFFS.exists(DEFAULT_CONFIG_FILE_NAME))
  {
    Serial.print("Using Config File: ");
    Serial.println(DEFAULT_CONFIG_FILE_NAME);
    File configFile = SPIFFS.open(DEFAULT_CONFIG_FILE_NAME, "r");
    if (!configFile)
    {
      Serial.println("But couldnt open it.."); //TODO: error handling
    }

    Serial.println("Using Settings: ");
    while (configFile.available())
    {
      String settingLine = configFile.readStringUntil('\n');
      parseSettingLine(settingLine);
    }
  }
  else
  {
    Serial.print("Couldnt Find Config File: ");
    Serial.println(DEFAULT_CONFIG_FILE_NAME);
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

  if (settingLine.startsWith("wifi_ssid"))
    settingString.toCharArray(wifiSSID, SSID_MAX_LENGTH);
  else if (settingLine.startsWith("wifi_password"))
    settingString.toCharArray(wifiPassword, WIFI_PASSWORD_MAX_LENGTH);
  else if (settingLine.startsWith("device_name"))
    deviceName = settingString;
  else if (settingLine.startsWith("smtp_server"))
    smtpServer = settingString;
  else if (settingLine.startsWith("smtp_account"))
    smtpAccount = settingString;
  else if (settingLine.startsWith("smtp_password"))
    smtpPassword = settingString;
  else if (settingLine.startsWith("notification_email"))
    notificationEmail = settingString;
  else if (settingLine.startsWith("notification_co2_ppm"))
    notificationCo2PPM = settingString.toInt();
  else
  {
    Serial.print("Unknown Setting: ");
    Serial.println(settingLine);
  }
}

bool sendNotificationEmail(String body)
{
  SendEmail e(smtpServer, 465, smtpAccount, smtpPassword, 5000, true);
  bool result = e.send("<" + smtpAccount + ">", "<" + notificationEmail + ">", deviceName, body); // Gmail requires <> around to/from addresses
  Serial.print("Email result " + result);
  Serial.println(result);

  // Send test email
  //SendEmail e("smtp.gmail.com", 465, "internetofscott@gmail.com", "M1TTEn$1", 5000, true);
  //bool result = e.send("<internetofscott@gmail.com>", "<scott.brust@gmail.com>", "ESP8266 subject", "Test message");
  //bool result = e.send("<internetofscott@gmail.com>", "<7074108156@vtext.com>", "ESP8266 subject2", "Test message2");
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

    // TODO: Better notification logic
    if (co2PPM > notificationCo2PPM)
    {
      inThresholdExceededState = true;

      if (!notificationSent)
      {
        String notificationText = "CO2 threshold hit: ";
        notificationText.concat(co2PPM);
        notificationText.concat(" TVOC ppm: ");
        notificationText.concat(tvocPPB);

        sendNotificationEmail(notificationText);
        notificationSent = true; // TODO: Only send one email per alarm. aka throttle somehow or whatever
      }
    }
    else
    {
      inThresholdExceededState = false;
    }
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
    digitalWrite(ledPinRed, LOW);
  }
  else
  {
    digitalWrite(ledPinRed, HIGH);
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
  else
  {
  // TODO: Make sure wifi is still connected  
  }

  // TODO: Dont send email if we are in config state
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
  WiFi.disconnect();
  bool apStartSuccess = WiFi.softAP("ScottsESP", "anniepassword");
  if (apStartSuccess)
  {
    Serial.print("Webserver start success: ");
    Serial.println(WiFi.softAPIP());
    server.on("/", handleRoot);
    server.on("/settings", handleSettings);
    server.begin();
  }
  else
    Serial.println("Webserver start failure.");
}

// Shut down webserver, connect to SSID from settings
void enterNormalState()
{
  inConfigState = false;
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
}

// TODO: expand these handlers to take input
//  String content = "<html><body><form action='/login' method='POST'>To log in, please use : admin/admin<br>";
//  content += "User:<input type='text' name='USERNAME' placeholder='user name'><br>";
//  content += "Password:<input type='password' name='PASSWORD' placeholder='password'><br>";
//  content += "<input type='submit' name='SUBMIT' value='Submit'></form>" + msg + "<br>";
//  content += "You also can go <a href='/inline'>here</a></body></html>";
//  server.send(200, "text/html", content);
void handleRoot() {
  String content = "<html><body><form action='settings' method='POST'>Please enter settings:<br>";
  content += "Wifi: <input type='text' name='wifissid'><br>";
  content += "Wifi Password: <input type='text' name='wifipassword'><br>";
  content += "Device Name: <input type='text' name='devicename'><br>";
  content += "SMTP server: <input type='text' name='smtpserver'><br>";
  content += "SMTP account: <input type='text' name='smtpaccount'><br>";
  content += "SMTP password: <input type='text' name='smtppassword'><br>";
  content += "Notification Email address: <input type='text' name='notificationemail'><br>";
  content += "Notification CO2 Threshold (PPM): <input type='text' name='notificationco2ppm' value='500'><br>";
  content += "<input type='submit' value='Submit'></form><br>";
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
  smtpPassword = server.arg("smptpassword");
  notificationEmail = server.arg("notificationemail");
  notificationCo2PPM = server.arg("notificationco2ppm").toInt();
  ssid.toCharArray(wifiSSID, SSID_MAX_LENGTH);
  password.toCharArray(wifiPassword, WIFI_PASSWORD_MAX_LENGTH);

  // Print settings to confirm
  Serial.println(wifiSSID);
  Serial.println(wifiPassword);
  Serial.println(deviceName);
  Serial.println(smtpServer);
  Serial.println(smtpAccount);
  Serial.println(smtpPassword);
  Serial.println(notificationEmail);
  Serial.println(notificationCo2PPM);

  //TODO: write these to file system

  server.send(200, "text/html", "<html><body><h1>Thanks for the settings!</h1><br><a href='/'>Enter Settings Again</a></body></html>");
}

