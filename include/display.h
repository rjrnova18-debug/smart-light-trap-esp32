#pragma once
// Pantalla OLED y botón de cambio de pantalla (pin 35).
#include <Adafruit_SSD1306.h>

extern Adafruit_SSD1306 display;

void IRAM_ATTR Interrupt_rutine(); // ISR del botón del pin 35: cambia de pantalla o enciende la OLED un rato
void showData();         // Visualizar datos en la OLED
