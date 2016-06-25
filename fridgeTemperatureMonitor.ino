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

#define APP_NAME "TemperatureMonitor"
const String VERSION = "Version 0.04";

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

TODO:
  * set thresholds from the blynk app
  * store thresholds for alarms in eeprom
  * pushbullet notifications of alarms
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
String sensorReading[MAX_NUMBER_OF_SENSORS];

/*******************************************************************************
 IO mapping
*******************************************************************************/
// A0 : thermistor 0
// A1 : thermistor 1
// A2 : thermistor 2
// A3 : thermistor 3
// and so on...
const int INPUT_SENSOR[8] = { A0, A1, A2, A3, A4, A5, A6, A7 };

/*******************************************************************************
 alarms variables for each sensor
*******************************************************************************/
bool alarmSensor[8] = { false, false, false, false, false, false, false, false };

elapsedMillis alarmSensor_timer[8];

int alarmSensor_index[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

unsigned long alarmSensor_next_alarm[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

/*******************************************************************************
 thresholds for each sensor
*******************************************************************************/
float sensorThreshold[MAX_NUMBER_OF_SENSORS] = { 0, 0, 0, 0 };

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
#define BLYNK_DISPLAY_SENSOR0 V0
#define BLYNK_DISPLAY_SENSOR1 V1
#define BLYNK_DISPLAY_SENSOR2 V2
#define BLYNK_DISPLAY_SENSOR3 V3
#define BLYNK_DISPLAY_MAX_THRESHOLD0 V10
#define BLYNK_DISPLAY_MAX_THRESHOLD1 V11
#define BLYNK_DISPLAY_MAX_THRESHOLD2 V12
#define BLYNK_DISPLAY_MAX_THRESHOLD3 V13
#define BLYNK_DISPLAY_SENSORS { V0, V1, V2, V3, V4, V5, V6, V7 }
#define BLYNK_DISPLAY_MAX_THRESHOLDS { V10, V11, V12, V13, V14, V15, V16, V17 }
#define BLYNK_LED_ALARM_SENSOR0 V20
#define BLYNK_LED_ALARM_SENSOR1 V21
#define BLYNK_LED_ALARM_SENSOR2 V22
#define BLYNK_LED_ALARM_SENSOR3 V23

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
#define FIRST_ALARM 10000 //10 seconds
#define SECOND_ALARM 60000 //1 minute
#define THIRD_ALARM 300000 //5 minutes
#define FOURTH_ALARM 900000 //15 minutes
#define FIFTH_ALARM 3600000 //1 hour
#define SIXTH_ALARM 14400000 //4 hours - and every 4 hours ever after, until the situation is rectified (ie no more water is detected)
int alarms_array[6]={FIRST_ALARM, SECOND_ALARM, THIRD_ALARM, FOURTH_ALARM, FIFTH_ALARM, SIXTH_ALARM};

/*******************************************************************************
 * Function Name  : setup
 * Description    : this function runs once at system boot
 *******************************************************************************/
void setup() {

  //publish startup message with firmware version
  Particle.publish(APP_NAME, VERSION, 60, PRIVATE);

  //declare and init all analog pins as inputs
  uint8_t i;
  for (i=0; i < 8; i++) {
    pinMode(INPUT_SENSOR[i], INPUT);
  }

  //declare cloud variables
  //https://docs.particle.io/reference/firmware/photon/#particle-variable-
  //Currently, up to 10 cloud variables may be defined and each variable name is limited to a maximum of 12 characters
  if (Particle.variable("sensor0", sensorReading[0])==false) {
    Particle.publish(APP_NAME, "ERROR: Failed to register variable sensor0", 60, PRIVATE);
  }
  if (Particle.variable("sensor1", sensorReading[1])==false) {
    Particle.publish(APP_NAME, "ERROR: Failed to register variable sensor1", 60, PRIVATE);
  }
  if (Particle.variable("sensor2", sensorReading[2])==false) {
    Particle.publish(APP_NAME, "ERROR: Failed to register variable sensor2", 60, PRIVATE);
  }
  if (Particle.variable("sensor3", sensorReading[3])==false) {
    Particle.publish(APP_NAME, "ERROR: Failed to register variable sensor3", 60, PRIVATE);
  }

  //init Blynk
  if (USE_BLYNK == "yes") {
    Blynk.begin(auth);
  }

  Time.zone(TIME_ZONE);

}

/*******************************************************************************
 * Function Name  : loop
 * Description    : this function runs continuously while the project is running
 *******************************************************************************/
void loop() {

  if (USE_BLYNK == "yes") {
    Blynk.run();
  }

  //is it time to read a sensor? if so, do it
  if (sensorSampleInterval > SENSOR_SAMPLE_INTERVAL) {

    //reset timer
    sensorSampleInterval = 0;

    //read the sensor
    float sensorReading = readSensor(sensorToRead);

    //publish and expose in the Particle Cloud and blynk server the reading of the sensor
    publishsensorReading(sensorToRead, sensorReading);

    //verify the reading has not exceeded the threshold
    if ( thresholdExceeded(sensorToRead, sensorReading) ){
      setAlarmForSensor(sensorToRead);
      sendAlarmToUser(sensorToRead);
    } else {
      resetAlarmForSensor(sensorToRead);
    }

    //increment the sensor to read
    sensorToRead = sensorToRead + 1;
    if ( sensorToRead == MAX_NUMBER_OF_SENSORS ) {
      sensorToRead = 0;
    }

    if (USE_BLYNK == "yes") {
      BLYNK_setAlarmLed0(alarmSensor[0]);
    }

    //debug
    Particle.publish("debug", String(sensorThreshold[0]), 60, PRIVATE);

  }

  //publish readings to the blynk server every minute so the History Graph gets updated
  // even when the blynk app is not on
  //is it time to store in the blynk cloud? if so, do it
  if (blynkStoreInterval > BLYNK_STORE_INTERVAL) {

    //reset timer
    blynkStoreInterval = 0;

    if (USE_BLYNK == "yes") {
      Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR0, sensorReading[0]);
      Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR1, sensorReading[1]);
      Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR2, sensorReading[2]);
      Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR3, sensorReading[3]);
    }

  }

}

/*******************************************************************************
 * Function Name  : readSensor
 * Description    : read the value of the thermistor, convert it and
                    store it in a public variable exposed in the cloud
 * Return         : the reading of the sensor in float
 *******************************************************************************/
float readSensor( int sensorIndex )
{

  //store the average of the readings here
  float average;

  //array for storing samples
  int samples[NUMSAMPLES];

  //take N samples in a row, with a slight delay
  uint8_t i;
  for (i=0; i < NUMSAMPLES; i++) {
    samples[i] = readTheAnalogInput(sensorIndex);
    delay(10);
  }

  //average all the samples
  average = 0;
  for (i=0; i< NUMSAMPLES; i++) {
    average += samples[i];
  }
  average /= NUMSAMPLES;

  //convert the value to resistance
  average = (4095 / average)  - 1;
  average = SERIESRESISTOR / average;

  float steinhart;
  steinhart = average / THERMISTORNOMINAL;     // (R/Ro)
  steinhart = log(steinhart);                  // ln(R/Ro)
  steinhart /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
  steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
  steinhart = 1.0 / steinhart;                 // Invert
  steinhart -= 273.15;                         // convert to C

  //Convert Celsius to Fahrenheit
  if (useFahrenheit) {
    steinhart = (steinhart * 9.0)/ 5.0 + 32.0;
  }

  return steinhart;
}

/*******************************************************************************
 * Function Name  : publishsensorReading
 * Description    : the temperature passed as parameter gets stored in an internal variable
                    and then published to the Particle Cloud and the blynk server
 * Return         : 0
 *******************************************************************************/
int publishsensorReading( int sensorIndex, float temperature ) {

  char currentTempChar[32];
  int currentTempDecimals = (temperature - (int)temperature) * 100;
  String tempToBePublished = "Sensor " + String(sensorIndex) + ": ";

  //this is a fix required for displaying properly negative temperatures
  // without the abs(), the project will display something like "-4.-87"
  // with the abs(), the project will display "-4.87"
  currentTempDecimals = abs(currentTempDecimals);

  //this converts the temperature into a more user friendly format with 2 decimals
  sprintf(currentTempChar,"%0d.%d", (int)temperature, currentTempDecimals);

  //store readings into exposed variables in the Particle Cloud
  sensorReading[sensorIndex] = String(currentTempChar);
  tempToBePublished = tempToBePublished + sensorReading[sensorIndex];

  //publish reading in the console logs of the dashboard at https://dashboard.particle.io/user/logs
  Particle.publish(APP_NAME, tempToBePublished + getTemperatureUnit(), 60, PRIVATE);

  if (USE_BLYNK == "yes") {
    switch (sensorIndex)
    {
      case 1:
        BLYNK_READ(BLYNK_DISPLAY_SENSOR0);
        break;
      case 2:
        BLYNK_READ(BLYNK_DISPLAY_SENSOR1);
        break;
      case 3:
        BLYNK_READ(BLYNK_DISPLAY_SENSOR2);
        break;
      case 4:
        BLYNK_READ(BLYNK_DISPLAY_SENSOR3);
        break;
    }
  }

  return 0;
}

/*******************************************************************************
 * Function Name  : readTemperature
 * Description    : read the value of the thermistor, convert it and
                    store it in a public variable exposed in the cloud
 * Return         : 0
 *******************************************************************************/
int readTheAnalogInput( int sensorIndex )
{
  return analogRead(INPUT_SENSOR[sensorIndex]);
}

/*******************************************************************************
 * Function Name  : getTemperatureUnit
 * Description    : return "°F" or "°C" according to useFahrenheit
 * Return         : String
 *******************************************************************************/
String getTemperatureUnit() {
  if ( useFahrenheit ) {
    return "°F";
  } else {
    return "°C";
  }
}

/*******************************************************************************
 * Function Name  : thresholdExceeded
 * Description    : this function verifies that the temperature passed as parameter
                    does not exceed the threshold assigned for the sensor
 * Return         : false if threshold has not been exceeded
                    true if threshold has been exceeded
 *******************************************************************************/
bool thresholdExceeded( int sensorIndex, float temperature ) {

  if ( sensorThreshold[sensorIndex] and ( temperature > sensorThreshold[sensorIndex] ) ) {
    return true;
  }

  return false;

}

/*******************************************************************************
 * Function Name  : setAlarmForSensor
 * Description    : this function sets the alarm for a sensor
 * Return         : nothing
 *******************************************************************************/
void setAlarmForSensor( int sensorIndex ) {

  //if the alarm is already set for the sensor, no need to do anything, since a notification is being fired
  if (alarmSensor[sensorIndex]){
    return;
  }

  alarmSensor[sensorIndex] = true;

  //reset alarm timer
  //TODO: add this var on top alarm_timer = 0;
  alarmSensor_timer[sensorIndex] = 0;

  //set next alarm
  //TODO: alarm_index = 0;
  alarmSensor_index[sensorIndex] = 0;
  //TODO: next_alarm = alarms_array[0];
  alarmSensor_next_alarm[sensorIndex] = alarms_array[0];

  return;

}

/*******************************************************************************
 * Function Name  : resetAlarmForSensor
 * Description    : this function resets the alarm for a sensor
 * Return         : nothing
 *******************************************************************************/
void resetAlarmForSensor( int sensorIndex ) {

  alarmSensor[sensorIndex] = false;

}
/*******************************************************************************
 * Function Name  : sendAlarmToUser
 * Description    : will fire notifications to the user at scheduled intervals
 * Return         : nothing
 *******************************************************************************/
void sendAlarmToUser( int sensorIndex ) {

    //is time up for sending the next alarm to the user?
    if (alarmSensor_timer[sensorIndex] < alarmSensor_next_alarm[sensorIndex]) {
        return;
    }

    //time is up, so reset timer
    alarmSensor_timer[sensorIndex] = 0;

    //set next alarm or just keep current one if there are no more alarms to set
    if (alarmSensor_index[sensorIndex] < arraySize(alarms_array)-1) {
        alarmSensor_index[sensorIndex] = alarmSensor_index[sensorIndex] + 1;
        alarmSensor_next_alarm[sensorIndex] = alarms_array[alarmSensor_index[sensorIndex]];
    }

    //publish readings in the console logs of the dashboard at https://dashboard.particle.io/user/logs
    Particle.publish(APP_NAME, "Threshold exceeded for sensor " + String(sensorIndex), 60, PRIVATE);

   return;
}

/*******************************************************************************/
/*******************************************************************************/
/*******************          BLYNK FUNCTIONS         **************************/
/*******************************************************************************/
/*******************************************************************************/

/*******************************************************************************
 * Function Name  : BLYNK_READ
 * Description    : sends the value of sensor1, sensor2, sensor3, sensor4 to the
                    corresponding widget in the blynk app
                    source: http://docs.blynk.cc/#widgets-displays-value-display
 *******************************************************************************/
BLYNK_READ(BLYNK_DISPLAY_SENSOR0) {
  Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR0, sensorReading[0]);
}
BLYNK_READ(BLYNK_DISPLAY_SENSOR1) {
  Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR1, sensorReading[1]);
}
BLYNK_READ(BLYNK_DISPLAY_SENSOR2) {
  Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR2, sensorReading[2]);
}
BLYNK_READ(BLYNK_DISPLAY_SENSOR3) {
  Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR3, sensorReading[3]);
}
BLYNK_READ(BLYNK_DISPLAY_MAX_THRESHOLD0) {
  Blynk.virtualWrite(BLYNK_DISPLAY_MAX_THRESHOLD0, sensorThreshold[0]);
}
BLYNK_READ(BLYNK_DISPLAY_MAX_THRESHOLD1) {
  Blynk.virtualWrite(BLYNK_DISPLAY_MAX_THRESHOLD1, sensorThreshold[1]);
}
BLYNK_READ(BLYNK_DISPLAY_MAX_THRESHOLD2) {
  Blynk.virtualWrite(BLYNK_DISPLAY_MAX_THRESHOLD2, sensorThreshold[2]);
}
BLYNK_READ(BLYNK_DISPLAY_MAX_THRESHOLD3) {
  Blynk.virtualWrite(BLYNK_DISPLAY_MAX_THRESHOLD3, sensorThreshold[3]);
}

//this is a blynk slider
// source: http://docs.blynk.cc/#widgets-controllers-slider
BLYNK_WRITE(BLYNK_DISPLAY_MAX_THRESHOLD0) {
   sensorThreshold[0] = float(param.asInt());
}

//this is a blynk led
// source: http://docs.blynk.cc/#widgets-displays-led
BLYNK_READ(BLYNK_LED_ALARM_SENSOR0) {
  if ( alarmSensor[0] ) {
    blynkAlarmSensor0.on();
  } else {
    blynkAlarmSensor0.off();
  }
}

void BLYNK_setAlarmLed0(bool alarm) {
  if (USE_BLYNK == "yes") {
    if ( alarm ) {
      blynkAlarmSensor0.on();
    } else {
      blynkAlarmSensor0.off();
    }
  }
}

BLYNK_CONNECTED() {
  Blynk.syncVirtual(BLYNK_DISPLAY_SENSOR0);
  Blynk.syncVirtual(BLYNK_DISPLAY_SENSOR1);
  Blynk.syncVirtual(BLYNK_DISPLAY_SENSOR2);
  Blynk.syncVirtual(BLYNK_DISPLAY_SENSOR3);
  Blynk.syncVirtual(BLYNK_DISPLAY_MAX_THRESHOLD0);
  Blynk.syncVirtual(BLYNK_DISPLAY_MAX_THRESHOLD1);
  Blynk.syncVirtual(BLYNK_DISPLAY_MAX_THRESHOLD2);
  Blynk.syncVirtual(BLYNK_DISPLAY_MAX_THRESHOLD3);

  BLYNK_setAlarmLed0(alarmSensor[0]);

}
