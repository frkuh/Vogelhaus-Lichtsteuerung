#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include "secrets.h" // Lädt die WLAN-Daten aus der separaten Datei

// --- GITHUB UPDATE URL ---
// Ersetzen Sie "frkuh" und "Vogelhaus-Lichtsteuerung" mit Ihren echten Daten
const char* firmwareUrl = "https://github.com/frkuh/Vogelhaus-Lichtsteuerung/releases/download/V_0.0.1/firmware.bin";

// Test-Intervall: Alle 30 Sekunden prüfen (Später für 5 Min ändern in: 5 * 60 * 1000)
const unsigned long updateInterval = 30000; 
unsigned long lastCheckTime = 0;

const String currentVersion = "V_0.0.1"; 
#define LED_BUILTIN 21 // Pin für das Seeed XIAO ESP32-S3

void setup() {
  Serial.begin(115200);
  delay(3000); // Kurz warten, bis der serielle Monitor bereit ist
  
  pinMode(LED_BUILTIN, OUTPUT);
  
  Serial.println("\n--- ESP32-S3 OTA System gestartet ---");
  Serial.print("Aktuelle Version: ");
  Serial.println(currentVersion);

  // WLAN Verbindung herstellen
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Verbinde mit WLAN");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempts++;
    if (attempts > 40) {
      Serial.println("\nWLAN fehlgeschlagen. Neustart...");
      ESP.restart();
    }
  }

  Serial.println("\nWLAN verbunden!");
}

void loop() {
  // Kleine Blink-Animation, damit man sieht, dass der Loop läuft
  digitalWrite(LED_BUILTIN, LOW);   // LED An (Active-Low beim XIAO)
  delay(200);
  digitalWrite(LED_BUILTIN, HIGH);  // LED Aus
  delay(800);

  unsigned long currentMillis = millis();

  // Prüfen, ob das Intervall abgelaufen ist
  if (currentMillis - lastCheckTime >= updateInterval || lastCheckTime == 0) {
    lastCheckTime = currentMillis;
    
    if (WiFi.status() == WL_CONNECTED) {
      checkForUpdate();
    } else {
      Serial.println("WLAN getrennt! Reconnect...");
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }
}

void checkForUpdate() {
  Serial.println("\n[OTA] Prüfe auf Updates von GitHub...");

  HTTPClient http;
  http.begin(firmwareUrl);
  http.setTimeout(10000);

  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    Serial.printf("[OTA] Update gefunden! Größe: %d Bytes\n", contentLength);

    if (contentLength > 0) {
      bool canBegin = Update.begin(contentLength);
      
      if (canBegin) {
        Serial.println("[OTA] Starte Firmware-Update...");
        
        WiFiClient* client = http.getStreamPtr();
        size_t written = Update.writeStream(*client);

        if (written == contentLength) {
          Serial.printf("[OTA] Erfolgreich geschrieben: %u Bytes\n", written);
        } else {
          Serial.printf("[OTA] Fehler: Nur %u von %u Bytes geschrieben.\n", written, contentLength);
        }

        if (Update.end()) {
          Serial.println("[OTA] Update erfolgreich abgeschlossen!");
          if (Update.isFinished()) {
            Serial.println("[OTA] Starte ESP32 neu...");
            ESP.restart();
          }
        } else {
          Serial.printf("[OTA] Update-Fehler Code: %d\n", Update.getError());
        }
      } else {
        Serial.println("[OTA] Nicht genügend Speicherplatz.");
      }
    }
  } else {
    Serial.printf("[OTA] Kein Update / HTTP Fehler: %d\n", httpCode);
  }

  http.end();
}
