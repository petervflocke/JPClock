/*
 *  Under development, somehow produtive :)
 *
 */

#include <Arduino.h>
#include "myClock.h"
#include <TimeLib.h>
#include <Timezone.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// OTA
#include <ESPmDNS.h>
#include <ArduinoOTA.h>


#include "esp_wps.h"

#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

#include <WiFiUdp.h>
#include <NTPClient.h>
//#include <MD_CirQueue.h> // switch to xQueueHandle
#include <MD_REncoder.h>
#include <BH1750.h>
// #include <SparkFunBME280.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#include "DFRobotDFPlayerMini.h"
#include <PPMax72xxPanel.h>
#include <PPmax72xxAnimate.h>
#include <myScheduler.h>
#include <Preferences.h>

boolean wpsNeeded;
String sWIFI_SSID;
String sWIFI_PASWD;


PPMax72xxPanel matrix = PPMax72xxPanel(pinCS, numberOfHorizontalDisplays, numberOfVerticalDisplays);
PPmax72xxAnimate zoneClockH0 = PPmax72xxAnimate(&matrix);
PPmax72xxAnimate zoneClockH1 = PPmax72xxAnimate(&matrix);
PPmax72xxAnimate zoneClockM0 = PPmax72xxAnimate(&matrix);
PPmax72xxAnimate zoneClockM1 = PPmax72xxAnimate(&matrix);
PPmax72xxAnimate zoneClockS1 = PPmax72xxAnimate(&matrix);
PPmax72xxAnimate zoneClockS0 = PPmax72xxAnimate(&matrix);

PPmax72xxAnimate zoneInfo0 = PPmax72xxAnimate(&matrix);
PPmax72xxAnimate zoneInfo1 = PPmax72xxAnimate(&matrix);
//PPmax72xxAnimate zoneInfo2 = PPmax72xxAnimate(&matrix);

SemaphoreHandle_t bufMutex;

float _t, _h, _p, _l;

BH1750 lightMeter;
Adafruit_BME280 mySensor;

temp_t tempTable[NumberOfPoints];
temp_t humiTable[NumberOfPoints];
temp_t presTable[NumberOfPoints];

//const uint8_t  QUEUE_SIZE = 15;
//MD_CirQueue Q(QUEUE_SIZE, sizeof(uint8_t)); // switch to xQueueHandle :
QueueHandle_t xQueue;

// set up encoder object
MD_REncoder R = MD_REncoder(PIN_A, PIN_B);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 0);
unsigned long NTPSyncPeriod = NTPRESYNC;
boolean DSTFlag = false;       // indicate if DS is on/off based on the time for Germany / CET_DELTA,CEST_DELTA
boolean SyncNTPError = false;  // true if the last NTP was finished with an error due the wifi or ntp failure

ClockStates ClockState = _Clock_init;   // current clock status
ClockStates goBackState = _Clock_init;  // last clock status

// more time zones, see  http://en.wikipedia.org/wiki/Time_zones_of_Europe
// United Kingdom (London, Belfast)
// TimeChangeRule rBST = {"BST", Last, Sun, Mar, 1, 60};   //British Summer Time
// TimeChangeRule rGMT = {"GMT", Last, Sun, Oct, 2, 0};    //Standard Time
// Timezone UK(rBST, rGMT);
TimeChangeRule rCEST = { "CEST", Last, Sun, Mar, 2, 120 };  // starts last Sunday in March at 2:00 am, UTC offset +120 minutes; Central European Summer Time (CEST_DELTA)
TimeChangeRule rCET = { "CET", Last, Sun, Oct, 3, 60 };     // ends last Sunday in October at 3:00 am, UTC offset +60 minutes; Central European Time (CET_DELTA)
Timezone CET(rCEST, rCET);


Schedular SensorUpdate(_Seconds);    // how often are the sensor read
Schedular NTPUpdateTask(_Seconds);   // how often is the clock synced with NTP server
Schedular DataDisplayTask(_Millis);  // scroll over to the next type of info for a full clock info mode
Schedular IntensityCheck(_Millis);   // how oftenshall be check the light to stear the intensity of LED Matrix
Schedular SnakeUpdate(_Millis);      // for the snake next step
Schedular StatTask(_Millis);         // update statistic screen period
Schedular SecondsVA(_Millis);        // delay for getting VA back to zero
Schedular ScreenSaver(_Seconds);     // LED Matrix standby time after last PIR movement detection
Schedular ChimeQuarter(_Seconds);    // time for qurterly chime
Schedular ChimeHour(_Seconds);       // time for hourly chime
Schedular ChimeWait(_Millis);        // time to wait after 4 quarter chime
Schedular UpdateMQTT(_Seconds);      // Frequency of updating IOT

// MP3 Player
HardwareSerial DFPSerial(1);
DFRobotDFPlayerMini DFPlayer;


// IOT variables
WiFiClientSecure client;
// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);
// Feeds

// Notice MQTT paths for AIO follow the form: <username>/feeds/<feedname>
Adafruit_MQTT_Publish tempMQTT = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME feedTemp);
Adafruit_MQTT_Publish humiMQTT = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME feedHumi);
Adafruit_MQTT_Publish presMQTT = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME feedPres);
Adafruit_MQTT_Publish brigMQTT = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME feedBrig);
Adafruit_MQTT_Publish onofMQTT = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME feedOnOf);
Adafruit_MQTT_Publish dataMQTT = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME feedData);

// Setup a feed for subscribing to changes.
Adafruit_MQTT_Subscribe ledMQTT = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME feedLED);

// Bug workaround for Arduino 1.6.6, it seems to need a function declaration
// for some reason (only affects ESP8266, likely an arduino-builder bug).
boolean MQTT_connect();
void clearScreen(void);
time_t requestSync();
void processSyncMessage();
boolean SetUpDST(uint8_t _month, uint8_t _day, uint8_t _weekday);
void correctByDST();
boolean SyncNTP();
boolean StartSyncNTP();
void SetupWiFi(void);
boolean updateTtime(int &value, int &lvalue, int newvalue, char what);
uint8_t IntensityMap(uint16_t sensor);
boolean checkTime(uint8_t hh, uint8_t mm, uint8_t ss, boolean exact);
uint8_t keyboard(ClockStates key_DIR_CW, ClockStates key_DIR_CCW, ClockStates key_SW_DOWN, ClockStates key_SW_UP);
boolean occupied(int ptrA, const int snakeLength, int *snakeX, int *snakeY);
int next(int ptr, const int snakeLength);
boolean equal(int ptrA, int ptrB, int *snakeX, int *snakeY);
void loadPreferences(void);
void savePreferences(void);
void wpsStart();
void wpsStop();
void WiFiEvent(WiFiEvent_t event, arduino_event_info_t info);
void wpsInitConfig();
void processButton();
void processEncoder();
void taskMQTT(void *parameter);
String digitalClockString();
String printDigits(int digits);


//globals for screensaver status main loop and MQTT updates
bool screenSaverNotActive = true;
String lastTime = "";


// Parameter section, just one for the time being
Preferences preferences;
boolean DingOnOff = true;


// PWM data for HDD voice coil
// setting PWM properties
#define DACVoiceCoil 26
#define voiceCoilMin 78 /* originally 58 to be increased for a high temperature in the room  */
#define voiceCoilMax 99 /* 95 originally */


void clearScreen(void) {
  matrix.setClip(0, matrix.width(), 0, matrix.height());
  matrix.fillScreen(LOW);
  matrix.write();
  dacWrite(DACVoiceCoil, 0);
}

/* to be removed
// Time management 
time_t requestSync() {return 0;} // the time will be sent later in response to serial mesg

void processSyncMessage() {
  // unsigned long pctime = 1509235190;
  unsigned long pctime = timeClient.getEpochTime();
  setTime(pctime); // Sync clock to the time received on the serial port
}


boolean SetUpDST(uint8_t _month, uint8_t _day, uint8_t _weekday) {
// Dst = True for summer time 
// _weekday = day of the week Sunday = 1, Saturday = 7

  if (_month < 3 || _month > 10)  return false; 
  if (_month > 3 && _month < 10)  return true; 

  int previousSunday = _day - _weekday;
  if (_month == 3)  return previousSunday >= 24;
  if (_month == 10) return previousSunday < 24;

  return false; // this line never gonna happend
}

void correctByDST() {
  //Check if DST has to correct the time
  if (month() == 3) {
    if (day() - weekday() >= 24) {
      if ( (hour() == 2) && (!DSTFlag) ) {
        DSTFlag = true;
        adjustTime(+SECS_PER_HOUR);
      }
    }
  } else if (month() == 10) {
    if (day() - weekday() >= 24) {
      if ( (hour() == 2) && (DSTFlag) ) {
        DSTFlag = false;
        adjustTime(-SECS_PER_HOUR);
      }      
    }
  }        
}
*/

boolean SyncNTP() {

  PRINTS("Contacting NTP Server process\n");
  if (timeClient.forceUpdate()) {
    PRINTS("NTP sync OK, UTC=");
    PRINTS(timeClient.getFormattedTime());
    PRINTLN;
    setTime(timeClient.getEpochTime());
    return true;
  } else {
    PRINTS("NTP sync failed!");
    return false;
  }
}

boolean StartSyncNTP() {
  boolean SyncError = false;

  PRINTS("NTP Sync Init started\n");
  zoneInfo0.setText("NTP Sync", _SCROLL_LEFT, _TO_LEFT, InfoTick1, I0s, I0e);
  zoneInfo0.Animate(true);
  if (WiFi.isConnected()) {
    SyncError = !SyncNTP();
  } else {
    PRINTS("Recovering WiFi connection\n");
    //WiFi.mode(WIFI_STA);
    matrix.fillScreen(LOW);
    matrix.setCursor(0, 0);
    zoneInfo0.setText("Connecting WiFi", _SCROLL_LEFT, _TO_FULL, InfoTick1, I0s, I0e);
    WiFi.begin();
    zoneInfo0.Animate(true);
    if (WiFi.isConnected()) {
      zoneInfo0.setText("Connecting NTP", _SCROLL_LEFT, _TO_FULL, InfoTick1, I0s, I0e);
      SyncError = !SyncNTP();
      zoneInfo0.Animate(true);
      //WiFi.mode(WIFI_OFF);
    } else {
      SyncError = true;  // wifi failure
    }
  }
  return SyncError;
}

void SetupWiFi(void) {
  Schedular UpdateWifi(_Seconds);  // Wifi reconnect
  int i;
  zoneInfo0.setText("WiFi?", _SCROLL_LEFT, _TO_LEFT, InfoTick1, I0s, I0e);
  zoneInfo0.Animate(true);
  PRINTS("Connecting WiFi\n");

  WiFi.disconnect();
  WiFi.softAPdisconnect(true);
  for (int x = voiceCoilMax; x > voiceCoilMin; dacWrite(DACVoiceCoil, x--)) { delay(10);}
  WiFi.mode(WIFI_STA);
  PRINT("SSID", sWIFI_SSID.c_str());
  PRINTLN;
  PRINT("PASW", sWIFI_PASWD.c_str());
  PRINTLN;
  WiFi.begin(sWIFI_SSID.c_str(), sWIFI_PASWD.c_str());


  #define MAXRET 30
  i = 1;
  UpdateWifi.start(-1);
  zoneInfo1.setText(">", _SCROLL_RIGHT, _TO_FULL, InfoTick1, 32, 64);
  while ((WiFi.status() != WL_CONNECTED) && i != 0) {
    if (i >= MAXRET) {
      PRINTS("\nWIFI connection failled - rebooting\n");
      dacWrite(DACVoiceCoil,0);
      ESP.restart();  // reboot and try again
    }
    if (zoneInfo1.Animate(false)) matrix.write();
    if (zoneInfo1.AnimateDone()) zoneInfo1.Reset();
    if (UpdateWifi.check(1)) {
      // WiFi.begin(WIFI_SSID, WIFI_PASWD);
      dacWrite(DACVoiceCoil, map(i, 0, MAXRET, voiceCoilMax-5, voiceCoilMin+5));      
      PRINTS(".");
      i++;
    }
    // if (button.uniquePress()) i = 0; // skip wifi run withou time current stamp
  }
  clearScreen();
  zoneInfo0.setText("Connected", _SCROLL_LEFT, _TO_LEFT, InfoTick1, I0s, I0e);
  zoneInfo0.Animate(true);
  PRINTS("WiFi connected\n");
  PRINTS("IP address: \n");
  PRINTS(WiFi.localIP());
  PRINTLN;
}

boolean updateTtime(int &value, int &lvalue, int newvalue, char what) {
  String sValue;
  if (value != newvalue) {
    value = newvalue;
    if (lvalue / 10 != value / 10) {
      sValue = String(lvalue / 10) + "\n" + String(value / 10);
      switch (what) {
        case 'H':
          zoneClockH1.setText(sValue, _SCROLL_UP_SMOOTH, _NONE_MOD, ClockAnimTick, H1s, H1e);
          break;
        case 'M':
          zoneClockM1.setText(sValue, _SCROLL_UP_SMOOTH, _NONE_MOD, ClockAnimTick, M1s, M1e);
          break;
        case 'S':
          zoneClockS1.setText(sValue, _SCROLL_UP_SMOOTH, _NONE_MOD, ClockAnimTick, S1s, S1e);
          break;
      }
    }
    sValue = String(lvalue % 10) + "\n" + String(value % 10);
    switch (what) {
      case 'H':
        zoneClockH0.setText(sValue, _SCROLL_UP_SMOOTH, _NONE_MOD, ClockAnimTick, H0s, H0e);
        break;
      case 'M':
        zoneClockM0.setText(sValue, _SCROLL_UP_SMOOTH, _NONE_MOD, ClockAnimTick, M0s, M0e);
        break;
      case 'S':
        zoneClockS0.setText(sValue, _SCROLL_UP_SMOOTH, _NONE_MOD, ClockAnimTick, S0s, S0e);
        break;
    }
    lvalue = value;
    return true;
  } else return false;
}

uint8_t IntensityMap(uint16_t sensor) {
  uint8_t Intensity;
  if (sensor < 80) Intensity = 0;
  else if (sensor < 110) Intensity = 1;
  else if (sensor < 140) Intensity = 2;
  else if (sensor < 170) Intensity = 3;
  else if (sensor < 200) Intensity = 4;
  else if (sensor < 250) Intensity = 5;
  else if (sensor < 300) Intensity = 6;
  else if (sensor < 350) Intensity = 7;
  else if (sensor < 400) Intensity = 8;
  else if (sensor < 450) Intensity = 9;
  else if (sensor < 500) Intensity = 10;
  else if (sensor < 550) Intensity = 11;
  else if (sensor < 600) Intensity = 12;
  else if (sensor < 650) Intensity = 13;
  else if (sensor < 700) Intensity = 14;
  else Intensity = 15;
  return Intensity;
}

/*
boolean checkTime(uint8_t hh, uint8_t mm, uint8_t ss, boolean exact) {
  time_t LocalTime = CET.toLocal(now());
  if (exact) return (hh == hour(LocalTime) && mm == minute(LocalTime) && ss == second(LocalTime));
  else return (hh == hour(LocalTime) && mm == minute(LocalTime) && ss <= second(LocalTime));
}
*/

uint8_t keyboard(ClockStates key_DIR_CW, ClockStates key_DIR_CCW, ClockStates key_SW_DOWN, ClockStates key_SW_UP) {
  uint8_t key = DIR_NONE;

  if (xQueueReceive(xQueue, &key, 0) == pdPASS) {
    switch (key) {
      case DIR_CW:
        if (key_DIR_CW != _Clock_none) ClockState = key_DIR_CW;
        break;
      case DIR_CCW:
        if (key_DIR_CCW != _Clock_none) ClockState = key_DIR_CCW;
        break;
      case SW_DOWN:
        if (key_SW_DOWN != _Clock_none) ClockState = key_SW_DOWN;
        break;
      case SW_UP:
        if (key_SW_UP != _Clock_none) ClockState = key_SW_UP;
        break;
    }
#if DEBUG_ON
    if (key != DIR_NONE) {
      PRINT("New Closk State: ", ClockState);
      PRINTLN;
    }
#endif
    return key;
  } else return DIR_NONE;
}

/* Snake Routines 
Start => */
boolean occupied(int ptrA, const int snakeLength, int *snakeX, int *snakeY) {
  for (int ptrB = 0; ptrB < snakeLength; ptrB++) {
    if (ptrA != ptrB) {
      if (equal(ptrA, ptrB, snakeX, snakeY)) {
        return true;
      }
    }
  }
  return false;
}

int next(int ptr, const int snakeLength) {
  return (ptr + 1) % snakeLength;
}

boolean equal(int ptrA, int ptrB, int *snakeX, int *snakeY) {
  return snakeX[ptrA] == snakeX[ptrB] && snakeY[ptrA] == snakeY[ptrB];
}
/* <= End Snake Routines */

void loadPreferences(void) {
  preferences.begin("clock", true);
  //preferences.clear();
  DingOnOff = preferences.getBool("DingOnOff", true);
  sWIFI_SSID = preferences.getString("ssid", WIFI_SSID);
  sWIFI_PASWD = preferences.getString("password", WIFI_PASWD);
  preferences.end();
}

void savePreferences(void) {
  preferences.begin("clock", false);
  //preferences.clear();
  preferences.putBool("DingOnOff", DingOnOff);
  preferences.putString("ssid", WiFi.SSID());
  preferences.putString("password", WiFi.psk());  
  preferences.end();
}

// -------------- Start WPS Functions --------------------------------------

#define ESP_WPS_MODE WPS_TYPE_PBC
#define ESP_MANUFACTURER "ESPRESSIF"
#define ESP_MODEL_NUMBER "ESP32"
#define ESP_MODEL_NAME "ESPRESSIF IOT"
#define ESP_DEVICE_NAME "ESP STATION"
static esp_wps_config_t config;

void wpsStart() {
  if (esp_wifi_wps_enable(&config)) {
    PRINTS("WPS Enable Failed\n");
  } else if (esp_wifi_wps_start(0)) {
    PRINTS("WPS Start Failed\n");
  }
}

void wpsStop() {
  if (esp_wifi_wps_disable()) {
    PRINTS("WPS Disable Failed\n");
  }
}

void WiFiEvent(WiFiEvent_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      PRINTS("Station Mode Started\n");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      PRINT("Connected to :", String(WiFi.SSID()));
      PRINTLN;
      PRINT("Got IP: ", WiFi.localIP());
      savePreferences();
      PRINTLN;
      while (true) {
        zoneInfo1.setText("", _SCROLL_RIGHT, _TO_FULL, InfoTick1, 54, 64);
        zoneInfo0.setText("OK, restarting", _SCROLL_LEFT, _TO_FULL, InfoTick1, 0, 64);
        zoneInfo0.Animate(true);
        PRINTS("Restarting ... in 3 seconds.");
        delay(3000);
        ESP.restart();
      }
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      PRINTS("Disconnected from station, attempting reconnection\n");
      WiFi.reconnect();
      break;
    case ARDUINO_EVENT_WPS_ER_SUCCESS:
      PRINT("WPS Successfull, stopping WPS and connecting to: ", String(WiFi.SSID()));
      PRINTLN;
      wpsStop();
      delay(10);
      WiFi.begin();
      break;
    case ARDUINO_EVENT_WPS_ER_FAILED:
      PRINTS("WPS Failed, retrying\n");
      wpsStop();
      wpsStart();
      break;
    case ARDUINO_EVENT_WPS_ER_TIMEOUT:
      PRINTS("WPS Timedout, retrying\n");
      wpsStop();
      wpsStart();
      break;
    case ARDUINO_EVENT_WPS_ER_PIN:
      // PRINT("WPS_PIN = ", wpspin2string(info.sta_er_pin.pin_code));PRINTLN;
      break;
    default:
      break;
  }
}


void wpsInitConfig() {
  config.wps_type = ESP_WPS_MODE;
  strcpy(config.factory_info.manufacturer, ESP_MANUFACTURER);
  strcpy(config.factory_info.model_number, ESP_MODEL_NUMBER);
  strcpy(config.factory_info.model_name, ESP_MODEL_NAME);
  strcpy(config.factory_info.device_name, ESP_DEVICE_NAME);
}

// -------------- End WPS Functions --------------------------------------


void setup() {
  //   #if  DEBUG_ON
  Serial.begin(115200);
  PRINTS("Clock started\n");
  //    #endif

  loadPreferences();

  // start serial interface for MP3 player
  DFPSerial.begin(9600, SERIAL_8N1, MP3RX, MP3TX);  // speed, type, RX, TX

  delay(250);  // Delay needed to initiate the serial interface(s)

  // SetUp Sensors
  Wire.begin(21, 22, 400000);
  //  ADDR=0 => 0x23 and ADDR=1 => 0x5C.
  lightMeter.begin();


  pinMode(sledPin, OUTPUT);        // passing seconds LED
  pinMode(PIN_BUT, INPUT_PULLUP);  // button to operate menu
  pinMode(modePin, INPUT_PULLUP);  // check setup
  pinMode(pirPin, INPUT);          // input from PIR sensor

  // matrix.setIntensity(0); // Use a value between 0 and 15 for brightness
  matrix.setIntensity(IntensityMap(lightMeter.readLightLevel()));

  matrix.setPosition(0, 7, 0);
  matrix.setRotation(0, 3);
  matrix.setPosition(1, 6, 0);
  matrix.setRotation(1, 3);
  matrix.setPosition(2, 5, 0);
  matrix.setRotation(2, 3);
  matrix.setPosition(3, 4, 0);
  matrix.setRotation(3, 3);
  matrix.setPosition(4, 3, 0);
  matrix.setRotation(4, 3);
  matrix.setPosition(5, 2, 0);
  matrix.setRotation(5, 3);
  matrix.setPosition(6, 1, 0);
  matrix.setRotation(6, 3);
  matrix.setPosition(7, 0, 0);
  matrix.setRotation(7, 3);

  matrix.fillScreen(LOW);
  matrix.setTextColor(HIGH, LOW);
  matrix.setTextWrap(false);

  IntensityCheck.start();

  zoneInfo0.setText("Starting Clock ...", _SCROLL_LEFT, _TO_FULL, InfoTick, I0s, I0e);
  zoneInfo0.Animate(true);

  wpsNeeded = false;
  if (digitalRead(modePin)) {
    zoneInfo0.setText("WPS?", _BLINK, _NONE_MOD, InfoTick1, I0s, I0e);
    for (int tmp = 0; (tmp < 30) && !wpsNeeded; tmp++) {
      if (zoneInfo0.Animate(false)) matrix.write();
      delay(100);
      if (!digitalRead(PIN_BUT)) wpsNeeded = true;
    }
    zoneInfo0.setText(" ", _SCROLL_LEFT, _TO_FULL, InfoTick, I0s, I0e);
    zoneInfo0.Animate(true);
  } else {
    sWIFI_SSID = WIFI_SSID;
    sWIFI_PASWD = WIFI_PASWD;
  }

  if (!wpsNeeded) {

    if (!DFPlayer.begin(DFPSerial)) {  //Use softwareSerial to communicate with mp3.
      PRINTX("MP3 Player failed", DFPlayer.readType());
      zoneInfo0.setText("MP3 Player failed", _SCROLL_LEFT, _TO_LEFT, InfoTick1, I0s, I0e);
      zoneInfo0.Animate(true);
    } else {
      DFPlayer.setTimeOut(500);
      DFPlayer.volume(30);
      DFPlayer.EQ(DFPLAYER_EQ_BASS);
      DFPlayer.outputDevice(DFPLAYER_DEVICE_SD);
    }
    DFPlayer.playFolder(3, 101);
    for (int wait = 10; wait >= 0; wait -= 1) {
      for (int x = 0; x < matrix.width() - 1; x++) {
        matrix.fillScreen(LOW);
        matrix.drawLine(x, 0, matrix.width() - 1 - x, matrix.height() - 1, HIGH);
        matrix.write();  // Send bitmap to display
        delay(wait);
      }
    }

    clearScreen();

    // Q.begin(); // start queue to collect button and rotery events
    xQueue = xQueueCreate(QUEUE_SIZE, sizeof(uint8_t));

    attachInterrupt(digitalPinToInterrupt(PIN_BUT), processButton, CHANGE);
    R.begin();  // start rotary decoder
    attachInterrupt(digitalPinToInterrupt(PIN_A), processEncoder, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_B), processEncoder, CHANGE);

    if (!mySensor.begin(0x76)) {
      PRINTS("Could not find a valid temp sensor, check wiring.\n");
      zoneInfo0.setText("BME Error", _SCROLL_LEFT, _TO_LEFT, InfoTick1, I0s, I0e);
      zoneInfo0.Animate(true);
      while (1)
        ;
    }
    mySensor.setSampling(Adafruit_BME280::MODE_NORMAL,
                         Adafruit_BME280::SAMPLING_X2,   // temperature
                         Adafruit_BME280::SAMPLING_X16,  // pressure
                         Adafruit_BME280::SAMPLING_X1,   // humidity
                         Adafruit_BME280::FILTER_X16,
                         Adafruit_BME280::STANDBY_MS_0_5);

    SensorUpdate.start(-1);

    // zero temperature table
    for (int i = 0; i < NumberOfPoints; tempTable[i++] = 0)
      ;
    for (int i = 0; i < NumberOfPoints; humiTable[i++] = 0)
      ;
    for (int i = 0; i < NumberOfPoints; presTable[i++] = 0)
      ;

    client.setInsecure();
    SetupWiFi();
    SyncNTPError = StartSyncNTP();
    if (!SyncNTPError) {
      /*
        if (SetUpDST(month(), day(), weekday())) {
          DSTFlag = true;
          timeClient.setTimeOffset(SECS_PER_HOUR*CEST_DELTA);
          adjustTime(+SECS_PER_HOUR*CEST_DELTA);
        } else {
          DSTFlag = false;
          timeClient.setTimeOffset(SECS_PER_HOUR*CET_DELTA);
          adjustTime(+SECS_PER_HOUR*CET_DELTA);
          //adjustTime(+48*60);
        }    
        // setTime(1512093784+60*56+40); // Friday, December 1, 2017 2:03:04 AM
        */
      PRINTS("             UTC Time=");
      PRINTS(timeClient.getFormattedTime());
      PRINTLN;
      PRINTS(digitalClockString());
      // correctByDST();
      // NTPUpdateTask.start( ((12-(hour()%12))*60 - minute())*60-second()+10*60 -NTPRESYNC );
      zoneInfo0.setText("Sync OK", _SCROLL_LEFT, _TO_LEFT, InfoTick1, I0s, I0e);
      zoneInfo0.Animate(true);
      delay(250);
    } else {
      PRINTS("Error at first NTP Sync\n");
      zoneInfo0.setText("NTP Sync ERROR", _SCROLL_LEFT, _TO_LEFT, InfoTick1, I0s, I0e);
      zoneInfo0.Animate(true);
    }
    NTPUpdateTask.start();

    SecondsVA.start();
    ScreenSaver.start();

    ChimeQuarter.start(((60 - minute() - ((int)(60 - minute()) / (int)15) * 15)) * 60 - second() - 15 * 60);
    ChimeHour.start((60 - minute()) * 60 - second() - 60 * 60);

    matrix.setClip(0, matrix.width(), 0, matrix.height());
    matrix.fillScreen(LOW);
    matrix.write();

    goBackState = _Clock_complete_info;
    ClockState = _Clock_complete_info_init;

    //ClockState = _Clock_simple_time_init;

    bufMutex = xSemaphoreCreateMutex();
    _t = mySensor.readTemperature();
    _h = mySensor.readHumidity();
    _p = mySensor.readPressure() / 100.0F;
    _l = lightMeter.readLightLevel();

    xTaskCreate(
      taskMQTT,   /* start regular  MQTT update task*/
      "taskMQTT", /* name of task. */
      8000,       /* Stack size of task */
      NULL,       /* parameter of the task */
      1,          /* priority of the task */
      NULL);      /* Task handle to keep track of created task */


    ArduinoOTA
      .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else  // U_SPIFFS
          type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Serial.println("Start updating " + type);
      })
      .onEnd([]() {
        Serial.println("\nEnd");
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
      });

    ArduinoOTA.begin();

  } else {
    PRINTS("WiFi WPS setup\n");
    zoneInfo0.setText("Start WPS", _SCROLL_LEFT, _TO_LEFT, InfoTick1, I0s, I0e);
    zoneInfo0.Animate(true);
    zoneInfo1.setText(">", _SCROLL_RIGHT, _TO_FULL, InfoTick1, 54, 64);
    // Start WPS
    PRINTS("Reseting WiFi Settings\n");
    WiFi.disconnect(true, true);
    delay(200);
    WiFi.onEvent(WiFiEvent);
    WiFi.mode(WIFI_MODE_STA);
    PRINTS("Starting WPS\n");
    wpsInitConfig();
    wpsStart();
  }
}

void loop() {
  time_t LocalTime;
  static unsigned long lastTimeMove;
  unsigned long voiceCoilDeta;

  ArduinoOTA.handle();

  if (wpsNeeded) {
    if (zoneInfo1.Animate(false)) matrix.write();
    if (zoneInfo1.AnimateDone()) zoneInfo1.Reset();
    delay(10);
  } else {
    // current and last time values
    static int valueH = 0;
    static int valueM = 0;
    static int valueS = 0;
    static int lvalueH = -1;
    static int lvalueM = -1;
    static int lvalueS = -1;

    static bool flasher = false;           // seconds passing flasher
    static uint8_t intensity = 0;          // brithness of the led matrix - all modules
    static uint8_t lintensity = 0;         // last brithness of the led matrix - all modules
    static boolean updateDisplay = false;  // Any change => display needs to be updated

    static char DataStr[] = "xx:xx:xx Xxx xx Xxx xxxx                ";
    // 0123456789012345678901234567891234567890
    // 0         2         3         4        5
    // 23:59:59 Sun 31 Oct 2016 100°C 1000HPa

    static uint8_t DataMode = 0;
    uint8_t key;

    // index for tables with measurements
    static uint8_t dayNumber = 0;
    static unsigned long measurementNumber = 1;

    static temp_t tempMin = +10000;
    static temp_t tempMax = -10000;
    static humi_t humiMin = +10000;
    static humi_t humiMax = -10000;
    static pres_t presMin = +10000;
    static pres_t presMax = -10000;

    temp_t tempValue;
    pres_t presValue;
    humi_t humiValue;
    int intValue;

    static temp_t averageTemp = 0;
    static humi_t averageHumi = 0;
    static pres_t averagePres = 0;

    textEffect_t textEffect;
    unsigned int infoTime;

    //    int16_t  tx1, ty1;
    //    uint16_t tw, th;
    //    boolean decPoint;

    // Snake variables
    static int snakeLength = 1;
    static int snakeX[MaxSnake], snakeY[MaxSnake];
    static int ptr, nextPtr;
    static int snakeRound = 0;
    static SnakeStates_t SnakeState;
    int attempt;
    boolean continueLoop = true;

    static byte VAValue;

    static String ParamS = DingOnOff ? "On" : "Off";


    if (SensorUpdate.check(MeasurementFreg)) {

      xSemaphoreTake(bufMutex, portMAX_DELAY);
      _t = mySensor.readTemperature();
      _h = mySensor.readHumidity();
      _p = mySensor.readPressure() / 100.0F;
      _l = lightMeter.readLightLevel();
      xSemaphoreGive(bufMutex);

      averageTemp = tempTable[dayNumber] + (_t - tempTable[dayNumber]) / measurementNumber;
      tempTable[dayNumber] = averageTemp;

      averagePres = presTable[dayNumber] + (_p - presTable[dayNumber]) / measurementNumber;
      presTable[dayNumber] = averagePres;

      averageHumi = humiTable[dayNumber] + (_h - humiTable[dayNumber]) / measurementNumber;
      humiTable[dayNumber] = averageHumi;


      //    PRINT("Day: ", dayNumber);
      //    PRINT("   Measuremeant: ", measurementNumber);
      //    PRINT("  Aver humi: ", humiTable[dayNumber]);
      //    PRINTLN;

      measurementNumber++;
      if (measurementNumber >= MaxMeasurements) {
        measurementNumber = 1;
        dayNumber++;
        if (ClockState == _Clock_Temp) ClockState = _Clock_Temp_init;
        if (dayNumber >= NumberOfPoints) {
          for (int ii = 0; ii <= NumberOfPoints - 2; ii++) {
            tempTable[ii] = tempTable[ii + 1];
            presTable[ii] = presTable[ii + 1];
            humiTable[ii] = humiTable[ii + 1];
          }
          tempTable[NumberOfPoints - 1] = 0;
          dayNumber = NumberOfPoints - 1;
        }
      }
    }

    if (second() == 59) {
      if (SecondsVA.check(VADelay)) {
        VAValue -= VADec;
        if (VAValue < MinV) VAValue = MinV;
        dacWrite(DACOut, VAValue);
      }
    } else {
      VAValue = MaxV;
      dacWrite(DACOut, map(second(), 0, 59, MinV, MaxV));
    }

    // check the light to setup matrix intensivity after a final write
    if (IntensityCheck.check(IntensityWait)) {
      uint16_t lux = lightMeter.readLightLevel();
      intensity = IntensityMap(lux);
      //PRINT("Light: ",lux);
      //PRINT(" lx  MAP:", intensity);
      //PRINTLN;
      if (intensity != lintensity) {
        //matrix.setIntensity(intensity);
        lintensity = intensity;
      }

      // check and activate screen saver mode max7219 -> shutdown mode and (re)set screenSaverNotActive flag for matrix.write
      if (digitalRead(pirPin)) {
        ScreenSaver.start();
        digitalWrite(sledPin, LOW);
        matrix.shutdown(false);
        screenSaverNotActive = true;
        lastTime = digitalClockString();
        lastTimeMove = millis();
        // dacWrite(DACVoiceCoil, voiceCoilMax);
      } else {
        if (ScreenSaver.check(ScreenTimeOut)) {
          digitalWrite(sledPin, HIGH);
          matrix.shutdown(true);
          screenSaverNotActive = false;
          // dacWrite(DACVoiceCoil, voiceCoilMin);
          PRINTS("ScreenSaver ON");
        } else {
          voiceCoilDeta = (unsigned long)(millis() - lastTimeMove);
          if (voiceCoilDeta <= ScreenTimeOut * 1000 && screenSaverNotActive) {
            //          PRINT("Delta:", voiceCoilDeta);
            //          PRINTLN;
            //          PRINT("Map  :", map(voiceCoilDeta, 0, ScreenTimeOut*1000, 90, 60));
            //          PRINTLN;
            //dacWrite(DACVoiceCoil, map(voiceCoilDeta, 0, ScreenTimeOut * 1000, voiceCoilMax, voiceCoilMin));
            ;
          }
        }
      }
    }

    // check if the last NTP sync was successful, if not reduce the resync time to 60s
    if (SyncNTPError) NTPSyncPeriod = 60;
    else NTPSyncPeriod = NTPRESYNC;
    // check if NTP sync is due?
    // If yes change clock status
    if (NTPUpdateTask.check(NTPSyncPeriod)) {
      ClockState = _Clock_NTP_Sync;
    }

    // for all status except ... check if the time of DST has came and if yes change the time accordingly
    if (ClockState != _Clock_init) {
      // correctByDST();
    }

    // check time dependant actions

    {
      LocalTime = CET.toLocal(now());
      int hourTemp = hour(LocalTime);
      if (ChimeQuarter.check(CHIMEQ)) {
        int fileNumber = minute(LocalTime) / 15;
        PRINT("Minute", minute(LocalTime));
        PRINT(" Play Qurter file number", fileNumber);
        PRINTLN;
        if (DingOnOff && hourTemp >= DingON && hourTemp < DingOFF) DFPlayer.playFolder(2, fileNumber);
        ChimeWait.start();
      }
      if (ChimeWait.check(CHIMEW)) {
        if (ChimeHour.check(CHIMEH)) {
          int fileNumber = hour(LocalTime) % 12;
          PRINT("Hour", hour(LocalTime));
          PRINT(" Play hour file number", fileNumber);
          PRINTLN;
          if (DingOnOff && hourTemp >= DingON && hourTemp < DingOFF) DFPlayer.playFolder(1, fileNumber);
        }
      }
    }

    // cehck the current clock / display status
    switch (ClockState) {
        //      case _Clock_init:
        //          ClockState = _Clock_simple_time_init;
        //          break;

      case _Clock_NTP_Sync:
        PRINTS(digitalClockString());
        if (StartSyncNTP()) {  // error by NTP sync
          zoneInfo0.setText("NTP Sync ERROR", _SCROLL_LEFT, _TO_FULL, InfoTick1, I0s, I0e);
          zoneInfo0.Animate(true);
          SyncNTPError = true;
        } else {
          zoneInfo0.setText("NTP Sync OK", _SCROLL_LEFT, _TO_FULL, InfoTick1, I0s, I0e);
          zoneInfo0.Animate(true);
          PRINTS("NTP sync OK, UTC=");
          PRINTS(timeClient.getFormattedTime());
          PRINTLN;
          // correctByDST();
          SyncNTPError = false;
        }
        ClockState = goBackState;
        PRINT("NTP Resync Completed\nNew mode=", ClockState);
        PRINTLN;
        PRINTS(digitalClockString());
        break;

      case _Clock_Temp_init:
        clearScreen();
        StatTask.start();
        tempMin = tempTable[0];
        tempMax = tempTable[0];
        for (ptr = 0; ptr < dayNumber; ptr++) {
          tempValue = tempTable[ptr];

          //            PRINT("tempValue : ", tempValue);
          //            PRINT("  min : ", tempMin);
          //            PRINT("  max : ", tempMax);
          //            PRINTLN;

          if (tempValue < tempMin) tempMin = tempValue;
          if (tempValue > tempMax) tempMax = tempValue;

          presValue = presTable[ptr];
          if (presValue < presMin) presMin = presValue;
          if (presValue > presMax) presMax = presValue;

          humiValue = humiTable[ptr];
          if (humiValue < humiMin) humiMin = humiValue;
          if (humiValue > humiMax) humiMax = humiValue;
        }
        if (tempMax - tempMin < 8) tempMax = tempMin + 8;
        if (presMax - presMin < 8) presMax = presMin + 8;
        if (humiMax - humiMin < 8) humiMax = humiMin + 8;

        goBackState = _Clock_Temp_init;
        ClockState = _Clock_Temp;
        DataMode = 0;
        break;

      case _Clock_Temp:

        if (StatTask.check(DiagramDelay)) {
          clearScreen();
          matrix.setCursor(0, 0);
          switch (DataMode) {
            case 0:
              matrix.print("T");
              break;
            case 1:
              matrix.print("P");
              break;
            case 2:
              matrix.print("H");
              break;
          }
          for (ptr = 0; ptr <= dayNumber; ptr++) {
            switch (DataMode) {
              case 0:
                intValue = map(tempTable[ptr], tempMin, tempMax, 0, 8);
                break;
              case 1:
                intValue = map(presTable[ptr], presMin, presMax, 0, 8);
                break;
              case 2:
                intValue = map(humiTable[ptr], humiMin, humiMax, 0, 8);
                break;
            }
            intValue = constrain(intValue, 0, 8);

            //              PRINT("i: ", ptr);
            //              PRINT("  Table: ", intValue);
            //              PRINT("  min : ", tempMin);
            //              PRINT("  max : ", tempMax);
            //              PRINTLN;

            matrix.drawFastVLine(ptr + ShiftDiagram, 8 - intValue, intValue, HIGH);
          }
          updateDisplay = true;
        }
        if (keyboard(_Clock_menu_init, _Clock_simple_time_init, _Clock_none, _Clock_none) == SW_UP) {
          DataMode = (DataMode + 1) % 3;
          StatTask.check(-DiagramDelay);
        }
        break;

      case _Clock_simple_time_init:

        clearScreen();
        LocalTime = CET.toLocal(now());
        valueH = hour(LocalTime);
        valueM = minute(LocalTime);
        valueS = second(LocalTime);
        lvalueH = valueH;
        lvalueM = valueM;
        lvalueS = valueS;

        matrix.drawChar(H1s, 0, (char)('0' + valueH / 10), HIGH, LOW, 1);
        matrix.drawChar(H0s, 0, (char)('0' + valueH % 10), HIGH, LOW, 1);
        matrix.drawChar(M1s, 0, (char)('0' + valueM / 10), HIGH, LOW, 1);
        matrix.drawChar(M0s, 0, (char)('0' + valueM % 10), HIGH, LOW, 1);
        matrix.drawChar(S1s, 0, (char)('0' + valueS / 10), HIGH, LOW, 1);
        matrix.drawChar(S0s, 0, (char)('0' + valueS % 10), HIGH, LOW, 1);
        matrix.drawPixel(M0e + 1, 0, HIGH);
        matrix.drawPixel(M0e + 1, 1, HIGH);
        matrix.drawPixel(H0e + 1, 2, HIGH);
        matrix.drawPixel(H0e + 1, 5, HIGH);
        matrix.drawPixel(H0e + 1, 7, SyncNTPError || !digitalRead(modePin));  // indicate if NTP sync error

        updateDisplay = true;

        //IntensityCheck.start();
        SnakeUpdate.start();
        randomSeed(analogRead(pinRandom));  // Initialize random generator
        SnakeState = _sInit;

        PRINTS("Simple Time Init closed\n");

        goBackState = ClockState;
        ClockState = _Clock_simple_time;
        break;

      case _Clock_simple_time:
        LocalTime = CET.toLocal(now());
        updateTtime(valueH, lvalueH, hour(LocalTime), 'H');
        updateTtime(valueM, lvalueM, minute(LocalTime), 'M');
        if (updateTtime(valueS, lvalueS, second(LocalTime), 'S')) {
          //            flasher = !flasher;
          //            digitalWrite(sledPin, flasher);
          updateDisplay = true;
          // PRINTS(digitalClockString());
        }
        updateDisplay |= zoneClockH0.Animate(false);
        updateDisplay |= zoneClockH1.Animate(false);
        updateDisplay |= zoneClockM0.Animate(false);
        updateDisplay |= zoneClockM1.Animate(false);
        updateDisplay |= zoneClockS1.Animate(false);
        updateDisplay |= zoneClockS0.Animate(false);

        // Snake animation
        if (SnakeUpdate.check(SnakeWait) || SnakeState == _sRunA) {
          matrix.setClip(SNs, SNe, 0, 8);
          updateDisplay = true;
          switch (SnakeState) {
            case _sInit:
              //                        PRINTS("\n Init");
              matrix.fillScreen(LOW);
              for (ptr = 0; ptr < snakeLength; ptr++) {
                snakeX[ptr] = SNs + (SNe - SNs) / 2;
                snakeY[ptr] = matrix.height() / 2;
              }
              nextPtr = 0;
              snakeLength = 1;
              snakeRound = 0;

              SnakeState = _sRunA;

              dacWrite(DACVoiceCoil, voiceCoilMax);

              break;

            case _sRunA:
              //                        PRINTS("\n State A");
              ptr = nextPtr;
              nextPtr = next(ptr, snakeLength);
              matrix.drawPixel(snakeX[ptr], snakeY[ptr], HIGH);  // Draw the head of the snake
              SnakeState = _sRunB;

              dacWrite(DACVoiceCoil, random(voiceCoilMin, voiceCoilMax));

              break;

            case _sRunB:
              //                        PRINTS("\n State B");
              if (!occupied(nextPtr, snakeLength, snakeX, snakeY)) {
                matrix.drawPixel(snakeX[nextPtr], snakeY[nextPtr], LOW);  // Remove the tail of the snake
              }

              continueLoop = true;
              for (attempt = 0; (attempt < SnakeAttempt) && continueLoop; attempt++) {
                // Jump at random one step up, down, left, or right
                switch (random(4)) {
                  case 0:
                    snakeX[nextPtr] = (snakeX[ptr] + 1 >= SNe) ? SNs : snakeX[ptr] + 1;
                    snakeY[nextPtr] = snakeY[ptr];
                    break;
                  case 1:
                    snakeX[nextPtr] = (snakeX[ptr] - 1 < SNs) ? SNe - 1 : snakeX[ptr] - 1;
                    snakeY[nextPtr] = snakeY[ptr];
                    break;
                  case 2:
                    snakeY[nextPtr] = (snakeY[ptr] + 1 > matrix.height() - 1) ? 0 : snakeY[ptr] + 1;
                    snakeX[nextPtr] = snakeX[ptr];
                    break;
                  case 3:
                    snakeY[nextPtr] = (snakeY[ptr] - 1 < 0) ? matrix.height() - 1 : snakeY[ptr] - 1;
                    snakeX[nextPtr] = snakeX[ptr];
                    break;
                }
                continueLoop = occupied(nextPtr, snakeLength, snakeX, snakeY);
              }
              if (attempt == SnakeAttempt) {
                matrix.fillScreen(HIGH);
                SnakeState = _sFail;
              } else {
                snakeRound = (snakeRound + 1) % SankeNextRound;
                if (snakeRound == 0) snakeLength = snakeLength + 1;
                if (snakeLength >= MaxSnake) {
                  matrix.fillScreen(HIGH);
                  SnakeState = _sFail;
                } else SnakeState = _sRunA;
              }
              dacWrite(DACVoiceCoil, random(voiceCoilMin, voiceCoilMax));
              break;
            case _sFail:
              //                        PRINTS("\n Fail");
              dacWrite(DACVoiceCoil, 0);
              SnakeState = _sInit;
              break;
          }
        }

        key = keyboard(_Clock_Temp_init, _Clock_complete_info_init, _Clock_none, _Clock_none);
        break;

      case _Clock_menu_init:
        clearScreen();
        zoneInfo0.setText("Menu:", _SCROLL_RIGHT, _TO_LEFT, InfoTick, I0s, I0e);
        zoneInfo0.Animate(true);
        updateDisplay = false;
        goBackState = _Clock_menu_init;
        ClockState = _Clock_menu;
        DataMode = 0;
        zoneInfo0.setText("Ding:", _PRINT, _NONE_MOD, InfoTick, MEs, MEe);
        zoneInfo1.setText(ParamS, _BLINK, _NONE_MOD, InfoSlow, PAs, PAe);
        break;

      case _Clock_menu:
        updateDisplay = zoneInfo1.Animate(false);
        if (keyboard(_Clock_complete_info_init, _Clock_Temp_init, _Clock_none, _Clock_none) == SW_UP) {
          DingOnOff = !(DingOnOff);
          ParamS = DingOnOff ? "On" : "Off";
          zoneInfo1.setText(ParamS, _BLINK, _NONE_MOD, InfoSlow, PAs, PAe);
          savePreferences();
          updateDisplay = true;
        }
        break;

      case _Clock_complete_info_init:

        clearScreen();
        LocalTime = CET.toLocal(now());
        valueH = hour(LocalTime);
        valueM = minute(LocalTime);
        valueS = second(LocalTime);
        lvalueH = valueH;
        lvalueM = valueM;
        lvalueS = valueS;

        matrix.drawChar(H1s, 0, (char)('0' + valueH / 10), HIGH, LOW, 1);
        matrix.drawChar(H0s, 0, (char)('0' + valueH % 10), HIGH, LOW, 1);
        matrix.drawChar(M1s, 0, (char)('0' + valueM / 10), HIGH, LOW, 1);
        matrix.drawChar(M0s, 0, (char)('0' + valueM % 10), HIGH, LOW, 1);
        matrix.drawPixel(H0e + 1, 7, SyncNTPError);  // indicate if NTP sync error
        PRINT("SyncNTPError =", SyncNTPError);
        PRINT("   Mode =", digitalRead(modePin));
        PRINTLN;

        updateDisplay = true;

        DataMode = 0;
        DataDisplayTask.start(-2000);

        //IntensityCheck.start();

        goBackState = ClockState;
        ClockState = _Clock_complete_info;
        PRINTS("Complete Time Init closed\n");

        break;

      case _Clock_complete_info:

        // PRINTS("Complete Info\n");
        LocalTime = CET.toLocal(now());
        updateTtime(valueH, lvalueH, hour(LocalTime), 'H');
        updateTtime(valueM, lvalueM, minute(LocalTime), 'M');
        if (updateTtime(valueS, lvalueS, second(LocalTime), 'S')) {
          flasher = !flasher;
          // digitalWrite(sledPin, flasher);
          updateDisplay = true;
          matrix.setClip(H0e + 1, H0e + 2, 0, 8);
          matrix.drawPixel(H0e + 1, 2, flasher);
          matrix.drawPixel(H0e + 1, 5, flasher);
        }
        updateDisplay |= zoneClockH0.Animate(false);
        updateDisplay |= zoneClockH1.Animate(false);
        updateDisplay |= zoneClockM0.Animate(false);
        updateDisplay |= zoneClockM1.Animate(false);

        if (DataDisplayTask.check(FullInfoDelay)) {
          switch (DataMode) {
            case 0:
              sprintf(DataStr, "%s%02d", monthShortStr(month(LocalTime)), day(LocalTime));
              textEffect = _SCROLL_LEFT;
              break;
            case 1:
              sprintf(DataStr, "%s%02d", dayShortStr(weekday(LocalTime)), day(LocalTime));
              textEffect = _SCROLL_LEFT;
              break;
            case 99:
              sprintf(DataStr, "%s", monthStr(month(LocalTime)));
              textEffect = _SCROLL_LEFT;
              break;
            case 2:
              // sprintf (DataStr, "%c%d%c", 160, (int)(mySensor.readTemperature()+0.5), 161);
              tempValue = round(mySensor.readTemperature());
              sprintf(DataStr, "%d%c", (int)(tempValue), 161);
              textEffect = tempValue > round(averageTemp) ? _SCROLL_UP : tempValue < round(averageTemp) ? _SCROLL_DOWN
                                                                                                        : _SCROLL_LEFT;
              break;
            case 3:
              //sprintf (DataStr, "%c%.0f%c", 162, mySensor.readPressure() / 100.0F, 163);

              presValue = round(mySensor.readPressure() / 100.0F);
              sprintf(DataStr, "%.0f%c", presValue, 163);
              textEffect = presValue > round(averagePres) ? _SCROLL_UP : presValue < round(averagePres) ? _SCROLL_DOWN
                                                                                                        : _SCROLL_LEFT;
              //textEffect = _SCROLL_LEFT;
              break;
            case 4:
              //sprintf (DataStr, "%c%d%%", 166, (int)(mySensor.readHumidity()+0.5));
              humiValue = round(mySensor.readHumidity());
              sprintf(DataStr, "%d%%", (int)(humiValue));

              //                 PRINT("Day:", dayNumber);
              //                 PRINT("  Srednia:", round(averageHumi));
              //                 PRINT("  Sensor: ", humiValue)
              //                 PRINTLN;

              textEffect = humiValue > round(averageHumi) ? _SCROLL_UP : humiValue < round(averageHumi) ? _SCROLL_DOWN
                                                                                                        : _SCROLL_LEFT;
              //textEffect = _SCROLL_LEFT;
              break;
            case 6:
              int measurement = hallRead();
              PRINT("Hall sensor measurement: ", measurement);
              sprintf(DataStr, "H:%02d", measurement);
              textEffect = _SCROLL_LEFT;
              break;
          }

          infoTime = textEffect == _SCROLL_LEFT ? InfoQuick : InfoSlow;
          zoneInfo1.setText(DataStr, textEffect, _TO_LEFT, infoTime, I1s, I1e);
          DataMode = (DataMode + 1) % 5;
        }
        updateDisplay |= zoneInfo1.Animate(false);
        if (keyboard(_Clock_simple_time_init, _Clock_menu_init, _Clock_none, _Clock_none) == SW_UP) {
          DataMode = (DataMode + 1) % 5;
          DataDisplayTask.start(-FullInfoDelay);
        }
        break;

        //      case _Clock_idle:
        //          break;

      default:;
    }


    if (updateDisplay) {
      if (screenSaverNotActive) {
        matrix.write();
        matrix.setIntensity(intensity);
      }
      updateDisplay = false;
    }
  }
}

void processButton() {
  static volatile uint8_t SWPrev = 1;

  uint8_t state = digitalRead(PIN_BUT);
  uint8_t x;
  if (state == 0) {
    if (SWPrev == 1) {
      SWPrev = 0;
      x = SW_DOWN;
      //Q.push((uint8_t *)&x);
      xQueueSendToBackFromISR(xQueue, (uint8_t *)&x, NULL);
      PRINTS("\nDown\n");
    }
  } else {
    if (SWPrev == 0) {
      SWPrev = 1;
      x = SW_UP;
      //Q.push((uint8_t *)&x);
      xQueueSendToBackFromISR(xQueue, (uint8_t *)&x, NULL);
      PRINTS("\nUp\n");
    }
  }
}

void processEncoder() {
  uint8_t x = R.read();
  if (x) {
    PRINTS((x == DIR_CW ? "\n+1\n" : "\n-1\n"));
    //Q.push(&x);
    xQueueSendToBackFromISR(xQueue, (uint8_t *)&x, NULL);
  }
}


void taskMQTT(void *parameter) {

  const TickType_t xTicksToWait = pdMS_TO_TICKS(Time2UpdateMQTT);
  float _tt, _hh, _pp, _ll;

  UBaseType_t uxHighWaterMark;

  while (true) {
    uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    // Serial.print("\nStack IN:"); Serial.println(uxHighWaterMark);
    vTaskDelay(xTicksToWait);
    PRINTS("MQTT publishing\n");
    if (MQTT_connect()) {
      xSemaphoreTake(bufMutex, portMAX_DELAY);
      _tt = _t;
      _hh = _h;
      _pp = _p;
      _ll = _l;
      xSemaphoreGive(bufMutex);
      tempMQTT.publish(_tt);
      humiMQTT.publish(_hh);
      presMQTT.publish(_pp);
      brigMQTT.publish(_ll);
      dataMQTT.publish(lastTime.c_str());
      onofMQTT.publish(screenSaverNotActive ? 1 : 0);
      //      PRINT("P", _p); PRINTLN;
      //      PRINT("H", _h ); PRINTLN;
      //      PRINT("T", _t ); PRINTLN;
      //      PRINT("L", _l ); PRINTLN;
    }
    uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    // Serial.print("\nStack OUT:"); Serial.println(uxHighWaterMark);
  }
}

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
boolean MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return true;
  }

  if (!WiFi.isConnected()) {
    PRINTS("Connecting to WIFI... ");
    WiFi.begin();
    delay(1000);
  }

  PRINTS("Connecting to MQTT... ");
  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0 && (retries > 0)) {  // connect will return 0 for connected
    PRINTS(mqtt.connectErrorString(ret));
    PRINTLN;
    PRINTS("Retrying MQTT connection in 5 seconds...\n");
    mqtt.disconnect();
    delay(5000);  // wait 5 seconds
    retries--;
  }
  if (ret == 0) {
    PRINTS("MQTT Connected!\n");
    return true;
  } else return false;
}

String digitalClockString() {
  // digital clock display of the time
  time_t LocalTime = CET.toLocal(now());
  return String(hour(LocalTime)) + printDigits(minute(LocalTime)) + printDigits(second(LocalTime)) + " " + String(day(LocalTime)) + "." + String(month(LocalTime)) + "." + String(year(LocalTime)) + "\n";
}

String printDigits(int digits) {
  // utility function for digital clock display: prints preceding colon and leading 0
  return String(":" + (digits < 10 ? String("0") : String("")) + String(digits));
}
