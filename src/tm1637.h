// Minimal driver for TM1637 4-digit 7-segment modules.
// Lines are driven open-drain style (LOW = output low, HIGH = input, the
// module's pull-ups pull the line up), so the module may run at 3.3 V.
#pragma once
#include <Arduino.h>

namespace seg {
// Segment bits: A=0x01 B=0x02 C=0x04 D=0x08 E=0x10 F=0x20 G=0x40, 0x80 = colon (digit 1)
constexpr uint8_t DIGITS[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
constexpr uint8_t BLANK = 0x00;
constexpr uint8_t MINUS = 0x40;
constexpr uint8_t DEGREE = 0x63;
constexpr uint8_t A = 0x77, C = 0x39, E = 0x79, F = 0x71, H = 0x76, I = 0x06, L = 0x38,
                  N = 0x54, O = 0x5c, P = 0x73, R = 0x50, S = 0x6d, T = 0x78, U = 0x3e;
}  // namespace seg

class TM1637 {
 public:
  TM1637(uint8_t clk, uint8_t dio) : clk_(clk), dio_(dio) {}

  void begin() {
    pinMode(clk_, INPUT);
    pinMode(dio_, INPUT);
    digitalWrite(clk_, LOW);
    digitalWrite(dio_, LOW);
  }

  // brightness 0..7
  void setBrightness(uint8_t b) { brightness_ = b > 7 ? 7 : b; }

  void show(const uint8_t segs[4], bool on = true) {
    start();
    writeByte(0x40);  // data command: write, auto increment
    stop();
    start();
    writeByte(0xC0);  // address 0
    for (int i = 0; i < 4; i++) writeByte(segs[i] & 0x7f);  // never light the colon
    stop();
    start();
    writeByte(on ? (0x88 | brightness_) : 0x80);
    stop();
  }

  void clear() {
    const uint8_t blank[4] = {0, 0, 0, 0};
    show(blank);
  }

 private:
  uint8_t clk_, dio_;
  uint8_t brightness_ = 4;

  static void bitDelay() { delayMicroseconds(60); }
  void low(uint8_t pin) { pinMode(pin, OUTPUT); }   // output register is LOW
  void high(uint8_t pin) { pinMode(pin, INPUT); }   // released, pulled up

  void start() {
    high(clk_);
    high(dio_);
    bitDelay();
    low(dio_);
    bitDelay();
  }

  void stop() {
    low(clk_);
    bitDelay();
    low(dio_);
    bitDelay();
    high(clk_);
    bitDelay();
    high(dio_);
    bitDelay();
  }

  bool writeByte(uint8_t b) {
    for (int i = 0; i < 8; i++) {
      low(clk_);
      bitDelay();
      if (b & 0x01) high(dio_); else low(dio_);
      bitDelay();
      high(clk_);
      bitDelay();
      b >>= 1;
    }
    // ACK
    low(clk_);
    high(dio_);
    bitDelay();
    high(clk_);
    bitDelay();
    bool ack = digitalRead(dio_) == LOW;
    if (ack) low(dio_);
    bitDelay();
    low(clk_);
    bitDelay();
    return ack;
  }
};
