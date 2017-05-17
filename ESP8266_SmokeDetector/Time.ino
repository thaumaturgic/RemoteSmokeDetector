// Shamelessly stolen from ESP8266 core time example
// Used to determine what time it is and if we should be in sleep mode or not

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

bool getNetworkTime()
{
  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());

  WiFi.hostByName(ntpServerName, timeServerIP);
  sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  delay(2000);
  int cb = 0;

  int i = 0;
  while (cb == 0 && i < 10)
  {
    delay(1000);
    cb = udp.parsePacket();
    i++;
    Serial.print(".");
  }

  if (!cb) {
    Serial.println(" no packet.");
    return false;
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
    currentGMTHour = (epoch  % 86400L) / 3600; // Set Global. This should probably return a struct but oh well.
    Serial.print(currentGMTHour); // print the hour (86400 equals secs per day)
    Serial.print(':');
    currentGMTMinute = ((epoch % 3600) / 60);
    if ( currentGMTMinute < 10 ) {
      // In the first 10 minutes of each hour, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.print(currentGMTMinute); // print the minute (3600 equals secs per minute)
    Serial.print(':');
    if ( (epoch % 60) < 10 ) {
      // In the first 10 seconds of each minute, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.println(epoch % 60); // print the second

    return true;
  }
}

bool inOperatingTimespan(int currentGMTHour)
{
  int startHourGMT = (startHour + 7) % 24; // Yes im aware this is hardcoded to PST with daylight savings time...
  int stopHourGMT = (stopHour + 7) % 24;

  if (startHourGMT < stopHourGMT) // Window that doesnt wrap day boundary
  {
    // Are we in the run window?
    if (currentGMTHour >= startHourGMT && currentGMTHour < stopHourGMT)
      return true; // TODO: calculate when we should START sleeping
  }
  else if (stopHourGMT < startHourGMT) // Window that does wrap day boundary
  {
    if (currentGMTHour >= startHourGMT || currentGMTHour < stopHourGMT)
      return true;
  }
  else if (stopHourGMT == startHourGMT)
    return true;

  return false;
}

int minutesUntilEndOfOperatingTimespan(int currentGMTHour, int currentGMTMinute)
{
  int startHourGMT = (startHour + 7) % 24; // Yes im aware this is hardcoded to PST with daylight savings time...
  int stopHourGMT = (stopHour + 7) % 24;

  if (startHourGMT < stopHourGMT) // Window that doesnt wrap day boundary
  {
    int hoursRemaining = stopHourGMT - currentGMTHour;
    int minutesRemaining = 60 - currentGMTMinute;
    return (60 * (hoursRemaining - 1)) + minutesRemaining;
  }
  else if (stopHourGMT < startHourGMT) // Window that does wrap day boundary
  {
    int hoursRemaining = (24 - currentGMTHour) + startHourGMT;
    int minutesRemaining = 60 - currentGMTMinute;
    return (60 * (hoursRemaining - 1)) + minutesRemaining;
  }
}

int minutesUntilStartOfOperatingTimespan(int currentGMTHour, int currentGMTMinute)
{
  int startHourGMT = (startHour + 7) % 24; // Yes im aware this is hardcoded to PST with daylight savings time...
  int stopHourGMT = (stopHour + 7) % 24;

  int hoursRemaining;
  if (currentGMTHour < startHourGMT)
    hoursRemaining = startHourGMT - currentGMTHour;
  else
    hoursRemaining = (24 - currentGMTHour) + startHourGMT;

  int minutesRemaining = 60 - currentGMTMinute;
  return (60 * (hoursRemaining - 1)) + minutesRemaining;
}

