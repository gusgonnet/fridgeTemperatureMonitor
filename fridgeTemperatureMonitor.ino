#include "math.h"
#include "application.h"
#include "elapsedMillis.h"
#include "blynk.h"
#include "blynkAuthToken.h"

#define APP_NAME "FridgeTemperatureMonitor"
const String VERSION = "Version 0.02";

/*******************************************************************************
  the sensor used: 10K Precision Epoxy Thermistor - 3950 NTC
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
 IO mapping
*******************************************************************************/
// A0 : thermistor 1
// A1 : thermistor 2
// A2 : thermistor 3
// A3 : thermistor 4
int INPUT_SENSOR1 = A0;
int INPUT_SENSOR2 = A1;
int INPUT_SENSOR3 = A2;
int INPUT_SENSOR4 = A3;

/*******************************************************************************
 Sensors
*******************************************************************************/
//Strings to store the sensors' reading so it can be exposed in the Particle Cloud
String sensor1;
String sensor2;
String sensor3;
String sensor4;

//we will read one after the other and this is an index that will help us keep track
// of which one we need to read next
// IMPORTANT: index starts at 1
int sensorToRead = 1;

//defines the maximum number of sensors in case one day we add more
#define MAX_NUMBER_OF_SENSORS 4

//defines how often the measurements are made (millisecs)
#define SENSOR_SAMPLE_INTERVAL 5000

//this is a library timer
elapsedMillis sensorSampleInterval;

/*******************************************************************************
 alarms variables for each sensor
*******************************************************************************/
bool alarmSensor1 = false;
bool alarmSensor2 = false;
bool alarmSensor3 = false;
bool alarmSensor4 = false;

elapsedMillis alarmSensor1_timer;
elapsedMillis alarmSensor2_timer;
elapsedMillis alarmSensor3_timer;
elapsedMillis alarmSensor4_timer;

int alarmSensor1_index = 0;
int alarmSensor2_index = 0;
int alarmSensor3_index = 0;
int alarmSensor4_index = 0;

unsigned long alarmSensor1_next_alarm = 0;
unsigned long alarmSensor2_next_alarm = 0;
unsigned long alarmSensor3_next_alarm = 0;
unsigned long alarmSensor4_next_alarm = 0;

/*******************************************************************************
 thresholds for each sensor
*******************************************************************************/
float thresholdSensor1 = 50;
float thresholdSensor2 = 50;
float thresholdSensor3 = 50;
float thresholdSensor4 = 50;

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
#define BLYNK_DISPLAY_SENSOR1 V1
#define BLYNK_DISPLAY_SENSOR2 V2
#define BLYNK_DISPLAY_SENSOR3 V3
#define BLYNK_DISPLAY_SENSOR4 V4
#define BLYNK_DISPLAY_THRESHOLD1 V11
#define BLYNK_DISPLAY_THRESHOLD2 V12
#define BLYNK_DISPLAY_THRESHOLD3 V13
#define BLYNK_DISPLAY_THRESHOLD4 V14

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

  //declare and init pins
  pinMode(INPUT_SENSOR1, INPUT);
  pinMode(INPUT_SENSOR2, INPUT);
  pinMode(INPUT_SENSOR3, INPUT);
  pinMode(INPUT_SENSOR4, INPUT);

  //declare cloud variables
  //https://docs.particle.io/reference/firmware/photon/#particle-variable-
  //Currently, up to 10 cloud variables may be defined and each variable name is limited to a maximum of 12 characters
  if (Particle.variable("sensor1", sensor1)==false) {
    Particle.publish(APP_NAME, "ERROR: Failed to register variable sensor1", 60, PRIVATE);
  }
  if (Particle.variable("sensor2", sensor2)==false) {
    Particle.publish(APP_NAME, "ERROR: Failed to register variable sensor2", 60, PRIVATE);
  }
  if (Particle.variable("sensor3", sensor3)==false) {
    Particle.publish(APP_NAME, "ERROR: Failed to register variable sensor3", 60, PRIVATE);
  }
  if (Particle.variable("sensor4", sensor4)==false) {
    Particle.publish(APP_NAME, "ERROR: Failed to register variable sensor4", 60, PRIVATE);
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

  //is time to read a sensor? if so, do it
  if (sensorSampleInterval > SENSOR_SAMPLE_INTERVAL) {

    //reset timer
    sensorSampleInterval = 0;

    //read the sensor
    float sensorReading = readSensor(sensorToRead);

    //publish and expose in the Particle Cloud and blynk server the reading of the sensor
    publishSensorReading(sensorToRead, sensorReading);

    //verify the reading has not exceeded the threshold
    if ( thresholdExceeded(sensorToRead, sensorReading) ){
      setAlarmForSensor(sensorToRead);
      sendAlarmToUser(sensorToRead);
    }

    //increment the sensor to read
    sensorToRead = sensorToRead + 1;
    if ( sensorToRead > MAX_NUMBER_OF_SENSORS ) {
      sensorToRead = 1;
    }

  }

  if (USE_BLYNK == "yes") {
    //all the Blynk magic happens here
    Blynk.run();
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
 * Function Name  : publishSensorReading
 * Description    : the temperature passed as parameter gets stored in an internal variable
                    and then published to the Particle Cloud and the blynk server
 * Return         : 0
 *******************************************************************************/
int publishSensorReading( int sensorIndex, float temperature ) {

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
  switch (sensorIndex)
  {
    case 1:
      sensor1 = String(currentTempChar);
      tempToBePublished = tempToBePublished + sensor1;
      break;
    case 2:
      sensor2 = String(currentTempChar);
      tempToBePublished = tempToBePublished + sensor2;
      break;
    case 3:
      sensor3 = String(currentTempChar);
      tempToBePublished = tempToBePublished + sensor3;
      break;
    case 4:
      sensor4 = String(currentTempChar);
      tempToBePublished = tempToBePublished + sensor4;
      break;
  }

  //publish reading in the console logs of the dashboard at https://dashboard.particle.io/user/logs
  Particle.publish(APP_NAME, tempToBePublished + getTemperatureUnit(), 60, PRIVATE);

  //publish reading to the blynk server so the History Graph gets updated even when the blynk app is not on
  if (USE_BLYNK == "yes") {
    switch (sensorIndex)
    {
      case 1:
        BLYNK_READ(BLYNK_DISPLAY_SENSOR1);
        break;
      case 2:
        BLYNK_READ(BLYNK_DISPLAY_SENSOR2);
        break;
      case 3:
        BLYNK_READ(BLYNK_DISPLAY_SENSOR3);
        break;
      case 4:
        BLYNK_READ(BLYNK_DISPLAY_SENSOR4);
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
  int reading;

  if (sensorIndex == 1) {
    reading = analogRead(INPUT_SENSOR1);
  }
  if (sensorIndex == 2) {
    reading = analogRead(INPUT_SENSOR2);
  }
  if (sensorIndex == 3) {
    reading = analogRead(INPUT_SENSOR3);
  }
  if (sensorIndex == 4) {
    reading = analogRead(INPUT_SENSOR4);
  }

  return reading;
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

  //a temporary, local variable that will store the threshold for the selected sensor
  float thresholdForSensor;

  //store readings into exposed variables in the Particle Cloud
  switch (sensorIndex)
  {
    case 1:
      thresholdForSensor = thresholdSensor1;
      break;
    case 2:
      thresholdForSensor = thresholdSensor2;
      break;
    case 3:
      thresholdForSensor = thresholdSensor3;
      break;
    case 4:
      thresholdForSensor = thresholdSensor4;
      break;
  }

  if ( thresholdForSensor > temperature ) {
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
  if (alarmSensor1){
    return;
  }

  alarmSensor1 = true;

  //reset alarm timer
  //TODO: add this var on top alarm_timer = 0;
  alarmSensor1_timer = 0;

  //set next alarm
  //TODO: alarm_index = 0;
  alarmSensor1_index = 0;
  //TODO: next_alarm = alarms_array[0];
  alarmSensor1_next_alarm = alarms_array[0];

  return;

}

/*******************************************************************************
 * Function Name  : sendAlarmToUser
 * Description    : will fire notifications to the user at scheduled intervals
 * Return         : nothing
 *******************************************************************************/
void sendAlarmToUser( int sensorIndex ) {

    //is time up for sending the next alarm to the user?
    if (alarmSensor1_timer < alarmSensor1_next_alarm) {
        return;
    }

    //time is up, so reset timer
    alarmSensor1_timer = 0;

    //set next alarm or just keep current one if there are no more alarms to set
    if (alarmSensor1_index < arraySize(alarms_array)-1) {
        alarmSensor1_index = alarmSensor1_index + 1;
        alarmSensor1_next_alarm = alarms_array[alarmSensor1_index];
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
BLYNK_READ(BLYNK_DISPLAY_SENSOR1) {
  Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR1, sensor1);
}
BLYNK_READ(BLYNK_DISPLAY_SENSOR2) {
  Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR2, sensor2);
}
BLYNK_READ(BLYNK_DISPLAY_SENSOR3) {
  Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR3, sensor3);
}
BLYNK_READ(BLYNK_DISPLAY_SENSOR4) {
  Blynk.virtualWrite(BLYNK_DISPLAY_SENSOR4, sensor4);
}
BLYNK_READ(BLYNK_DISPLAY_THRESHOLD1) {
  Blynk.virtualWrite(BLYNK_DISPLAY_THRESHOLD1, thresholdSensor1);
}
BLYNK_READ(BLYNK_DISPLAY_THRESHOLD2) {
  Blynk.virtualWrite(BLYNK_DISPLAY_THRESHOLD2, thresholdSensor2);
}
BLYNK_READ(BLYNK_DISPLAY_THRESHOLD3) {
  Blynk.virtualWrite(BLYNK_DISPLAY_THRESHOLD3, thresholdSensor3);
}
BLYNK_READ(BLYNK_DISPLAY_THRESHOLD4) {
  Blynk.virtualWrite(BLYNK_DISPLAY_THRESHOLD4, thresholdSensor4);
}

BLYNK_CONNECTED() {
  Blynk.syncVirtual(BLYNK_DISPLAY_SENSOR1);
  Blynk.syncVirtual(BLYNK_DISPLAY_SENSOR2);
  Blynk.syncVirtual(BLYNK_DISPLAY_SENSOR3);
  Blynk.syncVirtual(BLYNK_DISPLAY_SENSOR4);
  Blynk.syncVirtual(BLYNK_DISPLAY_THRESHOLD1);
  Blynk.syncVirtual(BLYNK_DISPLAY_THRESHOLD2);
  Blynk.syncVirtual(BLYNK_DISPLAY_THRESHOLD3);
  Blynk.syncVirtual(BLYNK_DISPLAY_THRESHOLD4);
}
