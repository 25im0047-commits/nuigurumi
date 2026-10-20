#include <Adafruit_NeoPixel.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <EEPROM.h>
#include "NuigurumiConfig.h"
#include "PressureModel.h"

const uint8_t SENSOR_PINS[Config::SENSOR_COUNT] = {A0, A1};
Adafruit_NeoPixel pixels(Config::PIXEL_COUNT, Config::PIXEL_PIN, NEO_GRB + NEO_KHZ800);
SoftwareSerial dfSerial(Config::PLAYER_RX_PIN, Config::PLAYER_TX_PIN);
DFRobotDFPlayerMini player;
Pressure::Feedback feedback;
Pressure::ReleaseGate releaseGate;

int16_t raw[Config::SENSOR_COUNT] = {0, 0};
int32_t filtered[Config::SENSOR_COUNT] = {0, 0}; // ADC値×256、丸めで小変化を消さない
int16_t rest[Config::SENSOR_COUNT] = {0, 0};
int16_t spans[Config::SENSOR_COUNT] = {0, 0};
uint16_t restNoise[Config::SENSOR_COUNT] = {0, 0};
uint16_t levels[Config::SENSOR_COUNT] = {0, 0};
bool restReady = false;
bool filterReady = false;
bool playerReady = false;
bool muted = false;
bool plotOnly = false;
bool logging = true;
int8_t captureTarget = -2; // -2=停止、-1=無負荷、0/1=各センサーの押し基準
Pressure::Capture captures[Config::SENSOR_COUNT];
uint32_t lastSample = 0;
uint32_t lastLed = 0;
uint32_t lastLog = 0;
uint16_t combinedLevel = 0;
uint32_t lastColors[Config::PIXEL_COUNT] = {0};

bool calibrated() {
  if (!restReady || captureTarget != -2) return false;
  for (uint8_t i = 0; i < Config::SENSOR_COUNT; ++i)
    if (!Pressure::validSpan(spans[i], restNoise[i])) return false;
  return true;
}

void stopSound() {
  if (playerReady) player.stop();
}

void playExistingTrack() {
  if (!playerReady) { Serial.println(F("Audio unavailable: check player and SD.")); return; }
  if (muted) { Serial.println(F("Audio muted. Send m to unmute.")); return; }
  player.play(Config::EXISTING_TRACK);
  Serial.println(F("Audio: existing SD track requested."));
}

void printHelp() {
  Serial.println(F("NUIGURUMI / 9600 baud"));
  Serial.println(F("b: release both sensors, capture rest for 1s"));
  Serial.println(F("1/2: hold sensor A0/A1 at adult reference, capture for 1s"));
  Serial.println(F("s: save spans / x: erase calibration / i: inspect"));
  Serial.println(F("m: mute / p: plot / l: log / v: monitor stream / h: help"));
  Serial.println(F("t: test existing SD track (respects mute)"));
  Serial.println(F("Power on: leave plush released for rest capture. Levels are not N."));
}

void inspectCalibration() {
  Serial.print(F("ready=")); Serial.print(calibrated() && releaseGate.armed);
  Serial.print(F(" calibrated=")); Serial.print(calibrated());
  Serial.print(F(" audio=")); Serial.print(playerReady);
  Serial.print(F(" muted=")); Serial.println(muted);
  for (uint8_t i = 0; i < Config::SENSOR_COUNT; ++i) {
    Serial.print(F("A")); Serial.print(i);
    Serial.print(F(" rest=")); Serial.print(rest[i]);
    Serial.print(F(" noise=")); Serial.print(restNoise[i]);
    Serial.print(F(" span=")); Serial.println(spans[i]);
  }
}

void loadCalibration() {
  uint8_t bytes[Pressure::RECORD_SIZE];
  for (uint8_t i = 0; i < Pressure::RECORD_SIZE; ++i) bytes[i] = EEPROM.read(i);
  Serial.println(Pressure::decode(bytes, spans) ?
                 F("Saved spans loaded. Leave plush released during startup.") :
                 F("No valid saved spans. Calibrate with b, 1, 2."));
}

void saveCalibration() {
  if (!calibrated()) {
    Serial.println(F("Save refused: complete b, 1, 2 first."));
    return;
  }
  uint8_t bytes[Pressure::RECORD_SIZE];
  Pressure::encode(spans, bytes);
  // チェックサムを最後に書く。保存はsの時だけ、同じ値は書き込まない。
  for (uint8_t i = 0; i < Pressure::RECORD_SIZE; ++i) EEPROM.update(i, bytes[i]);
  uint8_t readBack[Pressure::RECORD_SIZE];
  int16_t verified[Config::SENSOR_COUNT];
  for (uint8_t i = 0; i < Pressure::RECORD_SIZE; ++i) readBack[i] = EEPROM.read(i);
  bool matches = Pressure::decode(readBack, verified);
  for (uint8_t i = 0; matches && i < Config::SENSOR_COUNT; ++i)
    matches = verified[i] == spans[i];
  Serial.println(matches ? F("Saved and verified.") : F("Save verification failed."));
}

void startCapture(int8_t target) {
  if (captureTarget != -2) {
    Serial.println(F("Capture busy. Keep holding until DONE/FAILED."));
    return;
  }
  if (target >= 0 && !restReady) {
    Serial.println(F("First release both sensors and send b."));
    return;
  }
  stopSound();
  feedback.reset(millis());
  releaseGate.reset();
  captureTarget = target;
  if (target == -1) restReady = false;
  else spans[target] = 0;
  for (uint8_t i = 0; i < Config::SENSOR_COUNT; ++i) captures[i] = Pressure::Capture();
  Serial.println(F("CAPTURING: keep current position for 1 second."));
}

void finishCapture() {
  if (captureTarget == -1) {
    bool stable = true;
    for (uint8_t i = 0; i < Config::SENSOR_COUNT; ++i)
      if (captures[i].noise() > Config::MAX_REST_NOISE) stable = false;
    if (stable) {
      for (uint8_t i = 0; i < Config::SENSOR_COUNT; ++i) {
        rest[i] = captures[i].mean();
        restNoise[i] = captures[i].noise();
      }
      restReady = true;
      Serial.println(F("DONE: rest captured. Set references with 1, 2 if needed."));
    } else Serial.println(F("FAILED: rest unstable. Release plush, check mounting, retry b."));
  } else {
    const uint8_t i = captureTarget;
    const int16_t span = captures[i].mean() - rest[i];
    const uint16_t allowedNoise = max(static_cast<uint16_t>(4),
                                    static_cast<uint16_t>(Pressure::magnitude(span) / 4));
    if (Pressure::validSpan(span, restNoise[i]) && captures[i].noise() <= allowedNoise) {
      spans[i] = span;
      Serial.println(F("DONE: reference captured (direction learned). Release plush."));
    } else Serial.println(F("FAILED: small/unstable change. Check mounting, retry reference."));
  }
  captureTarget = -2;
  filterReady = false;
  feedback.reset(millis());
  inspectCalibration();
}

void handleCommands() {
  // 一度に処理する文字数を制限し、連続入力でも計測を続ける。
  for (uint8_t count = 0; count < 8 && Serial.available(); ++count) {
    const char command = Serial.read();
    switch (command) {
      case 'b': startCapture(-1); break;
      case '1': startCapture(0); break;
      case '2': startCapture(1); break;
      case 's': saveCalibration(); break;
      case 'i': inspectCalibration(); break;
      case 't':
        if (captureTarget == -2) playExistingTrack();
        else Serial.println(F("Capture busy. Wait before audio test."));
        break;
      case 'm':
        muted = !muted;
        if (muted) stopSound();
        Serial.println(muted ? F("Muted.") : F("Unmuted."));
        break;
      case 'p': plotOnly = !plotOnly; break;
      case 'l': logging = !logging; break;
      case 'v':
        logging = true;
        plotOnly = false;
        Serial.println(F("Monitor stream enabled."));
        break;
      case 'h': printHelp(); break;
      case 'x':
        EEPROM.update(0, 0);
        restReady = false;
        captureTarget = -2;
        for (uint8_t i = 0; i < Config::SENSOR_COUNT; ++i) spans[i] = 0;
        feedback.reset(millis());
        releaseGate.reset();
        stopSound();
        Serial.println(F("Calibration erased. Use b, 1, 2."));
        break;
      case '\r': case '\n': case ' ': break;
      default: Serial.println(F("Unknown command. Send h for help.")); break;
    }
  }
}

void sampleSensors(uint32_t now) {
  combinedLevel = 0;
  for (uint8_t i = 0; i < Config::SENSOR_COUNT; ++i) {
    analogRead(SENSOR_PINS[i]); // ADC入力切替直後の読み取りを捨てる
    raw[i] = analogRead(SENSOR_PINS[i]);
    if (!filterReady) filtered[i] = static_cast<int32_t>(raw[i]) * 256;
    else filtered[i] += (static_cast<int32_t>(raw[i]) * 256 - filtered[i]) / 4;
    levels[i] = restReady ? Pressure::normalize((filtered[i] + 128) / 256, rest[i], spans[i]) : 0;
    if (i == 1) levels[i] = Pressure::softenA1(levels[i]);
    if (levels[i] > combinedLevel) combinedLevel = levels[i];
    if (captureTarget != -2) captures[i].add(raw[i]);
  }
  filterReady = true;
  if (captureTarget != -2 && captures[0].count >= Config::CAPTURE_SAMPLES) finishCapture();
  if (!calibrated()) { combinedLevel = 0; return; }
  if (!releaseGate.update(combinedLevel, now)) return;
  const Pressure::State previousState = feedback.state;
  const Pressure::Cue cue = feedback.update(combinedLevel, now);
  if (Config::PLAY_EXISTING_ON_PRESS) {
    // 安定した押し始めだけで再生。色が変わっても再生し直さない。
    if (previousState == Pressure::State::Idle && feedback.state != Pressure::State::Idle)
      playExistingTrack();
    return;
  }
  uint16_t track = 0;
  switch (cue) {
    case Pressure::Cue::Praise: track = Config::PRAISE_TRACK; break;
    case Pressure::Cue::Ease: track = Config::EASE_TRACK; break;
    case Pressure::Cue::Strong: track = Config::STRONG_TRACK; break;
    default: break;
  }
  if (playerReady && !muted && track != 0) player.playMp3Folder(track);
}

void renderLeds() {
  uint8_t red = 0, green = 0, blue = 0;
  uint8_t count = Config::PIXEL_COUNT;
  if (captureTarget != -2) { red = 45; blue = 90; }
  else if (!calibrated() || !releaseGate.armed) { count = 1; blue = 60; }
  else {
    switch (feedback.state) {
      case Pressure::State::Idle: count = 1; blue = 8; break;
      case Pressure::State::Gentle:
        green = 120;
        count = min(static_cast<uint16_t>(Config::PIXEL_COUNT),
                    static_cast<uint16_t>(1 + combinedLevel * Config::PIXEL_COUNT / Config::EASE_LEVEL));
        break;
      case Pressure::State::Ease: red = 150; green = 75; break;
      case Pressure::State::Strong: red = 150; green = 30; blue = 15; break;
    }
  }
  bool changed = false;
  for (uint8_t i = 0; i < Config::PIXEL_COUNT; ++i) {
    const uint32_t color = i < count ? pixels.Color(red, green, blue) : 0;
    if (lastColors[i] != color) {
      pixels.setPixelColor(i, color);
      lastColors[i] = color;
      changed = true;
    }
  }
  if (changed) pixels.show(); // 不要なshowを減らし、SoftwareSerialの割り込み停止を減らす
}

void printLog() {
  if (!logging) return;
  // Plotterにも読めるラベル付き数値。未校正をready:0で区別する。
  Serial.print(F("raw0:")); Serial.print(raw[0]);
  Serial.print(F("\traw1:")); Serial.print(raw[1]);
  if (!plotOnly) {
    Serial.print(F("\tlevel0:")); Serial.print(levels[0]);
    Serial.print(F("\tlevel1:")); Serial.print(levels[1]);
    Serial.print(F("\tstate:")); Serial.print(static_cast<uint8_t>(feedback.state));
    Serial.print(F("\tready:")); Serial.print(calibrated() && releaseGate.armed);
    Serial.print(F("\tcal:")); Serial.print(calibrated());
    Serial.print(F("\tcapture:")); Serial.print(captureTarget);
    Serial.print(F("\taudio:")); Serial.print(playerReady);
    Serial.print(F("\tmuted:")); Serial.print(muted);
  }
  Serial.println();
}

void servicePlayer() {
  if (!playerReady || !player.available()) return;
  const uint8_t type = player.readType();
  const uint16_t detail = player.read();
  if (type == DFPlayerError || type == DFPlayerCardRemoved || type == TimeOut || type == WrongStack) {
    Serial.print(F("Audio error type=")); Serial.print(type);
    Serial.print(F(" detail=")); Serial.println(detail);
    if (type == DFPlayerCardRemoved) playerReady = false;
  }
}

void setup() {
  Serial.begin(Config::SERIAL_BAUD);
  pixels.begin();
  pixels.setBrightness(Config::LED_BRIGHTNESS);
  pixels.clear(); pixels.show();
  printHelp();
  loadCalibration();
  // ACKで実在確認してから、通常動作時はACK待ちを無効にする。
  // 初期化のみライブラリ内部で最大約2秒待つ。失敗しても光と計測は続く。
  dfSerial.begin(9600);
  player.setTimeOut(500);
  Serial.println(F("Initializing DFPlayer..."));
  playerReady = player.begin(dfSerial, true, true);
  if (playerReady) {
    player.begin(dfSerial, false, false);
    player.volume(Config::AUDIO_VOLUME);
    player.stop();
    Serial.println(Config::PLAY_EXISTING_ON_PRESS ?
                   F("DFPlayer ready. Existing SD track plays once per press.") :
                   F("DFPlayer ready. Tracks 0 are disabled."));
  } else Serial.println(F("DFPlayer unavailable. Sensor and LED operation continues."));
  feedback.reset(millis());
  if (Config::AUTO_REST_ON_BOOT) startCapture(-1);
  renderLeds();
}

void loop() {
  handleCommands();
  servicePlayer();
  const uint32_t now = millis();
  if (now - lastSample >= Config::SAMPLE_MS) { lastSample = now; sampleSensors(now); }
  if (now - lastLed >= Config::LED_MS) { lastLed = now; renderLeds(); }
  if (now - lastLog >= Config::LOG_MS) { lastLog = now; printLog(); }
}
