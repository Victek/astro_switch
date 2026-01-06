// ========================================
// CONFIGURACIÓN Y ESTRUCTURAS DE DATOS
// Interruptor astronómico v1.3 (Laia)
// ========================================

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ========================================
// CONFIGURACIÓN DE PINES
// ========================================
#define RELE_PIN 5
#define I2C_SDA 21
#define I2C_SCL 22
#define LED_WIFI 16

// ============== FIRMWARE ===============
#define FIRMWARE_VERSION "V-1.3 (Laia)"

// ========================================
// CONFIGURACIÓN WIFI ACCESS POINT
// ========================================
const char* ssid = "Parking";
const char* password = "123456789";

// ========================================
// CONFIGURACIÓN EEPROM
// ========================================
#define EEPROM_SIZE 256
#define ADDR_HORA_ON 0
#define ADDR_MINUTO_ON 1
#define ADDR_HORA_OFF 2
#define ADDR_MINUTO_OFF 3
#define ADDR_MODO_AUTO 4
#define ADDR_LATITUD 10
#define ADDR_LONGITUD 20
#define ADDR_UTC_OFFSET 30
#define ADDR_RESET_COUNT 100
#define ADDR_LAST_RESET_REASON 101
#define ADDR_HEAP_MIN_RECORD 102
#define ADDR_WIFI_SSID 110
#define ADDR_WIFI_PASS 142
#define ADDR_LAST_NTP_SYNC 206
#define ADDR_NTP_ENABLED 210
#define ADDR_BOOT_TIMESTAMP 214
#define ADDR_SAFE_MODE 218

// ========================================
// CONFIGURACIÓN WIFI DINÁMICA
// ========================================
#define WIFI_POTENCIA_ALTA  WIFI_POWER_19_5dBm
#define WIFI_POTENCIA_BAJA  WIFI_POWER_7dBm
#define TIMEOUT_INACTIVIDAD_CLIENTE 300000UL  // 5 minutos sin peticiones HTTP

// ========================================
// CONFIGURACIÓN OTA
// ========================================
#define OTA_TIMEOUT 240000  // 4 minutos

// ========================================
// CONFIGURACIÓN NTP
// ========================================
#define NTP_SERVER_PRIMARY "pool.ntp.org"
#define NTP_SERVER_BACKUP "time.google.com"
#define NTP_TIMEOUT 10000
#define WIFI_CONNECT_TIMEOUT 30000
#define NTP_SYNC_INTERVAL 43200000UL  // 12 horas
#define PING_HOST "8.8.8.8"

// ========================================
// PROTECCIÓN CONTRA RESET LOOPS
// ========================================
#define BOOT_INTERVAL_SAFE 300000UL  // 5 minutos
#define MAX_FAST_RESETS 3

// ========================================
// CONFIGURACIÓN LED PWM
// ========================================
#define RESOLUCION_PWM 8
#define FRECUENCIA_PWM 5000

// ========================================
// MODO DESARROLLO/PRODUCCIÓN
// ========================================
#define DEVELOPMENT false

#if DEVELOPMENT
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

// ========================================
// CONFIGURACIÓN WATCHDOG
// ========================================
#define WDT_TIMEOUT 8
#define CHECK_HEAP_INTERVAL 60000UL
#define CHECK_RTC_INTERVAL 60000UL
#define CHECK_RELE_INTERVAL 300000UL
#define MIN_HEAP_SAFE 30000

// ========================================
// CONFIGURACIÓN CACHE RTC
// ========================================
#define INTERVALO_LECTURA_RTC 950          // 950ms entre lecturas si se usa
#define CACHE_RTC_TIMEOUT 10000           // 10s máximo con cache viejo
#define INTERVALO_TEMP_DS3231 30000

// ========================================
// ESTRUCTURAS DE DATOS
// ========================================

// Estado NTP
struct NTPState {
    char ssid[33];
    char password[65];
    unsigned long lastSync;
    unsigned long lastAttempt;
    bool enabled;
    bool lastSyncSuccess;
    char lastError[64];
};

// Cache de tiempo RTC
struct CacheRTC {
    DateTime ultimaLectura;
    unsigned long timestampLectura;
    bool valido;
};

// Cache de temperatura DS3231
struct CacheTemperatura {
    float temperatura;
    unsigned long timestamp;
    bool valido;
};

// Estado del watchdog
struct WatchdogState {
    unsigned long lastHeapCheck;
    unsigned long lastRTCCheck;
    unsigned long lastReleCheck;
    uint32_t minHeapEver;
    uint16_t resetCount;
    bool rtcFailDetected;
    bool heapCritical;
    bool safeMode;
    uint8_t fastResetCount;
    volatile bool ejecutandoWD;
};

// Razones de reset
enum ResetReason {
    RESET_NORMAL = 0,
    RESET_HEAP_LOW = 1,
    RESET_RTC_FAIL = 2,
    RESET_RELE_STUCK = 3,
    RESET_WDT_TIMEOUT = 4,
    RESET_SAFE_MODE = 5
};

// Estado del sistema
struct EstadoSistema {
    bool modoAuto;
    bool lucesOn;
    bool comandoManual;
    unsigned long ultimoCambioManual;
};

#endif // CONFIG_H
