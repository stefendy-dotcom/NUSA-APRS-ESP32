#pragma once
#include <Arduino.h>
#include <Wire.h>

class NusaOled {
public:
  // controller: 1=SSD1306, 2=SH1106
  bool begin(int sda, int scl, uint8_t address, uint8_t contrast, bool flip, uint8_t controller=2);
  void end();
  bool ready() const { return _ready; }
  uint8_t address() const { return _addr; }
  void clear();
  void display();
  void setContrast(uint8_t v);
  void setFlip(bool v);
  void pixel(int x, int y, bool on=true);
  void hline(int x, int y, int w, bool on=true);
  void vline(int x, int y, int h, bool on=true);
  void rect(int x, int y, int w, int h, bool on=true);
  void fillRect(int x, int y, int w, int h, bool on=true);
  void text(int x, int y, const String &s, uint8_t scale=1, bool invert=false);
  void centered(int y, const String &s, uint8_t scale=1, bool invert=false);
  int textWidth(const String &s, uint8_t scale=1) const;
private:
  uint8_t _buf[1024]{};
  uint8_t _addr = 0x3C;
  bool _ready = false;
  bool _flip = false;
  uint8_t _controller = 2; // default SH1106 for common 1.3-inch modules
  void cmd(uint8_t c);
  bool probe(uint8_t a);
  const uint8_t* glyph(char c) const;
};
