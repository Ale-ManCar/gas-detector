#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ====== CONFIGURACIÓN WIFI ======
// MODE 1: Access Point (App local)
const char* ap_ssid = "DetectorGas_ESP32";
const char* ap_password = "12345678";

// MODE 2: WiFi Client (Telegram)
const char* wifi_ssid = "NETLIFE-MANTILLA";
const char* wifi_password = "Monica202#";

// ====== CONFIGURACIÓN TELEGRAM ======
const char* botToken = "8262757291:AAEejlv7c27rIwwhbV_aPAh_qF0GZj5s9Qw";
const char* chatID = "6346791271";

// ====== PINES ======
const int MQ2_PIN = 36;
const int MQ5_PIN = 39;
const int RELAY_PIN = 14;
const int BUZZER_PIN = 12;
const int LED_ROJO = 25;
const int LED_VERDE = 26;
const int LED_AMARILLO = 27;
const int SWITCH_PIN = 13;

// ====== UMBRALES ======
const int UMBRAL_ALERTA = 750;
const int UMBRAL_PELIGRO = 900;
const int UMBRAL_CRITICO = 950;

// ====== ESTADOS ======
enum Estado {
  OK_SEGURO,
  ALERTA,
  PELIGRO
};

Estado estadoActual = ALERTA;

// ====== VARIABLES ======
bool valvulaCerrada = true;
bool botonFisicoPresionado = false;
bool conteoActivo = false;
bool aperturaPermitida = false;

int contadorOK = 0;
const int OK_REQUERIDOS = 15;

// ====== VARIABLES TELEGRAM ======
unsigned long ultimaAlertaTelegram = 0;
const long INTERVALO_ALERTAS = 30000;
bool telegramIniciado = false;
String ultimoMensajeTelegram = "";
long ultimoUpdateID = 0;  // Evita comandos Antiguos

// ====== DATOS PARA LA APP ======
int ultimoNivelGas = 0;
String ultimoEvento = "Sistema iniciado";

// ====== SERVIDORES ======
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// ====== BOTÓN ======
bool estadoBotonAnterior = HIGH;

// ====== TIMERS ======
unsigned long tiempoAnterior = 0;
const unsigned long INTERVALO = 1000;
unsigned long buzzerAnterior = 0;
bool buzzerEstado = false;
unsigned long tiempoConteoAnterior = 0;
unsigned long ultimaReconexionWiFi = 0;

// ====== CONTROL EXCLUSIVO DE LEDS ======
void apagarTodosLEDs() {
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_AMARILLO, LOW);
  digitalWrite(LED_ROJO, LOW);
}

void encenderLED(int pinLED) {
  apagarTodosLEDs();
  digitalWrite(pinLED, HIGH);
}

void mostrarEstadoLED(Estado estado) {
  switch(estado) {
    case OK_SEGURO:
      encenderLED(LED_VERDE);
      break;
    case ALERTA:
      encenderLED(LED_AMARILLO);
      break;
    case PELIGRO:
      encenderLED(LED_ROJO);
      break;
  }
}

// ====== FUNCIONES TELEGRAM ======
void conectarWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n📡 Intentando conectar a WiFi para Telegram...");
    WiFi.begin(wifi_ssid, wifi_password);
    
    int intentos = 0;
    while (WiFi.status() != WL_CONNECTED && intentos < 20) {
      delay(500);
      Serial.print(".");
      intentos++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✅ WiFi conectado para Telegram");
      Serial.print("📡 IP: ");
      Serial.println(WiFi.localIP());
      telegramIniciado = true;
      enviarTelegram("🚀 SISTEMA DE GAS INICIADO\n"
                     "📍 Modo: AP + Telegram\n"
                     "📶 Señal WiFi: " + String(WiFi.RSSI()) + " dBm\n"
                     "✅ Sensores activados\n\n"
                     "🔧 Comandos disponibles:\n"
                     "/estado - Ver niveles\n"
                     "/abrir - Abrir válvula\n"
                     "/cerrar - Cerrar válvula\n"
                     "/ayuda - Ayuda");
    } else {
      Serial.println("\n❌ No se pudo conectar a WiFi para Telegram");
      telegramIniciado = false;
    }
  }
}

void enviarTelegram(String mensaje) {
  if (!telegramIniciado || WiFi.status() != WL_CONNECTED) {
    return;
  }
  
  // Codificar mensaje para URL
  mensaje.replace("\n", "%0A");
  mensaje.replace(" ", "%20");
  mensaje.replace("á", "a");
  mensaje.replace("é", "e");
  mensaje.replace("í", "i");
  mensaje.replace("ó", "o");
  mensaje.replace("ú", "u");
  mensaje.replace("ñ", "n");
  
  String url = "https://api.telegram.org/bot" + String(botToken) + 
               "/sendMessage?chat_id=" + String(chatID) + 
               "&text=" + mensaje;
  
  HTTPClient http;
  http.begin(url);
  http.setTimeout(5000);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    Serial.println("✅ Telegram enviado: " + mensaje.substring(0, 30) + "...");
    ultimoMensajeTelegram = mensaje;
  } else {
    Serial.print("❌ Error Telegram: ");
    Serial.println(httpCode);
  }
  
  http.end();
}

// ====== FUNCIÓN PARA EVITAR COMANDOS ANTIGUOS ======
void revisarComandosTelegram() {
  if (!telegramIniciado) return;
  
  // Offset para obtener comandos nuevos
  String url = "https://api.telegram.org/bot" + String(botToken) + 
               "/getUpdates?offset=" + String(ultimoUpdateID + 1) + "&timeout=1";
  
  HTTPClient http;
  http.begin(url);
  http.setTimeout(3000);
  
  if (http.GET() == 200) {
    String respuesta = http.getString();
    
    // Buscar TODOS los mensajes nuevos
    int startPos = 0;
    while ((startPos = respuesta.indexOf("\"update_id\":", startPos)) != -1) {
      startPos += 11;
      int endPos = respuesta.indexOf(",", startPos);
      long updateID = respuesta.substring(startPos, endPos).toInt();
      
      // Buscar el texto del comando
      int textStart = respuesta.indexOf("\"text\":\"", endPos);
      if (textStart != -1) {
        textStart += 8;
        int textEnd = respuesta.indexOf("\"", textStart);
        String comando = respuesta.substring(textStart, textEnd);
        
        // Procesar update nuevo
        if (updateID > ultimoUpdateID) {
          ultimoUpdateID = updateID;
          procesarComandoTelegram(comando);
        }
      }
      
      startPos = endPos;
    }
    
    // Limpiar el buffer de Telegram para evitar reenvíos
    if (ultimoUpdateID > 0) {
      String clearUrl = "https://api.telegram.org/bot" + String(botToken) + 
                        "/getUpdates?offset=" + String(ultimoUpdateID + 1);
      HTTPClient http2;
      http2.begin(clearUrl);
      http2.GET();
      http2.end();
    }
  }
  
  http.end();
}

void procesarComandoTelegram(String comando) {
  comando.toLowerCase();
  Serial.print("📱 Comando Telegram NUEVO: ");
  Serial.println(comando);
  
  if (comando == "/estado" || comando == "/status") {
    int mq2 = analogRead(MQ2_PIN);
    int mq5 = analogRead(MQ5_PIN);
    int maximo = max(mq2, mq5);
    
    String estado = "📊 ESTADO ACTUAL%0A";
    estado += "🔹 MQ-2: " + String(mq2) + "%0A";
    estado += "🔹 MQ-5: " + String(mq5) + "%0A";
    estado += "🔹 Máximo: " + String(maximo) + "%0A";
    estado += "🔌 Válvula: " + String(valvulaCerrada ? "CERRADA" : "ABIERTA") + "%0A";
    estado += "📶 WiFi AP: " + WiFi.softAPIP().toString() + "%0A";
    estado += "⏰ Uptime: " + String(millis() / 60000) + " minutos%0A";
    estado += "📱 Estado: ";
    
    switch(estadoActual) {
      case OK_SEGURO: estado += "SEGURO ✅"; break;
      case ALERTA: estado += "ALERTA ⚠️"; break;
      case PELIGRO: estado += "PELIGRO 🚨"; break;
    }
    
    if (conteoActivo) {
      estado += "%0A🔄 Verificando: " + String(contadorOK) + "/15";
    } else if (aperturaPermitida) {
      estado += "%0A✅ LISTO para confirmar apertura";
    }
    
    enviarTelegram(estado);
  }
  
  else if (comando == "/abrir") {
    // Validar comando
    static unsigned long ultimoComandoAbrir = 0;
    unsigned long ahora = millis();
    
    if (ahora - ultimoComandoAbrir < 1000) { // Evitar comandos demasiado rápidos
      enviarTelegram("⏳ Espere 1 segundo entre comandos");
      return;
    }
    ultimoComandoAbrir = ahora;
    
    int nivel = max(analogRead(MQ2_PIN), analogRead(MQ5_PIN));
    
    if (aperturaPermitida && nivel < UMBRAL_ALERTA) {
      // Confirmar apertura
      estadoActual = OK_SEGURO;
      valvulaCerrada = false;
      conteoActivo = false;
      aperturaPermitida = false;
      botonFisicoPresionado = false;
      contadorOK = 0;
      digitalWrite(RELAY_PIN, HIGH);
      
      ultimoEvento = "✅ VÁLVULA ABIERTA desde Telegram";
      enviarTelegram("✅ VÁLVULA ABIERTA desde Telegram%0A"
                     "Nivel actual: " + String(nivel) + "%0A"
                     "Sistema en estado SEGURO");
    } 
    else if (nivel >= UMBRAL_ALERTA) {
      enviarTelegram("❌ NO SE PUEDE ABRIR%0A"
                     "Nivel de gas: " + String(nivel) + "%0A"
                     "⚠️  Nivel demasiado alto para abrir");
    } 
    else if (!aperturaPermitida) {
      if (conteoActivo) {
        enviarTelegram("⏳ Verificación en progreso%0A"
                       "Completado: " + String(contadorOK) + "/15 lecturas seguras%0A"
                       "Espere a que se complete la verificación");
      } else {
        enviarTelegram("🔘 Primero debe:%0A"
                       "1. Presionar el BOTÓN FÍSICO%0A"
                       "2. Esperar 15 lecturas seguras consecutivas%0A"
                       "3. Luego podrá confirmar la apertura con /abrir");
      }
    }
  }
  
  else if (comando == "/cerrar") {
    estadoActual = ALERTA;
    valvulaCerrada = true;
    conteoActivo = false;
    aperturaPermitida = false;
    botonFisicoPresionado = false;
    contadorOK = 0;
    digitalWrite(RELAY_PIN, LOW);
    
    ultimoEvento = "🔒 VÁLVULA CERRADA desde Telegram";
    enviarTelegram("🔒 VÁLVULA CERRADA%0A"
                   "El flujo de gas ha sido detenido%0A"
                   "Sistema en estado ALERTA");
  }
  
  else if (comando == "/ayuda" || comando == "/start" || comando == "/help") {
    String ayuda = "🤖 COMANDOS DISPONIBLES:%0A%0A";
    ayuda += "/estado - Ver niveles de gas actuales%0A";
    ayuda += "/abrir - Confirmar apertura (si ya pasó verificación)%0A";
    ayuda += "/cerrar - Cerrar la válvula%0A";
    ayuda += "/ayuda - Mostrar esta ayuda%0A%0A";
    ayuda += "🚨 ALERTAS AUTOMÁTICAS:%0A";
    ayuda += "• >" + String(UMBRAL_CRITICO) + " - EMERGENCIA%0A";
    ayuda += "• >" + String(UMBRAL_PELIGRO) + " - ALERTA ROJA%0A";
    ayuda += "• >" + String(UMBRAL_ALERTA) + " - ALERTA AMARILLA%0A%0A";
    ayuda += "🔧 Sistema en modo DUAL:%0A";
    ayuda += "📱 App local: " + WiFi.softAPIP().toString() + "%0A";
    ayuda += "🤖 Telegram: Alertas remotas";
    
    enviarTelegram(ayuda);
  }
}

void enviarAlertaTelegramSegura(int nivelMaximo, int mq2, int mq5) {
  if (millis() - ultimaAlertaTelegram < INTERVALO_ALERTAS) {
    return;
  }
  
  if (nivelMaximo >= UMBRAL_CRITICO) {
    String alerta = "🚨🚨 EMERGENCIA - NIVEL CRÍTICO 🚨🚨%0A";
    alerta += "Nivel detectado: " + String(nivelMaximo) + "%0A";
    alerta += "MQ-2: " + String(mq2) + "%0A";
    alerta += "MQ-5: " + String(mq5) + "%0A";
    alerta += "🔴 VÁLVULA CERRADA AUTOMÁTICAMENTE%0A";
    alerta += "⚠️  EVACUAR EL ÁREA INMEDIATAMENTE%0A";
    alerta += "🚫 Evitar fuentes de ignición%0A";
    alerta += "🆘 Contactar a emergencias";
    
    enviarTelegram(alerta);
    ultimaAlertaTelegram = millis();
  }
  else if (nivelMaximo >= UMBRAL_PELIGRO) {
    String alerta = "🔴 ALERTA ROJA - NIVEL PELIGROSO%0A";
    alerta += "Nivel: " + String(nivelMaximo) + "%0A";
    alerta += "MQ-2: " + String(mq2) + "%0A";
    alerta += "MQ-5: " + String(mq5) + "%0A";
    alerta += "⚠️  Válvula cerrada por seguridad%0A";
    alerta += "💨 Ventilar el área%0A";
    alerta += "🔍 Buscar posibles fugas";
    
    enviarTelegram(alerta);
    ultimaAlertaTelegram = millis();
  }
  else if (nivelMaximo >= UMBRAL_ALERTA) {
    String alerta = "🟡 ALERTA AMARILLA - NIVEL MEDIO%0A";
    alerta += "Nivel: " + String(nivelMaximo) + "%0A";
    alerta += "MQ-2: " + String(mq2) + "%0A";
    alerta += "MQ-5: " + String(mq5) + "%0A";
    alerta += "📢 Incrementar ventilación%0A";
    alerta += "👀 Monitorear constantemente";
    
    enviarTelegram(alerta);
    ultimaAlertaTelegram = millis();
  }
}

// ====== WEBSOCKET ======
void enviarDatosWebSocket() {
  StaticJsonDocument<300> doc;
  doc["nivelGas"] = ultimoNivelGas;
  doc["estado"] = (int)estadoActual;
  doc["valvulaCerrada"] = valvulaCerrada;
  doc["conteoActivo"] = conteoActivo;
  doc["aperturaPermitida"] = aperturaPermitida;
  doc["contadorOK"] = contadorOK;
  doc["evento"] = ultimoEvento;
  doc["timestamp"] = millis();
  doc["telegram"] = telegramIniciado;
  
  String jsonString;
  serializeJson(doc, jsonString);
  webSocket.broadcastTXT(jsonString);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_TEXT) {
    String mensaje = String((char*)payload);
    
    if (mensaje == "CONFIRMAR_APERTURA") {
      if (aperturaPermitida && estadoActual == ALERTA && ultimoNivelGas < UMBRAL_ALERTA) {
        estadoActual = OK_SEGURO;
        valvulaCerrada = false;
        conteoActivo = false;
        aperturaPermitida = false;
        botonFisicoPresionado = false;
        contadorOK = 0;
        digitalWrite(RELAY_PIN, HIGH);
        
        ultimoEvento = "✅ APERTURA CONFIRMADA desde App Web";
        Serial.println(">>> APERTURA CONFIRMADA DESDE APP <<<");
        
        if (telegramIniciado) {
          enviarTelegram("✅ APERTURA CONFIRMADA desde App Web%0A"
                         "Nivel: " + String(ultimoNivelGas) + "%0A"
                         "Sistema en estado SEGURO");
        }
      }
    }
    else if (mensaje == "CERRAR_MANUAL") {
      estadoActual = ALERTA;
      valvulaCerrada = true;
      conteoActivo = false;
      aperturaPermitida = false;
      botonFisicoPresionado = false;
      contadorOK = 0;
      digitalWrite(RELAY_PIN, LOW);
      
      ultimoEvento = "🔒 Válvula cerrada manualmente desde app";
      Serial.println(">>> CIERRE MANUAL DESDE APP <<<");
      
      if (telegramIniciado) {
        enviarTelegram("🔒 VÁLVULA CERRADA manualmente desde App Web%0A"
                       "Sistema en estado ALERTA");
      }
    }
    
    enviarDatosWebSocket();
  }
}

// ====== PÁGINA WEB/APP ======
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Control Detector de Gas</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
        }
        
        body {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
            display: flex;
            justify-content: center;
            align-items: center;
        }
        
        .container {
            background: white;
            border-radius: 20px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            width: 100%;
            max-width: 550px;
            overflow: hidden;
        }
        
        .header {
            background: linear-gradient(90deg, #4b6cb7 0%, #182848 100%);
            color: white;
            padding: 25px;
            text-align: center;
            position: relative;
        }
        
        .header h1 {
            font-size: 24px;
            font-weight: 600;
            margin-bottom: 5px;
        }
        
        .header p {
            opacity: 0.9;
            font-size: 14px;
        }
        
        .status-badge {
            position: absolute;
            top: 20px;
            right: 20px;
            background: rgba(255,255,255,0.2);
            padding: 8px 15px;
            border-radius: 20px;
            font-size: 12px;
            font-weight: bold;
        }
        
        .telegram-badge {
            position: absolute;
            top: 50px;
            right: 20px;
            background: #0088cc;
            color: white;
            padding: 5px 10px;
            border-radius: 15px;
            font-size: 11px;
            display: flex;
            align-items: center;
            gap: 5px;
        }
        
        .content {
            padding: 25px;
        }
        
        .section {
            margin-bottom: 25px;
            padding: 20px;
            background: #f8f9fa;
            border-radius: 15px;
            border-left: 5px solid #4b6cb7;
        }
        
        .section-title {
            color: #2c3e50;
            font-size: 18px;
            font-weight: 600;
            margin-bottom: 15px;
        }
        
        .data-grid {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 15px;
            margin-bottom: 20px;
        }
        
        .data-card {
            background: white;
            padding: 15px;
            border-radius: 10px;
            text-align: center;
            box-shadow: 0 3px 10px rgba(0,0,0,0.08);
        }
        
        .data-value {
            font-size: 28px;
            font-weight: 700;
            color: #2c3e50;
            margin: 5px 0;
        }
        
        .data-label {
            font-size: 12px;
            color: #7f8c8d;
            text-transform: uppercase;
            letter-spacing: 1px;
        }
        
        .gas-level {
            margin: 20px 0;
            position: relative;
        }
        
        .gas-label {
            display: flex;
            justify-content: space-between;
            margin-bottom: 8px;
            font-size: 14px;
            color: #2c3e50;
        }
        
        .gas-bar {
            height: 25px;
            background: #ecf0f1;
            border-radius: 12px;
            overflow: hidden;
            position: relative;
        }
        
        .gas-fill {
            height: 100%;
            border-radius: 12px;
            transition: width 0.5s ease;
            position: relative;
            z-index: 2;
        }
        
        .gas-seguro { background: #2ecc71; }
        .gas-alerta { background: #f39c12; }
        .gas-peligro { background: #e74c3c; }
        
        .gas-markers-container {
            position: relative;
            height: 20px;
            margin-top: 5px;
        }
        
        .gas-marker {
            position: absolute;
            transform: translateX(-50%);
            font-size: 11px;
            color: #95a5a6;
            text-align: center;
        }
        
        .gas-marker::after {
            content: '';
            position: absolute;
            top: -15px;
            left: 50%;
            width: 1px;
            height: 10px;
            background: #bdc3c7;
            transform: translateX(-50%);
        }
        
        .marker-0 { left: 0%; }
        .marker-350 { left: 34.2%; }
        .marker-750 { left: 73.3%; }
        .marker-1023 { left: 100%; }
        
        .controls {
            display: flex;
            flex-direction: column;
            gap: 15px;
            margin: 25px 0;
        }
        
        .btn {
            padding: 18px;
            border: none;
            border-radius: 12px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s;
        }
        
        .btn-primary {
            background: #3498db;
            color: white;
        }
        
        .btn-primary:hover {
            background: #2980b9;
        }
        
        .btn-danger {
            background: #e74c3c;
            color: white;
        }
        
        .btn-danger:hover {
            background: #c0392b;
        }
        
        .btn-telegram {
            background: #0088cc;
            color: white;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 10px;
        }
        
        .btn-disabled {
            background: #95a5a6;
            color: #ecf0f1;
            cursor: not-allowed;
            opacity: 0.7;
        }
        
        .btn-disabled:hover {
            background: #95a5a6;
        }
        
        .notification {
            position: fixed;
            top: 20px;
            right: 20px;
            background: white;
            padding: 20px;
            border-radius: 10px;
            box-shadow: 0 5px 20px rgba(0,0,0,0.2);
            display: none;
            z-index: 1000;
            max-width: 300px;
            border-left: 5px solid #3498db;
        }
        
        .notification-warning {
            border-left-color: #f39c12;
        }
        
        .notification-success {
            border-left-color: #2ecc71;
        }
        
        .notification-danger {
            border-left-color: #e74c3c;
        }
        
        .notification-telegram {
            border-left-color: #0088cc;
        }
        
        .notification-title {
            font-weight: 600;
            margin-bottom: 5px;
            color: #2c3e50;
        }
        
        .notification-message {
            font-size: 14px;
            color: #7f8c8d;
        }
        
        .progress-container {
            margin: 20px 0;
            background: #ecf0f1;
            border-radius: 10px;
            overflow: hidden;
            height: 10px;
        }
        
        .progress-bar {
            height: 100%;
            background: #3498db;
            width: 0%;
            transition: width 0.5s ease;
        }
        
        .event-log {
            background: #f8f9fa;
            border-radius: 10px;
            padding: 15px;
            max-height: 200px;
            overflow-y: auto;
            margin-top: 20px;
        }
        
        .event-item {
            padding: 10px 0;
            border-bottom: 1px solid #e0e0e0;
            font-size: 13px;
            color: #2c3e50;
        }
        
        .event-item:last-child {
            border-bottom: none;
        }
        
        .event-time {
            color: #7f8c8d;
            font-size: 11px;
            margin-right: 10px;
        }
        
        .connection-status {
            position: fixed;
            bottom: 20px;
            left: 20px;
            background: rgba(0,0,0,0.7);
            color: white;
            padding: 10px 15px;
            border-radius: 20px;
            font-size: 12px;
            display: flex;
            align-items: center;
            gap: 8px;
        }
        
        .status-dot {
            width: 10px;
            height: 10px;
            border-radius: 50%;
            background: #e74c3c;
            display: inline-block;
        }
        
        .status-dot.connected {
            background: #2ecc71;
        }
        
        .telegram-dot {
            background: #0088cc;
        }
        
        .blink {
            animation: blink 1s infinite;
        }
        
        @keyframes blink {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.5; }
        }
        
        @media (max-width: 480px) {
            .container {
                margin: 10px;
                border-radius: 15px;
            }
            
            .header {
                padding: 20px 15px;
            }
            
            .content {
                padding: 20px 15px;
            }
            
            .data-grid {
                grid-template-columns: 1fr;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>DETECTOR DE GAS</h1>
            <p>Monitoreo local + Alertas Telegram</p>
            <div class="status-badge" id="statusBadge">CONECTANDO...</div>
            <div class="telegram-badge" id="telegramBadge" style="display: none;">
                <span>🤖 Telegram</span>
                <div class="status-dot telegram-dot" id="telegramDot"></div>
            </div>
        </div>
        
        <div class="content">
            <div class="section">
                <div class="section-title">ESTADO DEL SISTEMA</div>
                
                <div class="data-grid">
                    <div class="data-card">
                        <div class="data-label">NIVEL DE GAS</div>
                        <div class="data-value" id="nivelGas">0</div>
                        <div class="data-label">de 1023</div>
                    </div>
                    
                    <div class="data-card">
                        <div class="data-label">ESTADO ACTUAL</div>
                        <div class="data-value" id="estadoActual">ALERTA</div>
                        <div class="data-label" id="estadoDetalle">Sistema iniciado</div>
                    </div>
                </div>
                
                <div class="gas-level">
                    <div class="gas-label">
                        <span>SEGURO</span>
                        <span id="nivelTexto">Nivel: 0</span>
                        <span>PELIGRO</span>
                    </div>
                    <div class="gas-bar">
                        <div id="gasFill" class="gas-fill" style="width: 0%"></div>
                    </div>
                    <div class="gas-markers-container">
                        <div class="gas-marker marker-0">0</div>
                        <div class="gas-marker marker-350">350</div>
                        <div class="gas-marker marker-750">750</div>
                        <div class="gas-marker marker-1023">1023+</div>
                    </div>
                </div>
                
                <div class="data-grid">
                    <div class="data-card">
                        <div class="data-label">VALVULA</div>
                        <div class="data-value" id="valvulaEstado">CERRADA</div>
                        <div class="data-label" id="valvulaDetalle"></div>
                    </div>
                    
                    <div class="data-card">
                        <div class="data-label">VERIFICACIÓN</div>
                        <div class="data-value" id="contadorOK">0/15</div>
                        <div class="data-label" id="contadorEstado">Esperando</div>
                    </div>
                </div>
            </div>
            
            <div class="section">
                <div class="section-title">CONTROLES</div>
                
                <div class="controls">
                    <button id="btnConfirmar" class="btn btn-primary btn-disabled" onclick="confirmarApertura()">
                        ESPERANDO BOTON FISICO
                    </button>
                    
                    <button id="btnCerrar" class="btn btn-danger" onclick="controlManual('CERRAR_MANUAL')">
                        CERRAR MANUAL
                    </button>
                    
                    <button id="btnTelegram" class="btn btn-telegram" onclick="enviarTelegramEstado()">
                        <span>📱</span>
                        ENVIAR ESTADO A TELEGRAM
                    </button>
                </div>
                
                <div id="progressSection" style="display: none;">
                    <div style="display: flex; justify-content: space-between; margin-bottom: 5px;">
                        <span>Verificando nivel:</span>
                        <span id="contadorTexto">0/15</span>
                    </div>
                    <div class="progress-container">
                        <div id="progressBar" class="progress-bar"></div>
                    </div>
                </div>
            </div>
            
            <div class="section">
                <div class="section-title">EVENTOS RECIENTES</div>
                <div id="eventLog" class="event-log"></div>
            </div>
        </div>
    </div>
    
    <div id="notification" class="notification">
        <div class="notification-title" id="notifTitle">Notificación</div>
        <div class="notification-message" id="notifMessage">Mensaje de notificación</div>
    </div>
    
    <div class="connection-status">
        <div class="status-dot" id="statusDot"></div>
        <span id="connectionText">Conectando al ESP32...</span>
        <div class="status-dot telegram-dot" id="telegramStatusDot"></div>
        <span id="telegramStatusText">Telegram</span>
    </div>

    <script>
        let websocket = null;
        let eventos = [];
        let ultimoNivelGas = 0;
        let telegramConectado = false;
        
        function initWebSocket() {
            const ip = window.location.hostname;
            const wsUri = 'ws://' + ip + ':81/';
            
            websocket = new WebSocket(wsUri);
            
            websocket.onopen = function(event) {
                console.log('Conectado al ESP32');
                updateConnectionStatus(true);
                showNotification('Conectado al sistema', 'Conexión establecida correctamente', 'success');
            };
            
            websocket.onclose = function(event) {
                console.log('Desconectado del ESP32');
                updateConnectionStatus(false);
                showNotification('Desconectado', 'Intentando reconectar...', 'danger');
                
                setTimeout(initWebSocket, 3000);
            };
            
            websocket.onmessage = function(event) {
                try {
                    const data = JSON.parse(event.data);
                    updateUI(data);
                } catch (error) {
                    console.error('Error al procesar datos:', error);
                }
            };
            
            websocket.onerror = function(event) {
                console.error('Error en WebSocket');
                updateConnectionStatus(false);
            };
        }
        
        function updateUI(data) {
            ultimoNivelGas = data.nivelGas;
            telegramConectado = data.telegram || false;
            
            document.getElementById('nivelGas').textContent = ultimoNivelGas;
            document.getElementById('nivelTexto').textContent = 'Nivel: ' + ultimoNivelGas;
            
            updateTelegramStatus(telegramConectado);
            
            const porcentajeExacto = (ultimoNivelGas / 1023) * 100;
            const gasFill = document.getElementById('gasFill');
            gasFill.style.width = porcentajeExacto + '%';
            
            gasFill.className = 'gas-fill ';
            if(data.estado === 0) {
                gasFill.classList.add('gas-seguro');
                document.getElementById('estadoActual').textContent = 'SEGURO';
                document.getElementById('estadoDetalle').textContent = 'Sistema operativo normal';
                document.getElementById('statusBadge').textContent = 'SEGURO';
                document.getElementById('statusBadge').style.background = 'rgba(46, 204, 113, 0.3)';
            } else if(data.estado === 1) {
                gasFill.classList.add('gas-alerta');
                document.getElementById('estadoActual').textContent = 'ALERTA';
                document.getElementById('estadoDetalle').textContent = 'Monitoreando niveles';
                document.getElementById('statusBadge').textContent = 'ALERTA';
                document.getElementById('statusBadge').style.background = 'rgba(243, 156, 18, 0.3)';
            } else {
                gasFill.classList.add('gas-peligro');
                document.getElementById('estadoActual').textContent = 'PELIGRO';
                document.getElementById('estadoDetalle').textContent = 'NIVEL CRÍTICO';
                document.getElementById('statusBadge').textContent = 'PELIGRO';
                document.getElementById('statusBadge').style.background = 'rgba(231, 76, 60, 0.3)';
            }
            
            if(data.valvulaCerrada) {
                document.getElementById('valvulaEstado').textContent = 'CERRADA';
                document.getElementById('valvulaEstado').style.color = '#e74c3c';
                document.getElementById('valvulaDetalle').textContent = 'Sin flujo de gas';
            } else {
                document.getElementById('valvulaEstado').textContent = 'ABIERTA';
                document.getElementById('valvulaEstado').style.color = '#2ecc71';
                document.getElementById('valvulaDetalle').textContent = 'Flujo de gas activo';
            }
            
            document.getElementById('contadorOK').textContent = data.contadorOK + '/15';
            
            const contadorEstado = document.getElementById('contadorEstado');
            if(data.conteoActivo) {
                contadorEstado.textContent = 'Verificando estabilidad...';
                contadorEstado.style.color = '#3498db';
                
                document.getElementById('progressSection').style.display = 'block';
                const porcentajeContador = (data.contadorOK / 15) * 100;
                document.getElementById('progressBar').style.width = porcentajeContador + '%';
                document.getElementById('contadorTexto').textContent = data.contadorOK + '/15';
            } else {
                contadorEstado.textContent = 'Esperando acción';
                contadorEstado.style.color = '#7f8c8d';
                document.getElementById('progressSection').style.display = 'none';
            }
            
            const btnConfirmar = document.getElementById('btnConfirmar');
            
            if(data.aperturaPermitida && data.estado === 1 && ultimoNivelGas < 750) {
                btnConfirmar.classList.remove('btn-disabled');
                btnConfirmar.classList.add('blink');
                btnConfirmar.textContent = 'CONFIRMAR APERTURA';
            } else {
                btnConfirmar.classList.add('btn-disabled');
                btnConfirmar.classList.remove('blink');
                if(data.conteoActivo) {
                    btnConfirmar.textContent = 'VERIFICANDO... (' + data.contadorOK + '/15)';
                } else {
                    btnConfirmar.textContent = 'ESPERANDO BOTON FISICO';
                }
            }
            
            if(data.evento) {
                addEventToLog(data.evento, data.timestamp);
                
                if(data.evento.includes('PELIGRO') || data.evento.includes('CRITICO') || data.evento.includes('🚨')) {
                    showNotification('ALERTA DE PELIGRO', 'Nivel crítico de gas detectado', 'danger');
                } else if(data.evento.includes('Boton fisico presionado') || data.evento.includes('🔘')) {
                    showNotification('Botón físico presionado', 'Verificando estabilidad del gas...', 'warning');
                } else if(data.evento.includes('VALVULA ABIERTA') || data.evento.includes('✅') || data.evento.includes('🔓')) {
                    showNotification('Válvula abierta', 'El sistema ahora está en modo SEGURO', 'success');
                } else if(data.evento.includes('🔒')) {
                    showNotification('Válvula cerrada', 'El sistema está ahora en modo ALERTA', 'warning');
                } else if(data.evento.includes('Telegram')) {
                    showNotification('Telegram', 'Mensaje enviado a Telegram', 'telegram');
                }
            }
        }
        
        function confirmarApertura() {
            const btn = document.getElementById('btnConfirmar');
            if(!btn.classList.contains('btn-disabled')) {
                if(confirm('¿Confirmar apertura de la válvula?\nNivel actual: ' + ultimoNivelGas)) {
                    websocket.send("CONFIRMAR_APERTURA");
                    showNotification('Confirmación enviada', 'Abriendo válvula...', 'success');
                }
            }
        }
        
        function controlManual(accion) {
            const mensaje = '¿Estás seguro de cerrar la válvula manualmente?';
            
            if(confirm(mensaje)) {
                websocket.send(accion);
                showNotification('Comando enviado', 'Acción ejecutada correctamente', 'success');
            }
        }
        
        function enviarTelegramEstado() {
            if(websocket && websocket.readyState === WebSocket.OPEN) {
                websocket.send("TELEGRAM_ESTADO");
                showNotification('Solicitud enviada', 'Estado enviado a Telegram', 'telegram');
            }
        }
        
        function updateConnectionStatus(connected) {
            const statusDot = document.getElementById('statusDot');
            const connectionText = document.getElementById('connectionText');
            
            if(connected) {
                statusDot.classList.add('connected');
                connectionText.textContent = 'Conectado al ESP32';
            } else {
                statusDot.classList.remove('connected');
                connectionText.textContent = 'Desconectado - Reconectando...';
            }
        }
        
        function updateTelegramStatus(connected) {
            const telegramBadge = document.getElementById('telegramBadge');
            const telegramDot = document.getElementById('telegramDot');
            const telegramStatusDot = document.getElementById('telegramStatusDot');
            const telegramStatusText = document.getElementById('telegramStatusText');
            
            if(connected) {
                telegramBadge.style.display = 'flex';
                telegramDot.style.background = '#2ecc71';
                telegramStatusDot.style.background = '#2ecc71';
                telegramStatusText.textContent = 'Telegram: Conectado';
            } else {
                telegramBadge.style.display = 'flex';
                telegramDot.style.background = '#e74c3c';
                telegramStatusDot.style.background = '#e74c3c';
                telegramStatusText.textContent = 'Telegram: Desconectado';
            }
        }
        
        function showNotification(title, message, type) {
            const notification = document.getElementById('notification');
            const notifTitle = document.getElementById('notifTitle');
            const notifMessage = document.getElementById('notifMessage');
            
            notifTitle.textContent = title;
            notifMessage.textContent = message;
            
            notification.className = 'notification';
            if(type === 'warning') notification.classList.add('notification-warning');
            else if(type === 'success') notification.classList.add('notification-success');
            else if(type === 'danger') notification.classList.add('notification-danger');
            else if(type === 'telegram') notification.classList.add('notification-telegram');
            
            notification.style.display = 'block';
            
            setTimeout(function() {
                notification.style.display = 'none';
            }, 5000);
        }
        
        function addEventToLog(evento, timestamp) {
            const now = new Date(timestamp);
            const timeString = now.getHours().toString().padStart(2, '0') + ':' + 
                             now.getMinutes().toString().padStart(2, '0') + ':' + 
                             now.getSeconds().toString().padStart(2, '0');
            
            const eventLog = document.getElementById('eventLog');
            const eventItem = document.createElement('div');
            eventItem.className = 'event-item';
            eventItem.innerHTML = '<span class="event-time">[' + timeString + ']</span> ' + evento;
            
            eventLog.insertBefore(eventItem, eventLog.firstChild);
            
            if(eventLog.children.length > 10) {
                eventLog.removeChild(eventLog.lastChild);
            }
        }
        
        window.addEventListener('load', initWebSocket);
    </script>
</body>
</html>
)rawliteral";

// ====== HANDLERS DEL SERVIDOR WEB ======
void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not Found");
}

// ====== SETUP ======
void setup() {
  Serial.begin(115200);
  delay(2000);
  
  // Configuración pines
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AMARILLO, OUTPUT);
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  
  // Estado inicial
  apagarTodosLEDs();
  estadoActual = ALERTA;
  valvulaCerrada = true;
  botonFisicoPresionado = false;
  conteoActivo = false;
  aperturaPermitida = false;
  digitalWrite(RELAY_PIN, LOW);
  noTone(BUZZER_PIN);
  
  // Configurar WiFi (Access Point)
  Serial.println("\n=== CONFIGURANDO ACCESS POINT ===");
  WiFi.softAP(ap_ssid, ap_password);
  Serial.print("📱 AP SSID: ");
  Serial.println(ap_ssid);
  Serial.print("🔑 AP Password: ");
  Serial.println(ap_password);
  Serial.print("🌐 IP del ESP32: ");
  Serial.println(WiFi.softAPIP());
  
  // Configurar servidores
  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();
  
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  // Conectar a WiFi para Telegram
  Serial.println("\n=== CONECTANDO A WIFI PARA TELEGRAM ===");
  WiFi.mode(WIFI_AP_STA);
  conectarWiFi();
  
  Serial.println("\n=== SISTEMA INICIADO ===");
  Serial.println("✅ Modo DUAL Activado:");
  Serial.println("   1. Access Point local: " + WiFi.softAPIP().toString());
  Serial.print("   2. Telegram: ");
  Serial.println(telegramIniciado ? "CONECTADO" : "DESCONECTADO");
  Serial.println("=====================================\n");
}

// ====== LOOP PRINCIPAL ======
void loop() {
  server.handleClient();
  webSocket.loop();
  
  // Mantener conexión WiFi para Telegram
  if (millis() - ultimaReconexionWiFi > 60000) {
    ultimaReconexionWiFi = millis();
    if (WiFi.status() != WL_CONNECTED) {
      conectarWiFi();
    }
  }
  
  // Revisar comandos de Telegram cada 5 segundos
  static unsigned long ultimaConsultaTelegram = 0;
  if (telegramIniciado && millis() - ultimaConsultaTelegram >= 5000) {
    ultimaConsultaTelegram = millis();
    revisarComandosTelegram();
  }
  
  // ===== BOTÓN FÍSICO ====
  bool estadoBotonActual = digitalRead(SWITCH_PIN);
  if (estadoBotonAnterior == HIGH && estadoBotonActual == LOW) {
    botonFisicoPresionado = true;
    Serial.println(">>> BOTON FISICO PRESIONADO <<<");
    
    if (telegramIniciado) {
      enviarTelegram("🔘 BOTÓN FÍSICO PRESIONADO%0AIniciando verificación de estabilidad...");
    }
  }
  estadoBotonAnterior = estadoBotonActual;
  
  // ==== LECTURA DE SENSORES ====
  unsigned long ahora = millis();
  if (ahora - tiempoAnterior >= INTERVALO) {
    tiempoAnterior = ahora;
    
    int mq2 = analogRead(MQ2_PIN);
    int mq5 = analogRead(MQ5_PIN);
    ultimoNivelGas = max(mq2, mq5);
    
    Serial.print("MQ2=");
    Serial.print(mq2);
    Serial.print(" MQ5=");
    Serial.print(mq5);
    Serial.print(" NIVEL=");
    Serial.print(ultimoNivelGas);
    Serial.print(" | ");
    
    if (telegramIniciado) {
      enviarAlertaTelegramSegura(ultimoNivelGas, mq2, mq5);
    }
    
    // 🔴 PELIGRO
    if (ultimoNivelGas >= UMBRAL_PELIGRO) {
      estadoActual = PELIGRO;
      valvulaCerrada = true;
      conteoActivo = false;
      aperturaPermitida = false;
      botonFisicoPresionado = false;
      contadorOK = 0;
      digitalWrite(RELAY_PIN, LOW);
      mostrarEstadoLED(PELIGRO);
      
      ultimoEvento = "🚨 PELIGRO CRÍTICO - Nivel: " + String(ultimoNivelGas) + " - EVACUAR ÁREA";
      Serial.println("PELIGRO");
    }
    // 🟢 SEGURO
    else if (estadoActual == OK_SEGURO) {
      mostrarEstadoLED(OK_SEGURO);
      ultimoEvento = "✅ Sistema en estado SEGURO - Nivel: " + String(ultimoNivelGas);
      Serial.println("SEGURO");
    }
    // 🟠 ALERTA con gas bajo (<750)
    else if (ultimoNivelGas < UMBRAL_ALERTA) {
      if (conteoActivo) {
        contadorOK++;
        Serial.print("OK ");
        Serial.print(contadorOK);
        Serial.print("/15 | ");
        
        if (contadorOK >= OK_REQUERIDOS) {
          conteoActivo = false;
          aperturaPermitida = true;
          ultimoEvento = "✅ Verificación completada (" + String(contadorOK) + "/15) - Puede confirmar la apertura";
          Serial.println("LISTO PARA CONFIRMAR (esperando confirmación manual)");
          
          if (telegramIniciado) {
            enviarTelegram("✅ VERIFICACIÓN COMPLETADA%0A"
                           "Se han verificado " + String(contadorOK) + " lecturas seguras%0A"
                           "📱 Para abrir la válvula, confirme con:%0A"
                           "• App web: Botón 'CONFIRMAR APERTURA'%0A"
                           "• Telegram: Enviar /abrir nuevamente");
          }
        } else {
          estadoActual = ALERTA;
          mostrarEstadoLED(ALERTA);
          ultimoEvento = "🔄 Verificando estabilidad... " + String(contadorOK) + "/15";
          Serial.println("VERIFICANDO ESTABILIDAD...");
        }
      }
      else if (aperturaPermitida) {
        estadoActual = ALERTA;
        mostrarEstadoLED(ALERTA);
        ultimoEvento = "⏳ Esperando confirmación para abrir válvula";
        Serial.println("ESPERANDO CONFIRMACIÓN MANUAL...");
      }
      else {
        estadoActual = ALERTA;
        mostrarEstadoLED(ALERTA);
        
        if (botonFisicoPresionado) {
          conteoActivo = true;
          aperturaPermitida = false;
          botonFisicoPresionado = false;
          contadorOK = 1;
          ultimoEvento = "🔘 Botón físico presionado - Iniciando verificación de estabilidad (1/15)";
          Serial.print("OK ");
          Serial.print(contadorOK);
          Serial.print("/15 | INICIANDO CONTEO...");
        } else {
          if (ultimoNivelGas < 350) {
            ultimoEvento = "⚠️  Nivel de gas MUY BAJO (" + String(ultimoNivelGas) + ") - Presiona el botón físico para verificar";
          } else {
            ultimoEvento = "⚠️  Nivel de gas BAJO (" + String(ultimoNivelGas) + ") - Presiona el botón físico para verificar estabilidad";
          }
          Serial.println("GAS BAJO, ESPERANDO BOTON FISICO...");
        }
      }
    }
    // 🟠 ALERTA con gas medio (entre 750 y 900)
    else {
      estadoActual = ALERTA;
      valvulaCerrada = true;
      conteoActivo = false;
      aperturaPermitida = false;
      botonFisicoPresionado = false;
      contadorOK = 0;
      digitalWrite(RELAY_PIN, LOW);
      mostrarEstadoLED(ALERTA);
      
      if (ultimoNivelGas >= 850) {
        ultimoEvento = "⚠️  ALERTA - Nivel elevado (" + String(ultimoNivelGas) + ") - Verificar posibles fugas";
      } else if (ultimoNivelGas >= 800) {
        ultimoEvento = "⚠️  ALERTA - Nivel medio-alto (" + String(ultimoNivelGas) + ") - Monitorear constantemente";
      } else {
        ultimoEvento = "⚠️  ALERTA - Nivel medio (" + String(ultimoNivelGas) + ") - Se precisa revisión manual";
      }
      Serial.println("ALERTA");
    }
    
    digitalWrite(RELAY_PIN, valvulaCerrada ? LOW : HIGH);
  }
  
  // ==== BUZZER ====
  if (estadoActual == PELIGRO) {
    tone(BUZZER_PIN, 2500);
  }
  else if (estadoActual == ALERTA && !conteoActivo) {
    if (millis() - buzzerAnterior >= 350) {
      buzzerAnterior = millis();
      buzzerEstado = !buzzerEstado;
      buzzerEstado ? tone(BUZZER_PIN, 1200) : noTone(BUZZER_PIN);
    }
  }
  else {
    noTone(BUZZER_PIN);
  }
  
  // ==== ENVÍO DE DATOS A LA APP ====
  static unsigned long ultimoEnvio = 0;
  if (millis() - ultimoEnvio >= 500) {
    ultimoEnvio = millis();
    enviarDatosWebSocket();
  }
}