#pragma once
// Registro de datos: cada registro se escribe en la SD (CSV diario, carpetas por mes)
// y en el respaldo de la flash interna (backup.h), cada destino por separado: la
// falla de uno no impide el otro. Solo se llama desde taskComm (o desde setup()).
#include <Arduino.h>
#include "state.h"
#include "log_format.h"

extern int lastLoggedDay; // último día registrado (cambio de día en checkDayRollover())
extern String filenameINA;

float recuperarUltimaEnergia(String ubicacionArchivo); // E_Wh del último registro del día en la SD
void updateFileNames();                                // Rutas de los CSV del día en la SD
void initLogFolders();                                 // Crea las carpetas del mes en la SD
void checkDayRollover();                               // Cambio de día: rutas nuevas, rotación del respaldo y reinicio de E_Wh
void logBME();                                         // Clima (cada LOG_EVERY_MIN)
void logINA();                                         // Potencia promedio y energía del día; reinicia el promedio
void logEvent(const FanEvent &evt);                    // Activación de un área (ventiladores)
void writePendingFanEvents();                          // Escribe los eventos encolados por taskControl
void logBattery(BatEvent evento);                      // Voltaje y corriente de la batería con su evento
void logProtectionEvent(BatEvent evento);              // logBattery() desde taskComm, con el mutex I2C
bool sdWritable();                                     // SD montada y batería no crítica
bool loggingAllowed();                                 // Hay algún destino donde registrar (SD o respaldo)
