// =====================================================================
//  NOEUD PILOTAGE
//  - Bouton poussoir : publie "down" / "up" sur esp32/button1
//  - LED : commande manuelle "on"/"off" sur esp32/output
//          alarme "on"/"off" sur esp32/alarme (prioritaire : clignotement)
// =====================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

const char *ssid        = "iot";
const char *password    = "iotisis;";
const char *mqtt_server = "172.18.32.47";

const int buttonPin = 25;  // D2
const int ledPin    = 26;  // D3

const char *buttonTopic = "esp32/button1";
const char *ledTopic    = "esp32/output";
const char *alarmTopic  = "esp32/alarme";

WiFiClient espClient;
PubSubClient client(espClient);

int buttonState = HIGH;
int lastButtonReading = HIGH;
unsigned long lastButtonChange = 0;
unsigned long lastMqttAttempt = 0;
bool buttonNeedsPublish = true;

bool ledManual   = false;   // état demandé par le dashboard
bool alarmActive = false;   // alarme envoyée par Node-RED
bool blinkState  = false;
unsigned long lastBlink = 0;

void callback(char *topic, byte *message, unsigned int length)
{
  const bool on  = (length == 2 && memcmp(message, "on", 2) == 0);
  const bool off = (length == 3 && memcmp(message, "off", 3) == 0);
  if (!on && !off) {
    return;
  }

  if (strcmp(topic, ledTopic) == 0) {
    ledManual = on;
    Serial.println(on ? "LED on" : "LED off");
  } else if (strcmp(topic, alarmTopic) == 0) {
    alarmActive = on;
    Serial.println(on ? "ALARME ON" : "Alarme off");
  }
}

// Alarme = clignotement rapide, sinon état manuel
void updateLed(unsigned long now)
{
  if (alarmActive) {
    if (now - lastBlink >= 150) {
      lastBlink = now;
      blinkState = !blinkState;
      digitalWrite(ledPin, blinkState ? HIGH : LOW);
    }
  } else {
    digitalWrite(ledPin, ledManual ? HIGH : LOW);
  }
}

void setup()
{
  Serial.begin(115200);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  buttonState = digitalRead(buttonPin);
  lastButtonReading = buttonState;

  WiFi.begin(ssid, password);
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  lastMqttAttempt = millis() - 5000UL;
}

void loop()
{
  const unsigned long now = millis();
  const int reading = digitalRead(buttonPin);

  updateLed(now);

  // Anti-rebond 30 ms
  if (reading != lastButtonReading) {
    lastButtonReading = reading;
    lastButtonChange = now;
  }

  if (reading != buttonState && now - lastButtonChange >= 30) {
    buttonState = reading;
    buttonNeedsPublish = true;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (!client.connected()) {
    if (now - lastMqttAttempt < 5000) {
      return;
    }

    lastMqttAttempt = now;

    if (!client.connect("ESP32Client_Pilotage")) {
      Serial.printf("Connexion MQTT impossible, code %d\n", client.state());
      return;
    }

    client.subscribe(ledTopic);
    client.subscribe(alarmTopic);
    buttonNeedsPublish = true;
    Serial.println("MQTT connecte");
  }

  client.loop();

  if (buttonNeedsPublish) {
    const char *message = buttonState == LOW ? "down" : "up";
    if (client.publish(buttonTopic, message)) {
      buttonNeedsPublish = false;
      Serial.println(message);
    }
  }
}
