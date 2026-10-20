#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../PressureModel.h"

using Pressure::Cue;
using Pressure::State;

void normalization() {
  // 実際の旧基準付近でも、押すと減る向きで比較できる。
  assert(Pressure::normalize(32, 32, -8) == 0);
  assert(Pressure::normalize(28, 32, -8) == 500);
  assert(Pressure::normalize(24, 32, -8) == 1000);
  assert(Pressure::normalize(0, 32, -8) == 1000);
  assert(Pressure::normalize(40, 32, -8) == 0);
  assert(Pressure::normalize(150, 100, 100) == 500);
  assert(Pressure::normalize(0, 32, 0) == 0);
  assert(Pressure::normalize(0, 32, -3) == 0);
  assert(!Pressure::validSpan(-8, 3)); // ノイズに近い変化は校正を拒否
  assert(Pressure::validSpan(-12, 3));
  assert(!Pressure::validSpan(-32768));
  assert(!Pressure::validSpan(32767));
  for (int rest = 0; rest <= 1023; rest += 31) {
    for (int span = -1023; span <= 1023; span += 13) {
      uint16_t previous = 0;
      for (int amount = 0; amount <= 1023; amount += 17) {
        const int value = rest + (span < 0 ? -amount : amount);
        const uint16_t level = Pressure::normalize(value, rest, span);
        assert(level <= 1000);
        assert(level >= previous);
        previous = level;
      }
    }
  }
}

void captureAndPersistence() {
  Pressure::Capture capture;
  assert(capture.mean() == 0 && capture.noise() == 0);
  for (int i = 0; i < Config::CAPTURE_SAMPLES; ++i) capture.add(i % 2 ? 32 : 34);
  assert(capture.mean() == 33 && capture.noise() == 2);
  int16_t spans[2] = {-8, -120};
  uint8_t bytes[Pressure::RECORD_SIZE];
  Pressure::encode(spans, bytes);
  int16_t decoded[2] = {0, 0};
  assert(Pressure::decode(bytes, decoded));
  assert(decoded[0] == -8 && decoded[1] == -120);
  // 全バイトの単一ビット破損を拒否し、呼び出し元の校正値を壊さない。
  for (int i = 0; i < Pressure::RECORD_SIZE; ++i) {
    for (int bit = 0; bit < 8; ++bit) {
      bytes[i] ^= (1 << bit);
      assert(!Pressure::decode(bytes, decoded));
      assert(decoded[0] == -8 && decoded[1] == -120);
      bytes[i] ^= (1 << bit);
    }
  }
  memset(bytes, 255, sizeof bytes);
  assert(!Pressure::decode(bytes, decoded));
  spans[1] = 0;
  Pressure::encode(spans, bytes);
  assert(!Pressure::decode(bytes, decoded));
  spans[1] = -32768;
  Pressure::encode(spans, bytes);
  assert(!Pressure::decode(bytes, decoded));
}

void boundariesAndDebounce() {
  assert(Pressure::classify(149, State::Idle) == State::Idle);
  assert(Pressure::classify(150, State::Idle) == State::Gentle);
  assert(Pressure::classify(649, State::Gentle) == State::Gentle);
  assert(Pressure::classify(650, State::Gentle) == State::Ease);
  assert(Pressure::classify(849, State::Ease) == State::Ease);
  assert(Pressure::classify(850, State::Ease) == State::Strong);
  assert(Pressure::classify(810, State::Strong) == State::Strong);
  assert(Pressure::classify(809, State::Strong) == State::Ease);
  assert(Pressure::classify(610, State::Ease) == State::Ease);
  assert(Pressure::classify(609, State::Ease) == State::Gentle);
  assert(Pressure::classify(110, State::Gentle) == State::Gentle);
  assert(Pressure::classify(109, State::Gentle) == State::Idle);
  Pressure::Feedback feedback;
  feedback.reset(0);
  assert(feedback.update(950, 10) == Cue::None);
  assert(feedback.update(0, 100) == Cue::None);
  assert(feedback.state == State::Idle); // 短いスパイクを無視
  assert(feedback.update(500, 200) == Cue::None);
  assert(feedback.update(500, 319) == Cue::None);
  assert(feedback.state == State::Idle);
  assert(feedback.update(500, 320) == Cue::None);
  assert(feedback.state == State::Gentle);
}

void cuesAndRelease() {
  Pressure::Feedback feedback;
  feedback.reset(0);
  feedback.update(400, 0);
  feedback.update(400, 120);
  assert(feedback.update(400, 919) == Cue::None);
  assert(feedback.update(400, 920) == Cue::Praise);
  assert(feedback.update(400, 9000) == Cue::None); // 押し続けても褒め音を連打しない
  feedback.update(700, 9100);
  assert(feedback.update(700, 9220) == Cue::Ease);
  feedback.update(900, 9240);
  assert(feedback.update(900, 9360) == Cue::Strong); // 強い押しは音の待ちを飛ばす
  assert(feedback.update(900, 15000) == Cue::None);
  feedback.update(400, 15020);
  feedback.update(400, 15140);
  assert(feedback.update(400, 16000) == Cue::None); // 一度の押しでは褒め音1回
  feedback.update(0, 16100);
  feedback.update(0, 16220);
  assert(feedback.state == State::Idle);
  feedback.update(400, 16300);
  feedback.update(400, 16420);
  assert(feedback.update(400, 17220) == Cue::Praise); // 離すと再び褒める
}

void recoveryAndRollover() {
  Pressure::Feedback feedback;
  feedback.reset(0);
  feedback.update(900, 0);
  assert(feedback.update(900, 120) == Cue::Strong);
  feedback.update(400, 200);
  feedback.update(400, 320);
  assert(feedback.update(400, 1120) == Cue::None);
  assert(feedback.update(400, 3120) == Cue::Praise); // 弱めた時の成功を伝える
  feedback.reset(0);
  feedback.update(400, 0);
  feedback.update(400, 120);
  assert(feedback.update(900, 920) == Cue::None); // 強くなりかけた時に褒めない
  assert(feedback.update(900, 1040) == Cue::Strong);
  const uint32_t start = UINT32_MAX - 80;
  feedback.reset(start);
  feedback.update(400, start);
  feedback.update(400, start + 120);
  assert(feedback.state == State::Gentle);
  assert(feedback.update(400, start + 920) == Cue::Praise);
  feedback.reset(10000);
  assert(feedback.state == State::Idle);
  assert(feedback.update(0, 11000) == Cue::None);
}

void releaseBeforeUse() {
  Pressure::ReleaseGate gate;
  assert(!gate.update(1000, 0));
  assert(!gate.update(1000, 5000)); // 校正した押し方のままでは開始しない
  assert(!gate.update(0, 5020));
  assert(!gate.update(0, 5139));
  assert(gate.update(0, 5140));
  assert(gate.update(1000, 5160));
  gate.reset();
  assert(!gate.armed);
  assert(!gate.update(0, 5200));
  assert(!gate.update(500, 5250)); // 無負荷への短い復帰だけでは開始しない
  assert(!gate.update(0, 5300));
  assert(gate.update(0, 5420));
  gate.reset();
  const uint32_t start = UINT32_MAX - 80;
  assert(!gate.update(0, start));
  assert(gate.update(0, start + 120));
}

void a1Response() {
  assert(Pressure::softenA1(0) == 0);
  assert(Pressure::softenA1(Config::A1_SOFT_INPUT) == Config::A1_SOFT_OUTPUT);
  assert(Pressure::softenA1(1000) == 1000);
  assert(Pressure::softenA1(65535) == 1000);
  uint16_t previous = 0;
  for (uint16_t level = 0; level <= 1000; ++level) {
    uint16_t actual = Pressure::softenA1(level);
    assert(actual >= previous && actual <= level);
    previous = actual;
  }
  // 実測した軽い近づけ方: rest=341、raw=44、span=-311。
  const uint16_t light = Pressure::softenA1(Pressure::normalize(44, 341, -311));
  assert(light >= Config::TOUCH_LEVEL && light < Config::EASE_LEVEL);
  assert(Pressure::classify(light, State::Idle) == State::Gentle);
  assert(Pressure::classify(Pressure::softenA1(1000), State::Idle) == State::Strong);
  // A0の正規化結果は従来と同じ。
  assert(Pressure::normalize(369, 699, -660) == 500);
}

int main() {
  normalization();
  a1Response();
  captureAndPersistence();
  boundariesAndDebounce();
  cuesAndRelease();
  recoveryAndRollover();
  releaseBeforeUse();
  puts("PASS: normalization, noise rejection, calibration CRC, hysteresis, dwell, cues, rollover, release gate, A1 soft response");
}
