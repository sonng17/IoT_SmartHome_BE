#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include "DHT.h"
#include <AccelStepper.h>
#include <string.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

#define DHT_PIN 4
#define DHT_TYPE DHT22

#define LDR_PIN 32

#define TRIG_PIN 22
#define ECHO_PIN 23
//define sound speed in cm/uS
#define SOUND_SPEED 0.034


#define LED_PIN_1 2
#define LED_PIN_2 15

#define motorPin1 19
#define motorPin2 18
#define motorPin3 5
#define motorPin4 17
#define MotorInterfaceType 8

const char* ssid = "FPT Telecom";
const char* password = "1234567888";
const char* mqtt_server = "broker.mqttdashboard.com";
const char* subscribeTopic = "BINH.NB194231_SERVER";
const char* publishTopic = "BINH.NB194231_ESP32";
const String macAddress = WiFi.macAddress();

const float gama = 0.7;
const float rl10 = 50;

WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(DHT_PIN, DHT_TYPE);

AccelStepper stepper = AccelStepper(MotorInterfaceType, motorPin1, motorPin3, motorPin2, motorPin4);

long duration;
float distanceCm;

int lampsPort[2] = { LED_PIN_1, LED_PIN_2 };
int lampsStatus[2] = { 0, 0 };
int lampsBreakpoint[2] = { 0, 0 };
int lampsMode[2] = { 0, 0 };
String lampsTimerOff1[10] = {};
String lampsTimerOn1[10] = {};
String lampsTimerOff2[10] = {};
String lampsTimerOn2[10] = {};

int windowsMode[2] = { 0, 0 };
String windowBreakpoint[2] = { "", "" };
int windowArrayBreakpoint[10] = {};
int windowTimersInt[10] = {};

int windowAutoCurrentStatus = 0;

void setup() {

  Serial.begin(115200);

  WiFi.disconnect(true);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  //set up mqtt
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  stepper.setMaxSpeed(10000);
  dht.begin();

  pinMode(TRIG_PIN, OUTPUT);  // Sets the trigPin as an Output
  pinMode(ECHO_PIN, INPUT);   // Sets the echoPin as an Input

  pinMode(LED_PIN_1, OUTPUT);
  pinMode(LED_PIN_2, OUTPUT);



  digitalWrite(LED_PIN_1, LOW);
  digitalWrite(LED_PIN_2, LOW);

  timeClient.begin();
  timeClient.setTimeOffset(3600 * 7);
}

void loop() {

  if (!client.connected()) {
    reconnect();
  }
  client.loop();



  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }

  String msg = "{\"roomId\":\"" + macAddress + "\",\"humidity\":" + String(h) + ",\"temperature\":" + String(t) + ",\"lightIntensity\":" + String(getLight()) + "}";

  Serial.println(msg);

  for (int i = 0; i < 2; i++) {
    Serial.println(lampsMode[i]);

    if (lampsMode[i] == 1) {
      lampAutoControl(i);
    }
    if (lampsMode[i] == 2) {
      lampTimerControl(i + 1);
    }
  }
  for (int i = 0; i < 1; i++) {
    Serial.println(windowsMode[i]);

    if (windowsMode[i] == 1) {
      windowAutoControl(i);
    }
    if (windowsMode[i] == 2) {
      windowTimerControl(i);
    }
  }

  client.publish(publishTopic, msg.c_str());

  delay(3000);
}


void callback(char* topic, byte* payload, unsigned int length) {
  const size_t capacity = JSON_OBJECT_SIZE(100);  // Điều chỉnh theo kích thước JSON của bạn
  StaticJsonDocument<capacity> doc;

  // Parse JSON từ payload
  DeserializationError error = deserializeJson(doc, payload, length);

  // Kiểm tra lỗi trong quá trình parse
  if (error) {
    Serial.print(F("Failed to parse JSON: "));
    Serial.println(error.c_str());
    return;
  }

  // Truy cập dữ liệu từ JSON
  if (doc["roomId"] == macAddress.c_str()) {
    const char* roomId = doc["roomId"];
    const char* command = doc["command"];
    Serial.println(String(command));
    Serial.println(roomId);

    if (String(command) == "lamp-create") {
      const char* command = doc["lampOrder"];
      Serial.println(command);
      lampCreate(String(command));

    } else if (String(command) == "lamp-change-mode") {
      const char* lampOrder = doc["lampOrder"];
      Serial.println(lampOrder);
      const char* mode = doc["mode"];
      if (String(mode) == "auto") {
        lampChangeMode(lampOrder, 1);

      } else if (String(mode) == "manual") {
        lampChangeMode(lampOrder, 0);
      } else if (String(mode) == "timer") {
        lampChangeMode(lampOrder, 2);
      }
    } else if (String(command) == "lamp-control-manual") {
      const char* lampOrder = doc["lampOrder"];
      const char* status = doc["control"];
      lampControlManual(lampOrder, status);
    } else if (String(command) == "lamp-control-change-breakpoint") {
      const char* lampOrder = doc["lampOrder"];
      const char* breakPoint = doc["breakpoint"];
      lampChangeBreakpoint(lampOrder, breakPoint);
      // Code cho trường hợp "lamp-control-change-breakpoint"
    } else if (String(command) == "lamp-control-change-timer") {
      const char* timers = doc["timers"];
      const char* lampOrder = doc["lampOrder"];

      lampChangeTimer(lampOrder, timers);

      // Code cho trường hợp "lamp-control-change-timer"
    } else if (String(command) == "lamp-delete") {
      const char* lampOrder = doc["lampOrder"];
      int order = String(lampOrder).toInt();

      if (order == 1) {
        for (int i = 0; i < 10; i++) {
          lampsTimerOn1[i] = "";
          lampsTimerOff1[i] = "";
        }

        digitalWrite(lampsPort[0], LOW);

      } else if (order == 2) {
        for (int i = 0; i < 10; i++) {
          lampsTimerOn2[i] = "";
          lampsTimerOff2[i] = "";
        }
        digitalWrite(lampsPort[1], LOW);
      }
      lampsStatus[order - 1] = 0;
      lampsBreakpoint[order - 1] = 0;
      lampsMode[order - 1] = 0;

      // Code cho trường hợp "lamp-delete"
    } else if (String(command) == "window-create") {
      // Code cho trường hợp "window-create"
    } else if (String(command) == "window-change-height") {
      // Code cho trường hợp "window-change-height"
    } else if (String(command) == "window-change-mode") {
      const char* windownOrder = doc["windownOrder"];
      Serial.println(windownOrder);
      const char* mode = doc["mode"];
      if (String(mode) == "auto") {
        Serial.println("true");
        windowChangeMode(windownOrder, 1);
      } else if (String(mode) == "manual") {
        windowChangeMode(windownOrder, 0);
      } else if (String(mode) == "timer") {
        windowChangeMode(windownOrder, 2);
      }

    } else if (String(command) == "window-control-manual") {
      const char* windowOrder = doc["windowOrder"];
      const char* status = doc["status"];
      windowControlManual(windowOrder, status);
      // Code cho trường hợp "window-control-manual"
    } else if (String(command) == "window-control-change-breakpoint") {
      const char* breakpoints = doc["breakpoints"];
      const char* windowOrder = doc["windowOrder"];

      windowChangeBreakpoint(windowOrder, breakpoints);

    } else if (String(command) == "window-control-change-timer") {
      const char* timer = doc["timers"];
      const char* windowOrder = doc["windowOrder"];

      windowChangeTimer(windowOrder, timer);
    } else if (String(command) == "window-delete") {
      windowsMode[0] = 0;
      windowBreakpoint[0] = "";
      for (int i = 0; i < 10; i++) {
        windowArrayBreakpoint[i] = NULL;
        windowTimersInt[i] = NULL;
      }


      windowAutoCurrentStatus = 0;
    } else {
      Serial.println("value does not match any case");
    }
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID or can use mac address
    String client_id = "esp32-client-20194231";
    client_id += String(WiFi.macAddress());
    Serial.printf("The client %s is connecting to the public mqtt broker\n", client_id.c_str());
    if (client.connect(client_id.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish(publishTopic, "{\"test\":\"Bình test\"}");
      // ... and resubscribe
      client.subscribe(subscribeTopic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 1 seconds");
      delay(1000);
    }
  }
}

void lampCreate(String lampOrder) {
  Serial.println(lampOrder);
}

void lampControlManual(String lampOrder, String status) {
  Serial.println("lampOrder: " + lampOrder);
  Serial.println("status: " + status);
  const int lampOrderInt = lampOrder.toInt();
  lampsMode[lampOrderInt - 1] = 0;
  if (status == "true") {
    digitalWrite(lampsPort[lampOrderInt - 1], HIGH);
  } else {
    digitalWrite(lampsPort[lampOrderInt - 1], LOW);
  }
}

void windowControlManual(String windowOrder, String status) {
  const int windowOrderInt = windowOrder.toInt();
  const int statusInt = status.toInt();

  float tmpDistance = getDistance();

  Serial.println(tmpDistance < statusInt);

  const bool check = tmpDistance < statusInt;

  if (check) {
    stepper.setCurrentPosition(0);
    Serial.println("run if else");
    while (tmpDistance <= (statusInt+ 5)) {
      Serial.println("run in while");
      Serial.println(tmpDistance);

      stepper.setSpeed(-3000);
      stepper.runSpeed();
      tmpDistance = getDistance();

      if (tmpDistance > statusInt) {
        break;
      }
    }
  } else {
    stepper.setCurrentPosition(0);
    while (tmpDistance >= (statusInt + 5)) {
      Serial.println("run in while");
      Serial.println(tmpDistance);

      stepper.setSpeed(3000);
      stepper.runSpeed();
      tmpDistance = getDistance();

      if (tmpDistance < statusInt) {
        break;
      }
    }
  }
}

float getLight() {
  int nilaiLDR = analogRead(LDR_PIN);
  nilaiLDR = map(nilaiLDR, 4095, 0, 1024, 0);  //mengubah nilai pembacaan sensor LDR dari nilai ADC arduino menjadi nilai ADC ESP32
  float voltase = nilaiLDR / 1024. * 5;
  float resistansi = 2000 * voltase / (1 - voltase / 5);
  float analogValue = pow(rl10 * 1e3 * pow(10, gama) / resistansi, (1 / gama));
  return analogValue;
}

float getDistance() {
  // Clears the trigPin
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  // Sets the trigPin on HIGH state for 10 micro seconds
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Reads the echoPin, returns the sound wave travel time in microseconds
  duration = pulseIn(ECHO_PIN, HIGH);

  // Calculate the distance
  float tmpDistanceCm = duration * SOUND_SPEED / 2;

  Serial.println(tmpDistanceCm);
  return tmpDistanceCm;
}

void lampChangeMode(String lampOrder, int mode) {
  const int lampOrderInt = lampOrder.toInt();

  Serial.println(lampOrderInt);
  lampsMode[lampOrderInt - 1] = mode;
}

void lampChangeBreakpoint(String lampOrder, String breakPoint) {
  const int lampOrderInt = lampOrder.toInt();
  const int breakpoint = breakPoint.toInt();
  lampsBreakpoint[lampOrderInt - 1] = breakpoint;
  Serial.println(lampsBreakpoint[lampOrderInt - 1]);
}

void lampChangeTimer(String lampOrder, String timers) {
  const int lampOrderInt = lampOrder.toInt();
  char* token;
  char* mystring = (char*)timers.c_str();

  Serial.println(mystring);

  const char* delimiter = ",-";

  token = strtok(mystring, delimiter);
  int i = 0;

  String tmpArr[10] = {};

  while (token != NULL) {
    // Serial.println(token);
    tmpArr[i] = token;
    i++;
    token = strtok(NULL, delimiter);
  };

  int j = 0;
  int k = 0;


  if (lampOrderInt == 1) {
    for (int i = 0; i < 5; i++) {
      if (tmpArr[i] != NULL) {
        if (tmpArr[2 * i + 1] == "1") {
          lampsTimerOn1[j] = tmpArr[i * 2];
          j++;
        } else if (tmpArr[2 * 1 + 1] == "0") {
          lampsTimerOff1[k] = tmpArr[i * 2];
          k++;
        }
      }
    }
  } else if (lampOrderInt == 2) {
    for (int i = 0; i < 5; i++) {
      if (tmpArr[i] != NULL) {
        if (tmpArr[2 * i + 1] == "1") {
          lampsTimerOn2[j] = tmpArr[i * 2];
          j++;
        } else if (tmpArr[2 * 1 + 1] == "0") {
          lampsTimerOff2[k] = tmpArr[i * 2];
          k++;
        }
      }
    }
  }
}


void lampAutoControl(int order) {
  float light = getLight();
  Serial.println(lampsBreakpoint[order]);
  if (light <= lampsBreakpoint[order]) {
    digitalWrite(lampsPort[order], HIGH);
  } else {
    digitalWrite(lampsPort[order], LOW);
  }
}

void lampTimerControl(int order) {
  timeClient.update();

  // Lấy thời gian hiện tại
  String formattedTime = timeClient.getFormattedTime();
  Serial.println(formattedTime.substring(0, 5));

  if (order == 1) {
    for (int i = 0; i < 10; i++) {
      if (lampsTimerOn1[i] != NULL) {
        if (lampsTimerOn1[i] == formattedTime.substring(0, 5)) {
          digitalWrite(lampsPort[0], HIGH);
        }
      }
    }
    for (int i = 0; i < 10; i++) {
      if (lampsTimerOff1[i] != NULL) {
        if (lampsTimerOff1[i] == formattedTime.substring(0, 5)) {
          digitalWrite(lampsPort[0], LOW);
        }
      }
    }
  } else if (order == 2) {
    for (int i = 0; i < 10; i++) {
      if (lampsTimerOn2[i] != NULL) {
        if (lampsTimerOn2[i] == formattedTime.substring(0, 5)) {
          digitalWrite(lampsPort[1], HIGH);
        }
      }
    }
    for (int i = 0; i < 10; i++) {
      if (lampsTimerOff2[i] != NULL) {
        if (lampsTimerOff2[i] == formattedTime.substring(0, 5)) {
          digitalWrite(lampsPort[1], LOW);
        }
      }
    }
  }
}

void windowChangeMode(String windowOrder, int mode) {

  const int windowOrderInt = windowOrder.toInt();
  windowsMode[windowOrderInt - 1] = mode;
}
void windowChangeBreakpoint(String windowOrder, String breakpoint) {
  const int windowOrderInt = windowOrder.toInt();
  windowBreakpoint[windowOrderInt - 1] = breakpoint;

  char* token;
  char* mystring = (char*)breakpoint.c_str();

  Serial.println(mystring);

  const char* delimiter = ",-";

  token = strtok(mystring, delimiter);
  int i = 0;

  while (token != NULL) {
    // Serial.println(token);
    windowArrayBreakpoint[i] = String(token).toInt();
    i++;
    token = strtok(NULL, delimiter);
  }

  for (int i = 0; i < 10; i++) {
    Serial.println(windowArrayBreakpoint[i]);
  }
}

void windowChangeTimer(String windowOrder, String timer) {
  String windowTimers[10] = {};
  const int windowOrderInt = windowOrder.toInt();

  char* token;
  char* mystring = (char*)timer.c_str();

  Serial.println(mystring);

  const char* delimiter = ",-";

  token = strtok(mystring, delimiter);
  int i = 0;

  while (token != NULL) {
    // Serial.println(token);
    windowTimers[i] = token;
    i++;
    token = strtok(NULL, delimiter);
  }

  for (int i = 0; i < 5; i++) {
    String tmpTime = windowTimers[2 * i];
    int hour = tmpTime.substring(0, 2).toInt();
    int minute = tmpTime.substring(3, 5).toInt();
    int total = hour * 3600 + minute * 60;
    windowTimersInt[i * 2] = total;
    windowTimersInt[i * 2 + 1] = String(windowTimers[i * 2 + 1]).toInt();
  }

  for (int i = 0; i < 10; i++) {
    Serial.println(windowTimersInt[i]);
  }
}

void windowAutoControl(int order) {
  float light = getLight();
  Serial.println(light);
  for (int i = 0; i < 5; i++) {
    if (windowArrayBreakpoint[i * 2] != 0) {
      if (light <= windowArrayBreakpoint[i * 2]) {
        if (windowAutoCurrentStatus != windowArrayBreakpoint[i * 2 + 1]) {
          windowAutoCurrentStatus = windowArrayBreakpoint[i * 2 + 1];
          windowControlManual(String("1"), String(windowArrayBreakpoint[i * 2 + 1]));
        }
        break;
      }
    }
  }
}

void windowTimerControl(int order) {
  timeClient.update();
  // Lấy thời gian hiện tại
  String formattedTime = timeClient.getFormattedTime();
  Serial.println(formattedTime);
  int hour = formattedTime.substring(0, 2).toInt();
  int minute = formattedTime.substring(3, 5).toInt();
  int second = formattedTime.substring(6, 8).toInt();

  int total = hour * 3600 + minute * 60 + second;
  Serial.println(total);

  for (int i = 0; i < 5; i++) {
    if (windowTimersInt[2 * i] < total) {
      if ((total - windowTimersInt[2 * i]) < 3) {
        windowControlManual(String("1"), String(windowTimersInt[i * 2 + 1]));
      }
    } else if (windowTimersInt[2 * i] == total) {
      windowControlManual(String("1"), String(windowTimersInt[i * 2 + 1]));
    }
  }
}