#pragma once
// RTC DS1307, BME280 e INA219.
#include <Adafruit_BME280.h>
#include <Adafruit_INA219.h>
#include "RTClib.h"

extern Adafruit_BME280 bme;
extern Adafruit_INA219 ina219;
extern RTC_DS1307 rtc;

bool readRtc(DateTime &out); // Lee el RTC; false si no responde o la fecha no es válida
bool getTime();              // Actualiza la hora de systemData; false si la lectura no es válida (ver modo seguro)
DateTime systemDateTime();  // Hora de la última lectura del RTC (systemData) como DateTime
void readBME(); // Lee sensor BME280 (promedio de 5 llamadas)
void readINA(); // Lee sensor INA219 (promedio de 5 llamadas) e integra la energía
