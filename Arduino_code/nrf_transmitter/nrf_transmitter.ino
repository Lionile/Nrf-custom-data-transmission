#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

RF24 radio(7, 8); // CE, CSN

const uint64_t address = 0xE8E8F0F0E1LL;
const int flagBytesCount = 5;
byte transmitBytesFlag = 0x01; // [0] - 0x01, [1,2] - byte count,
byte transmitImageFlag = 0x02; // [0] - 0x02, [1,2] - image height, [3,4] - image width
byte transmitStringFlag = 0x03; // after receiving string flag, read string until /n (ReadLine)
byte transmit3BitImageFlag = 0x04;
byte ackFlag = 0xFF; // ack - {0xFF, 0xXX}, 0xXX - whatever the sent flag was

byte transmitStringFlagMessage[flagBytesCount];

void setup() {
  transmitStringFlagMessage[0] = transmitStringFlag;

  Serial.begin(1000000);

  radio.begin();
  radio.setDataRate(RF24_2MBPS);
  radio.setRetries(5, 30); // 1500us delay between retries, 30 retries
  radio.openWritingPipe(address);
  radio.stopListening(); // sets the nrf to transmitting mode
}

void loop() {
  if (Serial.available()) {
    byte flag[flagBytesCount];
    Serial.readBytes(flag, sizeof(flag));
    // Choose the next step depending on what type of message is transmitting
    if(flag[0] == transmitBytesFlag){
      radio.write(flag, sizeof(flag));
      delay(5);//wait for ack
      transmitBytes(((unsigned long)flag[1] << 24) | ((unsigned long)flag[2] << 16)
                | ((unsigned long)flag[3] << 8) | (unsigned long)flag[4]);
      //(flag[1] << 8) | flag[2] - read second and third byte as integer
    }
    else if(flag[0] == transmitImageFlag){
      radio.write(flag, sizeof(flag));
      delay(5);
      int height = (flag[1] << 8) | flag[2];
      int width = (flag[3] << 8) | flag[4];
      transmitImage((flag[1] << 8) | flag[2], (flag[3] << 8) | flag[4]);
      //(flag[1] << 8) | flag[2] - read second and third byte as integer
      //(flag[3] << 8) | flag[4] - read fourth and fifth byte as integer
    }
    else if(flag[0] == transmit3BitImageFlag){
      radio.write(flag, sizeof(flag));
      delay(5);
      int height = (flag[1] << 8) | flag[2];
      int width = (flag[3] << 8) | flag[4];
      transmitImage3Bit((flag[1] << 8) | flag[2], (flag[3] << 8) | flag[4]);
      //(flag[1] << 8) | flag[2] - read second and third byte as integer
      //(flag[3] << 8) | flag[4] - read fourth and fifth byte as integer
    }
  }
  delay(2);
}

void transmitImage(int height, int width){
  for(int i = 0; i < height; i++){
    int count = width;
    transmitBytes(count);
  }
}

void transmitImage3Bit(int height, int width){
  unsigned long count = (unsigned long)height * (unsigned long)width /2;
  Serial.write(transmitStringFlagMessage, sizeof(transmitStringFlagMessage));
  Serial.println("count: " + String(count));
  transmitBytes(count);
}

void transmitBytes(unsigned long count){
  // Keep receiving bytes until you get all of it
  while(count > 0){
    int bytesToSend = 0;
    // Take at most a 32 byte chunk
    if(count > 32)
      bytesToSend = 32;
    else
      bytesToSend = count;

    byte data[bytesToSend];
    // TODO: only wait for a certain ammount of time (flush te buffer if exceeded maybe?)
    while (Serial.available() < bytesToSend); // wait until the whole packet comes

    Serial.readBytes(data, sizeof(data));
    /*Serial.write(transmitStringFlagMessage, sizeof(transmitStringFlagMessage));
    printAsHex(data, sizeof(data));*/
    radio.write(data, sizeof(data));
    count -= bytesToSend;
    byte ackFlagMessage[flagBytesCount];
    ackFlagMessage[0] = ackFlag;
    ackFlagMessage[1] = (byte)(count >> 24);
    ackFlagMessage[2] = (byte)(count >> 16);
    ackFlagMessage[3] = (byte)(count >> 8);
    ackFlagMessage[4] = (byte)(count & 0xFF);
    Serial.write(ackFlagMessage, sizeof(ackFlagMessage));
    delay(1); // wait for ack
  }
}


void printAsHex(byte data[], int arrSize){
  for (int i = 0; i < arrSize; i++) {
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}