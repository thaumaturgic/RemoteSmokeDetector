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
}

void loop()
{
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


  String test = "1.5";
  Serial.println(test);
  float convertedString = test.toFloat();
  Serial.println(convertedString);

  if(convertedString > 1)
  {
    Serial.println("Greater than 1");
  }

  
 
}

