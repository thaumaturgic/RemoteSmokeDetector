/*
  Simple sketch to see what the Arduino is capable of.

  This sketch interfaces with:
  -A RN42 bluetooth modem
  -An MQ-2 Gas/Smoke detector via Analog pin 0

  The sensor is designed to:
  1) Maintain a bluetooth connection to receiver, keep this connection in SPP data mode
  2) Periodicaly poll the ADC and report the voltage value over the bluetooth link to the master

  TODO: I'll worry about battery power optimization later / look at SY command (lowers transmit power) / SW command to enable 'low power sniff mode'
  TODO: improve connection failure/success detection
  TODO: Better way to automate connection on startup?
  TODO: How to handle not finding a pair?
  TODO: How to verify that the connection is still alive?
*/
#include <SoftwareSerial.h>

int bluetoothTx = 13;  // TX-O pin of bluetooth mate
int bluetoothRx = 12;  // RX-I pin of bluetooth mate

SoftwareSerial bluetooth(bluetoothTx, bluetoothRx); // Serial Connection to the bluetooth modem

void setup()
{
  delay(1000);

  Serial.begin(9600);  // Begin the serial monitor at 9600bps

  bluetooth.begin(115200);  // The Bluetooth Mate defaults to 115200bps
  bluetooth.print("$$$");  // Enter command mode
  delay(100);  // Short delay, wait for the Mate to send back CMD
  bluetooth.println("U,9600,N");  // Temporarily Change the baudrate to 9600, no parity
  // 115200 can be too fast at times for NewSoftSerial to relay the data reliably
  bluetooth.begin(9600);  // Start bluetooth serial at 9600
  delay(100);

  AttemptBluetoothConnect();
}

const unsigned long CONNECTION_STATUS_QUERY_PERIOD_MILLISECONDS = 10000;//60000; // 1 minute
unsigned long nextConnectionQueryMilliseconds = CONNECTION_STATUS_QUERY_PERIOD_MILLISECONDS; // Start out with first query 1 minute after boot

void loop()
{
  // TODO: Validate that the bluetooth connection is available still. Reconnect if needed
  unsigned long currentMilliseconds = millis();
  if (currentMilliseconds >= nextConnectionQueryMilliseconds)
  {
    bool isBluetoothConnected = IsBlueToothConnected(true);
    Serial.print("BlueTooth Status ");
    Serial.println(isBluetoothConnected);
    if (!isBluetoothConnected)
    {
      // Attempt once just for now
      AttemptBluetoothConnect();
    }

    nextConnectionQueryMilliseconds += CONNECTION_STATUS_QUERY_PERIOD_MILLISECONDS; // Query again in a minute // TODO: handle milis() rollover
  }

  // Every minute check if the bluetooth connection is alive
  // If it isnt, then try to reconnect

  while (bluetooth.available()) // If the bluetooth sent any characters
  {
    // Send any characters the bluetooth prints to the serial monitor
    Serial.print((char)bluetooth.read());
  }
  while (Serial.available()) // If stuff was typed in the serial monitor
  {
    // Send any characters the Serial monitor prints to the bluetooth
    bluetooth.print((char)Serial.read());
  }

  //////////////////////////////////////////////////////////////////////////////////

  float sensorValue = 0;
  const int ANALOG_SAMPLES = 10.0;

  for (int i = 0; i < ANALOG_SAMPLES; i++)
  {
    sensorValue += analogRead(A0);
  }

  // Use the raw voltage / ADC value. We dont particularly care about calculating an exact PPM from the sensor
  float averageSensorValue = sensorValue / ANALOG_SAMPLES;
  float sensorVoltage = averageSensorValue * 5.0 / 1023.0; // 10 bit Analog precision @ 5 volts;

  //  Serial.print(averageSensorValue);
  //  Serial.print(" ");
  //  Serial.print(sensorVoltage);
  //  Serial.println();
  //  delay(200);
  //
  //  bluetooth.print(sensorVoltage);
  //  bluetooth.print(" ");
}

bool AttemptBluetoothConnect(void)
{
  const int connectedResponseLength = 6;

  bluetooth.print("$$$");  // Command mode
  delay(100);
  bluetooth.println("C");  // Tell it to connect
  delay(5000); // Wait an arbitrary 5 seconds for it to connect.
  
  bool bluetoothConnected = IsBlueToothConnected(false);
  bluetooth.println("---");

  return bluetoothConnected;
}

// bool enterCommandMode : Should we put the modem in command mode to query or is it already in command mode?
bool IsBlueToothConnected(bool enterCommandMode)
{
  // Enter command mode if needed
  if (enterCommandMode)
  {
    bluetooth.print("$$$");
    delay(100);
  }

  // flush any pending data, we want to make sure we get the next char from the GK command correctly
  while (bluetooth.available())
  {
    Serial.print((char)bluetooth.read());
  }

  // Query connection status
  bluetooth.println("GK");
  delay(100);

  
  // First char of the result tells if the bluetooth connection is active or not
  char result = bluetooth.read();

  // Flush the rest of the stuff
  while (bluetooth.available())
  {
    Serial.print((char)bluetooth.read());
  }

  // If we entered command mode, then be sure to exit it
  if (enterCommandMode)
  {
    bluetooth.println("---");
  }

  return result == '1' ? true : false;
}

