#include <Arduino.h>

#include <SPI.h>
#include <RF24.h>
#include <avr/sleep.h>

const int ESP_WAKE_PIN = 3;

RF24 radio(7, 8); // CE, CSN

const uint8_t address[] = "00050";
const unsigned int flagBytesCount = 5;
const unsigned int payloadSize = 30; // need 2 bytes free for payloadCount
const byte transmitBytesFlag = 0x01; // [0] - 0x01, [1,2] - byte count,
const byte transmitImageFlag = 0x02; // [0] - 0x02, [1,2] - image height, [3,4] - image width
const byte transmitStringFlag = 0x03; // [0] - 0x03, [1,...,4] - string length
const byte transmit3BitImageFlag = 0x04;
const byte ackFlag = 0xFF; // acknoledgement
const byte nakFlag = 0x00; // negative acknoledgement


void receiveInterrupt();
void wakeEsp();
bool sendAck(unsigned long payloadCount);
void receiveImage(int height, int width);
void receiveImage3Bit(int height, int width);
void receiveBytes(unsigned long count);
void receiveString(unsigned long length);
void printAsHex(byte data[], int arrSize);


void setup() {

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
      
      wakeEsp();
      // delay(500);
      Serial.write(flag, sizeof(flag));
      // read second, third, fourth and fifth byte as integer and call receiveBytes
      receiveBytes(((unsigned long)flag[1] << 24) | ((unsigned long)flag[2] << 16)
                | ((unsigned long)flag[3] << 8) | (unsigned long)flag[4]);
    }
    else if(flag[0] == transmitImageFlag){
      
      wakeEsp();
      Serial.write(flag, sizeof(flag));
      receiveImage((flag[1] << 8) | flag[2], (flag[3] << 8) | flag[4]);
      //(flag[1] << 8) | flag[2] - read second and third byte as integer
      //(flag[3] << 8) | flag[4] - read fourth and fifth byte as integer
    }
    else if(flag[0] == transmit3BitImageFlag){
      
      wakeEsp();
      Serial.write(flag, sizeof(flag));
      receiveImage3Bit((flag[1] << 8) | flag[2], (flag[3] << 8) | flag[4]);
      //(flag[1] << 8) | flag[2] - read second and third byte as integer
      //(flag[3] << 8) | flag[4] - read fourth and fifth byte as integer
    }
    else if(flag[0] == transmitStringFlag){
      
      wakeEsp();
      Serial.write(flag, sizeof(flag));
      // read second, third, fourth and fifth byte as integer and call receiveString
      receiveString(((unsigned long)flag[1] << 24) | ((unsigned long)flag[2] << 16)
                | ((unsigned long)flag[3] << 8) | (unsigned long)flag[4]);
    }

    radio.flush_rx(); // clear the rx buffer
    radio.flush_tx(); // clear the tx buffer
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



bool sendAck(unsigned long payloadCount){
  bool report = false;
  byte ackMessage[flagBytesCount];

  ackMessage[0] = ackFlag;
  ackMessage[1] = payloadCount >> 8;
  ackMessage[2] = payloadCount & 0xFF;

  radio.stopListening();  // put in TX mode
  radio.writeFast(&ackMessage, sizeof(ackMessage));  // load response to TX FIFO
  report = radio.txStandBy(150);          // keep retrying for 150 ms
  radio.startListening();  // put back in RX mode

  return report;
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
  while(count > 0){
    int bytesToReceive = 0;
    // Take at most *payloadSize* byte chunk
    if(count > payloadSize)
      bytesToReceive = payloadSize;
    else
      bytesToReceive = count;
    
    byte data[bytesToReceive + 2]; // +2 for payloadCount

    // only wait for a certain ammount of time before canceling transmission
    unsigned long startTime = millis();
    while (!radio.available()) {
      if (millis() - startTime >= 1000) {
        //Serial.println(F("Transmission timed out"));
        return; // or any other action you want to take when canceling the transmission
      }
    }
    
    radio.read(&data, sizeof(data));
    
    unsigned long receivedPayloadCount = ((unsigned long)data[bytesToReceive] << 8) | data[bytesToReceive + 1];
    
    // check if the packet was already received (if the previous ack failed and the transmitter resent the packet)
    if(receivedPayloadCount < payloadCount){
      //Serial.println(F("Received old packet"));
      sendAck(receivedPayloadCount);
      continue;
    }
    else if(receivedPayloadCount > payloadCount){
      //Serial.println(F("Received packet out of order"));
      return;
    }


    Serial.write(data, bytesToReceive);
    count -= bytesToReceive;

    // send ack to transmitter
    bool report = sendAck(receivedPayloadCount);
    //Serial.println("Ack report: " + String(report));
    
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