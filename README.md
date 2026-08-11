# Vogelhaus-Lichtsteuerung
ESP32C6 Code for smart Vogelhaus-Beleuchtung

/*
 Vogelhaus - ESP32-C6 port (synchronous WebServer + Web OTA + ArduinoOTA)
 1.5_Vogelhaus_FinalVer._4_ESP32C6_09.05.26_IP10.20.1.22_MissionProject_mitSleep_mitPowerConsumption_opimized_withMOSFET_mitWebSeiteInactive_mitNachtruhe_mitOTA
 _Nun Abends dauerhaft an, kein Server Sleep mehr_Optimierung an bleiben abends, wenn Licht angeht_Fading beim Einschalten von Helligkeit und Farbe
 _ESP Uhrzeit im Webbrowser, Sommer und WinterZeitumstellung passiert automatisch, Täglich wird maximale Reading erfasst und Abends entsprechend angepasst.
 
 Mit Neopixel WS2812
 IP: 10.20.1.22
 Lichtfarben: 233,32,169
 Einstellung Schwellwert für Licht start Stop in Zeile 221.            Aktuell 2400
 Einstellung Schwellwert für ESP Sleep oder nicht. Zeile: 826 UND 938. Aktuell 2900
 Einstellung Schwellwert Nachtruhe Zeile 936.                          Aktuell 23:30
 
 LDR PIN: 2
 RGB LED Stuerungs PIN: 22
 MOSFET Steuerungs PIN: 23
 WAKE UP PIN: 1
 
 Last up date: 19.07.2026  // Thresholds geändet into Automatik-Berechnungn
 Version: V8
 ESP BoardVersion: 3.3.2 / 3.3.8
 IDE Version: 1.8.13 / (old Arduino IDE)

 Fehlercodes:
 Unexpected response from device: '944' -> Funktioniert trotzdem

*/
