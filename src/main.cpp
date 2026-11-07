// Light Trap — firmware de la trampa de luz (ESP32). Este archivo solo orquesta:
// setup(), loop() y las tres tareas FreeRTOS. La lógica vive en los módulos de src/
// y la configuración en include/config.h (claves en include/secrets.h).
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include "config.h"
#include "state.h"
#include "settings.h"
#include "sensors.h"
#include "trap.h"
#include "power.h"
#include "sd_logger.h"
#include "backup.h"
#include "display.h"
#include "web_server.h"

// WiFi del punto de acceso (ver include/secrets.h)
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

int lastLogMinute = -1; // minuto del último registro periódico (ver LOG_EVERY_MIN)
bool batWakeLogged = false; // false hasta registrar la primera fila del ciclo (deep sleep reinicia el ESP32)

void taskControl(void *pvParameters)
{
  for (;;)
  {
    readIRSensors();
    LED_Operation();
    evaluateAreas();
    controlFans();
    vTaskDelay(pdMS_TO_TICKS(2)); // Ejecuta cada 2ms
  }
}

void taskUI(void *pvParameters)
{
  for (;;)
  {
    // Protegemos el bus I2C para leer sensor y escribir en OLED
    if (xSemaphoreTake(xI2C_Mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
      if (systemData.rtcAvailable)
        getTime();
      if (systemData.bmeAvailable)
        readBME();
      if (systemData.inaAvailable)
        readINA();

      // OLED: encendida desde el dashboard o temporalmente por el botón del pin 35.
      // Apagada no se redibuja (ahorra 1 KB por I2C cada ciclo).
      if (systemData.oledAvailable)
      {
        if (oledTempOn && millis() - oledTempOnStart >= OLED_TEMP_ON_MS)
          oledTempOn = false;
        bool oledWanted = oledEnabled || oledTempOn;
        static bool oledShown = oledEnabled; // estado en que la dejó setup()
        if (oledWanted)
          showData();
        if (oledWanted != oledShown)
        {
          display.ssd1306_command(oledWanted ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
          oledShown = oledWanted;
        }
      }
      xSemaphoreGive(xI2C_Mutex);
    }
    // Con 100ms, tarda 500ms (0.5s) en tener un promedio nuevo (5 muestras * 100ms)
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void taskComm(void *pvParameters)
{
  for (;;)
  {
    // Lógica de la ventana del AP (con la batería crítica solo el botón/"Posponer" lo enciende)
    bool shouldBeActive = (operatingWindow_AP() && !criticalBatteryLock) || forceAwake;
    if (shouldBeActive && !apIsActive)
    {
      // Encendido completo
      WiFi.mode(WIFI_AP);
      WiFi.softAP(ssid, password);
      Serial.print("IP del servidor: ");
      Serial.println(WiFi.softAPIP());

      server.begin(); // Opcional, pero asegura que el socket esté listo
      apIsActive = true;
      Serial.println("AP y Radio Encendidos");
    }
    else if (!shouldBeActive && apIsActive)
    {
      // Apagado total del hardware de radio
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_OFF);
      apIsActive = false;
      Serial.println("AP y Radio Apagados (WIFI_OFF)");
    }
    // Solo manejar clientes si el AP está activo
    if (apIsActive)
    {
      server.handleClient();
    }

    // Protección por bajo voltaje (registra CORTE_BAJO_V / RECONEXION)
    updateLowBatteryProtection();

    // Comandos "respaldo ..." por el Monitor serial (extracción por cable)
    backupHandleSerial();

    // Registros (SD y respaldo interno): siempre que el equipo esté despierto (ya no
    // depende de la ventana SD), en cada minuto múltiplo de LOG_EVERY_MIN. Con la
    // batería crítica solo el respaldo guarda filas BAT. Esta es la única tarea que
    // escribe en el almacenamiento.
    if (loggingAllowed())
    {
      // busVoltage > 0 espera el primer promedio del INA219 tras despertar (~0.5 s)
      bool inaReady = !systemData.inaAvailable || systemData.busVoltage > 0;
      bool logDue = inaReady && (systemData.minute % LOG_EVERY_MIN == 0) && (systemData.minute != lastLogMinute);
      bool wakeRowDue = inaReady && systemData.inaAvailable && !batWakeLogged;
      bool eventsDue = uxQueueMessagesWaiting(fanEventQueue) > 0;

      if ((logDue || wakeRowDue || eventsDue) && xSemaphoreTake(xI2C_Mutex, pdMS_TO_TICKS(200)) == pdTRUE)
      {
        checkDayRollover();
        writePendingFanEvents();
        if (wakeRowDue)
        {
          logBattery(EV_INICIO);
          batWakeLogged = true;
        }
        if (logDue)
        {
          if (systemData.bmeAvailable)
            logBME();
          if (systemData.inaAvailable)
          {
            logINA();
            logBattery(EV_PERIODICO);
          }
          lastLogMinute = systemData.minute;
        }
        xSemaphoreGive(xI2C_Mutex);
      }
    }
    // INTENTAR DORMIR (Cada 5 segundos para no saturar)
    static unsigned long lastSleepCheck = 0;
    if (millis() - lastSleepCheck > 5000)
    {
      checkDeepSleep();
      lastSleepCheck = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // Ceder tiempo al sistema 10 ms
  }
}

void setup()
{
  Serial.begin(115200);
  delay(20);
  Serial.println("Serial iniciado");

  // Valores predeterminados; lo guardado en NVS (dashboard) los sobrescribe
  loadSettingsFromNVS();

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0)
  {
    forceAwake = true;
    forceAwakeStart = millis();
    Serial.println("Despertado por BOTÓN: Modo espera activo (5 min)");
  }
  else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER)
  {
    Serial.println(sampleWake ? "Despertado por TIMER: muestreo de batería" : "Despertado por TIMER: Hora de despertar");
  }
  else
  {
    Serial.println("Despertado por motivo desconocido o primer arranque");
  }

  // Despertar de muestreo: guarda el voltaje y vuelve a dormir sin pasar por el
  // resto del arranque (quickBatterySample() termina en deep sleep si no hay que operar)
  if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER && sampleWake && quickBatterySample())
    return;

  // PUSH BOTON PIN 35 (cambia de pantalla en el OLED)
  pinMode(INTERRUPT_BUTTON, INPUT);
  attachInterrupt(INTERRUPT_BUTTON, Interrupt_rutine, FALLING);
  // PUSH BOTON PIN 32 (despierta del deep sleep, ver esp_sleep_enable_ext0_wakeup en checkDeepSleep();
  // despierto, este mismo botón fuerza el WiFi/AP encendido vía WakeButton_rutine())
  pinMode(GPIO_NUM_32, INPUT_PULLUP);
  attachInterrupt(GPIO_NUM_32, WakeButton_rutine, FALLING);
  // LED PIN (ON-OFF)
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  Serial.println("LED configurado");
  // MUX PIN CONFIG
  pinMode(MUX_S0, OUTPUT);
  pinMode(MUX_S1, OUTPUT);
  pinMode(MUX_S2, OUTPUT);
  pinMode(MUX_S3, OUTPUT);
  // GPIO34 es uno de los pines de solo-entrada del ESP32 (34-39): el chip NO
  // tiene resistencia interna de pull-up/pull-down para estos pines, así que
  // INPUT_PULLUP/INPUT_PULLDOWN no tienen efecto aquí. Si un canal del MUX
  // queda flotante (sin sensor conectado), la lectura de MUX_SIG puede quedar
  // a merced del ruido. Fix recomendado: resistencia pull-down externa
  // (~10kΩ) entre MUX_SIG y GND. El software solo filtra ruido transitorio
  // (ver readIRSensors()), no reemplaza esa resistencia.
  pinMode(MUX_SIG, INPUT);
  Serial.println("MUX configurado");
  for (int i = 0; i < 4; i++)
  {
    pinMode(FAN_PINS[i], OUTPUT);
    digitalWrite(FAN_PINS[i], LOW);
  }
  Serial.println("Ventiladores configurados");

  Wire.begin(SDA_PIN, SCL_PIN);
  Serial.println("I2C iniciado");

  // RTC DS1307
  if (!rtc.begin())
  {
    Serial.println("[ERROR] No se encontró el RTC");
    systemData.rtcAvailable = false;
  }
  else
  {
    Serial.println("RTC encontrado");
    systemData.rtcAvailable = true;
  }
  if (systemData.rtcAvailable && !rtc.isrunning())
  {
    // DS1307 sin hora (pila agotada o primer uso): se pone la fecha de compilación,
    // que puede estar atrasada. Hay que corregirla desde el dashboard (Configuración).
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    Serial.println("[AVISO] El RTC no tenía hora: se ajustó a la fecha de compilación. Corrígela desde el dashboard");
  }
  getTime(); // Obtener tiempo inicial

  // BME280
  if (!bme.begin(BME_ADDR))
  {
    Serial.println("[ERROR] BME280 no encontrado");
    systemData.bmeAvailable = false;
  }
  else
  {
    Serial.println("BME280 encontrado");
    systemData.bmeAvailable = true;
  }
  // INA219
  if (!ina219.begin())
  {
    Serial.println("[ERROR] INA219 no encontrado (protección por bajo voltaje inactiva)");
    systemData.inaAvailable = false;
  }
  else
  {
    Serial.println("INA219 encontrado");
    systemData.inaAvailable = true;
  }
  // Respaldo interno en la flash (antes de revisar la batería: si está crítica, el
  // evento BAT_CRITICA queda en el respaldo aunque la SD no se toque)
  backupBegin();

  // Batería crítica (p. ej. el BMS acaba de reconectar al amanecer): volver a
  // dormir sin montar la SD ni encender el WiFi, para no repetir el ciclo arrancar →
  // escribir → el BMS corta otra vez. Despertado por el botón sí arranca (sin
  // escribir en la SD) para poder revisar la trampa en campo.
  if (systemData.inaAvailable)
  {
    sampleBatteryNow();
    updateCriticalBatteryAtBoot();
    if (criticalBatteryLock && systemData.rtcAvailable && wakeup_reason != ESP_SLEEP_WAKEUP_EXT0)
      enterDeepSleep();
  }
  // SD Card
  if (!SD.begin(SD_CS))
  {
    Serial.println("[ERROR] No se encontró SD");
    systemData.sdAvailable = false;
  }
  else
  {
    systemData.sdAvailable = true;
    Serial.println("SD iniciado");
    if (sdWritable()) // initLogFolders() puede crear carpetas
    {
      initLogFolders();
      Serial.println("Carpetas SD inicializadas");
    }
  }

  // Obtener la hora actual para saber en qué archivo buscar.
  getTime();
  // Actualizar las rutas de los archivos.
  updateFileNames();
  // RECUPERACIÓN de la energía del día (E_Wh) antes de empezar a medir: primero el
  // respaldo interno (sigue escribiendo aunque saquen la SD) y, si no tiene nada de
  // hoy, el CSV de la SD.
  float recoveredEnergy = backupLastEnergyToday();
  if (recoveredEnergy >= 0)
    Serial.println("Energía del día recuperada del respaldo interno");
  else if (systemData.sdAvailable && SD.exists(filenameINA))
    recoveredEnergy = recuperarUltimaEnergia(filenameINA);
  systemData.totalEnergy_Wh = max(recoveredEnergy, 0.0f);
  // SINCRONIZACIÓN: Evita que checkDayRollover() reinicie la energía recuperada al primer registro
  lastLoggedDay = systemData.day;
  Serial.print("Energía inicializada en: ");
  Serial.println(systemData.totalEnergy_Wh);

  // Rutas del servidor web. El AP y el servidor arrancan en taskComm, solo dentro de
  // su ventana horaria (o con el botón del pin 32).
  setupWebServer();

  // OLED (opcional): sin pantalla la trampa sigue registrando y durmiendo. begin()
  // no detecta si falta, así que primero se pregunta por su dirección en el bus I2C.
  Wire.beginTransmission(OLED_ADDR);
  systemData.oledAvailable = (Wire.endTransmission() == 0) && display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (systemData.oledAvailable)
  {
    Serial.println("OLED iniciada");
    display.clearDisplay();
    display.display();
    if (!oledEnabled)
      display.ssd1306_command(SSD1306_DISPLAYOFF); // apagada desde el dashboard (ver /setOptions)
  }
  else
  {
    Serial.println("[ERROR] OLED no encontrada: se sigue sin pantalla");
  }

  // Crear el Mutex para proteger el bus I2C
  xI2C_Mutex = xSemaphoreCreateMutex();
  // Cola de eventos de ventilador (taskControl -> taskComm, que escribe en la SD)
  fanEventQueue = xQueueCreate(16, sizeof(FanEvent));
  // Tarea 1: Control de Sensores IR y Ventiladores (Núcleo 1, Prioridad máxima)
  xTaskCreatePinnedToCore(taskControl, "ControlTask", 4096, NULL, 3, NULL, 1);
  // Tarea 2: Lectura BME y Pantalla OLED (Núcleo 1, Prioridad media)
  xTaskCreatePinnedToCore(taskUI, "UITask", 4096, NULL, 2, NULL, 1);
  // Tarea 3: WebServer y SD (Núcleo 0, Prioridad baja - Tareas pesadas)
  xTaskCreatePinnedToCore(taskComm, "CommTask", 8192, NULL, 1, NULL, 0);
}

void loop()
{
  // El loop principal queda vacío, las tareas se ejecutan en paralelo
}
