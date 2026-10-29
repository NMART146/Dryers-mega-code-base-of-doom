#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <AccelStepper.h>
#include <RBDdimmer.h>

// Initialize I2C multiplexer and BME280 sensor
#define multiplexer 0x70 // I2C address for multiplexer
Adafruit_BME280 bme;

// Stepper pins
#define stepPin 33
#define dirPin 32
#define enablePin 25
#define motorInterfaceType 1
AccelStepper stepper(motorInterfaceType, stepPin, dirPin);

// Dimmer pins
#define ZC_PIN 13
#define HEATER_PIN 14
#define FAN1_PIN 27
#define FAN2_PIN 26
dimmerLamp HeaterDimmer(HEATER_PIN, ZC_PIN);
dimmerLamp Fan1Dimmer(FAN1_PIN, ZC_PIN);
dimmerLamp Fan2Dimmer(FAN2_PIN, ZC_PIN);


// Storage arrays: [temp, pressure, humidity]
float sensor1Data[3] = {0.0, 0.0, 0.0};
float sensor2Data[3] = {0.0, 0.0, 0.0};
float sensor3Data[3] = {0.0, 0.0, 0.0};

// Constants for wheel speed func
float omega_dot = 3.16; //rate of desiccant adsorption (g/(m^2*s))
float r = 0.2; //radius of wheel (0.2m)
float rho = 983200; //density of water @60c (g/m^3)
float h = 31125; //fabric mass enthalpy

float desired_m_water = 0.081; //desired water mass flow rate (g/s); equiv to 7L/day


// Helper function to switch the active multiplexer channel (0 through 7)
void tcaselect(uint8_t bus) {
  if (bus > 7) return;
  Wire.beginTransmission(multiplexer);
  Wire.write(1 << bus);
  Wire.endTransmission();
}

// Function Prototype
void sensorPull(uint8_t channel, float &temp, float &pressure, float &humidity);

void thetaDotToStepsPerSecond(float &theta_dot) {
  // Convert theta_dot (rad/s) to steps per second for the stepper motor
  // Assuming 200 steps per revolution and a gear ratio of 1:1
  float steps_per_revolution = 200.0;
  float steps_per_second = (theta_dot / (2 * PI)) * steps_per_revolution;
  stepper.setSpeed(steps_per_second);
}

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

  // Initialize stepper motor
  // Set maximum allowable speed threshold (steps per second)
  stepper.setMaxSpeed(1000);
  // Set constant running speed (steps per second)
  stepper.setSpeed(30);

  // Initialize dimmers
  HeaterDimmer.begin(NORMAL_MODE, ON);
  Fan1Dimmer.begin(NORMAL_MODE, ON);
  Fan2Dimmer.begin(NORMAL_MODE, ON);
  // Set dimmer levels to 100% power
  HeaterDimmer.setPower(100);
  Fan1Dimmer.setPower(100);
  Fan2Dimmer.setPower(100);
}

void loop()
{
    //sensors pull data
    sensorPull(1, sensor1Data[0], sensor1Data[1], sensor1Data[2]);
    sensorPull(2, sensor2Data[0], sensor2Data[1], sensor2Data[2]);
    sensorPull(3, sensor3Data[0], sensor3Data[1], sensor3Data[2]);

    thetaDotToStepsPerSecond(massFlowWaterToThetaDotTransfer(desired_m_water)); //assign to a var

    // Runs the motor continuously at the constant speed set by setSpeed()
    stepper.runSpeed();

    delay(100);
}

void sensorPull(uint8_t channel, float &temp, float &pressure, float &humidity) {
  tcaselect(channel); // Switch multiplexer to target channel

  temp = bme.readTemperature();
  pressure = bme.readPressure();
  humidity = bme.readHumidity();
}

float massFlowWaterToThetaDotTransfer(float desired_flow)
{
  //iteratively adjust enthalpy value to find true value. if water flow rate is too low, lower enthalpy. too high, raise.
  //(30*pi^2*r^2*rho*h*omega_desmax)/(m.air*t_test*(omega_3-omega_4)(m.air*(omega_3-omega_4)+30*pi*r^2*rho*h*omega_3))
	//r, pi, rho, h, omega_desmax, t_test all constants
	//other vars will be collected with state sensors

  
  float omega_3 = sensor3Data[2]; //humidity at state 3 (updated every cycle)
  float omega_4 = sensor4Data[2]; //humidity at state 4 (updated every cycle)
  float theta_dot = (30 * (omega_3 - omega_4) * PI * PI * pow(r,2)*rho*h*omega_dot)/
  ((omega_3-omega_4)*(m_air*(omega_3-omega_4)+30*PI*pow(r,2)*rho*h*omega_3));
  
  if(m_air * (omega_3 - omega_4) > desired_m_water)
  {
    h += 1;
  }
  else
  {
    h -= 1;
  }
  return theta_dot;
}

float heaterControl()
{
  //h_air = dependent on flow rate of air through fans
  //A_heater = area of heater surface

  //rise time is 20 sec, settle time is 57 sec, overshoot is 5%
  float zeta = 0.7;
  float omega_n = 0.11;
  float kp = 8.8/(2.2/omega_n)**2-1/(h_air*A_heater);
  float ki = omega_n**2;

  float TF = 
}