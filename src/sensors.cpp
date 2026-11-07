#include <Wire.h>
#include <Adafruit_Sensor.h>
#include "sensors.h"
#include "config.h"
#include "state.h"

Adafruit_INA219 ina219;
Adafruit_BME280 bme;
RTC_DS1307 rtc;

void getTime()
{
  DateTime now = rtc.now();

  systemData.hour = now.hour();
  systemData.minute = now.minute();
  systemData.second = now.second();
  systemData.day = now.day();
  systemData.month = now.month();
  systemData.year = now.year();
}

DateTime systemDateTime()
{
  return DateTime(systemData.year, systemData.month, systemData.day,
                  systemData.hour, systemData.minute, systemData.second);
}

void readBME()
{
  // Variables estáticas para acumular datos entre llamadas
  static float tempSum = 0, humSum = 0, presSum = 0, altSum = 0;
  static int sampleCount = 0;
  const int targetSamples = 5;

  // Una sola muestra por llamada (sin bucle ni delay): taskUI llama cada ~100 ms
  tempSum += bme.readTemperature();
  humSum += bme.readHumidity();
  presSum += bme.readPressure() / 100.0F; // Pa -> hPa
  altSum += bme.readAltitude(SEALEVELPRESSURE_HPA);

  sampleCount++;

  if (sampleCount >= targetSamples)
  {
    // Calculamos el promedio y actualizamos la estructura global
    systemData.temperature = tempSum / targetSamples;
    systemData.humidity = humSum / targetSamples;
    systemData.pressure = presSum / targetSamples;
    systemData.altitude = altSum / targetSamples;

    // Reiniciamos acumuladores para el siguiente ciclo
    tempSum = 0;
    humSum = 0;
    presSum = 0;
    altSum = 0;
    sampleCount = 0;
  }
}

void readINA()
{
  static float voltageSum = 0, currentSum = 0;
  static int sampleCount = 0;
  const int targetSamples = 5;

  // Leer valores del INA219
  float busVoltage = ina219.getBusVoltage_V();
  float current_mA = ina219.getCurrent_mA();

  voltageSum += busVoltage;
  currentSum += current_mA;

  sampleCount++;

  if (sampleCount >= targetSamples)
  {
    // Promedio de los valores actuales
    systemData.busVoltage = voltageSum / targetSamples;
    systemData.current_mA = currentSum / targetSamples;

    // Calculo de la potencia promedio P=V*I, asegurando que la corriente en Amperios
    systemData.power_W = (systemData.busVoltage * systemData.current_mA) / 1000.0; // 1 mA/1000 = 1 A

    // Acumular para el promedio de potencia del intervalo de registro
    systemData.powerSumInterval += systemData.power_W;
    systemData.intervalSamples++;

    // Integración: Energía (Wh) = Potencia (W) * Tiempo (h), con el tiempo REAL
    // desde el promedio anterior (no 0.5 s fijo: cada ciclo de taskUI tarda más
    // por la OLED/I2C, y se subestimaba la energía).
    static unsigned long lastAverageMs = 0;
    unsigned long nowMs = millis();
    if (lastAverageMs != 0)
      systemData.totalEnergy_Wh += systemData.power_W * ((nowMs - lastAverageMs) / 3600000.0);
    lastAverageMs = nowMs;

    // Reiniciar acumuladores
    voltageSum = 0;
    currentSum = 0;
    sampleCount = 0;
  }
}
