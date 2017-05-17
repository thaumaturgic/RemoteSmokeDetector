

// Parse each line of the settings file to load settings into RAM
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
  else if (settingLine.startsWith(CONFIG_START_HOUR))
    startHour = settingString.toInt();
  else if (settingLine.startsWith(CONFIG_STOP_HOUR))
    stopHour = settingString.toInt();
  else
  {
    Serial.print("Unknown Setting: ");
    Serial.println(settingLine);
  }
}

/////////////////// CONFIGURATION WEBSERVER CALLBACKS ///////////////////

// Display main login page with current settings
// TODO: better string building...
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
  content += "Hour of day to start running (24h): <input type='text' name='starthour' value=";
  content += startHour;
  content += "><br>";
  content += "Hour of day to stop running (24h): <input type='text' name='stophour' value=";
  content += stopHour;
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

// TODO: better string handling... I hate Arduino String
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
  startHour = server.arg("starthour").toInt();
  stopHour = server.arg("stophour").toInt();

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

  settingsString += CONFIG_START_HOUR;
  settingsString += "=";
  settingsString += startHour;
  settingsString += "\r\n";

  settingsString += CONFIG_STOP_HOUR;
  settingsString += "=";
  settingsString += stopHour;
  settingsString += "\r\n";

  Serial.println(settingsString);

  File configFile = SPIFFS.open(DEFAULT_CONFIG_FILE_NAME, "w+");
  configFile.print(settingsString);
  configFile.close();

  server.send(200, "text/html", "<html><body><h1>Thanks for the settings!</h1><br><a href='/'>Enter Settings Again</a></body></html>");
}
