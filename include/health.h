#pragma once
// Salud del firmware: lo que mantiene viva a la trampa sola en campo y permite
// diagnosticarla.
//
//  - Watchdog de tareas: si una tarea vigilada deja de avisar "sigo viva" durante
//    TASK_WDT_TIMEOUT_S, el ESP32 se reinicia en vez de quedar colgado (sin registrar,
//    sin dormir y sin protección de batería).
//  - Rollback de la OTA: un firmware recién instalado queda "a prueba" hasta que se
//    confirma. Si se reinicia antes (cuelgue, error, watchdog), el bootloader vuelve
//    solo al firmware anterior. Así una OTA mala no deja la trampa inservible en campo.
//  - Diagnóstico: pila libre de cada tarea, memoria y motivo del último reinicio.
#include <Arduino.h>
#include "log_format.h"

// -------- Watchdog --------
void healthBegin();           // En setup(), antes de crear las tareas: configura el watchdog
void healthWatchThisTask();   // La tarea que llama queda vigilada (al inicio de cada tarea)
void healthUnwatchThisTask(); // Deja de vigilarla (antes de un trabajo que no puede avisar, p. ej. formatear)
void healthFeed();            // "Sigo viva": en cada vuelta del bucle y dentro de los trabajos largos

// -------- Rollback de la OTA --------
void healthCheckFirmware();   // Desde taskComm: confirma el firmware nuevo tras OTA_TRIAL_MS funcionando
void healthConfirmFirmware(); // Lo confirma ya (antes del deep sleep: despertar sin confirmar = rollback)

// -------- Diagnóstico --------
void healthRegisterTask(TaskHandle_t task, const char *name); // Para mostrar su pila libre en el diagnóstico
void healthPrintDiag();                                      // Comando serie "diag"

// Si el arranque anterior terminó por una falla, deja en event el evento a registrar
// (EV_REINICIO_ERROR o EV_REINICIO_BROWNOUT) y devuelve true. Si fue normal, false.
bool healthLastResetWasFailure(BatEvent &event);
