#!/usr/bin/env python3
import json
import re
import time
import logging
import requests
import paho.mqtt.client as mqtt

# ========== CONFIGURA ESTO ==========
BROKER = "localhost"           # Cambia a la IP del broker MQTT (ej. 10.42.0.1) si no corre localmente
PORT = 1883
TOPIC = "dht22/datos/+"       # suscribe a todos los nodos
SERVER_URL = "http://localhost:5000/datos"  # URL del Flask (usa http si Flask corre local)
# ====================================

logging.basicConfig(level=logging.INFO, format='%(asctime)s %(levelname)s: %(message)s')

def parse_message(payload: str):
    """Intenta parsear JSON; si no, intenta extraer con regex formato antiguo;
       retorna dict con keys temperatura, humedad u el contenido original en 'mensaje'."""
    try:
        data = json.loads(payload)
        # normalized keys: acepta "temperature" o "temperatura" y "humidity" o "humedad"
        map_out = {}
        if 'temperature' in data or 'temperatura' in data:
            temp = data.get('temperature', data.get('temperatura'))
            hum = data.get('humidity', data.get('humedad'))
            map_out['temperatura'] = float(temp) if temp is not None else None
            map_out['humedad'] = float(hum) if hum is not None else None
            # copy other known fields if present
            if 'node_name' in data:
                map_out['nodeName'] = data['node_name']
            if 'mac' in data:
                map_out['mac'] = data['mac']
            return map_out
        return data
    except Exception:
        # try parse "Temp:23.5C Hum:45.2%" style
        try:
            match = re.search(r"Temp:([-+]?\d+(\.\d+)?)C.*?Hum:([-+]?\d+(\.\d+)?)%", payload)
            if match:
                return {"temperatura": float(match.group(1)), "humedad": float(match.group(3))}
        except Exception as e:
            logging.debug(f"Regex parse error: {e}")
        # fallback: send raw payload
        return {"mensaje": payload}

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        logging.info("Conectado a broker MQTT")
        client.subscribe(TOPIC)
        logging.info(f"Suscrito a {TOPIC}")
    else:
        logging.error(f"Error conectando MQTT rc={rc}")

def on_message(client, userdata, msg):
    try:
        topic = msg.topic
        payload = msg.payload.decode('utf-8', errors='ignore')
        logging.info(f"Mensaje MQTT {topic}: {payload}")

        data = parse_message(payload)
        # determine node id from topic last segment
        node_id = topic.split('/')[-1] if '/' in topic else "unknown"
        if isinstance(data, dict):
            # ensure fields temperatura/humedad exist when possible
            if 'temperatura' in data or 'humedad' in data:
                send = {}
                if 'temperatura' in data:
                    send['temperatura'] = data['temperatura']
                if 'humedad' in data:
                    send['humedad'] = data['humedad']
                send['nodeId'] = node_id
                send['timestamp'] = int(time.time())
            else:
                # fallback: attach whole dict under 'mensaje'
                send = {'mensaje': data, 'nodeId': node_id, 'timestamp': int(time.time())}
        else:
            send = {'mensaje': payload, 'nodeId': node_id, 'timestamp': int(time.time())}

        logging.info(f"POST a servidor: {send}")
        try:
            resp = requests.post(SERVER_URL, json=send, timeout=10)
            if resp.status_code == 200:
                logging.info("Dato enviado al servidor correctamente")
            else:
                logging.error(f"Error al enviar al servidor: {resp.status_code} - {resp.text}")
        except Exception as e:
            logging.exception(f"Error en POST al servidor: {e}")

    except Exception as e:
        logging.exception(f"Error procesando mensaje MQTT: {e}")

def main():
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        logging.info(f"Conectando a broker {BROKER}:{PORT} ...")
        client.connect(BROKER, PORT, keepalive=60)
    except Exception as e:
        logging.exception(f"No se pudo conectar al broker MQTT: {e}")
        return

    try:
        logging.info("Iniciando loop MQTT")
        client.loop_forever()
    except KeyboardInterrupt:
        logging.info("Interrumpido por usuario")
    except Exception as e:
        logging.exception(f"Loop MQTT finalizado con error: {e}")

if __name__ == '__main__':
    main()