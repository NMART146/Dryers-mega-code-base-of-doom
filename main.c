#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define BME1_SCK 13 //Update pins
#define BME1_MISO 12 //Update pins
#define BME1_MOSI 11 //Update pins
#define BME1_CS 10 //Update pins

Adafruit_BME280 bme1;

int desired_water_flowrate //can be calculated from our desired amount per day
int[3] sensor1Data = [0,0,0]; //temp, pressure, humidity
int[3] sensor2Data = [0,0,0]; //temp, pressure, humidity
int[3] sensor3Data = [0,0,0]; //temp, pressure, humidity

void setup()
{
  Serial.begin(115200);
  bme1.begin(BME1_CS,BME1_MOSI,BME1_MISO,BME1_SCK); //2-3 more of these depending on sensor count
  bme2.begin(BME2_CS,BME2_MOSI,BME2_MISO,BME2_SCK);
  bme3.begin(BME3_CS,BME3_MOSI,BME3_MISO,BME3_SCK);
}

void loop()
{
    //sensors pull data
    sensorPull(1, &sensor1Data[0], &sensor1Data[1], &sensor1Data[2]);
    sensorPull(2, &sensor2Data[0], &sensor2Data[1], &sensor2Data[2]);
    sensorPull(3, &sensor3Data[0], &sensor3Data[1], &sensor3Data[2]);
}

void sensorPull(int pin, float temp, float pressure, float humidity)
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

float massFlowWaterToOmegaTransfer(int desired_flow)
{
    //(30*pi^2*r^2*rho*h*omega_desmax)/(m.air*t_test*(omega_3-omega_4)(m.air*(omega_3-omega_4)+30*pi*r^2*rho*h*omega_3))
	//r, pi, rho, h, omega_desmax, t_test all constants
	//other vars will be collected with state sensors
}
