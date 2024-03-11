#include <SPI.h>
#include <RF24.h>

RF24 radio(7, 8); // CE, CSN

const uint8_t address[] = "00050";
const unsigned int flagBytesCount = 5;
const unsigned int payloadSize = 30; // need 2 bytes free for payloadCount
byte transmitBytesFlag = 0x01; // [0] - 0x01, [1,2] - byte count,
byte transmitImageFlag = 0x02; // [0] - 0x02, [1,2] - image height, [3,4] - image width
byte transmitStringFlag = 0x03; // [0] - 0x03, [1,...,4] - string length
byte transmit3BitImageFlag = 0x04;
byte ackFlag = 0xFF; // ack - {0xFF, 0xXX}, 0xXX - whatever the sent flag was

byte transmitStringFlagMessage[flagBytesCount];

void sendAck(unsigned long count);
void transmitImage(int height, int width);
void transmitImage3Bit(int height, int width);
void transmitBytes(unsigned long count);
void transmitString(unsigned long length);
void printAsHex(byte data[], int arrSize);



void setup() {
  transmitStringFlagMessage[0] = transmitStringFlag;

  Serial.begin(1000000);

  radio.begin();
  radio.setPALevel(RF24_PA_LOW); // RF24_PA_MAX is default.
  radio.enableDynamicPayloads();
  radio.enableDynamicAck();
  radio.setChannel(85);
  radio.openWritingPipe(address);
  radio.openReadingPipe(1, address);
  radio.stopListening(); // put radio in TX mode
}



void loop() {
  if (Serial.available()) {
    byte flag[flagBytesCount];
    Serial.readBytes(flag, sizeof(flag));
    // Choose the next step depending on what type of message is transmitting
    if(flag[0] == transmitBytesFlag){
      radio.write(flag, sizeof(flag));
      delay(5);//wait for ack
      // read second, third, fourth and fifth byte as integer and call transmitBytes
      transmitBytes(((unsigned long)flag[1] << 24) | ((unsigned long)flag[2] << 16)
                | ((unsigned long)flag[3] << 8) | (unsigned long)flag[4]);
    }
    else if(flag[0] == transmitImageFlag){
      radio.write(flag, sizeof(flag));
      delay(5);
      transmitImage((flag[1] << 8) | flag[2], (flag[3] << 8) | flag[4]);
      //(flag[1] << 8) | flag[2] - read second and third byte as integer
      //(flag[3] << 8) | flag[4] - read fourth and fifth byte as integer
    }
    else if(flag[0] == transmit3BitImageFlag){
      radio.write(flag, sizeof(flag));
      delay(5);
      transmitImage3Bit((flag[1] << 8) | flag[2], (flag[3] << 8) | flag[4]);
      //(flag[1] << 8) | flag[2] - read second and third byte as integer
      //(flag[3] << 8) | flag[4] - read fourth and fifth byte as integer
    }
    else if(flag[0] == transmitStringFlag){
      radio.write(flag, sizeof(flag));
      delay(5);
      
      // read second, third, fourth and fifth byte as integer and call transmitString
      transmitString(((unsigned long)flag[1] << 24) | ((unsigned long)flag[2] << 16)
                | ((unsigned long)flag[3] << 8) | (unsigned long)flag[4]);
    }
  }
  delay(2);
}



void sendAck(unsigned long count){
  byte ackFlagMessage[flagBytesCount];
  ackFlagMessage[0] = ackFlag;
  ackFlagMessage[1] = (byte)(count >> 24);
  ackFlagMessage[2] = (byte)(count >> 16);
  ackFlagMessage[3] = (byte)(count >> 8);
  ackFlagMessage[4] = (byte)(count & 0xFF);
  Serial.write(ackFlagMessage, sizeof(ackFlagMessage));
}



void transmitImage(int height, int width){
  for(int i = 0; i < height; i++){
    int count = width;
    transmitBytes(count);
  }
}



void transmitImage3Bit(int height, int width){
  unsigned long count = (unsigned long)height * (unsigned long)width /2;
  transmitBytes(count);
}



void transmitBytes(unsigned long count){
  unsigned long payloadCount = 0; // current payload index

  // Keep receiving bytes until you get all of it
  while(count > 0){
    int bytesToSend = 0;
    // Take at most *payloadSize* byte chunk
    if(count > payloadSize)
      bytesToSend = payloadSize;
    else
      bytesToSend = count;

    byte data[bytesToSend + 2];
    // TODO: only wait for a certain ammount of time (flush te buffer if exceeded maybe?)
    while (Serial.available() < bytesToSend); // wait until the whole packet comes

    Serial.readBytes(data, bytesToSend);
    data[bytesToSend] = (byte)(payloadCount >> 8);
    data[bytesToSend + 1] = (byte)(payloadCount & 0xFF);

    /*Serial.write(transmitStringFlagMessage, sizeof(transmitStringFlagMessage));
    printAsHex(data, sizeof(data));*/
    bool sent = radio.write(data, sizeof(data));
    while(!sent){
      delay(1);
      Serial.write(transmitStringFlagMessage, sizeof(transmitStringFlagMessage));
      Serial.println("failed to send payload");
      sent = radio.write(data, sizeof(data));
    }
    count -= bytesToSend;
    
    // wait for ack from the receiver
    // TODO: if no ack is received in 200ms, send the data again
    radio.startListening();                  // put in RX mode
    unsigned long start_timeout = millis();
    while (!radio.available()) {             // wait for response
      if (millis() - start_timeout > 200){    // wait for 200 ms
        Serial.write(transmitStringFlagMessage, sizeof(transmitStringFlagMessage));
        Serial.println("no ack");
        break; // TODO: instead of break, send the data again
      }
    }
    radio.stopListening(); // put back in TX mode

    uint8_t pipe;
    if (radio.available(&pipe)) {  // is there an ack
      byte received[flagBytesCount];
      radio.read(&received, sizeof(received));
      unsigned long receivedPayloadCount = (unsigned long)received[1] << 8 | (unsigned long)received[2];
      if(receivedPayloadCount != payloadCount) // check if the ack isn't for the current payload
        return; // TODO: instead of return, send the data again
      
      /*Serial.write(transmitStringFlagMessage, sizeof(transmitStringFlagMessage));
      Serial.print("received ack: ");
      printAsHex(received, sizeof(received));*/
    }
    else{
      return; // TODO: instead of return, send the data again
    }
    
    
    payloadCount++;
    sendAck(count);
  }
}



void transmitString(unsigned long length){
  transmitBytes(length);
}



void printAsHex(byte data[], int arrSize){
  for (int i = 0; i < arrSize; i++) {
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}