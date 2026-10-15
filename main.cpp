#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define multiplexer 0x70 // I2C address for multiplexer

Adafruit_BME280 bme;

// Storage arrays: [temp, pressure, humidity]
float sensor1Data[3] = {0.0, 0.0, 0.0};
float sensor2Data[3] = {0.0, 0.0, 0.0};
float sensor3Data[3] = {0.0, 0.0, 0.0};

// Constants for wheel speed func
float omega_dot = 3.16; //rate of desiccant adsorption (g/(m^2*s))
float r = 0.2; //radius of wheel (0.2m)
float rho = 983200; //density of water @60c (g/m^3)
float h = 31125; //fabric mass enthalpy



// Helper function to switch the active multiplexer channel (0 through 7)
void tcaselect(uint8_t bus) {
  if (bus > 7) return;
  Wire.beginTransmission(multiplexer);
  Wire.write(1 << bus);
  Wire.endTransmission();
}

// Function Prototype
void sensorPull(uint8_t channel, float &temp, float &pressure, float &humidity);

void setup()
{
  Serial.begin(115200);
  while (!Serial);
  Wire.begin();

  // Initialize sensors on multiplexer channels 0, 1, and 2
  for (uint8_t channel = 0; channel < 3; channel++) {
    tcaselect(channel);
    if (!bme.begin(0x76, &Wire)) { 
      Serial.printf("BME280 not found on channel %d\n", channel);
    } else {
      Serial.printf("BME280 initialized on channel %d\n", channel);
    }
  }
}

void loop()
{
    //sensors pull data
    sensorPull(1, sensor1Data[0], sensor1Data[1], sensor1Data[2]);
    sensorPull(2, sensor2Data[0], sensor2Data[1], sensor2Data[2]);
    sensorPull(3, sensor3Data[0], sensor3Data[1], sensor3Data[2]);

    delay(100);
}

void sensorPull(uint8_t channel, float &temp, float &pressure, float &humidity) {
  tcaselect(channel); // Switch multiplexer to target channel

  temp = bme.readTemperature();
  pressure = bme.readPressure();
  humidity = bme.readHumidity();
}

float massFlowWaterToOmegaTransfer(float desired_flow)
{
  //iteratively adjust enthalpy value to find true value. if water flow rate is too low, lower enthalpy. too high, raise.
  //(30*pi^2*r^2*rho*h*omega_desmax)/(m.air*t_test*(omega_3-omega_4)(m.air*(omega_3-omega_4)+30*pi*r^2*rho*h*omega_3))
	//r, pi, rho, h, omega_desmax, t_test all constants
	//other vars will be collected with state sensors
  float omega_3 = //humidity at state 3 (update every cycle)
  float omega_4 = //humidity at state 4 (update every cycle)
  float omega = (30 * PI * PI * pow(r,2)*rho*h*omega_dot)/(m_air*(omega_3-omega_4)*(m_air*(omega_3-omega_4)+30*PI*pow(r,2)*rho*h*omega_3));
  return omega;
}