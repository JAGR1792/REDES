#include <painlessMesh.h>
#include <DHT.h>
#include <WiFi.h>
#include <Preferences.h>

#define MESH_PREFIX "Mesh"
#define MESH_PASSWORD "12345678"
#define MESH_PORT 5555

#define DHTPIN 4
#define DHTTYPE DHT22

Scheduler userScheduler;
painlessMesh mesh;
DHT dht(DHTPIN, DHTTYPE);
Preferences prefs;

String nodeName;

// Enviar datos cada 10s
Task taskSendData(TASK_SECOND * 10, TASK_FOREVER, []() {
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();

  if (!isnan(temp) && !isnan(hum)) {
    String msg = "{";
    msg += "\"node_id\":" + String(mesh.getNodeId()) + ",";
    msg += "\"node_name\":\"" + nodeName + "\",";
    msg += "\"mac\":\"" + WiFi.macAddress() + "\",";
    msg += "\"sensor\":\"DHT22\",";
    msg += "\"temperature\":" + String(temp, 2) + ",";
    msg += "\"humidity\":" + String(hum, 2);
    msg += "}";
    mesh.sendBroadcast(msg);
    Serial.println("Enviado: " + msg);
  } else {
    Serial.println("Error leyendo DHT22");
  }
});

void sendIdentification(uint32_t to) {
  String idMsg = "{";
  idMsg += "\"node_id\":" + String(mesh.getNodeId()) + ",";
  idMsg += "\"node_name\":\"" + nodeName + "\",";
  idMsg += "\"mac\":\"" + WiFi.macAddress() + "\",";
  idMsg += "\"sensors\":[\"DHT22\"]";
  idMsg += "}";
  mesh.sendSingle(to, idMsg);
  Serial.printf("Enviado ID a %u: %s\n", to, idMsg.c_str());
}

void receivedCallback(uint32_t from, String &msg) {
  Serial.printf("Mensaje recibido de %u: %s\n", from, msg.c_str());
  // Si el gateway pide info, respondemos con identificación
  if (msg.indexOf("\"cmd\":\"req_info\"") != -1) {
    sendIdentification(from);
  }
}

void newConnectionCallback(uint32_t nodeId) {
  Serial.printf("Nueva conexión (otro nodo): %u\n", nodeId);
}

void changedConnectionCallback() {
  Serial.printf("Conexiones cambiadas. Nodos: %d\n", mesh.getNodeList().size());
}

String defaultNameFromMac() {
  String mac = WiFi.macAddress(); // "AA:BB:CC:DD:EE:FF"
  mac.replace(":", "");
  // usa últimos 4 caracteres para un nombre corto
  String tail = mac.substring(mac.length() - 4);
  return "NODE-" + tail;
}

// Opción: permitir setear nombre por Serial al arrancar (10s)
void trySetNameFromSerial() {
  Serial.println("Para cambiar nombre escriba: setname NOMBRE (10s)");
  unsigned long start = millis();
  String line = "";
  while (millis() - start < 10000) {
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        line.trim();
        if (line.startsWith("setname ")) {
          String newName = line.substring(8);
          newName.trim();
          if (newName.length() > 0) {
            prefs.putString("name", newName);
            nodeName = newName;
            Serial.println("Nombre guardado: " + nodeName);
          }
        }
        line = "";
      } else {
        line += c;
      }
    }
    delay(10);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("=== NODO DHT22 INICIANDO ===");

  prefs.begin("node_cfg", false);
  WiFi.mode(WIFI_STA); // para WiFi.macAddress()
  delay(50);

  String stored = prefs.getString("name", "");
  if (stored.length() > 0) {
    nodeName = stored;
    Serial.println("Nombre cargado: " + nodeName);
  } else {
    nodeName = defaultNameFromMac();
    prefs.putString("name", nodeName);
    Serial.println("Nombre por defecto: " + nodeName);
    trySetNameFromSerial(); // opcional: permite cambiar en el primer arranque
  }

  dht.begin();

  mesh.setDebugMsgTypes(ERROR | STARTUP | CONNECTION);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);

  userScheduler.addTask(taskSendData);
  taskSendData.enable();

  Serial.println("Mesh configurado. Enviando datos periódicos.");
}

void loop() {
  mesh.update();
  userScheduler.execute();
}