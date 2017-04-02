/*
  Simple sketch to see what the Arduino is capable of.

  This sketch interfaces with:
  -A RN42 bluetooth modem
  -LEDs to illustrate gas sensor status (Green, Yellow, Red)
  -Simple buzzer to alarm when the concentration gets to a critical threshold

  The sensor is designed to:
  1) Maintain a bluetooth connection to the smoke sensor, keep this connection in SPP data mode
  2) Read the periodic reports from the sensor, based on some defined smoke thresholds update the status LEDs (green, yellow, red)

  TODO: improve connection failure/success detection
  TODO: Better way to automate connection on startup?
  TODO: How to handle not finding a pair?
  TODO: How to verify that the connection is still alive?
*/
#include <SoftwareSerial.h>

// Pins used for software serial
int bluetoothTx = 13;  // TX-O pin of bluetooth mate
int bluetoothRx = 12;  // RX-I pin of bluetooth mate

// Pins used for the LEDs
int redLedPin = 9;
int yellowLedPin = 8;
int greenLedPin = 7;

SoftwareSerial bluetooth(bluetoothTx, bluetoothRx); // Serial Connection to the bluetooth modem

void setup()
{
  pinMode(redLedPin, OUTPUT);
  pinMode(yellowLedPin, OUTPUT);
  pinMode(greenLedPin, OUTPUT);

  delay(500);
  Serial.begin(9600);  // Begin the serial monitor at 9600bps

  bluetooth.begin(115200);  // The Bluetooth Mate defaults to 115200bps
  bluetooth.print("$$$");  // Enter command mode
  delay(100);  // Short delay, wait for the Mate to send back CMD
  bluetooth.println("U,9600,N");  // Temporarily Change the baudrate to 9600, no parity
  // 115200 can be too fast at times for NewSoftSerial to relay the data reliably
  bluetooth.begin(9600);  // Start bluetooth serial at 9600
  delay(100);
}

void loop()
{
  // TODO: If we havent gotten any measurements from the sensor for a 5 minutes, then set a warning that we have lost connection. Maybe flash all the LEDs?
  if (bluetooth.available()) // If the bluetooth sent any characters
  {
    float receivedMeasurement = bluetooth.readStringUntil(' ').toFloat();
    Serial.println(receivedMeasurement);
    UpdateLEDs(receivedMeasurement);
  }
  if (Serial.available()) // If stuff was typed in the serial monitor
  {
    // Send any characters the Serial monitor prints to the bluetooth
    bluetooth.print((char)Serial.read());
  }
}

void UpdateLEDs(float voltageMeasurement)
{
  // Start from a clean state. Turn everything off
  digitalWrite(greenLedPin, LOW);
  digitalWrite(redLedPin, LOW);
  digitalWrite(yellowLedPin, LOW);

  if (voltageMeasurement > 4.0) // Set Red LED and buzzer
  {
    digitalWrite(redLedPin, HIGH);
    // TODO: buzzer?
  }
  else if (voltageMeasurement > 3.0) // Set Red LED
  {
    digitalWrite(redLedPin, HIGH);
  }
  else if (voltageMeasurement > 2.0) // Set Yellow LED
  {
    digitalWrite(yellowLedPin, HIGH);
  }
  else // Set Green LED
  {
    digitalWrite(greenLedPin, HIGH);
  }
}

