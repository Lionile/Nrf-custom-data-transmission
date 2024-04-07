#include <Arduino.h>

#include <SPI.h>
#include <RF24.h>

RF24 radio(7, 8); // CE, CSN

const uint8_t address[] = "00050";
const unsigned int flagBytesCount = 5;
const unsigned int payloadSize = 30; // need 2 bytes free for payloadCount
const byte transmitBytesFlag = 0x01; // [0] - 0x01, [1,2] - byte count,
const byte transmitImageFlag = 0x02; // [0] - 0x02, [1,2] - image height, [3,4] - image width
const byte transmitStringFlag = 0x03; // [0] - 0x03, [1,...,4] - string length
const byte transmit3BitImageFlag = 0x04;
const byte ackFlag = 0xFF;
const byte nakFlag = 0x00;

byte transmitStringFlagMessage[flagBytesCount];

void sendAck(unsigned long count);
void transmitBytes(unsigned long count);
void printAsHex(byte data[], int arrSize);
void resetRadio();
void sendNak(unsigned long count);
bool sendPayload(byte data[], int size, int timeout);
bool waitForAck(int timeout = 100);


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


void resetRadio(){
  radio.begin();
  radio.setPALevel(RF24_PA_LOW); // RF24_PA_MAX is default.
  radio.enableDynamicPayloads();
  radio.enableDynamicAck();
  radio.setChannel(85);
  radio.openWritingPipe(address);
  radio.openReadingPipe(1, address);
  radio.flush_rx();
  radio.flush_tx();
  radio.stopListening(); // put radio in TX mode
}



void loop() {
  if (Serial.available()) {
    byte flag[flagBytesCount];
    Serial.readBytes(flag, sizeof(flag));
    // Choose the next step depending on what type of message is transmitting
    if(flag[0] == transmitBytesFlag){
      radio.write(flag, sizeof(flag));
      unsigned long count = ((unsigned long)flag[1] << 24) | ((unsigned long)flag[2] << 16)
                | ((unsigned long)flag[3] << 8) | (unsigned long)flag[4];
      
      bool ackReceived = waitForAck();
      if(!ackReceived){               // waiting for ack timed out
        Serial.write(transmitStringFlagMessage, sizeof(transmitStringFlagMessage));
        Serial.println("no flag ack");
        return;                     // try sending the data again
      }

      // read the ack and check if it's correct
      byte received[flagBytesCount];
      radio.read(&received, sizeof(received));
      unsigned long ackCount = ((unsigned long)received[1] << 24) | ((unsigned long)received[2] << 16)
                | ((unsigned long)received[3] << 8) | (unsigned long)received[4];
      if(ackCount != count) // check if the ack isn't for the current payload
        return; // TODO: instead of return, send the data again

      sendAck(count);
      // read second, third, fourth and fifth byte as integer and call transmitBytes
      transmitBytes(count);
    }
  }
  delay(2);
}


// for sending the ack back to the sender
void sendAck(unsigned long count){
  byte ackFlagMessage[flagBytesCount];
  ackFlagMessage[0] = ackFlag;
  ackFlagMessage[1] = (byte)(count >> 24);
  ackFlagMessage[2] = (byte)(count >> 16);
  ackFlagMessage[3] = (byte)(count >> 8);
  ackFlagMessage[4] = (byte)(count & 0xFF);
  Serial.write(ackFlagMessage, sizeof(ackFlagMessage));
}


// for sending the nak back to the sender
void sendNak(unsigned long count){
  byte nakFlagMessage[flagBytesCount];
  nakFlagMessage[0] = nakFlag;
  nakFlagMessage[1] = (byte)(count >> 24);
  nakFlagMessage[2] = (byte)(count >> 16);
  nakFlagMessage[3] = (byte)(count >> 8);
  nakFlagMessage[4] = (byte)(count & 0xFF);
  Serial.write(nakFlagMessage, sizeof(nakFlagMessage));
}


bool sendPayload(byte data[], int size, int timeout = 300){
  unsigned long send_timeout_start = millis();
  bool sent = radio.write(data, size);
  while(!sent && millis() - send_timeout_start < timeout){
    delay(1);
    Serial.write(transmitStringFlagMessage, sizeof(transmitStringFlagMessage));
    Serial.println("failed to send payload");
    sent = radio.write(data, size);
  }
  if(sent)
    return true;

  return false;
}


bool waitForAck(int timeout = 100){
  radio.startListening();         // put in RX mode
  unsigned long ack_timeout_start = millis();
  while (!radio.available()) {             // wait for response
    if (millis() - ack_timeout_start > 100){    // wait for some time
      radio.stopListening();      // put back in TX mode
      radio.flush_rx();           // clear the buffer
      return false;
    }
  }
  radio.stopListening();      // put back in TX mode

  return true;
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

    byte data[bytesToSend + 2]; // +2 bytes for payloadCount

    // read the bytes from the serial port
    // only wait for a certain ammount of time before canceling transmission
    unsigned long start_waiting = millis();
    while (Serial.available() < bytesToSend) {
      if (millis() - start_waiting > 1000) {
        Serial.write(transmitStringFlagMessage, sizeof(transmitStringFlagMessage));
        Serial.println("Transmission canceled");
        return;
      }
    }

    Serial.readBytes(data, bytesToSend);
    data[bytesToSend] = (byte)(payloadCount >> 8);
    data[bytesToSend + 1] = (byte)(payloadCount & 0xFF);


    // keep trying to send the payload until the receiver acknowledges it, or time runs out
    bool sent_and_acknowledged = false;
    unsigned long send_start = millis();
    while(millis() - send_start < 2000){
      // try sending payload
      bool sent = sendPayload(data, sizeof(data)); // tries to send it for a certain period of time, and returns true if it was sent
      if(!sent){ // couldn't send the payload
        Serial.write(transmitStringFlagMessage, sizeof(transmitStringFlagMessage));
        Serial.println("Transmission canceled: failed to send payload");
        sendNak(payloadCount);
        resetRadio();
        return;
      }
      count -= bytesToSend;
      

      // payload was sent, wait for ack from the receiver
      // if no ack is received for some time, send the data again
      bool ackReceived = waitForAck();
      if(!ackReceived){               // waiting for ack timed out
        Serial.write(transmitStringFlagMessage, sizeof(transmitStringFlagMessage));
        Serial.println("no ack");
        count += bytesToSend;         // return count to its previous value
        continue;                     // try sending the data again
      }

      // packet was sent, and ack was received
      sent_and_acknowledged = true;
      break;
    }

    if(!sent_and_acknowledged){
      Serial.write(transmitStringFlagMessage, sizeof(transmitStringFlagMessage));
      Serial.println("Transmission canceled: failed to send and ack payload");
      sendNak(payloadCount);
      radio.stopListening();
      radio.flush_rx();
      radio.flush_tx();
      return;
    }


    // read the ack and check if it's correct
    byte received[flagBytesCount];
    radio.read(&received, sizeof(received));
    unsigned long receivedPayloadCount = ((unsigned long)received[1] << 24) | ((unsigned long)received[2] << 16)
                | ((unsigned long)received[3] << 8) | (unsigned long)received[4];
    if(receivedPayloadCount != payloadCount) // check if the ack isn't for the current payload
      return; // TODO: instead of return, send the data again
    
    sendAck(payloadCount);
    payloadCount++;
  }
}



void printAsHex(byte data[], int arrSize){
  for (int i = 0; i < arrSize; i++) {
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}