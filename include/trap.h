#pragma once
// Operación de la trampa: luz, sensores IR (MUX), conteo por área e interlock de ventiladores.
#include <Arduino.h>

void LED_Operation(); // Activa o Desactiva LED
void selectMuxChannel(uint8_t channel);
void readIRSensors();                                                 // Lee sensores IR
void evaluateAreas();                                                 // Evalúa actividad en áreas y actualiza contadores
void controlFans();                                                   // Controla apagado y tiempo de encendido Fans
int countActiveFans();                                                // cuenta ventiladores encendidos (manual o automático)
bool isAreaBusy(int area);                                            // true si algún ventilador asociado a esa área ya está encendido
void activateFan(int i, unsigned long now);                           // Enciende el ventilador i
int getAreaFanCountNeeded(int area);                                  // Cuántos ventiladores automáticos tiene disponibles esta área ahora mismo
void activateAreaFans(int area, unsigned long now, int triggerCount); // Enciende todos los ventiladores automáticos de esta área juntos y registra el evento
