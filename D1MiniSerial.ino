/**
 * @file D1Mini_MowerBridge.ino
 * @brief Bridge tra WiFi e MowerArduino per D1 Mini
 *
 * Questo sketch permette a un D1 Mini di funzionare come bridge tra la rete WiFi
 * e il sistema MowerArduino, inoltrando comandi e ricevendo dati di telemetria.
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <Ticker.h>
#include <SoftwareSerial.h>  // Aggiunta della libreria SoftwareSerial

//#define LED_BUILTIN D4 // LED integrato su D1 Mini

// Dichiarazioni delle funzioni
void connectToWiFi();
void setupWebServer();
void handleRoot();
void handleNotFound();
void handleGetStatus();
void handleStart();
void handleStop();
void handleDock();
void handleForward();
void handleBackward();
void handleLeft();
void handleRight();
void readFromMower();
void sendCommandToMower(const String& command);

// Funzioni MQTT
void setupMqtt();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void mqttReconnect();
void publishMqttDiscovery();
void publishMqttState();
void mqttPublish(const char* topic, const char* payload, bool retain = false);

// Configurazione WiFi
const char* ssid = "***REMOVED***";
const char* password = "***REMOVED***";
const char* host = "mowerbridge";  // Nome host mDNS

// Configurazione MQTT
const char* mqtt_server = "***REMOVED***";  // O l'IP del tuo server MQTT
const int mqtt_port = 1883;
const char* mqtt_user = "***REMOVED***";      // Sostituisci con le tue credenziali
const char* mqtt_password = "***REMOVED***";
const char* mqtt_client_id = "d1mini_mower";
const char* mqtt_base_topic = "home/mower";

// Oggetti per MQTT
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Ticker mqttReconnectTimer;
bool shouldSaveConfig = false;
unsigned long lastMqttReconnectAttempt = 0;
const unsigned long mqttReconnectInterval = 5000;  // 5 secondi

// Configurazione server web
ESP8266WebServer server(80);

// Configurazione seriale per comunicazione con MowerArduino
#define SERIAL_BAUDRATE 115200
#define SERIAL_TIMEOUT 1000
#define RX_PIN D2  // Pin RX per SoftwareSerial
#define TX_PIN D3  // Pin TX per SoftwareSerial

// Inizializza SoftwareSerial
SoftwareSerial mowerSerial(RX_PIN, TX_PIN);  // RX, TX

// Buffer per il parsing JSON
StaticJsonDocument<1024> jsonDoc;

// Stato del tagliaerba
struct MowerStatus {
  bool isRunning = false;
  bool isCharging = false;
  float batteryVoltage = 0.0;
  float batteryPercentage = 0.0;
  String currentState = "unknown";
  unsigned long lastUpdate = 0;
} mowerStatus;

// ====== MAPPATURA COMANDI VECCHI -> NUOVI ======
String translateCommand(const String& legacyCmd) {
  String cmd = legacyCmd;
  cmd.toUpperCase();
  if (cmd == "START") return "startMowing";
  if (cmd == "STOP") return "stopMowing";
  if (cmd == "DOCK") return "returnToBase";
  if (cmd == "FORWARD") return "moveForward";
  if (cmd == "BACKWARD") return "moveBackward";
  if (cmd == "LEFT") return "turnLeft";
  if (cmd == "RIGHT") return "turnRight";
  // default: pass through unchanged
  return legacyCmd;
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // LED spento all'avvio
  // Inizializza la seriale per il debug
  Serial.begin(115200);
  
  // Inizializza SoftwareSerial
  mowerSerial.begin(SERIAL_BAUDRATE);
  mowerSerial.setTimeout(SERIAL_TIMEOUT);
  Serial.println("SoftwareSerial avviato su RX:" + String(RX_PIN) + " TX:" + String(TX_PIN));

  // Connessione al WiFi
  connectToWiFi();

  // Configurazione server web
  setupWebServer();

  // Avvia il server
  server.begin();
  Serial.println("Server HTTP avviato");

  // Avvia mDNS
  if (MDNS.begin(host)) {
    Serial.println("mDNS responder avviato");
    Serial.print("Puoi aprire http://");
    Serial.print(host);
    Serial.println(".local nel tuo browser");
  }
  
  // Configura MQTT
  setupMqtt();

  Serial.println("Pronto!");
}

void loop() {
  // Gestisci le richieste del client
  server.handleClient();
  MDNS.update();
  
  // Leggi i dati dal tagliaerba
  readFromMower();
  
  // Gestisci MQTT
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastMqttReconnectAttempt > mqttReconnectInterval) {
      lastMqttReconnectAttempt = now;
      mqttReconnect();
    }
  } else {
    mqttClient.loop();
  }
  
  // Pubblica lo stato su MQTT ogni 30 secondi
  static unsigned long lastMqttUpdate = 0;
  if (millis() - lastMqttUpdate > 30000) {
    lastMqttUpdate = millis();
    if (mqttClient.connected()) {
      publishMqttState();
    }
  }
}

// Connessione alla rete WiFi
void connectToWiFi() {
  Serial.print("Connessione a ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connesso");
  Serial.print("Indirizzo IP: ");
  Serial.println(WiFi.localIP());
}

// Configurazione del server web
// Gestione richieste root
void handleRoot() {
  String html = R"=====(
<!DOCTYPE html>
<html lang="it">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Mower Control Panel</title>
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.1.3/dist/css/bootstrap.min.css" rel="stylesheet">
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/bootstrap-icons@1.10.5/font/bootstrap-icons.css">
    <link rel="stylesheet" href="https://unpkg.com/leaflet@1.7.1/dist/leaflet.css" />
    <script src="https://unpkg.com/leaflet@1.7.1/dist/leaflet.js"></script>
    <style>
        body { padding-top: 2rem; background-color: #f8f9fa; }
        .card { margin-bottom: 1.5rem; box-shadow: 0 0.125rem 0.25rem rgba(0,0,0,0.075); }
        .status-card { border-left: 5px solid #0d6efd; }
        .battery-card { border-left: 5px solid #198754; }
        .map-container { height: 400px; border-radius: 0.25rem; overflow: hidden; }
        #map { height: 100%; width: 100%; }
        .btn-action { min-width: 100px; margin: 0 5px; }
.btn-direction {
    width: 56px;
    height: 56px;
    font-size: 1.5rem;
    padding: 0;
    border-radius: 50%;
    display: flex;
    align-items: center;
    justify-content: center;
}
    </style>
</head>
<body>
    <div class="container">
        <h1 class="mb-4">🏠 Controllo Tagliaerba</h1>
        
        <div class="row">
            <!-- Stato -->
            <div class="col-12">
                <div class="card status-card">
                    <div class="card-header d-flex justify-content-between align-items-center">
                        <h5 class="card-title mb-0">Stato del Tagliaerba</h5>
                        <span id="lastUpdate" class="text-muted small">Mai aggiornato</span>
                    </div>
                    <div class="card-body text-center">
                        <h2 id="currentState" class="mb-4"><span class="badge bg-secondary">Sconosciuto</span></h2>
                        
                        <div class="row">
                            <div class="col-6">
                                <div class="card mb-3">
                                    <div class="card-body">
                                        <h6 class="card-subtitle mb-2 text-muted">Batteria</h6>
                                        <div class="d-flex justify-content-between align-items-center">
                                            <h4 class="mb-0"><span id="batteryPercentage">0</span>%</h4>
                                            <span id="batteryVoltage" class="text-muted">0.0V</span>
                                        </div>
                                        <div class="progress mt-2" style="height: 10px;">
                                            <div id="batteryProgress" class="progress-bar" role="progressbar" style="width: 0%;" 
                                                aria-valuenow="0" aria-valuemin="0" aria-valuemax="100"></div>
                                        </div>
                                    </div>
                                </div>
                            </div>
                            <div class="col-6">
                                <div class="card">
                                    <div class="card-body">
                                        <h6 class="card-subtitle mb-2 text-muted">Stato</h6>
                                        <div class="btn-group w-100" role="group">
                                            <button id="btnStart" class="btn btn-success btn-action">Avvia</button>
                                            <button id="btnStop" class="btn btn-danger btn-action">Ferma</button>
                                            <button id="btnDock" class="btn btn-warning btn-action">Richiama</button>
                                        </div>
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>
    </div>

    <!-- Mappa -->
    <div class="container">
        <div class="map-container mb-4 position-relative">
            <!-- Pulsanti Direzionali sovrapposti -->
            <div id="direction-controls" style="position:absolute; top:12px; right:12px; z-index:1000;">
                <div class="d-flex flex-column align-items-center">
                    <button id="btnForward" class="btn btn-primary btn-direction mb-1"><i class="bi bi-arrow-up"></i></button>
                    <div class="d-flex">
                        <button id="btnLeft" class="btn btn-primary btn-direction me-1"><i class="bi bi-arrow-left"></i></button>
                        <button id="btnRight" class="btn btn-primary btn-direction ms-1"><i class="bi bi-arrow-right"></i></button>
                    </div>
                    <button id="btnBackward" class="btn btn-primary btn-direction mt-1"><i class="bi bi-arrow-down"></i></button>
                </div>
            </div>
            <div id="map"></div>
        </div>
    </div>

    <script>
    //<![CDATA[
    // Inizializzazione variabili
    var updateInterval;

    // Aggiorna i dati della pagina
    var updateData = function() {
        fetch('/api/status')
            .then(function(response) {
                return response.json();
            })
            .then(function(data) {
                // Aggiorna stato
                document.getElementById('currentState').textContent = data.currentState;
                document.getElementById('currentState').className = 'badge ' + 
                    (data.isRunning ? 'bg-success' : data.isCharging ? 'bg-warning' : 'bg-secondary');
                
                // Aggiorna batteria
                document.getElementById('batteryVoltage').textContent = data.batteryVoltage.toFixed(1) + 'V';
                document.getElementById('batteryPercentage').textContent = data.batteryPercentage.toFixed(0);
                var batteryProgress = document.getElementById('batteryProgress');
                batteryProgress.style.width = data.batteryPercentage + '%';
                batteryProgress.className = 'progress-bar ' +
                    (data.batteryPercentage > 50 ? 'bg-success' : data.batteryPercentage > 20 ? 'bg-warning' : 'bg-danger');
                
                // Aggiorna ultimo aggiornamento
                var now = new Date();
                document.getElementById('lastUpdate').textContent = 'Ultimo aggiornamento: ' + now.toLocaleTimeString();
                // Per ora non aggiorniamo marker/lat/lng (manca dato reale)
                // if (typeof marker !== 'undefined' && typeof map !== 'undefined' && typeof lat !== 'undefined' && typeof lng !== 'undefined') {
                //     marker.setLatLng([lat, lng]);
                //     map.panTo([lat, lng]);
                // }
            })
            .catch(function(error) {
                console.error('Errore durante l\'aggiornamento dei dati:', error);
            });
    }

    // Gestione pulsanti
    document.getElementById('btnStart').addEventListener('click', function() { fetch('/api/start'); });
    document.getElementById('btnStop').addEventListener('click', function() { fetch('/api/stop'); });
    document.getElementById('btnDock').addEventListener('click', function() { fetch('/api/dock'); });
    // Pulsanti direzionali
    document.getElementById('btnForward').addEventListener('click', function() { fetch('/api/forward'); });
    document.getElementById('btnBackward').addEventListener('click', function() { fetch('/api/backward'); });
    document.getElementById('btnLeft').addEventListener('click', function() { fetch('/api/left'); });
    document.getElementById('btnRight').addEventListener('click', function() { fetch('/api/right'); });

    // Inizializzazione
    var map, marker;
    function initMap() {
        map = L.map('map').setView([41.9028, 12.4964], 13); // Roma
        L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
            attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
        }).addTo(map);
        marker = L.marker([41.9028, 12.4964]).addTo(map)
            .bindPopup('Posizione fittizia').openPopup();
    }
    document.addEventListener('DOMContentLoaded', function() {
        initMap();
        updateData();
        updateInterval = setInterval(updateData, 5000); // Aggiorna ogni 5 secondi
    });

    // Pulizia all'uscita
    window.addEventListener('beforeunload', function() {
        if (updateInterval) clearInterval(updateInterval);
    });
    //]]>
    </script>
</body>
</html>
    )=====";

    server.send(200, "text/html", html);
}

// Gestione 404
void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

void setupWebServer() {
  // Pagina principale
  server.on("/", HTTP_GET, handleRoot);
  
  // Gestione file statici (CSS/JS esterni)
  server.onNotFound(handleNotFound);

  // Comandi API
  server.on("/api/status", HTTP_GET, handleGetStatus);
  server.on("/api/start", HTTP_GET, handleStart);
  server.on("/api/stop", HTTP_GET, handleStop);
  server.on("/api/dock", HTTP_GET, handleDock);
  server.on("/api/forward", HTTP_GET, handleForward);
  server.on("/api/backward", HTTP_GET, handleBackward);
  server.on("/api/left", HTTP_GET, handleLeft);
  server.on("/api/right", HTTP_GET, handleRight);

  server.begin();
  Serial.println("Server HTTP avviato");
}

// Gestione richieste API
void handleGetStatus() {
  String json = "{\"isRunning\":" + String(mowerStatus.isRunning ? "true" : "false") +
                ",\"isCharging\":" + String(mowerStatus.isCharging ? "true" : "false") +
                ",\"batteryVoltage\":" + String(mowerStatus.batteryVoltage, 2) +
                ",\"batteryPercentage\":" + String(mowerStatus.batteryPercentage, 1) +
                ",\"currentState\":\"" + mowerStatus.currentState + "\"";
  
  // Aggiungi timestamp
  unsigned long currentTime = millis();
  json += ",\"timestamp\":" + String(currentTime) + 
           ",\"uptime\":" + String(currentTime / 1000) + 
           ",\"rssi\":" + String(WiFi.RSSI()) + 
           ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  
  json += "}";
  
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
}

void handleStart() {
    sendCommandToMower("START");
    mowerStatus.isRunning = true;
    mowerStatus.currentState = "Avviato";
    mowerStatus.lastUpdate = millis();
  
    String json = "{\"status\":\"ok\",\"message\":\"Avvio richiesto\",\"state\":\"" + mowerStatus.currentState + "\"}";
    server.send(200, "application/json", json);
  
    Serial.println("Richiesto avvio tagliaerba");
}

void handleStop() {
    sendCommandToMower("STOP");
    mowerStatus.isRunning = false;
    mowerStatus.currentState = "Fermo";
    mowerStatus.lastUpdate = millis();
  
    String json = "{\"status\":\"ok\",\"message\":\"Arresto richiesto\",\"state\":\"" + mowerStatus.currentState + "\"}";
    server.send(200, "application/json", json);
  
    Serial.println("Richiesto arresto tagliaerba");
}

void handleDock() {
    sendCommandToMower("DOCK");
    mowerStatus.isRunning = false;
    mowerStatus.isCharging = true;
    mowerStatus.currentState = "Ritorno alla base";
    mowerStatus.lastUpdate = millis();
  
    String json = "{\"status\":\"ok\",\"message\":\"Ritorno alla base richiesto\",\"state\":\"" + mowerStatus.currentState + "\"}";
    server.send(200, "application/json", json);
  
    Serial.println("Richiesto ritorno alla base");
}

// Handler per i comandi di direzione
void handleForward() {
    sendCommandToMower("FORWARD");
    mowerStatus.currentState = "Avanti";
    mowerStatus.lastUpdate = millis();
    String json = "{\"status\":\"ok\",\"message\":\"Avanti richiesto\",\"state\":\"" + mowerStatus.currentState + "\"}";
    server.send(200, "application/json", json);
    Serial.println("Richiesto movimento avanti");
}

void handleBackward() {
    sendCommandToMower("BACKWARD");
    mowerStatus.currentState = "Indietro";
    mowerStatus.lastUpdate = millis();
    String json = "{\"status\":\"ok\",\"message\":\"Indietro richiesto\",\"state\":\"" + mowerStatus.currentState + "\"}";
    server.send(200, "application/json", json);
    Serial.println("Richiesto movimento indietro");
}

void handleLeft() {
    sendCommandToMower("LEFT");
    mowerStatus.currentState = "Sinistra";
    mowerStatus.lastUpdate = millis();
    String json = "{\"status\":\"ok\",\"message\":\"Sinistra richiesta\",\"state\":\"" + mowerStatus.currentState + "\"}";
    server.send(200, "application/json", json);
    Serial.println("Richiesto movimento sinistra");
}

void handleRight() {
    sendCommandToMower("RIGHT");
    mowerStatus.currentState = "Destra";
    mowerStatus.lastUpdate = millis();
    String json = "{\"status\":\"ok\",\"message\":\"Destra richiesta\",\"state\":\"" + mowerStatus.currentState + "\"}";
    server.send(200, "application/json", json);
    Serial.println("Richiesto movimento destra");
}

// Invia un comando al tagliaerba
void sendCommandToMower(const String& command) {
  digitalWrite(LED_BUILTIN, LOW);   // LED acceso (TX)
  delay(10); // breve lampeggio visibile
  jsonDoc.clear();
  jsonDoc["cmd"] = translateCommand(command);
  jsonDoc["timestamp"] = millis();

  String jsonString;
  serializeJson(jsonDoc, jsonString);
  mowerSerial.println(jsonString);
  digitalWrite(LED_BUILTIN, HIGH);  // LED spento
  Serial.println("Comando inviato: " + command);
  
  // Pubblica il comando su MQTT
  if (mqttClient.connected()) {
    String topic = String(mqtt_base_topic) + "/command";
    mqttPublish(topic.c_str(), command.c_str(), true);
  }
}

// ===== Funzioni MQTT =====

void setupMqtt() {
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1024);
  mqttReconnect();
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Messaggio ricevuto su topic: ");
  Serial.println(topic);
  
  // Converti il payload in una stringa
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  
  String topicStr = String(topic);
  String messageStr = String(message);
  
  // Gestisci i comandi
  if (topicStr.endsWith("/set")) {
    if (messageStr == "START") {
      sendCommandToMower("START");
      mowerStatus.isRunning = true;
      mowerStatus.currentState = "Avviato";
    } else if (messageStr == "STOP") {
      sendCommandToMower("STOP");
      mowerStatus.isRunning = false;
      mowerStatus.currentState = "Fermato";
    } else if (messageStr == "DOCK") {
      sendCommandToMower("DOCK");
      mowerStatus.isRunning = false;
      mowerStatus.isCharging = true;
      mowerStatus.currentState = "Richiamo alla base";
    }
  }
}

void mqttReconnect() {
  if (mqttClient.connected()) {
    return;
  }
  
  Serial.print("Connessione a MQTT...");
  
  if (mqttClient.connect(mqtt_client_id, mqtt_user, mqtt_password)) {
    Serial.println("connesso");
    
    // Iscriviti ai topic di comando
    String commandTopic = String(mqtt_base_topic) + "/set";
    mqttClient.subscribe(commandTopic.c_str());
    
    // Pubblica la configurazione Home Assistant
    publishMqttDiscovery();
    
    // Pubblica lo stato iniziale
    publishMqttState();
  } else {
    Serial.print("fallito, rc=");
    Serial.print(mqttClient.state());
    Serial.println(" riprovo tra 5 secondi");
  }
}

void publishMqttDiscovery() {
  if (!mqttClient.connected()) return;
  
  String deviceConfig = "{\"identifiers\":[\"" + String(mqtt_client_id) + "\"],"
                     "\"name\":\"Tagliaerba Robotico\","
                     "\"manufacturer\":\"D1MiniSerial\"}";
  
  // Pubblica la configurazione per lo switch
  String switchTopic = String("homeassistant/switch/") + mqtt_client_id + "/config";
  String switchConfig = "{\"name\":\"Tagliaerba\","
                      "\"command_topic\":\"" + String(mqtt_base_topic) + "/set\","
                      "\"state_topic\":\"" + String(mqtt_base_topic) + "/state\","
                      "\"payload_on\":\"START\","
                      "\"payload_off\":\"STOP\","
                      "\"state_on\":\"RUNNING\","
                      "\"state_off\":\"STOPPED\","
                      "\"device\":" + deviceConfig + "}";
  mqttPublish(switchTopic.c_str(), switchConfig.c_str(), true);
  
  // Pubblica la configurazione per il sensore di batteria
  String batteryTopic = String("homeassistant/sensor/") + mqtt_client_id + "_battery/config";
  String batteryConfig = "{\"name\":\"Tagliaerba Batteria\","
                       "\"device_class\":\"battery\","
                       "\"unit_of_measurement\":\"%\","
                       "\"state_topic\":\"" + String(mqtt_base_topic) + "/state\","
                       "\"value_template\":\"{{ value_json.battery_percentage }}\","
                       "\"device\":" + deviceConfig + "}";
  mqttPublish(batteryTopic.c_str(), batteryConfig.c_str(), true);
  
  // Pubblica la configurazione per il sensore di tensione
  String voltageTopic = String("homeassistant/sensor/") + mqtt_client_id + "_voltage/config";
  String voltageConfig = "{\"name\":\"Tagliaerba Tensione\","
                       "\"device_class\":\"voltage\","
                       "\"unit_of_measurement\":\"V\","
                       "\"state_topic\":\"" + String(mqtt_base_topic) + "/state\","
                       "\"value_template\":\"{{ value_json.battery_voltage }}\","
                       "\"device\":" + deviceConfig + "}";
  mqttPublish(voltageTopic.c_str(), voltageConfig.c_str(), true);
  
  // Pubblica la configurazione per il sensore di stato
  String stateTopic = String("homeassistant/sensor/") + mqtt_client_id + "_state/config";
  String stateConfig = "{\"name\":\"Tagliaerba Stato\","
                     "\"state_topic\":\"" + String(mqtt_base_topic) + "/state\","
                     "\"value_template\":\"{{ value_json.state }}\","
                     "\"device\":" + deviceConfig + "}";
  mqttPublish(stateTopic.c_str(), stateConfig.c_str(), true);
}

void publishMqttState() {
  if (!mqttClient.connected()) return;
  
  DynamicJsonDocument doc(512);
  doc["state"] = mowerStatus.isRunning ? "RUNNING" : "STOPPED";
  doc["battery_voltage"] = mowerStatus.batteryVoltage;
  doc["battery_percentage"] = mowerStatus.batteryPercentage;
  doc["status"] = mowerStatus.currentState;
  doc["last_update"] = mowerStatus.lastUpdate;
  
  String stateJson;
  serializeJson(doc, stateJson);
  
  String stateTopic = String(mqtt_base_topic) + "/state";
  mqttPublish(stateTopic.c_str(), stateJson.c_str(), true);
}

void mqttPublish(const char* topic, const char* payload, bool retain) {
  if (mqttClient.connected()) {
    mqttClient.publish(topic, payload, retain);
  }
}

// Legge i dati dal tagliaerba
void readFromMower() {
  while (mowerSerial.available()) {
    digitalWrite(LED_BUILTIN, LOW);   // LED acceso (RX)
    String line = mowerSerial.readStringUntil('\n');
    delay(10); // breve lampeggio visibile
    digitalWrite(LED_BUILTIN, HIGH);  // LED spento
    if (line.length() > 0) {  
      Serial.print("Ricevuto: ");
      Serial.println(line);
      
      // Esempio: STATUS:RUNNING,BATT:24.5,PERCENT:75
      if (line.startsWith("STATUS:")) {
        if (line.indexOf("RUNNING") != -1) {
          mowerStatus.isRunning = true;
          mowerStatus.currentState = "In funzione";
        } else if (line.indexOf("STOPPED") != -1) {
          mowerStatus.isRunning = false;
          mowerStatus.currentState = "Fermo";
        } else if (line.indexOf("CHARGING") != -1) {
          mowerStatus.isCharging = true;
          mowerStatus.currentState = "In carica";
        }
      }
      
      // Estrai il livello della batteria
      int battIndex = line.indexOf("BATT:");
      if (battIndex != -1) {
        int commaIndex = line.indexOf(',', battIndex);
        if (commaIndex == -1) commaIndex = line.length();
        String battStr = line.substring(battIndex + 5, commaIndex);
        mowerStatus.batteryVoltage = battStr.toFloat();
      }
      
      // Estrai la percentuale della batteria
      int percentIndex = line.indexOf("PERCENT:");
      if (percentIndex != -1) {
        int commaIndex = line.indexOf(',', percentIndex);
        if (commaIndex == -1) commaIndex = line.length();
        String percentStr = line.substring(percentIndex + 8, commaIndex);
        mowerStatus.batteryPercentage = percentStr.toFloat();
      }
      
      mowerStatus.lastUpdate = millis();
      
      // Pubblica l'aggiornamento su MQTT
      if (mqttClient.connected()) {
        publishMqttState();
      }
    }
  }
}
