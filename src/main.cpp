#include <Arduino.h>
#include "myClock.h"
#include "display_state.h"
#include <TimeLib.h>
#include <Timezone.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>

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
#include "sky_stars_mode.h"

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
// Keep the internal clock in UTC. Local time, including DST, is derived via
// the Timezone rules below whenever the UI or logic needs local wall-clock time.
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 0);
unsigned long NTPSyncPeriod = NTPRESYNC;
boolean SyncNTPError = false;  // true if the last NTP was finished with an error due the wifi or ntp failure

ClockStates ClockState = _Clock_init;   // current clock status
ClockStates goBackState = _Clock_init;  // last clock status

TimeChangeRule rCEST = { "CEST", Last, Sun, Mar, 2, 120 };  // Central European Summer Time
TimeChangeRule rCET = { "CET", Last, Sun, Oct, 3, 60 };     // Central European Time
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
Schedular RemoteMessageRepeat(_Millis);
Schedular RemoteMessageMotion(_Millis);
Schedular DisplayOffFade(_Millis);
Schedular WakeGreetingDisplay(_Millis);
Schedular AlarmRepeat(_Millis);
Schedular AlarmAnimation(_Millis);
Schedular AlarmFinalDisplay(_Millis);

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
Adafruit_MQTT_Publish msgMQTTPub = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME feedMsg);
WebServer webServer(80);

// Setup a feed for subscribing to changes.
Adafruit_MQTT_Subscribe ledMQTT = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME feedLED);
Adafruit_MQTT_Subscribe msgMQTTSub = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME feedMsg);

// Bug workaround for Arduino 1.6.6, it seems to need a function declaration
// for some reason (only affects ESP8266, likely an arduino-builder bug).
boolean MQTT_connect();
void clearScreen(void);
void showOtaStartMessage(void);
boolean SyncNTP();
boolean StartSyncNTP();
void SetupWiFi(void);
bool waitForNetworkReady(const char *hostname, unsigned long timeoutMs);
uint8_t IntensityMap(uint16_t sensor);
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
String formatTimeStamp(time_t stamp);
String formatLocalTimeStamp(time_t utc);
void printClockTimes(const char *prefix);
void rememberTimezoneState();
void announceTimezoneChange();
void applyDstTestTime();
time_t buildUtcTime(uint16_t yearValue, uint8_t monthValue, uint8_t dayValue, uint8_t hourValue, uint8_t minuteValue, uint8_t secondValue);
uint8_t lastSundayOfMonth(uint16_t yearValue, uint8_t monthValue);
ClockStates normalizeClockStateForReturn(ClockStates state);
uint32_t buildLocalDateKey(time_t localTime);
void resetWakeGreetingFlagsIfNeeded(time_t localTime);
time_t getWakeGreetingLocalTime(time_t actualLocalTime);
void printWakeGreetingTestConfiguration(time_t actualLocalTime);
bool prepareWakeGreeting(time_t localTime, unsigned long inactivityMs);
#if EnableSoundTestMode
void resetSoundTestSelection();
void advanceSoundTestSelection();
void updateSoundTestDisplayText(DisplayState &display);
void playCurrentSoundTestSelection();
void stopSoundTestPlayback();
#endif
void queueRemoteMessage(const char *message);
bool hasPendingRemoteMessage();
bool takePendingRemoteMessage();
void queueRemoteMessageAck(const char *message);
bool publishQueuedRemoteMessageAck();
void queueRemoteMessageClear(const char *message);
bool publishQueuedRemoteMessageClear();
bool fetchLastRemoteMessage(String &message);
String extractJsonStringField(const String &json, const char *fieldName);
bool isRemoteMessageEmpty(const char *message);
bool isRemoteMessageEmpty(const String &message);
void rememberLastRemoteMessage(const char *message);
bool copyLastRemoteMessage(String &message);
String formatAlarmTime(uint8_t hourValue, uint8_t minuteValue);
String buildAlarmMenuText();
void refreshAlarmMenuText(DisplayState &display);
void restoreDisplayOutput(DisplayState &display);
bool isAlarmState(ClockStates state);
bool isAlarmActiveState(ClockStates state);
void startAlarmTakeover(DisplayState &display, ClockStates returnState);
void triggerAlarmBurst(DisplayState &display);
void drawAlarmAnimation(DisplayState &display, bool forceRedraw);
void finishAlarmTakeover(DisplayState &display, bool stopAudio);
bool parseAlarmTimeValue(const String &value, uint8_t &hourValue, uint8_t &minuteValue);
void setAlarmConfig(bool enabled, uint8_t hourValue, uint8_t minuteValue, bool persist);
void stopDfPlayerPlayback();
bool applyRemoteAlarmCommand(const char *message);
void applyQueuedAlarmCommand();
void processIncomingRemoteMessage(const char *message);
String clockModeName(ClockStates state);
bool applyRequestedMode(const String &mode, String &error);
String jsonEscape(const String &value);
String buildWebStatusJson();
void sendJsonResponse(int statusCode, const String &body);
void setupWebServer();
void handleWebRoot();
void handleWebStatus();
void handleWebAlarmSave();
void handleWebAlarmStop();
void handleWebMode();
ClockStates resolveModeChangeCoilReadyState(ClockStates state);
void armModeChangeCoilPulse(ClockStates targetState);
void cancelModeChangeCoilPulse();
void triggerModeChangeCoilPulse();
void updatePendingModeChangeCoilPulse();
void wakeDisplayToMenu(DisplayState &display);


//globals for screensaver status main loop and MQTT updates
bool screenSaverNotActive = true;
bool displayEnabled = true;
String lastTime = "";
char currentTimezoneAbbrev[6] = "";
bool timezoneStateKnown = false;
bool pendingTimezoneRefresh = false;
DisplayState displayState;
SemaphoreHandle_t messageMutex;
char pendingRemoteMessage[161] = "";
char activeRemoteMessage[161] = "";
char pendingRemoteAck[96] = "";
char pendingRemoteClearMessage[161] = "";
char lastKnownRemoteMessage[161] = "";
char pendingAlarmCommand[32] = "";
bool remoteMessagePending = false;
bool remoteMessageActive = false;
bool remoteMessageAckPending = false;
bool remoteMessageClearPending = false;
bool alarmCommandPending = false;
bool remoteMessageWaitingForRepeat = false;
bool remoteMessageMotionActive = false;
bool clockReadyForRemoteMessages = false;
ClockStates remoteMessageReturnState = _Clock_complete_info_init;
ClockStates alarmReturnState = _Clock_complete_info_init;
bool morningGreetingShownToday = false;
bool afternoonGreetingShownToday = false;
uint32_t wakeGreetingDayKey = 0;
char wakeGreetingMessage[32] = "";
uint8_t wakeGreetingSoundFolder = 0;
uint16_t wakeGreetingSoundFile = 0;
bool alarmEnabled = false;
uint8_t alarmHour = 7;
uint8_t alarmMinute = 0;
uint32_t alarmLastTriggeredDayKey = 0;
uint16_t alarmLastTriggeredMinuteOfDay = 0xFFFF;
uint8_t alarmBurstCount = 0;
uint8_t alarmAnimationFrame = 0;
bool alarmFinalReturnPending = false;
bool modeChangeCoilPulsePending = false;
ClockStates modeChangeCoilPulseReadyState = _Clock_none;
unsigned long lastModeChangeCoilPulseAt = 0;

#if EnableSoundTestMode
struct SoundTestCatalogEntry {
  uint8_t folder;
  uint16_t firstTrack;
  uint16_t lastTrack;
  bool useLargeFolder;
};

static const SoundTestCatalogEntry soundTestCatalog[] = {
  {1, 0, 11, false},
  {2, 0, 3, false},
  {3, 101, 102, false},
  {4, 0, 3, false},
  {5, 0, 1, false},
};

static const size_t soundTestCatalogCount = sizeof(soundTestCatalog) / sizeof(soundTestCatalog[0]);
uint8_t soundTestCatalogIndex = 0;
uint16_t soundTestTrack = 0;
#endif


// Parameter section, just one for the time being
Preferences preferences;
boolean DingOnOff = true;
uint8_t displayFadeIntensity = 0;
SkyStarsMode skyStarsMode;


// PWM data for HDD voice coil
// Keep the motion range away from the mechanical end stops. The arm is only
// driven strongly during the startup kick; regular motion stays within the
// quieter range and 0 fully releases the coil.
#define DACVoiceCoil 26
#define voiceCoilOff 0
#define voiceCoilKick 130
#define voiceCoilLow 20
#define voiceCoilMid 75
#define voiceCoilHigh 95
#define voiceCoilWiFiStart voiceCoilHigh
#define voiceCoilWiFiEnd voiceCoilOff
#define voiceCoilKickMs 90
#define voiceCoilReleaseMs 120
#define voiceCoilSettleMs 80
#define voiceCoilMotionMinMs 45
#define voiceCoilMotionMaxMs 120
#define voiceCoilHighPulseChance 6
#define voiceCoilModeChangePulseCooldownMs 250

enum VoiceCoilState : uint8_t {
  _vcOff,
  _vcKickHigh,
  _vcKickRelease,
  _vcRandomMotion,
  _vcModePulseHigh,
  _vcModePulseRelease
};

VoiceCoilState voiceCoilState = _vcOff;
Schedular VoiceCoilMotion(_Millis);
long voiceCoilPeriod = voiceCoilSettleMs;

void setVoiceCoilLevel(uint8_t level) {
  dacWrite(DACVoiceCoil, level);
}

void stopVoiceCoilMotion() {
  voiceCoilState = _vcOff;
  voiceCoilPeriod = voiceCoilSettleMs;
  setVoiceCoilLevel(voiceCoilOff);
}

uint8_t nextVoiceCoilLevel(uint8_t currentLevel) {
  // Use a few distinct levels inside the safe motion window.
  static const uint8_t levels[] = { voiceCoilLow, voiceCoilMid, voiceCoilHigh };
  uint8_t nextLevel = currentLevel;

  while (nextLevel == currentLevel) {
    nextLevel = levels[random(0, 3)];
  }

  // Occasionally jump to the top of the safe motion range.
  if (currentLevel != voiceCoilHigh && random(0, voiceCoilHighPulseChance) == 0) {
    nextLevel = voiceCoilHigh;
  }

  return nextLevel;
}

ClockStates normalizeClockStateForReturn(ClockStates state) {
  switch (state) {
    case _Clock_simple_time_init:
    case _Clock_simple_time:
      return _Clock_simple_time_init;
    case _Clock_complete_info_init:
    case _Clock_complete_info:
      return _Clock_complete_info_init;
    case _Clock_Temp_init:
    case _Clock_Temp:
      return _Clock_Temp_init;
    case _Clock_alarm_menu_init:
    case _Clock_alarm_menu:
      return _Clock_alarm_menu_init;
    case _Clock_alarm_active_init:
    case _Clock_alarm_active:
      return alarmReturnState;
    case _Clock_ip_init:
    case _Clock_ip:
      return _Clock_ip_init;
    case _Clock_wake_greeting_init:
    case _Clock_wake_greeting:
      return _Clock_complete_info_init;
#if EnableSoundTestMode
    case _Clock_sound_test_init:
    case _Clock_sound_test:
      return _Clock_sound_test_init;
#endif
    case _Clock_menu_init:
    case _Clock_menu:
      return _Clock_menu_init;
    case _Clock_menu_display_init:
    case _Clock_menu_display:
      return _Clock_menu_display_init;
    case _Clock_sky_stars_init:
    case _Clock_sky_stars:
      return _Clock_sky_stars_init;
    case _Clock_display_off_init:
    case _Clock_display_off:
      return _Clock_display_off_init;
    case _Clock_remote_message_init:
    case _Clock_remote_message:
      return remoteMessageReturnState;
    default:
      return _Clock_complete_info_init;
  }
}

ClockStates resolveModeChangeCoilReadyState(ClockStates state) {
  switch (state) {
    case _Clock_simple_time_init:
    case _Clock_simple_time:
      return _Clock_simple_time;
    case _Clock_complete_info_init:
    case _Clock_complete_info:
      return _Clock_complete_info;
    case _Clock_Temp_init:
    case _Clock_Temp:
      return _Clock_Temp;
    case _Clock_alarm_menu_init:
    case _Clock_alarm_menu:
      return _Clock_alarm_menu;
    case _Clock_ip_init:
    case _Clock_ip:
      return _Clock_ip;
    case _Clock_menu_init:
    case _Clock_menu:
      return _Clock_menu;
    case _Clock_menu_display_init:
    case _Clock_menu_display:
      return _Clock_menu_display;
    case _Clock_sky_stars_init:
    case _Clock_sky_stars:
      return _Clock_sky_stars;
    case _Clock_display_off_init:
    case _Clock_display_off:
      return _Clock_display_off;
#if EnableSoundTestMode
    case _Clock_sound_test_init:
    case _Clock_sound_test:
      return _Clock_sound_test;
#endif
    default:
      return _Clock_none;
  }
}

bool isClockStateInitState(ClockStates state) {
  switch (state) {
    case _Clock_simple_time_init:
    case _Clock_complete_info_init:
    case _Clock_Temp_init:
    case _Clock_alarm_menu_init:
    case _Clock_alarm_active_init:
    case _Clock_ip_init:
    case _Clock_menu_init:
    case _Clock_menu_display_init:
    case _Clock_sky_stars_init:
    case _Clock_display_off_init:
    case _Clock_wake_greeting_init:
    case _Clock_remote_message_init:
#if EnableSoundTestMode
    case _Clock_sound_test_init:
#endif
      return true;
    default:
      return false;
  }
}

bool isAlarmState(ClockStates state) {
  return state == _Clock_alarm_menu_init || state == _Clock_alarm_menu
         || state == _Clock_alarm_active_init || state == _Clock_alarm_active;
}

bool isAlarmActiveState(ClockStates state) {
  return state == _Clock_alarm_active_init || state == _Clock_alarm_active;
}

String formatAlarmTime(uint8_t hourValue, uint8_t minuteValue) {
  char timeBuffer[6];
  snprintf(timeBuffer, sizeof(timeBuffer), "%02u:%02u", hourValue, minuteValue);
  return String(timeBuffer);
}

String buildAlarmMenuText() {
  return formatAlarmTime(alarmHour, alarmMinute) + (alarmEnabled ? " ON" : " OFF");
}

String clockModeName(ClockStates state) {
  switch (state) {
    case _Clock_complete_info_init:
    case _Clock_complete_info:
      return "normal";
    case _Clock_simple_time_init:
    case _Clock_simple_time:
      return "snake";
    case _Clock_sky_stars_init:
    case _Clock_sky_stars:
      return "sky";
    case _Clock_display_off_init:
    case _Clock_display_off:
      return "off";
    case _Clock_Temp_init:
    case _Clock_Temp:
      return "temp";
    case _Clock_alarm_menu_init:
    case _Clock_alarm_menu:
      return "alarm-menu";
    case _Clock_alarm_active_init:
    case _Clock_alarm_active:
      return "alarm-active";
    case _Clock_menu_init:
    case _Clock_menu:
      return "ding";
    case _Clock_menu_display_init:
    case _Clock_menu_display:
      return "led";
    case _Clock_ip_init:
    case _Clock_ip:
      return "ip";
    case _Clock_remote_message_init:
    case _Clock_remote_message:
      return "remote-message";
    case _Clock_wake_greeting_init:
    case _Clock_wake_greeting:
      return "wake-greeting";
#if EnableSoundTestMode
    case _Clock_sound_test_init:
    case _Clock_sound_test:
      return "sound-test";
#endif
    case _Clock_NTP_Sync:
      return "ntp-sync";
    default:
      return "unknown";
  }
}

uint32_t buildLocalDateKey(time_t localTime) {
  return (uint32_t)year(localTime) * 10000UL + (uint32_t)month(localTime) * 100UL + (uint32_t)day(localTime);
}

void restoreDisplayOutput(DisplayState &display) {
  displayEnabled = true;
  screenSaverNotActive = true;
  digitalWrite(sledPin, LOW);
  matrix.shutdown(false);
  ScreenSaver.start();
  const uint16_t lux = lightMeter.readLightLevel();
  display.intensity = IntensityMap(lux);
  display.lintensity = display.intensity;
  matrix.setIntensity(display.intensity);
}

void refreshAlarmMenuText(DisplayState &display) {
  display.paramS = buildAlarmMenuText();
  zoneInfo0.setText(display.paramS, _BLINK, _NONE_MOD, InfoSlow, I0s, I0e);
}

bool parseAlarmTimeValue(const String &value, uint8_t &hourValue, uint8_t &minuteValue) {
  String trimmed = value;
  trimmed.trim();
  const int separator = trimmed.indexOf(':');
  if (separator <= 0 || separator >= trimmed.length() - 1) return false;

  const int parsedHour = trimmed.substring(0, separator).toInt();
  const int parsedMinute = trimmed.substring(separator + 1).toInt();
  if (parsedHour < 0 || parsedHour > 23 || parsedMinute < 0 || parsedMinute > 59) return false;

  hourValue = (uint8_t)parsedHour;
  minuteValue = (uint8_t)parsedMinute;
  return true;
}

void setAlarmConfig(bool enabled, uint8_t hourValue, uint8_t minuteValue, bool persist) {
  alarmEnabled = enabled;
  alarmHour = hourValue;
  alarmMinute = minuteValue;
  if (persist) savePreferences();
}

bool applyRequestedMode(const String &mode, String &error) {
  String normalized = mode;
  normalized.trim();
  normalized.toLowerCase();

  if (isAlarmActiveState(ClockState)) {
    error = "alarm active";
    return false;
  }

  if (normalized == "off") {
    if (resolveModeChangeCoilReadyState(ClockState) == _Clock_display_off) return true;
    ClockState = _Clock_display_off_init;
    armModeChangeCoilPulse(_Clock_display_off_init);
    return true;
  }

  if (normalized == "normal") {
    if (resolveModeChangeCoilReadyState(ClockState) == _Clock_complete_info) return true;
    restoreDisplayOutput(displayState);
    ClockState = _Clock_complete_info_init;
    armModeChangeCoilPulse(_Clock_complete_info_init);
    return true;
  }

  if (normalized == "snake") {
    if (resolveModeChangeCoilReadyState(ClockState) == _Clock_simple_time) return true;
    restoreDisplayOutput(displayState);
    ClockState = _Clock_simple_time_init;
    armModeChangeCoilPulse(_Clock_simple_time_init);
    return true;
  }

  if (normalized == "sky") {
    if (resolveModeChangeCoilReadyState(ClockState) == _Clock_sky_stars) return true;
    restoreDisplayOutput(displayState);
    ClockState = _Clock_sky_stars_init;
    armModeChangeCoilPulse(_Clock_sky_stars_init);
    return true;
  }

  error = "unsupported mode";
  return false;
}

void armModeChangeCoilPulse(ClockStates targetState) {
  const ClockStates readyState = resolveModeChangeCoilReadyState(targetState);
  if (readyState == _Clock_none) return;

  modeChangeCoilPulseReadyState = readyState;
  modeChangeCoilPulsePending = true;
}

void cancelModeChangeCoilPulse() {
  modeChangeCoilPulsePending = false;
  modeChangeCoilPulseReadyState = _Clock_none;
}

void triggerModeChangeCoilPulse() {
  const unsigned long now = millis();
  if (now - lastModeChangeCoilPulseAt < voiceCoilModeChangePulseCooldownMs) return;

  lastModeChangeCoilPulseAt = now;
  voiceCoilState = _vcModePulseHigh;
  setVoiceCoilLevel(voiceCoilKick);
  voiceCoilPeriod = voiceCoilKickMs;
  VoiceCoilMotion.start();
}

void updatePendingModeChangeCoilPulse() {
  if (!modeChangeCoilPulsePending) return;

  switch (ClockState) {
    case _Clock_remote_message_init:
    case _Clock_remote_message:
    case _Clock_alarm_active_init:
    case _Clock_alarm_active:
    case _Clock_NTP_Sync:
      cancelModeChangeCoilPulse();
      return;
    default:
      break;
  }

  if (ClockState == modeChangeCoilPulseReadyState) {
    triggerModeChangeCoilPulse();
    cancelModeChangeCoilPulse();
    return;
  }

  if (!isClockStateInitState(ClockState)) {
    cancelModeChangeCoilPulse();
  }
}

void resetWakeGreetingFlagsIfNeeded(time_t localTime) {
  const uint32_t localDayKey = buildLocalDateKey(localTime);
  if (wakeGreetingDayKey != localDayKey) {
    wakeGreetingDayKey = localDayKey;
    morningGreetingShownToday = false;
    afternoonGreetingShownToday = false;
  }
}

time_t getWakeGreetingLocalTime(time_t actualLocalTime) {
#if WakeGreetingTestMode == 1 || WakeGreetingTestMode == 2
  tmElements_t tm;
  breakTime(actualLocalTime, tm);
  tm.Hour = WakeGreetingTestMode == 1 ? WakeGreetingMorningStartHour : WakeGreetingAfternoonStartHour;
  tm.Minute = 30;
  tm.Second = 0;
  return makeTime(tm);
#else
  return actualLocalTime;
#endif
}

void printWakeGreetingTestConfiguration(time_t actualLocalTime) {
#if WakeGreetingTestMode == 1 || WakeGreetingTestMode == 2
  const time_t mockLocalTime = getWakeGreetingLocalTime(actualLocalTime);
  PRINTS("Wake greeting test mode active: ");
  PRINTS(WakeGreetingTestMode == 1 ? "morning" : "afternoon");
  PRINTS(" screenSaver=");
  PRINTS(ScreenTimeOut);
  PRINTS("s idle=");
  PRINTS(WakeGreetingTestMode == 1 ? WakeGreetingMorningIdleSeconds : WakeGreetingAfternoonIdleSeconds);
  PRINTS("s local=");
  PRINTS(formatTimeStamp(mockLocalTime));
  PRINTLN;
#else
  (void)actualLocalTime;
#endif
}

bool prepareWakeGreeting(time_t localTime, unsigned long inactivityMs) {
  if (ClockState != _Clock_complete_info && ClockState != _Clock_complete_info_init) return false;

  if (hour(localTime) >= WakeGreetingMorningStartHour && hour(localTime) < WakeGreetingMorningEndHour) {
    if (!morningGreetingShownToday && inactivityMs >= WakeGreetingMorningIdleSeconds * 1000UL) {
      randomSeed(analogRead(pinRandom) + micros());
      strncpy(wakeGreetingMessage, "Good Morning", sizeof(wakeGreetingMessage) - 1);
      wakeGreetingMessage[sizeof(wakeGreetingMessage) - 1] = '\0';
      wakeGreetingSoundFolder = WakeGreetingMorningSoundFolder;
      wakeGreetingSoundFile = random(WakeGreetingMorningSoundFirst, WakeGreetingMorningSoundLast + 1);
      morningGreetingShownToday = true;
      PRINT("Wake greeting prepared: morning track ", wakeGreetingSoundFile);
      PRINTLN;
      return true;
    }
    if (morningGreetingShownToday) PRINTS("Wake greeting skipped: morning already shown today\n");
  }

  if (hour(localTime) >= WakeGreetingAfternoonStartHour && hour(localTime) < WakeGreetingAfternoonEndHour) {
    if (!afternoonGreetingShownToday && inactivityMs >= WakeGreetingAfternoonIdleSeconds * 1000UL) {
      randomSeed(analogRead(pinRandom) + micros());
      strncpy(wakeGreetingMessage, "Great to see you again", sizeof(wakeGreetingMessage) - 1);
      wakeGreetingMessage[sizeof(wakeGreetingMessage) - 1] = '\0';
      wakeGreetingSoundFolder = WakeGreetingAfternoonSoundFolder;
      wakeGreetingSoundFile = random(WakeGreetingAfternoonSoundFirst, WakeGreetingAfternoonSoundLast + 1);
      afternoonGreetingShownToday = true;
      PRINT("Wake greeting prepared: afternoon track ", wakeGreetingSoundFile);
      PRINTLN;
      return true;
    }
    if (afternoonGreetingShownToday) PRINTS("Wake greeting skipped: afternoon already shown today\n");
  }

  return false;
}

void stopDfPlayerPlayback() {
  DFPlayer.disableLoop();
  DFPlayer.disableLoopAll();
  DFPlayer.stopAdvertise();
  DFPlayer.stop();
  DFPlayer.pause();
}

#if EnableSoundTestMode
void resetSoundTestSelection() {
  soundTestCatalogIndex = 0;
  soundTestTrack = soundTestCatalog[0].firstTrack;
}

void advanceSoundTestSelection() {
  const SoundTestCatalogEntry &entry = soundTestCatalog[soundTestCatalogIndex];
  if (soundTestTrack < entry.lastTrack) {
    soundTestTrack++;
    return;
  }

  soundTestCatalogIndex = (soundTestCatalogIndex + 1) % soundTestCatalogCount;
  soundTestTrack = soundTestCatalog[soundTestCatalogIndex].firstTrack;
}

void updateSoundTestDisplayText(DisplayState &display) {
  char label[16];
  snprintf(label, sizeof(label), "Play %u:%03u", soundTestCatalog[soundTestCatalogIndex].folder, (unsigned int)soundTestTrack);
  display.paramS = label;
}

void playCurrentSoundTestSelection() {
  const SoundTestCatalogEntry &entry = soundTestCatalog[soundTestCatalogIndex];
  if (entry.useLargeFolder) {
    DFPlayer.playLargeFolder(entry.folder, soundTestTrack);
  } else {
    DFPlayer.playFolder(entry.folder, (uint8_t)soundTestTrack);
  }
}

void stopSoundTestPlayback() {
  stopDfPlayerPlayback();
}
#endif

void wakeDisplayToMenu(DisplayState &display) {
  const uint16_t lux = lightMeter.readLightLevel();
  restoreDisplayOutput(display);
  PRINT_BRIGHTNESS("[Brightness] wake lux=");
  PRINT_BRIGHTNESS_VALUE("", lux);
  PRINT_BRIGHTNESS_VALUE(" mapped=", display.intensity);
  PRINT_BRIGHTNESS_LN();
  ClockState = _Clock_menu_display_init;
}

void rememberLastRemoteMessage(const char *message) {
  if (!message || isRemoteMessageEmpty(message)) return;

  if (!messageMutex) {
    strncpy(lastKnownRemoteMessage, message, sizeof(lastKnownRemoteMessage) - 1);
    lastKnownRemoteMessage[sizeof(lastKnownRemoteMessage) - 1] = '\0';
    return;
  }

  xSemaphoreTake(messageMutex, portMAX_DELAY);
  strncpy(lastKnownRemoteMessage, message, sizeof(lastKnownRemoteMessage) - 1);
  lastKnownRemoteMessage[sizeof(lastKnownRemoteMessage) - 1] = '\0';
  xSemaphoreGive(messageMutex);
}

bool copyLastRemoteMessage(String &message) {
  char messageCopy[sizeof(lastKnownRemoteMessage)];

  if (!messageMutex) {
    message = String(lastKnownRemoteMessage);
    return !isRemoteMessageEmpty(message);
  }

  xSemaphoreTake(messageMutex, portMAX_DELAY);
  strncpy(messageCopy, lastKnownRemoteMessage, sizeof(messageCopy) - 1);
  messageCopy[sizeof(messageCopy) - 1] = '\0';
  xSemaphoreGive(messageMutex);

  message = String(messageCopy);
  return !isRemoteMessageEmpty(message);
}

bool applyRemoteAlarmCommand(const char *message) {
  if (!message || isRemoteMessageEmpty(message)) return false;

  String command = message;
  command.trim();
  String lowerCommand = command;
  lowerCommand.toLowerCase();
  if (!lowerCommand.startsWith("alarm:")) return false;

  if (!messageMutex) {
    strncpy(pendingAlarmCommand, message, sizeof(pendingAlarmCommand) - 1);
    pendingAlarmCommand[sizeof(pendingAlarmCommand) - 1] = '\0';
    alarmCommandPending = true;
    return true;
  }

  xSemaphoreTake(messageMutex, portMAX_DELAY);
  strncpy(pendingAlarmCommand, message, sizeof(pendingAlarmCommand) - 1);
  pendingAlarmCommand[sizeof(pendingAlarmCommand) - 1] = '\0';
  alarmCommandPending = true;
  xSemaphoreGive(messageMutex);
  return true;
}

void applyQueuedAlarmCommand() {
  char commandCopy[sizeof(pendingAlarmCommand)];
  bool hasCommand = false;

  if (!messageMutex) {
    if (alarmCommandPending) {
      strncpy(commandCopy, pendingAlarmCommand, sizeof(commandCopy) - 1);
      commandCopy[sizeof(commandCopy) - 1] = '\0';
      pendingAlarmCommand[0] = '\0';
      alarmCommandPending = false;
      hasCommand = true;
    }
  } else {
    xSemaphoreTake(messageMutex, portMAX_DELAY);
    if (alarmCommandPending) {
      strncpy(commandCopy, pendingAlarmCommand, sizeof(commandCopy) - 1);
      commandCopy[sizeof(commandCopy) - 1] = '\0';
      pendingAlarmCommand[0] = '\0';
      alarmCommandPending = false;
      hasCommand = true;
    }
    xSemaphoreGive(messageMutex);
  }

  if (!hasCommand) return;

  String command = commandCopy;
  command.trim();
  String payload = command.substring(6);
  payload.trim();
  String lowerPayload = payload;
  lowerPayload.toLowerCase();

  if (lowerPayload == "off") {
    setAlarmConfig(false, alarmHour, alarmMinute, true);
    queueRemoteMessageAck("Alarm Off");
    queueRemoteMessageClear(commandCopy);
    if (isAlarmActiveState(ClockState)) finishAlarmTakeover(displayState, true);
    return;
  }

  uint8_t newHour = alarmHour;
  uint8_t newMinute = alarmMinute;
  if (parseAlarmTimeValue(payload, newHour, newMinute)) {
    setAlarmConfig(true, newHour, newMinute, true);
    const String ackMessage = "Alarm " + formatAlarmTime(newHour, newMinute);
    queueRemoteMessageAck(ackMessage.c_str());
  } else {
    queueRemoteMessageAck("Alarm command invalid");
  }
  queueRemoteMessageClear(commandCopy);
}

void processIncomingRemoteMessage(const char *message) {
  rememberLastRemoteMessage(message);
  if (applyRemoteAlarmCommand(message)) return;
  queueRemoteMessage(message);
}

void queueRemoteMessage(const char *message) {
  if (!message || isRemoteMessageEmpty(message) || !messageMutex) return;

  xSemaphoreTake(messageMutex, portMAX_DELAY);
  strncpy(pendingRemoteMessage, message, sizeof(pendingRemoteMessage) - 1);
  pendingRemoteMessage[sizeof(pendingRemoteMessage) - 1] = '\0';
  remoteMessagePending = true;
  xSemaphoreGive(messageMutex);
  PRINT("Queued remote message: ", pendingRemoteMessage);
  PRINTLN;
}

bool hasPendingRemoteMessage() {
  if (!messageMutex) return false;

  xSemaphoreTake(messageMutex, portMAX_DELAY);
  const bool pending = remoteMessagePending;
  xSemaphoreGive(messageMutex);

  return pending;
}

bool takePendingRemoteMessage() {
  bool activated = false;

  if (!messageMutex) return false;

  xSemaphoreTake(messageMutex, portMAX_DELAY);
  if (remoteMessagePending) {
    strncpy(activeRemoteMessage, pendingRemoteMessage, sizeof(activeRemoteMessage) - 1);
    activeRemoteMessage[sizeof(activeRemoteMessage) - 1] = '\0';
    pendingRemoteMessage[0] = '\0';
    remoteMessagePending = false;
    remoteMessageActive = !isRemoteMessageEmpty(activeRemoteMessage);
    activated = remoteMessageActive;
  }
  xSemaphoreGive(messageMutex);

  if (activated) queueRemoteMessageClear(activeRemoteMessage);

  return activated;
}

void queueRemoteMessageAck(const char *message) {
  if (!messageMutex) return;

  String ackMessage = "OK " + formatLocalTimeStamp(now());
  if (message && message[0]) {
    ackMessage += " | ";
    ackMessage += message;
  }

  xSemaphoreTake(messageMutex, portMAX_DELAY);
  strncpy(pendingRemoteAck, ackMessage.c_str(), sizeof(pendingRemoteAck) - 1);
  pendingRemoteAck[sizeof(pendingRemoteAck) - 1] = '\0';
  remoteMessageAckPending = true;
  xSemaphoreGive(messageMutex);
}

bool publishQueuedRemoteMessageAck() {
  char ackMessage[sizeof(pendingRemoteAck)];
  bool shouldPublish = false;

  if (!messageMutex) return true;

  xSemaphoreTake(messageMutex, portMAX_DELAY);
  if (remoteMessageAckPending) {
    strncpy(ackMessage, pendingRemoteAck, sizeof(ackMessage) - 1);
    ackMessage[sizeof(ackMessage) - 1] = '\0';
    shouldPublish = true;
  }
  xSemaphoreGive(messageMutex);

  if (!shouldPublish) return true;

  if (dataMQTT.publish(ackMessage)) {
    xSemaphoreTake(messageMutex, portMAX_DELAY);
    remoteMessageAckPending = false;
    pendingRemoteAck[0] = '\0';
    xSemaphoreGive(messageMutex);
    return true;
  }

  return false;
}

void queueRemoteMessageClear(const char *message) {
  if (!messageMutex) return;

  xSemaphoreTake(messageMutex, portMAX_DELAY);
  strncpy(pendingRemoteClearMessage, message ? message : "", sizeof(pendingRemoteClearMessage) - 1);
  pendingRemoteClearMessage[sizeof(pendingRemoteClearMessage) - 1] = '\0';
  remoteMessageClearPending = true;
  xSemaphoreGive(messageMutex);
}

bool publishQueuedRemoteMessageClear() {
  char clearMessage[sizeof(pendingRemoteClearMessage)];
  bool shouldPublish = false;

  if (!messageMutex) return true;

  xSemaphoreTake(messageMutex, portMAX_DELAY);
  shouldPublish = remoteMessageClearPending;
  strncpy(clearMessage, pendingRemoteClearMessage, sizeof(clearMessage) - 1);
  clearMessage[sizeof(clearMessage) - 1] = '\0';
  xSemaphoreGive(messageMutex);

  if (!shouldPublish) return true;

  String currentMessage;
  if (!fetchLastRemoteMessage(currentMessage)) return false;

  if (isRemoteMessageEmpty(currentMessage)) {
    xSemaphoreTake(messageMutex, portMAX_DELAY);
    remoteMessageClearPending = false;
    pendingRemoteClearMessage[0] = '\0';
    xSemaphoreGive(messageMutex);
    return true;
  }

  if (currentMessage != String(clearMessage)) {
    xSemaphoreTake(messageMutex, portMAX_DELAY);
    remoteMessageClearPending = false;
    pendingRemoteClearMessage[0] = '\0';
    xSemaphoreGive(messageMutex);
    return true;
  }

  if (msgMQTTPub.publish("#")) {
    xSemaphoreTake(messageMutex, portMAX_DELAY);
    remoteMessageClearPending = false;
    pendingRemoteClearMessage[0] = '\0';
    xSemaphoreGive(messageMutex);
    return true;
  }

  return false;
}

bool isRemoteMessageEmpty(const char *message) {
  if (!message) return true;
  while (*message == ' ' || *message == '\r' || *message == '\n' || *message == '\t') message++;
  return *message == '\0' || (*message == '#' && *(message + 1) == '\0');
}

bool isRemoteMessageEmpty(const String &message) {
  return isRemoteMessageEmpty(message.c_str());
}

String extractJsonStringField(const String &json, const char *fieldName) {
  const String key = "\"" + String(fieldName) + "\":";
  const int keyStart = json.indexOf(key);
  if (keyStart < 0) return "";

  const int valueStart = json.indexOf('"', keyStart + key.length());
  if (valueStart < 0) return "";

  String value;
  bool escaped = false;
  for (int i = valueStart + 1; i < json.length(); i++) {
    const char c = json.charAt(i);
    if (escaped) {
      switch (c) {
        case 'n':
          value += '\n';
          break;
        case 'r':
          break;
        case 't':
          value += '\t';
          break;
        default:
          value += c;
          break;
      }
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '"') {
      break;
    } else {
      value += c;
    }
  }

  return value;
}

bool fetchLastRemoteMessage(String &message) {
  if (!WiFi.isConnected()) {
    PRINTS("Msg fetch skipped: WiFi disconnected\n");
    return false;
  }

  WiFiClientSecure httpClient;
  httpClient.setInsecure();
  if (!httpClient.connect(AIO_SERVER, 443)) {
    PRINTS("Msg fetch connect failed\n");
    return false;
  }

  const String path = "/api/v2/" + String(AIO_USERNAME) + String(feedMsg) + "/data?limit=1&include=value";
  httpClient.print(String("GET ") + path + " HTTP/1.1\r\n"
                   + "Host: " + AIO_SERVER + "\r\n"
                   + "User-Agent: JPClock\r\n"
                   + "X-AIO-Key: " + AIO_KEY + "\r\n"
                   + "Connection: close\r\n\r\n");
  PRINT("Msg fetch path: ", path);
  PRINTLN;

  const String statusLine = httpClient.readStringUntil('\n');
  PRINT("Msg fetch HTTP: ", statusLine);
  PRINTLN;
  if (statusLine.indexOf(" 200 ") < 0) {
    PRINT("Msg fetch status: ", statusLine);
    PRINTLN;
    httpClient.stop();
    return false;
  }

  while (httpClient.connected()) {
    const String headerLine = httpClient.readStringUntil('\n');
    if (headerLine == "\r") break;
  }

  const String body = httpClient.readString();
  httpClient.stop();
  PRINT("Msg fetch body: ", body);
  PRINTLN;

  message = extractJsonStringField(body, "value");
  PRINT("Msg fetch parsed: ", message);
  PRINTLN;
  if (isRemoteMessageEmpty(message)) message = "";
  return true;
}

static const char WEB_PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>JPClock</title>
<style>
:root{--bg:#0d1114;--panel:#151c22;--line:#26323d;--accent:#ff7a18;--accent-soft:#ffd166;--text:#f5efe2;--muted:#98a8b7;--danger:#ff5c5c}
*{box-sizing:border-box}
body{margin:0;background:radial-gradient(circle at top,#4a2410 0,transparent 28%),linear-gradient(180deg,#10161c,#080b0e);color:var(--text);font:16px/1.45 "Courier New","Lucida Console",monospace}
main{max-width:960px;margin:0 auto;padding:24px 16px 40px;display:grid;gap:16px}
.hero{padding:18px 20px;border:1px solid var(--line);background:linear-gradient(135deg,rgba(255,122,24,.18),rgba(255,209,102,.06) 55%,rgba(21,28,34,.96));border-radius:18px}
.eyebrow{margin:0 0 8px;color:var(--accent-soft);text-transform:uppercase;letter-spacing:.18em;font-size:.82rem}
h1{margin:0 0 8px;font-size:clamp(30px,6vw,44px);letter-spacing:.08em;text-transform:uppercase}
.sub{margin:0;color:var(--muted);max-width:52rem}
.panel{border:1px solid var(--line);background:linear-gradient(180deg,rgba(21,28,34,.96),rgba(11,15,19,.98));border-radius:18px;padding:18px}
.grid{display:grid;gap:12px;grid-template-columns:repeat(auto-fit,minmax(180px,1fr))}
.stat{padding:14px;border:1px solid rgba(255,255,255,.05);border-radius:14px;background:rgba(255,255,255,.02)}
.stat span{display:block;color:var(--muted);font-size:.82rem;text-transform:uppercase;letter-spacing:.12em;margin-bottom:6px}
.stat strong{display:block;font-size:1.3rem;color:var(--accent-soft)}
h2{margin:0 0 12px;font-size:1rem;letter-spacing:.12em;text-transform:uppercase;color:var(--accent-soft)}
.alarm-form{display:grid;gap:12px;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));align-items:end}
.alarm-time-fields{display:grid;gap:10px;grid-template-columns:minmax(0,1fr) auto minmax(0,1fr);align-items:end}
.alarm-time-fields label{margin:0}
.time-colon{align-self:end;padding:0 2px 12px;color:var(--accent-soft);font-size:1.5rem;line-height:1}
label{display:grid;gap:8px;color:var(--muted);font-size:.9rem}
input[type=number]{appearance:none;border:1px solid var(--line);border-radius:12px;background:#0d1318;color:var(--text);padding:12px 14px;font:inherit;width:100%}
.toggle{display:flex;align-items:center;gap:10px;padding:12px 14px;border:1px solid var(--line);border-radius:12px;background:#0d1318;color:var(--text)}
.toggle input{width:18px;height:18px}
.actions{display:flex;flex-wrap:wrap;gap:10px}
button{border:1px solid rgba(255,122,24,.35);border-radius:999px;padding:12px 16px;background:linear-gradient(180deg,rgba(255,122,24,.18),rgba(255,122,24,.08));color:var(--text);font:inherit;letter-spacing:.08em;text-transform:uppercase;cursor:pointer}
button:hover{border-color:rgba(255,209,102,.6)}
button:disabled{opacity:.45;cursor:not-allowed}
.danger{border-color:rgba(255,92,92,.45);background:linear-gradient(180deg,rgba(255,92,92,.2),rgba(255,92,92,.08))}
.note{min-height:1.3em;margin:8px 0 0;color:var(--muted)}
pre{margin:0;min-height:3.2em;white-space:pre-wrap;word-break:break-word;border:1px solid rgba(255,255,255,.05);border-radius:14px;background:#0b1014;padding:14px;color:#ffd9b0}
@media (max-width:640px){main{padding:16px 12px 28px}.panel,.hero{padding:16px}}
</style>
</head>
<body>
<main>
  <section class="hero">
    <p class="eyebrow">LAN Console</p>
    <h1>JPClock</h1>
  </section>

  <section class="panel grid">
    <div class="stat"><span>Local time</span><strong id="local-time">--</strong></div>
    <div class="stat"><span>Mode</span><strong id="mode-name">--</strong></div>
    <div class="stat"><span>Display</span><strong id="display-state">--</strong></div>
    <div class="stat"><span>Alarm</span><strong id="alarm-state">--</strong></div>
    <div class="stat"><span>Temperature</span><strong id="sensor-temp">--</strong></div>
    <div class="stat"><span>Humidity</span><strong id="sensor-humi">--</strong></div>
    <div class="stat"><span>Pressure</span><strong id="sensor-pres">--</strong></div>
    <div class="stat"><span>Brightness</span><strong id="sensor-light">--</strong></div>
  </section>

  <section class="panel">
    <h2>Alarm Setup</h2>
    <form id="alarm-form" class="alarm-form">
      <div class="alarm-time-fields">
        <label>Hour
          <input id="alarm-hour" type="number" min="0" max="23" step="1" inputmode="numeric" required>
        </label>
        <div class="time-colon">:</div>
        <label>Minute
          <input id="alarm-minute" type="number" min="0" max="59" step="1" inputmode="numeric" required>
        </label>
      </div>
      <label class="toggle">
        <input id="alarm-enabled" name="enabled" type="checkbox" value="1">
        <span>Alarm enabled</span>
      </label>
      <button type="submit">Save Alarm</button>
      <button id="alarm-stop" class="danger" type="button">Stop Alarm</button>
    </form>
    <p id="alarm-note" class="note"></p>
  </section>

  <section class="panel">
    <h2>Clock Modes</h2>
    <div class="actions">
      <button data-mode="normal" type="button">Normal</button>
      <button data-mode="snake" type="button">Snake</button>
      <button data-mode="sky" type="button">Sky</button>
      <button data-mode="off" class="danger" type="button">Display Off</button>
    </div>
  </section>

  <section class="panel">
    <h2>Last Message</h2>
    <pre id="last-message">No stored message</pre>
  </section>
</main>
<script>
const $ = (selector) => document.querySelector(selector);
const modeButtons = [...document.querySelectorAll('[data-mode]')];
const alarmForm = $('#alarm-form');
const alarmEditorFields = [$('#alarm-hour'), $('#alarm-minute'), $('#alarm-enabled')];
let alarmFormDirty = false;
let alarmFormEditing = false;
function note(text, bad = false) {
  const node = $('#alarm-note');
  node.textContent = text;
  node.style.color = bad ? 'var(--danger)' : 'var(--muted)';
}
function syncAlarmEditor(status) {
  if (alarmFormDirty || alarmFormEditing) return;
  const [hour, minute] = status.alarmTime.split(':');
  $('#alarm-hour').value = hour;
  $('#alarm-minute').value = minute;
  $('#alarm-enabled').checked = status.alarmEnabled;
}
function paint(status) {
  $('#local-time').textContent = status.localTime;
  $('#mode-name').textContent = status.mode;
  $('#display-state').textContent = !status.displayEnabled ? 'Manual off' : (status.displayVisible ? 'Visible' : 'Screen saver');
  $('#alarm-state').textContent = status.alarmActive ? 'Burst ' + status.alarmBurstsUsed + '/' + status.alarmBurstsTotal : (status.alarmEnabled ? status.alarmTime + ' on' : status.alarmTime + ' off');
  $('#sensor-temp').textContent = status.temperature.toFixed(1) + ' C';
  $('#sensor-humi').textContent = status.humidity.toFixed(1) + ' %';
  $('#sensor-pres').textContent = status.pressure.toFixed(1) + ' hPa';
  $('#sensor-light').textContent = status.brightness.toFixed(0) + ' lx';
  syncAlarmEditor(status);
  $('#alarm-stop').disabled = !status.alarmActive;
  $('#last-message').textContent = status.lastMessage || 'No stored message';
}
async function refresh() {
  try {
    const response = await fetch('/api/status', { cache: 'no-store' });
    const status = await response.json();
    paint(status);
  } catch (error) {
    note('Clock did not answer: ' + error.message, true);
  }
}
async function post(url, data) {
  const response = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams(data)
  });
  if (!response.ok) {
    throw new Error(await response.text());
  }
  const status = await response.json();
  paint(status);
}
alarmEditorFields.forEach((field) => {
  field.addEventListener('input', () => {
    alarmFormDirty = true;
  });
});
alarmForm.addEventListener('focusin', () => {
  alarmFormEditing = true;
});
alarmForm.addEventListener('focusout', () => {
  setTimeout(() => {
    alarmFormEditing = alarmForm.contains(document.activeElement);
  }, 0);
});
$('#alarm-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  const hourValue = Number($('#alarm-hour').value);
  const minuteValue = Number($('#alarm-minute').value);
  if (!Number.isInteger(hourValue) || hourValue < 0 || hourValue > 23
      || !Number.isInteger(minuteValue) || minuteValue < 0 || minuteValue > 59) {
    note('Use 24-hour values: hour 0-23 and minute 0-59.', true);
    return;
  }
  try {
    await post('/api/alarm/save', {
      time: String(hourValue).padStart(2, '0') + ':' + String(minuteValue).padStart(2, '0'),
      enabled: $('#alarm-enabled').checked ? '1' : '0'
    });
    alarmFormDirty = false;
    alarmFormEditing = false;
    note('Alarm settings saved.');
  } catch (error) {
    note(error.message, true);
  }
});
$('#alarm-stop').addEventListener('click', async () => {
  try {
    await post('/api/alarm/stop', {});
    note('Alarm stopped.');
  } catch (error) {
    note(error.message, true);
  }
});
modeButtons.forEach((button) => {
  button.addEventListener('click', async () => {
    try {
      await post('/api/mode', { mode: button.dataset.mode });
      note('Mode changed to ' + button.dataset.mode + '.');
    } catch (error) {
      note(error.message, true);
    }
  });
});
refresh();
setInterval(refresh, 3000);
</script>
</body>
</html>
)HTML";

String jsonEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    const char c = value.charAt(i);
    switch (c) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += c;
        break;
    }
  }
  return escaped;
}

String buildWebStatusJson() {
  float tempValue = _t;
  float humiValue = _h;
  float presValue = _p;
  float lightValue = _l;
  if (bufMutex) {
    xSemaphoreTake(bufMutex, portMAX_DELAY);
    tempValue = _t;
    humiValue = _h;
    presValue = _p;
    lightValue = _l;
    xSemaphoreGive(bufMutex);
  }

  String lastMessage;
  copyLastRemoteMessage(lastMessage);
  String json;
  json.reserve(512);
  json += "{";
  json += "\"localTime\":\"" + jsonEscape(formatLocalTimeStamp(now())) + "\",";
  json += "\"mode\":\"" + jsonEscape(clockModeName(ClockState)) + "\",";
  json += "\"displayEnabled\":";
  json += (displayEnabled ? "true" : "false");
  json += ",\"displayVisible\":";
  json += ((displayEnabled && screenSaverNotActive) ? "true" : "false");
  json += ",\"temperature\":";
  json += String(tempValue, 1);
  json += ",\"humidity\":";
  json += String(humiValue, 1);
  json += ",\"pressure\":";
  json += String(presValue, 1);
  json += ",\"brightness\":";
  json += String(lightValue, 0);
  json += ",\"alarmEnabled\":";
  json += (alarmEnabled ? "true" : "false");
  json += ",\"alarmTime\":\"" + formatAlarmTime(alarmHour, alarmMinute) + "\",";
  json += "\"alarmActive\":";
  json += (isAlarmActiveState(ClockState) ? "true" : "false");
  json += ",\"alarmBurstsUsed\":";
  json += String(alarmBurstCount);
  json += ",\"alarmBurstsTotal\":";
  json += String(AlarmTotalBursts);
  json += ",\"lastMessage\":\"" + jsonEscape(lastMessage) + "\"";
  json += "}";
  return json;
}

void sendJsonResponse(int statusCode, const String &body) {
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(statusCode, "application/json; charset=utf-8", body);
}

void handleWebRoot() {
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send_P(200, "text/html; charset=utf-8", WEB_PAGE);
}

void handleWebStatus() {
  sendJsonResponse(200, buildWebStatusJson());
}

void handleWebAlarmSave() {
  const String timeValue = webServer.arg("time");
  uint8_t newHour = alarmHour;
  uint8_t newMinute = alarmMinute;
  if (!parseAlarmTimeValue(timeValue, newHour, newMinute)) {
    sendJsonResponse(400, "{\"error\":\"invalid alarm time\"}");
    return;
  }

  bool enabled = webServer.hasArg("enabled");
  if (enabled) {
    String enabledValue = webServer.arg("enabled");
    enabledValue.toLowerCase();
    if (enabledValue == "0" || enabledValue == "false" || enabledValue == "off") enabled = false;
  }

  setAlarmConfig(enabled, newHour, newMinute, true);
  if (!enabled && isAlarmActiveState(ClockState)) finishAlarmTakeover(displayState, true);
  sendJsonResponse(200, buildWebStatusJson());
}

void handleWebAlarmStop() {
  if (isAlarmActiveState(ClockState)) finishAlarmTakeover(displayState, true);
  sendJsonResponse(200, buildWebStatusJson());
}

void handleWebMode() {
  String error;
  if (!applyRequestedMode(webServer.arg("mode"), error)) {
    const int statusCode = error == "alarm active" ? 409 : 400;
    sendJsonResponse(statusCode, "{\"error\":\"" + jsonEscape(error) + "\"}");
    return;
  }
  sendJsonResponse(200, buildWebStatusJson());
}

void setupWebServer() {
  webServer.on("/", HTTP_GET, handleWebRoot);
  webServer.on("/api/status", HTTP_GET, handleWebStatus);
  webServer.on("/api/alarm/save", HTTP_POST, handleWebAlarmSave);
  webServer.on("/api/alarm/stop", HTTP_POST, handleWebAlarmStop);
  webServer.on("/api/mode", HTTP_POST, handleWebMode);
  webServer.onNotFound([]() {
    if (webServer.uri().startsWith("/api/")) {
      sendJsonResponse(404, "{\"error\":\"not found\"}");
    } else {
      webServer.send(404, "text/plain; charset=utf-8", "Not found");
    }
  });
  webServer.begin();
}

void drawAlarmAnimation(DisplayState &display, bool forceRedraw) {
  if (!forceRedraw && !AlarmAnimation.check(AlarmAnimationTick)) return;

  if (!forceRedraw) {
    alarmAnimationFrame = (alarmAnimationFrame + 1) % (AlarmTextHoldTicks * 2);
  }

  static const uint8_t outerX[] = {13, 10, 7, 4, 7, 10};
  static const uint8_t outerY[] = {3, 2, 1, 0, 1, 2};
  static const uint8_t outerH[] = {2, 4, 6, 8, 6, 4};
  static const uint8_t innerX[] = {15, 12, 9, 6, 9, 12};
  static const uint8_t innerY[] = {3, 2, 2, 1, 2, 2};
  static const uint8_t innerH[] = {2, 4, 4, 6, 4, 4};

  const uint8_t pulsePhase = alarmAnimationFrame % 6;
  const bool showAlarmText = ((alarmAnimationFrame / AlarmTextHoldTicks) % 2U) == 0;
  const String text = showAlarmText ? "ALARM" : formatAlarmTime(alarmHour, alarmMinute);
  const int16_t textWidth = (int16_t)text.length() * 6;
  int16_t textX = (matrix.width() - textWidth) / 2;
  if (textX < 0) textX = 0;

  matrix.setClip(0, matrix.width(), 0, matrix.height());
  matrix.fillScreen(LOW);
  matrix.setCursor(textX, 0);
  matrix.print(text);

  matrix.drawFastVLine(outerX[pulsePhase], outerY[pulsePhase], outerH[pulsePhase], HIGH);
  matrix.drawFastVLine(matrix.width() - 1 - outerX[pulsePhase], outerY[pulsePhase], outerH[pulsePhase], HIGH);
  matrix.drawFastVLine(innerX[pulsePhase], innerY[pulsePhase], innerH[pulsePhase], HIGH);
  matrix.drawFastVLine(matrix.width() - 1 - innerX[pulsePhase], innerY[pulsePhase], innerH[pulsePhase], HIGH);
  if ((pulsePhase & 1U) == 0) {
    matrix.drawPixel(0, 0, HIGH);
    matrix.drawPixel(0, matrix.height() - 1, HIGH);
    matrix.drawPixel(matrix.width() - 1, 0, HIGH);
    matrix.drawPixel(matrix.width() - 1, matrix.height() - 1, HIGH);
  }

  display.updateDisplay = true;
}

void triggerAlarmBurst(DisplayState &display) {
  if (alarmBurstCount < AlarmTotalBursts) {
    alarmBurstCount++;
  }

  const uint8_t soundTrack = (uint8_t)(AlarmSoundFirstTrack + alarmBurstCount - 1);
  alarmAnimationFrame = 0;
  alarmFinalReturnPending = alarmBurstCount >= AlarmTotalBursts;
  AlarmAnimation.start(-AlarmAnimationTick);
  if (alarmFinalReturnPending) {
    AlarmFinalDisplay.start();
  } else {
    AlarmRepeat.start();
  }

  DFPlayer.playFolder(AlarmSoundFolder, soundTrack);
  drawAlarmAnimation(display, true);
}

void finishAlarmTakeover(DisplayState &display, bool stopAudio) {
  if (stopAudio) stopDfPlayerPlayback();
  alarmBurstCount = 0;
  alarmAnimationFrame = 0;
  alarmFinalReturnPending = false;
  display.updateDisplay = false;
  ClockState = alarmReturnState;
}

void startAlarmTakeover(DisplayState &display, ClockStates returnState) {
  alarmReturnState = returnState;
  cancelModeChangeCoilPulse();
  restoreDisplayOutput(display);
  stopVoiceCoilMotion();
  stopDfPlayerPlayback();
  alarmBurstCount = 0;
  alarmAnimationFrame = 0;
  alarmFinalReturnPending = false;
  ClockState = _Clock_alarm_active_init;
}

void startVoiceCoilMotion(bool withKick = true) {
  voiceCoilState = withKick ? _vcKickHigh : _vcRandomMotion;
  if (withKick) {
    setVoiceCoilLevel(voiceCoilKick);
    voiceCoilPeriod = voiceCoilKickMs;
    VoiceCoilMotion.start();
  } else {
    setVoiceCoilLevel(voiceCoilMid);
    voiceCoilPeriod = voiceCoilSettleMs;
    VoiceCoilMotion.start();
  }
}

void updateVoiceCoilMotion() {
  static uint8_t currentLevel = voiceCoilLow;

  if (voiceCoilState == _vcOff) {
    currentLevel = voiceCoilOff;
  } else if (voiceCoilState == _vcKickHigh && VoiceCoilMotion.check(voiceCoilPeriod)) {
    currentLevel = voiceCoilKick;
    setVoiceCoilLevel(voiceCoilOff);
    voiceCoilState = _vcKickRelease;
    voiceCoilPeriod = voiceCoilReleaseMs;
  } else if (voiceCoilState == _vcKickRelease && VoiceCoilMotion.check(voiceCoilPeriod)) {
    currentLevel = voiceCoilOff;
    voiceCoilState = _vcRandomMotion;
    currentLevel = voiceCoilMid;
    setVoiceCoilLevel(currentLevel);
    voiceCoilPeriod = voiceCoilSettleMs;
  } else if (voiceCoilState == _vcModePulseHigh && VoiceCoilMotion.check(voiceCoilPeriod)) {
    currentLevel = voiceCoilKick;
    setVoiceCoilLevel(voiceCoilOff);
    voiceCoilState = _vcModePulseRelease;
    voiceCoilPeriod = voiceCoilReleaseMs;
  } else if (voiceCoilState == _vcModePulseRelease && VoiceCoilMotion.check(voiceCoilPeriod)) {
    currentLevel = voiceCoilOff;
    voiceCoilState = _vcOff;
    setVoiceCoilLevel(currentLevel);
    voiceCoilPeriod = voiceCoilSettleMs;
  } else if (voiceCoilState == _vcRandomMotion && VoiceCoilMotion.check(voiceCoilPeriod)) {
    currentLevel = nextVoiceCoilLevel(currentLevel);
    setVoiceCoilLevel(currentLevel);
    voiceCoilPeriod = random(voiceCoilMotionMinMs, voiceCoilMotionMaxMs + 1);
  }
}


void clearScreen(void) {
  matrix.setClip(0, matrix.width(), 0, matrix.height());
  matrix.fillScreen(LOW);
  matrix.write();
  stopVoiceCoilMotion();
}

void clearDisplayBuffer(void) {
  matrix.setClip(0, matrix.width(), 0, matrix.height());
  matrix.fillScreen(LOW);
}

float updateRunningAverage(float currentAverage, float sample, unsigned long sampleCount) {
  return currentAverage + (sample - currentAverage) / sampleCount;
}

void shiftHistoryTablesLeft(void) {
  for (int ii = 0; ii < NumberOfPoints - 1; ++ii) {
    tempTable[ii] = tempTable[ii + 1];
    presTable[ii] = presTable[ii + 1];
    humiTable[ii] = humiTable[ii + 1];
  }
  tempTable[NumberOfPoints - 1] = 0;
  presTable[NumberOfPoints - 1] = 0;
  humiTable[NumberOfPoints - 1] = 0;
}

void ensureHistoryRange(float &minValue, float &maxValue, float minimumRange) {
  if (maxValue - minValue >= minimumRange) {
    return;
  }

  const float center = (minValue + maxValue) * 0.5f;
  minValue = center - minimumRange * 0.5f;
  maxValue = center + minimumRange * 0.5f;
}

void updateHistoryBounds(DisplayState &display, uint8_t historyPointCount) {
  if (historyPointCount == 0) {
    display.tempMin = 0;
    display.tempMax = HistoryMinTempRange;
    display.presMin = 0;
    display.presMax = HistoryMinPresRange;
    display.humiMin = 0;
    display.humiMax = HistoryMinHumiRange;
    return;
  }

  display.tempMin = display.tempMax = tempTable[0];
  display.presMin = display.presMax = presTable[0];
  display.humiMin = display.humiMax = humiTable[0];

  for (uint8_t ii = 1; ii < historyPointCount; ++ii) {
    const temp_t tempValue = tempTable[ii];
    if (tempValue < display.tempMin) display.tempMin = tempValue;
    if (tempValue > display.tempMax) display.tempMax = tempValue;

    const pres_t presValue = presTable[ii];
    if (presValue < display.presMin) display.presMin = presValue;
    if (presValue > display.presMax) display.presMax = presValue;

    const humi_t humiValue = humiTable[ii];
    if (humiValue < display.humiMin) display.humiMin = humiValue;
    if (humiValue > display.humiMax) display.humiMax = humiValue;
  }

  ensureHistoryRange(display.tempMin, display.tempMax, HistoryMinTempRange);
  ensureHistoryRange(display.presMin, display.presMax, HistoryMinPresRange);
  ensureHistoryRange(display.humiMin, display.humiMax, HistoryMinHumiRange);
}

int scaleHistoryValue(float value, float minValue, float maxValue) {
  const float range = maxValue - minValue;
  if (range <= 0.0f) {
    return 1;
  }

  const float scaled = 1.0f + ((value - minValue) * 7.0f / range);
  return constrain((int)(scaled + 0.5f), 1, 8);
}

void showOtaStartMessage(void) {
  matrix.shutdown(false);
  matrix.setClip(0, matrix.width(), 0, matrix.height());
  matrix.fillScreen(LOW);
  matrix.setCursor(0, 0);
  matrix.print("OTA...");
  matrix.write();
}

boolean SyncNTP() {

  PRINTS("Contacting NTP Server process\n");
  if (timeClient.forceUpdate()) {
    setTime(timeClient.getEpochTime());
    printClockTimes("NTP sync OK");
    return true;
  } else {
    PRINTS("NTP sync failed!");
    return false;
  }
}

bool waitForNetworkReady(const char *hostname, unsigned long timeoutMs) {
  const unsigned long deadline = millis() + timeoutMs;

  while ((long)(millis() - deadline) < 0) {
    if (WiFi.isConnected()) {
      const IPAddress localIp = WiFi.localIP();
      const IPAddress gatewayIp = WiFi.gatewayIP();
      const IPAddress dnsIp = WiFi.dnsIP();

      if (localIp != IPAddress((uint32_t)0) &&
          gatewayIp != IPAddress((uint32_t)0) &&
          dnsIp != IPAddress((uint32_t)0)) {
        if (!hostname || hostname[0] == '\0') {
          return true;
        }

        IPAddress resolvedIp;
        if (WiFi.hostByName(hostname, resolvedIp) == 1 && resolvedIp != IPAddress((uint32_t)0)) {
          return true;
        }
      }
    }

    delay(100);
  }

  PRINT("Network not ready. IP=", WiFi.localIP());
  PRINT(" GW=", WiFi.gatewayIP());
  PRINT(" DNS=", WiFi.dnsIP());
  PRINTLN;
  return false;
}

boolean StartSyncNTP() {
  boolean SyncError = false;

  PRINTS("NTP Sync Init started\n");
  zoneInfo0.setText("NTP Sync", _SCROLL_LEFT, _TO_LEFT, InfoTick1, I0s, I0e);
  zoneInfo0.Animate(true);
  if (WiFi.isConnected()) {
    if (waitForNetworkReady("europe.pool.ntp.org", 8000)) {
      SyncError = !SyncNTP();
    } else {
      SyncError = true;
    }
  } else {
    PRINTS("Recovering WiFi connection\n");
    //WiFi.mode(WIFI_STA);
    matrix.fillScreen(LOW);
    matrix.setCursor(0, 0);
    zoneInfo0.setText("Connecting WiFi", _SCROLL_LEFT, _TO_FULL, InfoTick1, I0s, I0e);
    WiFi.begin();
    zoneInfo0.Animate(true);
    if (waitForNetworkReady(nullptr, 10000)) {
      zoneInfo0.setText("Connecting NTP", _SCROLL_LEFT, _TO_FULL, InfoTick1, I0s, I0e);
      zoneInfo0.Animate(true);
      if (waitForNetworkReady("europe.pool.ntp.org", 8000)) {
        SyncError = !SyncNTP();
      } else {
        SyncError = true;
      }
      //WiFi.mode(WIFI_OFF);
    } else {
      SyncError = true;  // wifi failure
    }
  }
  return SyncError;
}

time_t buildUtcTime(uint16_t yearValue, uint8_t monthValue, uint8_t dayValue, uint8_t hourValue, uint8_t minuteValue, uint8_t secondValue) {
  tmElements_t tm;
  tm.Second = secondValue;
  tm.Minute = minuteValue;
  tm.Hour = hourValue;
  tm.Day = dayValue;
  tm.Month = monthValue;
  tm.Year = CalendarYrToTm(yearValue);
  return makeTime(tm);
}

uint8_t lastSundayOfMonth(uint16_t yearValue, uint8_t monthValue) {
  const uint8_t nextMonth = monthValue == 12 ? 1 : monthValue + 1;
  const uint16_t nextMonthYear = monthValue == 12 ? yearValue + 1 : yearValue;
  const time_t lastDay = buildUtcTime(nextMonthYear, nextMonth, 1, 0, 0, 0) - SECS_PER_DAY;
  return day(lastDay) - (weekday(lastDay) - 1);
}

String formatTimeStamp(time_t stamp) {
  return String(year(stamp)) + "-" + (month(stamp) < 10 ? "0" : "") + String(month(stamp)) + "-" + (day(stamp) < 10 ? "0" : "") + String(day(stamp))
         + " " + (hour(stamp) < 10 ? "0" : "") + String(hour(stamp)) + ":" + (minute(stamp) < 10 ? "0" : "") + String(minute(stamp))
         + ":" + (second(stamp) < 10 ? "0" : "") + String(second(stamp));
}

String formatLocalTimeStamp(time_t utc) {
  TimeChangeRule *tcr;
  const time_t local = CET.toLocal(utc, &tcr);
  return formatTimeStamp(local) + " " + String(tcr ? tcr->abbrev : "TZ?");
}

void printClockTimes(const char *prefix) {
  PRINTS(prefix);
  PRINTS(" UTC=");
  PRINTS(formatTimeStamp(now()));
  PRINTS(" Local=");
  PRINTS(formatLocalTimeStamp(now()));
  PRINTLN;
}

void rememberTimezoneState() {
  TimeChangeRule *tcr;
  CET.toLocal(now(), &tcr);
  if (!tcr) return;
  strncpy(currentTimezoneAbbrev, tcr->abbrev, sizeof(currentTimezoneAbbrev) - 1);
  currentTimezoneAbbrev[sizeof(currentTimezoneAbbrev) - 1] = '\0';
  timezoneStateKnown = true;
}

void announceTimezoneChange() {
  TimeChangeRule *tcr;
  CET.toLocal(now(), &tcr);
  if (!tcr) return;

  if (!timezoneStateKnown) {
    rememberTimezoneState();
    return;
  }

  if (strncmp(currentTimezoneAbbrev, tcr->abbrev, sizeof(currentTimezoneAbbrev)) != 0) {
    strncpy(currentTimezoneAbbrev, tcr->abbrev, sizeof(currentTimezoneAbbrev) - 1);
    currentTimezoneAbbrev[sizeof(currentTimezoneAbbrev) - 1] = '\0';
    printClockTimes("Timezone change");
    pendingTimezoneRefresh = true;
  }
}

void applyDstTestTime() {
#if DST_TEST_MODE == 1 || DST_TEST_MODE == 2
  const uint16_t testYear = year(now());
  const uint8_t testMonth = DST_TEST_MODE == 1 ? 3 : 10;
  const uint8_t testDay = lastSundayOfMonth(testYear, testMonth);
  const time_t transitionUtc = buildUtcTime(testYear, testMonth, testDay, 1, 0, 0);
  setTime(transitionUtc - DST_TEST_LEAD_SECONDS);
  rememberTimezoneState();
  printClockTimes("DST test time");
  zoneInfo0.setText(DST_TEST_MODE == 1 ? "DST->CEST" : "DST->CET", _SCROLL_LEFT, _TO_FULL, InfoTick1, I0s, I0e);
  zoneInfo0.Animate(true);
#endif
}

void SetupWiFi(void) {
  Schedular UpdateWifi(_Seconds);  // Wifi reconnect
  int i;
  zoneInfo0.setText("WiFi?", _SCROLL_LEFT, _TO_LEFT, InfoTick1, I0s, I0e);
  zoneInfo0.Animate(true);
  PRINTS("Connecting WiFi\n");

  WiFi.disconnect();
  WiFi.softAPdisconnect(true);
  for (int x = voiceCoilWiFiStart; x >= voiceCoilWiFiEnd; x--) {
    dacWrite(DACVoiceCoil, x);
    delay(10);
  }
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
      stopVoiceCoilMotion();
      ESP.restart();  // reboot and try again
    }
    if (zoneInfo1.Animate(false)) matrix.write();
    if (zoneInfo1.AnimateDone()) zoneInfo1.Reset();
    if (UpdateWifi.check(1)) {
      // Use a clear downward sweep so the arm visibly shows retry progress.
      dacWrite(DACVoiceCoil, map(i, 0, MAXRET, voiceCoilWiFiStart, voiceCoilWiFiEnd));
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
  waitForNetworkReady(nullptr, 8000);
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

static void logBrightnessReading(const char *context, uint16_t lux, uint8_t intensity) {
  PRINT_BRIGHTNESS("[Brightness] ");
  PRINT_BRIGHTNESS(context);
  PRINT_BRIGHTNESS_VALUE(" lux=", lux);
  PRINT_BRIGHTNESS_VALUE(" mapped=", intensity);
  PRINT_BRIGHTNESS_LN();
}

uint8_t keyboard(ClockStates key_DIR_CW, ClockStates key_DIR_CCW, ClockStates key_SW_DOWN, ClockStates key_SW_UP) {
  uint8_t key = DIR_NONE;

  if (xQueueReceive(xQueue, &key, 0) == pdPASS) {
    switch (key) {
      case DIR_CW:
        if (key_DIR_CW != _Clock_none) {
          ClockState = key_DIR_CW;
          armModeChangeCoilPulse(key_DIR_CW);
        }
        break;
      case DIR_CCW:
        if (key_DIR_CCW != _Clock_none) {
          ClockState = key_DIR_CCW;
          armModeChangeCoilPulse(key_DIR_CCW);
        }
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
  alarmEnabled = preferences.getBool("AlarmOn", false);
  alarmHour = (uint8_t)constrain(preferences.getUChar("AlarmHour", 7), 0, 23);
  alarmMinute = (uint8_t)constrain(preferences.getUChar("AlarmMinute", 0), 0, 59);
  sWIFI_SSID = preferences.getString("ssid", WIFI_SSID);
  sWIFI_PASWD = preferences.getString("password", WIFI_PASWD);
  preferences.end();
}

void savePreferences(void) {
  preferences.begin("clock", false);
  //preferences.clear();
  preferences.putBool("DingOnOff", DingOnOff);
  preferences.putBool("AlarmOn", alarmEnabled);
  preferences.putUChar("AlarmHour", alarmHour);
  preferences.putUChar("AlarmMinute", alarmMinute);
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
  randomSeed(analogRead(pinRandom) + micros());


  pinMode(sledPin, OUTPUT);        // passing seconds LED
  pinMode(PIN_BUT, INPUT_PULLUP);  // button to operate menu
  pinMode(modePin, INPUT_PULLUP);  // check setup
  pinMode(pirPin, INPUT);          // input from PIR sensor

  // matrix.setIntensity(0); // Use a value between 0 and 15 for brightness
  const uint16_t startupLux = lightMeter.readLightLevel();
  const uint8_t startupIntensity = IntensityMap(startupLux);
  logBrightnessReading("startup", startupLux, startupIntensity);
  matrix.setIntensity(startupIntensity);

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

	    bufMutex = xSemaphoreCreateMutex();
	    messageMutex = xSemaphoreCreateMutex();

	    client.setInsecure();
	    SetupWiFi();
    setupWebServer();
    {
      String startupMessage;
      if (fetchLastRemoteMessage(startupMessage) && !isRemoteMessageEmpty(startupMessage)) {
        PRINT("Startup Msg: ", startupMessage);
        PRINTLN;
        processIncomingRemoteMessage(startupMessage.c_str());
      } else {
        PRINTS("Startup Msg: none\n");
      }
    }
    SyncNTPError = StartSyncNTP();
    if (!SyncNTPError) {
      rememberTimezoneState();
      applyDstTestTime();
      printClockTimes("Startup sync");
      printWakeGreetingTestConfiguration(CET.toLocal(now()));
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

        showOtaStartMessage();
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
  static bool pirWasHigh = false;
  unsigned long voiceCoilDeta;

  ArduinoOTA.handle();

  if (wpsNeeded) {
    if (zoneInfo1.Animate(false)) matrix.write();
    if (zoneInfo1.AnimateDone()) zoneInfo1.Reset();
    delay(10);
  } else {
    webServer.handleClient();
    DisplayState &display = displayState;
    uint8_t key;

    // History is stored in hourly slots across the 56 graph columns.
    static uint8_t historyIndex = 0;
    static uint8_t historyPointCount = 0;
    static unsigned long historySampleCount = 0;

    temp_t tempValue;
    pres_t presValue;
    humi_t humiValue;
    int intValue;

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

    LocalTime = CET.toLocal(now());
    const time_t wakeGreetingLocalTime = getWakeGreetingLocalTime(LocalTime);
    resetWakeGreetingFlagsIfNeeded(wakeGreetingLocalTime);
    applyQueuedAlarmCommand();
    if (voiceCoilState == _vcModePulseHigh || voiceCoilState == _vcModePulseRelease) {
      updateVoiceCoilMotion();
    }

    if (SensorUpdate.check(MeasurementFreg)) {

      xSemaphoreTake(bufMutex, portMAX_DELAY);
      _t = mySensor.readTemperature();
      _h = mySensor.readHumidity();
      _p = mySensor.readPressure() / 100.0F;
      _l = lightMeter.readLightLevel();
      xSemaphoreGive(bufMutex);

      historySampleCount++;
      if (historyPointCount == 0 || historyIndex >= historyPointCount) {
        historyPointCount = historyIndex + 1;
      }

      display.averageTemp = updateRunningAverage(tempTable[historyIndex], _t, historySampleCount);
      tempTable[historyIndex] = display.averageTemp;

      display.averagePres = updateRunningAverage(presTable[historyIndex], _p, historySampleCount);
      presTable[historyIndex] = display.averagePres;

      display.averageHumi = updateRunningAverage(humiTable[historyIndex], _h, historySampleCount);
      humiTable[historyIndex] = display.averageHumi;


      //    PRINT("Slot: ", historyIndex);
      //    PRINT("   Samples: ", historySampleCount);
      //    PRINT("  Aver humi: ", humiTable[historyIndex]);
      //    PRINTLN;

      if (historySampleCount >= MaxMeasurements) {
        historySampleCount = 0;
        historyIndex++;
        if (historyIndex >= NumberOfPoints) {
          shiftHistoryTablesLeft();
          historyIndex = NumberOfPoints - 1;
          historyPointCount = NumberOfPoints - 1;
        }
        tempTable[historyIndex] = 0;
        presTable[historyIndex] = 0;
        humiTable[historyIndex] = 0;
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
      const bool screenSaverAllowed = ClockState != _Clock_sky_stars_init && ClockState != _Clock_sky_stars;
      const bool pirHigh = digitalRead(pirPin);
      const bool pirRisingEdge = pirHigh && !pirWasHigh;
      const bool screenSaverWasActive = !screenSaverNotActive;
      const unsigned long inactivityMs = millis() - lastTimeMove;
      display.intensity = IntensityMap(lux);
      logBrightnessReading("periodic", lux, display.intensity);
      if (display.intensity != display.lintensity) {
        //matrix.setIntensity(intensity);
        display.lintensity = display.intensity;
      }

      // The manual display-off mode must stay dark regardless of PIR motion.
      if (!displayEnabled) {
        digitalWrite(sledPin, HIGH);
        matrix.shutdown(true);
        screenSaverNotActive = false;
      } else if (!screenSaverAllowed) {
        ScreenSaver.start();
        digitalWrite(sledPin, LOW);
        matrix.shutdown(false);
        screenSaverNotActive = true;
        lastTimeMove = millis();
      } else {
        // check and activate screen saver mode max7219 -> shutdown mode and (re)set screenSaverNotActive flag for matrix.write
        if (pirHigh) {
          ScreenSaver.start();
          digitalWrite(sledPin, LOW);
          matrix.shutdown(false);
          screenSaverNotActive = true;
          lastTime = digitalClockString();
          lastTimeMove = millis();
          if (screenSaverWasActive && pirRisingEdge && prepareWakeGreeting(wakeGreetingLocalTime, inactivityMs)) {
            goBackState = _Clock_complete_info_init;
            ClockState = _Clock_wake_greeting_init;
          }
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
      pirWasHigh = pirHigh;
    }

    // check if the last NTP sync was successful, if not reduce the resync time to 60s
    if (SyncNTPError) NTPSyncPeriod = 60;
    else NTPSyncPeriod = NTPRESYNC;

    announceTimezoneChange();
    if (pendingTimezoneRefresh) {
      if (ClockState == _Clock_simple_time) {
        ClockState = _Clock_simple_time_init;
        pendingTimezoneRefresh = false;
      } else if (ClockState == _Clock_complete_info) {
        ClockState = _Clock_complete_info_init;
        pendingTimezoneRefresh = false;
      }
    }

    LocalTime = CET.toLocal(now());
    const uint32_t alarmDayKey = buildLocalDateKey(LocalTime);
    const uint16_t alarmMinuteOfDay = (uint16_t)hour(LocalTime) * 60U + (uint16_t)minute(LocalTime);
    const bool alarmAlreadyTriggered = alarmLastTriggeredDayKey == alarmDayKey
                                       && alarmLastTriggeredMinuteOfDay == alarmMinuteOfDay;
    if (alarmEnabled && !alarmAlreadyTriggered && !isAlarmActiveState(ClockState)
        && hour(LocalTime) == alarmHour && minute(LocalTime) == alarmMinute) {
      alarmLastTriggeredDayKey = alarmDayKey;
      alarmLastTriggeredMinuteOfDay = alarmMinuteOfDay;
      startAlarmTakeover(display, normalizeClockStateForReturn(ClockState));
    }

    // check if NTP sync is due?
    // If yes change clock status
    if (!isAlarmActiveState(ClockState) && NTPUpdateTask.check(NTPSyncPeriod)) {
      cancelModeChangeCoilPulse();
      ClockState = _Clock_NTP_Sync;
    }

    // check time dependant actions

    if (!isAlarmActiveState(ClockState)) {
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
        clockReadyForRemoteMessages = false;
        printClockTimes("NTP resync start");
        if (StartSyncNTP()) {  // error by NTP sync
          zoneInfo0.setText("NTP Sync ERROR", _SCROLL_LEFT, _TO_FULL, InfoTick1, I0s, I0e);
          zoneInfo0.Animate(true);
          SyncNTPError = true;
        } else {
          rememberTimezoneState();
          zoneInfo0.setText("NTP Sync OK", _SCROLL_LEFT, _TO_FULL, InfoTick1, I0s, I0e);
          zoneInfo0.Animate(true);
          printClockTimes("NTP resync OK");
          SyncNTPError = false;
        }
        ClockState = goBackState;
        PRINT("NTP Resync Completed\nNew mode=", ClockState);
        PRINTLN;
        printClockTimes("NTP resync done");
        break;

      case _Clock_Temp_init:
        clearScreen();
        clockReadyForRemoteMessages = false;
        StatTask.start(-DiagramDelay);
        goBackState = _Clock_Temp_init;
        ClockState = _Clock_Temp;
        display.dataMode = 0;
        break;

      case _Clock_Temp:
        clockReadyForRemoteMessages = true;

        if (StatTask.check(DiagramDelay)) {
          clearDisplayBuffer();
          updateHistoryBounds(display, historyPointCount);
          matrix.setCursor(0, 0);
          switch (display.dataMode) {
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
          for (ptr = 0; ptr < historyPointCount; ptr++) {
            switch (display.dataMode) {
              case 0:
                intValue = scaleHistoryValue(tempTable[ptr], display.tempMin, display.tempMax);
                break;
              case 1:
                intValue = scaleHistoryValue(presTable[ptr], display.presMin, display.presMax);
                break;
              case 2:
                intValue = scaleHistoryValue(humiTable[ptr], display.humiMin, display.humiMax);
                break;
            }

            //              PRINT("i: ", ptr);
            //              PRINT("  Table: ", intValue);
            //              PRINT("  min : ", tempMin);
            //              PRINT("  max : ", tempMax);
            //              PRINTLN;

            matrix.drawFastVLine(ptr + ShiftDiagram, 8 - intValue, intValue, HIGH);
          }
          display.updateDisplay = true;
        }
        if (keyboard(_Clock_alarm_menu_init, _Clock_ip_init, _Clock_none, _Clock_none) == SW_UP) {
          display.dataMode = (display.dataMode + 1) % 3;
          StatTask.check(-DiagramDelay);
        }
        break;

      case _Clock_alarm_menu_init:
        clearScreen();
        clockReadyForRemoteMessages = false;
        refreshAlarmMenuText(display);
        display.updateDisplay = zoneInfo0.Animate(false);
        goBackState = _Clock_alarm_menu_init;
        ClockState = _Clock_alarm_menu;
        break;

      case _Clock_alarm_menu:
        clockReadyForRemoteMessages = true;
        display.updateDisplay |= zoneInfo0.Animate(false);
        if (keyboard(_Clock_menu_init, _Clock_Temp_init, _Clock_none, _Clock_none) == SW_UP) {
          setAlarmConfig(!alarmEnabled, alarmHour, alarmMinute, true);
          refreshAlarmMenuText(display);
          display.updateDisplay |= zoneInfo0.Animate(false);
        }
        break;

      case _Clock_alarm_active_init:
        clearScreen();
        clockReadyForRemoteMessages = false;
        triggerAlarmBurst(display);
        goBackState = alarmReturnState;
        ClockState = _Clock_alarm_active;
        break;

      case _Clock_alarm_active:
        clockReadyForRemoteMessages = false;
        if (!alarmEnabled) {
          finishAlarmTakeover(display, true);
          break;
        }
        key = keyboard(_Clock_none, _Clock_none, _Clock_none, _Clock_none);
        if (key == SW_DOWN || key == SW_UP) {
          finishAlarmTakeover(display, true);
          break;
        }
        if (alarmFinalReturnPending) {
          if (AlarmFinalDisplay.check(AlarmFinalDisplayMs)) {
            finishAlarmTakeover(display, true);
            break;
          }
        } else if (AlarmRepeat.check(AlarmRepeatDelayMs)) {
          triggerAlarmBurst(display);
        }
        drawAlarmAnimation(display, false);
        break;

      case _Clock_ip_init:
        clearScreen();
        clockReadyForRemoteMessages = false;
        if (WiFi.isConnected()) {
          display.paramS = "IP " + WiFi.localIP().toString();
        } else {
          display.paramS = "IP No WiFi";
        }
        zoneInfo0.setText(display.paramS, _SCROLL_LEFT, _TO_FULL, InfoQuick, I0s, I0e);
        zoneInfo0.Animate(true);
        goBackState = _Clock_ip_init;
        ClockState = _Clock_ip;
        break;

      case _Clock_ip:
        clockReadyForRemoteMessages = true;
        display.updateDisplay |= zoneInfo0.Animate(false);
        if (zoneInfo0.AnimateDone()) {
          zoneInfo0.Reset();
          display.updateDisplay = true;
        }
        key = keyboard(_Clock_Temp_init, _Clock_simple_time_init, _Clock_none, _Clock_none);
        break;

      case _Clock_wake_greeting_init:
        clearScreen();
        clockReadyForRemoteMessages = false;
        if (wakeGreetingMessage[0] == '\0') {
          ClockState = _Clock_complete_info_init;
          break;
        }
        zoneInfo0.setText(wakeGreetingMessage, _SCROLL_LEFT, _TO_FULL, InfoQuick, I0s, I0e);
        display.updateDisplay = zoneInfo0.Animate(false);
        WakeGreetingDisplay.start();
        if (wakeGreetingSoundFolder > 0) {
          DFPlayer.playFolder(wakeGreetingSoundFolder, (uint8_t)wakeGreetingSoundFile);
        }
        goBackState = _Clock_complete_info_init;
        ClockState = _Clock_wake_greeting;
        break;

      case _Clock_wake_greeting:
        clockReadyForRemoteMessages = false;
        {
          const bool greetingExpired = WakeGreetingDisplay.check(WakeGreetingDurationMs);
          display.updateDisplay |= zoneInfo0.Animate(false);
          if (!greetingExpired && zoneInfo0.AnimateDone()) {
            zoneInfo0.Reset();
            display.updateDisplay = true;
          }
          if (greetingExpired) {
            ClockState = _Clock_complete_info_init;
          }
        }
        break;

#if EnableSoundTestMode
      case _Clock_sound_test_init:
        clearScreen();
        clockReadyForRemoteMessages = false;
        resetSoundTestSelection();
        updateSoundTestDisplayText(display);
        zoneInfo0.setText(display.paramS, _PRINT, _NONE_MOD, InfoTick, I0s, I0e);
        display.updateDisplay = zoneInfo0.Animate(false);
        goBackState = _Clock_sound_test_init;
        ClockState = _Clock_sound_test;
        break;

      case _Clock_sound_test:
        clockReadyForRemoteMessages = true;
        key = keyboard(_Clock_complete_info_init, _Clock_sky_stars_init, _Clock_none, _Clock_none);
        if (key == SW_DOWN) {
          playCurrentSoundTestSelection();
        } else if (key == SW_UP) {
          advanceSoundTestSelection();
          updateSoundTestDisplayText(display);
          zoneInfo0.setText(display.paramS, _PRINT, _NONE_MOD, InfoTick, I0s, I0e);
          display.updateDisplay = zoneInfo0.Animate(false);
        } else if (key == DIR_CW || key == DIR_CCW) {
          stopSoundTestPlayback();
        }
        break;
#endif

      case _Clock_remote_message_init:
        clearScreen();
        clockReadyForRemoteMessages = false;
        if (!takePendingRemoteMessage()) {
          PRINTS("Remote Msg init without pending message\n");
          ClockState = remoteMessageReturnState;
          break;
        }
        PRINT("Remote Msg init: ", activeRemoteMessage);
        PRINTLN;
        DFPlayer.playFolder(3, 102);
        startVoiceCoilMotion();
        remoteMessageMotionActive = true;
        RemoteMessageMotion.start();
        zoneInfo0.setText(activeRemoteMessage, _SCROLL_LEFT, _TO_FULL, InfoQuick, I0s, I0e);
        zoneInfo0.Animate(true);
        remoteMessageWaitingForRepeat = false;
        display.updateDisplay = true;
        goBackState = remoteMessageReturnState;
        ClockState = _Clock_remote_message;
        break;

      case _Clock_remote_message:
        clockReadyForRemoteMessages = true;
        display.updateDisplay |= zoneInfo0.Animate(false);
        if (remoteMessageMotionActive) {
          if (RemoteMessageMotion.check(RemoteMessageMotionMs)) {
            remoteMessageMotionActive = false;
            stopVoiceCoilMotion();
          } else {
            updateVoiceCoilMotion();
          }
        }
        key = keyboard(_Clock_none, _Clock_none, _Clock_none, _Clock_none);
        if (key == SW_DOWN) {
          char acknowledgedMessage[sizeof(activeRemoteMessage)];
          xSemaphoreTake(messageMutex, portMAX_DELAY);
          strncpy(acknowledgedMessage, activeRemoteMessage, sizeof(acknowledgedMessage) - 1);
          acknowledgedMessage[sizeof(acknowledgedMessage) - 1] = '\0';
          remoteMessageActive = false;
          activeRemoteMessage[0] = '\0';
          xSemaphoreGive(messageMutex);
          queueRemoteMessageAck(acknowledgedMessage);
          remoteMessageWaitingForRepeat = false;
          remoteMessageMotionActive = false;
          stopVoiceCoilMotion();
          ClockState = remoteMessageReturnState;
        } else if (zoneInfo0.AnimateDone()) {
          if (!remoteMessageWaitingForRepeat) {
            RemoteMessageRepeat.start();
            remoteMessageWaitingForRepeat = true;
          } else if (RemoteMessageRepeat.check(RemoteMessageRepeatDelay)) {
            zoneInfo0.Reset();
            remoteMessageWaitingForRepeat = false;
            display.updateDisplay = true;
          }
        }
        break;

      case _Clock_simple_time_init:

        clearScreen();
        clockReadyForRemoteMessages = false;
        LocalTime = CET.toLocal(now());
        initSimpleTimeDisplay(display, matrix, LocalTime, SyncNTPError, digitalRead(modePin));

        //IntensityCheck.start();
        SnakeUpdate.start();
        startVoiceCoilMotion();
        randomSeed(analogRead(pinRandom));  // Initialize random generator
        SnakeState = _sInit;

        PRINTS("Simple Time Init closed\n");

        goBackState = ClockState;
        ClockState = _Clock_simple_time;
        break;

      case _Clock_simple_time:
        clockReadyForRemoteMessages = true;
        LocalTime = CET.toLocal(now());
        updateSimpleTimeDisplay(display, LocalTime, zoneClockH0, zoneClockH1, zoneClockM0, zoneClockM1, zoneClockS0, zoneClockS1);
        updateVoiceCoilMotion();

        // Snake animation
        if (SnakeUpdate.check(SnakeWait) || SnakeState == _sRunA) {
          matrix.setClip(SNs, SNe, 0, 8);
          display.updateDisplay = true;
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
              startVoiceCoilMotion();

              break;

            case _sRunA:
              //                        PRINTS("\n State A");
              ptr = nextPtr;
              nextPtr = next(ptr, snakeLength);
              matrix.drawPixel(snakeX[ptr], snakeY[ptr], HIGH);  // Draw the head of the snake
              SnakeState = _sRunB;

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
                stopVoiceCoilMotion();
                matrix.fillScreen(HIGH);
                SnakeState = _sFail;
              } else {
                snakeRound = (snakeRound + 1) % SankeNextRound;
                if (snakeRound == 0) snakeLength = snakeLength + 1;
                if (snakeLength >= MaxSnake) {
                  stopVoiceCoilMotion();
                  matrix.fillScreen(HIGH);
                  SnakeState = _sFail;
                } else SnakeState = _sRunA;
              }
              break;
            case _sFail:
              //                        PRINTS("\n Fail");
              stopVoiceCoilMotion();
              SnakeState = _sInit;
              break;
          }
        }

        key = keyboard(_Clock_ip_init, _Clock_complete_info_init, _Clock_none, _Clock_none);
        break;

      case _Clock_menu_init:
        clearScreen();
        clockReadyForRemoteMessages = false;
        zoneInfo0.setText("Ding:", _SCROLL_RIGHT, _TO_LEFT, InfoTick, I0s, I0e);
        zoneInfo0.Animate(true);
        display.updateDisplay = false;
        goBackState = _Clock_menu_init;
        ClockState = _Clock_menu;
        zoneInfo0.setText("Ding:", _PRINT, _NONE_MOD, InfoTick, MEs, MEe);
        display.paramS = DingOnOff ? "On" : "Off";
        zoneInfo1.setText(display.paramS, _BLINK, _NONE_MOD, InfoSlow, PAs, PAe);
        break;

      case _Clock_menu:
        clockReadyForRemoteMessages = true;
        display.updateDisplay = zoneInfo1.Animate(false);
        if (keyboard(_Clock_menu_display_init, _Clock_alarm_menu_init, _Clock_none, _Clock_none) == SW_UP) {
          DingOnOff = !DingOnOff;
          display.paramS = DingOnOff ? "On" : "Off";
          zoneInfo1.setText(display.paramS, _BLINK, _NONE_MOD, InfoSlow, PAs, PAe);
          savePreferences();
          display.updateDisplay = true;
        }
        break;

      case _Clock_menu_display_init:
        clearScreen();
        clockReadyForRemoteMessages = false;
        zoneInfo0.setText("LED:", _SCROLL_RIGHT, _TO_LEFT, InfoTick, I0s, I0e);
        zoneInfo0.Animate(true);
        display.updateDisplay = false;
        goBackState = _Clock_menu_display_init;
        ClockState = _Clock_menu_display;
        zoneInfo0.setText("LED:", _PRINT, _NONE_MOD, InfoTick, MEs, MEe);
        display.paramS = displayEnabled ? "On" : "Off";
        zoneInfo1.setText(display.paramS, _BLINK, _NONE_MOD, InfoSlow, PAs, PAe);
        break;

      case _Clock_menu_display:
        clockReadyForRemoteMessages = true;
        display.updateDisplay = zoneInfo1.Animate(false);
        if (keyboard(_Clock_sky_stars_init, _Clock_menu_init, _Clock_none, _Clock_none) == SW_UP) {
          if (displayEnabled) {
            display.paramS = "Off";
            zoneInfo1.setText(display.paramS, _BLINK, _NONE_MOD, InfoSlow, PAs, PAe);
            display.updateDisplay = true;
            ClockState = _Clock_display_off_init;
          }
        }
        break;

      case _Clock_sky_stars_init:
        clearScreen();
        clockReadyForRemoteMessages = false;
        skyStarsMode.init(matrix);
        display.updateDisplay = true;
        goBackState = ClockState;
        ClockState = _Clock_sky_stars;
        break;

      case _Clock_sky_stars:
        clockReadyForRemoteMessages = true;
        display.updateDisplay |= skyStarsMode.update(matrix);
#if EnableSoundTestMode
        key = keyboard(_Clock_sound_test_init, _Clock_menu_display_init, _Clock_none, _Clock_none);
#else
        key = keyboard(_Clock_complete_info_init, _Clock_menu_display_init, _Clock_none, _Clock_none);
#endif
        break;

      case _Clock_display_off_init:
        clockReadyForRemoteMessages = true;
        stopVoiceCoilMotion();
        displayEnabled = false;
        displayFadeIntensity = display.lintensity;
        if (displayFadeIntensity > display.intensity) displayFadeIntensity = display.intensity;
        matrix.setIntensity(displayFadeIntensity);
        DisplayOffFade.start();
        ClockState = _Clock_display_off;
        break;

      case _Clock_display_off:
        clockReadyForRemoteMessages = true;
        key = keyboard(_Clock_none, _Clock_none, _Clock_none, _Clock_none);
        if (key == SW_UP || key == DIR_CW || key == DIR_CCW) {
          wakeDisplayToMenu(display);
          break;
        }
        if (DisplayOffFade.check(90)) {
          if (displayFadeIntensity > 0) {
            displayFadeIntensity--;
            matrix.setIntensity(displayFadeIntensity);
            matrix.write();
          } else {
            matrix.fillScreen(LOW);
            matrix.write();
            matrix.shutdown(true);
            screenSaverNotActive = false;
          }
        }
        break;

      case _Clock_complete_info_init:

        clearScreen();
        clockReadyForRemoteMessages = false;
        LocalTime = CET.toLocal(now());
        initCompleteInfoDisplay(display, matrix, LocalTime, SyncNTPError, digitalRead(modePin));

        display.dataMode = 0;
        DataDisplayTask.start(-2000);

        //IntensityCheck.start();

        goBackState = ClockState;
        ClockState = _Clock_complete_info;
        PRINTS("Complete Time Init closed\n");

        break;

      case _Clock_complete_info:
        clockReadyForRemoteMessages = true;

        // PRINTS("Complete Info\n");
        LocalTime = CET.toLocal(now());
        updateCompleteInfoClock(display, matrix, LocalTime, zoneClockH0, zoneClockH1, zoneClockM0, zoneClockM1);

        if (DataDisplayTask.check(FullInfoDelay)) {
          switch (display.dataMode) {
            case 0:
              sprintf(display.dataStr, "%s%02d", monthShortStr(month(LocalTime)), day(LocalTime));
              textEffect = _SCROLL_LEFT;
              break;
            case 1:
              sprintf(display.dataStr, "%s%02d", dayShortStr(weekday(LocalTime)), day(LocalTime));
              textEffect = _SCROLL_LEFT;
              break;
            case 99:
              sprintf(display.dataStr, "%s", monthStr(month(LocalTime)));
              textEffect = _SCROLL_LEFT;
              break;
            case 2:
              // sprintf (DataStr, "%c%d%c", 160, (int)(mySensor.readTemperature()+0.5), 161);
              tempValue = round(mySensor.readTemperature());
              sprintf(display.dataStr, "%d%c", (int)(tempValue), GLYPH_DEGREE);
              textEffect = tempValue > round(display.averageTemp) ? _SCROLL_UP : tempValue < round(display.averageTemp) ? _SCROLL_DOWN
                                                                                                        : _SCROLL_LEFT;
              break;
            case 3:
              //sprintf (DataStr, "%c%.0f%c", 162, mySensor.readPressure() / 100.0F, 163);

              presValue = round(mySensor.readPressure() / 100.0F);
              sprintf(display.dataStr, "%.0f%c", presValue, GLYPH_HPA);
              textEffect = presValue > round(display.averagePres) ? _SCROLL_UP : presValue < round(display.averagePres) ? _SCROLL_DOWN
                                                                                                        : _SCROLL_LEFT;
              //textEffect = _SCROLL_LEFT;
              break;
            case 4:
              //sprintf (DataStr, "%c%d%%", 166, (int)(mySensor.readHumidity()+0.5));
              humiValue = round(mySensor.readHumidity());
              sprintf(display.dataStr, "%d%%", (int)(humiValue));

              //                 PRINT("Day:", dayNumber);
              //                 PRINT("  Srednia:", round(averageHumi));
              //                 PRINT("  Sensor: ", humiValue)
              //                 PRINTLN;

              textEffect = humiValue > round(display.averageHumi) ? _SCROLL_UP : humiValue < round(display.averageHumi) ? _SCROLL_DOWN
                                                                                                        : _SCROLL_LEFT;
              //textEffect = _SCROLL_LEFT;
              break;
            case 6:
              int measurement = hallRead();
              PRINT("Hall sensor measurement: ", measurement);
              sprintf(display.dataStr, "H:%02d", measurement);
              textEffect = _SCROLL_LEFT;
              break;
          }

          infoTime = textEffect == _SCROLL_LEFT ? InfoQuick : InfoSlow;
          zoneInfo1.setText(display.dataStr, textEffect, _TO_LEFT, infoTime, I1s, I1e);
          display.dataMode = (display.dataMode + 1) % 5;
        }
        display.updateDisplay |= zoneInfo1.Animate(false);
#if EnableSoundTestMode
        if (keyboard(_Clock_simple_time_init, _Clock_sound_test_init, _Clock_none, _Clock_none) == SW_UP) {
#else
        if (keyboard(_Clock_simple_time_init, _Clock_sky_stars_init, _Clock_none, _Clock_none) == SW_UP) {
#endif
          display.dataMode = (display.dataMode + 1) % 5;
          DataDisplayTask.start(-FullInfoDelay);
        }
        break;

        //      case _Clock_idle:
        //          break;

      default:;

    }

    if (clockReadyForRemoteMessages && hasPendingRemoteMessage()) {
      remoteMessageReturnState = normalizeClockStateForReturn(ClockState);
      PRINT("Remote Msg takeover from state ", remoteMessageReturnState);
      PRINTLN;
#if EnableSoundTestMode
      if (ClockState == _Clock_sound_test) stopSoundTestPlayback();
#endif
      cancelModeChangeCoilPulse();
      ClockState = _Clock_remote_message_init;
    }

    updatePendingModeChangeCoilPulse();

    if (display.updateDisplay) {
      if (displayEnabled && screenSaverNotActive) {
        matrix.write();
        matrix.setIntensity(display.intensity);
      }
      display.updateDisplay = false;
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
  const TickType_t xTicksToWait = pdMS_TO_TICKS(Time2PollMQTT);
  float _tt, _hh, _pp, _ll;
  unsigned long lastPublishAt = 0;

  UBaseType_t uxHighWaterMark;

  while (true) {
    uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    // Serial.print("\nStack IN:"); Serial.println(uxHighWaterMark);
    vTaskDelay(xTicksToWait);
    if (MQTT_connect()) {
      Adafruit_MQTT_Subscribe *subscription;
      while ((subscription = mqtt.readSubscription(10))) {
        if (subscription == &msgMQTTSub) {
          processIncomingRemoteMessage((char *)msgMQTTSub.lastread);
          PRINT("MQTT message: ", (char *)msgMQTTSub.lastread);
          PRINTLN;
        }
      }

      publishQueuedRemoteMessageAck();
      publishQueuedRemoteMessageClear();

      if (millis() - lastPublishAt >= Time2UpdateMQTT) {
        PRINTS("MQTT publishing\n");
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
        lastPublishAt = millis();
      }
    }
    uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    // Serial.print("\nStack OUT:"); Serial.println(uxHighWaterMark);
  }
}

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
boolean MQTT_connect() {
  int8_t ret;

  mqtt.subscribe(&ledMQTT);
  mqtt.subscribe(&msgMQTTSub);

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
