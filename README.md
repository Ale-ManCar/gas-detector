# 🚨 Detector de Gas con Arduino - Sistema IoT

Sistema de detección de fugas de gas con Arduino que envía alertas instantáneas a Telegram y monitorea en tiempo real a través de una app web.

## 🛠️ Tecnologías Utilizadas

### Hardware
- ESP32
- Sensor MQ-2/MQ-5 (Gas LPG/Propano)
- Módulo Wi-Fi ESP8266/ESP32
- Buzzer activo
- LED indicador
- Resistores y cables

### Software
- Arduino IDE (C++)
- Telegram Bot API
- Node.js (Backend opcional)
- JavaScript/HTML/CSS (Dashboard web)

## 📋 Características

- ✅ **Detección en tiempo real** de concentraciones peligrosas de gas
- ✅ **Alertas instantáneas** vía Telegram Bot
- ✅ **Dashboard web** para monitoreo remoto
- ✅ **Indicadores visuales y auditivos** locales
- ✅ **Umbrales configurables** según sensibilidad
- ✅ **Registro histórico** de eventos

## 📡 Configuración WiFi y Telegram

1. **Configurar credenciales WiFi:**
```cpp
const char* ssid = "TU_SSID";
const char* password = "TU_PASSWORD";
