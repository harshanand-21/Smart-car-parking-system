#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>

// Wi-Fi and MQTT Network Configuration
const char* ssid = "Meera's S24";
const char* password = "200meera";
const char* mqtt_server = "broker.hivemq.com";

WiFiClient espClient;
PubSubClient client(espClient);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo gate;

// Hardware Pin Definitions
#define ENTRY_SENSOR 27
#define SLOT1_SENSOR 26
#define SLOT2_SENSOR 25
#define SLOT3_SENSOR 33
#define SERVO_PIN 13
#define BUZZER_PIN 12

bool entryTriggered = false;

// Hardware Tracking States: 'E' = Empty, 'F' = Full (Occupied), 'R' = Reserved
char slot1_status = 'E';
char slot2_status = 'E';
char slot3_status = 'E';

void setup() {
  Serial.begin(115200);
  pinMode(ENTRY_SENSOR, INPUT);
  pinMode(SLOT1_SENSOR, INPUT);
  pinMode(SLOT2_SENSOR, INPUT);
  pinMode(SLOT3_SENSOR, INPUT);

  gate.attach(SERVO_PIN);
  gate.write(0); // Ensure gate starts closed

  ledcAttach(BUZZER_PIN, 2000, 8);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("SMART PARKING");
  lcd.setCursor(0, 1);
  lcd.print("SYSTEM READY");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback); // Link the incoming web reservation handler
}

// Handles Incoming Web Reservation Commands
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) { message += (char)payload[i]; }
  String topicStr = String(topic);

  // Sound buzzer to confirm incoming web command handshake
  ledcWriteTone(BUZZER_PIN, 1800);
  delay(150);
  ledcWriteTone(BUZZER_PIN, 0);

  if (topicStr == "idp/parking/reserve1") {
    if (message == "book") slot1_status = 'R';
    else if (message == "release") slot1_status = 'E';
  }
  if (topicStr == "idp/parking/reserve2") {
    if (message == "book") slot2_status = 'R';
    else if (message == "release") slot2_status = 'E';
  }
  if (topicStr == "idp/parking/reserve3") {
    if (message == "book") slot3_status = 'R';
    else if (message == "release") slot3_status = 'E';
  }
}

void reconnect() {
  while (!client.connected()) {
    String clientId = "ESP32_SmartPark_" + String(random(0, 9999));
    if (client.connect(clientId.c_str())) {
      // Subscribe to web dashboard reservation channels
      client.subscribe("idp/parking/reserve1");
      client.subscribe("idp/parking/reserve2");
      client.subscribe("idp/parking/reserve3");
    } else {
      delay(2000);
    }
  }
}

void loop() {
  if (!client.connected()) { reconnect(); }
  client.loop();

  // Read infrared slot proximity levels
  int r1 = digitalRead(SLOT1_SENSOR);
  int r2 = digitalRead(SLOT2_SENSOR);
  int r3 = digitalRead(SLOT3_SENSOR);

  // Physical occupancy takes logic priority over a digital reservation
  if (r1 == LOW) { slot1_status = 'F'; client.publish("idp/parking/slot1", "occupied"); }
  else if (slot1_status == 'F') { slot1_status = 'E'; client.publish("idp/parking/slot1", "free"); }
  else if (slot1_status == 'R') { client.publish("idp/parking/slot1", "reserved"); }
  else { client.publish("idp/parking/slot1", "free"); }

  if (r2 == LOW) { slot2_status = 'F'; client.publish("idp/parking/slot2", "occupied"); }
  else if (slot2_status == 'F') { slot2_status = 'E'; client.publish("idp/parking/slot2", "free"); }
  else if (slot2_status == 'R') { client.publish("idp/parking/slot2", "reserved"); }
  else { client.publish("idp/parking/slot2", "free"); }

  if (r3 == LOW) { slot3_status = 'F'; client.publish("idp/parking/slot3", "occupied"); }
  else if (slot3_status == 'F') { slot3_status = 'E'; client.publish("idp/parking/slot3", "free"); }
  else if (slot3_status == 'R') { client.publish("idp/parking/slot3", "reserved"); }
  else { client.publish("idp/parking/slot3", "free"); }

  // Compute remaining open space availability
  int available = 0;
  if (slot1_status == 'E') available++;
  if (slot2_status == 'E') available++;
  if (slot3_status == 'E') available++;

  // Update physical I2C LCD readout screen
  lcd.setCursor(0, 0);
  lcd.print("Avail:"); lcd.print(available); lcd.print("   ");

  lcd.setCursor(0, 1);
  lcd.print("1:"); lcd.print(slot1_status);
  lcd.print(" 2:"); lcd.print(slot2_status);
  lcd.print(" 3:"); lcd.print(slot3_status);
  lcd.print("   ");

  // Check Entry Barrier Sensor Status
  if (digitalRead(ENTRY_SENSOR) == LOW && !entryTriggered) {
    entryTriggered = true;
    if (available > 0) {
      client.publish("idp/parking/gate", "open"); // Broadcast status: OPEN
      
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("WELCOME");
      lcd.setCursor(0, 1); lcd.print("GATE OPENING");
      
      ledcWriteTone(BUZZER_PIN, 1500); delay(200); ledcWriteTone(BUZZER_PIN, 0);
      gate.write(90); 
      delay(3000); // Hold open gate for 3 seconds
      gate.write(0);
      
      client.publish("idp/parking/gate", "closed"); // Broadcast status: CLOSED
      lcd.clear();
    } else {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("PARKING FULL");
      lcd.setCursor(0, 1); lcd.print("TRY LATER");
      for (int i = 0; i < 3; i++) {
        ledcWriteTone(BUZZER_PIN, 1000); delay(300); ledcWriteTone(BUZZER_PIN, 0); delay(200);
      }
      delay(1500);
      lcd.clear();
    }
  }

  if (digitalRead(ENTRY_SENSOR) == HIGH) { entryTriggered = false; }
  delay(300);
}