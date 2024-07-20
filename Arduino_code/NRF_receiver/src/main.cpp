#include <Arduino.h>

#include <SPI.h>
#include <RF24.h>
#include "LowPower.h"

//#define debug

#define ARG_COUNT(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, COUNT, ...) COUNT
#define COUNT_ARGS(...) ARG_COUNT(__VA_ARGS__, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)

#ifdef debug
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINT_1(x) Serial.print(x)
  #define DEBUG_PRINT_2(x, y) Serial.print(x, y)
#else
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINT_1(x)
  #define DEBUG_PRINT_2(x, y)
#endif

// To automatically select the macro based on the number of arguments
  #define DEBUG_PRINT_HELPER2(count, ...) DEBUG_PRINT_##count(__VA_ARGS__)
  #define DEBUG_PRINT_HELPER1(count, ...) DEBUG_PRINT_HELPER2(count, __VA_ARGS__)
  #define DEBUG_PRINT(...) DEBUG_PRINT_HELPER1(COUNT_ARGS(__VA_ARGS__), __VA_ARGS__)

const int RECEIVER_WAKE_PIN = 3; // to wake the ESP32 or other receiving controller

RF24 radio(7, 8); // CE, CSN

const uint8_t address[] = "00050";
const unsigned int flagBytesCount = 5;
const unsigned int payloadSize = 30; // need 2 bytes free for payloadCount
const byte transmitBytesFlag = 0x01; // [0] - 0x01, [1,...,4] - byte count
const byte transmitBytesWakeFlag = 0x02; // [0] - 0x02, [1,...,4] - byte count -> before sending the data, wakes up the receiver
const byte ackFlag = 0xFF; // acknoledgement
const byte nakFlag = 0x00; // negative acknoledgement
const String wakeMessage = "awake";
const int sleep_time = 1; // total sleep time: sleep_time * 8 seconds
const int sleep_timeout = 5000; // how long will the receiver wait for a message before going to sleep
unsigned long last_sleep_time = 0; // when the receiver last woke up

bool waitForWake(int timeout = 1000);
void receiveInterrupt();
void wakeReceiver();
bool sendAck(unsigned long payloadCount);
void receiveBytes(unsigned long count);
void printAsHex(byte data[], int arrSize);
void setupRadio();

int nrf_power_pin = 4; // controls the power connected to the nrf24l01 module


void setup() {
  pinMode(nrf_power_pin, OUTPUT);
  digitalWrite(nrf_power_pin, LOW);
  pinMode(RECEIVER_WAKE_PIN, OUTPUT);
  digitalWrite(RECEIVER_WAKE_PIN, LOW);

  Serial1.begin(1000000);
  #ifdef debug
    Serial.begin(1000000);
  #endif

  setupRadio();

  last_sleep_time = millis();
}



void setupRadio(){
  radio.begin();
  radio.maskIRQ(false, false, true); // interrupt - (tx_ok, tx_fail, rx_ready)
  radio.setPALevel(RF24_PA_LOW);
  radio.enableDynamicPayloads();
  radio.enableDynamicAck();
  radio.setChannel(85);
  radio.openWritingPipe(address);
  radio.openReadingPipe(1, address);  // using pipe 1
  radio.startListening(); // put radio in RX mode
}


void loop() {
  if (radio.available()) {
    byte flag[flagBytesCount];
    radio.read(&flag, sizeof(flag));
    if(flag[0] == transmitBytesFlag){
      // read second, third, fourth and fifth byte as integer and call receiveBytes
      unsigned long count = ((unsigned long)flag[1] << 24) | ((unsigned long)flag[2] << 16)
                | ((unsigned long)flag[3] << 8) | (unsigned long)flag[4];
      
      bool report = sendAck(count);
      
      receiveBytes(count);
    }
    else if(flag[0] == transmitBytesWakeFlag){
      // read second, third, fourth and fifth byte as integer and call receiveBytes
      unsigned long count = ((unsigned long)flag[1] << 24) | ((unsigned long)flag[2] << 16)
                | ((unsigned long)flag[3] << 8) | (unsigned long)flag[4];
      
      wakeReceiver();

      if(!waitForWake()){
        DEBUG_PRINTLN("Wake signal not received");
        return;
      }
      bool report = sendAck(count);
      
      receiveBytes(count);
    }

    radio.flush_rx(); // clear the rx buffer
    radio.flush_tx(); // clear the tx buffer
  }

  if(millis() - last_sleep_time >= sleep_timeout){
    DEBUG_PRINTLN("Going to sleep");
    digitalWrite(nrf_power_pin, HIGH);
    delay(2); //wait for everything to finish
    for(int i = 0; i < sleep_time; i++){ // go to sleep for sleep_time * 8 seconds
      LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF); // Sleep for 8 seconds
    }
    // woke up
    digitalWrite(nrf_power_pin, LOW);
    setupRadio();
    last_sleep_time = millis();
  }
}



// waits until receiving controller sends wake signal
// returns true if wake signal is received, false otherwise
bool waitForWake(int timeout = 1000){
  char buffer[sizeof(wakeMessage)];
  int index = 0;

  unsigned long startTime = millis();
  while(millis() - startTime < timeout){
    if(Serial1.available()){
      buffer[index] = Serial1.read();
      index = (index + 1) % 5;

      for (int i = 0; i < sizeof(wakeMessage); i++) {
        char temp[sizeof(wakeMessage) + 1]; // +1 for null terminator
        for (int j = 0; j < 5; j++) {
          temp[j] = buffer[(i + j) % 5];
        }
        temp[5] = '\0';

        if (strcmp(temp, wakeMessage.c_str()) == 0) {
          return true;
        }
      }
    }
  }

  return false;
}


// wake up the receiving controller (or PC)
void wakeReceiver(){
  digitalWrite(RECEIVER_WAKE_PIN, HIGH);
  delayMicroseconds(500);
  digitalWrite(RECEIVER_WAKE_PIN, LOW);
}


// for sending the ack back to the transmitter
bool sendAck(unsigned long payloadCount){
  bool report = false;
  byte ackMessage[flagBytesCount];

  ackMessage[0] = ackFlag;
  ackMessage[1] = payloadCount >> 24;
  ackMessage[2] = payloadCount >> 16;
  ackMessage[3] = payloadCount >> 8;
  ackMessage[4] = payloadCount & 0xFF;

  radio.stopListening();  // put in TX mode
  radio.writeFast(&ackMessage, sizeof(ackMessage));  // load response to TX FIFO
  report = radio.txStandBy(150);          // keep retrying for 150 ms
  radio.startListening();  // put back in RX mode

  return report;
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
        DEBUG_PRINTLN("Transmission timed out");
        last_sleep_time = millis(); // if the transmission fails, let the transmitter try again
        return; // cancel transmission
      }
    }
    
    radio.read(&data, sizeof(data));
    
    unsigned long receivedPayloadCount = ((unsigned long)data[bytesToReceive] << 8) | data[bytesToReceive + 1];
    
    // check if the packet was already received (if the previous ack failed and the transmitter resent the packet)
    if(receivedPayloadCount < payloadCount){
      DEBUG_PRINTLN("Received old packet");
      sendAck(receivedPayloadCount);
      continue;
    }
    else if(receivedPayloadCount > payloadCount){
      DEBUG_PRINTLN("Received future packet");
      last_sleep_time = millis(); // if the transmission fails, let the transmitter try again
      return;
    }


    Serial1.write(data, bytesToReceive);
    count -= bytesToReceive;

    // send ack to transmitter
    bool report = sendAck(receivedPayloadCount);
    DEBUG_PRINTLN("Ack report: " + String(report));
    
    payloadCount++;
  }
}



void printAsHex(byte data[], int arrSize){
  for (int i = 0; i < arrSize; i++) {
    DEBUG_PRINT(data[i], HEX);
    DEBUG_PRINT(" ");
  }
  DEBUG_PRINTLN();
}