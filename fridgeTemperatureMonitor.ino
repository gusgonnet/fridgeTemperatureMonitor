PRODUCT_ID(1274);
PRODUCT_VERSION(10);

#include <math.h>
#include <application.h>
#include "blynkAuthToken.h"

/*******************************************************************************
 These INCLUDE statements work when building in https://build.particle.io/build
 Their usage depends on where you are building this software
*******************************************************************************/
// #include "elapsedMillis/elapsedMillis.h"
// #include "blynk/blynk.h"
/*******************************************************************************
 These INCLUDE statements work when building locally
 Their usage depends on where you are building this software
*******************************************************************************/
#include "elapsedMillis.h"
#include "blynk.h"
#include "localData.h"

#define APP_NAME "TemperatureMonitor"
const String VERSION = "Version 10";

/*******************************************************************************
 * changes in version 0.01:
       * initial version, 4 sensors are being monitored
       * blynk app: http://tinyurl.com/z8osw3z
 * changes in version 0.02:
       * added threshold alarm for sensor1
 * changes in version 0.03:
      * refactoring variables into arrays to simplify code
      * renaming the app to TemperatureMonitor (it's a bit more generic)
      * if thresholds are set to 0 that means they are deactivated
      * added an alarm led in the blynk app
 * changes in version 0.04:
      * alarms get reset when temperature is below threshold
      * added new version of the blynk app
 * changes in version 0.05:
      * notifications of alarms via the blynk app
      * set thresholds from the blynk app
 * changes in version 0.06:
      * store thresholds for alarms in eeprom
 * changes in version 0.07:
      * adding blynk email notifications
        (set email in blynkAuthToken.h: #define EMAIL_ADDRESS "example_email@gmail.com")
      * changed sensors display index to a more user friendly format
        example: sensor0 is displayed to user as "sensor 1" (in the blynk app, the notifications and emails)
 * changes in version 0.08:
      * adding the Calibration tab on the blynk app to calibrate sensors reading
      * stepped up EEPROM_VERSION to 138
      * Calibration settings are stored in eeprom
FROM NOW ON WE FOLLOW THE PARTICLE's VERSION
 * changes in version 10:
      * adding ubidots support for temperature, thresholds and alarms

*******************************************************************************/

/*******************************************************************************
 Sensors
*******************************************************************************/
//defines the maximum number of sensors in case one day we add more
// sensor0, sensor1, sensor2...
#define MAX_NUMBER_OF_SENSORS 4

//the plan is to read one after the other and this is an index that will help to keep track
// of which sensor we need to read next
// IMPORTANT: index starts at 0
int sensorToRead = 0;

//defines how often the measurements are made (millisecs)
#define SENSOR_SAMPLE_INTERVAL 5000
elapsedMillis sensorSampleInterval;

//array of Strings to store the sensors' readings so it can be exposed in the Particle Cloud
String sensorReadingString[MAX_NUMBER_OF_SENSORS];
float sensorReadingFloat[MAX_NUMBER_OF_SENSORS];

/*******************************************************************************
 IO mapping
*******************************************************************************/
// A0 : thermistor 0
// A1 : thermistor 1
// A2 : thermistor 2
// A3 : thermistor 3
// and so on...
const int INPUT_SENSOR[8] = {A0, A1, A2, A3, A4, A5, A6, A7};

/*******************************************************************************
 alarms variables for each sensor
*******************************************************************************/
bool alarmSensor[8] = {false, false, false, false, false, false, false, false};

elapsedMillis alarmSensor_timer[8];

int alarmSensor_index[8] = {0, 0, 0, 0, 0, 0, 0, 0};

unsigned long alarmSensor_next_alarm[8] = {0, 0, 0, 0, 0, 0, 0, 0};

/*******************************************************************************
 thresholds for each sensor
*******************************************************************************/
float sensorThreshold[MAX_NUMBER_OF_SENSORS] = {0, 0, 0, 0};

/*******************************************************************************
 calibration for each sensor
*******************************************************************************/
float calibration[MAX_NUMBER_OF_SENSORS] = {0, 0, 0, 0};

/*******************************************************************************
 structure for writing thresholds in eeprom
 https://docs.particle.io/reference/firmware/photon/#eeprom
*******************************************************************************/
//randomly chosen value here. The only thing that matters is that it's not 255
// since 255 is the default value for uninitialized eeprom
#define EEPROM_VERSION 138
#define EEPROM_VERSION_137 137
#define EEPROM_ADDRESS 0

struct EepromMemoryStructure
{
  uint8_t version = EEPROM_VERSION;
  float sensorThresholdInEeprom[MAX_NUMBER_OF_SENSORS];
  float calibrationInEeprom[MAX_NUMBER_OF_SENSORS];
};
EepromMemoryStructure eepromMemory;

bool settingsHaveChanged = false;
elapsedMillis settingsHaveChanged_timer;
#define SAVE_SETTINGS_INTERVAL 10000

/*******************************************************************************
 Other variables
*******************************************************************************/
//this variable is used for publishing a message that will be detected by a
// webhook running in the particle cloud
#define PUSHBULLET_NOTIF "pushbullet"

//TODO: this can be configurable in the future
const int TIME_ZONE = -4;

//by default, we'll display the temperature in Fahrenheit
// but if you prefer Celsius please set this constant to false
bool useFahrenheit = true;

/*******************************************************************************
  sensor used: 10K Precision Epoxy Thermistor - 3950 NTC
  https://www.adafruit.com/products/372
*******************************************************************************/
//resistance at 77 Fahrenheit or 25 degrees Celsius
#define THERMISTORNOMINAL 10000
//temp. for nominal resistance (almost always 25 C)
#define TEMPERATURENOMINAL 25
//how many samples to take and average, more samples take longer
// but measurement is 'smoother'
#define NUMSAMPLES 5
//The beta coefficient of the thermistor (usually 3000-4000)
#define BCOEFFICIENT 3950
//the value of the other resistor in series
#define SERIESRESISTOR 10000
/******************************************************************************/

/*******************************************************************************
 Here you decide if you want to use Blynk or not
 Your blynk token goes in another file to avoid sharing it by mistake
 The file containing your blynk auth token has to be named blynkAuthToken.h and it should contain
 something like this:
  #define BLYNK_AUTH_TOKEN "1234567890123456789012345678901234567890"
 replace with your project auth token (the blynk app will give you one)
*******************************************************************************/
#define USE_BLYNK "yes"
char auth[] = BLYNK_AUTH_TOKEN;

//definitions for the blynk interface
//main tab widgets
#define BLYNK_DISPLAY_SENSOR0 V0
#define BLYNK_DISPLAY_SENSOR1 V1
#define BLYNK_DISPLAY_SENSOR2 V2
#define BLYNK_DISPLAY_SENSOR3 V3
#define BLYNK_LED_ALARM_SENSOR0 V20
#define BLYNK_LED_ALARM_SENSOR1 V21
#define BLYNK_LED_ALARM_SENSOR2 V22
#define BLYNK_LED_ALARM_SENSOR3 V23

//settings tab widgets
#define BLYNK_DISPLAY_MAX_THRESHOLD0 V10
#define BLYNK_DISPLAY_MAX_THRESHOLD1 V11
#define BLYNK_DISPLAY_MAX_THRESHOLD2 V12
#define BLYNK_DISPLAY_MAX_THRESHOLD3 V13

//calibration tab widgets
#define BLYNK_DISPLAY_SENSOR0b V4
#define BLYNK_DISPLAY_SENSOR1b V5
#define BLYNK_DISPLAY_SENSOR2b V6
#define BLYNK_DISPLAY_SENSOR3b V7
#define BLYNK_DISPLAY_CALIBRATE0 V14
#define BLYNK_DISPLAY_CALIBRATE1 V15
#define BLYNK_DISPLAY_CALIBRATE2 V16
#define BLYNK_DISPLAY_CALIBRATE3 V17

#define BLYNK_DISPLAY_SENSORS      \
  {                                \
    V0, V1, V2, V3, V4, V5, V6, V7 \
  }
#define BLYNK_DISPLAY_MAX_THRESHOLDS       \
  {                                        \
    V10, V11, V12, V13, V14, V15, V16, V17 \
  }

//blynk leds - source: http://docs.blynk.cc/#widgets-displays-led
WidgetLED blynkAlarmSensor0(BLYNK_LED_ALARM_SENSOR0); //register led to virtual pin V20
WidgetLED blynkAlarmSensor1(BLYNK_LED_ALARM_SENSOR1); //register led to virtual pin V21
WidgetLED blynkAlarmSensor2(BLYNK_LED_ALARM_SENSOR2); //register led to virtual pin V22
WidgetLED blynkAlarmSensor3(BLYNK_LED_ALARM_SENSOR3); //register led to virtual pin V23

//this defines how often the readings are sent to the blynk cloud (millisecs)
#define BLYNK_STORE_INTERVAL 5000
elapsedMillis blynkStoreInterval;

/*******************************************************************************
 here we define the frequency of the alarms sent to the user
*******************************************************************************/
#define FIRST_ALARM 10000    //10 seconds
#define SECOND_ALARM 60000   //1 minute
#define THIRD_ALARM 300000   //5 minutes
#define FOURTH_ALARM 900000  //15 minutes
#define FIFTH_ALARM 3600000  //1 hour
#define SIXTH_ALARM 14400000 //4 hours - and every 4 hours ever after, until the situation is rectified (ie no more water is detected)
int alarms_array[6] = {FIRST_ALARM, SECOND_ALARM, THIRD_ALARM, FOURTH_ALARM, FIFTH_ALARM, SIXTH_ALARM};

/*******************************************************************************
 ubidots variables

 webhook definition:
  Event name: ubidots
  url: https://things.ubidots.com/api/v1.6/devices/{{ubi-dsl-vl}}/values/?token={{ubi-token}}
  Request type: POST
  Device: Any
  Advanced settings:
  Send custom data: JSON  
  and then enter:
    {
    "value": "{{ubi-value}}"
    }
  include default data: no
  enforce ssl: yes
*******************************************************************************/
// This value comes from ubidots
const String ubidotsToken = UBIDOTS_TOKEN;

//defines how often the measurements are sent to the cloud
#define CLOUD_PUBLISH_INTERVAL 900000
elapsedMillis cloudPublish_timer;

/*******************************************************************************
 * Function Name  : setup
 * Description    : this function runs once at system boot
 *******************************************************************************/
void setup()
{

  //publish startup message with firmware version
  Particle.publish(APP_NAME, VERSION, 60, PRIVATE);

  //declare and init all analog pins as inputs
  uint8_t i;
  for (i = 0; i < 8; i++)
  {
    pinMode(INPUT_SENSOR[i], INPUT);
  }

  //declare cloud variables
  //https://docs.particle.io/reference/firmware/photon/#particle-variable-
  //Currently, up to 10 cloud variables may be defined and each variable name is limited to a maximum of 12 characters
  if (Particle.variable("sensor0", sensorReadingString[0]) == false)
  {
    Particle.publish(APP_NAME, "ERROR: Failed to register variable sensor0", 60, PRIVATE);
  }
  if (Particle.variable("sensor1", sensorReadingString[1]) == false)
  {
    Particle.publish(APP_NAME, "ERROR: Failed to register variable sensor1", 60, PRIVATE);
  }
  if (Particle.variable("sensor2", sensorReadingString[2]) == false)
  {
    Particle.publish(APP_NAME, "ERROR: Failed to register variable sensor2", 60, PRIVATE);
  }
  if (Particle.variable("sensor3", sensorReadingString[3]) == false)
  {
    Particle.publish(APP_NAME, "ERROR: Failed to register variable sensor3", 60, PRIVATE);
  }

  //init Blynk
  if (USE_BLYNK == "yes")
  {
    Blynk.begin(auth);
  }

  Time.zone(TIME_ZONE);

  //restore settings (thresholds) from eeprom, if there were any saved before
  readFromEeprom();
}

/*******************************************************************************
 * Function Name  : loop
 * Description    : this function runs continuously while the project is running
 *******************************************************************************/
void loop()
{

  if (USE_BLYNK == "yes")
  {
    Blynk.run();
  }

  //is it time to read a sensor? if so, do it
  if (sensorSampleInterval > SENSOR_SAMPLE_INTERVAL)
  {

    //reset timer
    sensorSampleInterval = 0;

    //read the sensor
    float sensorMeasurement = readSensor(sensorToRead);

    //store the sensor reading in the internal array of floats
    sensorReadingFloat[sensorToRead] = sensorMeasurement;
    //store the sensor reading in the array of exposed variables in the Particle Cloud
    sensorReadingString[sensorToRead] = userFriendlyTemperature(getCalibratedSensorReading(sensorToRead));

    //publish the reading of the sensor
    publishSensorReading(sensorToRead);

    //verify the reading has not exceeded the threshold
    if (thresholdExceeded(sensorToRead, getCalibratedSensorReading(sensorToRead)))
    {
      setAlarmForSensor(sensorToRead);
      sendAlarmToUser(sensorToRead);
    }
    else
    {
      resetAlarmForSensor(sensorToRead);
    }

    //update blynk leds for alarms if there is a need
    updateBlynkLEDs();

    //debug - by moving the slider in the blynk app I know the blynk app is really connected
    // to the photon if this variable shows the updated value
    Particle.publish("DEBUG: threshold " + userFriendlySensor(sensorToRead) + " is: ", String(sensorThreshold[sensorToRead]), 60, PRIVATE);

    //increment the sensor to read
    sensorToRead = sensorToRead + 1;
    if (sensorToRead == MAX_NUMBER_OF_SENSORS)
    {
      sensorToRead = 0;
    }
  }

  //publish readings to the blynk server every minute so the History Graph gets updated
  // even when the blynk app is not on (running) in the users phone
  updateBlynkCloud();

  //every now and then we save the settings of the thresholds (only if they were changed)
  saveSettings();

  // publish readings to the ubidots cloud
  publishTemperatureToUbidots();
}

/*******************************************************************************
 * Function Name  : readSensor
 * Description    : read the value of the thermistor, convert it and
                    store it in a public variable exposed in the cloud
 * Return         : the reading of the sensor in float
 *******************************************************************************/
float readSensor(int sensorIndex)
{

  //store the average of the readings here
  float average;

  //array for storing samples
  int samples[NUMSAMPLES];

  //take N samples in a row, with a slight delay
  uint8_t i;
  for (i = 0; i < NUMSAMPLES; i++)
  {
    samples[i] = readTheAnalogInput(sensorIndex);
    delay(10);
  }

  //average all the samples
  average = 0;
  for (i = 0; i < NUMSAMPLES; i++)
  {
    average += samples[i];
  }
  average /= NUMSAMPLES;

  //convert the value to resistance
  average = (4095 / average) - 1;
  average = SERIESRESISTOR / average;

  float steinhart;
  steinhart = average / THERMISTORNOMINAL;          // (R/Ro)
  steinhart = log(steinhart);                       // ln(R/Ro)
  steinhart /= BCOEFFICIENT;                        // 1/B * ln(R/Ro)
  steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
  steinhart = 1.0 / steinhart;                      // Invert
  steinhart -= 273.15;                              // convert to C

  //Convert Celsius to Fahrenheit
  if (useFahrenheit)
  {
    steinhart = (steinhart * 9.0) / 5.0 + 32.0;
  }

  return steinhart;
}

/*******************************************************************************
 * Function Name  : publishSensorReading
 * Description    : the temperature of the sensor gets published to the Particle Cloud
                      (with a call to Particle.publish)
 * Return         : none
 *******************************************************************************/
void publishSensorReading(int sensorIndex)
{

  String tempToBePublished = "Sensor " + userFriendlySensor(sensorIndex) + ": ";
  tempToBePublished = tempToBePublished + userFriendlyTemperature(getCalibratedSensorReading(sensorIndex));
  tempToBePublished = tempToBePublished + getTemperatureUnit();

  //publish reading in the console logs of the dashboard at https://dashboard.particle.io/user/logs
  Particle.publish(APP_NAME, tempToBePublished, 60, PRIVATE);
}

/*******************************************************************************
 * Function Name  : getCalibratedSensorReading
 * Description    : this function returns the calibrated value of a sensor
 * Return         : the reading calibrated
 *******************************************************************************/
float getCalibratedSensorReading(int sensorIndex)
{
  //calibrate reading from sensor
  return sensorReadingFloat[sensorIndex] + (calibration[sensorIndex] / 1000);
}

/*******************************************************************************
 * Function Name  : readTheAnalogInput
 * Description    : read the value of the input
 * Return         : the reading of the input
 *******************************************************************************/
int readTheAnalogInput(int sensorIndex)
{
  return analogRead(INPUT_SENSOR[sensorIndex]);
}

/*******************************************************************************
 * Function Name  : getTemperatureUnit
 * Description    : return "°F" or "°C" according to useFahrenheit
 * Return         : String
 *******************************************************************************/
String getTemperatureUnit()
{
  if (useFahrenheit)
  {
    return "°F";
  }
  else
  {
    return "°C";
  }
}

/*******************************************************************************
 * Function Name  : userFriendlySensor
 * Description    : return sensorIndex+1 so the messsages are more user friendly
                    Example: sensor0 returns 1, so messages to user show
                    "Sensor 1 exceeded the threshold"
 * Return         : String
 *******************************************************************************/
String userFriendlySensor(int sensorIndex)
{
  return String(sensorIndex + 1);
}

/*******************************************************************************
 * Function Name  : userFriendlyTemperature
 * Description    : returns the temperature in string with 2 decimals
 * Return         : String
 *******************************************************************************/
String userFriendlyTemperature(float temperature)
{

  //init vars
  char currentTempChar[32];
  int currentTempDecimals = (temperature - (int)temperature) * 100;

  //this is a fix required for displaying properly negative temperatures
  // without the abs(), the project will display something like "-4.-87"
  // with the abs(), the project will display "-4.87"
  currentTempDecimals = abs(currentTempDecimals);

  //this converts the temperature into a more user friendly format with 2 decimals
  sprintf(currentTempChar, "%0d.%d", (int)temperature, currentTempDecimals);

  return String(currentTempChar);
}

/*******************************************************************************
 * Function Name  : thresholdExceeded
 * Description    : this function verifies that the temperature passed as parameter
                    does not exceed the threshold assigned for the sensor
 * Return         : false if threshold has not been exceeded
                    true if threshold has been exceeded
 *******************************************************************************/
bool thresholdExceeded(int sensorIndex, float temperature)
{

  if (sensorThreshold[sensorIndex] and (temperature > sensorThreshold[sensorIndex]))
  {
    return true;
  }

  return false;
}

/*******************************************************************************
 * Function Name  : setAlarmForSensor
 * Description    : this function sets the alarm for a sensor
 * Return         : none
 *******************************************************************************/
void setAlarmForSensor(int sensorIndex)
{

  //if the alarm is already set for the sensor, no need to do anything, since a notification is being fired
  if (alarmSensor[sensorIndex])
  {
    return;
  }

  //set alarm flag
  alarmSensor[sensorIndex] = true;

  //reset alarm timer
  alarmSensor_timer[sensorIndex] = 0;

  //set next alarm
  alarmSensor_index[sensorIndex] = 0;
  alarmSensor_next_alarm[sensorIndex] = alarms_array[0];
}

/*******************************************************************************
 * Function Name  : resetAlarmForSensor
 * Description    : this function resets the alarm for a sensor
 * Return         : none
 *******************************************************************************/
void resetAlarmForSensor(int sensorIndex)
{

  // if the alarm is set for the sensor then reset it
  if (alarmSensor[sensorIndex]) {
    alarmSensor[sensorIndex] = false;
    publishToUbidots("alarm_on_sensor" + userFriendlySensor(sensorIndex), "0");
  }

}

/*******************************************************************************
 * Function Name  : sendAlarmToUser
 * Description    : will fire notifications to the user at scheduled intervals
 * Return         : none
 *******************************************************************************/
void sendAlarmToUser(int sensorIndex)
{

  //is time up for sending the next alarm to the user?
  if (alarmSensor_timer[sensorIndex] < alarmSensor_next_alarm[sensorIndex])
  {
    return;
  }

  //time is up, so reset timer
  alarmSensor_timer[sensorIndex] = 0;

  //set next alarm or just keep current one if there are no more alarms to set
  if (alarmSensor_index[sensorIndex] < arraySize(alarms_array) - 1)
  {
    alarmSensor_index[sensorIndex] = alarmSensor_index[sensorIndex] + 1;
    alarmSensor_next_alarm[sensorIndex] = alarms_array[alarmSensor_index[sensorIndex]];
  }

  //publish readings in the console logs of the dashboard at https://dashboard.particle.io/user/logs
  Particle.publish(APP_NAME, "Threshold exceeded for sensor " + userFriendlySensor(sensorIndex), 60, PRIVATE);

  Blynk.notify("Threshold exceeded for sensor " + userFriendlySensor(sensorIndex));
  Blynk.email(EMAIL_ADDRESS, "TEMPERATURE ALARM", "Threshold exceeded for sensor " + userFriendlySensor(sensorIndex));

  publishToUbidots("alarm_on_sensor" + userFriendlySensor(sensorIndex), "1");

}

/*******************************************************************************/
/*******************************************************************************/
/*******************          BLYNK FUNCTIONS         **************************/
/*******************************************************************************/
/*******************************************************************************/

/*******************************************************************************
 * Function Name  : BLYNK_READ
 * Description    : these functions are called by blynk when the blynk app wants
                     to read values from the photon
                    source: http://docs.blynk.cc/#blynk-main-operations-get-data-from-hardware
 *******************************************************************************/
BLYNK_READ(BLYNK_DISPLAY_SENSOR0)
{
  Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR0, userFriendlyTemperature(getCalibratedSensorReading(0)));
}
BLYNK_READ(BLYNK_DISPLAY_SENSOR1)
{
  Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR1, userFriendlyTemperature(getCalibratedSensorReading(1)));
}
BLYNK_READ(BLYNK_DISPLAY_SENSOR2)
{
  Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR2, userFriendlyTemperature(getCalibratedSensorReading(2)));
}
BLYNK_READ(BLYNK_DISPLAY_SENSOR3)
{
  Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR3, userFriendlyTemperature(getCalibratedSensorReading(3)));
}
BLYNK_READ(BLYNK_DISPLAY_SENSOR0b)
{
  Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR0b, userFriendlyTemperature(getCalibratedSensorReading(0)));
}
BLYNK_READ(BLYNK_DISPLAY_SENSOR1b)
{
  Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR1b, userFriendlyTemperature(getCalibratedSensorReading(1)));
}
BLYNK_READ(BLYNK_DISPLAY_SENSOR2b)
{
  Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR2b, userFriendlyTemperature(getCalibratedSensorReading(2)));
}
BLYNK_READ(BLYNK_DISPLAY_SENSOR3b)
{
  Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR3b, userFriendlyTemperature(getCalibratedSensorReading(3)));
}
BLYNK_READ(BLYNK_DISPLAY_MAX_THRESHOLD0)
{
  Blynk.virtualWrite(BLYNK_DISPLAY_MAX_THRESHOLD0, sensorThreshold[0]);
}
BLYNK_READ(BLYNK_DISPLAY_MAX_THRESHOLD1)
{
  Blynk.virtualWrite(BLYNK_DISPLAY_MAX_THRESHOLD1, sensorThreshold[1]);
}
BLYNK_READ(BLYNK_DISPLAY_MAX_THRESHOLD2)
{
  Blynk.virtualWrite(BLYNK_DISPLAY_MAX_THRESHOLD2, sensorThreshold[2]);
}
BLYNK_READ(BLYNK_DISPLAY_MAX_THRESHOLD3)
{
  Blynk.virtualWrite(BLYNK_DISPLAY_MAX_THRESHOLD3, sensorThreshold[3]);
}
BLYNK_READ(BLYNK_DISPLAY_CALIBRATE0)
{
  Blynk.virtualWrite(BLYNK_DISPLAY_CALIBRATE0, calibration[0]);
}
BLYNK_READ(BLYNK_DISPLAY_CALIBRATE1)
{
  Blynk.virtualWrite(BLYNK_DISPLAY_CALIBRATE1, calibration[1]);
}
BLYNK_READ(BLYNK_DISPLAY_CALIBRATE2)
{
  Blynk.virtualWrite(BLYNK_DISPLAY_CALIBRATE2, calibration[2]);
}
BLYNK_READ(BLYNK_DISPLAY_CALIBRATE3)
{
  Blynk.virtualWrite(BLYNK_DISPLAY_CALIBRATE3, calibration[3]);
}
BLYNK_READ(BLYNK_LED_ALARM_SENSOR0)
{
  if (alarmSensor[0])
  {
    blynkAlarmSensor0.on();
  }
  else
  {
    blynkAlarmSensor0.off();
  }
}
BLYNK_READ(BLYNK_LED_ALARM_SENSOR1)
{
  if (alarmSensor[1])
  {
    blynkAlarmSensor1.on();
  }
  else
  {
    blynkAlarmSensor1.off();
  }
}
BLYNK_READ(BLYNK_LED_ALARM_SENSOR2)
{
  if (alarmSensor[2])
  {
    blynkAlarmSensor2.on();
  }
  else
  {
    blynkAlarmSensor2.off();
  }
}
BLYNK_READ(BLYNK_LED_ALARM_SENSOR3)
{
  if (alarmSensor[3])
  {
    blynkAlarmSensor3.on();
  }
  else
  {
    blynkAlarmSensor3.off();
  }
}

/*******************************************************************************
 * Function Name  : BLYNK_WRITE
 * Description    : these functions are called by blynk when the blynk app wants
                     to write values to the photon
                    source: http://docs.blynk.cc/#blynk-main-operations-send-data-from-app-to-hardware
 *******************************************************************************/
//this are all blynk sliders
// source: http://docs.blynk.cc/#widgets-controllers-slider
BLYNK_WRITE(BLYNK_DISPLAY_MAX_THRESHOLD0)
{
  sensorThreshold[0] = float(param.asInt());
  flagSettingsHaveChanged();
}
BLYNK_WRITE(BLYNK_DISPLAY_MAX_THRESHOLD1)
{
  sensorThreshold[1] = float(param.asInt());
  flagSettingsHaveChanged();
}
BLYNK_WRITE(BLYNK_DISPLAY_MAX_THRESHOLD2)
{
  sensorThreshold[2] = float(param.asInt());
  flagSettingsHaveChanged();
}
BLYNK_WRITE(BLYNK_DISPLAY_MAX_THRESHOLD3)
{
  sensorThreshold[3] = float(param.asInt());
  flagSettingsHaveChanged();
}

BLYNK_WRITE(BLYNK_DISPLAY_CALIBRATE0)
{
  calibration[0] = float(param.asInt());
  sensorReadingString[0] = userFriendlyTemperature(getCalibratedSensorReading(0));
  flagSettingsHaveChanged();
}
BLYNK_WRITE(BLYNK_DISPLAY_CALIBRATE1)
{
  calibration[1] = float(param.asInt());
  sensorReadingString[1] = userFriendlyTemperature(getCalibratedSensorReading(1));
  flagSettingsHaveChanged();
}
BLYNK_WRITE(BLYNK_DISPLAY_CALIBRATE2)
{
  calibration[2] = float(param.asInt());
  sensorReadingString[2] = userFriendlyTemperature(getCalibratedSensorReading(2));
  flagSettingsHaveChanged();
}
BLYNK_WRITE(BLYNK_DISPLAY_CALIBRATE3)
{
  calibration[3] = float(param.asInt());
  sensorReadingString[3] = userFriendlyTemperature(getCalibratedSensorReading(3));
  flagSettingsHaveChanged();
}

/*******************************************************************************
 * Function Name  : BLYNK_setAlarmLedX
 * Description    : these functions are called by our program to update the status
                    of the alarm leds in the blynk cloud and the blynk app
                    source: http://docs.blynk.cc/#blynk-main-operations-send-data-from-app-to-hardware
*******************************************************************************/
void BLYNK_setAlarmLed0(bool alarm)
{
  if (alarm)
  {
    blynkAlarmSensor0.on();
  }
  else
  {
    blynkAlarmSensor0.off();
  }
}
void BLYNK_setAlarmLed1(bool alarm)
{
  if (alarm)
  {
    blynkAlarmSensor1.on();
  }
  else
  {
    blynkAlarmSensor1.off();
  }
}
void BLYNK_setAlarmLed2(bool alarm)
{
  if (alarm)
  {
    blynkAlarmSensor2.on();
  }
  else
  {
    blynkAlarmSensor2.off();
  }
}
void BLYNK_setAlarmLed3(bool alarm)
{
  if (alarm)
  {
    blynkAlarmSensor3.on();
  }
  else
  {
    blynkAlarmSensor3.off();
  }
}

BLYNK_CONNECTED()
{
  Blynk.syncVirtual(BLYNK_DISPLAY_SENSOR0);
  Blynk.syncVirtual(BLYNK_DISPLAY_SENSOR1);
  Blynk.syncVirtual(BLYNK_DISPLAY_SENSOR2);
  Blynk.syncVirtual(BLYNK_DISPLAY_SENSOR3);
  Blynk.syncVirtual(BLYNK_DISPLAY_SENSOR0b);
  Blynk.syncVirtual(BLYNK_DISPLAY_SENSOR1b);
  Blynk.syncVirtual(BLYNK_DISPLAY_SENSOR2b);
  Blynk.syncVirtual(BLYNK_DISPLAY_SENSOR3b);
  Blynk.syncVirtual(BLYNK_DISPLAY_MAX_THRESHOLD0);
  Blynk.syncVirtual(BLYNK_DISPLAY_MAX_THRESHOLD1);
  Blynk.syncVirtual(BLYNK_DISPLAY_MAX_THRESHOLD2);
  Blynk.syncVirtual(BLYNK_DISPLAY_MAX_THRESHOLD3);
  Blynk.syncVirtual(BLYNK_DISPLAY_CALIBRATE0);
  Blynk.syncVirtual(BLYNK_DISPLAY_CALIBRATE1);
  Blynk.syncVirtual(BLYNK_DISPLAY_CALIBRATE2);
  Blynk.syncVirtual(BLYNK_DISPLAY_CALIBRATE3);

  BLYNK_setAlarmLed0(alarmSensor[0]);
  BLYNK_setAlarmLed1(alarmSensor[1]);
  BLYNK_setAlarmLed2(alarmSensor[2]);
  BLYNK_setAlarmLed3(alarmSensor[3]);
}

/*******************************************************************************
 * Function Name  : updateBlynkCloud
 * Description    : publish readings to the blynk server every minute so the
                    History Graph gets updated even when
                    the blynk app is not on (running) in the users phone
 * Return         : none
 *******************************************************************************/
void updateBlynkCloud()
{

  //is it time to store in the blynk cloud? if so, do it
  if ((USE_BLYNK == "yes") and (blynkStoreInterval > BLYNK_STORE_INTERVAL))
  {

    //reset timer
    blynkStoreInterval = 0;

    //publish every sensor reading to the blynk cloud
    Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR0, userFriendlyTemperature(getCalibratedSensorReading(0)));
    Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR1, userFriendlyTemperature(getCalibratedSensorReading(1)));
    Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR2, userFriendlyTemperature(getCalibratedSensorReading(2)));
    Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR3, userFriendlyTemperature(getCalibratedSensorReading(3)));

    //and these are for the calibration tab
    Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR0b, userFriendlyTemperature(getCalibratedSensorReading(0)));
    Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR1b, userFriendlyTemperature(getCalibratedSensorReading(1)));
    Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR2b, userFriendlyTemperature(getCalibratedSensorReading(2)));
    Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR3b, userFriendlyTemperature(getCalibratedSensorReading(3)));
  }
}

/*******************************************************************************
 * Function Name  : updateBlynkLEDs
 * Description    : updated the blynk alarm LEDs in the blynk app
 * Return         : none
 *******************************************************************************/
void updateBlynkLEDs()
{

  if (USE_BLYNK == "yes")
  {
    BLYNK_setAlarmLed0(alarmSensor[0]);
    BLYNK_setAlarmLed1(alarmSensor[1]);
    BLYNK_setAlarmLed2(alarmSensor[2]);
    BLYNK_setAlarmLed3(alarmSensor[3]);
  }
}

/*******************************************************************************/
/*******************************************************************************/
/*******************          EEPROM FUNCTIONS         *************************/
/********  https://docs.particle.io/reference/firmware/photon/#eeprom  *********/
/*******************************************************************************/
/*******************************************************************************/

/*******************************************************************************
 * Function Name  : flagSettingsHaveChanged
 * Description    : this function gets called when the user of the blynk app
                    changes a setting. The blynk app calls the blynk cloud and in turn
                    it calls the functions BLYNK_WRITE()
 * Return         : none
 *******************************************************************************/
void flagSettingsHaveChanged()
{
  settingsHaveChanged = true;
  settingsHaveChanged_timer = 0;
}

/*******************************************************************************
 * Function Name  : readFromEeprom
 * Description    : retrieves the settings from the EEPROM memory
 * Return         : none
 *******************************************************************************/
void readFromEeprom()
{

  EepromMemoryStructure myObj;
  EEPROM.get(EEPROM_ADDRESS, myObj);

  //only read the eeprom if it was written before
  // how do we detect that? we read it.
  // If version (or any data) is 255 it means the eeprom was never written in the first place, hence the
  // data just read with the previous EEPROM.get() is invalid and we will ignore it

  //EEPROM_VERSION_137 means that the eeprom contains data up to version 0.7
  // this data does not include the calibration parameters added in version 0.8
  //TODO: this code can be removed in version 0.9
  if (myObj.version == EEPROM_VERSION_137)
  {
    for (int i = 0; i < arraySize(myObj.sensorThresholdInEeprom); i++)
    {
      sensorThreshold[i] = myObj.sensorThresholdInEeprom[i];
    }
    Particle.publish(APP_NAME, "DEBUG: read new data from EEPROM (" + String(EEPROM_VERSION_137) + ")", 60, PRIVATE);
  }

  if (myObj.version == EEPROM_VERSION)
  {

    //this assignment gives a compiler error and I don't know why
    // so I'm solving this issue with the for loop below
    //sensorThreshold = myObj.sensorThresholdInEeprom;

    //recover thresholds from eeprom into memory
    for (int i = 0; i < arraySize(myObj.sensorThresholdInEeprom); i++)
    {
      sensorThreshold[i] = myObj.sensorThresholdInEeprom[i];
    }

    //recover calibration settings from eeprom into memory
    for (int i = 0; i < arraySize(myObj.calibrationInEeprom); i++)
    {
      calibration[i] = myObj.calibrationInEeprom[i];
    }

    Particle.publish(APP_NAME, "DEBUG: read new data from EEPROM", 60, PRIVATE);

    // update graphs in ubidots
    publishThresholdsToUbidots();
  }
}

/*******************************************************************************
 * Function Name  : saveSettings
 * Description    : in this function we wait a bit to give the user time
                    to adjust the right value for them and in this way we try not
                    to save in EEPROM at every little change.
                    Remember that each eeprom writing cycle is a precious and finite resource
 * Return         : none
 *******************************************************************************/
void saveSettings()
{

  //if no settings were changed, get out of here
  if (not settingsHaveChanged)
  {
    return;
  }

  //if settings have changed, is it time to store them?
  if (settingsHaveChanged_timer < SAVE_SETTINGS_INTERVAL)
  {
    return;
  }

  //reset timer and flag
  settingsHaveChanged_timer = 0;
  settingsHaveChanged = false;

  //store thresholds in the struct type that will be saved in the eeprom
  eepromMemory.version = EEPROM_VERSION;

  //this assignment gives a compiler error and I don't know why
  // so I'm solving this issue with the for loop below
  //eepromMemory.sensorThresholdInEeprom = sensorThreshold;

  //store thresholds from memory into eeprom
  for (int i = 0; i < arraySize(sensorThreshold); i++)
  {
    eepromMemory.sensorThresholdInEeprom[i] = sensorThreshold[i];
  }

  //store calibration settings from memory into eeprom
  for (int i = 0; i < arraySize(calibration); i++)
  {
    eepromMemory.calibrationInEeprom[i] = calibration[i];
  }

  //then save
  EEPROM.put(EEPROM_ADDRESS, eepromMemory);

  // update graphs in ubidots
  publishThresholdsToUbidots();
}

/*******************************************************************************
********************************************************************************
********************************************************************************
 CLOUD FUNCTIONS
********************************************************************************
********************************************************************************
*******************************************************************************/

/*******************************************************************************
 * Function Name  : publishTemperatureToUbidots
 * Description    : sends the value of the data we want to ubidots
 * Notes          : WARNING: THIS FUNCTION CAN TAKE A LONG TIME TO EXECUTE (few seconds...)
 * Return         : none
 *******************************************************************************/
void publishTemperatureToUbidots()
{

  // time to publish data to the cloud?
  if (cloudPublish_timer < CLOUD_PUBLISH_INTERVAL)
  {
    return;
  }

  // reset timer
  cloudPublish_timer = 0;

  publishToUbidots("sensor1", userFriendlyTemperature(getCalibratedSensorReading(0)));
  delay(1100);
  publishToUbidots("sensor2", userFriendlyTemperature(getCalibratedSensorReading(1)));
  delay(1100);
  publishToUbidots("sensor3", userFriendlyTemperature(getCalibratedSensorReading(2)));
  delay(1100);
  publishToUbidots("sensor4", userFriendlyTemperature(getCalibratedSensorReading(3)));
  delay(1100);
}

/*******************************************************************************
 * Function Name  : publishThresholdsToUbidots
 * Description    : sends the value of the data we want to ubidots
 * Notes          : WARNING: THIS FUNCTION CAN TAKE A LONG TIME TO EXECUTE (few seconds...)
 * Return         : none
 *******************************************************************************/
void publishThresholdsToUbidots()
{

  publishToUbidots("threshold1", String(sensorThreshold[0]));
  delay(1100);
  publishToUbidots("threshold2", String(sensorThreshold[1]));
  delay(1100);
  publishToUbidots("threshold3", String(sensorThreshold[2]));
  delay(1100);
  publishToUbidots("threshold4", String(sensorThreshold[3]));
  delay(1100);
}

/*******************************************************************************
 * Function Name  : publishToUbidots
 * Description    : sends the value of the parameter to ubidots
 * Parameters     : String name: name of the value
                    String value: value to store in ubidots
 * Return         : none
 *******************************************************************************/
void publishToUbidots(String name, String value)
{
  Particle.publish("ubidots", "{\"ubi-dsl-vl\":\"" + Particle.deviceID() + "/" + name + "\", \"ubi-token\":\"" + ubidotsToken + "\", \"ubi-value\":\"" + value + "\"}", 60, PRIVATE);
}
