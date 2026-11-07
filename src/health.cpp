#include <esp_task_wdt.h>
#include <esp_ota_ops.h>
#include "health.h"
#include "config.h"

// ===========================================================================
// Watchdog de tareas
// ===========================================================================

void healthBegin()
{
  // El sistema ya trae un watchdog de 5 s que solo vigila la tarea ociosa del núcleo 0.
  // Se reconfigura con un tiempo más largo (los trabajos largos avisan por el camino)
  // y con reinicio (panic = true) cuando una tarea vigilada deja de avisar.
  esp_task_wdt_init(TASK_WDT_TIMEOUT_S, true);
}

void healthWatchThisTask()
{
  esp_task_wdt_add(NULL); // NULL = la tarea que llama
}

void healthUnwatchThisTask()
{
  esp_task_wdt_delete(NULL);
}

void healthFeed()
{
  esp_task_wdt_reset(); // si la tarea no está vigilada (p. ej. setup()) no hace nada
}

// ===========================================================================
// Rollback de la OTA
// ===========================================================================

// Arduino confirma el firmware nuevo apenas arranca (initArduino(), antes de setup()),
// así que el rollback del bootloader nunca llegaba a actuar. Devolviendo true aquí,
// esa confirmación queda en manos de healthConfirmFirmware().
// extern "C": la versión que se reemplaza está declarada en un archivo C del core.
extern "C" bool verifyRollbackLater()
{
  return true;
}

static bool firmwareConfirmed = false;

// true si este firmware llegó por OTA y todavía no se confirmó
static bool firmwareOnTrial()
{
  esp_ota_img_states_t state;
  return esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
         state == ESP_OTA_IMG_PENDING_VERIFY;
}

void healthConfirmFirmware()
{
  if (firmwareConfirmed)
    return;
  firmwareConfirmed = true;
  if (firmwareOnTrial())
  {
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("Firmware nuevo confirmado: ya no se vuelve al anterior");
  }
}

void healthCheckFirmware()
{
  if (!firmwareConfirmed && millis() >= OTA_TRIAL_MS)
    healthConfirmFirmware();
}

// ===========================================================================
// Diagnóstico
// ===========================================================================

struct RegisteredTask
{
  TaskHandle_t handle;
  const char *name;
};
static RegisteredTask registeredTasks[4];
static int registeredTaskCount = 0;

void healthRegisterTask(TaskHandle_t task, const char *name)
{
  if (task != NULL && registeredTaskCount < 4)
    registeredTasks[registeredTaskCount++] = {task, name};
}

static const char *resetReasonName(esp_reset_reason_t reason)
{
  switch (reason)
  {
  case ESP_RST_POWERON:
    return "encendido";
  case ESP_RST_EXT:
    return "botón EN";
  case ESP_RST_SW:
    return "reinicio por software (p. ej. después de una OTA)";
  case ESP_RST_PANIC:
    return "ERROR del firmware (panic)";
  case ESP_RST_INT_WDT:
  case ESP_RST_TASK_WDT:
  case ESP_RST_WDT:
    return "ERROR: watchdog (una tarea se colgó)";
  case ESP_RST_DEEPSLEEP:
    return "despertar del deep sleep";
  case ESP_RST_BROWNOUT:
    return "caída de voltaje (brownout)";
  default:
    return "desconocido";
  }
}

bool healthLastResetWasFailure(BatEvent &event)
{
  switch (esp_reset_reason())
  {
  case ESP_RST_PANIC:
  case ESP_RST_INT_WDT:
  case ESP_RST_TASK_WDT:
  case ESP_RST_WDT:
    event = EV_REINICIO_ERROR;
    return true;
  case ESP_RST_BROWNOUT:
    event = EV_REINICIO_BROWNOUT;
    return true;
  default:
    return false;
  }
}

void healthPrintDiag()
{
  Serial.println("----- Diagnóstico -----");
  Serial.printf("Encendido hace: %lu s\n", millis() / 1000);
  Serial.printf("Motivo del último arranque: %s\n", resetReasonName(esp_reset_reason()));
  Serial.printf("Firmware: %s\n", firmwareOnTrial() ? "A PRUEBA (si se reinicia ahora, vuelve al anterior)" : "confirmado");

  // Pila: lo mínimo que llegó a quedar libre desde el arranque. Si baja de ~1 KB,
  // conviene agrandar la pila de esa tarea en main.cpp (xTaskCreatePinnedToCore).
  for (int i = 0; i < registeredTaskCount; i++)
    Serial.printf("Pila libre mínima, %-12s: %u bytes\n", registeredTasks[i].name,
                  (unsigned)uxTaskGetStackHighWaterMark(registeredTasks[i].handle));

  Serial.printf("Memoria (heap): %u bytes libres, mínimo histórico %u, bloque mayor %u\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  Serial.println("-----------------------");
}
