
import os
from flask import Flask, request, jsonify, render_template
from flask_sqlalchemy import SQLAlchemy
from datetime import datetime, timezone
from flask_socketio import SocketIO, emit

app = Flask(__name__, template_folder='templates')

# Configuración (archivo SQLite local)
app.config['SQLALCHEMY_DATABASE_URI'] = 'sqlite:///datos_sensores.db'
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False

db = SQLAlchemy(app)
socketio = SocketIO(app, cors_allowed_origins='*', async_mode='threading')

# ==================== MODELO ====================
class DatosSensor(db.Model):
    __tablename__ = 'datos_sensor'
    id = db.Column(db.Integer, primary_key=True)
    temperatura = db.Column(db.Float, nullable=False)
    humedad = db.Column(db.Float, nullable=False)
    nodeId = db.Column(db.String(100), nullable=True)
    timestamp = db.Column(db.Integer, nullable=True)
    fecha_creacion = db.Column(db.DateTime, default=lambda: datetime.now(timezone.utc))

    def to_dict(self):
        return {
            'id': self.id,
            'temperatura': self.temperatura,
            'humedad': self.humedad,
            'nodeId': self.nodeId,
            'timestamp': self.timestamp,
            'fecha_creacion': self.fecha_creacion.strftime('%Y-%m-%d %H:%M:%S %Z')
        }

# Crear la base de datos si no existe
with app.app_context():
    db.create_all()

# ==================== RUTAS ====================
@app.route('/')
def home():
    try:
        total_registros = DatosSensor.query.count()
        ultimo = DatosSensor.query.order_by(DatosSensor.fecha_creacion.desc()).first()
        return render_template('index.html', total_registros=total_registros, ultimo_dato=ultimo)
    except Exception as e:
        return f"Servidor activo. Error: {e}", 500

@app.route('/datos', methods=['POST'])
def recibir_datos():
    try:
        data = request.get_json(force=True, silent=True)
        app.logger.debug(f"POST /datos payload: {data}")
        if not data:
            return jsonify({"status": "error", "mensaje": "No se recibió JSON"}), 400

        if 'temperatura' not in data or 'humedad' not in data:
            return jsonify({"status": "error", "mensaje": "Faltan campos temperatura/humedad"}), 400

        nuevo = DatosSensor(
            temperatura=float(data['temperatura']),
            humedad=float(data['humedad']),
            nodeId=str(data.get('nodeId')) if data.get('nodeId') is not None else None,
            timestamp=int(data.get('timestamp', int(datetime.now().timestamp())))
        )
        db.session.add(nuevo)
        db.session.commit()
        app.logger.info(f"Dato guardado en BD: ID={nuevo.id}")

        # Emitir por SocketIO (evento 'nuevo_dato')
        try:
            socketio.emit('nuevo_dato', nuevo.to_dict(), broadcast=True)
        except Exception as e:
            app.logger.warning(f"No se pudo emitir por SocketIO: {e}")

        return jsonify({"status": "ok", "mensaje": "Dato guardado en BD", "id": nuevo.id}), 200
    except Exception as e:
        app.logger.exception("Error al procesar /datos")
        db.session.rollback()
        return jsonify({"status": "error", "mensaje": str(e)}), 500

@app.route('/api/datos')
def api_datos():
    try:
        limit = request.args.get('limit', 100, type=int)
        datos = DatosSensor.query.order_by(DatosSensor.fecha_creacion.desc()).limit(limit).all()
        return jsonify([d.to_dict() for d in datos])
    except Exception as e:
        return jsonify({"error": str(e)}), 500

# ==================== SOCKET.IO ====================
@socketio.on('connect')
def on_connect():
    app.logger.info("Cliente SocketIO conectado")
    try:
        ultimos = DatosSensor.query.order_by(DatosSensor.fecha_creacion.desc()).limit(10).all()
        emit('datos_iniciales', {'datos': [d.to_dict() for d in ultimos]})
    except Exception as e:
        emit('error', {'mensaje': str(e)})

@socketio.on('disconnect')
def on_disconnect():
    app.logger.info("Cliente SocketIO desconectado")

# ==================== ARRANQUE ====================
if __name__ == '__main__':
    # DEBUG True imprime logs; en producción usar waitress/gunicorn y eventlet/gevent según necesidad
    socketio.run(app, host='0.0.0.0', port=5000, debug=True, allow_unsafe_werkzeug=True)