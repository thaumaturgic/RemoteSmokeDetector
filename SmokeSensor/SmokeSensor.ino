/*
  Simple sketch to see what the Arduino is capable of.

  This sketch interfaces with:
  -A RN42 bluetooth modem
  -An MQ-2 Gas/Smoke detector via Analog pin 0

  The sensor is designed to:
  1) Maintain a bluetooth connection to receiver, keep this connection in SPP data mode
  2) Periodicaly poll the ADC and report the voltage value over the bluetooth link to the master

  TODO: I'll worry about battery power optimization later / look at SY command (lowers transmit power) / SW command to enable 'low power sniff mode'
*/
#include <SoftwareSerial.h>

int bluetoothTx = 13;  // TX-O pin of bluetooth mate
int bluetoothRx = 12;  // RX-I pin of bluetooth mate

const unsigned long CONNECTION_STATUS_QUERY_PERIOD_MILLISECONDS = 60000; // 1 minute
unsigned long nextConnectionQueryMilliseconds = CONNECTION_STATUS_QUERY_PERIOD_MILLISECONDS; // Start out with first query 1 minute after boot

const unsigned long SMOKE_MEASUREMENT_DELAY_MILLISECONDS = 500;
unsigned long nextSmokeMeasurementMilliseconds = SMOKE_MEASUREMENT_DELAY_MILLISECONDS;

bool isBluetoothConnected = false;

SoftwareSerial bluetooth(bluetoothTx, bluetoothRx); // Serial Connection to the bluetooth modem

void setup()
{
  delay(1000);

  Serial.begin(9600);  // Begin the serial monitor at 9600bps

  bluetooth.begin(115200);  // The Bluetooth Mate defaults to 115200bps
  bluetooth.print("$$$");  // Enter command mode
  delay(1000);  // Short delay, wait for the Mate to send back CMD
  bluetooth.println("U,9600,N");  // Temporarily Change the baudrate to 9600, no parity
  // 115200 can be too fast at times for NewSoftSerial to relay the data reliably
  bluetooth.begin(9600);  // Start bluetooth serial at 9600
  delay(100);

  AttemptBluetoothConnect();
}

void loop()
{
  unsigned long currentMilliseconds = millis();

  // Every minute check if the bluetooth connection is alive. Reconnect if needed
  if (currentMilliseconds >= nextConnectionQueryMilliseconds)
  {
    bool isBluetoothConnected = IsBlueToothConnected(true);
    Serial.print("BlueTooth Status ");
    Serial.println(isBluetoothConnected);
    if (!isBluetoothConnected)
    {
      Serial.println("Trying to reconnect");
      AttemptBluetoothConnect();
    }

    // Query again in a minute // TODO: handle milis() rollover
    nextConnectionQueryMilliseconds += CONNECTION_STATUS_QUERY_PERIOD_MILLISECONDS;
  }

  // If the bluetooth sent any characters...
  while (bluetooth.available())
  {
    // ...print them to the serial monitor
    Serial.print((char)bluetooth.read());
  }
  // If stuff was typed in the serial monitor...
  while (Serial.available())
  {
    // ...print to the bluetooth connection
    bluetooth.print((char)Serial.read());
  }

  //////////////////////////////////////////////////////////////////////////////////
  // Only poll the smoke sensor every so often. Do things this way instead of using delay() to avoid messing with the bluetooth serial connection
  if (currentMilliseconds < nextSmokeMeasurementMilliseconds)
  {
    return;
  }

  // Set the next measurement time in the future. //TODO: Handle rollover
  nextSmokeMeasurementMilliseconds = currentMilliseconds + SMOKE_MEASUREMENT_DELAY_MILLISECONDS;

  float sensorValue = 0;
  const int ANALOG_SAMPLES = 10.0;

  for (int i = 0; i < ANALOG_SAMPLES; i++)
  {
    sensorValue += analogRead(A0);
  }

  // Use the raw voltage / ADC value. We dont particularly care about calculating an exact PPM from the sensor
  float averageSensorValue = sensorValue / ANALOG_SAMPLES;
  float sensorVoltage = averageSensorValue * 5.0 / 1023.0; // 10 bit Analog precision @ 5 volts;

  Serial.print(sensorVoltage);
  Serial.print(" ");
  Serial.print(averageSensorValue);
  Serial.print(" ");
  Serial.println();

  if (isBluetoothConnected)
  {
    bluetooth.print(sensorVoltage);
    bluetooth.print(" ");
  }
}

bool AttemptBluetoothConnect(void)
{
  const int connectedResponseLength = 6;

  // There needs to be a delay of at least 1 second before and after the $$$ sequence before the RN42 bluetooth modem will accept a command in command mode
  delay(1100);
  bluetooth.print("$$$");  // Command mode
  delay(1100);
  bluetooth.println("C");  // Tell it to connect
  delay(6000); // Wait an arbitrary 6 seconds for it to connect.
  bool bluetoothConnected = IsBlueToothConnected(false);
  bluetooth.println("---");
  delay(20);

  return bluetoothConnected;
}

// bool enterCommandMode : Should we put the modem in command mode to query or is it already in command mode?
bool IsBlueToothConnected(bool enterCommandMode)
{
  // Enter command mode if needed
  if (enterCommandMode)
  {
    delay(1100);
    bluetooth.print("$$$");
    delay(1100);
  }

  // flush any pending data, we want to make sure we get the next char from the GK command correctly
  while (bluetooth.available())
  {
    Serial.print((char)bluetooth.read());
  }

  // Query connection status
  bluetooth.println("GK");
  delay(50);

  // First char of the result tells if the bluetooth connection is active or not
  char result = (char)bluetooth.read();
  Serial.print(result); // just to be complete

  // Flush the rest of the stuff
  while (bluetooth.available())
  {
    Serial.print((char)bluetooth.read());
  }

  // If we entered command mode, then be sure to exit it
  if (enterCommandMode)
  {
    bluetooth.println("---");
    delay(20);
  }

  isBluetoothConnected = result;
  return result == '1' ? true : false;
}

