#include <M5Unified.h>

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(40, 100);
  M5.Display.println("otageLabs core-s3");

  Serial.println("CoreS3 boot OK");
}

void loop() {
  M5.update();
  delay(100);
}
