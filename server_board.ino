#include <ESP8266WiFi.h>

const char* ssid = "test";         // SSID of the server AP
const char* password = "test1234"; // Password of the server AP
const char* serverIP = "192.168.1.1"; // IP address of the server (AP)
const int serverPort = 8888;       // Port number the server is listening on

const uint8_t serverMAC[] = {0xEA, 0xDB, 0x84, 0xE0, 0xCC, 0xF1}; // Example server MAC address to match

int distance = 0;

// Timer constants
const unsigned long connectionTimeout = 4000;
const unsigned long keepAliveInterval = 3000;
const unsigned long triggerDuration = 12;
const unsigned long distanceTimeout = 38000;
const unsigned long doorDuration = 7000;
const unsigned long doorOpenDuration = 10000;

// Timer variables
unsigned long connectionTimer = 0;
unsigned long keepAliveTimer = 0;
unsigned long triggerTimer = 0;
unsigned long distanceTimer = 0;
unsigned long doorTimer = 0;
unsigned long plotterTimer = 0;

// Door control blocking save
unsigned long lastMillis = 0;

// Timer booleans
bool isConnected = false;
bool isCloseEnough = false;
bool override = false;
bool updateDistance = false;

// State variables
int WiFiConnectionState = 0;
int sonarState = 0;
int doorState = 0;

// Open Password
const String doorPass = "Keep alive";
const String doorPassOpenOverride = "Keep alive and Open";

WiFiClient client;

// Pin configuration
const int triggerPin = D6;
const int echoPin = D5;
const int sonarVcc = D7;
const int thresholdLED[] = {D0, D1, D2, D3};
const int doorOpen = D4;
const int doorClose = D8;

void setUpdateDistance() {
  updateDistance = true;
}

void setup() {
  Serial.begin(9600); // Initialize serial communication at 9600 baud rate
  delay(100);// Short delay for stabilization fro serial configuration

  WiFi.begin(ssid, password);// Begin WiFi connection with the given SSID and password
  WiFiConnectionState = 0; // Initialize WiFi connection state

  // Initialize pins modes
  pinMode(sonarVcc, OUTPUT);
  pinMode(triggerPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(thresholdLED[0], OUTPUT);
  pinMode(thresholdLED[1], OUTPUT);
  pinMode(thresholdLED[2], OUTPUT);
  pinMode(thresholdLED[3], OUTPUT);
  pinMode(doorOpen, OUTPUT);
  pinMode(doorClose, OUTPUT);

  // Initialize pins to low states
  digitalWrite(sonarVcc, HIGH);
  digitalWrite(triggerPin, LOW);
  digitalWrite(thresholdLED[0], LOW);
  digitalWrite(thresholdLED[1], LOW);
  digitalWrite(thresholdLED[2], LOW);
  digitalWrite(thresholdLED[3], LOW);
  digitalWrite(doorOpen, LOW);
  digitalWrite(doorClose, LOW);

  // Set up timer interrupt for checking distance every second
  timer1_attachInterrupt(setUpdateDistance);
  timer1_enable(TIM_DIV256, TIM_EDGE, TIM_LOOP);
  timer1_write(156250); // 0.5 second interval
}

void plotterOut() {
  if(millis() - plotterTimer > 100) {
    Serial.print("Door_Max:");
    Serial.print(doorDuration);
    Serial.print(",MillisMinusDoorTimer:");
    Serial.println(millis() - doorTimer);
    plotterTimer = millis();
  }
}

void loop() {
  // plotterOut();

  // Manage connection and verification
  switch (WiFiConnectionState) {
    case 0:
      // Waiting for connection or connection timeout
      isConnected = false;

    //Checks if the WiFi status is connected using WiFi.status()
      if (WiFi.status() == WL_CONNECTED) {
        const uint8_t *mac = WiFi.BSSID(); // Get the MAC address of the connected AP

         // Check if the MAC address matches the server's MAC address
        if (memcmp(mac, serverMAC, 6) == 0) WiFiConnectionState = 1; // Move to the next state if MAC matches
        else ESP.restart();
      }
      break;

    case 1:
      // Try to connect with TCP server
      //try connecting to the server at the specified IP address (serverIP) and port (serverPort).
      if (client.connect(serverIP, serverPort)) {
        WiFiConnectionState = 2;  // Move to the next state if connection is successful
        connectionTimer = millis(); // Reset connection timer
        keepAliveTimer = millis(); // Reset keep-alive timer
      } else {
        WiFiConnectionState = 0; // Return to initial state if connection fails
      }
      break;

    case 2:
      // When connected with both the network and the TCP server
      
      //Checks if the TCP client is still connected using client.connected()
      if (client.connected()) {
        // Send a keep-alive message to the server at intervals
        if (keepAliveInterval < millis() - keepAliveTimer)  {
          keepAliveTimer = millis();
          client.print("A"); // Keep-alive message
        }

        // Check for server response
        if (client.available()) {
          String response = client.readStringUntil('\n'); // Read response from server
          response.trim();
          
          // Verify the password received from the server
          if(response.compareTo(doorPass) == 0) {
            isConnected = true;
            Serial.println("Correct password recieved");
          }
          else if(response.compareTo(doorPassOpenOverride) == 0) {
            isConnected = true;
            override = true;
            Serial.println("Correct password recieved with override");
          }
          else {
            Serial.print("Password incorrect");
            ESP.restart(); 
          }

          connectionTimer = millis(); // Reset connection timer
        }

        // Connection timeout handling
        if (millis() - connectionTimer > connectionTimeout) {
          isConnected = false;
          WiFiConnectionState = 0;  // Return to initial state on timeout
        }

      } else {
        WiFiConnectionState = 0;  // Return to initial state if disconnected
      }
      break;

    default:
      WiFiConnectionState = 0;// Default case to reset state
      break;
  }

  // Measure distance continuously
  if(isConnected) {
    switch (sonarState) {
      case 0:
        // Wait for echo signal to reset
        isCloseEnough = false;

        if (digitalRead(echoPin) == LOW && updateDistance == true) sonarState = 1;
        break;

      case 1:
        // Send Trigger signal
        digitalWrite(triggerPin, HIGH); // rising edge
        triggerTimer = micros();
        sonarState = 2;
        break;

      case 2:
        // Stop Trigger signal
        if (micros() - triggerTimer > triggerDuration) {
          digitalWrite(triggerPin, LOW); // falling edge
          sonarState = 3;
        }
        break;

      case 3:
        // Wait for echo high
        if (digitalRead(echoPin) == HIGH) {
          distanceTimer = micros();
          sonarState = 4;
        } else if (micros() - triggerTimer > distanceTimeout) {
          // Timeout if echo doesn't go high
          Serial.println("Echo signal too narrow");
          updateDistance = false;
          sonarState = 0;
        }
        break;

      case 4:
        // Wait for echo low or timeout
        if (digitalRead(echoPin) == LOW) {
          unsigned long duration = micros() - distanceTimer;
          distance = duration / 58.2; // Convert duration to cm (HC-SR04 specific)
          
          // Serial output for debugging
          Serial.print("Distance: ");
          Serial.println(distance);


          // Threshold LEDs for operation
          if(distance < 175) digitalWrite(thresholdLED[0], HIGH);
          else digitalWrite(thresholdLED[0], LOW);
          if(distance < 150) digitalWrite(thresholdLED[1], HIGH);
          else digitalWrite(thresholdLED[1], LOW);
          if(distance < 125) digitalWrite(thresholdLED[2], HIGH);
          else digitalWrite(thresholdLED[2], LOW);
          if(distance < 100) digitalWrite(thresholdLED[3], HIGH);
          else digitalWrite(thresholdLED[3], LOW);

          isCloseEnough = (distance < 100); // Example threshold
          sonarState = 0;
        } else if (micros() - distanceTimer > distanceTimeout) {
          // Timeout if echo doesn't go low
          Serial.println("Echo signal too wide");
          digitalWrite(thresholdLED[0], LOW);
          digitalWrite(thresholdLED[1], LOW);
          digitalWrite(thresholdLED[2], LOW);
          digitalWrite(thresholdLED[3], LOW);
          sonarState = 0;
        }
        updateDistance = false;
        break;

    }
  }
  else {
    digitalWrite(triggerPin, LOW);
    sonarState = 0;
    digitalWrite(thresholdLED[0], LOW);
    digitalWrite(thresholdLED[1], LOW);
    digitalWrite(thresholdLED[2], LOW);
    digitalWrite(thresholdLED[3], LOW);
  }

  // Handling door motion controls
  switch(doorState){
    case 0:
      // Door closed
      digitalWrite(doorOpen, LOW);
      digitalWrite(doorClose, LOW);

      // Open if both connected and is close enough
      if (isConnected && (isCloseEnough || override)) {
        override = false;
        Serial.println("Opening door...");
        doorState = 1;// doorOpenstate
        doorTimer = millis(); //doortimer =current time
      }
      break;

    case 1:
      // Door Opening
      digitalWrite(doorClose, LOW);
      digitalWrite(doorOpen, HIGH);
      override = false;

      // Start closing if the wifi disconnects
      if (!isConnected) {
        Serial.println("Disconnected, closing door...");
        doorState = 4;//doorClosing
        doorTimer = millis() + millis() - doorTimer; // Time spent opening is the time required to close
        digitalWrite(doorClose, LOW);
        digitalWrite(doorOpen, LOW);
      }
      // Stop opening if it's fully open
      else if (millis() - doorTimer > doorDuration) {
        doorState = 2;//doorOpened
        doorTimer = millis();
      }
      break;

    case 2:
      // Door Opened
      Serial.println("Door Opened...");
      digitalWrite(doorOpen, LOW);
      digitalWrite(doorClose, LOW);
      doorTimer = millis();
      doorState = 3;
      override = false;
      break;

    case 3:
      // Door Opened idle before closing
      override = false;
      
      // Close the door on either timelapse or WiFi disconnect
      if (!isConnected || millis() - doorTimer > doorOpenDuration) {
        Serial.println("Closing door...");
        doorTimer = millis() + doorDuration;
        doorState = 4;
        digitalWrite(doorClose, LOW);
        digitalWrite(doorOpen, LOW);
      }
      break;

    case 4:
      // Door closing
      digitalWrite(doorOpen, LOW);
      digitalWrite(doorClose, HIGH);

      // Stop closing if the door is closed
      if (doorTimer < millis()) {
        Serial.println("Door Closed...");
        doorState = 0;//doorclosed
        doorTimer = millis();
      }
      // Start opening if is connected and is close enough
      if (isConnected && (isCloseEnough || override)) {
        override = false;
        Serial.println("Opening door...");
        doorState = 1;//dooropening
        doorTimer = millis() + millis() - doorTimer; // Time left to close is the time required to open
      }
      break;

  }

  // Handle blocking
  lastMillis = millis();
}