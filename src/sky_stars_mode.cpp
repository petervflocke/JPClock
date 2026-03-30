#include "sky_stars_mode.h"

namespace {

bool timeReached(unsigned long now, unsigned long target) {
  return static_cast<long>(now - target) >= 0;
}

}  // namespace

void SkyStarsMode::init(PPMax72xxPanel &matrix) {
  randomSeed(analogRead(pinRandom));

  for (uint8_t i = 0; i < SkyStarsMax; i++) {
    stars_[i] = Star{};
  }

  comet_ = Comet{};
  nextStarSerial_ = 1;
  burstRemaining_ = 0;

  scheduleNextSpawn(true);
  scheduleNextComet();
  redraw(matrix);
}

bool SkyStarsMode::update(PPMax72xxPanel &matrix) {
  const unsigned long now = millis();
  bool changed = false;

  expireStars(now, changed);
  updateTwinkle(now, changed);
  maybeStartComet(matrix, now, changed);
  updateComet(matrix, changed);
  maybeSpawnStar(matrix, now, changed);

  if (changed) redraw(matrix);
  return changed;
}

void SkyStarsMode::scheduleNextSpawn(bool firstSpawn) {
  if (firstSpawn) {
    nextSpawnDelay_ = random(40, 180);
    spawnTimer_.start(-nextSpawnDelay_);
    return;
  }

  if (burstRemaining_ > 0) {
    nextSpawnDelay_ = random(70, 170);
    burstRemaining_--;
    return;
  }

  const uint8_t roll = random(0, 100);
  if (roll < 18) {
    burstRemaining_ = random(1, 4);
    nextSpawnDelay_ = random(70, 160);
  } else if (roll > 92) {
    nextSpawnDelay_ = random(900, 1700);
  } else {
    nextSpawnDelay_ = random(SkyStarsWaitMin, SkyStarsWaitMax + 1);
  }
}

void SkyStarsMode::scheduleNextComet() {
  nextCometAt_ = millis() + random(9000, 22000);
}

void SkyStarsMode::maybeSpawnStar(PPMax72xxPanel &matrix, unsigned long now, bool &changed) {
  if (!spawnTimer_.check(nextSpawnDelay_)) return;

  spawnStar(matrix, now);
  scheduleNextSpawn(false);
  changed = true;
}

void SkyStarsMode::spawnStar(PPMax72xxPanel &matrix, unsigned long now) {
  if (activeStarCount() >= SkyStarsMax) {
    const uint8_t oldest = findOldestStar();
    if (oldest < SkyStarsMax) stars_[oldest] = Star{};
  }

  const uint8_t slot = findFreeSlot();
  if (slot >= SkyStarsMax) return;

  int8_t x = 0;
  int8_t y = 0;
  pickCoordinate(matrix, x, y);

  Star &star = stars_[slot];
  star.active = true;
  star.visible = true;
  star.twinkle = !hasTwinkleStar() && random(0, 4) == 0;
  star.x = x;
  star.y = y;
  star.expiresAt = now + chooseLifetime();
  star.nextBlinkAt = now + (star.twinkle ? random(160, 500) : 0);
  star.serial = nextStarSerial_++;
}

void SkyStarsMode::expireStars(unsigned long now, bool &changed) {
  for (uint8_t i = 0; i < SkyStarsMax; i++) {
    if (!stars_[i].active) continue;
    if (!timeReached(now, stars_[i].expiresAt)) continue;

    stars_[i] = Star{};
    changed = true;
  }
}

void SkyStarsMode::updateTwinkle(unsigned long now, bool &changed) {
  for (uint8_t i = 0; i < SkyStarsMax; i++) {
    Star &star = stars_[i];
    if (!star.active || !star.twinkle) continue;
    if (!timeReached(now, star.nextBlinkAt)) continue;

    star.visible = !star.visible;
    star.nextBlinkAt = now + (star.visible ? random(180, 520) : random(60, 220));
    changed = true;
  }
}

void SkyStarsMode::maybeStartComet(PPMax72xxPanel &matrix, unsigned long now, bool &changed) {
  if (comet_.active || !timeReached(now, nextCometAt_)) return;

  startComet(matrix, now);
  changed = true;
}

void SkyStarsMode::startComet(PPMax72xxPanel &matrix, unsigned long now) {
  comet_.active = true;
  comet_.dx = random(0, 2) == 0 ? 1 : -1;
  comet_.dy = random(-1, 2);
  comet_.length = random(3, 6);
  comet_.headX = comet_.dx > 0 ? -1 : matrix.width();
  comet_.headY = random(0, matrix.height());
  cometStepDelay_ = random(45, 95);
  cometStepTimer_.start(-cometStepDelay_);

  if (comet_.dy != 0 && random(0, 3) == 0) {
    comet_.headY = constrain(comet_.headY + random(-2, 3), 0, matrix.height() - 1);
  }

  (void)now;
}

void SkyStarsMode::updateComet(PPMax72xxPanel &matrix, bool &changed) {
  if (!comet_.active || !cometStepTimer_.check(cometStepDelay_)) return;

  comet_.headX += comet_.dx;
  comet_.headY += comet_.dy;
  changed = true;

  bool visible = false;
  for (uint8_t segment = 0; segment < comet_.length; segment++) {
    const int16_t x = comet_.headX - segment * comet_.dx;
    const int16_t y = comet_.headY - segment * comet_.dy;
    if (x >= 0 && x < matrix.width() && y >= 0 && y < matrix.height()) {
      visible = true;
      break;
    }
  }

  if (!visible) {
    comet_.active = false;
    scheduleNextComet();
  }
}

void SkyStarsMode::redraw(PPMax72xxPanel &matrix) {
  matrix.setClip(0, matrix.width(), 0, matrix.height());
  matrix.fillScreen(LOW);

  for (uint8_t i = 0; i < SkyStarsMax; i++) {
    const Star &star = stars_[i];
    if (!star.active || !star.visible) continue;
    matrix.drawPixel(star.x, star.y, HIGH);
  }

  if (!comet_.active) return;

  for (uint8_t segment = 0; segment < comet_.length; segment++) {
    const int16_t x = comet_.headX - segment * comet_.dx;
    const int16_t y = comet_.headY - segment * comet_.dy;
    if (x < 0 || x >= matrix.width() || y < 0 || y >= matrix.height()) continue;
    matrix.drawPixel(x, y, HIGH);
  }
}

bool SkyStarsMode::isOccupied(PPMax72xxPanel &matrix, int8_t x, int8_t y) const {
  for (uint8_t i = 0; i < SkyStarsMax; i++) {
    const Star &star = stars_[i];
    if (star.active && star.x == x && star.y == y) return true;
  }

  if (!comet_.active) return false;

  for (uint8_t segment = 0; segment < comet_.length; segment++) {
    const int16_t cometX = comet_.headX - segment * comet_.dx;
    const int16_t cometY = comet_.headY - segment * comet_.dy;
    if (cometX < 0 || cometX >= matrix.width() || cometY < 0 || cometY >= matrix.height()) continue;
    if (cometX == x && cometY == y) return true;
  }

  return false;
}

uint8_t SkyStarsMode::activeStarCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < SkyStarsMax; i++) {
    if (stars_[i].active) count++;
  }
  return count;
}

uint8_t SkyStarsMode::findFreeSlot() const {
  for (uint8_t i = 0; i < SkyStarsMax; i++) {
    if (!stars_[i].active) return i;
  }
  return SkyStarsMax;
}

uint8_t SkyStarsMode::findOldestStar() const {
  uint8_t oldest = SkyStarsMax;
  uint32_t oldestSerial = 0;

  for (uint8_t i = 0; i < SkyStarsMax; i++) {
    if (!stars_[i].active) continue;
    if (oldest == SkyStarsMax || stars_[i].serial < oldestSerial) {
      oldest = i;
      oldestSerial = stars_[i].serial;
    }
  }

  return oldest;
}

bool SkyStarsMode::hasTwinkleStar() const {
  for (uint8_t i = 0; i < SkyStarsMax; i++) {
    if (stars_[i].active && stars_[i].twinkle) return true;
  }
  return false;
}

unsigned long SkyStarsMode::chooseLifetime() const {
  const uint8_t roll = random(0, 100);
  if (roll < 30) return random(450, 1200);
  if (roll < 75) return random(1200, 3200);
  return random(3200, 6200);
}

void SkyStarsMode::pickCoordinate(PPMax72xxPanel &matrix, int8_t &x, int8_t &y) const {
  for (uint8_t attempt = 0; attempt < 32; attempt++) {
    const int8_t candidateX = random(0, matrix.width());
    const int8_t candidateY = random(0, matrix.height());
    if (!isOccupied(matrix, candidateX, candidateY)) {
      x = candidateX;
      y = candidateY;
      return;
    }
  }

  for (int8_t candidateY = 0; candidateY < matrix.height(); candidateY++) {
    for (int8_t candidateX = 0; candidateX < matrix.width(); candidateX++) {
      if (!isOccupied(matrix, candidateX, candidateY)) {
        x = candidateX;
        y = candidateY;
        return;
      }
    }
  }

  x = 0;
  y = 0;
}
