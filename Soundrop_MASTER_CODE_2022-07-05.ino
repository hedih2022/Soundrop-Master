Soundrop © 2022 by Hedy Hurban is licensed under CC BY-NC-SA 4.0 

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <TinyMPU6050.h>
#include <ESP8266WiFi.h>
#include <OSCMessage.h>
#include <WiFiUdp.h>

//variables you can change
#define DEVICENAME "/device6" // the device name, change this for each
String feedbackColour = "green"; // change between red, green, or blue
const bool maxmsp = true; // set true for max

#define SERVERIP 192,168,0,100 // server ip
//#define SERVERIP 192,168,10,114 // iml pc
const int outPort = 6448; // server port
const char* ssid = "TPLINK";  // wifi name
const char* password = "84478221"; // wifi password
//const char* ssid = "Fulldome.Pro";
//const char* password = "fulldome108";
const int wifiTimeout = 10000; // how long to try and connect to wifi (10 seconds = 10000)
int touchDelay = 100; // how long to hold the touch pad before it registers start/stop (1 second = 1000)
int touchReconnect = 4; // how many taps are needed to trigger reconnect attempt
const int smoothingAmount = 8; // higher value will make it smoother
const int sensitivity = 100; // how sensitive the device is to movement. Lower numbers are more sensitive 
const bool debugging = false; // print debugging info to the serial
//

const int touchPin = 12;
const int motorPin = 14;
const int ledDelay = 150;
int ledMillis;
int touchState;
int lastTouchState;
unsigned int touchPressed;
unsigned int touchStop;
int touchTapCount;
int touchTapDelay = 500;
bool runningState = false;
float mpuValues[6];
float smoothing[smoothingAmount], mpuValues0Smoothing[smoothingAmount], mpuValues1Smoothing[smoothingAmount];
int smoothingIndex;
float mpuTotal, mpuSmoothedTotal, mpuAverage, mpuValues0Total, mpuValues0Average, mpuValues1Total, mpuValues1Average;
int feedbackValue;
const IPAddress ip(SERVERIP);
const int localPort = 12000;
bool connectingFailed;
unsigned int wifiStart;
unsigned int strengthMillis;
int wifiStrength;
int strengthDelay = 2000;

Adafruit_NeoPixel pixels(12, 13, NEO_GRB + NEO_KHZ800);
MPU6050 mpu(Wire);
WiFiUDP Udp;

void setup() {
  Serial.begin(9600);
  pinMode(touchPin, INPUT);
  pixels.begin();
  mpu.Initialize();
  connectWifi();
  Udp.begin(localPort);  
}

void loop() {
  readTouch();

  if (runningState == true) {
    getMpu();
    feedback();
    sendOsc(mpuAverage);
  }

  if (debugging == true) {
    debugText();
    //debugNumbers();
  }
  delay(5);
}

void readTouch() {
  touchState = digitalRead(touchPin);

  if (millis() - touchPressed >= touchTapDelay) {
    touchTapCount = 0;
  }
  
  if (touchState != lastTouchState) {
    if (touchState == HIGH) {
      touchPressed = millis();
      touchTapCount++;
    } else {
      touchStop = millis();
    }
  }
  lastTouchState = touchState;

  if (touchTapCount == touchReconnect) {
    touchTapCount = 0;
    connectingFailed = false;
    connectWifi();
    return;
  }

  if (millis() - touchPressed >= touchDelay && touchPressed > touchStop) {
    touchTapCount = 0;
    runningState = !runningState;
    touchStop = millis();
    if (runningState == true) {
      blinkLed(0,100,0,100);
    } else {
      blinkLed(0,0,100,100);
      analogWrite(motorPin, 0);
      sendOsc(0);
      mpuTotal = 0;
      mpuAverage = 0;
    }
  }
}

void blinkLed(float cr, float cg, float cb, float btime) {
      pixels.fill(pixels.Color(cr,cg,cb));
      pixels.show();
      delay(btime);
      pixels.clear();
      pixels.show();
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid,password);
  wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && connectingFailed == false) {
    blinkLed(25,25,25,100);
    delay(500);
    if (millis() - wifiStart >= wifiTimeout) {
      connectingFailed = true;
      WiFi.mode(WIFI_OFF);
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    blinkLed(50,50,50,1000);
    WiFi.setAutoReconnect(true);
  } else {
    for (int i = 0; i < 5; i++) {
      blinkLed(255,0,0,50);
      delay(100);
    }
  }
}

void getMpu() {
  mpuTotal = 0;
  mpu.Execute();
 
  mpuValues[0] = mpu.GetGyroX();
  mpuValues[1] = mpu.GetGyroY();
  mpuValues[2] = mpu.GetGyroZ();
  mpuValues[3] = mpu.GetAccX();
  mpuValues[4] = mpu.GetAccY();
  mpuValues[5] = mpu.GetAccZ();

  mpuValues0Total = mpuValues0Total - mpuValues0Smoothing[smoothingIndex];
  mpuValues0Smoothing[smoothingIndex] = mpuValues[0];
  mpuValues0Total = mpuValues0Total + mpuValues0Smoothing[smoothingIndex];

  mpuValues1Total = mpuValues1Total - mpuValues1Smoothing[smoothingIndex];
  mpuValues1Smoothing[smoothingIndex] = mpuValues[1];
  mpuValues1Total = mpuValues1Total + mpuValues1Smoothing[smoothingIndex];

  for (int i = 0; i < 6; i++) {
    if (mpuValues[i] < 0) {
      mpuValues[i] = mpuValues[i] * -1;
    }
  }

  for (int i = 0; i < 3; i++) { // only taking the Gyro data! Change to 6 for all.
    mpuTotal = mpuTotal + mpuValues[i];
  }

  if (mpuTotal < sensitivity) {
    mpuTotal = 0;
  }

  mpuSmoothedTotal = mpuSmoothedTotal - smoothing[smoothingIndex];
  smoothing[smoothingIndex] = mpuTotal;
  mpuSmoothedTotal = mpuSmoothedTotal + smoothing[smoothingIndex];


  //common part
  smoothingIndex++;
  if (smoothingIndex >= smoothingAmount) {
    smoothingIndex = 0;
  }
  
  mpuAverage = mpuSmoothedTotal / smoothingAmount;
  mpuValues0Average = mpuValues0Total / smoothingAmount;
  mpuValues1Average = mpuValues1Total / smoothingAmount;
}

void feedback(){
  feedbackValue = map(mpuAverage, 0, 1500, 0, 255);
  if (feedbackColour == "red") {
    pixels.fill(pixels.Color(feedbackValue,0,0));
  }
  else if (feedbackColour == "green") {
    pixels.fill(pixels.Color(0,feedbackValue,0));
  }
  else if (feedbackColour == "blue") {
    pixels.fill(pixels.Color(0,0,feedbackValue));
  }
  if (millis() - ledMillis >= ledDelay) { // don't update the leds constantly to reduce flickering
    ledMillis = millis();
    analogWrite(motorPin, feedbackValue*1.5);
    pixels.show();
  }
}

void sendOsc(float sendValue){
  OSCMessage msg(DEVICENAME);
  msg.add(sendValue);
  if (maxmsp == false) {
    msg.add(mpuValues0Average);
    msg.add(mpuValues1Average);
  }
  Udp.beginPacket(ip, outPort);
  msg.send(Udp);
  Udp.endPacket();
  msg.empty();
}

void debugText() {
  if (millis() - strengthMillis >= strengthDelay) {
    wifiStrength = WiFi.RSSI();
    strengthMillis = millis();
  }
  Serial.print("wifiStrength:");
  Serial.print(wifiStrength);
  Serial.print(", touchTapCount:");
  Serial.print(touchTapCount);
  Serial.print(", mpuTotal:");
  Serial.print(mpuTotal);
  Serial.print(", mpuAverage:");
  Serial.println(mpuAverage);
}

void debugNumbers() {
  Serial.print(mpuValues[0]);
  Serial.print(", ");
  Serial.print(mpuValues[1]);
  Serial.print(", ");
  Serial.print(mpuValues[2]);
  Serial.print(", ");
  Serial.print(mpuValues[3]);
  Serial.print(", ");
  Serial.print(mpuValues[4]);
  Serial.print(", ");
  Serial.println(mpuValues[5]);
}
