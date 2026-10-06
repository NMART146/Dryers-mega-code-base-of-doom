#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define BME_SCK 13
#define BME_MISO 12
#define BME_MOSI 11
#define BME_CS 10


void setup()
{
  Serial.begin(115200);
}

void loop()
{

}

int omegaToMassFlowWaterTransfer()
{
    //(30*pi^2*r^2*rho*h*omega_desmax)/(m.air*t_test*(omega_3-omega_4)(m.air*(omega_3-omega_4)+30*pi*r^2*rho*h*omega_3))
	//r, pi, rho, h, omega_desmax, t_test all constants
	//other vars will be collected with state sensors
}
