/*
  Simple sketch to see what the Arduino is capable of.

  This sketch interfaces with:
  -A RN42 bluetooth modem
  -An MQ-2 Gas/Smoke detector via Analog pin 0

  The sensor is designed to:
  1) Maintain a bluetooth connection to receiver, keep this connection in SPP data mode
  2) Periodicaly poll the ADC and report the voltage value over the bluetooth link to the master

  I'll worry about battery power optimization later
*/
#include <SoftwareSerial.h>

int bluetoothTx = 13;  // TX-O pin of bluetooth mate
int bluetoothRx = 12;  // RX-I pin of bluetooth mate

SoftwareSerial bluetooth(bluetoothTx, bluetoothRx); // Serial Connection to the bluetooth modem

void setup()
{
  delay(3000);
  
  Serial.begin(9600);  // Begin the serial monitor at 9600bps

  bluetooth.begin(115200);  // The Bluetooth Mate defaults to 115200bps
  bluetooth.print("$");  // Print three times individually
  bluetooth.print("$");
  bluetooth.print("$");  // Enter command mode
  delay(100);  // Short delay, wait for the Mate to send back CMD
  bluetooth.println("U,9600,N");  // Temporarily Change the baudrate to 9600, no parity
  // 115200 can be too fast at times for NewSoftSerial to relay the data reliably
  bluetooth.begin(9600);  // Start bluetooth serial at 9600
  delay(100);
  bluetooth.print("$$$");  // Command mode
  delay(100);
  bluetooth.println("C");  // Tell it to connect
  delay(5000); 
  bluetooth.println("---"); // Exit command mode
}

void loop()
{
  // Issue command mode: $$$
  // Issue search command: I
  // Wait 11 seconds
  // Issue connect command C (how do we know it succeeded?)
  // Better way to automate connection on startup?
  // How to handle not finding a pair?
  // How to verify that the connection is still alive?

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

  float averageSensorValue = sensorValue / ANALOG_SAMPLES; 
  float sensorVoltage = averageSensorValue * 5.0 / 1023.0; // 10 bit Analog precision @ 5 volts;
  
  // Use the raw voltage / ADC value compared to an absolute threshold

  Serial.print(averageSensorValue);
  Serial.print(" ");
  Serial.print(sensorVoltage);
  Serial.println();
  delay(500);

  bluetooth.print(sensorVoltage);

  // and loop forever and ever!
}

