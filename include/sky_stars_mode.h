#pragma once

#include <Arduino.h>

#include <Adafruit_GFX.h>
#include <PPMax72xxPanel.h>
#include <myScheduler.h>

#include "myClock.h"

class SkyStarsMode {
 public:
  void init(PPMax72xxPanel &matrix);
  bool update(PPMax72xxPanel &matrix);

 private:
  struct Star {
    bool active = false;
    bool visible = false;
    bool twinkle = false;
    int8_t x = -1;
    int8_t y = -1;
    unsigned long expiresAt = 0;
    unsigned long nextBlinkAt = 0;
    uint32_t serial = 0;
  };

  struct Comet {
    bool active = false;
    int8_t headX = 0;
    int8_t headY = 0;
    int8_t dx = 1;
    int8_t dy = 0;
    uint8_t length = 4;
  };

  Schedular spawnTimer_{_Millis};
  Schedular cometStepTimer_{_Millis};
  Star stars_[SkyStarsMax];
  Comet comet_;
  long nextSpawnDelay_ = SkyStarsWaitMin;
  long cometStepDelay_ = 70;
  unsigned long nextCometAt_ = 0;
  uint32_t nextStarSerial_ = 1;
  uint8_t burstRemaining_ = 0;

  void scheduleNextSpawn(bool firstSpawn);
  void scheduleNextComet();
  void maybeSpawnStar(PPMax72xxPanel &matrix, unsigned long now, bool &changed);
  void spawnStar(PPMax72xxPanel &matrix, unsigned long now);
  void expireStars(unsigned long now, bool &changed);
  void updateTwinkle(unsigned long now, bool &changed);
  void maybeStartComet(PPMax72xxPanel &matrix, unsigned long now, bool &changed);
  void startComet(PPMax72xxPanel &matrix, unsigned long now);
  void updateComet(PPMax72xxPanel &matrix, bool &changed);
  void redraw(PPMax72xxPanel &matrix);
  bool isOccupied(PPMax72xxPanel &matrix, int8_t x, int8_t y) const;
  uint8_t activeStarCount() const;
  uint8_t findFreeSlot() const;
  uint8_t findOldestStar() const;
  bool hasTwinkleStar() const;
  unsigned long chooseLifetime() const;
  void pickCoordinate(PPMax72xxPanel &matrix, int8_t &x, int8_t &y) const;
};
