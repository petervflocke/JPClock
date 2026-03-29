# JPClock

ESP32-based matrix clock built with PlatformIO. The firmware drives an 8-module MAX72xx LED matrix, reads ambient and environmental sensors, keeps time from NTP with Central European DST rules, plays quarter/hour chimes through a DFPlayer Mini, exposes telemetry over Adafruit IO MQTT, and supports OTA updates.

Demo: https://youtu.be/KMVDdp78MfU

## What The Firmware Does

The implementation in [src/main.cpp](/home/peter/Development/platformio/JPClock/src/main.cpp) is centered around a small state machine with these display modes:

- `complete info`: main clock view with animated hours/minutes and a rotating info line
- `simple time`: full `HH:MM:SS` display plus a snake animation on the right side of the matrix
- `menu`: toggle the chime function on or off
- `temp`: graph view for temperature, pressure, and humidity history
- `NTP sync`: background/manual resynchronization state

The info line in `complete info` cycles through:

- month/day
- weekday/day
- temperature
- pressure
- humidity

The firmware also:

- auto-adjusts matrix brightness from the BH1750 light sensor
- blanks the display after no PIR motion for `300` seconds
- stores Wi-Fi credentials and chime preference in ESP32 `Preferences`
- publishes measurements and status to Adafruit IO over TLS
- supports Arduino OTA once the device is on Wi-Fi

## Hardware Used By The Code

The current source and pin definitions in [include/myClock.h](/home/peter/Development/platformio/JPClock/include/myClock.h) expect:

- ESP32 dev board: `az-delivery-devkit-v4`
- 8 x MAX7219/MAX7221 8x8 LED modules in one horizontal chain
- BH1750 light sensor on I2C
- BME280 sensor on I2C address `0x76`
- DFPlayer Mini MP3 module with SD card for clock chimes
- rotary encoder with push button
- PIR motion sensor
- analog output / voice coil related circuitry driven from the ESP32 DAC pins

Pin mapping from the header:

- Matrix CS: `GPIO5`
- Rotary encoder: `GPIO13` and `GPIO15`
- Encoder button: `GPIO12`
- Mode/WPS select input: `GPIO27`
- PIR input: `GPIO14`
- DFPlayer RX/TX: `GPIO16` / `GPIO17`
- I2C SDA/SCL: `GPIO21` / `GPIO22`
- Status LED: `GPIO2`
- DAC outputs: `GPIO25` and `GPIO26`

## Startup And Wi-Fi Behavior

On boot the firmware:

1. Loads saved preferences (`DingOnOff`, SSID, password).
2. Initializes the matrix, sensors, DFPlayer, input devices, and queue/interrupt handlers.
3. Optionally enters Wi-Fi WPS pairing mode.
4. Connects to Wi-Fi.
5. Performs an initial NTP sync using `europe.pool.ntp.org`.
6. Starts the MQTT task and Arduino OTA handler.

Wi-Fi handling is split into two modes:

- Normal boot: uses saved credentials from `Preferences`, falling back to the values defined in `include/credentials.h`.
- WPS boot: during the startup prompt, pressing the encoder button enables ESP32 WPS pairing and stores the newly acquired credentials.

The code also contains a special boot path controlled by `modePin` (`GPIO27`). If that pin is held low during startup, the firmware skips the WPS prompt and uses the compile-time Wi-Fi credentials directly.

## Timekeeping

Time is synchronized from NTP and then converted with `Timezone` rules for:

- `CEST`: last Sunday in March
- `CET`: last Sunday in October

If an NTP sync fails, the firmware retries more aggressively by reducing the resync interval from `3600` seconds to `60` seconds until synchronization succeeds again.

## Controls

The rotary encoder/button is used to move between views:

- In `complete info`, clockwise goes to `simple time`, counter-clockwise goes to `menu`, and button release advances the info line immediately.
- In `simple time`, clockwise goes to `menu` and counter-clockwise goes to `complete info`.
- In `menu`, clockwise goes to `complete info`, counter-clockwise goes to `temp`, and button release toggles hourly/quarter chime on or off.
- In `temp`, clockwise goes to `menu`, counter-clockwise goes to `simple time`, and button release switches between temperature, pressure, and humidity graphs.

## Audio / Chimes

The DFPlayer Mini is configured for SD-card playback. The clock expects MP3 files in numbered folders:

- folder `1`: hour count chimes
- folder `2`: quarter chimes
- folder `3`: startup / status sounds

The repository includes candidate source audio in [`sounds/`](/home/peter/Development/platformio/JPClock/sounds) and prepared assets in [`sounds/final/`](/home/peter/Development/platformio/JPClock/sounds/final), but those files are not automatically copied to a DFPlayer SD card by PlatformIO.

By default, chimes are allowed only between `09:00` and `22:59` because the code checks `hour >= 9 && hour < 23`.

## MQTT / Adafruit IO

The firmware publishes to Adafruit IO feeds defined in [include/myClock.h](/home/peter/Development/platformio/JPClock/include/myClock.h):

- `/feeds/temperature`
- `/feeds/humidity`
- `/feeds/pressure`
- `/feeds/brightness`
- `/feeds/data`
- `/feeds/onoff`

It also defines a subscription for:

- `/feeds/led`

The MQTT update task runs every `60000` ms and sends the last measured sensor values plus display on/off state.

## Project Layout

- [src/main.cpp](/home/peter/Development/platformio/JPClock/src/main.cpp): main firmware, state machine, networking, OTA, sensors, display logic
- [include/myClock.h](/home/peter/Development/platformio/JPClock/include/myClock.h): pinout, timing constants, feed names, state enums
- [include/credentials.h](/home/peter/Development/platformio/JPClock/include/credentials.h): local credentials and Adafruit IO keys used by the current build
- [lib/myScheduler/src/myScheduler.h](/home/peter/Development/platformio/JPClock/lib/myScheduler/src/myScheduler.h): lightweight periodic scheduler helper
- [lib/PPMax72xxPanel/](/home/peter/Development/platformio/JPClock/lib/PPMax72xxPanel): local fork of the MAX72xx panel library with clipping and scrolling support
- [platformio.ini](/home/peter/Development/platformio/JPClock/platformio.ini): PlatformIO environment and library dependencies

## Building

The configured PlatformIO environment is `az-delivery-devkit-v4`.

```bash
pio run
pio run -t upload
pio device monitor -b 115200
```

Dependencies declared in [platformio.ini](/home/peter/Development/platformio/JPClock/platformio.ini):

- `Time`
- `Timezone`
- `Adafruit GFX Library`
- `Adafruit MQTT Library`
- `NTPClient`
- `MD_REncoder`
- `BH1750`
- `Adafruit BME280 Library`
- `DFRobotDFPlayerMini`

## Configuration Notes

- `include/credentials.h` is used as a fallback source for Wi-Fi and Adafruit IO credentials.
- The current repository version contains real-looking secrets in that file. That is convenient for a private local build, but it is not appropriate for a public repository.
- If this project is meant to be shared, move credentials out of source control and commit a redacted template instead.

## Notes From The Current Code

- `DEBUG_ON` is disabled by default in [include/myClock.h](/home/peter/Development/platformio/JPClock/include/myClock.h).
- The matrix is configured as 8 horizontal displays and manually remapped/rotated in `setup()`.
- Sensor history is stored in 56 points, matching the usable graph width defined by `NumberOfPoints`.
- The code uses a FreeRTOS task for MQTT and interrupt-driven input handling for the encoder/button.
- OTA is initialized without authentication in the current implementation.
