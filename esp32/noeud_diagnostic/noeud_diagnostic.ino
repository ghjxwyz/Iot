// =====================================================================
//  NOEUD DIAGNOSTIC ("valise de diagnostic")
//  - Maître I2C (IO21 = SDA, IO22 = SCL) relié au noeud principal (0x55)
//  - Affiche l'état du noeud principal sur le Serial Monitor
// =====================================================================

#include <Wire.h>

#define I2C_DEV_ADDR 0x55
#define DIAG_LEN     4      // [code erreur, SpO2, BPM, flags]

#define FLAG_WIFI    0x01
#define FLAG_MQTT    0x02
#define FLAG_CAPTEUR 0x04
#define FLAG_MESURE  0x08

// ---------------- Table des codes erreur (identique au noeud principal) ----------------
#define ERR_NONE              0
#define ERR_WIFI_LOST         1
#define ERR_SENSOR_DISCONNECT 2
#define ERR_SENSOR_READ       3
#define ERR_TEMP_LIMIT        4
#define ERR_LOW_VOLTAGE       5
#define ERR_SERIAL_LOST       6
#define ERR_LOW_MEMORY        7
#define ERR_I2C               8
#define ERR_WATCHDOG          9
#define ERR_UNKNOWN           10
#define ERR_MQTT_LOST         11

const char *getErrorMessage(uint8_t code) {
  switch (code) {
    case ERR_NONE:              return "Fonctionnement nominal : acquisition capteur en cours";
    case ERR_WIFI_LOST:         return "Impossible de se connecter au WiFi";
    case ERR_SENSOR_DISCONNECT: return "Probleme liaison capteur (capteur non connecte)";
    case ERR_SENSOR_READ:       return "Mesure invalide (doigt absent ou mal place)";
    case ERR_TEMP_LIMIT:        return "Temperature hors limites";
    case ERR_LOW_VOLTAGE:       return "Tension alimentation insuffisante";
    case ERR_SERIAL_LOST:       return "Perte communication serie";
    case ERR_LOW_MEMORY:        return "Memoire insuffisante";
    case ERR_I2C:               return "Erreur communication I2C";
    case ERR_WATCHDOG:          return "Watchdog declenche";
    case ERR_UNKNOWN:           return "Erreur systeme inconnue";
    case ERR_MQTT_LOST:         return "Broker MQTT injoignable";
    default:                    return "Code erreur non repertorie";
  }
}

const char *okKo(bool ok) { return ok ? "OK" : "KO"; }

void setup() {
  Serial.begin(115200);
  Wire.begin();   // maître, IO21/IO22
  Serial.println("Noeud diagnostic initialise");
}

void loop() {
  delay(2000);

  uint8_t n = Wire.requestFrom((uint8_t)I2C_DEV_ADDR, (uint8_t)DIAG_LEN);

  Serial.println("========== DIAGNOSTIC NOEUD PRINCIPAL ==========");

  if (n != DIAG_LEN) {
    // Le principal ne répond pas : câble I2C, alimentation ou plantage
    while (Wire.available()) Wire.read();
    Serial.println("Etat    : AUCUNE REPONSE du noeud principal (liaison I2C, alim ou plantage)");
    Serial.println("================================================\n");
    return;
  }

  uint8_t code  = Wire.read();
  uint8_t spo2  = Wire.read();
  uint8_t bpm   = Wire.read();
  uint8_t flags = Wire.read();

  Serial.printf("Etat    : %s\n", getErrorMessage(code));
  Serial.printf("Code    : %u\n", code);
  Serial.printf("WiFi    : %s\n", okKo(flags & FLAG_WIFI));
  Serial.printf("MQTT    : %s\n", okKo(flags & FLAG_MQTT));
  Serial.printf("Capteur : %s\n", (flags & FLAG_CAPTEUR) ? "connecte" : "DEBRANCHE");

  if (flags & FLAG_MESURE) {
    Serial.printf("Mesure  : %u BPM | SpO2 %u %%\n", bpm, spo2);
  } else {
    Serial.println("Mesure  : aucune mesure valide");
  }
  Serial.println("================================================\n");
}
