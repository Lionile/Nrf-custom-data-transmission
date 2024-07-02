#include "Inkplate.h"  //Include Inkplate library to the sketch
//#include "image.h"
Inkplate display(INKPLATE_3BIT);  // Create object on Inkplate library and set library to work in gray mode (3-bit)
                                  // Other option is BW mode, which is demonstrated in next example
                                  // "Inkplate_basic_BW"

#define DELAY_MS 5000

const int WAKE_PIN = 14;

volatile unsigned long wakeStart = 0;
const int SLEEP_TIME = 1500; // how long it takes for the esp to go to sleep after receiving an interrupt

const int flagBytesCount = 5; 
byte transmitBytesFlag = 0x01;   // [0] - 0x01, [1,2] - byte count,
byte transmitImageFlag = 0x02;   // [0] - 0x02, [1,2] - image height, [3,4] - image width
byte transmitStringFlag = 0x03;  // [0] - 0x03, [1,...,4] - string length
byte transmit3BitImageFlag = 0x04;
byte ackFlag = 0xFF;  // ack - {0xFF, 0xXX}, 0xXX - whatever the sent flag was
const String wakeMessage = "awake";

// TODO: currently, when the inkplate goes to sleep, it doesn't wake up fast enough to get the needed data

void setup() {
  Serial2.begin(1000000, SERIAL_8N1, 12, 13);  //rx, tx
  Serial.begin(2000000);
  display.begin();         // Init library (you should call this function ONLY ONCE)
  display.clearDisplay();  // Clear any data that may have been in (software) frame buffer.
                           //(NOTE! This does not clean image on screen, it only clears it in the frame buffer inside
                           // ESP32).
  
  /*display.drawImage(picture1, 100, 100, 500,
                    332);  // Arguments are: array variable name, start X, start Y,  size X, size Y
  display.display();
  delay(5000);*/

  Serial.println("\n" + wakeMessage);
  Serial2.print(wakeMessage); // to signal the nrf receiver
  Serial2.flush();
  Serial2.println(); // to signal that it is awake

  esp_sleep_enable_ext0_wakeup(GPIO_NUM_14, 1); // GPIO_NUM_X needs to be the same as WAKE_PIN!!!
  //pinMode(WAKE_PIN, INPUT_PULLDOWN);
  //attachInterrupt(digitalPinToInterrupt(WAKE_PIN), wakeUp, HIGH);
  wakeStart = millis();
}

void loop() {
  if (Serial2.available()) {

    byte flag[flagBytesCount];
    Serial2.readBytes(flag, sizeof(flag));

    // Choose the next step depending on what type of message is transmitting
    if (flag[0] == transmitBytesFlag) {

      // read second, third, fourth and fifth byte as integer and call transmitBytes
      receiveBytes(((unsigned long)flag[1] << 24) | ((unsigned long)flag[2] << 16)
                   | ((unsigned long)flag[3] << 8) | (unsigned long)flag[4]);
      wakeStart = millis(); 

    } else if (flag[0] == transmit3BitImageFlag) {
      Serial.println("Receiving image3bit");
      display.clearDisplay();
      int height = (flag[1] << 8) | flag[2];
      int width = (flag[3] << 8) | flag[4];
      Serial.println("3bit- Height: " + String(height) + ", Width: " + String(width));
      receiveImage3Bit((flag[1] << 8) | flag[2], (flag[3] << 8) | flag[4]);
      //(flag[1] << 8) | flag[2] - read second and third byte as integer
      //(flag[3] << 8) | flag[4] - read fourth and fifth byte as integer
      wakeStart = millis(); 

    } else if (flag[0] == transmitStringFlag) {

      // read second, third, fourth and fifth byte as integer and call transmitString
      receiveString(((unsigned long)flag[1] << 24) | ((unsigned long)flag[2] << 16)
                    | ((unsigned long)flag[3] << 8) | (unsigned long)flag[4]);
      wakeStart = millis(); 
    }
  }
  if(millis() - wakeStart > SLEEP_TIME)
  {
    Serial.println("going to sleep!");
    esp_deep_sleep_start();
  }
}

void wakeUp(){
  // calling the function
  Serial.println("woke up!");
  wakeStart = millis();
  
}

void receiveBytes(unsigned long count) {
  Serial.println("Receiving " + String(count) + " bytes");
  while (count > 0) {
    int bytesToReceive = 0;
    // Take at most a 32 byte chunk
    if (count > 32)
      bytesToReceive = 32;
    else
      bytesToReceive = count;

    byte data[bytesToReceive];
    
    unsigned long startTime = millis();
    while (Serial2.available() < bytesToReceive){
      if (millis() - startTime >= 1000) {
        Serial.println(F("Transmission timed out"));
        return; // or any other action you want to take when canceling the transmission
      }
    }

    Serial2.readBytes(data, sizeof(data));
    printAsHex(data, sizeof(data));
    count -= bytesToReceive;
  }
}

void receiveImage3Bit(int height, int width) {
  unsigned long count = (unsigned long)height * (unsigned long)width / 2;
  unsigned long total = count;
  // receive data for the image,
  // store it in the 3bit buffer
  while (count > 0) {
    int bytesToReceive = 0;
    // Take at most a 32 byte chunk
    if (count > 32)
      bytesToReceive = 32;
    else
      bytesToReceive = count;

    byte data[bytesToReceive];
    unsigned long startTime = millis();
    while (Serial2.available() < bytesToReceive){
      if (millis() - startTime >= 1000) {
        Serial.println(F("Transmission timed out"));
        return; // or any other action you want to take when canceling the transmission
      }
    }

    Serial2.readBytes(data, sizeof(data));
    // for each pixel received, save it to the buffer

    for (int i = 0; i < bytesToReceive; i++) {
      int bufferIndex = total - count + i;
      int x = (bufferIndex * 2) % width;
      int y = (bufferIndex * 2) / width;
      byte pixel1 = data[i] >> 4;
      display.drawPixel(x, y, pixel1);  //x, y, pixel color

      x = (bufferIndex * 2 + 1) % width;
      y = (bufferIndex * 2 + 1) / width;
      byte pixel2 = data[i] & 0x0F;
      display.drawPixel(x, y, pixel2);
    }

    //printAsHex(data, sizeof(data));
    count -= bytesToReceive;
  }

  display.display();
  Serial.println("Image 3bit received!");
}

void receiveString(unsigned long length) {
  // TODO: implement
}

void drawRandomRectangles() {
  display.clearDisplay();
  for (int i = 0; i < 50; i++) {
    display.drawRect(random(0, 799), random(0, 599), 100, 150, random(0, 7));
  }
  displayCurrentAction("Drawing many rectangles in random colors");
  display.display();
  delay(DELAY_MS);
}

void drawRandomLines() {
  display.clearDisplay();
  for (int i = 0; i < 100; i++) {
    display.drawLine(random(0, 799), random(0, 599), random(0, 799), random(0, 599), random(0, 7));
  }
  displayCurrentAction("Drawing 50 random lines in random colors");
  display.display();
  delay(DELAY_MS);
}

void displayCurrentAction(String text) {
  display.setTextSize(2);
  display.setCursor(2, 580);
  display.print(text);
}

void printAsHex(byte data[], int arrSize) {
  for (int i = 0; i < arrSize; i++) {
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}