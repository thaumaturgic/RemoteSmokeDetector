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

// Configuration Button pin to enter into configuration mode
int configButtonPin = 5;

// Status RGB LED pins
int ledPinRed = 4;
int ledPinBlue = 12;
int ledPinGreen = 13;

// Sensor mosfet pin used to turn sensor on/off when going out/in sleep mode
int pfetGatePin = 0;

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
int debounceTimer = 0;
#define BUTTON_DEBOUNCE_PERIOD_MS 500

// How often in MS to poll the air quality sensor
unsigned long sensorTimer = 0;
#define SENSOR_TIMER_PERIOD_MS 500

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
  digitalWrite(pfetGatePin, HIGH);

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

  enterNormalState();
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

  // TODO: Check if we should sleep
}

/////////////////// State functions ///////////////////

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
  for (i = 0; (i < 10) && (WiFi.status() != WL_CONNECTED); i++)
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
    adjustedSmtpAccount = "<" + smtpAccount + ">";

  adjustedNotificationEmail = "<" + notificationEmail + ">";

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
//////////TIME STUFF

unsigned int localPort = 2390;
IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "time.nist.gov";
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
WiFiUDP udp;

unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

void getNetworkTime()
{
  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());

  WiFi.hostByName(ntpServerName, timeServerIP);
  sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  delay(1000);
  int cb = udp.parsePacket();

  if (!cb) {
    Serial.println("no packet yet");
  }
  else {
    Serial.print("packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    Serial.print("Seconds since Jan 1 1900 = " );
    Serial.println(secsSince1900);

    // now convert NTP time into everyday time:
    Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    // print Unix time:
    Serial.println(epoch);

    // print the hour, minute and second:
    Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
    Serial.print((epoch  % 86400L) / 3600); // print the hour (86400 equals secs per day)
    Serial.print(':');
    if ( ((epoch % 3600) / 60) < 10 ) {
      // In the first 10 minutes of each hour, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.print((epoch  % 3600) / 60); // print the minute (3600 equals secs per minute)
    Serial.print(':');
    if ( (epoch % 60) < 10 ) {
      // In the first 10 seconds of each minute, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.println(epoch % 60); // print the second
  }
}

