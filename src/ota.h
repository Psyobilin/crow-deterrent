#ifndef OTA_H
#define OTA_H

#include <Arduino.h>

// Wird zur Build-Zeit per -DFIRMWARE_VERSION gesetzt (GitHub Action).
// Lokale Builds ohne Flag laufen als "dev".
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

// Muss regelmäßig in loop() aufgerufen werden. Nicht-blockierend, außer im
// Moment einer tatsächlichen Prüfung/eines Updates.
void otaTick();

// Stößt sofort eine Prüfung + ggf. Update an (Dashboard-Knopf).
void otaCheckNow();

// Aktuelle eingebaute Firmware-Version (für die Statusanzeige).
String otaCurrentVersion();

// Kurzer Textstatus fürs Dashboard ("Bereit", "Prüfe...", "Aktuell",
// "Update läuft...", "Fehler").
String otaStatus();

#endif
