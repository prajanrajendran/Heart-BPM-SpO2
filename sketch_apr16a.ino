#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

const char* ssid = "Prajan";
const char* password = "12345678";
const char* thingspeakAPIKey = "HGETWRAXZGFXPKUJ";
const char* serverName = "http://api.thingspeak.com/update";

WebServer server(80);
MAX30105 particleSensor;

#define BUFFER_SIZE 100
uint32_t irBuffer[BUFFER_SIZE];
uint32_t redBuffer[BUFFER_SIZE];

const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;

float beatsPerMinute;
int beatAvg;
int32_t spo2 = 0;
int8_t validSPO2 = 0;

bool measurementStarted = false;
unsigned long lastUploadTime = 0;
const unsigned long uploadInterval = 20000; // 20 seconds

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");

  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", getHTML());
  });

  server.on("/start", HTTP_GET, []() {
    measurementStarted = true;
    server.send(200, "text/plain", "Measurement Started");
    start();
  });

  server.begin();

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 was not found. Please check wiring/power.");
    while (1);
  }

  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);
}

void loop() {
  server.handleClient();
  if (!measurementStarted) return;

  long irValue = particleSensor.getIR();
  long redValue = particleSensor.getRed();

  static int bufferIndex = 0;
  if (bufferIndex < BUFFER_SIZE) {
    irBuffer[bufferIndex] = irValue;
    redBuffer[bufferIndex] = redValue;
    bufferIndex++;
  }

  if (bufferIndex >= BUFFER_SIZE) {
    int32_t hr;
    int8_t hrValid;

    maxim_heart_rate_and_oxygen_saturation(irBuffer, BUFFER_SIZE, redBuffer,  &spo2, &validSPO2, &hr, &hrValid);

    if (hrValid) {
      beatsPerMinute = hr;
      rates[rateSpot++] = hr;
      rateSpot %= RATE_SIZE;
      beatAvg = 0;
      for (byte x = 0; x < RATE_SIZE; x++) beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
    }

    bufferIndex = 0;
  }

  if (millis() - lastUploadTime > uploadInterval && WiFi.status() == WL_CONNECTED) {
    uploadToThingSpeak(beatAvg, validSPO2 ? spo2 : 0);
    lastUploadTime = millis();
  }

  Serial.print("IR="); Serial.print(irValue);
  Serial.print(", BPM="); Serial.print(beatsPerMinute);
  Serial.print(", Avg BPM="); Serial.print(beatAvg);
  Serial.print(", SpO2="); Serial.println(spo2);
}

void start() {
  Serial.println("Measurement Started!");
}

void uploadToThingSpeak(int bpm, int spo2) {
  HTTPClient http;
  String url = String(serverName) + "?api_key=" + thingspeakAPIKey +
               "&field1=" + bpm + "&field2=" + spo2;

  http.begin(url);
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    Serial.println("Data uploaded to ThingSpeak.");
  } else {
    Serial.print("Error uploading data: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

String getHTML() {
  String html = "<!DOCTYPE html><html><head><title>Heart Rate Monitor</title>";
  html += "<style>body { font-family: Arial; text-align: center; background: #f4f4f4; }";
  html += "h1 { color: #333; } h2 { padding: 10px 20px; border-radius: 10px; display: inline-block; }";
  html += ".bpm { background-color: #ff4d4d; color: white; }";
  html += ".spo2 { background-color: #4CAF50; color: white; margin-top: 10px; }";
  html += "button { margin-top: 30px; background: #4CAF50; color: white; padding: 15px 25px;";
  html += "border: none; border-radius: 10px; font-size: 18px; cursor: pointer; }";
  html += "button:hover { background-color: #45a049; }</style></head><body>";
  html += "<h1>ESP32 Heart Rate & SpO2 Monitor</h1>";
  html += "<h2 class='bpm'>Average BPM: " + String(beatAvg) + "</h2><br>";
  html += "<h2 class='spo2'>SpO₂: " + String(validSPO2 ? spo2 : 0) + " %</h2>";
  html += "<br><button onclick=\"startMeasurement()\">Start</button>";
  html += "<script>function startMeasurement() { fetch('/start'); }</script>";
  html += "</body></html>";
  return html;
}
