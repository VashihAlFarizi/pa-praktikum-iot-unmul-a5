#include <Wire.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ================= WIFI & MQTT =================
#define WIFI_SSID     "E2R"
#define WIFI_PASS     "02MINYUK"
#define MQTT_SERVER   "broker.emqx.io"
#define MQTT_PORT     1883

// ================= TELEGRAM =================
#define TELEGRAM_BOT_TOKEN  "8308292051:AAFX-OlQLEFtsbZUJ3eZo8Fon-U7euHJs3M"
#define TELEGRAM_CHAT_ID    "-5114327615"
#define TELEGRAM_INTERVAL   10000

// ================= TOPIC MQTT =================
#define TOPIC_CONTROL     "ari/pa/control"
#define TOPIC_MODE        "ari/pa/mode"
#define TOPIC_BUZZER      "ari/pa/buzzer"
#define TOPIC_STATUS      "ari/pa/statuslevel"
#define TOPIC_JARAK       "ari/pa/jarakair"
#define TOPIC_HUJAN       "ari/pa/levelhujan"
#define TOPIC_SUBSCRIBE   "ari/pa/#"

// ================= PIN =================
#define TRIG_PIN      33
#define ECHO_PIN      25

#define LEDHijau_PIN  26
#define LEDKuning_PIN 14
#define LEDMerah_PIN  27

#define BUZZER_PIN    32
#define RAIN_PIN      34

// ================= PARAMETER KETINGGIAN AIR =================
#define BATAS_AMAN     7
#define BATAS_WASPADA  4

// ================= PARAMETER SENSOR HUJAN =================
#define HUJAN_LEBAT   2500
#define HUJAN_BIASA   3500

// ================= INTERVAL =================
#define PUBLISH_INTERVAL 1000

// ================= STATE =================
String modeSistem   = "AUTO";
String buzzerManual = "OFF";

// State tracking notifikasi Telegram (mencegah spam)
String kondisiSebelumnya  = "";
String hujanSebelumnya    = "";
unsigned long lastTelegram = 0;

WiFiClient   espClient;
PubSubClient mqttClient(espClient);

unsigned long lastPublish = 0;

// ================= KIRIM PESAN TELEGRAM =================
void kirimTelegram(String pesan) {
  // Cek jeda minimal antar pesan
  if (millis() - lastTelegram < TELEGRAM_INTERVAL) {
    Serial.println("[Telegram] Terlalu cepat, pesan ditunda.");
    return;
  }

  WiFiClientSecure clientSecure;
  clientSecure.setInsecure();

  HTTPClient http;
  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) + "/sendMessage";

  http.begin(clientSecure, url);
  http.addHeader("Content-Type", "application/json");

  pesan.replace("\"", "\\\"");

  String body = "{\"chat_id\":\"" + String(TELEGRAM_CHAT_ID) +
                "\",\"text\":\"" + pesan +
                "\",\"parse_mode\":\"HTML\"}";

  int httpCode = http.POST(body);

  if (httpCode == 200) {
    Serial.println("[Telegram] ✓ Pesan terkirim: " + pesan);
    lastTelegram = millis();
  } else {
    Serial.print("[Telegram] ✗ Gagal, HTTP code: ");
    Serial.println(httpCode);
  }

  http.end();
}

// ================= CEK & KIRIM NOTIFIKASI =================
void cekNotifikasiTelegram(String kondisi, String rainStatus, float jarak) {
  // ── Notifikasi HUJAN LEBAT (tanpa peduli kondisi air) ──
  if (rainStatus == "HUJAN LEBAT" && hujanSebelumnya != "HUJAN LEBAT") {
    String pesan = "<b>WASPADA BANJIR</b>\n"
                   "Hujan sedang lebat di area pemantauan.\n"
                   "Harap waspada dan pantau terus kondisi air!";
    kirimTelegram(pesan);
    hujanSebelumnya = "HUJAN LEBAT";
  }
  else if (rainStatus != "HUJAN LEBAT") {
    hujanSebelumnya = rainStatus;
  }

  // ── Notifikasi BAHAYA ketinggian air ──
  if (kondisi == "BAHAYA" && kondisiSebelumnya != "BAHAYA") {
    String pesan = "<b>BAHAYA BANJIR!</b>\n"
                   "Ketinggian air mencapai <b>" + String(jarak, 1) + " cm</b>\n"
                   "Status: BAHAYA — Segera ambil tindakan!";
    kirimTelegram(pesan);
    kondisiSebelumnya = "BAHAYA";
  }
  else if (kondisi != "BAHAYA") {
    // Kirim notifikasi pemulihan saat kondisi membaik dari BAHAYA
    if (kondisiSebelumnya == "BAHAYA") {
      String pesan = "<b>Kondisi Membaik</b>\n"
                     "Ketinggian air kini: <b>" + String(jarak, 1) + " cm</b>\n"
                     "Status: " + kondisi;
      kirimTelegram(pesan);
    }
    kondisiSebelumnya = kondisi;
  }
}

// ================= WIFI =================
void setupWifi() {
  Serial.print("Menghubungkan ke WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi terhubung!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// ================= MQTT CALLBACK =================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String topicStr = String(topic);
  String pesan = "";

  for (int i = 0; i < length; i++) {
    pesan += (char)payload[i];
  }
  pesan.trim();

  Serial.println("\n[MQTT] Pesan masuk:");
  Serial.print("  Topic : "); Serial.println(topicStr);
  Serial.print("  Pesan : "); Serial.println(pesan);

  if (topicStr == TOPIC_CONTROL) {
    if (pesan == "MODE_AUTO") {
      modeSistem = "AUTO";
      Serial.println("  → Mode diubah ke AUTO");
      mqttClient.publish(TOPIC_MODE, "AUTO");
    }
    else if (pesan == "MODE_MANUAL") {
      modeSistem = "MANUAL";
      Serial.println("  → Mode diubah ke MANUAL");
      mqttClient.publish(TOPIC_MODE, "MANUAL");
    }
    else if (pesan == "BUZZER_ON") {
      if (modeSistem == "MANUAL") {
        buzzerManual = "ON";
        digitalWrite(BUZZER_PIN, HIGH);
        Serial.println("  → Buzzer ON (Manual)");
        mqttClient.publish(TOPIC_BUZZER, "ON");
      } else {
        Serial.println("  → BUZZER_ON diabaikan (mode AUTO)");
        mqttClient.publish(TOPIC_BUZZER, "AUTO");
      }
    }
    else if (pesan == "BUZZER_OFF") {
      if (modeSistem == "MANUAL") {
        buzzerManual = "OFF";
        digitalWrite(BUZZER_PIN, LOW);
        Serial.println("  → Buzzer OFF (Manual)");
        mqttClient.publish(TOPIC_BUZZER, "OFF");
      } else {
        Serial.println("  → BUZZER_OFF diabaikan (mode AUTO)");
        mqttClient.publish(TOPIC_BUZZER, "AUTO");
      }
    }
  }
}

// ================= MQTT RECONNECT =================
void mqttReconnect() {
  while (!mqttClient.connected()) {
    Serial.print("Menghubungkan ke MQTT broker...");
    String clientId = "ari-esp32-" + String(random(0xffff), HEX);

    if (mqttClient.connect(clientId.c_str())) {
      Serial.println(" terhubung!");
      mqttClient.subscribe(TOPIC_SUBSCRIBE);
      Serial.println("Subscribe ke: ari/pa/#");
    } else {
      Serial.print(" gagal, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" coba lagi dalam 3 detik...");
      delay(3000);
    }
  }
}

// ================= SENSOR =================
float bacaJarak() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long durasi = pulseIn(ECHO_PIN, HIGH);
  return durasi * 0.034 / 2;
}

int bacaHujan() {
  return analogRead(RAIN_PIN);
}

// ================= STATUS =================
String statusAir(float jarak) {
  if (jarak > BATAS_AMAN)          return "AMAN";
  else if (jarak >= BATAS_WASPADA) return "WASPADA";
  else                              return "BAHAYA";
}

String statusHujan(int nilaiHujan) {
  if (nilaiHujan <= HUJAN_LEBAT)       return "HUJAN LEBAT";
  else if (nilaiHujan <= HUJAN_BIASA)  return "HUJAN BIASA";
  else                                  return "TIDAK HUJAN";
}

// ================= LOGIKA KONDISI GABUNGAN =================
String kondisiSistem(float jarak, int rainValue) {
  if (jarak < BATAS_WASPADA) {
    return "BAHAYA";
  }
  else if (jarak <= BATAS_AMAN || rainValue <= HUJAN_LEBAT) {
    return "WASPADA";
  }
  else {
    return "AMAN";
  }
}

// ================= AKTUATOR =================
void aktifkanAktuator(String kondisi) {
  digitalWrite(LEDHijau_PIN,  LOW);
  digitalWrite(LEDKuning_PIN, LOW);
  digitalWrite(LEDMerah_PIN,  LOW);

  if (kondisi == "AMAN")         digitalWrite(LEDHijau_PIN,  HIGH);
  else if (kondisi == "WASPADA") digitalWrite(LEDKuning_PIN, HIGH);
  else if (kondisi == "BAHAYA")  digitalWrite(LEDMerah_PIN,  HIGH);

  if (modeSistem == "AUTO") {
    digitalWrite(BUZZER_PIN, (kondisi == "BAHAYA") ? HIGH : LOW);
  }
}

// ================= PUBLISH MQTT =================
void publishData(float jarak, String airStatus,
                 int rainValue, String rainStatus,
                 String kondisi) {

  mqttClient.publish(TOPIC_JARAK,  String(jarak, 1).c_str());
  mqttClient.publish(TOPIC_HUJAN,  (rainStatus + " (" + String(rainValue) + ")").c_str());
  mqttClient.publish(TOPIC_STATUS, kondisi.c_str());
  mqttClient.publish(TOPIC_MODE,   modeSistem.c_str());

  if (modeSistem == "AUTO") {
    mqttClient.publish(TOPIC_BUZZER, (kondisi == "BAHAYA") ? "ON" : "OFF");
  } else {
    mqttClient.publish(TOPIC_BUZZER, buzzerManual.c_str());
  }
}

// ================= SERIAL DASHBOARD =================
void tampilSerial(float jarak, String airStatus,
                  int rainValue, String rainStatus,
                  String kondisi) {
  Serial.println("\n========== Dashboard Sistem ==========");
  Serial.println("----- Mode -----");
  Serial.print("Mode Sistem    : "); Serial.println(modeSistem);
  if (modeSistem == "MANUAL") {
    Serial.print("Buzzer Manual  : "); Serial.println(buzzerManual);
  }
  Serial.println("----- Sensor -----");
  Serial.print("Ketinggian Air : "); Serial.print(jarak); Serial.println(" cm");
  Serial.print("Status Air     : "); Serial.println(airStatus);
  Serial.print("Nilai Hujan    : "); Serial.println(rainValue);
  Serial.print("Status Hujan   : "); Serial.println(rainStatus);
  Serial.println("----- Kondisi Sistem -----");
  Serial.print("KONDISI        : "); Serial.println(kondisi);
  Serial.println("======================================");
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN,      OUTPUT);
  pinMode(ECHO_PIN,      INPUT);
  pinMode(LEDHijau_PIN,  OUTPUT);
  pinMode(LEDKuning_PIN, OUTPUT);
  pinMode(LEDMerah_PIN,  OUTPUT);
  pinMode(BUZZER_PIN,    OUTPUT);
  pinMode(RAIN_PIN,      INPUT);

  digitalWrite(LEDHijau_PIN,  LOW);
  digitalWrite(LEDKuning_PIN, LOW);
  digitalWrite(LEDMerah_PIN,  LOW);
  digitalWrite(BUZZER_PIN,    LOW);

  setupWifi();

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  // Kirim notifikasi sistem online ke Telegram
  kirimTelegram("<b>Sistem Monitoring Banjir Online</b>\nPerangkat siap memantau.");

  Serial.println("Sistem Monitoring Banjir Siap...");
}

// ================= LOOP =================
void loop() {
  if (!mqttClient.connected()) {
    mqttReconnect();
  }
  mqttClient.loop();

  float  jarak     = bacaJarak();
  int    rainValue = bacaHujan();

  String airStatus  = statusAir(jarak);
  String rainStatus = statusHujan(rainValue);
  String kondisi    = kondisiSistem(jarak, rainValue);

  aktifkanAktuator(kondisi);
  tampilSerial(jarak, airStatus, rainValue, rainStatus, kondisi);

  // ── CEK NOTIFIKASI TELEGRAM ──
  cekNotifikasiTelegram(kondisi, rainStatus, jarak);

  if (millis() - lastPublish >= PUBLISH_INTERVAL) {
    publishData(jarak, airStatus, rainValue, rainStatus, kondisi);
    lastPublish = millis();
  }

  delay(1000);
}