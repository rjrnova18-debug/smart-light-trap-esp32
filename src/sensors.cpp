#include <Wire.h>
#include <Adafruit_Sensor.h>
#include "sensors.h"
#include "config.h"
#include "state.h"

Adafruit_INA219 ina219;
Adafruit_BME280 bme;
RTC_DS1307 rtc;

// Lee el RTC y devuelve true solo si la hora es válida. Dos comprobaciones:
//  1. Que el RTC responda en el bus I2C. Si no responde, rtc.now() (RTClib) no avisa
//     y arma la fecha con lo que haya en memoria: basura que podría parecer válida.
//  2. Que la fecha exista (DateTime::isValid()): descarta lecturas corruptas, p. ej. 165:165.
bool readRtc(DateTime &out)
{
  Wire.beginTransmission(RTC_ADDR);
  if (Wire.endTransmission() != 0)
    return false;
  out = rtc.now();
  return out.isValid();
}

// Actualiza la hora de systemData. Una lectura fallida (cable flojo, RTC sin
// respuesta) haría creer que la ventana de la luz está siempre activa o que hay que
// dormir un tiempo negativo. Por eso:
//  - lectura válida: actualiza systemData y la hora vuelve a ser confiable;
//  - lectura inválida: se conserva la última hora buena; tras RTC_MAX_BAD_READS
//    seguidas la hora deja de ser confiable (rtcAvailable = false) y la trampa pasa a
//    modo seguro: no opera por horario (ver isInsideWindow()) hasta que el RTC responda.
bool getTime()
{
  static int badReads = 0;
  DateTime now;

  if (!readRtc(now))
  {
    // Se cuenta hasta el límite (y ahí se avisa una sola vez)
    if (badReads < RTC_MAX_BAD_READS && ++badReads == RTC_MAX_BAD_READS)
    {
      systemData.rtcAvailable = false;
      Serial.println("[ERROR] El RTC no da una hora válida: MODO SEGURO (sin horario) hasta que responda");
    }
    return false;
  }

  if (badReads >= RTC_MAX_BAD_READS)
    Serial.println("RTC recuperado: se vuelve a operar por horario");
  badReads = 0;
  systemData.rtcAvailable = true;
  systemData.hour = now.hour();
  systemData.minute = now.minute();
  systemData.second = now.second();
  systemData.day = now.day();
  systemData.month = now.month();
  systemData.year = now.year();
  return true;
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
