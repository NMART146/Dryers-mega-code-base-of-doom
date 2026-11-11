#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <AccelStepper.h>
#include <RBDdimmer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>


//******************** PWM Fan Setup ********************//

const int fanAdsorb_PIN = 25;
const int fanRegen_PIN = 27;
const int PWM_FREQ = 25000;    // 25 kHz target frequency
const int fanAdsorb_CHANNEL = 0;     // LEDC channel (0-15)
const int fanRegen_CHANNEL = 1;       // LEDC channel (0-15)
const int PWM_RESOLUTION = 8;  // 8-bit resolution (0-255)

float cfmAdsorb = 0;
float cfmRegen = 0;

// Tachometer Pin Assignments
const int fanAdsorb_TACHO_PIN = 35;
const int fanRegen_TACHO_PIN = 26;

// Volatile pulse counters
volatile unsigned long pulseCountAdsorb = 0;
volatile unsigned long pulseCountRegen = 0;

// Shared lock for critical section
portMUX_TYPE tachoMux = portMUX_INITIALIZER_UNLOCKED;

// Interrupt Service Routine for Fan 1 (Adsorb)
void IRAM_ATTR tachoISR1() {
  portENTER_CRITICAL_ISR(&tachoMux);
  pulseCountAdsorb++;
  portEXIT_CRITICAL_ISR(&tachoMux);
}

// Interrupt Service Routine for Fan 2 (Regen)
void IRAM_ATTR tachoISR2() {
  portENTER_CRITICAL_ISR(&tachoMux);
  pulseCountRegen++;
  portEXIT_CRITICAL_ISR(&tachoMux);
}

void processFanSpeeds() {
  // Atomic copy and reset for both counts simultaneously
  portENTER_CRITICAL(&tachoMux);
  unsigned long count1 = pulseCountAdsorb;
  unsigned long count2 = pulseCountRegen;
  pulseCountAdsorb = 0;
  pulseCountRegen = 0;
  portEXIT_CRITICAL(&tachoMux);

  // Calculate RPM for both fans (count * 60 / 2) for 2 pulses per revolution
  int rpmAdsorb = (int)(count1 * 60 / 2);
  int rpmRegen = (int)(count2 * 60 / 2);

  // Assuming 2200 max RPM and 32.666 max CFM from Noctua's specs for NF-A8 PWM, calculate/estimate CFM for both fans
  cfmAdsorb = (float)(32.666 * rpmAdsorb / 2200);
  cfmRegen = (float)(32.666 * rpmRegen / 2200);

  // Serial.printf("Fan 1: %d RPM | Fan 2: %d RPM\n", rpmAdsorb, rpmRegen);
  // Serial.printf("Fan 1: %d CFM | Fan 2: %d CFM\n", cfmAdsorb, cfmRegen);
}

//*******************************************************//

//************* Sensors Setup and Variables *************//

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
dimmerLamp HeaterDimmer(HEATER_PIN, ZC_PIN);


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

// Control parameters
bool fanOn = true;
bool heaterOn = false;
bool motorOn = false;
float adsorbPower = 30.0;
float regenPower = 30.0;

//*******************************************************//

//********************** WIFI Setup *********************//

// WIFI Setup (setup specific to Mines network)
const char* SSID         = "Mines";
const char* EAP_IDENTITY = "email@mines.edu"; // mines email
const char* EAP_USERNAME = "email@mines.edu"; // mines email
const char* EAP_PASSWORD = "password"; // your Mines password
// IP of the computer running python app
const char* SERVER_URL = "http://10.186.161.208:5000/data";
const unsigned long SEND_INTERVAL_MS = 1000;  // how often to send a sample

HTTPClient http;
unsigned long lastSend = 0;

// Generate random sensor data for network testing purposes
void randomDataTest() {
  sensor1Data[0] = 20.0  + (random(0, 500)  / 100.0);  // Temp: 20.0 - 25.0 °C
  sensor1Data[1] = 980.0 + (random(0, 5000) / 100.0);  // Pressure: 980.0 - 1030.0 hPa
  sensor1Data[2] = 40.0  + (random(0, 1000) / 100.0);  // Humidity: 40.0 - 50.0 %

  sensor2Data[0] = 20.0  + (random(0, 500)  / 100.0);  // Temp: 20.0 - 25.0 °C
  sensor2Data[1] = 980.0 + (random(0, 5000) / 100.0);  // Pressure: 980.0 - 1030.0 hPa
  sensor2Data[2] = 40.0  + (random(0, 1000) / 100.0);  // Humidity: 40.0 - 50.0 %

  sensor3Data[0] = 20.0  + (random(0, 500)  / 100.0);  // Temp: 20.0 - 25.0 °C
  sensor3Data[1] = 980.0 + (random(0, 5000) / 100.0);  // Pressure: 980.0 - 1030.0 hPa
  sensor3Data[2] = 40.0  + (random(0, 1000) / 100.0);  // Humidity: 40.0 - 50.0 %
}

void connectWiFi() {
  Serial.print("Connecting to Wi-Fi");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);
  // PEAP/MSCHAPv2 (the most common 802.1X setup)
  WiFi.begin(SSID, WPA2_AUTH_PEAP, EAP_IDENTITY, EAP_USERNAME, EAP_PASSWORD);
  Serial.print("Connecting");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Connection failed");
  }
}

void sendData();

//*******************************************************//

//******************* Misc. Functions *******************//

// Helper function to switch the active multiplexer channel (0 through 7)
void tcaselect(uint8_t bus) {
  if (bus > 7) return;
  Wire.beginTransmission(multiplexer);
  Wire.write(1 << bus);
  Wire.endTransmission();
}

// Function Declarations
void sensorPull(uint8_t channel, float &temp, float &pressure, float &humidity);
void updateHardware();

void thetaDotToStepsPerSecond(float &theta_dot) {
  // Convert theta_dot (rad/s) to steps per second for the stepper motor
  // Assuming 200 steps per revolution and a gear ratio of 1:1
  float steps_per_revolution = 200.0;
  float steps_per_second = (theta_dot / (2 * PI)) * steps_per_revolution;
  stepper.setSpeed(steps_per_second);
}

//*******************************************************//
//*******************************************************//

//********************* ESP32 Setup *********************//

void setup()
{
  Serial.begin(115200);
  while (!Serial);
  delay(500);
  connectWiFi();
  http.setReuse(true);  // keep the TCP connection open between samples
  delay(500);
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

  // Initialize PWM for fan control
  ledcSetup(fanAdsorb_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcSetup(fanRegen_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(fanAdsorb_PIN, fanAdsorb_CHANNEL);
  ledcAttachPin(fanRegen_PIN, fanRegen_CHANNEL);
  ledcWrite(fanAdsorb_CHANNEL, 255);
  ledcWrite(fanRegen_CHANNEL, 255);

  // Configure Fan 1 (Adsorb) Interrupt
  pinMode(fanAdsorb_TACHO_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(fanAdsorb_TACHO_PIN), tachoISR1, FALLING);

  // Configure Fan 2 (Regen) Interrupt
  pinMode(fanRegen_TACHO_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(fanRegen_TACHO_PIN), tachoISR2, FALLING);

  // Initialize stepper motor
  // Set maximum allowable speed threshold (steps per second)
  stepper.setMaxSpeed(1000);
  // Set constant running speed (steps per second)
  stepper.setSpeed(30);

  // Initialize dimmers
  HeaterDimmer.begin(NORMAL_MODE, ON);
  // Set dimmer level to 100% power
  HeaterDimmer.setPower(0);
}

//*******************************************************//

//******************* ESP32 Main Loop *******************//

// Loop Timing
unsigned long lastLoopTime = 0;
const unsigned long LOOP_INTERVAL_MS = 100;  // 100 ms loop interval
void loop()
{
  unsigned long currentTime = millis();
  if (currentTime - lastLoopTime >= LOOP_INTERVAL_MS) {
    lastLoopTime = currentTime;

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Wi-Fi lost, reconnecting...");
      connectWiFi();
    }
    //sensors pull data
    sensorPull(1, sensor1Data[0], sensor1Data[1], sensor1Data[2]);
    sensorPull(2, sensor2Data[0], sensor2Data[1], sensor2Data[2]);
    sensorPull(3, sensor3Data[0], sensor3Data[1], sensor3Data[2]);

    // randomDataTest();
    sendData();

    float theta_dot = massFlowWaterToThetaDotTransfer(desired_m_water);
    thetaDotToStepsPerSecond(theta_dot); // set stepper speed based on calculated theta_dot
  }

  updateHardware();
}

//*******************************************************//
//*******************************************************//

//********* Sensor, Wifi, and Hardware Functions ********//

void sensorPull(uint8_t channel, float &temp, float &pressure, float &humidity) {
  tcaselect(channel); // Switch multiplexer to target channel

  temp = bme.readTemperature();
  pressure = bme.readPressure();
  humidity = bme.readHumidity();
}

void sendData() {
  if (WiFi.status() == WL_CONNECTED && millis() - lastSend >= SEND_INTERVAL_MS) {
    lastSend = millis();
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/json");

    processFanSpeeds(); // Get the fan RPMs and calculate CFM values

    // Create JSON payload
    String payload = "{";
    payload += "\"cfmAdsorb\": " + String(cfmAdsorb) + ",";
    payload += "\"cfmRegen\": " + String(cfmRegen) + ",";
    payload += "\"sensor1\": {\"temp\": " + String(sensor1Data[0]) + ", \"pressure\": " + String(sensor1Data[1]) + ", \"humidity\": " + String(sensor1Data[2]) + "},";
    payload += "\"sensor2\": {\"temp\": " + String(sensor2Data[0]) + ", \"pressure\": " + String(sensor2Data[1]) + ", \"humidity\": " + String(sensor2Data[2]) + "},";
    payload += "\"sensor3\": {\"temp\": " + String(sensor3Data[0]) + ", \"pressure\": " + String(sensor3Data[1]) + ", \"humidity\": " + String(sensor3Data[2]) + "}";
    payload += "}";

    int httpResponseCode = http.POST(payload);

    if (httpResponseCode > 0) {
        Serial.printf("HTTP Response code: %d\n", httpResponseCode);
        
        String response = http.getString();

        // Allocate JSON document and parse
        JsonDocument doc; 
        DeserializationError error = deserializeJson(doc, response);

        if (!error) {
            bool ok = doc["ok"];
            bool fanOn = doc["fan_on"];
            bool heaterOn = doc["heater_on"];
            bool motorOn = doc["motor_on"];
            float adsorbPower = doc["adsorb_power"];
            float regenPower = doc["regen_power"];

            // Serial.printf("Response: ok=%d, fan_on=%d, heater_on=%d, motor_on=%d\n", ok, fanOn, heaterOn, motorOn);

        } else {
            Serial.printf("JSON parse failed: %s\n", error.c_str());
        }
    } else {
        Serial.printf("Error sending POST: %s\n", http.errorToString(httpResponseCode).c_str());
    }
    http.end();
  }
}

void updateHardware() {
  // Update fan speeds based on received power levels
  adsorbPower = constrain(adsorbPower, 0.0, 100.0);
  regenPower = constrain(regenPower, 0.0, 100.0);
  if (!fanOn) {
    adsorbPower = 0.0;
    regenPower = 0.0;
  }
  ledcWrite(fanAdsorb_CHANNEL, (uint8_t)(adsorbPower / 100.0 * 255));
  ledcWrite(fanRegen_CHANNEL, (uint8_t)(regenPower / 100.0 * 255));

  // Update heater dimmer
  HeaterDimmer.setPower(heaterOn ? 100 : 0);

  // Enable or disable stepper motor
  if (motorOn) {
    stepper.runSpeed();
  } else {
    stepper.stop();
  }
}

//*******************************************************//

//******************* Control Systems *******************//

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

double heaterControl(double T_in)
{
  //h_air = dependent on flow rate of air through fans
  //A_heater = area of heater surface
  static double T_prev = 0.0;
  static double V_prev = 0.0;

  double Ts = 0.1; //sampling time

  //rise time is 20 sec, settle time is 57 sec, overshoot is 5%
  double zeta = 0.7;
  double omega_n = 0.11;
  double kp = 8.8/(2.2/omega_n)**2-1/(h_air*A_heater);
  double ki = omega_n**2;

  double C = (1.0 / (h_air * A_heater)) + kp;
  double D = 8.0 * C + 4.0 * ki * Ts;

  double b0 = (2.0 * kp + ki * Ts) / D;
  double b1 = (ki * Ts - 2.0 * kp) / D;
  double a1 = (4.0 * ki * Ts - 8.0 * C) / D;

  double V_out = b0 * T_in + b1 * T_prev - a1 * V_prev;

  T_prev = T_in;
  V_prev = V_out;

  return V_out;

}