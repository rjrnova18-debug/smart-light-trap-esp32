#include <ArduinoJson.h>
#include <Update.h>
#include "web_server.h"
#include "admin_page.h"
#include "web_files.h" // dashboard comprimido, generado por tools/embed_web.py
#include "config.h"
#include "state.h"
#include "settings.h"
#include "sensors.h"
#include "sd_logger.h"
#include "sd_files.h"
#include "backup.h"
#include "backup_files.h"
#include "power.h"
#include "health.h"

// -------- Servidor Web ESP32 --------
WebServer server(80);

// -------- Mantenimiento remoto: OTA --------
// Usuario/clave para /admin, /update y /borrar (aparte de la clave del WiFi).
// Se definen en include/secrets.h.
const char *adminUser = ADMIN_USER;
const char *adminPassword = ADMIN_PASSWORD;
static bool maintAuthOk = false; // resultado de autenticación, cacheado durante una subida

// El dashboard vive en la flash, comprimido: se envía tal cual y el navegador lo
// descomprime (Content-Encoding: gzip). No depende de la SD.
void sendGzipped(const char *contentType, const uint8_t *data, size_t len)
{
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, contentType, (PGM_P)data, len);
}

void handleRoot()
{
  sendGzipped("text/html; charset=utf-8", WEB_DASHBOARD_HTML_GZ, WEB_DASHBOARD_HTML_GZ_LEN);
}

void handleEstado()
{
  JsonDocument doc; // Usando ArduinoJson 7

  doc["temp"] = systemData.temperature;
  doc["hum"] = systemData.humidity;
  doc["pres"] = systemData.pressure;
  doc["alt"] = systemData.altitude;

  char fechaBuf[20];
  sprintf(fechaBuf, "%02d/%02d/%04d %02d:%02d",
          systemData.day, systemData.month, systemData.year,
          systemData.hour, systemData.minute);
  doc["timestamp"] = fechaBuf;
  doc["second"] = systemData.second; // Para diagnóstico del RTC (ver "Hora actual del RTC" en Configuración)

  doc["led"] = systemData.ledState;
  JsonArray fans = doc["fans"].to<JsonArray>();
  for (int i = 0; i < 4; i++)
  {
    fans.add(systemData.loads[i]);
  }

  JsonArray counts = doc["areaCounts"].to<JsonArray>();
  for (int i = 0; i < 4; i++)
  {
    counts.add(systemData.areaCounts[i]);
  }

  // Estado crudo de los 16 sensores IR (mismo orden que ya se corrigió para la OLED,
  // ver IR_DISPLAY_ORDER), para poder verlos desde el dashboard sin abrir la carcasa.
  JsonArray irSensorsJson = doc["irSensors"].to<JsonArray>();
  for (int i = 0; i < 16; i++)
  {
    irSensorsJson.add(systemData.irSensors[IR_DISPLAY_ORDER[i]]);
  }

  doc["manualLed"] = manualLedOverride;

  JsonArray manualFans = doc["manualFans"].to<JsonArray>();
  for (int i = 0; i < 4; i++)
  {
    manualFans.add(manualFanOverride[i]);
  }

  doc["ledSH"] = ledStartH;
  doc["ledSM"] = ledStartM;
  doc["ledEH"] = ledEndH;
  doc["ledEM"] = ledEndM;

  doc["fanSH"] = fanStartH;
  doc["fanSM"] = fanStartM;
  doc["fanEH"] = fanEndH;
  doc["fanEM"] = fanEndM;

  doc["apMoSH"] = apMorningStartH;
  doc["apMoSM"] = apMorningStartM;
  doc["apMoEH"] = apMorningEndH;
  doc["apMoEM"] = apMorningEndM;

  doc["apEvSH"] = apEveningStartH;
  doc["apEvSM"] = apEveningStartM;
  doc["apEvEH"] = apEveningEndH;
  doc["apEvEM"] = apEveningEndM;

  doc["sdSH"] = sdStartH;
  doc["sdSM"] = sdStartM;
  doc["sdEH"] = sdEndH;
  doc["sdEM"] = sdEndM;

  doc["apActive"] = operatingWindow_AP();
  doc["ledActive"] = operatingWindow_LED();
  doc["sdActive"] = operatingWindow_SD();
  doc["fanActive"] = operatingWindow_Fan();

  // Para el aviso de cierre de conexión (AP) en el dashboard
  unsigned long forceAwakeLeftMs = forceAwakeRemainingMs();
  doc["forceAwake"] = forceAwakeLeftMs > 0;
  doc["forceAwakeRemainingSec"] = forceAwakeLeftMs / 1000;

  // Sin hora confiable del RTC la trampa está en modo seguro (aviso en el dashboard)
  doc["rtcOk"] = systemData.rtcAvailable;

  doc["fanDur"] = FAN_ON_TIME / 60000; // Lo enviamos en minutos al HTML
  doc["threshold"] = AREA_THRESHOLD;
  doc["maxFans"] = MAX_CONCURRENT_FANS;

  // Almacenamiento: el dashboard avisa si la SD no está montada y muestra el uso
  // del respaldo interno (y elige "Memoria interna" como origen si falta la SD)
  doc["sdOk"] = systemData.sdAvailable;
  doc["backupOk"] = systemData.backupAvailable;
  doc["backupUsoPct"] = backupUsedPct();

  doc["busVoltage"] = systemData.busVoltage;
  doc["current_mA"] = systemData.current_mA;
  doc["power_W"] = systemData.power_W;
  doc["totalEnergyWh"] = systemData.totalEnergy_Wh;

  // Protección por bajo voltaje (aviso en el dashboard)
  doc["lowBattery"] = lowBatteryLock;
  doc["lowBatCutoff"] = LOW_BAT_CUTOFF_V;
  doc["lowBatReconnect"] = LOW_BAT_RECONNECT_V;

  // Tarjeta "Energía y pantalla" y aviso de modo solo carga
  doc["chargeMode"] = chargeOnlyMode;
  doc["oledOn"] = (bool)oledEnabled;

  // Ventiladores en cola por el interlock (esperando cupo para encender junto
  // con el otro ventilador de su misma área — la cola ahora es por área, no
  // por ventilador individual, ver pendingAreaMask).
  JsonArray fansQueued = doc["fansQueued"].to<JsonArray>();
  for (int fan = 0; fan < 4; fan++)
  {
    bool queued = false;
    for (int area = 0; area < 4 && !queued; area++)
    {
      if (!(pendingAreaMask & (1 << area)))
        continue;
      for (int k = 0; k < 2; k++)
      {
        if (AREA_TO_FANS[area][k] == fan)
        {
          queued = !manualFanOverride[fan];
          break;
        }
      }
    }
    fansQueued.add(queued);
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleClearManual()
{
  manualLedOverride = false;

  for (int i = 0; i < 4; i++)
  {
    manualFanOverride[i] = false;
  }

  server.send(200, "text/plain", "Manual mode cleared");
}

void handleExtendConnection()
{
  forceAwakeStartNow();
  server.send(200, "application/json", "{\"message\":\"Conexión extendida 5 minutos\"}");
}

// Tarjeta "Energía y pantalla" del dashboard: JSON {chargeMode?: bool, oled?: bool}.
// Se aplica y se guarda en NVS al instante (sobrevive al deep sleep y a cortes de energía).
void handleSetOptions()
{
  if (!server.hasArg("plain"))
  {
    server.send(400, "application/json", "{\"error\":\"No body\"}");
    return;
  }

  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));

  preferences.begin("config", false);
  if (doc["chargeMode"].is<bool>() && doc["chargeMode"].as<bool>() != chargeOnlyMode)
  {
    chargeOnlyMode = doc["chargeMode"].as<bool>();
    if (chargeOnlyMode)
    {
      // Arranca sin manuales: uno olvidado dejaría la carga (y al ESP32) encendida toda la noche
      manualLedOverride = false;
      for (int i = 0; i < 4; i++)
        manualFanOverride[i] = false;
    }
    preferences.putBool("chargeMode", chargeOnlyMode);
    logProtectionEvent(chargeOnlyMode ? EV_MODO_CARGA_ON : EV_MODO_CARGA_OFF);
    Serial.printf("Modo solo carga: %s\n", chargeOnlyMode ? "ACTIVO" : "desactivado");
  }
  if (doc["oled"].is<bool>())
  {
    oledEnabled = doc["oled"].as<bool>();
    oledTempOn = false;
    preferences.putBool("oledOn", oledEnabled);
  }
  preferences.end();

  server.send(200, "application/json", "{\"message\":\"Opciones guardadas\"}");
}

void handleAdminPage()
{
  if (!server.authenticate(adminUser, adminPassword))
  {
    server.requestAuthentication();
    return;
  }
  server.send_P(200, "text/html", ADMIN_PAGE_HTML);
}

// ---- OTA: POST /update ----
void handleOTAUpload()
{
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START)
  {
    maintAuthOk = server.authenticate(adminUser, adminPassword);
    if (!maintAuthOk)
      return;

    Serial.printf("Actualizando firmware: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN))
    {
      Update.printError(Serial);
    }
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    // Toda la subida (15-30 s) ocurre dentro de una sola llamada a handleClient():
    // se avisa al watchdog en cada bloque para que no reinicie a mitad de la OTA.
    healthFeed();
    if (!maintAuthOk)
      return;
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
    {
      Update.printError(Serial);
    }
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    if (!maintAuthOk)
      return;
    if (Update.end(true))
    {
      Serial.printf("Firmware OTA: %u bytes escritos correctamente\n", upload.totalSize);
    }
    else
    {
      Update.printError(Serial);
    }
  }
}

void handleOTAResult()
{
  if (!maintAuthOk)
  {
    server.requestAuthentication();
    return;
  }
  server.sendHeader("Connection", "close");
  if (Update.hasError())
  {
    server.send(200, "text/plain", "Error actualizando firmware. El dispositivo sigue con el firmware anterior; puedes volver a intentarlo.");
  }
  else
  {
    server.send(200, "text/plain", "Firmware actualizado correctamente. Reiniciando...");
    delay(1000);
    ESP.restart();
  }
}

void handleControl()
{
  if (!server.hasArg("plain"))
  {
    server.send(400, "text/plain", "No body received");
    return;
  }

  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));

  // ---- LED ----
  if (!doc["led"].isNull())
  {
    manualLedOverride = true;
    manualLedState = doc["led"];
  }

  // ---- FAN ----
  if (!doc["fan"].isNull())
  {
    int index = doc["fan"];
    if (index >= 0 && index < 4)
    {
      manualFanOverride[index] = true;
      manualFanState[index] = doc["state"];
    }
  }
  server.send(200, "text/plain", "OK");
}

// Horas y minutos de las 5 ventanas: tienen que venir todas en /saveWindows
static const char *const WINDOW_KEYS[] = {
    "apMoSH", "apMoSM", "apMoEH", "apMoEM", // AP mañana: inicio (H, M) y fin (H, M)
    "apEvSH", "apEvSM", "apEvEH", "apEvEM", // AP tarde/noche
    "ledSH", "ledSM", "ledEH", "ledEM",     // luz
    "fanSH", "fanSM", "fanEH", "fanEM",     // ventiladores
    "sdSH", "sdSM", "sdEH", "sdEM",         // ventana extra (SD)
};

void handleSaveWindows()
{
  if (!server.hasArg("plain"))
  {
    server.send(400, "application/json", "{\"error\":\"No body\"}");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok)
  {
    server.send(400, "application/json", "{\"error\":\"JSON inválido: no se cambió nada\"}");
    return;
  }
  // Se valida TODO antes de cambiar algo. Antes, una hora que faltaba quedaba en 00:00
  // y se guardaba en NVS: una ventana 00:00-00:00 dejaba el AP o la luz siempre
  // encendidos (ahora además cuenta como ventana vacía, ver isInsideWindow()).
  for (const char *key : WINDOW_KEYS)
  {
    if (!doc[key].is<int>())
    {
      String error = String("{\"error\":\"Falta o no es un número: ") + key + ". No se cambió nada\"}";
      server.send(400, "application/json", error);
      return;
    }
  }

  // Si el JSON trae estas llaves, actualizamos las variables
  if (!doc["fanDur"].isNull())
  {
    int minRecibidos = doc["fanDur"].as<int>();
    int minValidos = constrain(minRecibidos, 1, 10); // Validar entre 1 y 10 minutos
    // Convertimos los minutos recibidos a milisegundos
    FAN_ON_TIME = minValidos * 60000;
  }
  if (!doc["threshold"].isNull())
  {
    int thresholdRecibido = doc["threshold"].as<int>();
    AREA_THRESHOLD = constrain(thresholdRecibido, 1, 30); // Validar entre 1 y 30
  }
  if (!doc["maxFans"].isNull())
  {
    int maxRecibido = doc["maxFans"].as<int>();
    MAX_CONCURRENT_FANS = constrain(maxRecibido, 1, 4); // Validar entre 1 y 4
  }

  // AP (dos rangos: mañana y tarde/noche)
  apMorningStartH = constrain((int)doc["apMoSH"], 0, 23);
  apMorningStartM = constrain((int)doc["apMoSM"], 0, 59);
  apMorningEndH = constrain((int)doc["apMoEH"], 0, 23);
  apMorningEndM = constrain((int)doc["apMoEM"], 0, 59);

  apEveningStartH = constrain((int)doc["apEvSH"], 0, 23);
  apEveningStartM = constrain((int)doc["apEvSM"], 0, 59);
  apEveningEndH = constrain((int)doc["apEvEH"], 0, 23);
  apEveningEndM = constrain((int)doc["apEvEM"], 0, 59);

  // LED
  ledStartH = constrain((int)doc["ledSH"], 0, 23);
  ledStartM = constrain((int)doc["ledSM"], 0, 59);
  ledEndH = constrain((int)doc["ledEH"], 0, 23);
  ledEndM = constrain((int)doc["ledEM"], 0, 59);

  // FAN
  fanStartH = constrain((int)doc["fanSH"], 0, 23);
  fanStartM = constrain((int)doc["fanSM"], 0, 59);
  fanEndH = constrain((int)doc["fanEH"], 0, 23);
  fanEndM = constrain((int)doc["fanEM"], 0, 59);

  // SD
  sdStartH = constrain((int)doc["sdSH"], 0, 23);
  sdStartM = constrain((int)doc["sdSM"], 0, 59);
  sdEndH = constrain((int)doc["sdEH"], 0, 23);
  sdEndM = constrain((int)doc["sdEM"], 0, 59);

  saveSettingsToNVS();

  server.send(200, "application/json", "{\"message\":\"Windows saved\"}");
}

void handleResetDefaults()
{
  loadDefaultSettings();
  saveSettingsToNVS();
  server.send(200, "application/json", "{\"message\":\"Valores predeterminados restaurados\"}");
}

void handleSetRTC()
{
  if (!server.hasArg("plain"))
  {
    server.send(400, "application/json", "{\"error\":\"No body\"}");
    return;
  }

  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));

  if (doc["year"].isNull() || doc["month"].isNull() || doc["day"].isNull() ||
      doc["hour"].isNull() || doc["minute"].isNull() || doc["second"].isNull())
  {
    server.send(400, "application/json", "{\"error\":\"Faltan campos de fecha/hora\"}");
    return;
  }

  int year = constrain(doc["year"].as<int>(), 2020, 2099);
  int month = constrain(doc["month"].as<int>(), 1, 12);
  int day = constrain(doc["day"].as<int>(), 1, 31);
  int hour = constrain(doc["hour"].as<int>(), 0, 23);
  int minute = constrain(doc["minute"].as<int>(), 0, 59);
  int second = constrain(doc["second"].as<int>(), 0, 59);

  // El RTC está en el mismo bus I2C que el OLED/BME280: hay que pedir el mutex antes de tocarlo.
  if (xSemaphoreTake(xI2C_Mutex, pdMS_TO_TICKS(200)) == pdTRUE)
  {
    rtc.adjust(DateTime(year, month, day, hour, minute, second));
    getTime(); // Refresca systemData para que el próximo /estado ya muestre la hora nueva
    xSemaphoreGive(xI2C_Mutex);
    server.send(200, "application/json", "{\"message\":\"RTC actualizado\"}");
  }
  else
  {
    server.send(503, "application/json", "{\"error\":\"Bus I2C ocupado, intenta de nuevo\"}");
  }
}

void setupWebServer()
{
  // Dashboard embebido en el firmware (web/ → include/web_files.h al compilar)
  server.on("/", HTTP_GET, handleRoot);
  server.on("/style.css", HTTP_GET, []()
            { sendGzipped("text/css", WEB_STYLE_CSS_GZ, WEB_STYLE_CSS_GZ_LEN); });
  server.on("/chart.js", HTTP_GET, []()
            { sendGzipped("application/javascript", WEB_CHART_JS_GZ, WEB_CHART_JS_GZ_LEN); });
  server.onNotFound([]()
                    { server.send(404, "text/plain", "No encontrado"); });

  // API del dashboard
  server.on("/estado", HTTP_GET, handleEstado);
  server.on("/archivos", HTTP_GET, handleArchives);
  server.on("/descargar", HTTP_GET, handleDownload);
  server.on("/descargarZip", HTTP_GET, handleDownloadZip);
  server.on("/borrar", HTTP_POST, handleDeletePath);
  server.on("/respaldo/formatear", HTTP_POST, handleFormatBackup);
  server.on("/saveWindows", HTTP_POST, handleSaveWindows);
  server.on("/resetDefaults", HTTP_POST, handleResetDefaults);
  server.on("/clearManual", HTTP_POST, handleClearManual);
  server.on("/control", HTTP_POST, handleControl);
  server.on("/setRTC", HTTP_POST, handleSetRTC);
  server.on("/extendConnection", HTTP_POST, handleExtendConnection);
  server.on("/setOptions", HTTP_POST, handleSetOptions);

  // Mantenimiento remoto: siempre disponible sin importar el estado de la SD
  server.on("/admin", HTTP_GET, handleAdminPage);
  server.on("/update", HTTP_POST, handleOTAResult, handleOTAUpload);
}
