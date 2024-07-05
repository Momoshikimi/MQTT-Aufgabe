#include <WiFi.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <Adafruit_ILI9341.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// WiFi-Konfigurationsdaten
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// MQTT-Konfigurationsdaten
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
const char* topic = "sensor/daten";

#define TFT_CS    5
#define TFT_RST   14
#define TFT_DC    15
#define TFT_SCLK  33
#define TFT_MOSI  35
#define TFT_MISO  17

#define DHTPIN 4
#define DHTTYPE DHT22
const int sensorPin = 2;

DHT dht(DHTPIN, DHTTYPE);
StaticJsonDocument<200> jsonData;
WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000);

// WiFi-Verbindung initialisieren
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Verbinden mit ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi verbunden.");
  Serial.print("IP Adresse: ");
  Serial.println(WiFi.localIP());
}

// Callback-Funktion für MQTT-Nachrichten
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Nachricht angekommen [");
  Serial.print(topic);
  Serial.print("]: ");
  
  // Payload in einen String umwandeln
  char message[length + 1];
  for (unsigned int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';
  Serial.println(message);

  // JSON-Daten deserialisieren und ausgeben
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, message);
  if (error) {
    Serial.print("Fehler beim Deserialisieren: ");
    Serial.println(error.f_str());
    return;
  }

  // Ausgabe der deserialisierten Daten (optional)
  const char* schule = doc["schule"];
  const char* bau = doc["bau"];
  const char* stock = doc["stock"];
  const char* raum = doc["raum"];
  float temperatur = doc["temperatur"];
  float luftfeuchtigkeit = doc["luftfeuchtigkeit"];
  bool anwesenheit = doc["anwesenheit"];
  const char* zeit = doc["zeit"];

  Serial.print("Schule: ");
  Serial.println(schule);
  Serial.print("Bau: ");
  Serial.println(bau);
  Serial.print("Stock: ");
  Serial.println(stock);
  Serial.print("Raum: ");
  Serial.println(raum);
  Serial.print("Temperatur: ");
  Serial.println(temperatur);
  Serial.print("Luftfeuchtigkeit: ");
  Serial.println(luftfeuchtigkeit);
  Serial.print("Anwesenheit: ");
  Serial.println(anwesenheit ? "Ja" : "Nein");
  Serial.print("Zeit: ");
  Serial.println(zeit);
}

// Verbindung zum MQTT-Server herstellen
void reconnect() {
  while (!client.connected()) {
    Serial.print("Verbinden mit MQTT-Server ...");
    if (client.connect("ESP32Client")) {
      Serial.println("verbunden");

      // Auf das MQTT-Thema abonnieren
      client.subscribe(topic);
    } else {
      Serial.print("Fehler, rc=");
      Serial.print(client.state());
      Serial.println(" in 5 Sekunden erneut versuchen");
      delay(5000);
    }
  }
}

// Setup-Funktion für Initialisierung
void setup() {
  pinMode(sensorPin, INPUT);
  tft.begin();
  tft.setFont();
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(10, 10);
  Serial.begin(9600);
  dht.begin();
  delay(2000);
  Serial.println("Setup abgeschlossen. Sensoren initialisiert.");
  
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  timeClient.begin();
}

// Variablen für Zeitsteuerung und Bewegungserkennung
int timeBewegung = 11;
int timeSendung = 0;
bool Bewegung = false;

// Loop-Funktion für die Hauptausführung
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  timeClient.update();

  delay(1000);

  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Fehler beim Lesen des DHT-Sensors!");
    return;
  }
  
  Serial.print("Luftfeuchtigkeit: ");
  Serial.print(humidity);
  Serial.print("%, Temperatur: ");
  Serial.print(temperature);
  Serial.println("°C");

  if (digitalRead(sensorPin) == HIGH) {
    if (timeBewegung == 10) {
      timeBewegung = 0; 
      Bewegung = true;
      Serial.println("Bewegung erkannt.");
    } else {
      timeBewegung++; 
      Bewegung = true;
    }
  } else {
    Bewegung = false;
    timeBewegung = 0;
    Serial.println("Keine Bewegung.");
  }
  
  if (timeSendung == 60) {
    String jsonString = createJson(humidity, temperature, Bewegung);

    if (client.publish(topic, jsonString.c_str())) {
      Serial.println("Daten erfolgreich gesendet.");
    } else {
      Serial.println("Fehler beim Senden der Daten.");
    }

    displayData(jsonString);

    timeSendung = 0;
  } else {
    timeSendung++;
  }
}

void displayData(String jsonString) {
  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(10, 10);

  StaticJsonDocument<200> jsonData;
  deserializeJson(jsonData, jsonString);

  tft.println("Schule, Bau und Raum: " + String(jsonData["schule"].as<const char*>()) + " " + String(jsonData["bau"].as<const char*>()) + " " + String(jsonData["raum"].as<const char*>()));

  tft.println("Zeit: " + String(jsonData["zeit"].as<const char*>()));

  if (jsonData["anwesenheit"]) {
    tft.println("Anwesenheit: Erkannt -> Licht an");
  } else {
    tft.println("Anwesenheit: Nicht erkannt -> Licht aus");
  }

  if (jsonData["temperatur"] >= 25) {
    tft.println(String(jsonData["temperatur"].as<float>()) + "° -> Klimaanlage an");
  } else {
    tft.println(String(jsonData["temperatur"].as<float>()) + "° -> Klimaanlage aus");
  }
  delay(2000);
}

String createJson(float humidity, float temperature, bool Bewegung) {
  jsonData["schule"] = "HWS";
  jsonData["bau"] = "C";
  jsonData["stock"] = "1.OG";
  jsonData["raum"] = "107";
  jsonData["temperatur"] = temperature;
  jsonData["luftfeuchtigkeit"] = humidity;
  jsonData["anwesenheit"] = Bewegung;
  jsonData["zeit"] = timeClient.getFormattedTime();

  String output;
  serializeJson(jsonData, output);
  return output;
}
