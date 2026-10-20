#pragma once
#include <stdint.h>

// 現在の配線を維持。センサーの型番が決まったら電圧・抵抗を確認する。
namespace Config {
constexpr uint8_t SENSOR_COUNT = 2;
constexpr uint8_t PIXEL_PIN = 3;
constexpr uint8_t PIXEL_COUNT = 5;
constexpr uint8_t PLAYER_RX_PIN = 10; // Arduino RX ← DFPlayer TX
constexpr uint8_t PLAYER_TX_PIN = 11; // Arduino TX → 1kΩ → DFPlayer RX
constexpr uint8_t LED_BRIGHTNESS = 70;
constexpr uint8_t AUDIO_VOLUME = 12; // 仮設定。実際の音量は音源・スピーカーで確認。
constexpr uint32_t SERIAL_BAUD = 9600;
constexpr uint16_t SAMPLE_MS = 20;
constexpr uint16_t LED_MS = 40;
constexpr uint16_t LOG_MS = 200; // 状態通知を含めても9600 baudに余裕を残す
constexpr uint16_t CAPTURE_SAMPLES = 50; // 約1秒、押し加減を保って採取
constexpr uint16_t MAX_REST_NOISE = 6; // 無負荷時のADC最大値−最小値
constexpr int16_t MIN_SPAN = 4;
constexpr bool AUTO_REST_ON_BOOT = true; // 電源投入時はぬいぐるみを押さない

// 0〜1000は「無負荷〜大人が設定した基準の変形」。力[N]ではない。
constexpr uint16_t TOUCH_LEVEL = 150;
constexpr uint16_t EASE_LEVEL = 650;
constexpr uint16_t STRONG_LEVEL = 850;
// A1は手を軽く近づけても約950に達した実測に合わせ、中間域を抑える。
// 0→0、950→600、1000→1000。A0の応答とEEPROMの校正形式は維持する。
constexpr uint16_t A1_SOFT_INPUT = 950;
constexpr uint16_t A1_SOFT_OUTPUT = 600;
constexpr uint16_t HYSTERESIS = 40;
constexpr uint16_t STATE_HOLD_MS = 120;
constexpr uint16_t PRAISE_HOLD_MS = 800;
constexpr uint16_t SOUND_GAP_MS = 3000;

// 当面はSDカード内の既存音を、押すたびに1回鳴らす。
// play()のSD通し番号を使うため、/mp3/0001.mp3という配置は要求しない。
constexpr bool PLAY_EXISTING_ON_PRESS = true;
constexpr uint16_t EXISTING_TRACK = 1;

// 状態ごとの音声に切り替える時は上をfalseにし、以下を設定する。
// /mp3/0001.mp3「やさしくできたね」等。0はその反応を無効にする。
constexpr uint16_t PRAISE_TRACK = 0;
constexpr uint16_t EASE_TRACK = 0;
constexpr uint16_t STRONG_TRACK = 0;

static_assert(TOUCH_LEVEL > HYSTERESIS, "touch threshold too small");
static_assert(TOUCH_LEVEL < EASE_LEVEL && EASE_LEVEL < STRONG_LEVEL &&
              STRONG_LEVEL <= 1000, "invalid thresholds");
static_assert(A1_SOFT_INPUT > 0 && A1_SOFT_INPUT < 1000 &&
              A1_SOFT_OUTPUT > 0 && A1_SOFT_OUTPUT < A1_SOFT_INPUT,
              "invalid A1 soft response");
static_assert(PIXEL_COUNT > 0 && SENSOR_COUNT == 2, "check hardware layout");
}
