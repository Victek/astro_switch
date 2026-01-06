// ========================================
// INTERFAZ WEB Y ENDPOINTS HTTP
// Interruptor astronómico v1.3 (Laia)
// ========================================

#ifndef WEB_INTERFACE_H
#define WEB_INTERFACE_H

#include <WebServer.h>
#include <WiFi.h>
#include "config.h"
#include "esp_task_wdt.h"

// Declaraciones externas de variables globales
extern WebServer server;
extern EstadoSistema estado;
extern NTPState ntpState;
extern WatchdogState wdState;
extern bool rtcConectado;
extern RTC_DS3231 rtc;
extern float latitud;
extern float longitud;
extern int utcOffset;
extern bool potenciaAlta;
extern int horaEncendido;
extern int minutoEncendido;
extern int horaApagado;
extern int minutoApagado;
extern bool otaHabilitado;
extern unsigned long otaHabilitadoDesde;
extern unsigned long ultimaActividadHTTP;

// Declaraciones de funciones externas necesarias
extern bool obtenerTiempoSistema(DateTime* tiempo);
extern bool esHorarioVerano(DateTime ahora);
extern int calcularAtardecer(DateTime ahora);
extern int calcularAmanecer(DateTime ahora);
extern void obtenerEstadoBateriaCompleto(char* buffer, size_t bufferSize);
extern void guardarConfiguracion();
extern void sincronizarConAstronomico();
extern void invalidarCacheRTC();
extern void invalidarCacheHorarios();
extern float leerTemperaturaDS3231();
extern void obtenerEstadisticasWatchdog(char* buffer, size_t bufferSize);
extern void obtenerEstadoNTP(char* buffer, size_t bufferSize);
extern void ajustarPotenciaWiFi(bool altaPotencia);
extern void guardarCredencialesWiFi(const char* ssid, const char* password);
extern void borrarCredencialesWiFi();
extern bool ejecutarSincronizacionCompleta();
extern bool conectarWiFiCliente();
extern void desconectarWiFiCliente();
extern bool pingGoogle();
extern unsigned long millisSafe(unsigned long referencia);
extern bool esFechaValida(int year, int month, int day);
extern bool esHoraValida(int hour, int minute, int second);
extern void setReleEstado(bool encender);
extern bool sonCoordenadasValidas(float lat, float lon);
extern void formatearUptime(unsigned long milliseconds, char* buffer, size_t bufferSize);

// ========================================
// PÁGINA WEB PRINCIPAL
// ========================================
void enviarPaginaWeb() {
    // Registrar actividad HTTP
    ultimaActividadHTTP = millis();

    // Reducir uso de stack usando static para buffers temporales
    static char buffer[64];  // Buffer reutilizable

    DateTime ahora;
    bool tiempoValido = obtenerTiempoSistema(&ahora);
    const char* fuenteTiempo = "Desconocida";

    if (tiempoValido) {
        if (rtcConectado) fuenteTiempo = "RTC Físico";
        else if (ntpState.lastSyncSuccess) fuenteTiempo = "NTP";
        else fuenteTiempo = "Cache RTC";
    }

    bool horarioVerano = tiempoValido ? esHorarioVerano(ahora) : false;
    char estadoBateria[64];
    obtenerEstadoBateriaCompleto(estadoBateria, sizeof(estadoBateria));

    int proxAtardecer = 0, proxAmanecer = 0;
    if (tiempoValido) {
        proxAtardecer = calcularAtardecer(ahora);
        proxAmanecer = calcularAmanecer(ahora);
    }

    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html; charset=utf-8", "");

    server.sendContent_P(PSTR(
        "<!DOCTYPE html><html lang=\"es\"><head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "<title>Luz Entrada Parking</title>"
        "<style>"
        "*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
        "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
        "margin:0;padding:20px;background:#0a0a0a;color:#e0e0e0;min-height:100vh;overflow-x:hidden}"
        ".container{max-width:1200px;margin:0 auto;background:#1a1a1a;border-radius:15px;"
        "padding:25px;border:1px solid #333;box-shadow:0 10px 30px rgba(0,0,0,0.5)}"
        ".grid-container{display:grid;grid-template-columns:1fr 1fr;gap:20px;align-items:start}"
        ".btn{padding:16px 20px;margin:10px 0;font-size:16px;border:none;border-radius:10px;"
        "cursor:pointer;width:100%;transition:all 0.3s ease;font-weight:600;display:flex;"
        "align-items:center;justify-content:center;gap:10px}"
        ".btn:hover{transform:translateY(-2px);box-shadow:0 5px 15px rgba(0,0,0,0.3);filter:brightness(1.1)}"
        ".btn:active{transform:scale(0.98)}"
        ".on{background:linear-gradient(135deg,#27ae60,#2ecc71);color:white}"
        ".off{background:linear-gradient(135deg,#c0392b,#e74c3c);color:white}"
        ".auto{background:linear-gradient(135deg,#2980b9,#3498db);color:white}"
        ".status{background:linear-gradient(135deg,#d35400,#e67e22);color:white}"
        ".config{background:linear-gradient(135deg,#8e44ad,#9b59b6);color:white}"
        ".time{background:linear-gradient(135deg,#34495e,#5d6d7e);color:white}"
        ".info{background:#252525;padding:20px;margin:15px 0;border-radius:12px;"
        "border-left:5px solid #3498db;border:1px solid #444}"
        ".info.error{border-left-color:#e74c3c}"
        ".info.success{border-left-color:#27ae60}"
        ".form-group{margin:15px 0}"
        ".input-row{display:flex;justify-content:center;align-items:center;gap:5px;margin:8px 0}"
        "input{padding:10px 8px;border:2px solid #444;border-radius:6px;width:100%;"
        "text-align:center;font-size:14px;background:#333;color:#e0e0e0;font-weight:600}"
        "input:focus{border-color:#3498db;outline:none;background:#3a3a3a}"
        "select{width:100%;padding:10px;background:#333;color:white;border:2px solid #444;border-radius:6px}"
        "h1{color:#fff;margin:0 0 25px 0;font-size:32px;text-align:center;font-weight:700;grid-column:1/-1}"
        "h3{color:#bdc3c7;margin:0 0 15px 0;font-size:18px;font-weight:600}"
        ".status-badge{display:inline-block;padding:8px 15px;border-radius:20px;font-size:14px;"
        "margin:5px;font-weight:600;box-shadow:0 2px 4px rgba(0,0,0,0.3)}"
        ".badge-on{background:linear-gradient(135deg,#27ae60,#2ecc71);color:white}"
        ".badge-off{background:linear-gradient(135deg,#c0392b,#e74c3c);color:white}"
        ".badge-auto{background:linear-gradient(135deg,#2980b9,#3498db);color:white}"
        ".badge-manual{background:linear-gradient(135deg,#d35400,#e67e22);color:white}"
        ".badge-ok{background:linear-gradient(135deg,#27ae60,#2ecc71);color:white}"
        ".badge-error{background:linear-gradient(135deg,#c0392b,#e74c3c);color:white}"
        "label{color:#bdc3c7;font-weight:600;font-size:14px;display:block;margin-bottom:8px}"
        ".clock-display{font-size:42px;font-weight:700;color:#3498db;margin:15px 0;"
        "text-align:center;text-shadow:0 0 10px rgba(52,152,219,0.3)}"
        ".date-display{font-size:20px;color:#95a5a6;margin-bottom:20px;text-align:center}"
        ".footer{margin-top:25px;color:#7f8c8d;font-size:12px;border-top:1px solid #333;"
        "padding-top:15px;text-align:center;grid-column:1/-1}"
        ".section-title{display:flex;align-items:center;gap:10px;margin-bottom:15px}"
        ".section-title span{font-size:20px}"
        ".status-row{display:flex;justify-content:space-between;align-items:center;margin:12px 0;"
        "padding:12px;background:#2d2d2d;border-radius:8px}"
        ".status-label{font-weight:600;color:#bdc3c7}"
        ".button-group{margin:15px 0}"
        "@media (max-width:768px){"
        "body{padding:10px}.container{padding:15px 10px;border-radius:10px}"
        ".grid-container{grid-template-columns:1fr;gap:15px}"
        ".btn{padding:14px 12px;font-size:15px}"
        "input{padding:12px 10px;font-size:15px}"
        ".clock-display{font-size:32px}"
        "h1{font-size:24px;margin-bottom:20px}"
        ".info{padding:15px 12px;margin:10px 0}}"
        "</style></head><body><div class=\"container\">"
        "<h1>⚡ Luz Parking Craywinckel,2-4. " FIRMWARE_VERSION " ⚡</h1>"
        "<div class=\"grid-container\"><div>"
    ));

    esp_task_wdt_reset();

    // Estado RTC
    server.sendContent_P(PSTR("<div class=\"info "));
    server.sendContent(rtcConectado ? "success" : "error");
    server.sendContent_P(PSTR("\">"
        "<div class=\"section-title\"><span>⏰</span><h3>Reloj RTC</h3></div>"
        "<div class=\"clock-display\">"));

    // Generar hora dinámicamente
    if (tiempoValido) {
        snprintf(buffer, sizeof(buffer), "%02d:%02d", ahora.hour(), ahora.minute());
    } else {
        strcpy(buffer, "--:--");
    }
    server.sendContent(buffer);

    server.sendContent_P(PSTR("</div><div class=\"date-display\">"));

    // Generar fecha dinámicamente
    if (tiempoValido) {
        snprintf(buffer, sizeof(buffer), "%02d/%02d/%d", ahora.day(), ahora.month(), ahora.year());
    } else {
        strcpy(buffer, "--/--/----");
    }
    server.sendContent(buffer);
    server.sendContent_P(PSTR("</div>"
        "<div class=\"status-row\"><span class=\"status-label\">Estado RTC:</span>"
        "<span class=\"status-badge "));
    server.sendContent(rtcConectado ? "badge-ok\">✅ Conectado" : "badge-error\">❌ Desconectado");
    server.sendContent_P(PSTR("</span></div>"
        "<div class=\"status-row\"><span class=\"status-label\">Batería RTC:</span>"
        "<span class=\"status-badge "));

    if (strstr(estadoBateria, "✅") != NULL) {
        server.sendContent_P(PSTR("badge-ok\">"));
    } else if (strstr(estadoBateria, "⚠️") != NULL) {
        server.sendContent_P(PSTR("badge-manual\">"));
    } else {
        server.sendContent_P(PSTR("badge-error\">"));
    }
    server.sendContent(estadoBateria);

    server.sendContent_P(PSTR("</span></div>"
        "<div class=\"status-row\"><span class=\"status-label\">Fuente hora:</span>"
        "<span class=\"status-badge badge-manual\">"));
    server.sendContent(fuenteTiempo);
    server.sendContent_P(PSTR("</span></div>"
        "<div class=\"status-row\"><span class=\"status-label\">Ajuste Horario:</span>"
        "<span class=\"status-badge "));
    server.sendContent(horarioVerano ? "badge-auto\">VERANO (+1h)" : "badge-manual\">INVIERNO");
    server.sendContent_P(PSTR("</span></div></div>"));

    if (wdState.safeMode) {
        server.sendContent_P(PSTR(
            "<div class=\"info\" style=\"border-left-color:#f39c12\">"
            "<strong>⚠️ MODO SEGURO ACTIVO</strong><br>"
            "El sistema detectó resets frecuentes y deshabilitó funciones no críticas."
            "</div>"
        ));
    }

    esp_task_wdt_reset();

    // Control Luces
    server.sendContent_P(PSTR(
        "<div class=\"info\">"
        "<div class=\"section-title\"><span>🎛️</span><h3>Controladora astronómica</h3></div>"
        "<div class=\"status-row\"><span class=\"status-label\">Luces:</span>"
        "<span class=\"status-badge "));
    server.sendContent(estado.lucesOn ? "badge-on\">🔆 ACTIVADAS" : "badge-off\">🌙 DESACTIVADAS");
    server.sendContent_P(PSTR("</span></div>"
        "<div class=\"status-row\"><span class=\"status-label\">Modo:</span>"
        "<span class=\"status-badge "));
    server.sendContent(estado.modoAuto ? "badge-auto\">🤖 AUTOMÁTICO" : "badge-manual\">👤 MANUAL");
    server.sendContent_P(PSTR("</span></div>"
        "<div class=\"status-row\"><span class=\"status-label\">Próxima Activación:</span>"
        "<span style=\"color:#27ae60;font-weight:bold\">"));

    // Generar próximo ON dinámicamente
    if (tiempoValido) {
        snprintf(buffer, sizeof(buffer), "%02d:%02d", proxAtardecer/60, proxAtardecer%60);
    } else {
        strcpy(buffer, "--:--");
    }
    server.sendContent(buffer);

    server.sendContent_P(PSTR("</span></div>"
        "<div class=\"status-row\"><span class=\"status-label\">Próxima Desactivación:</span>"
        "<span style=\"color:#e74c3c;font-weight:bold\">"));

    // Generar próximo OFF dinámicamente
    if (tiempoValido) {
        snprintf(buffer, sizeof(buffer), "%02d:%02d", proxAmanecer/60, proxAmanecer%60);
    } else {
        strcpy(buffer, "--:--");
    }
    server.sendContent(buffer);
    server.sendContent_P(PSTR("</span></div>"
        "<div class=\"button-group\">"
        "<button class=\"btn on\" onclick=\"control('on')\">🔆 ACTIVAR</button>"
        "<button class=\"btn off\" onclick=\"control('off')\">🌙 DESACTIVAR</button>"
        "<button class=\"btn auto\" onclick=\"control('auto')\">🤖 AUTOMÁTICO</button>"
        "</div></div></div>"));

    // Columna Derecha - Configuración
    server.sendContent_P(PSTR(
        "<div><div class=\"info\">"
        "<div class=\"section-title\"><span>📍</span><h3>Configurar Ubicación</h3></div>"
        "<form action=\"/configurar_ubicacion\" method=\"post\">"
        "<div class=\"form-group\"><label>Latitud</label>"
        "<input type=\"number\" step=\"0.0001\" name=\"latitud\" value=\""));

    snprintf(buffer, sizeof(buffer), "%.4f", latitud);
    server.sendContent(buffer);

    server.sendContent_P(PSTR("\" min=\"-90\" max=\"90\" required></div>"
        "<div class=\"form-group\"><label>Longitud</label>"
        "<input type=\"number\" step=\"0.0001\" name=\"longitud\" value=\""));

    snprintf(buffer, sizeof(buffer), "%.4f", longitud);
    server.sendContent(buffer);
    server.sendContent_P(PSTR("\" min=\"-180\" max=\"180\" required></div>"
        "<div class=\"form-group\"><label>Zona Horaria UTC</label>"
        "<select name=\"utc_offset\">"));

    for (int i = -12; i <= 12; i++) {
        char opt[60];
        if (i == 1) {
            snprintf(opt, sizeof(opt), "<option value=\"%d\"%s>UTC+1</option>",
                     i, (utcOffset == i) ? " selected" : "");
        } else if (i == 0) {
            snprintf(opt, sizeof(opt), "<option value=\"0\"%s>UTC±0</option>",
                     (utcOffset == i) ? " selected" : "");
        } else {
            snprintf(opt, sizeof(opt), "<option value=\"%d\"%s>UTC%+d</option>",
                     i, (utcOffset == i) ? " selected" : "", i);
        }
        server.sendContent(opt);
    }

    esp_task_wdt_reset();

    server.sendContent_P(PSTR(
        "</select></div>"
        "<button class=\"btn config\" type=\"submit\">💾 GUARDAR</button>"
        "</form></div>"));

    // Ajustar Hora
    server.sendContent_P(PSTR(
        "<div class=\"info\">"
        "<div class=\"section-title\"><span>🕐</span><h3>Ajustar Reloj</h3></div>"
        "<form action=\"/ajustarhora\" method=\"post\">"
        "<div class=\"form-group\"><label>Fecha</label><div class=\"input-row\">"
        "<input type=\"number\" name=\"dia\" id=\"inp-dia\" value=\""));

    snprintf(buffer, sizeof(buffer), "%d", tiempoValido ? ahora.day() : 0);
    server.sendContent(buffer);

    server.sendContent_P(PSTR("\" min=\"1\" max=\"31\" placeholder=\"Día\" required>"
        "<input type=\"number\" name=\"mes\" id=\"inp-mes\" value=\""));

    snprintf(buffer, sizeof(buffer), "%d", tiempoValido ? ahora.month() : 0);
    server.sendContent(buffer);

    server.sendContent_P(PSTR("\" min=\"1\" max=\"12\" placeholder=\"Mes\" required>"
        "<input type=\"number\" name=\"anio\" id=\"inp-anio\" value=\""));

    snprintf(buffer, sizeof(buffer), "%d", tiempoValido ? ahora.year() : 2025);
    server.sendContent(buffer);

    server.sendContent_P(PSTR("\" min=\"2020\" max=\"2050\" placeholder=\"Año\" required>"
        "</div></div>"
        "<div class=\"form-group\"><label>Hora</label><div class=\"input-row\">"
        "<input type=\"number\" name=\"hora\" id=\"inp-hora\" value=\""));

    snprintf(buffer, sizeof(buffer), "%d", tiempoValido ? ahora.hour() : 0);
    server.sendContent(buffer);

    server.sendContent_P(PSTR("\" min=\"0\" max=\"23\" placeholder=\"Hora\" required>"
        "<input type=\"number\" name=\"minuto\" id=\"inp-min\" value=\""));

    snprintf(buffer, sizeof(buffer), "%d", tiempoValido ? ahora.minute() : 0);
    server.sendContent(buffer);

    server.sendContent_P(PSTR("\" min=\"0\" max=\"59\" placeholder=\"Minuto\" required>"
        "<input type=\"number\" name=\"segundo\" id=\"inp-seg\" value=\""));

    snprintf(buffer, sizeof(buffer), "%d", tiempoValido ? ahora.second() : 0);
    server.sendContent(buffer);
    server.sendContent_P(PSTR("\" min=\"0\" max=\"59\" placeholder=\"Segundo\" required>"
        "</div></div>"
        "<button class=\"btn time\" type=\"submit\">🕐 AJUSTAR</button>"
        "</form></div>"
        "<button class=\"btn status\" onclick=\"getStatus()\">📊 RESUMEN</button>"
        "</div></div>"));

    // Footer y JavaScript
    server.sendContent_P(PSTR(
    "<div class=\"footer\">"
    "\"El software es como una cebolla - tiene capas, y te hace llorar\"<br>"
    "<em style=\"color:#95a5a6;\">- Uno que piensa código sensato -</em><br><br>"
    "⚡ " FIRMWARE_VERSION " - @victek-is-a-geek.com ⚡"
    "</div></div>"
    "<script>"
    "function control(a){fetch('/'+a).then(r=>r.text()).then(d=>{alert(d);location.reload()})"
    ".catch(e=>alert('Error: '+e))}"
    "function getStatus(){fetch('/status').then(r=>r.text()).then(d=>alert(d))"
    ".catch(e=>alert('Error: '+e))}"
    "setInterval(()=>{"
    "const now=new Date();"
    "document.querySelector('.clock-display').textContent="
    "now.getHours().toString().padStart(2,'0')+':'+now.getMinutes().toString().padStart(2,'0');"
    "document.querySelector('.date-display').textContent="
    "now.getDate().toString().padStart(2,'0')+'/'+("
    "now.getMonth()+1).toString().padStart(2,'0')+'/'+now.getFullYear();"
    "const ih=document.getElementById('inp-hora');"
    "const im=document.getElementById('inp-min');"
    "const is=document.getElementById('inp-seg');"
    "const id=document.getElementById('inp-dia');"
    "const imo=document.getElementById('inp-mes');"
    "const ia=document.getElementById('inp-anio');"
    "if(ih&&!ih.matches(':focus'))ih.value=now.getHours();"
    "if(im&&!im.matches(':focus'))im.value=now.getMinutes();"
    "if(is&&!is.matches(':focus'))is.value=now.getSeconds();"
    "if(id&&!id.matches(':focus'))id.value=now.getDate();"
    "if(imo&&!imo.matches(':focus'))imo.value=now.getMonth()+1;"
    "if(ia&&!ia.matches(':focus'))ia.value=now.getFullYear();"
    "},1000);"
    "</script></body></html>"
));

esp_task_wdt_reset();
}

// ========================================
// ENDPOINTS NTP
// ========================================
void configurarEndpointsNTP() {
    server.on("/ntp_config", HTTP_GET, []() {
        ultimaActividadHTTP = millis();
        DEBUG_PRINTLN(F("📡 Escaneando WiFi..."));

        esp_task_wdt_reset();
        WiFi.scanDelete();
        int n = WiFi.scanNetworks();
        esp_task_wdt_reset();

        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "text/html; charset=utf-8", "");

        server.sendContent_P(PSTR(
            "<!DOCTYPE html><html lang=\"es\"><head>"
            "<meta charset=\"UTF-8\">"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
            "<title>Config NTP</title>"
            "<style>"
            "*{box-sizing:border-box}"
            "body{font-family:Arial;margin:0;padding:20px;background:#0a0a0a;color:#e0e0e0}"
            ".container{max-width:600px;margin:0 auto;background:#1a1a1a;border-radius:15px;"
            "padding:25px;border:1px solid #333}"
            "h1{color:#3498db;margin:0 0 25px 0;font-size:28px;text-align:center}"
            ".form-group{margin:20px 0}"
            "label{color:#bdc3c7;font-weight:600;font-size:14px;display:block;margin-bottom:8px}"
            "select,input{padding:12px;border:2px solid #444;border-radius:6px;width:100%;"
            "background:#333;color:#e0e0e0;font-size:14px}"
            "select:focus,input:focus{border-color:#3498db;outline:none;background:#3a3a3a}"
            ".btn{padding:14px;margin:10px 0;font-size:16px;border:none;border-radius:10px;"
            "cursor:pointer;width:100%;font-weight:600;color:white}"
            ".btn-save{background:linear-gradient(135deg,#27ae60,#2ecc71)}"
            ".btn-test{background:linear-gradient(135deg,#2980b9,#3498db)}"
            ".btn-delete{background:linear-gradient(135deg,#c0392b,#e74c3c)}"
            ".btn-refresh{background:linear-gradient(135deg,#f39c12,#e67e22)}"
            ".btn:hover{filter:brightness(1.1)}"
            ".info{background:#252525;padding:15px;margin:15px 0;border-radius:8px;"
            "border-left:5px solid #3498db}"
            "</style></head><body><div class=\"container\">"
            "<h1>🌐 Configuración NTP</h1>"
            "<div class=\"info\">Sincronización automática cada 12 horas.</div>"
        ));

        if (n == 0) {
            server.sendContent_P(PSTR(
                "<div class=\"info\" style=\"border-left-color:#e74c3c\">"
                "❌ Sin redes WiFi. <a href=\"/ntp_config\" style=\"color:#3498db\">Recargar</a>"
                "</div>"
            ));
        } else {
            server.sendContent_P(PSTR(
                "<form action=\"/ntp_config\" method=\"post\">"
                "<div class=\"form-group\"><label>Red WiFi ("
            ));

            char numBuf[10];
            snprintf(numBuf, sizeof(numBuf), "%d", n);
            server.sendContent(numBuf);

            server.sendContent_P(PSTR(
                " encontradas)</label>"
                "<select name=\"ssid\" id=\"ssid-select\" required>"
                "<option value=\"\">-- Selecciona --</option>"
            ));

            // Scan de redes inalámbricas cercanas  
            for (int i = 0; i < n; i++) {
                String ssid_temp = WiFi.SSID(i);
                const char* ssid_raw = ssid_temp.c_str();

                // Validar longitud del SSID para prevenir buffer overflow
                if (strlen(ssid_raw) > 32) {
                    continue;  // Saltar SSIDs anormalmente largos
                }

                int rssi = WiFi.RSSI(i);
                bool isOpen = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);

                const char* signalIcon = "📶";
                if (rssi > -50) {
                    signalIcon = "📶📶📶";
                } else if (rssi > -70) {
                    signalIcon = "📶📶";
                }

                bool selected = (ntpState.ssid[0] != '\0' && strcmp(ssid_raw, ntpState.ssid) == 0);

                // Buffer suficientemente grande para prevenir overflow
                char option[256];
                snprintf(option, sizeof(option),
                    "<option value=\"%.32s\"%s>%.32s%s (%ddBm %s)</option>",
                    ssid_raw,
                    selected ? " selected" : "",
                    ssid_raw,
                    isOpen ? " 🔓" : " 🔒",
                    rssi,
                    signalIcon);

                server.sendContent(option);
            }

            server.sendContent_P(PSTR(
                "</select></div>"
                "<div class=\"form-group\"><label>Contraseña WiFi</label>"
                "<input type=\"password\" name=\"password\" maxlength=\"64\" "
                "placeholder=\"Contraseña\" required></div>"
                "<button class=\"btn btn-save\" type=\"submit\">💾 GUARDAR</button>"
                "</form>"
            ));
        }

        server.sendContent_P(PSTR(
            "<button class=\"btn btn-refresh\" onclick=\"location.reload()\">🔄 ESCANEAR</button>"
        ));

        if (ntpState.enabled) {
            server.sendContent_P(PSTR(
                "<button class=\"btn btn-test\" onclick=\"location.href='/ntp_test'\">🔍 TEST</button>"
                "<button class=\"btn btn-test\" onclick=\"location.href='/ntp_sync'\">🔄 SINCRONIZAR</button>"
                "<button class=\"btn btn-delete\" onclick=\"if(confirm('¿Borrar?'))location.href='/ntp_delete'\">🗑️ BORRAR</button>"
            ));
        }

        if (ntpState.enabled) {
            server.sendContent_P(PSTR(
                "<div class=\"info\" style=\"border-left-color:#27ae60\">"
                "✅ <strong>Red:</strong> "
            ));
            server.sendContent(ntpState.ssid);
            server.sendContent_P(PSTR("</div>"));
        }

        server.sendContent_P(PSTR(
            "</div>"
            "<script>"
            "document.getElementById('ssid-select').addEventListener('change',function(){"
            "document.querySelector('input[name=\"password\"]').value='';"
            "});"
            "</script>"
            "</body></html>"
        ));

        WiFi.scanDelete();
    });

    server.on("/ntp_config", HTTP_POST, []() {
        ultimaActividadHTTP = millis();
        if (server.hasArg("ssid") && server.hasArg("password")) {
            String ssid = server.arg("ssid");
            String password = server.arg("password");

            guardarCredencialesWiFi(ssid.c_str(), password.c_str());

            server.send(200, "text/html; charset=utf-8",
                "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
                "<meta http-equiv=\"refresh\" content=\"2; url=/ntp_config\">"
                "<style>body{font-family:Arial;text-align:center;padding:50px;"
                "background:#0a0a0a;color:white}"
                ".success{background:#27ae60;padding:20px;border-radius:10px}</style></head>"
                "<body><div class=\"success\"><h2>✅ Guardado</h2>"
                "<p>Redirigiendo...</p></div></body></html>");
        } else {
            server.send(400, "text/plain", "Faltan parámetros");
        }
        WiFi.scanDelete();
    });

    server.on("/ntp_sync", []() {
        ultimaActividadHTTP = millis();
        if (!ntpState.enabled) {
            server.send(200, "text/plain; charset=utf-8",
                "❌ NTP no configurado\n\nConfigura en /ntp_config");
            return;
        }

        server.send(200, "text/plain; charset=utf-8",
            "🔄 Iniciando sinc NTP...\n\nTarda hasta 45s.\nResultado en /ntp_status");

        delay(100);
        ejecutarSincronizacionCompleta();
    });

    server.on("/ntp_test", []() {
        ultimaActividadHTTP = millis();
        if (!ntpState.enabled) {
            server.send(200, "text/plain; charset=utf-8",
                "❌ NTP no configurado");
            return;
        }

        char resultado[512];
        int pos = 0;

        pos += snprintf(resultado + pos, sizeof(resultado) - pos,
            "🔍 TEST CONECTIVIDAD\n\nRed: %s\n\n📡 Conectando...\n",
            ntpState.ssid);

        if (conectarWiFiCliente()) {
            pos += snprintf(resultado + pos, sizeof(resultado) - pos,
                "✅ WiFi OK\n📶 IP: %s\n📊 RSSI: %d dBm\n\n🌍 Verificando internet...\n",
                WiFi.localIP().toString().c_str(),
                WiFi.RSSI());

            if (pingGoogle()) {
                pos += snprintf(resultado + pos, sizeof(resultado) - pos,
                    "✅ Internet OK\n\n🎯 Todo correcto");
            } else {
                pos += snprintf(resultado + pos, sizeof(resultado) - pos,
                    "❌ Sin internet\n\n⚠️ WiFi OK pero sin conexión");
            }

            desconectarWiFiCliente();
        } else {
            pos += snprintf(resultado + pos, sizeof(resultado) - pos,
                "❌ Error WiFi\n\n⚠️ Verifica credenciales");
        }

        server.send(200, "text/plain; charset=utf-8", resultado);
    });

    server.on("/ntp_status", []() {
        ultimaActividadHTTP = millis();
        char buffer[512];
        obtenerEstadoNTP(buffer, sizeof(buffer));
        server.send(200, "text/plain; charset=utf-8", buffer);
    });

    server.on("/ntp_delete", []() {
        ultimaActividadHTTP = millis();
        borrarCredencialesWiFi();
        server.send(200, "text/html; charset=utf-8",
            "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
            "<meta http-equiv=\"refresh\" content=\"2; url=/ntp_config\">"
            "<style>body{font-family:Arial;text-align:center;padding:50px;"
            "background:#0a0a0a;color:white}"
            ".success{background:#e74c3c;padding:20px;border-radius:10px}</style></head>"
            "<body><div class=\"success\"><h2>🗑️ Borrado</h2>"
            "<p>Redirigiendo...</p></div></body></html>");
    });
}

// ========================================
// CONFIGURAR SERVIDOR WEB
// ========================================
void configurarServidor() {
    // Página principal
    server.on("/", []() {
        enviarPaginaWeb();
    });

    // Captive Portal: redirigir peticiones comunes de detección
    server.on("/generate_204", []() {  // Android
        enviarPaginaWeb();
    });
    server.on("/fwlink", []() {  // Microsoft
        enviarPaginaWeb();
    });
    server.on("/hotspot-detect.html", []() {  // Apple
        enviarPaginaWeb();
    });
    server.on("/canonical.html", []() {  // Firefox
        enviarPaginaWeb();
    });
    server.on("/success.txt", []() {  // Firefox
        enviarPaginaWeb();
    });
    server.on("/ncsi.txt", []() {  // Windows
        enviarPaginaWeb();
    });

    server.on("/on", []() {
        ultimaActividadHTTP = millis();
        setReleEstado(true);
        estado.modoAuto = false;
        estado.comandoManual = true;
        estado.ultimoCambioManual = millis();

        guardarConfiguracion();
        server.send(200, "text/plain; charset=utf-8", "Luces ON");
    });

    server.on("/off", []() {
        ultimaActividadHTTP = millis();
        setReleEstado(false);
        estado.modoAuto = false;
        estado.comandoManual = true;
        estado.ultimoCambioManual = millis();

        guardarConfiguracion();
        server.send(200, "text/plain; charset=utf-8", "Luces OFF");
    });

    server.on("/auto", []() {
        ultimaActividadHTTP = millis();
        estado.modoAuto = true;
        estado.comandoManual = true;
        estado.ultimoCambioManual = millis();

        sincronizarConAstronomico();

        guardarConfiguracion();
        server.send(200, "text/plain; charset=utf-8", "Modo AUTOMÁTICO");
    });

    server.on("/configurar_ubicacion", HTTP_POST, []() {
        ultimaActividadHTTP = millis();

        // Validar coordenadas antes de guardar
        float nuevaLat = latitud;
        float nuevaLon = longitud;

        if (server.hasArg("latitud")) nuevaLat = server.arg("latitud").toFloat();
        if (server.hasArg("longitud")) nuevaLon = server.arg("longitud").toFloat();

        // Solo actualizar si las coordenadas son válidas
        if (sonCoordenadasValidas(nuevaLat, nuevaLon)) {
            latitud = nuevaLat;
            longitud = nuevaLon;
        }

        if (server.hasArg("utc_offset")) {
            int nuevoOffset = server.arg("utc_offset").toInt();
            if (nuevoOffset >= -12 && nuevoOffset <= 12) {
                utcOffset = nuevoOffset;
            }
        }

        guardarConfiguracion();

        // ✅ CORREGIR BUG: Invalidar cache de horarios y forzar recálculo
        invalidarCacheHorarios();

        // Sincronizar solo si está en modo automático
        if (estado.modoAuto) {
            sincronizarConAstronomico();
        }

        char latBuf[12], lonBuf[12], utcBuf[5];
        snprintf(latBuf, sizeof(latBuf), "%.4f", latitud);
        snprintf(lonBuf, sizeof(lonBuf), "%.4f", longitud);
        snprintf(utcBuf, sizeof(utcBuf), "%+d", utcOffset);

        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "text/html; charset=utf-8", "");

        server.sendContent_P(PSTR(
            "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
            "<meta http-equiv=\"refresh\" content=\"2; url=/\">"
            "<style>body{font-family:Arial;text-align:center;padding:50px;"
            "background:#0a0a0a;color:white}"
            ".success{background:#27ae60;padding:20px;border-radius:10px}</style></head><body>"
            "<div class=\"success\"><h2>✅ Ubicación Guardada</h2><p>Lat: "));

        server.sendContent(latBuf);
        server.sendContent_P(PSTR("</p><p>Lon: "));
        server.sendContent(lonBuf);
        server.sendContent_P(PSTR("</p><p>UTC: "));
        server.sendContent(utcBuf);
        server.sendContent_P(PSTR("</p></div></body></html>"));
    });

    server.on("/ajustarhora", HTTP_POST, []() {
        ultimaActividadHTTP = millis();
        bool exito = false;

        if (server.hasArg("anio") && server.hasArg("mes") && server.hasArg("dia") &&
            server.hasArg("hora") && server.hasArg("minuto") && server.hasArg("segundo")) {

            int anio = server.arg("anio").toInt();
            int mes = server.arg("mes").toInt();
            int dia = server.arg("dia").toInt();
            int hora = server.arg("hora").toInt();
            int minuto = server.arg("minuto").toInt();
            int segundo = server.arg("segundo").toInt();

            // Usar funciones helper para validación más robusta
            if (esFechaValida(anio, mes, dia) && esHoraValida(hora, minuto, segundo)) {
                rtc.adjust(DateTime(anio, mes, dia, hora, minuto, segundo));
                invalidarCacheRTC();
                invalidarCacheHorarios();
                exito = true;

                // Sincronizar solo si está en modo automático
                if (estado.modoAuto) {
                    sincronizarConAstronomico();
                }
            }
        }

        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "text/html; charset=utf-8", "");

        server.sendContent_P(PSTR(
            "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
            "<meta http-equiv=\"refresh\" content=\"2; url=/\">"
            "<style>body{font-family:Arial;text-align:center;padding:50px;"
            "background:#0a0a0a;color:white}"
            ".msg{padding:20px;border-radius:10px;color:white}"));

        server.sendContent_P(exito ? PSTR(".msg{background:#27ae60}") : PSTR(".msg{background:#e74c3c}"));
        server.sendContent_P(PSTR("</style></head><body><div class=\"msg\"><h2>"));
        server.sendContent_P(exito ? PSTR("✅ Hora OK") : PSTR("❌ Error"));
        server.sendContent_P(PSTR("</h2></div></body></html>"));
    });

    server.on("/status", []() {
        ultimaActividadHTTP = millis();

        int clientesConectados = WiFi.softAPgetStationNum();
        bool hayClientes = (clientesConectados > 0);

        if (hayClientes != potenciaAlta) {
            ajustarPotenciaWiFi(hayClientes);
        }

        DateTime ahora;
        bool tiempoValido = obtenerTiempoSistema(&ahora);
        char fuenteTiempo[20] = "Desconocida";

        if (tiempoValido) {
            if (rtcConectado) strcpy(fuenteTiempo, "RTC Físico");
            else if (ntpState.lastSyncSuccess) strcpy(fuenteTiempo, "NTP");
            else strcpy(fuenteTiempo, "Cache RTC");
        }

        bool horarioVerano = tiempoValido ? esHorarioVerano(ahora) : false;
        char estadoBateria[64];
        obtenerEstadoBateriaCompleto(estadoBateria, sizeof(estadoBateria));
        int proxAtardecer = tiempoValido ? calcularAtardecer(ahora) : 0;
        int proxAmanecer = tiempoValido ? calcularAmanecer(ahora) : 0;
        float temperatura = leerTemperaturaDS3231();

        char uptimeBuffer[32];
        formatearUptime(millis(), uptimeBuffer, sizeof(uptimeBuffer));

        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "text/plain; charset=utf-8", "");

        char buf[300];
        snprintf(buf, sizeof(buf),
            "⚡ %s ⚡\n\n"
            "🔋 Batería RTC: %s\n"
            "🕐 Fuente hora: %s\n"
            "🌞 Horario: %s\n\n"
            "💡 Luces: %s\n"
            "🔧 Modo: %s\n\n"
            "🌅 Próx. ON: %02d:%02d\n"
            "🌄 Próx. OFF: %02d:%02d\n\n",
            FIRMWARE_VERSION,
            estadoBateria,
            fuenteTiempo,
            horarioVerano ? "VERANO" : "INVIERNO",
            estado.lucesOn ? "ON" : "OFF",
            estado.modoAuto ? "AUTO" : "MANUAL",
            proxAtardecer/60, proxAtardecer%60,
            proxAmanecer/60, proxAmanecer%60
        );
        server.sendContent(buf);

        snprintf(buf, sizeof(buf),
            "--- ESTABILIDAD ---\n"
            "⏱️ Uptime: %s\n"
            "🌡️ Temp: %.1f°C\n"
            "💾 RAM: %d bytes\n"
            "📡 WiFi: %s\n"
            "🛡️ Modo Reset: %s\n\n",
            uptimeBuffer,
            temperatura,
            ESP.getFreeHeap(),
            potenciaAlta ? "ALTA" : "BAJA",
            wdState.safeMode ? "SEGURO" : "NORMAL"
        );
        server.sendContent(buf);

        server.sendContent_P(PSTR(
            "--- TEAM ---\n"
            "Algoritmo: Jean Meeus\n"
            "Diseño y Programación: Vicente Soriano\n"
            "victek.is-a-geek.com\n"));

        server.sendContent("");
    });

    server.on("/watchdog", []() {
        ultimaActividadHTTP = millis();
        char buffer[512];
        obtenerEstadisticasWatchdog(buffer, sizeof(buffer));
        server.send(200, "text/plain; charset=utf-8", buffer);
    });

    // Endpoint para habilitar OTA (sin password en URL)
    server.on("/ota_enable", []() {
        ultimaActividadHTTP = millis();
        // Habilitar OTA por 90 segundos
        otaHabilitado = true;
        otaHabilitadoDesde = millis();

        server.send(200, "text/plain; charset=utf-8",
            "✅ OTA HABILITADO\n\n"
            "🕐 Timeout: 4 minutos\n"
            "📡 IP: 192.168.4.1\n"
            "🌐 Hostname: parking.local\n"
            "🔧 Puerto: 3232\n\n"
            "Arduino IDE te pedirá el password al subir.\n"
            "El OTA se deshabilitará automáticamente tras 4 minutos.");
    });

    // Endpoint para verificar estado OTA
    server.on("/ota_status", []() {
        ultimaActividadHTTP = millis();
        char buffer[256];

        IPAddress ip = WiFi.softAPIP();

        int pos = snprintf(buffer, sizeof(buffer),
            "⚡ FIRMWARE INFO ⚡\n\n"
            "🔢 Versión: %s\n"
            "📡 IP AP: %d.%d.%d.%d\n"
            "🌐 Hostname: parking.local\n",
            FIRMWARE_VERSION,
            ip[0], ip[1], ip[2], ip[3]);

        if (otaHabilitado) {
            unsigned long tiempoRestante = OTA_TIMEOUT - millisSafe(otaHabilitadoDesde);
            if (tiempoRestante > OTA_TIMEOUT) tiempoRestante = 0;  // Overflow protection

            pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                "\n🟢 OTA: HABILITADO\n"
                "⏱️ Tiempo restante: %lu s\n",
                tiempoRestante / 1000);
        } else {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                "\n🔴 OTA: DESHABILITADO\n"
                "💡 Para habilitar: /ota_enable\n");
        }

        server.send(200, "text/plain; charset=utf-8", buffer);
    });

    configurarEndpointsNTP();

    // Capturar TODAS las peticiones no reconocidas (Captive Portal)
    // Esto redirige cualquier URL a la página principal
    server.onNotFound([]() {
        ultimaActividadHTTP = millis();
        // Redirigir a la página principal
        enviarPaginaWeb();
    });
}

#endif // WEB_INTERFACE_H
