#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define BME_SCK 13 //Update pins
#define BME_MISO 12 //Update pins
#define BME_MOSI 11 //Update pins
#define BME_CS 10 //Update pins

Adafruit_BME280 bme1;

int desired_water_flowrate //can be calculated from our desired amount per day

void setup()
{
  Serial.begin(115200);
  bme1.begin(BME_CS,BME_MOSI,BME_MISO,BME_SCK); //2-3 more of these depending on sensor count
}

void loop()
{
    //sensors pull data

}

float sensorPull(int pin, float temp, float pressure, float humidity)
{
    switch(pin):
    {
        case 1:
            temp = bme1.readTemperature(); //celsius bih
            pressure = bme1.readPressure(); //pascals
            humidity = bme1.readHumidity(); //percentage
        
        case 2:
            temp = bme2.readTemperature(); //celsius bih
            pressure = bme2.readPressure(); //pascals
            humidity = bme2.readHumidity(); //percentage

        case 3:
            temp = bme3.readTemperature(); //celsius bih
            pressure = bme3.readPressure(); //pascals
            humidity = bme3.readHumidity(); //percentage
    }
    
}

int massFlowWaterToOmegaTransfer(int desired_flow)
{
    //(30*pi^2*r^2*rho*h*omega_desmax)/(m.air*t_test*(omega_3-omega_4)(m.air*(omega_3-omega_4)+30*pi*r^2*rho*h*omega_3))
	//r, pi, rho, h, omega_desmax, t_test all constants
	//other vars will be collected with state sensors
}
