// Interruptor astronómico para luz parking - VERSIÓN 1.2 (Laia)
// Se activa a la puesta de sol y desactiva al amanecer.
// Se basa en el algoritmo de Meeus
// Concepto y Programación: Vicente Soriano
// Revisión Octubre 2025
// ----------------------------------------------------------
// MEJORAS v1.3
// 31/12/25
// - Sistema de fallback RTC/NTP/Cache
// - Reconexión RTC programada cada 6 horas
// - Correción compensación UTC no funcionaba
// - Verificación robusta de estado RTC
// - Fuente de tiempo unificada en toda la aplicación
// - Código modularizado (config.h + web_interface.h)
// - Conexiones inactivas son expulsadas tras 5 minutos y baja la potencia del Wifi a 5mW de nuevo
// - Ahora ya realiza el ajuste automático entre hora de invierno/verano
// - Actualización por OTA y desconexión a los 4 minutos si no se realiza envío
// - Placa ESP32 Wrover Module 1.9MB-APP/128KB-SPIFFS (Flash Mode QIO)
// - Primer Flash borrado completo


#include <WiFi.h>
#include <time.h>
#include <WebServer.h>
#include <Wire.h>
#include <RTClib.h>
#include <EEPROM.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include "esp_task_wdt.h"
#include "driver/ledc.h"
#include "esp_wifi.h"

// Incluir archivos de configuración
#include "config.h"
#include "web_interface.h"

// ========================================
// VARIABLES GLOBALES
// ========================================

// Wifi dinámica activada por evento
bool potenciaAlta = false;
unsigned long ultimaActividadHTTP = 0;

// Estado NTP
NTPState ntpState = {"", "", 0, 0, false, false, ""};

// Cache de tiempo RTC
CacheRTC cacheRTC = {DateTime(2025, 1, 1, 0, 0, 0), 0, false};

// Cache de temperatura DS3231
CacheTemperatura cacheTempDS3231 = {-999.9f, 0, false};

// Estructura de estado del watchdog
WatchdogState wdState = {0, 0, 0, 0xFFFFFFFF, 0, false, false, false, 0, false};

// RTC
RTC_DS3231 rtc;
bool rtcConectado = false;

// Servidor web
WebServer server(80);

// DNS Server para Captive Portal
DNSServer dnsServer;
const byte DNS_PORT = 53;

// Variables de configuración
int horaEncendido = 18;
int minutoEncendido = 30;
int horaApagado = 7;
int minutoApagado = 0;

// Variables de ubicación por defecto (Barcelona)
float latitud = 41.412418;
float longitud = 2.139698;
int utcOffset = 1;

// Estado del sistema
EstadoSistema estado = {true, false, false, 0};

// Estado OTA
bool otaHabilitado = false;
unsigned long otaHabilitadoDesde = 0;

// Cache de horarios astronómicos
int ultimoDiaCalculado = -1;
int amanecerCache = -1;
int atardecerCache = -1;

// Mutex para proteger acceso al relé contra race conditions
static portMUX_TYPE releMux = portMUX_INITIALIZER_UNLOCKED;

// ========================================
// FUNCIÓN HELPER PARA MILLIS() MEJORADA
// ========================================
// Usa aritmética de complemento a dos para manejar overflow automáticamente
unsigned long millisSafe(unsigned long referencia) {
    return millis() - referencia;
}

// ========================================
// FUNCIONES HELPER PARA REDUCIR DUPLICACIÓN
// ========================================

// Verificar conectividad I2C con RTC
bool verificarConexionI2C_RTC() {
    Wire.beginTransmission(0x68);
    return (Wire.endTransmission() == 0);
}

// Validar si un año es válido para el sistema
bool esAñoValido(int year) {
    return (year >= 2024 && year <= 2050);
}

// Formatear minutos totales a formato HH:MM en buffer
void formatearHora(int minutosTotales, char* buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize, "%02d:%02d", minutosTotales / 60, minutosTotales % 60);
}

// Validar fecha completa (año, mes, día)
bool esFechaValida(int year, int month, int day) {
    if (!esAñoValido(year)) return false;
    if (month < 1 || month > 12) return false;
    if (day < 1 || day > 31) return false;

    // Validar días por mes (simplificado)
    if (month == 2 && day > 29) return false;
    if ((month == 4 || month == 6 || month == 9 || month == 11) && day > 30) return false;

    return true;
}

// Validar hora completa (hora, minuto, segundo)
bool esHoraValida(int hour, int minute, int second) {
    return (hour >= 0 && hour <= 23 &&
            minute >= 0 && minute <= 59 &&
            second >= 0 && second <= 59);
}

// Controlar relé de forma segura (con protección atómica)
void setReleEstado(bool encender) {
    portENTER_CRITICAL(&releMux);
    digitalWrite(RELE_PIN, encender ? HIGH : LOW);
    estado.lucesOn = encender;
    portEXIT_CRITICAL(&releMux);
}

// Leer estado del relé de forma segura
bool getReleEstado() {
    portENTER_CRITICAL(&releMux);
    bool estado = digitalRead(RELE_PIN) == HIGH;
    portEXIT_CRITICAL(&releMux);
    return estado;
}

// Formatear uptime en formato legible
void formatearUptime(unsigned long milliseconds, char* buffer, size_t bufferSize) {
    unsigned long segundos = milliseconds / 1000;
    unsigned long dias = segundos / 86400;
    unsigned long horas = (segundos % 86400) / 3600;
    unsigned long minutos = (segundos % 3600) / 60;

    snprintf(buffer, bufferSize, "%lud %02luh %02lum", dias, horas, minutos);
}

// Validar coordenadas geográficas
bool sonCoordenadasValidas(float lat, float lon) {
    return (lat >= -90.0f && lat <= 90.0f &&
            lon >= -180.0f && lon <= 180.0f);
}

// ========================================
// GESTIÓN CACHE RTC MEJORADA
// ========================================
DateTime obtenerTiempoRTC() {
    unsigned long ahora = millis();

    if (cacheRTC.valido &&
        (millisSafe(cacheRTC.timestampLectura) < INTERVALO_LECTURA_RTC) &&
        (millisSafe(cacheRTC.timestampLectura) < CACHE_RTC_TIMEOUT)) {
        return cacheRTC.ultimaLectura;
    }

    if (rtcConectado) {
        cacheRTC.ultimaLectura = rtc.now();
        cacheRTC.timestampLectura = ahora;
        cacheRTC.valido = true;
    }

    return cacheRTC.ultimaLectura;
}

void invalidarCacheRTC() {
    cacheRTC.valido = false;
}

// ========================================
// SISTEMA DE FALLBACK DE TIEMPO MEJORADO
// ========================================
bool obtenerTiempoSistema(DateTime* tiempo) {
    // PRIORIDAD 1: RTC físico (si está disponible y válido)
    if (rtcConectado) {
        if (verificarConexionI2C_RTC()) {
            DateTime ahora = rtc.now();
            if (esAñoValido(ahora.year())) {
                *tiempo = ahora;
                DEBUG_PRINTLN(F("🕐 Fuente: RTC físico"));
                return true;
            } else {
                DEBUG_PRINTLN(F("⚠️ RTC con fecha inválida"));
                rtcConectado = false;
            }
        } else {
            DEBUG_PRINTLN(F("❌ RTC no responde I2C"));
            rtcConectado = false;
        }
    }

    // PRIORIDAD 2: Cache RTC (si el RTC físico falló pero tenemos cache válido)
    DateTime cache = obtenerTiempoRTC();
    if (esAñoValido(cache.year())) {
        *tiempo = cache;
        DEBUG_PRINTLN(F("🕐 Fuente: Cache RTC"));
        return true;
    }

    DEBUG_PRINTLN(F("❌ Sin fuente de tiempo disponible"));
    return false;
}

// ========================================
// RECONEXIÓN PROGRAMADA DEL RTC (6 HORAS)
// ========================================
void verificarReconexionRTC() {
    static unsigned long ultimaVerificacion = 0;
    const unsigned long INTERVALO_RECONEXION = 21600000UL; // 6 horas

    if (millisSafe(ultimaVerificacion) < INTERVALO_RECONEXION) return;
    ultimaVerificacion = millis();

    if (!rtcConectado) {
        DEBUG_PRINTLN(F("🔄 Verificación RTC programada (6h)..."));

        if (verificarConexionI2C_RTC()) {
            rtcConectado = true;
            invalidarCacheRTC();
            DEBUG_PRINTLN(F("✅ RTC reconectado"));
            // Nota: El RTC será recalibrado en la próxima sincronización NTP automática
        } else {
            DEBUG_PRINTLN(F("❌ RTC sigue desconectado"));
        }
    }
}

// ========================================
// INDICADOR PWM "ORGÁNICO" COMBINADO
// ========================================
void actualizarIndicadorHumano() {
    static int clientesCache = 0;
    static unsigned long ultimaConsultaClientes = 0;
    static unsigned long ultimoCambioPWM = 0;
    static int brilloActual = 0;
    static int direccionBrillo = 1;
    static bool bateriaRTCCache = false;
    static unsigned long ultimaConsultaBateria = 0;

    // Actualizar cache de clientes WiFi cada 2 segundos
    if (millisSafe(ultimaConsultaClientes) >= 2000) {
        clientesCache = WiFi.softAPgetStationNum();
        ultimaConsultaClientes = millis();
    }

    // Actualizar cache de batería RTC cada 24 horas
    if (millisSafe(ultimaConsultaBateria) >= 86400000UL) {
        if (rtcConectado && verificarConexionI2C_RTC()) {
            bateriaRTCCache = rtc.lostPower();
        } else {
            bateriaRTCCache = false;
        }
        ultimaConsultaBateria = millis();
    }

    // PRIORIDAD 1: Batería RTC agotada - Parpadeo SOS rápido
    if (bateriaRTCCache) {
        if (millisSafe(ultimoCambioPWM) >= 200) {  // Parpadeo cada 200ms
            ultimoCambioPWM = millis();
            brilloActual = (brilloActual == 0) ? 255 : 0;
            ledcWrite(LED_WIFI, brilloActual);
        }
        return;
    }

    // PRIORIDAD 2: Luces encendidas - Respiración o fijo según modo
    if (estado.lucesOn) {
        if (estado.modoAuto) {
            // Modo AUTO: respiración suave
            if (millisSafe(ultimoCambioPWM) >= 50) {
                ultimoCambioPWM = millis();
                brilloActual += direccionBrillo * 25;

                if (brilloActual >= 255) {
                    brilloActual = 255;
                    direccionBrillo = -1;
                } else if (brilloActual <= 80) {
                    brilloActual = 80;
                    direccionBrillo = 1;
                }

                ledcWrite(LED_WIFI, brilloActual);
            }
        } else {
            // Modo MANUAL: encendido fijo
            ledcWrite(LED_WIFI, 255);
        }
        return;
    }

    // PRIORIDAD 3: Cliente WiFi conectado - Respiración rápida
    if (clientesCache > 0) {
        if (millisSafe(ultimoCambioPWM) >= 30) {
            ultimoCambioPWM = millis();
            brilloActual += direccionBrillo * 15;

            if (brilloActual >= 200) {
                brilloActual = 200;
                direccionBrillo = -1;
            } else if (brilloActual <= 20) {
                brilloActual = 20;
                direccionBrillo = 1;
            }

            ledcWrite(LED_WIFI, brilloActual);
        }
        return;
    }

    // PRIORIDAD 4: Modo normal - Respiración muy suave
    if (millisSafe(ultimoCambioPWM) >= 100) {
        ultimoCambioPWM = millis();
        brilloActual += direccionBrillo * 3;

        if (brilloActual >= 80) {
            brilloActual = 80;
            direccionBrillo = -1;
        } else if (brilloActual <= 0) {
            brilloActual = 0;
            direccionBrillo = 1;
        }

        ledcWrite(LED_WIFI, brilloActual);
    }
}

// ========================================
// TEMPERATURA INTERIOR
// ========================================
float leerTemperaturaDS3231() {
    unsigned long ahora = millis();

    if (cacheTempDS3231.valido && (millisSafe(cacheTempDS3231.timestamp) < INTERVALO_TEMP_DS3231)) {
        return cacheTempDS3231.temperatura;
    }

    Wire.beginTransmission(0x68);
    Wire.write(0x11);
    if (Wire.endTransmission() != 0) {
        cacheTempDS3231.valido = false;
        return -999.9f;
    }

    Wire.requestFrom(0x68, 2);
    if (Wire.available() >= 2) {
        int8_t temp_entera = Wire.read();
        uint8_t temp_fraccion = Wire.read();
        cacheTempDS3231.temperatura = temp_entera + (temp_fraccion >> 6) * 0.25f;
        cacheTempDS3231.timestamp = ahora;
        cacheTempDS3231.valido = true;
        return cacheTempDS3231.temperatura;
    }

    cacheTempDS3231.valido = false;
    return -999.9f;
}

// ========================================
// POTENCIA WIFI DINÁMICA
// ========================================
void ajustarPotenciaWiFi(bool altaPotencia) {
    if (altaPotencia != potenciaAlta) {
        wifi_power_t potencia = altaPotencia ? WIFI_POTENCIA_ALTA : WIFI_POTENCIA_BAJA;

        if (WiFi.setTxPower(potencia)) {
            potenciaAlta = altaPotencia;
            Serial.print("📡 Potencia WiFi: ");
            Serial.println(altaPotencia ? "ALTA (100 mW)" : "BAJA (5 mW)");
        }
    }
}

void verificarClientesInactivos() {
    // Solo verificar si hay clientes conectados
    int clientesConectados = WiFi.softAPgetStationNum();
    if (clientesConectados == 0) return;

    // Verificar si se ha superado el timeout de inactividad
    unsigned long tiempoInactivo = millisSafe(ultimaActividadHTTP);

    if (tiempoInactivo >= TIMEOUT_INACTIVIDAD_CLIENTE) {
        Serial.print(F("⏱️ Clientes inactivos por "));
        Serial.print(tiempoInactivo / 1000);
        Serial.print(F(" segundos ("));
        Serial.print(clientesConectados);
        Serial.println(F(" cliente(s) conectado(s))"));

        // Desconectar todos los clientes reiniciando el AP
        Serial.println(F("🚫 Reiniciando AP para desconectar clientes..."));
        WiFi.softAPdisconnect(false);  // false = no apagar el AP, solo desconectar clientes
        delay(100);
        WiFi.softAP(ssid, password, 8);
        WiFi.softAPConfig(IPAddress(192, 168, 4, 1),
                          IPAddress(192, 168, 4, 1),
                          IPAddress(255, 255, 255, 0));

        Serial.println(F("✅ AP reiniciado - Clientes desconectados"));

        // Resetear timestamp para evitar expulsiones repetitivas inmediatas
        ultimaActividadHTTP = millis();
    }
}

// ========================================
// ALGORITMO ASTRONÓMICO
// ========================================
float calcularJulianDay(int year, int month, int day) {
    if (month <= 2) {
        year -= 1;
        month += 12;
    }
    int A = year / 100;
    int B = 2 - A + (A / 4);
    return (int)(365.25 * (year + 4716)) + (int)(30.6001 * (month + 1)) + day + B - 1524.5;
}

int calcularSolar(DateTime ahora, float lat, float lon, int utcOff, bool esVerano, bool amanecer) {
    float JD = calcularJulianDay(ahora.year(), ahora.month(), ahora.day());
    float n = JD - 2451545.0 + 0.0008;
    float L = fmod(280.460 + 0.9856474 * n, 360.0);
    float g = fmod(357.528 + 0.9856003 * n, 360.0);
    float gRad = g * DEG_TO_RAD;
    float lambda = L + 1.915 * sin(gRad) + 0.020 * sin(2 * gRad);
    float lambdaRad = lambda * DEG_TO_RAD;
    float epsilon = 23.439 - 0.0000004 * n;
    float epsilonRad = epsilon * DEG_TO_RAD;
    float declinacion = asin(sin(epsilonRad) * sin(lambdaRad)) * RAD_TO_DEG;
    float latRad = lat * DEG_TO_RAD;
    float decRad = declinacion * DEG_TO_RAD;
    float cosH = (sin(-0.833 * DEG_TO_RAD) - sin(latRad) * sin(decRad)) / (cos(latRad) * cos(decRad));

    if (cosH > 1.0) return amanecer ? 0 : 1439;
    if (cosH < -1.0) return amanecer ? 1439 : 0;

    float H = acos(cosH) * RAD_TO_DEG;
    float E = L - atan2(sin(lambdaRad), cos(epsilonRad) * cos(lambdaRad)) * RAD_TO_DEG;
    E = fmod(E + 180, 360) - 180;
    float tSolar = amanecer ? (12.0 - H / 15.0) : (12.0 + H / 15.0);
    float tLocal = tSolar - (lon / 15.0) - (E / 60.0) + utcOff + (esVerano ? 1 : 0);

    while (tLocal < 0) tLocal += 24;
    while (tLocal >= 24) tLocal -= 24;

    return (int)(tLocal * 60.0);
}

bool esHorarioVerano(DateTime ahora) {
    int mes = ahora.month();
    int dia = ahora.day();

    if (mes < 3 || mes > 10) return false;
    if (mes > 3 && mes < 10) return true;

    int ultimoDomingo = 31;
    while (ultimoDomingo > 0) {
        DateTime temp(ahora.year(), mes, ultimoDomingo, 0, 0, 0);
        if (temp.dayOfTheWeek() == 0) break;
        ultimoDomingo--;
    }

    if (mes == 3) return dia >= ultimoDomingo;
    if (mes == 10) return dia < ultimoDomingo;

    return false;
}

int calcularAmanecer(DateTime ahora) {
    bool esVerano = esHorarioVerano(ahora);
    return calcularSolar(ahora, latitud, longitud, utcOffset, esVerano, true);
}

int calcularAtardecer(DateTime ahora) {
    bool esVerano = esHorarioVerano(ahora);
    return calcularSolar(ahora, latitud, longitud, utcOffset, esVerano, false);
}

// ========================================
// FUNCIONES DE CONTROL
// ========================================
void sincronizarConAstronomico() {
    DateTime ahora;
    if (!obtenerTiempoSistema(&ahora)) {
        Serial.println(F("❌ No se puede sincronizar - Sin fuente de tiempo"));
        return;
    }

    int amanecer = calcularAmanecer(ahora);
    int atardecer = calcularAtardecer(ahora);
    int minutosActuales = ahora.hour() * 60 + ahora.minute();

    bool deberianEstarEncendidas = (minutosActuales >= atardecer) || (minutosActuales < amanecer);

    setReleEstado(deberianEstarEncendidas);
    estado.comandoManual = false;

    Serial.print("🔀 Sincronizado - Luces ");
    Serial.println(deberianEstarEncendidas ? "ACTIVADAS" : "DESACTIVADAS");
}

void invalidarCacheHorarios() {
    // Fuerza recálculo de horarios astronómicos en el próximo ciclo
    ultimoDiaCalculado = -1;
    DEBUG_PRINTLN(F("🔄 Cache horarios invalidado"));
}

void controlarLucesAutomatico() {
    static unsigned long ultimaComprobacion = 0;

    DateTime ahora;
    if (!obtenerTiempoSistema(&ahora)) {
        return;
    }

    if (!estado.modoAuto) return;

    if (estado.comandoManual && (millisSafe(estado.ultimoCambioManual) < 2000)) {
        return;
    }

    if (millisSafe(ultimaComprobacion) < 1000) return;
    ultimaComprobacion = millis();

    if (ahora.day() != ultimoDiaCalculado) {
        amanecerCache = calcularAmanecer(ahora);
        atardecerCache = calcularAtardecer(ahora);
        ultimoDiaCalculado = ahora.day();
    }

    int minutosActuales = ahora.hour() * 60 + ahora.minute();
    bool deberianEstarEncendidas = (minutosActuales >= atardecerCache) || (minutosActuales < amanecerCache);

    if (deberianEstarEncendidas != estado.lucesOn && !estado.comandoManual) {
        setReleEstado(deberianEstarEncendidas);

        Serial.print("💡 Luces ");
        Serial.println(deberianEstarEncendidas ? "ACTIVADAS" : "DESACTIVADAS");
    }

    if (estado.comandoManual && (millisSafe(estado.ultimoCambioManual) >= 2000)) {
        sincronizarConAstronomico();
    }
}

// ========================================
// VERIFICACIÓN DE BATERÍA RTC MEJORADA
// ========================================
void obtenerEstadoBateriaCompleto(char* buffer, size_t bufferSize) {
    if (!rtcConectado) {
        snprintf(buffer, bufferSize, "❌ RTC desconectado");
        return;
    }

    if (!verificarConexionI2C_RTC()) {
        snprintf(buffer, bufferSize, "❌ RTC no responde");
        rtcConectado = false;
        return;
    }

    DateTime ahora = rtc.now();

    if (!esAñoValido(ahora.year())) {
        snprintf(buffer, bufferSize, "⚠️ Hora inválida (%d)", ahora.year());
        return;
    }

    if (rtc.lostPower()) {
        snprintf(buffer, bufferSize, "❌ Batería agotada");
        return;
    }

    Wire.beginTransmission(0x68);
    Wire.write(0x0F);
    if (Wire.endTransmission() != 0) {
        snprintf(buffer, bufferSize, "⚠️ Error comunicación");
        return;
    }

    Wire.requestFrom(0x68, 1);
    if (Wire.available()) {
        byte status = Wire.read();
        if (status & 0x80) {
            snprintf(buffer, bufferSize, "⚠️ Oscilador detenido");
            return;
        }
    }

    snprintf(buffer, bufferSize, "✅ OK");
}

// ========================================
// FUNCIONES DE CONFIGURACIÓN
// ========================================
void guardarConfiguracion() {
    bool cambios = false;

    if (EEPROM.read(ADDR_HORA_ON) != horaEncendido) {
        EEPROM.write(ADDR_HORA_ON, horaEncendido);
        cambios = true;
    }
    if (EEPROM.read(ADDR_MINUTO_ON) != minutoEncendido) {
        EEPROM.write(ADDR_MINUTO_ON, minutoEncendido);
        cambios = true;
    }
    if (EEPROM.read(ADDR_HORA_OFF) != horaApagado) {
        EEPROM.write(ADDR_HORA_OFF, horaApagado);
        cambios = true;
    }
    if (EEPROM.read(ADDR_MINUTO_OFF) != minutoApagado) {
        EEPROM.write(ADDR_MINUTO_OFF, minutoApagado);
        cambios = true;
    }
    if (EEPROM.read(ADDR_MODO_AUTO) != (byte)estado.modoAuto) {
        EEPROM.write(ADDR_MODO_AUTO, estado.modoAuto);
        cambios = true;
    }

    float latitudActual, longitudActual;
    EEPROM.get(ADDR_LATITUD, latitudActual);
    EEPROM.get(ADDR_LONGITUD, longitudActual);

    if (fabs(latitudActual - latitud) > 0.0001f) {
        EEPROM.put(ADDR_LATITUD, latitud);
        cambios = true;
    }
    if (fabs(longitudActual - longitud) > 0.0001f) {
        EEPROM.put(ADDR_LONGITUD, longitud);
        cambios = true;
    }

    int utcOffsetActual = EEPROM.read(ADDR_UTC_OFFSET) - 128;
    if (utcOffsetActual != utcOffset) {
        EEPROM.write(ADDR_UTC_OFFSET, utcOffset + 128);
        cambios = true;
    }

    if (cambios) {
        EEPROM.commit();
        DEBUG_PRINTLN(F("💾 Configuración guardada"));
    }
}

void cargarConfiguracion() {
    EEPROM.begin(EEPROM_SIZE);

    horaEncendido = EEPROM.read(ADDR_HORA_ON);
    minutoEncendido = EEPROM.read(ADDR_MINUTO_ON);
    horaApagado = EEPROM.read(ADDR_HORA_OFF);
    minutoApagado = EEPROM.read(ADDR_MINUTO_OFF);

    byte modoAutoByte = EEPROM.read(ADDR_MODO_AUTO);
    estado.modoAuto = (modoAutoByte == 255) ? true : (bool)modoAutoByte;

    EEPROM.get(ADDR_LATITUD, latitud);
    EEPROM.get(ADDR_LONGITUD, longitud);
    utcOffset = EEPROM.read(ADDR_UTC_OFFSET) - 128;

    // Validar horas usando helper
    if (horaEncendido == 255 || !esHoraValida(horaEncendido, 0, 0)) horaEncendido = 18;
    if (minutoEncendido == 255 || minutoEncendido > 59) minutoEncendido = 30;
    if (horaApagado == 255 || !esHoraValida(horaApagado, 0, 0)) horaApagado = 7;
    if (minutoApagado == 255 || minutoApagado > 59) minutoApagado = 0;

    // Validar coordenadas usando helper
    if (isnan(latitud) || isnan(longitud) || !sonCoordenadasValidas(latitud, longitud)) {
        latitud = 41.412418;
        longitud = 2.139698;
    }

    if (utcOffset < -12 || utcOffset > 12) utcOffset = 1;

    estado.lucesOn = false;
    estado.comandoManual = false;
    estado.ultimoCambioManual = millis();

    Serial.println(F("✅ Configuración cargada"));
}

// ========================================
// PROTECCIÓN RESET LOOPS
// ========================================
void verificarResetRapido() {
    DateTime ahora;

    // Usar timestamp RTC en lugar de millis() para detectar resets rápidos
    // millis() se reinicia en cada boot, no sirve para comparar entre sesiones
    if (!obtenerTiempoSistema(&ahora)) {
        // Sin RTC válido, no podemos verificar resets rápidos de forma fiable
        wdState.fastResetCount = 0;
        return;
    }

    // Obtener timestamp del último boot (en segundos desde epoch)
    unsigned long ultimoBootEpoch = 0;
    EEPROM.get(ADDR_BOOT_TIMESTAMP, ultimoBootEpoch);

    // Convertir DateTime actual a timestamp epoch (segundos desde 2000-01-01)
    unsigned long ahoraEpoch = ahora.unixtime() - 946684800UL; // Ajuste para epoch 2000

    // Calcular tiempo transcurrido en segundos
    unsigned long tiempoTranscurrido = (ultimoBootEpoch > 0 && ahoraEpoch > ultimoBootEpoch)
        ? (ahoraEpoch - ultimoBootEpoch)
        : BOOT_INTERVAL_SAFE; // Si no hay lectura válida, asumir intervalo seguro

    // Si el tiempo desde el último boot es menor que el intervalo de seguridad (5 min = 300s),
    // consideramos que fue un reset rápido
    if (ultimoBootEpoch > 0 && tiempoTranscurrido < (BOOT_INTERVAL_SAFE / 1000UL)) {
        wdState.fastResetCount++;
        DEBUG_PRINT(F("⚠️ Reset rápido detectado #"));
        DEBUG_PRINTLN(wdState.fastResetCount);

        if (wdState.fastResetCount >= MAX_FAST_RESETS) {
            wdState.safeMode = true;
            EEPROM.write(ADDR_SAFE_MODE, 1);
            ntpState.enabled = false;
            DEBUG_PRINTLN(F("🛡️ MODO SEGURO ACTIVADO"));
        }
    } else {
        wdState.fastResetCount = 0;
    }

    EEPROM.put(ADDR_BOOT_TIMESTAMP, ahoraEpoch);
    EEPROM.commit();
}

// ========================================
// WATCHDOG MEJORADO
// ========================================
void inicializarWatchdog() {
    DEBUG_PRINTLN(F("🛡️ Inicializando Watchdog..."));

    esp_task_wdt_add(NULL);

    wdState.resetCount = EEPROM.read(ADDR_RESET_COUNT);
    if (wdState.resetCount == 0xFF) wdState.resetCount = 0;

    uint8_t lastReason = EEPROM.read(ADDR_LAST_RESET_REASON);

    wdState.resetCount++;
    EEPROM.write(ADDR_RESET_COUNT, wdState.resetCount);

    verificarResetRapido();

    DEBUG_PRINT(F("🔄 Reset #"));
    DEBUG_PRINTLN(wdState.resetCount);

    EEPROM.write(ADDR_LAST_RESET_REASON, RESET_NORMAL);
    EEPROM.commit();

    DEBUG_PRINTLN(F("✅ Watchdog activo"));
}

bool verificarMemoria() {
    uint32_t heapActual = ESP.getFreeHeap();

    if (heapActual < wdState.minHeapEver) {
        wdState.minHeapEver = heapActual;
        EEPROM.put(ADDR_HEAP_MIN_RECORD, wdState.minHeapEver);
        EEPROM.commit();
    }

    if (heapActual < MIN_HEAP_SAFE) {
        if (!wdState.heapCritical) {
            DEBUG_PRINT(F("🚨 MEMORIA CRÍTICA: "));
            DEBUG_PRINT(heapActual);
            DEBUG_PRINTLN(F(" bytes"));
            wdState.heapCritical = true;
        }

        if (heapActual < 20000) {
            DEBUG_PRINTLN(F("💥 MEMORIA AGOTADA - Reset"));
            EEPROM.write(ADDR_LAST_RESET_REASON, RESET_HEAP_LOW);
            EEPROM.commit();
            delay(100);
            ESP.restart();
        }
        return false;
    } else {
        if (wdState.heapCritical) {
            DEBUG_PRINTLN(F("✅ Memoria recuperada"));
            wdState.heapCritical = false;
        }
    }

    return true;
}

bool verificarRTC() {
    DateTime ahora = obtenerTiempoRTC();

    if (!esAñoValido(ahora.year())) {
        if (!wdState.rtcFailDetected) {
            DEBUG_PRINTLN(F("⚠️ RTC fecha inválida"));
            wdState.rtcFailDetected = true;
        }

        Wire.begin(I2C_SDA, I2C_SCL);
        delay(50);

        if (rtc.begin()) {
            DEBUG_PRINTLN(F("✅ RTC reconectado"));
            rtcConectado = true;
            wdState.rtcFailDetected = false;
            invalidarCacheRTC();
            return true;
        } else {
            DEBUG_PRINTLN(F("❌ RTC no responde"));
            rtcConectado = false;
            return false;
        }
    }

    if (wdState.rtcFailDetected) {
        DEBUG_PRINTLN(F("✅ RTC OK"));
        wdState.rtcFailDetected = false;
    }

    return true;
}

bool verificarRele() {
    bool releEncendido = getReleEstado();
    bool lucesEstado = estado.lucesOn;

    if (releEncendido != lucesEstado) {
        DEBUG_PRINTLN(F("⚠️ Inconsistencia RELÉ"));
        setReleEstado(estado.lucesOn);
        DEBUG_PRINTLN(F("✅ Relé corregido"));
        return false;
    }

    return true;
}

void ejecutarWatchdog() {
    // Protección atómica contra condición de carrera
    static portMUX_TYPE watchdogMux = portMUX_INITIALIZER_UNLOCKED;

    portENTER_CRITICAL(&watchdogMux);
    if (wdState.ejecutandoWD) {
        portEXIT_CRITICAL(&watchdogMux);
        return;
    }
    wdState.ejecutandoWD = true;
    portEXIT_CRITICAL(&watchdogMux);

    unsigned long ahora = millis();

    esp_task_wdt_reset();

    if (millisSafe(wdState.lastHeapCheck) >= CHECK_HEAP_INTERVAL) {
        wdState.lastHeapCheck = ahora;
        verificarMemoria();
    }

    if (millisSafe(wdState.lastRTCCheck) >= CHECK_RTC_INTERVAL) {
        wdState.lastRTCCheck = ahora;
        if (!verificarRTC() && rtcConectado) {
            static uint8_t fallosRTC = 0;
            fallosRTC++;
            if (fallosRTC >= 3) {
                DEBUG_PRINTLN(F("💥 RTC fallo permanente"));
                EEPROM.write(ADDR_LAST_RESET_REASON, RESET_RTC_FAIL);
                EEPROM.commit();
                delay(100);
                ESP.restart();
            }
        }
    }

    if (millisSafe(wdState.lastReleCheck) >= CHECK_RELE_INTERVAL) {
        wdState.lastReleCheck = ahora;
        verificarRele();
    }

    // Verificar clientes WiFi inactivos (aprovechamos el ciclo del watchdog)
    verificarClientesInactivos();

    // Verificar reconexión RTC programada (aprovechamos el ciclo del watchdog)
    verificarReconexionRTC();

    portENTER_CRITICAL(&watchdogMux);
    wdState.ejecutandoWD = false;
    portEXIT_CRITICAL(&watchdogMux);
}

void obtenerEstadisticasWatchdog(char* buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize,
        "--- WATCHDOG ---\n"
        "🔄 Resets: %d\n"
        "💾 Heap min: %lu bytes\n"
        "💾 Heap actual: %d bytes\n"
        "⏱️ Timeout: %ds\n"
        "✅ RTC: %s\n"
        "✅ Memoria: %s\n"
        "🛡️ Modo seguro: %s\n"
        "⚡ Resets rápidos: %d/%d\n\n",
        wdState.resetCount,
        wdState.minHeapEver,
        ESP.getFreeHeap(),
        WDT_TIMEOUT,
        rtcConectado ? "OK" : "FAIL",
        wdState.heapCritical ? "CRÍTICA" : "OK",
        wdState.safeMode ? "ACTIVO" : "NORMAL",
        wdState.fastResetCount, MAX_FAST_RESETS
    );
}

// ========================================
// FUNCIONES NTP MEJORADAS
// ========================================
void cargarCredencialesWiFi() {
    bool ssidValido = false;
    for (int i = 0; i < 32; i++) {
        ntpState.ssid[i] = EEPROM.read(ADDR_WIFI_SSID + i);
        if (ntpState.ssid[i] == '\0' && i > 0) {
            ssidValido = true;
            break;
        }
    }
    if (!ssidValido) ntpState.ssid[0] = '\0';

    bool passValido = false;
    for (int i = 0; i < 64; i++) {
        ntpState.password[i] = EEPROM.read(ADDR_WIFI_PASS + i);
        if (ntpState.password[i] == '\0' && i > 0) {
            passValido = true;
            break;
        }
    }
    if (!passValido) ntpState.password[0] = '\0';

    EEPROM.get(ADDR_LAST_NTP_SYNC, ntpState.lastSync);

    byte enabled = EEPROM.read(ADDR_NTP_ENABLED);
    ntpState.enabled = (enabled == 1);

    if (wdState.safeMode) {
        ntpState.enabled = false;
    }

    if (ntpState.ssid[0] == 0xFF || ntpState.ssid[0] == '\0') {
        ntpState.enabled = false;
    }

    DEBUG_PRINT(F("📡 NTP: "));
    DEBUG_PRINTLN(ntpState.enabled ? "SÍ" : "NO");
}

void guardarCredencialesWiFi(const char* ssid, const char* password) {
    for (int i = 0; i < 32; i++) {
        EEPROM.write(ADDR_WIFI_SSID + i, i < strlen(ssid) ? ssid[i] : 0);
    }

    for (int i = 0; i < 64; i++) {
        EEPROM.write(ADDR_WIFI_PASS + i, i < strlen(password) ? password[i] : 0);
    }

    EEPROM.write(ADDR_NTP_ENABLED, 1);
    EEPROM.commit();

    strncpy(ntpState.ssid, ssid, 32);
    ntpState.ssid[32] = '\0';
    strncpy(ntpState.password, password, 64);
    ntpState.password[64] = '\0';
    ntpState.enabled = true;

    DEBUG_PRINTLN(F("✅ WiFi guardado"));
}

void borrarCredencialesWiFi() {
    for (int i = 0; i < 32; i++) {
        EEPROM.write(ADDR_WIFI_SSID + i, 0);
    }
    for (int i = 0; i < 64; i++) {
        EEPROM.write(ADDR_WIFI_PASS + i, 0);
    }
    EEPROM.write(ADDR_NTP_ENABLED, 0);
    EEPROM.commit();

    ntpState.ssid[0] = '\0';
    ntpState.password[0] = '\0';
    ntpState.enabled = false;

    DEBUG_PRINTLN(F("🗑️ WiFi borrado"));
}

bool pingGoogle() {
    DEBUG_PRINTLN(F("🌍 Verificando internet..."));

    IPAddress ip;
    if (WiFi.hostByName(PING_HOST, ip)) {
        DEBUG_PRINT(F("✅ Ping OK: "));
        DEBUG_PRINTLN(ip);
        return true;
    }

    DEBUG_PRINTLN(F("❌ Ping falló"));
    return false;
}

bool conectarWiFiCliente() {
    if (!ntpState.enabled || ntpState.ssid[0] == '\0') {
        DEBUG_PRINTLN(F("⚠️ Sin credenciales"));
        strncpy(ntpState.lastError, "Sin credenciales", sizeof(ntpState.lastError) - 1);
        return false;
    }

    DEBUG_PRINT(F("📡 Conectando: "));
    DEBUG_PRINTLN(ntpState.ssid);

    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ntpState.ssid, ntpState.password);

    unsigned long inicio = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millisSafe(inicio) > WIFI_CONNECT_TIMEOUT) {
            DEBUG_PRINTLN(F("❌ Timeout WiFi"));
            strncpy(ntpState.lastError, "Timeout", sizeof(ntpState.lastError) - 1);
            WiFi.disconnect();
            esp_task_wdt_reset();
            WiFi.mode(WIFI_AP);
            return false;
        }
        delay(500);
        esp_task_wdt_reset();
        DEBUG_PRINT(F("."));
    }

    DEBUG_PRINTLN(F("\n✅ WiFi conectado"));
    DEBUG_PRINT(F("📶 IP: "));
    DEBUG_PRINTLN(WiFi.localIP());

    return true;
}

bool sincronizarNTP() {
    DEBUG_PRINTLN(F("🕐 Sincronizando NTP..."));

    configTime(0, 0, NTP_SERVER_PRIMARY, NTP_SERVER_BACKUP);

    unsigned long inicio = millis();
    struct tm timeinfo;

    while (!getLocalTime(&timeinfo)) {
        if (millisSafe(inicio) > NTP_TIMEOUT) {
            DEBUG_PRINTLN(F("❌ Timeout NTP"));
            strncpy(ntpState.lastError, "Timeout NTP", sizeof(ntpState.lastError) - 1);
            return false;
        }
        esp_task_wdt_reset();
        delay(500);
        DEBUG_PRINT(F("."));
    }

    DEBUG_PRINTLN(F("\n✅ Hora NTP OK"));

    if (rtcConectado) {
        rtc.adjust(DateTime(
            timeinfo.tm_year + 1900,
            timeinfo.tm_mon + 1,
            timeinfo.tm_mday,
            timeinfo.tm_hour,
            timeinfo.tm_min,
            timeinfo.tm_sec
        ));

        invalidarCacheRTC();

        DEBUG_PRINT(F("🕐 RTC actualizado: "));
        DEBUG_PRINT(timeinfo.tm_mday);
        DEBUG_PRINT(F("/"));
        DEBUG_PRINT(timeinfo.tm_mon + 1);
        DEBUG_PRINT(F("/"));
        DEBUG_PRINTLN(timeinfo.tm_year + 1900);

        return true;
    } else {
        DEBUG_PRINTLN(F("⚠️ RTC no disponible"));
        strncpy(ntpState.lastError, "RTC no disponible", sizeof(ntpState.lastError) - 1);
        return false;
    }
}

void desconectarWiFiCliente() {
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    DEBUG_PRINTLN(F("📡 Volviendo a AP"));
}

bool ejecutarSincronizacionCompleta() {
    DEBUG_PRINTLN(F("\n========== SINC NTP =========="));

    ntpState.lastAttempt = millis();
    bool exito = false;

    if (!conectarWiFiCliente()) {
        desconectarWiFiCliente();
        return false;
    }

    delay(1000);
    esp_task_wdt_reset();

    if (!pingGoogle()) {
        strncpy(ntpState.lastError, "Sin internet", sizeof(ntpState.lastError) - 1);
        desconectarWiFiCliente();
        return false;
    }

    if (sincronizarNTP()) {
        ntpState.lastSync = millis();
        ntpState.lastSyncSuccess = true;
        ntpState.lastError[0] = '\0';

        EEPROM.put(ADDR_LAST_NTP_SYNC, ntpState.lastSync);
        EEPROM.commit();

        exito = true;
        DEBUG_PRINTLN(F("✅ Sync NTP exitosa"));

        // Reconexión RTC después de NTP exitoso
        if (!rtcConectado) {
            Wire.beginTransmission(0x68);
            if (Wire.endTransmission() == 0) {
                rtcConectado = true;
                invalidarCacheRTC();
                DEBUG_PRINTLN(F("✅ RTC reconectado tras NTP"));
            }
        }

        if (estado.modoAuto) {
            sincronizarConAstronomico();
        }
    } else {
        ntpState.lastSyncSuccess = false;
    }

    desconectarWiFiCliente();

    DEBUG_PRINTLN(F("========== FIN SINC ==========\n"));

    return exito;
}

void verificarSincronizacionAutomatica() {
    if (!ntpState.enabled) return;

    unsigned long tiempoDesdeSync = millisSafe(ntpState.lastSync);

    if (tiempoDesdeSync >= NTP_SYNC_INTERVAL) {
        DEBUG_PRINTLN(F("⏰ 12h - sync automática"));
        ejecutarSincronizacionCompleta();
    }
}

void obtenerEstadoNTP(char* buffer, size_t bufferSize) {
    int pos = 0;

    pos += snprintf(buffer + pos, bufferSize - pos,
        "--- NTP ---\n"
        "📡 Estado: %s\n",
        ntpState.enabled ? "HABILITADO" : "DESHABILITADO");

    if (ntpState.enabled && pos < (int)bufferSize) {
        pos += snprintf(buffer + pos, bufferSize - pos,
            "📶 SSID: %s\n", ntpState.ssid);

        if (ntpState.lastSync > 0 && pos < (int)bufferSize) {
            unsigned long tiempoDesdeSync = millisSafe(ntpState.lastSync) / 1000;
            unsigned long horas = tiempoDesdeSync / 3600;
            unsigned long minutos = (tiempoDesdeSync % 3600) / 60;

            pos += snprintf(buffer + pos, bufferSize - pos,
                "✅ Última sync: hace %luh %lum\n"
                "🎯 Resultado: %s\n",
                horas, minutos,
                ntpState.lastSyncSuccess ? "Perfecta" : "Falló");
        } else if (pos < (int)bufferSize) {
            pos += snprintf(buffer + pos, bufferSize - pos,
                "⚠️ Nunca sincronizado\n");
        }

        if (ntpState.lastError[0] != '\0' && pos < (int)bufferSize) {
            pos += snprintf(buffer + pos, bufferSize - pos,
                "❌ Error: %s\n", ntpState.lastError);
        }
    }

    if (pos < (int)bufferSize) {
        pos += snprintf(buffer + pos, bufferSize - pos, "\n");
    }
}

// ========================================
// SETUP Y LOOP
// ========================================
void setup() {
    Serial.begin(115200);
    delay(100);

    Serial.println(F("\n⚡" FIRMWARE_VERSION " ⚡"));
    Serial.println(F("━━━━━━━━━━━━━━━━━━━━━━━━"));

    EEPROM.begin(EEPROM_SIZE);
    cargarConfiguracion();
    inicializarWatchdog();
    cargarCredencialesWiFi();

    pinMode(RELE_PIN, OUTPUT);
    digitalWrite(RELE_PIN, LOW);

    ledcAttach(LED_WIFI, FRECUENCIA_PWM, RESOLUCION_PWM);
    ledcWrite(LED_WIFI, 0);

    Wire.begin(I2C_SDA, I2C_SCL);
    delay(100);

    if (!rtc.begin()) {
        Serial.println(F("❌ RTC no encontrado"));
        rtcConectado = false;
        digitalWrite(RELE_PIN, LOW);
        guardarConfiguracion();
    } else {
        Serial.println(F("✅ RTC conectado"));
        rtcConectado = true;

        if (rtc.lostPower()) {
            Serial.println(F("⚠️ RTC sin energía"));
            rtc.adjust(DateTime(2025, 11, 3, 12, 0, 0));
            invalidarCacheRTC();
        }

        DateTime ahora = rtc.now();
        Serial.print(F("📅 UTC:"));
        Serial.print(ahora.day()); Serial.print(F("/"));
        Serial.print(ahora.month()); Serial.print(F("/"));
        Serial.print(ahora.year()); Serial.print(F(" "));
        Serial.print(ahora.hour()); Serial.print(F(":"));
        if (ahora.minute() < 10) Serial.print(F("0"));
        Serial.println(ahora.minute());

        bool esVerano = esHorarioVerano(ahora);
        int amanecer = calcularAmanecer(ahora);
        int atardecer = calcularAtardecer(ahora);

        Serial.print(F("🌄 Amanecer: "));
        Serial.print(amanecer/60); Serial.print(F(":"));
        if (amanecer%60 < 10) Serial.print(F("0"));
        Serial.println(amanecer%60);

        Serial.print(F("🌅 Atardecer: "));
        Serial.print(atardecer/60); Serial.print(F(":"));
        if (atardecer%60 < 10) Serial.print(F("0"));
        Serial.println(atardecer%60);

        Serial.print(F("🌍 "));
        Serial.println(esVerano ? F("VERANO") : F("INVIERNO"));

        if (estado.modoAuto) {
            sincronizarConAstronomico();
        }
    }

    WiFi.softAP(ssid, password, 8);
    WiFi.setTxPower(WIFI_POTENCIA_BAJA);
    potenciaAlta = false;
    ultimaActividadHTTP = millis();  // Inicializar para evitar expulsión inmediata
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
            Serial.println(F("📱 Cliente conectado"));
            ajustarPotenciaWiFi(true);
        } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
            Serial.println(F("📱 Cliente desconectado"));
            if (WiFi.softAPgetStationNum() == 0) {
                ajustarPotenciaWiFi(false);
            }
        }
    });
    Serial.println("📡 WiFi 5mW (eficiente)");
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1),
                      IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));

    Serial.print(F("📶 SSID: ")); Serial.println(ssid);
    Serial.print(F("🌐 IP: ")); Serial.println(WiFi.softAPIP());

    // Iniciar DNS Server para Captive Portal
    // Redirige TODAS las peticiones DNS a la IP del ESP32
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    Serial.println(F("🌐 Captive Portal activo"));

    if (MDNS.begin("parking")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println(F("🚗 http://parking.local"));
    }

    // Configurar ArduinoOTA (pero no iniciarlo aún)
    ArduinoOTA.setHostname("parking");
    ArduinoOTA.setPassword(password);

    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.println("🔄 Iniciando actualización OTA: " + type);
        // Deshabilitar watchdog durante actualización
        esp_task_wdt_delete(NULL);
    });

    ArduinoOTA.onEnd([]() {
        Serial.println(F("\n✅ OTA completada"));
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("📊 Progreso: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("❌ Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println(F("Auth Failed"));
        else if (error == OTA_BEGIN_ERROR) Serial.println(F("Begin Failed"));
        else if (error == OTA_CONNECT_ERROR) Serial.println(F("Connect Failed"));
        else if (error == OTA_RECEIVE_ERROR) Serial.println(F("Receive Failed"));
        else if (error == OTA_END_ERROR) Serial.println(F("End Failed"));
    });

    configurarServidor();
    server.begin();
    Serial.println(F("📱 Servidor activo"));

    Serial.print(F("💾 RAM: "));
    Serial.print(ESP.getFreeHeap());
    Serial.println(F(" bytes"));

    if (wdState.safeMode) {
        Serial.println(F("\n🛡️ MODO SEGURO ACTIVO"));
        Serial.println(F("Funciones no críticas deshabilitadas"));
    }

    Serial.println(F("\n📋 Endpoints:"));
    Serial.println(F("   /status"));
    Serial.println(F("   /watchdog"));
    Serial.println(F("   /ntp_config"));
    Serial.println(F("   /ntp_status"));
    Serial.println(F("   /ota_enable"));
    Serial.println(F("   /ota_status"));
    Serial.println(F("\n━━━━━━━━━━━━━━━━━━━━━━━━\n"));
}

void loop() {
    static unsigned long ultimoWatchdog = 0;
    static bool otaIniciado = false;

    // Gestión de OTA con timeout
    if (otaHabilitado) {
        // Verificar timeout (4 minutos = 240 segundos)
        if (millisSafe(otaHabilitadoDesde) >= OTA_TIMEOUT) {
            otaHabilitado = false;
            otaIniciado = false;
            ArduinoOTA.end();
            Serial.println(F("⏱️ OTA deshabilitado por timeout"));
        } else {
            // Iniciar OTA si aún no se ha iniciado
            if (!otaIniciado) {
                ArduinoOTA.begin();
                otaIniciado = true;
                Serial.println(F("🟢 OTA iniciado"));
            }
            // Procesar OTA
            ArduinoOTA.handle();
        }
    } else if (otaIniciado) {
        // Detener OTA si fue deshabilitado manualmente
        ArduinoOTA.end();
        otaIniciado = false;
        Serial.println(F("🔴 OTA detenido"));
    }

    if (millisSafe(ultimoWatchdog) >= 2000) {
        ejecutarWatchdog();
        ultimoWatchdog = millis();
    }

    // Procesar peticiones DNS (Captive Portal)
    dnsServer.processNextRequest();

    server.handleClient();
    actualizarIndicadorHumano();
    controlarLucesAutomatico();

    if (!wdState.safeMode) {
        verificarSincronizacionAutomatica();
    }

    delay(50);
}
