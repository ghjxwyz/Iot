#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

const char *ssid = "iot";
const char *password = "iotisis;";
const char *mqtt_server = "172.18.32.47";

const int buttonPin = 25;  // D2
const int ledPin = 26;     // D3

const char *buttonTopic = "esp32/button1";
const char *ledTopic = "esp32/output";

WiFiClient espClient;
PubSubClient client(espClient);

int buttonState = HIGH;
int lastButtonReading = HIGH;
unsigned long lastButtonChange = 0;
unsigned long lastMqttAttempt = 0;
bool buttonNeedsPublish = true;

void callback(char *topic, byte *message, unsigned int length)
{
  if (strcmp(topic, ledTopic) != 0) {
    return;
  }

  if (length == 2 && memcmp(message, "on", 2) == 0) {
    digitalWrite(ledPin, HIGH);
    Serial.println("LED on");
  } else if (length == 3 && memcmp(message, "off", 3) == 0) {
    digitalWrite(ledPin, LOW);
    Serial.println("LED off");
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

