#include <SPI.h>
#include <RF24.h>
#include <avr/sleep.h>

const int ESP_WAKE_PIN = 3;

RF24 radio(7, 8); // CE, CSN

const uint8_t address[] = "00050";
const unsigned int flagBytesCount = 5;
const unsigned int payloadSize = 30; // need 2 bytes free for payloadCount
byte transmitBytesFlag = 0x01; // [0] - 0x01, [1,2] - byte count,
byte transmitImageFlag = 0x02; // [0] - 0x02, [1,2] - image height, [3,4] - image width
byte transmitStringFlag = 0x03; // [0] - 0x03, [1,...,4] - string length
byte transmit3BitImageFlag = 0x04;
byte ackFlag = 0xFF; // ack - {0xFF, 0xXX}, 0xXX - whatever the sent flag was

byte ackMessage[flagBytesCount];  // [0] - ackFlag, [1,...] - packet number
                                  // [0] - ackFlag, [1] - received flag (for first flag message)

void receiveInterrupt();
void wakeEsp();
void receiveImage(int height, int width);
void receiveImage3Bit(int height, int width);
void receiveBytes(unsigned long count);
void receiveString(unsigned long length);
void printAsHex(byte data[], int arrSize);

void setup() {
  ackMessage[0] = ackFlag;

  pinMode(ESP_WAKE_PIN, OUTPUT);
  digitalWrite(ESP_WAKE_PIN, LOW);

  Serial.begin(1000000);

  radio.begin();
  radio.maskIRQ(false, false, true); // interrupt - (tx_ok, tx_fail, rx_ready)
  radio.setPALevel(RF24_PA_LOW);
  radio.enableDynamicPayloads();
  radio.enableDynamicAck();
  radio.setChannel(85);
  radio.openWritingPipe(address);
  radio.openReadingPipe(1, address);  // using pipe 1
  radio.startListening(); // put radio in RX mode

  // go to sleep
  attachInterrupt(digitalPinToInterrupt(2), receiveInterrupt, HIGH);
  sleep_enable();
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
}



void loop() {
  if (radio.available()) {
    byte flag[flagBytesCount];
    radio.read(&flag, sizeof(flag));
    if(flag[0] == transmitBytesFlag){

      Serial.write(flag, sizeof(flag));
      // read second, third, fourth and fifth byte as integer and call receiveBytes
      receiveBytes(((unsigned long)flag[1] << 24) | ((unsigned long)flag[2] << 16)
                | ((unsigned long)flag[3] << 8) | (unsigned long)flag[4]);
    }
    else if(flag[0] == transmitImageFlag){
      
      Serial.write(flag, sizeof(flag));
      receiveImage((flag[1] << 8) | flag[2], (flag[3] << 8) | flag[4]);
      //(flag[1] << 8) | flag[2] - read second and third byte as integer
      //(flag[3] << 8) | flag[4] - read fourth and fifth byte as integer
    }
    else if(flag[0] == transmit3BitImageFlag){
      
      Serial.write(flag, sizeof(flag));
      receiveImage3Bit((flag[1] << 8) | flag[2], (flag[3] << 8) | flag[4]);
      //(flag[1] << 8) | flag[2] - read second and third byte as integer
      //(flag[3] << 8) | flag[4] - read fourth and fifth byte as integer
    }
    else if(flag[0] == transmitStringFlag){
      
      Serial.write(flag, sizeof(flag));
      // read second, third, fourth and fifth byte as integer and call receiveString
      receiveString(((unsigned long)flag[1] << 24) | ((unsigned long)flag[2] << 16)
                | ((unsigned long)flag[3] << 8) | (unsigned long)flag[4]);
    }

    // put to sleep
    attachInterrupt(digitalPinToInterrupt(2), receiveInterrupt, HIGH);
    sleep_enable();
  }
}



void receiveInterrupt(){
  // wake from sleep
  detachInterrupt(digitalPinToInterrupt(2));
  sleep_disable();
}



void wakeEsp(){
  digitalWrite(ESP_WAKE_PIN, HIGH);
  delayMicroseconds(200);
  digitalWrite(ESP_WAKE_PIN, LOW);
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
  unsigned long payloadCount = 0; // current payload index

  // Keep receiving bytes until you get all of it
  // TODO: if it doesn't receive the next bytes in a certain ammount
  // of time cancel the sending and send back a signal (if ack works)
  while(count > 0){
    int bytesToReceive = 0;
    // Take at most *payloadSize* byte chunk
    if(count > payloadSize)
      bytesToReceive = payloadSize;
    else
      bytesToReceive = count;
    
    byte data[bytesToReceive + 2];
    while(!radio.available()); // wait until the packet comes

    radio.read(&data, sizeof(data));
    
    unsigned long receivedPayloadCount = ((unsigned long)data[bytesToReceive] << 8) | data[bytesToReceive + 1];
    ackMessage[1] = receivedPayloadCount >> 8;
    ackMessage[2] = receivedPayloadCount & 0xFF;
    
    // check if the packet was already received (if the previous ack failed and the transmitter resent the packet)
    if(receivedPayloadCount < payloadCount){
      Serial.println(F("Received old packet"));
      return; // TODO: instead of returning, send ack again, and continue to next iteration without changing count
    }
    else if(receivedPayloadCount > payloadCount){
      Serial.println(F("Received packet out of order"));
      return;
    }

    //printAsHex(data, sizeof(data));
    Serial.write(data, bytesToReceive);
    count -= bytesToReceive;

    // send ack to transmitter
    // TODO: try sending ack until it is received
    radio.stopListening();  // put in TX mode
    radio.writeFast(&ackMessage, sizeof(ackMessage));  // load response to TX FIFO
    bool report = radio.txStandBy(150);          // keep retrying for 150 ms
    radio.startListening();  // put back in RX mode

    if (report) {
      //Serial.println(F("Sent ack"));
    } else {
      //Serial.println(F("Ack failed."));  // failed to send response
    }

    payloadCount++;
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