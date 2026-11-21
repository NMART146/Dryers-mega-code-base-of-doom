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

void processFanSpeeds(unsigned long dt_ms) {
  portENTER_CRITICAL(&tachoMux);
  unsigned long count1 = pulseCountAdsorb;
  unsigned long count2 = pulseCountRegen;
  pulseCountAdsorb = 0;
  pulseCountRegen = 0;
  portEXIT_CRITICAL(&tachoMux);

  // pulses / (2 pulses per rev) / (dt in minutes)
  int rpmAdsorb = (int)(count1 * 60000UL / (2UL * dt_ms));
  int rpmRegen  = (int)(count2 * 60000UL / (2UL * dt_ms));

  cfmAdsorb = 32.666f * rpmAdsorb / 2200.0f;
  cfmRegen  = 32.666f * rpmRegen  / 2200.0f;
}

//*******************************************************//

//************* Sensors Setup and Variables *************//

// Initialize I2C multiplexer and BME280 sensor
#define multiplexer 0x70 // I2C address for multiplexer
Adafruit_BME280 bme[3];

// Stepper pins
#define stepPin 33
#define dirPin 32
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
struct Telemetry {
  float cfmAdsorb, cfmRegen;
  float s[3][3];            // [sensor][temp, pressure, humidity]
};
struct Commands {
  bool fanOn, heaterOn, motorOn;
  float adsorbPower, regenPower;
};

Telemetry telem = {};
Commands  cmd   = {true, false, false, 30.0f, 30.0f};
SemaphoreHandle_t dataMutex;

Commands localCmd = {true, false, false, 30.0f, 30.0f};   // add
float heaterPct = 0;

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

void networkTask(void *);

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
  // Guard against NaN or negative angular velocities
  if (isnan(theta_dot) || theta_dot < 0.0f) {
    stepper.setSpeed(0.0f); // Safely halt the motor instead of holding the previous speed
    return;
  }

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
  dataMutex = xSemaphoreCreateMutex();
  delay(500);
  Wire.begin();

  // Initialize sensors on multiplexer channels 0, 1, and 2
  for (uint8_t channel = 0; channel < 3; channel++) {
    tcaselect(channel);
    if (!bme[channel].begin(0x76, &Wire)) {
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

  xTaskCreatePinnedToCore(networkTask, "net", 8192, NULL, 1, NULL, 0);
}

//*******************************************************//

//******************* ESP32 Main Loop *******************//

// Loop Timing
unsigned long lastLoopTime = millis();
const unsigned long LOOP_INTERVAL_MS = 100;  // 100 ms loop interval
void loop() {
  unsigned long now = millis();
  if (now - lastLoopTime >= LOOP_INTERVAL_MS) {
    lastLoopTime += LOOP_INTERVAL_MS;      // += avoids drift (not = now)

    sensorPull(0, sensor1Data[0], sensor1Data[1], sensor1Data[2]);
    sensorPull(1, sensor2Data[0], sensor2Data[1], sensor2Data[2]);
    sensorPull(2, sensor3Data[0], sensor3Data[1], sensor3Data[2]);

    float theta_dot = massFlowWaterToThetaDotTransfer(desired_m_water);
    thetaDotToStepsPerSecond(theta_dot);

    // Publish telemetry
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    localCmd = cmd;
    memcpy(telem.s[0], sensor1Data, sizeof(sensor1Data));
    memcpy(telem.s[1], sensor2Data, sizeof(sensor2Data));
    memcpy(telem.s[2], sensor3Data, sizeof(sensor3Data));
    telem.cfmAdsorb = cfmAdsorb;
    telem.cfmRegen  = cfmRegen;
    xSemaphoreGive(dataMutex);

    heaterPct = localCmd.heaterOn
      ? constrain(heaterControl(sensor1Data[0]) * 100.0, 0.0, 100.0)
      : 0.0;
  }


  static unsigned long lastTach = millis();
  unsigned long dt = millis() - lastTach;
  if (dt >= 1000) {
    processFanSpeeds(dt);
    lastTach = millis();
  }

  updateHardware();    // reads a local copy of cmd, then does the same as before
}

//*******************************************************//
//*******************************************************//

//********* Sensor, Wifi, and Hardware Functions ********//

void sensorPull(uint8_t channel, float &temp, float &pressure, float &humidity) {
  tcaselect(channel); // Switch multiplexer to target channel

  temp = bme[channel].readTemperature();
  pressure = bme[channel].readPressure();
  humidity = bme[channel].readHumidity();
}

void networkTask(void *) {
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();                       // blocking is fine here, control loop is unaffected
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    // Snapshot telemetry
    Telemetry t;
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    t = telem;
    xSemaphoreGive(dataMutex);

    char payload[400];
    snprintf(payload, sizeof(payload),
      "{\"cfmAdsorb\":%.2f,\"cfmRegen\":%.2f,"
      "\"sensor1\":{\"temp\":%.2f,\"pressure\":%.2f,\"humidity\":%.2f},"
      "\"sensor2\":{\"temp\":%.2f,\"pressure\":%.2f,\"humidity\":%.2f},"
      "\"sensor3\":{\"temp\":%.2f,\"pressure\":%.2f,\"humidity\":%.2f}}",
      t.cfmAdsorb, t.cfmRegen,
      t.s[0][0], t.s[0][1], t.s[0][2],
      t.s[1][0], t.s[1][1], t.s[1][2],
      t.s[2][0], t.s[2][1], t.s[2][2]);

    http.begin(SERVER_URL);
    http.setTimeout(2000);                 // don't hang forever on a dead server
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(payload);

    if (code > 0) {
      JsonDocument doc;
      if (!deserializeJson(doc, http.getString())) {
        xSemaphoreTake(dataMutex, portMAX_DELAY);
        cmd.fanOn       = doc["fan_on"]       | cmd.fanOn;
        cmd.heaterOn    = doc["heater_on"]    | cmd.heaterOn;
        cmd.motorOn     = doc["motor_on"]     | cmd.motorOn;
        cmd.adsorbPower = doc["adsorb_power"] | cmd.adsorbPower;
        cmd.regenPower  = doc["regen_power"]  | cmd.regenPower;
        xSemaphoreGive(dataMutex);
      }
    } else {
      Serial.printf("POST error: %s\n", http.errorToString(code).c_str());
    }
    http.end();

    vTaskDelay(pdMS_TO_TICKS(SEND_INTERVAL_MS));
  }
}

void updateHardware() {
  Commands c;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  c = cmd;
  xSemaphoreGive(dataMutex);

  float adsorb = c.fanOn ? constrain(c.adsorbPower, 0.0f, 100.0f) : 0.0f;
  float regen  = c.fanOn ? constrain(c.regenPower,  0.0f, 100.0f) : 0.0f;
  ledcWrite(fanAdsorb_CHANNEL, (uint8_t)(adsorb / 100.0f * 255));
  ledcWrite(fanRegen_CHANNEL,  (uint8_t)(regen  / 100.0f * 255));

  HeaterDimmer.setPower((int)(heaterPct));

  if (c.motorOn) {
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

  double Ts = LOOP_INTERVAL_MS/1000.0; //sampling time

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

  return V_out/110;

}