#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

RF24 radio(7, 8); // CE, CSN

const uint8_t address[] = "00050";
int flagBytesCount = 5;
byte transmitBytesFlag = 0x01; // [0] - 0x01, [1,2] - byte count,
byte transmitImageFlag = 0x02; // [0] - 0x02, [1,2] - image height, [3,4] - image width
byte transmitStringFlag = 0x03; // [0] - 0x03, [1,...,4] - string length
byte transmit3BitImageFlag = 0x04;
byte ackFlag = 0xFF; // ack - {0xFF, 0xXX}, 0xXX - whatever the sent flag was

void setup() {
  Serial.begin(1000000);

  radio.begin();
  radio.setDataRate(RF24_2MBPS);

  radio.openReadingPipe(1, address);
  radio.startListening(); // sets the nrf to receiving mode
}

void loop() {
  if (radio.available()) {
    byte flag[flagBytesCount];
    radio.read(&flag, sizeof(flag));
    if(flag[0] == transmitBytesFlag){
      // sendAck(data);
      Serial.write(flag, sizeof(flag));
      // read second, third, fourth and fifth byte as integer and call receiveBytes
      receiveBytes(((unsigned long)flag[1] << 24) | ((unsigned long)flag[2] << 16)
                | ((unsigned long)flag[3] << 8) | (unsigned long)flag[4]);
    }
    else if(flag[0] == transmitImageFlag){
      // sendAck(data);
      Serial.write(flag, sizeof(flag));
      receiveImage((flag[1] << 8) | flag[2], (flag[3] << 8) | flag[4]);
      //(flag[1] << 8) | flag[2] - read second and third byte as integer
      //(flag[3] << 8) | flag[4] - read fourth and fifth byte as integer
    }
    else if(flag[0] == transmit3BitImageFlag){
      // sendAck(data);
      Serial.write(flag, sizeof(flag));
      receiveImage3Bit((flag[1] << 8) | flag[2], (flag[3] << 8) | flag[4]);
      //(flag[1] << 8) | flag[2] - read second and third byte as integer
      //(flag[3] << 8) | flag[4] - read fourth and fifth byte as integer
    }
    else if(flag[0] == transmitStringFlag){
       // sendAck(data);
      Serial.write(flag, sizeof(flag));
      // read second, third, fourth and fifth byte as integer and call receiveString
      receiveString(((unsigned long)flag[1] << 24) | ((unsigned long)flag[2] << 16)
                | ((unsigned long)flag[3] << 8) | (unsigned long)flag[4]);
    }
  }
}

void sendAck(byte data[]){
  // can't get manual ack to work
}

void receiveImage(int height, int width){
  for(int i = 0; i < height; i++){
    int count = width;
    receiveBytes(count);
  }
}

void receiveImage3Bit(int height, int width){
  unsigned long count = (unsigned long)height * (unsigned long)width /2;
  receiveBytes(count);
}

void receiveBytes(unsigned long count){
  // Keep receiving bytes until you get all of it
  // TODO: if it doesn't receive the next bytes in a certain ammount
  // of time cancel the sending and send back a signal (if ack works)
  while(count > 0){
    int bytesToReceive = 0;
    // Take at most a 32 byte chunk
    if(count > 32)
      bytesToReceive = 32;
    else
      bytesToReceive = count;
    
    byte data[bytesToReceive];
    while(!radio.available()); // wait until the packet comes

    radio.read(&data, sizeof(data));

    //printAsHex(data, sizeof(data));
    Serial.write(data, sizeof(data));
    count -= bytesToReceive;
  }
}

void receiveString(unsigned long length){
  receiveBytes(length);
}


void printAsHex(byte data[], int arrSize){
  for (int i = 0; i < arrSize; i++) {
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}