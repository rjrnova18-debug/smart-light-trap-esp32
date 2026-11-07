#include <Wire.h>
#include <SD.h>
#include "power.h"
#include "config.h"
#include "state.h"
#include "settings.h"
#include "sensors.h"
#include "sd_logger.h"
#include "backup.h"
#include "display.h"
#include "health.h"

// ---------------------------------------------------------------------------
// Despierto a la fuerza (FORCE_AWAKE_MS)
// ---------------------------------------------------------------------------
// Lo activan el botón del pin 32 (desde su interrupción, en el núcleo 1), "Posponer"
// en el dashboard y los comandos serie (desde taskComm, en el núcleo 0). Como se
// escribe y se lee desde dos núcleos a la vez, las dos variables solo se tocan dentro
// de una sección crítica: así nunca se lee la bandera nueva con la hora vieja (lo que
// hacía perder una pulsación del botón).
//
// Dura exactamente FORCE_AWAKE_MS desde la última activación y vence sola, sin
// importar si alguna ventana sigue abierta. OJO: no debe cancelarse solo porque "hay
// una ventana activa ahora mismo": "Posponer" normalmente se presiona CON la ventana
// todavía abierta (antes de que cierre), y cancelarlo ahí lo anularía de inmediato.
static portMUX_TYPE forceAwakeMux = portMUX_INITIALIZER_UNLOCKED;
static bool forceAwakeOn = false;
static unsigned long forceAwakeSince = 0;

void IRAM_ATTR forceAwakeStartNow()
{
  unsigned long now = millis();
  portENTER_CRITICAL_SAFE(&forceAwakeMux); // _SAFE: sirve desde una interrupción y desde una tarea
  forceAwakeOn = true;
  forceAwakeSince = now;
  portEXIT_CRITICAL_SAFE(&forceAwakeMux);
}

unsigned long forceAwakeRemainingMs()
{
  portENTER_CRITICAL(&forceAwakeMux);
  bool on = forceAwakeOn;
  unsigned long elapsed = millis() - forceAwakeSince;
  portEXIT_CRITICAL(&forceAwakeMux);
  return (on && elapsed < FORCE_AWAKE_MS) ? FORCE_AWAKE_MS - elapsed : 0;
}

bool forceAwakeActive()
{
  return forceAwakeRemainingMs() > 0;
}

// Botón del pin 32: despierta del deep sleep (esp_sleep_enable_ext0_wakeup) y,
// mientras el sistema ya está despierto, mantiene el WiFi/AP encendido 5 minutos
// aunque la ventana horaria esté cerrada (igual que "Posponer" del dashboard).
volatile unsigned long lastWakeButtonTime = 0;
const unsigned long WAKE_BUTTON_DEBOUNCE = 300; // ms

void IRAM_ATTR WakeButton_rutine()
{
  unsigned long now = millis();

  if (now - lastWakeButtonTime > WAKE_BUTTON_DEBOUNCE)
  {
    forceAwakeStartNow();
    lastWakeButtonTime = now;
  }
}

// ---------------------------------------------------------------------------
// Deep sleep
// ---------------------------------------------------------------------------

// Segundos hasta el próximo despertar: el inicio de ventana o la próxima marca de
// muestreo (:00/:30), lo que llegue primero. Pone sampleWake en true si ese despertar
// es solo para medir la batería (ver quickBatterySample()).
static long secondsUntilNextWake(const DateTime &now)
{
  int currentTotal = now.hour() * 60 + now.minute();

  // Lista de inicios de ventana (los dos rangos de AP cuentan por separado,
  // así nunca se duerme pasando de largo la ventana de la mañana o la de la tarde/noche)
  int starts[] = {
      apMorningStartH * 60 + apMorningStartM,
      apEveningStartH * 60 + apEveningStartM,
      ledStartH * 60 + ledStartM,
      sdStartH * 60 + sdStartM,
      fanStartH * 60 + fanStartM,
  };
  int minWait = 1440; // Máximo minutos en un día

  for (int i = 0; i < 5; i++)
  {
    int diff = (starts[i] > currentTotal) ? (starts[i] - currentTotal) : (1440 - currentTotal + starts[i]);
    if (diff < minWait)
      minWait = diff;
  }

  // Si la próxima marca de muestreo llega antes que la próxima ventana, ese
  // despertar será solo para guardar el voltaje (ver quickBatterySample()).
  int sampleWait = SAMPLE_PERIOD_MIN - (currentTotal % SAMPLE_PERIOD_MIN);
  sampleWake = sampleWait < minWait;
  if (sampleWake)
    minWait = sampleWait;

  return (minWait * 60L) - now.second();
}

// Calcula el próximo despertar y entra en deep sleep. No pide el mutex: lo llaman
// checkDeepSleep() (con el mutex ya tomado), quickBatterySample() y setup().
void enterDeepSleep()
{
  long sleepSeconds;
  DateTime now;
  if (readRtc(now))
  {
    sleepSeconds = secondsUntilNextWake(now);
  }
  else
  {
    // Sin hora válida no se sabe cuándo empieza la próxima ventana: se despierta en
    // SAMPLE_PERIOD_MIN para medir la batería y volver a intentar leer el RTC.
    Serial.println("[ERROR] RTC sin hora válida: se duerme el periodo de muestreo y se reintenta");
    sleepSeconds = SAMPLE_PERIOD_MIN * 60L;
    sampleWake = true;
  }
  // Límite de seguridad: un valor negativo convertido a uint64_t sería un sueño
  // de miles de años (solo el botón lo despertaría).
  sleepSeconds = constrain(sleepSeconds, 1L, 86400L);

  // Llegar a dormir prueba que el firmware arrancó y funciona: si es una OTA nueva se
  // confirma ahora, porque despertar sin confirmarla haría volver al firmware anterior.
  healthConfirmFirmware();

  // Configurar interrupciones
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_32, 0); // Despertar por botón (nivel bajo en GPIO32)
  esp_sleep_enable_timer_wakeup((uint64_t)sleepSeconds * 1000000ULL);

  Serial.printf("Durmiendo %ld segundos (%s)...\n", sleepSeconds, sampleWake ? "muestreo" : "ventana");
  Serial.flush(); // Fuerza la salida de los datos seriales pendientes
  delay(100);     // Breve pausa de seguridad

  esp_deep_sleep_start();
}

// Despertar de muestreo durante el deep sleep (~1 s, sin WiFi, OLED ni tareas).
// Devuelve false si hay que seguir con el arranque normal: falta el RTC o el INA219,
// la hora no es válida, hay una ventana activa, o la protección se liberó dentro de
// la ventana de la trampa. Sin SD ya no aborta (antes arrancaba el sistema completo
// cada 30 min y no registraba nada): la fila queda en el respaldo interno.
bool quickBatterySample()
{
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!rtc.begin() || !ina219.begin())
    return false; // El arranque normal reporta qué periférico falló
  systemData.inaAvailable = true;
  if (!getTime())
    return false; // Sin hora válida: el arranque normal pasa a modo seguro

  // Se mide antes de montar la SD: con la batería crítica ni se toca
  sampleBatteryNow();
  backupBegin(); // montar solo lee (salvo el primer formateo)
  updateCriticalBatteryAtBoot();
  if (anyWakeWindowActive())
    return false;

  if (!criticalBatteryLock && SD.begin(SD_CS))
  {
    systemData.sdAvailable = true;
    initLogFolders(); // Actualiza rutas (updateFileNames) y crea directorios si no existen
  }
  logBattery(EV_MUESTREO); // SD si está montada y la batería no es crítica; respaldo siempre

  if (lowBatteryLock && systemData.busVoltage >= LOW_BAT_RECONNECT_V)
  {
    lowBatteryLock = false;
    logBattery(EV_RECONEXION);
    if (anyWakeWindowActive())
      return false; // La batería se recuperó dentro de la ventana de la trampa: operar normal
  }

  enterDeepSleep();
  return true;
}

void checkDeepSleep()
{
  // Verificación de anulaciones manuales (LED y Ventiladores). Con la protección
  // por bajo voltaje activa no cuentan: igual no pueden encender nada.
  bool anyManualOverride = false;
  if (!lowBatteryLock)
  {
    anyManualOverride = manualLedOverride;
    for (int i = 0; i < 4; i++)
    {
      if (manualFanOverride[i])
      {
        anyManualOverride = true;
        break;
      }
    }
  }
  // Solo entrar en sueño si TODAS las ventanas están cerradas
  if (!anyWakeWindowActive() && !anyManualOverride)
  {
    // Despierto a la fuerza (botón, "Posponer" o comando serie): esperar a que venza
    if (forceAwakeActive())
      return;

    Serial.println("Entrando en modo de ahorro de energía...");

    // Pedir permiso para usar el bus I2C antes de morir
    if (xSemaphoreTake(xI2C_Mutex, pdMS_TO_TICKS(500)) == pdTRUE)
    {
      // --- APAGADO SUAVE ---
      Serial.println("Cerrando periféricos I2C...");
      if (systemData.oledAvailable)
        display.ssd1306_command(SSD1306_DISPLAYOFF); // Apaga el panel OLED
      digitalWrite(ledPin, LOW);                   // Asegura LED apagado

      // Guardar lo pendiente antes de dormir (el mutex ya está tomado aquí): la
      // fila INA deja el E_Wh acumulado para retomarlo al despertar.
      if (loggingAllowed())
      {
        checkDayRollover();
        writePendingFanEvents();
        if (systemData.inaAvailable)
        {
          logINA();
          logBattery(EV_DORMIR);
        }
      }

      enterDeepSleep();
    }
  }
}

// true cuando wantChange se sostuvo LOW_BAT_CONFIRM_MS seguidos (pending/since
// guardan entre llamadas si la condición de cambio se está cumpliendo y desde cuándo)
bool changeConfirmed(bool wantChange, bool &pending, unsigned long &since)
{
  if (!wantChange)
  {
    pending = false;
    return false;
  }
  if (!pending)
  {
    pending = true;
    since = millis();
    return false;
  }
  if (millis() - since < LOW_BAT_CONFIRM_MS)
    return false;
  pending = false;
  return true;
}

// Protección por bajo voltaje (ver LOW_BAT_* y CRIT_BAT_*). Se llama desde taskComm,
// que es la única tarea que escribe en la SD, para registrar el corte y la reconexión.
void updateLowBatteryProtection()
{
  static bool lowPending = false, critPending = false;
  static unsigned long lowSince = 0, critSince = 0;

  if (!systemData.inaAvailable || systemData.busVoltage <= 0)
    return; // Sin medición no se protege (ni se bloquea)
  float v = systemData.busVoltage;

  if (changeConfirmed(lowBatteryLock ? (v >= LOW_BAT_RECONNECT_V) : (v < LOW_BAT_CUTOFF_V), lowPending, lowSince))
  {
    lowBatteryLock = !lowBatteryLock;
    Serial.printf("Protección por bajo voltaje: %s (%.2f V)\n",
                  lowBatteryLock ? "luz y ventiladores BLOQUEADOS" : "liberada", v);
    logProtectionEvent(lowBatteryLock ? EV_CORTE_BAJO_V : EV_RECONEXION);
  }

  if (changeConfirmed(criticalBatteryLock ? (v >= CRIT_BAT_RECONNECT_V) : (v < CRIT_BAT_CUTOFF_V), critPending, critSince))
  {
    // BAT_CRITICA se registra antes de bloquear: es la última fila de la SD hasta que
    // el voltaje se recupere (el respaldo interno sigue con las filas BAT)
    if (!criticalBatteryLock)
      logProtectionEvent(EV_BAT_CRITICA);
    criticalBatteryLock = !criticalBatteryLock;
    Serial.printf("Batería crítica: %s (%.2f V)\n",
                  criticalBatteryLock ? "SD y WiFi en pausa" : "SD reanudada", v);
  }
}

// Promedio de 5 lecturas, igual que readINA(), para decidir al despertar (antes
// de montar la SD o encender el WiFi) si la batería está crítica.
void sampleBatteryNow()
{
  float voltageSum = 0, currentSum = 0;
  delay(5); // Primera conversión del INA219 tras begin()
  for (int k = 0; k < 5; k++)
  {
    voltageSum += ina219.getBusVoltage_V();
    currentSum += ina219.getCurrent_mA();
    delay(10);
  }
  systemData.busVoltage = voltageSum / 5;
  systemData.current_mA = currentSum / 5;
}

// Al despertar no se espera LOW_BAT_CONFIRM_MS. Si la batería está crítica no se toca
// la SD en absoluto; el evento BAT_CRITICA queda solo en el respaldo interno (montado
// antes de llamar a esta función).
void updateCriticalBatteryAtBoot()
{
  if (!criticalBatteryLock && systemData.busVoltage < CRIT_BAT_CUTOFF_V)
  {
    criticalBatteryLock = true;
    lowBatteryLock = true; // por debajo del corte crítico también está bajo el corte normal
    Serial.printf("Batería crítica al despertar (%.2f V): SD y WiFi en pausa\n", systemData.busVoltage);
    logBattery(EV_BAT_CRITICA);
  }
  else if (criticalBatteryLock && systemData.busVoltage >= CRIT_BAT_RECONNECT_V)
  {
    criticalBatteryLock = false;
    Serial.printf("Batería recuperada (%.2f V): SD reanudada\n", systemData.busVoltage);
  }
}
