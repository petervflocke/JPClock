#pragma once

#include <Arduino.h>
#include <TimeLib.h>

#include "myClock.h"
#include <Adafruit_GFX.h>
#include <PPMax72xxPanel.h>
#include <PPmax72xxAnimate.h>

struct DisplayState {
  int valueH = 0;
  int valueM = 0;
  int valueS = 0;
  int lvalueH = -1;
  int lvalueM = -1;
  int lvalueS = -1;

  bool flasher = false;
  uint8_t intensity = 0;
  uint8_t lintensity = 0;
  bool updateDisplay = false;

  char dataStr[41] = "xx:xx:xx Xxx xx Xxx xxxx                ";
  uint8_t dataMode = 0;

  temp_t tempMin = +10000;
  temp_t tempMax = -10000;
  humi_t humiMin = +10000;
  humi_t humiMax = -10000;
  pres_t presMin = +10000;
  pres_t presMax = -10000;

  temp_t averageTemp = 0;
  humi_t averageHumi = 0;
  pres_t averagePres = 0;

  String paramS = "On";
};

bool updateDisplayTime(DisplayState &state, int &value, int &lastValue, int newValue, char what,
                       PPmax72xxAnimate &zoneClockH0, PPmax72xxAnimate &zoneClockH1,
                       PPmax72xxAnimate &zoneClockM0, PPmax72xxAnimate &zoneClockM1,
                       PPmax72xxAnimate &zoneClockS0, PPmax72xxAnimate &zoneClockS1);

void initSimpleTimeDisplay(DisplayState &state, PPMax72xxPanel &matrix, time_t localTime, bool syncNtpError, bool modePinHigh);
void updateSimpleTimeDisplay(DisplayState &state, time_t localTime,
                             PPmax72xxAnimate &zoneClockH0, PPmax72xxAnimate &zoneClockH1,
                             PPmax72xxAnimate &zoneClockM0, PPmax72xxAnimate &zoneClockM1,
                             PPmax72xxAnimate &zoneClockS0, PPmax72xxAnimate &zoneClockS1);

void initCompleteInfoDisplay(DisplayState &state, PPMax72xxPanel &matrix, time_t localTime, bool syncNtpError, int modePinState);
void updateCompleteInfoClock(DisplayState &state, PPMax72xxPanel &matrix, time_t localTime,
                             PPmax72xxAnimate &zoneClockH0, PPmax72xxAnimate &zoneClockH1,
                             PPmax72xxAnimate &zoneClockM0, PPmax72xxAnimate &zoneClockM1);
