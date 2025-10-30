#include <painlessMesh.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <map>

#define MESH_PREFIX "Mesh"
#define MESH_PASSWORD "12345678"
#define MESH_PORT 5555

#define WIFI_SSID "Doofenshmirtz"
#define WIFI_PASSWORD "1023374689"
#define MQTT_SERVER "10.42.0.1"
#define MQTT_PORT 1883
#define MQTT_TOPIC "dht22/datos" 

Scheduler userScheduler;
painlessMesh mesh;
WiFiClient espClient;
PubSubClient client(espClient);

// mapa runtime nodeId -> nodeName (o MAC)
std::map<uint32_t, String> nodeNames;
std::map<uint32_t, String> nodeMacs;

void requestNodeInfo(uint32_t nodeId) {
  String req = "{\"cmd\":\"req_info\"}";
  mesh.sendSingle(nodeId, req);
  Serial.printf("Solicitado info a nodo %u\n", nodeId);
}

void receivedCallback(uint32_t from, String &msg) {
  Serial.printf("Datos recibidos desde nodo %u: %s\n", from, msg.c_str());

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, msg);

  if (!err) {
    // Si el mensaje trae node_name o mac, guardamos la info
    if (doc.containsKey("node_name")) {
      String nname = doc["node_name"].as<String>();
      nodeNames[from] = nname;
      Serial.printf("Mapa actualizado: %u -> %s\n", from, nname.c_str());
    }
    if (doc.containsKey("mac")) {
      String mac = doc["mac"].as<String>();
      nodeMacs[from] = mac;
      Serial.printf("MAC de %u = %s\n", from, mac.c_str());
    }

    // Para datos de sensor, publicamos en MQTT usando node_name si existe
    String topicId;
    if (nodeNames.count(from)) {
      topicId = nodeNames[from];
    } else if (doc.containsKey("node_name")) {
      topicId = doc["node_name"].as<String>();
    } else {
      topicId = String(from); // fallback numérico
    }

    if (doc.containsKey("temperature") || doc.containsKey("humidity")) {
      String topic = String(MQTT_TOPIC) + "/" + topicId;
      if (client.connected()) {
        if (client.publish(topic.c_str(), msg.c_str())) {
          Serial.println("Publicado en MQTT: " + topic);
        } else {
          Serial.println("Error publicando en MQTT");
        }
      } else {
        Serial.println("MQTT no conectado; no se publica");
      }
    }

    // Si solo era identificación, ya está guardada en nodeNames/nodeMacs
    if (doc.containsKey("node_name") && !(doc.containsKey("temperature") || doc.containsKey("humidity"))) {
      Serial.printf("Identificación recibida desde %u (%s)\n", from, nodeNames[from].c_str());
    }
  } else {
    Serial.println("Mensaje no-JSON recibido; se puede publicar en MQTT usando id numérico.");
    // fallback: publicar raw en topic con id numérico
    String topic = String(MQTT_TOPIC) + "/" + String(from);
    if (client.connected()) client.publish(topic.c_str(), msg.c_str());
  }
}

void newConnectionCallback(uint32_t nodeId) {
  Serial.printf("Nueva conexión mesh, nodeId = %u\n", nodeId);
  requestNodeInfo(nodeId);
}

void changedConnectionCallback() {
  Serial.printf("Conexiones cambiadas. Nodos: %d\n", mesh.getNodeList().size());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando a MQTT...");
    String clientId = "ESP32Gateway-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("MQTT Conectado!");
    } else {
      Serial.printf("Fallo MQTT, rc=%d reintentando en 5s\n", client.state());
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("=== ESP32 GATEWAY INICIANDO ===");

  mesh.setDebugMsgTypes(ERROR | STARTUP | CONNECTION);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);

  mesh.stationManual(WIFI_SSID, WIFI_PASSWORD);
  mesh.setHostname("ESP32-Gateway");

  client.setServer(MQTT_SERVER, MQTT_PORT);

  Serial.println("Gateway listo. Esperando nodos...");
}

void loop() {
  mesh.update();

  // publicar estado cada 30s
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 30000) {
    lastStatus = millis();
    IPAddress ip = mesh.getStationIP();
    Serial.printf("Estado: IP=%s, Nodos=%d, MQTT=%s\n",
                  ip.toString().c_str(),
                  mesh.getNodeList().size(),
                  client.connected() ? "BIEN" : "MAL");
  }

  if (mesh.getStationIP() != IPAddress(0,0,0,0)) {
    if (!client.connected()) reconnect();
    client.loop();
  }
}