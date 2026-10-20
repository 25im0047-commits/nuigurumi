#pragma once
#include <stdint.h>
#include "NuigurumiConfig.h"

namespace Pressure {
enum class State : uint8_t { Idle, Gentle, Ease, Strong };
enum class Cue : uint8_t { None, Praise, Ease, Strong };

inline int32_t magnitude(int16_t value) {
  const int32_t wide = value;
  return wide < 0 ? -wide : wide;
}
inline bool validSpan(int16_t span, uint16_t noise = 0) {
  return magnitude(span) >= Config::MIN_SPAN && magnitude(span) <= 1023 &&
         magnitude(span) >= static_cast<int32_t>(noise) * 3 + 2;
}

// 正負の校正差で方向を決める。逆方向の動きは0、上限は1000。
inline uint16_t normalize(int16_t value, int16_t rest, int16_t span) {
  if (!validSpan(span)) return 0;
  int32_t delta = static_cast<int32_t>(value) - rest;
  if (span < 0) delta = -delta;
  if (delta <= 0) return 0;
  const int32_t level = delta * 1000 / magnitude(span);
  return level > 1000 ? 1000 : static_cast<uint16_t>(level);
}

// A1だけ中間域を緩やかにする。校正基準の1000は保ち、強い側も判別可能にする。
inline uint16_t softenA1(uint16_t level) {
  if (level > 1000) level = 1000;
  if (level <= Config::A1_SOFT_INPUT)
    return static_cast<uint32_t>(level) * Config::A1_SOFT_OUTPUT / Config::A1_SOFT_INPUT;
  return Config::A1_SOFT_OUTPUT +
         static_cast<uint32_t>(level - Config::A1_SOFT_INPUT) * (1000 - Config::A1_SOFT_OUTPUT) /
         (1000 - Config::A1_SOFT_INPUT);
}

struct Capture {
  uint32_t sum = 0;
  uint16_t minimum = 1023;
  uint16_t maximum = 0;
  uint16_t count = 0;
  void add(uint16_t value) {
    sum += value;
    if (value < minimum) minimum = value;
    if (value > maximum) maximum = value;
    ++count;
  }
  int16_t mean() const { return count ? (sum + count / 2) / count : 0; }
  uint16_t noise() const { return count ? maximum - minimum : 0; }
};

inline uint16_t boundary(State state) {
  switch (state) {
    case State::Gentle: return Config::TOUCH_LEVEL;
    case State::Ease: return Config::EASE_LEVEL;
    case State::Strong: return Config::STRONG_LEVEL;
    default: return 0;
  }
}

inline State classify(uint16_t level, State previous) {
  State wanted = level >= Config::STRONG_LEVEL ? State::Strong :
                 level >= Config::EASE_LEVEL ? State::Ease :
                 level >= Config::TOUCH_LEVEL ? State::Gentle : State::Idle;
  // 下がる時だけ境界を少しずらし、境界付近の色の往復を防ぐ。
  if (static_cast<uint8_t>(wanted) < static_cast<uint8_t>(previous) &&
      level >= boundary(previous) - Config::HYSTERESIS) return previous;
  return wanted;
}

class Feedback {
 public:
  State state = State::Idle;

  void reset(uint32_t now) {
    state = candidate = State::Idle;
    candidateSince = stateSince = lastSound = now;
    praised = eased = warned = sounded = false;
  }

  Cue update(uint16_t level, uint32_t now) {
    const State wanted = classify(level, state);
    if (wanted != candidate) {
      candidate = wanted;
      candidateSince = now;
    }
    if (candidate != state && now - candidateSince >= Config::STATE_HOLD_MS) {
      state = candidate;
      stateSince = now;
      if (state == State::Idle) praised = eased = warned = false;
    }
    // 一度の押し操作で各音は1回。強い押しは前の音からの待ちを飛ばす。
    if (state == State::Strong && !warned) {
      warned = true;
      sounded = true;
      lastSound = now;
      return Cue::Strong;
    }
    // 別の状態へ移りかけている時に、古い状態の音を鳴らさない。
    if (candidate != state || (sounded && now - lastSound < Config::SOUND_GAP_MS))
      return Cue::None;
    Cue cue = Cue::None;
    if (state == State::Ease && !eased) {
      eased = true;
      cue = Cue::Ease;
    } else if (state == State::Gentle && !praised &&
               now - stateSince >= Config::PRAISE_HOLD_MS) {
      praised = true;
      cue = Cue::Praise;
    }
    if (cue != Cue::None) { sounded = true; lastSound = now; }
    return cue;
  }

 private:
  State candidate = State::Idle;
  uint32_t candidateSince = 0;
  uint32_t stateSince = 0;
  uint32_t lastSound = 0;
  bool praised = false;
  bool eased = false;
  bool warned = false;
  bool sounded = false;
};

// 校正直後に基準の押し方が残っていても、子ども向けの反応を始めない。
class ReleaseGate {
 public:
  bool armed = false;
  void reset() { armed = timing = false; }
  bool update(uint16_t level, uint32_t now) {
    if (armed) return true;
    if (level >= Config::TOUCH_LEVEL - Config::HYSTERESIS) {
      timing = false;
      return false;
    }
    if (!timing) { timing = true; since = now; }
    if (now - since >= Config::STATE_HOLD_MS) armed = true;
    return armed;
  }
 private:
  bool timing = false;
  uint32_t since = 0;
};

// EEPROMの固定9バイト形式。構造体のパディングに依存しない。
constexpr uint8_t RECORD_SIZE = 9;
inline uint8_t checksum(const uint8_t* bytes) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < RECORD_SIZE - 1; ++i) {
    crc ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x07) :
                          static_cast<uint8_t>(crc << 1);
  }
  return crc;
}
inline void encode(const int16_t* spans, uint8_t* bytes) {
  bytes[0] = 'N'; bytes[1] = 'U'; bytes[2] = 1; bytes[3] = Config::SENSOR_COUNT;
  for (uint8_t i = 0; i < Config::SENSOR_COUNT; ++i) {
    bytes[4 + i * 2] = static_cast<uint16_t>(spans[i]) & 0xff;
    bytes[5 + i * 2] = static_cast<uint16_t>(spans[i]) >> 8;
  }
  bytes[8] = checksum(bytes);
}
inline bool decode(const uint8_t* bytes, int16_t* spans) {
  if (bytes[0] != 'N' || bytes[1] != 'U' || bytes[2] != 1 ||
      bytes[3] != Config::SENSOR_COUNT || bytes[8] != checksum(bytes)) return false;
  int16_t decoded[Config::SENSOR_COUNT];
  for (uint8_t i = 0; i < Config::SENSOR_COUNT; ++i) {
    const uint16_t bits = bytes[4 + i * 2] | (static_cast<uint16_t>(bytes[5 + i * 2]) << 8);
    decoded[i] = bits <= 32767 ? static_cast<int16_t>(bits) :
                 static_cast<int16_t>(static_cast<int32_t>(bits) - 65536L);
    if (!validSpan(decoded[i])) return false;
  }
  for (uint8_t i = 0; i < Config::SENSOR_COUNT; ++i) spans[i] = decoded[i];
  return true;
}
}
