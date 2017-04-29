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

// Bit Bang I2C pin definitions
const int SDAPin = 4;
const int SCLPin = 13;

void setup()
{
  // Setup I2C pins. NOTE: Clock stretching is REQUIRED for the AMS iAQ module. it uses it extensively
  Wire.begin(SDAPin, SCLPin);
  Wire.setClock(100000);
  Wire.setClockStretchLimit(15000);

  // Setup Debug port
  Serial.begin(115200);
  Serial.println();

  // Start File System, attempt to retrieve settings
  SPIFFS.begin();
  if (SPIFFS.exists(DEFAULT_CONFIG_FILE_NAME))
  {
    Serial.print("Using Config File: ");
    Serial.println(DEFAULT_CONFIG_FILE_NAME);
    File configFile = SPIFFS.open(DEFAULT_CONFIG_FILE_NAME, "r");
    if (!configFile)
    {
      Serial.println("But couldnt open it..");
    }
    //String configFileString = configFile.readString();
    //Serial.println(configFileString);

    Serial.println("Using Settings: ");
    while (configFile.available())
    {
      String settingLine = configFile.readStringUntil('\n');
      parseSettingLine(settingLine);
    }
  }
  else
  {
    //TODO: Write default file
    Serial.print("Couldnt Find Config File: ");
    Serial.println(DEFAULT_CONFIG_FILE_NAME);
  }

  // Connect to wifi
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

bool notificationSent = false;

void querySensor()
{
  // See datasheet for details on data format, etc
  Wire.requestFrom(0x5A, 9);
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
    unsigned int co2PPM = (co2MSB * 256) + co2LSB;
    unsigned int tvocPPB = (tvocMSB * 256) + tvocLSB;
    Serial.println("CO2 PPM: " + co2PPM);
    Serial.print(co2PPM);
    Serial.print(" TVOC PPB: ");
    Serial.println(tvocPPB);

    // TODO: Better notification logic
    if (co2PPM > notificationCo2PPM && !notificationSent)
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
    Serial.print("Warming up: 0x");
    Serial.println(statusByte, HEX);
  }

  //  while (Wire.available())
  //  {
  //    // TODO: parse output
  //    unsigned char c = Wire.read();
  //    Serial.print(c, HEX);
  //    Serial.print(" ");
  //  }
  //  Serial.println();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
unsigned long sensorTimer = 0;
int sensorTimerPeriod = 500; // How often in MS to poll the air quality sensor

void loop()
{
  // TODO: Make sure wifi is still connected

  // Query sensor every second
  if (millis() - sensorTimer >= sensorTimerPeriod)
  {
    sensorTimer = millis();
    querySensor();
  }

}

