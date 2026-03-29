#include "display_state.h"

bool updateDisplayTime(DisplayState &state, int &value, int &lastValue, int newValue, char what,
                       PPmax72xxAnimate &zoneClockH0, PPmax72xxAnimate &zoneClockH1,
                       PPmax72xxAnimate &zoneClockM0, PPmax72xxAnimate &zoneClockM1,
                       PPmax72xxAnimate &zoneClockS0, PPmax72xxAnimate &zoneClockS1) {
  String sValue;
  if (value != newValue) {
    value = newValue;
    if (lastValue / 10 != value / 10) {
      sValue = String(lastValue / 10) + "\n" + String(value / 10);
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
    sValue = String(lastValue % 10) + "\n" + String(value % 10);
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
    lastValue = value;
    return true;
  }
  return false;
}

void initSimpleTimeDisplay(DisplayState &state, PPMax72xxPanel &matrix, time_t localTime, bool syncNtpError, bool modePinHigh) {
  state.valueH = hour(localTime);
  state.valueM = minute(localTime);
  state.valueS = second(localTime);
  state.lvalueH = state.valueH;
  state.lvalueM = state.valueM;
  state.lvalueS = state.valueS;

  matrix.drawChar(H1s, 0, (char)('0' + state.valueH / 10), HIGH, LOW, 1);
  matrix.drawChar(H0s, 0, (char)('0' + state.valueH % 10), HIGH, LOW, 1);
  matrix.drawChar(M1s, 0, (char)('0' + state.valueM / 10), HIGH, LOW, 1);
  matrix.drawChar(M0s, 0, (char)('0' + state.valueM % 10), HIGH, LOW, 1);
  matrix.drawChar(S1s, 0, (char)('0' + state.valueS / 10), HIGH, LOW, 1);
  matrix.drawChar(S0s, 0, (char)('0' + state.valueS % 10), HIGH, LOW, 1);
  matrix.drawPixel(M0e + 1, 0, HIGH);
  matrix.drawPixel(M0e + 1, 1, HIGH);
  matrix.drawPixel(H0e + 1, 2, HIGH);
  matrix.drawPixel(H0e + 1, 5, HIGH);
  matrix.drawPixel(H0e + 1, 7, syncNtpError || !modePinHigh);

  state.updateDisplay = true;
}

void updateSimpleTimeDisplay(DisplayState &state, time_t localTime,
                             PPmax72xxAnimate &zoneClockH0, PPmax72xxAnimate &zoneClockH1,
                             PPmax72xxAnimate &zoneClockM0, PPmax72xxAnimate &zoneClockM1,
                             PPmax72xxAnimate &zoneClockS0, PPmax72xxAnimate &zoneClockS1) {
  updateDisplayTime(state, state.valueH, state.lvalueH, hour(localTime), 'H',
                    zoneClockH0, zoneClockH1, zoneClockM0, zoneClockM1, zoneClockS0, zoneClockS1);
  updateDisplayTime(state, state.valueM, state.lvalueM, minute(localTime), 'M',
                    zoneClockH0, zoneClockH1, zoneClockM0, zoneClockM1, zoneClockS0, zoneClockS1);
  if (updateDisplayTime(state, state.valueS, state.lvalueS, second(localTime), 'S',
                        zoneClockH0, zoneClockH1, zoneClockM0, zoneClockM1, zoneClockS0, zoneClockS1)) {
    state.updateDisplay = true;
  }

  state.updateDisplay |= zoneClockH0.Animate(false);
  state.updateDisplay |= zoneClockH1.Animate(false);
  state.updateDisplay |= zoneClockM0.Animate(false);
  state.updateDisplay |= zoneClockM1.Animate(false);
  state.updateDisplay |= zoneClockS0.Animate(false);
  state.updateDisplay |= zoneClockS1.Animate(false);
}

void initCompleteInfoDisplay(DisplayState &state, PPMax72xxPanel &matrix, time_t localTime, bool syncNtpError, int modePinState) {
  state.valueH = hour(localTime);
  state.valueM = minute(localTime);
  state.valueS = second(localTime);
  state.lvalueH = state.valueH;
  state.lvalueM = state.valueM;
  state.lvalueS = state.valueS;

  matrix.drawChar(H1s, 0, (char)('0' + state.valueH / 10), HIGH, LOW, 1);
  matrix.drawChar(H0s, 0, (char)('0' + state.valueH % 10), HIGH, LOW, 1);
  matrix.drawChar(M1s, 0, (char)('0' + state.valueM / 10), HIGH, LOW, 1);
  matrix.drawChar(M0s, 0, (char)('0' + state.valueM % 10), HIGH, LOW, 1);
  matrix.drawPixel(H0e + 1, 7, syncNtpError);
  PRINT("SyncNTPError =", syncNtpError);
  PRINT("   Mode =", modePinState);
  PRINTLN;

  state.updateDisplay = true;
}

void updateCompleteInfoClock(DisplayState &state, PPMax72xxPanel &matrix, time_t localTime,
                             PPmax72xxAnimate &zoneClockH0, PPmax72xxAnimate &zoneClockH1,
                             PPmax72xxAnimate &zoneClockM0, PPmax72xxAnimate &zoneClockM1) {
  updateDisplayTime(state, state.valueH, state.lvalueH, hour(localTime), 'H',
                    zoneClockH0, zoneClockH1, zoneClockM0, zoneClockM1, zoneClockM0, zoneClockM1);
  updateDisplayTime(state, state.valueM, state.lvalueM, minute(localTime), 'M',
                    zoneClockH0, zoneClockH1, zoneClockM0, zoneClockM1, zoneClockM0, zoneClockM1);
  if (state.valueS != second(localTime)) {
    state.valueS = second(localTime);
    state.lvalueS = state.valueS;
    state.flasher = !state.flasher;
    state.updateDisplay = true;
    matrix.setClip(H0e + 1, H0e + 2, 0, 8);
    matrix.drawPixel(H0e + 1, 2, state.flasher);
    matrix.drawPixel(H0e + 1, 5, state.flasher);
  }

  state.updateDisplay |= zoneClockH0.Animate(false);
  state.updateDisplay |= zoneClockH1.Animate(false);
  state.updateDisplay |= zoneClockM0.Animate(false);
  state.updateDisplay |= zoneClockM1.Animate(false);
}
