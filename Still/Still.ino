void setup();
void loop();
#include "SceneAssets.h"
#include "UiFonts.h"
#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <atomic>
#include <driver/i2s.h>
#include <rom/miniz.h>
TFT_eSPI t;
Preferences prefs;
struct Calibration {
  uint32_t magic;
  float a, b, c, d, e, f;
} cal;
const char *files[] = {"/STILL/rain.pcm", "/STILL/fireplace.pcm",
                       "/STILL/wind.pcm", "/STILL/birds.pcm"};
// Match the four bundled recordings by perceived level before the user mix.
constexpr float trackTrim[] = {8.0f, 22.0f, 3.5f, 9.0f};
constexpr float limiterKnee = 72.0f;
constexpr float limiterHeadroom = 40.0f;
const char *names[] = {"Rain", "Fire", "Wind", "Birds"};
const char *scenes[] = {"Rain garden", "Campfire", "Woodland", "My mix"};
int presets[][4] = {
    {85, 0, 20, 35}, {0, 90, 20, 0}, {10, 0, 45, 80}, {50, 15, 30, 40}};
enum MediaState {
  CardOpening,
  CardReady,
  CardMountFailed,
  LibraryIncomplete,
  CardReadFailed,
  OutputFailed
};
std::atomic<int> level[4], master{55};
std::atomic<MediaState> media{CardOpening};
std::atomic<bool> playAfterRetry{false};
std::atomic<uint32_t> audioBlocks{0};
std::atomic<bool> run{false}, retry{true};
int page = 0, scene = 0, timer = 0;
uint32_t until = 0, saveAt = 0;
uint16_t sky = 0xDF7F, ink = 0x11C8, white = 0xffff, blue = 0x14D4,
         mint = 0xAEB6;
File tracks[4];
bool imageUi = false;

uint8_t block[4][512];
uint16_t out[1024];
MediaState openLibrary() {
  for (auto &track : tracks)
    track.close();
  SD.end();
  if (!SD.begin(5, SPI, 4000000)) {
    Serial.println("SD mount failed");
    return CardMountFailed;
  }
  for (int channel = 0; channel < 4; ++channel) {
    tracks[channel] = SD.open(files[channel], FILE_READ);
    if (!tracks[channel] || tracks[channel].isDirectory() ||
        tracks[channel].size() <= 512) {
      Serial.printf("SD audio file missing or invalid: %s\n", files[channel]);
      return LibraryIncomplete;
    }
  }
  Serial.println("SD audio ready: 4 tracks");
  return CardReady;
}

bool readAudioBlock(int channel, uint32_t &position) {
  File &track = tracks[channel];
  for (int filled = 0; filled < 512;) {
    if (!track) {
      track = SD.open(files[channel], FILE_READ);
      if (!track || !track.seek(position)) {
        track.close();
        return false;
      }
    }
    const uint32_t size = track.size();
    if (!size) {
      track.close();
      return false;
    }
    if (position == size) {
      if (!track.seek(0)) {
        track.close();
        return false;
      }
      position = 0;
    }
    const int requested = min<uint32_t>(512 - filled, size - position);
    const int got = track.read(block[channel] + filled, requested);
    if (got <= 0 || got > requested) {
      track.close();
      return false;
    }
    filled += got;
    position += got;
  }
  return true;
}

void audioTask(void *) {
  i2s_config_t cfg = {};
  cfg.mode =
      (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
  cfg.sample_rate = 16000;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  cfg.use_apll = true;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK ||
      i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN) != ESP_OK) {
    media = OutputFailed;
    vTaskDelete(NULL);
    return;
  }
  SPI.begin(18, 19, 23, 5);
  float gain[4] = {};
  uint32_t position[4] = {};
  uint8_t failures[4] = {};
  while (true) {
    if (retry.exchange(false)) {
      media = CardOpening;
      const MediaState status = openLibrary();
      memset(position, 0, sizeof(position));
      memset(failures, 0, sizeof(failures));
      media = status;
      if (playAfterRetry.exchange(false) && status == CardReady)
        run = true;
    }
    const bool active = run && media == CardReady;
    bool complete = active;
    for (int j = 0; j < 4; ++j) {
      memset(block[j], 128, sizeof(block[j]));
      if (!active)
        continue;
      const uint32_t start = position[j];
      if (readAudioBlock(j, position[j])) {
        failures[j] = 0;
      } else {
        position[j] = start;
        tracks[j].close();
        memset(block[j], 128, sizeof(block[j]));
        complete = false;
        const int count = ++failures[j];
        Serial.printf("SD audio read failed: %s offset=%lu attempt=%d\n",
                      files[j], static_cast<unsigned long>(start), count);
        if (count >= 3) {
          run = false;
          media = CardReadFailed;
          break;
        }
      }
    }
    if (complete)
      ++audioBlocks;
    float target[4];
    const float masterGain = master.load() / 100.0f;
    for (int j = 0; j < 4; j++)
      target[j] = active && media == CardReady
                      ? trackTrim[j] * level[j].load() * masterGain / 100.0f
                      : 0;
    for (int i = 0; i < 512; i++) {
      float sum = 0;
      for (int j = 0; j < 4; j++) {
        gain[j] += .005f * (target[j] - gain[j]);
        sum += (int(block[j][i]) - 128) * gain[j];
      }
      // Leave normal levels linear and smoothly cap only the louder peaks.
      const float magnitude = fabsf(sum);
      if (magnitude > limiterKnee) {
        const float excess = magnitude - limiterKnee;
        sum = copysignf(limiterKnee + limiterHeadroom * excess /
                                          (limiterHeadroom + excess),
                        sum);
      }
      int v = 128 + lroundf(sum);
      out[2 * i] = out[2 * i + 1] = constrain(v, 0, 255) << 8;
    }
    size_t written;
    i2s_write(I2S_NUM_0, out, sizeof(out), &written, portMAX_DELAY);
  }
}
uint8_t transfer(uint8_t v) {
  uint8_t r = 0;
  for (int b = 7; b >= 0; b--) {
    digitalWrite(32, (v >> b) & 1);
    digitalWrite(25, HIGH);
    r = (r << 1) | digitalRead(39);
    digitalWrite(25, LOW);
  }
  return r;
}
int sample(int cmd) {
  digitalWrite(33, LOW);
  transfer(cmd);
  int hi = transfer(0);
  int lo = transfer(0);
  digitalWrite(33, HIGH);
  return ((hi << 8) | lo) >> 3;
}
bool raw(float &x, float &y) {
  if (digitalRead(36))
    return false;
  x = sample(0x90);
  y = sample(0xD0);
  return !digitalRead(36);
}
void label(String s, int x, int y, uint16_t bg) {
  t.setTextColor(ink, bg);
  t.drawString(s, x, y, 2);
}
void pill(int x, int y, int w, int h, String s, uint16_t bg) {
  t.fillRoundRect(x, y, w, h, 9, bg);
  t.setTextColor(ink, bg);
  t.drawCentreString(s, x + w / 2, y + (h - 16) / 2, 2);
}
void calibrate() {
  float x[3], y[3];
  int sx[] = {24, 296, 24}, sy[] = {30, 30, 210};
  for (int i = 0; i < 3; i++) {
    t.fillScreen(sky);
    label("Hold the circle", 100, 100, sky);
    t.drawCircle(sx[i], sy[i], 12, blue);
    while (!digitalRead(36))
      delay(10);
    do {
      delay(20);
    } while (!raw(x[i], y[i]));
    delay(100);
  }
  float a = x[1] - x[0], b = y[1] - y[0], c = x[2] - x[0], d = y[2] - y[0],
        det = a * d - b * c;
  if (fabs(det) < 10000)
    return;
  cal = {0x43594431, 272 * d / det,  -272 * c / det,
         0,          -180 * b / det, 180 * a / det,
         0};
  cal.c = 24 - cal.a * x[0] - cal.b * y[0];
  cal.f = 30 - cal.d * x[0] - cal.e * y[0];
  prefs.putBytes("touch", &cal, sizeof cal);
  while (!digitalRead(36))
    delay(10);
}
// Graphics are decoded from internal flash in small strips. Only the audio
// task accesses the SD card.
constexpr int motionFrames = 24;
constexpr int motionIntervalMs = 200;
constexpr int motionWidth = 320;
constexpr int motionHeight = 104;
int lastMotionFrame = -1;
int motionScene = -1;
uint32_t motionStart = 0;
bool motionAvailable = true;
static tinfl_decompressor sceneInflater;
static uint16_t sceneStrip[320 * 8];

bool tileFrame(const char *path, int x, int y, int w, int h, int frame,
               int frameCount) {
  const char *filename = strrchr(path, '/');
  filename = filename ? filename + 1 : path;
  const SceneAsset *asset = nullptr;
  for (const auto &candidate : sceneAssets) {
    if (strcmp(filename, candidate.name) == 0) {
      asset = &candidate;
      break;
    }
  }
  if (!asset || asset->width != w || asset->height != h ||
      asset->frames != frameCount || frame < 0 || frame >= frameCount)
    return false;
  uint32_t strip = asset->first + frame * ((h + 7) / 8);
  t.setSwapBytes(true);
  for (int row = 0; row < h; row += 8, ++strip) {
    const int rows = min(8, h - row);
    const uint32_t begin = sceneStripOffsets[strip];
    size_t input = sceneStripOffsets[strip + 1] - begin;
    size_t output = rows * w * 2;
    tinfl_init(&sceneInflater);
    const auto status =
        tinfl_decompress(&sceneInflater, scenePixels + begin, &input,
                         reinterpret_cast<uint8_t *>(sceneStrip),
                         reinterpret_cast<uint8_t *>(sceneStrip), &output,
                         TINFL_FLAG_PARSE_ZLIB_HEADER |
                             TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    if (status != TINFL_STATUS_DONE || output != unsigned(rows * w * 2))
      return false;
    t.pushImage(x, y + row, w, rows, sceneStrip);
  }
  return true;
}

bool tile(const char *path, int x, int y, int w, int h) {
  return tileFrame(path, x, y, w, h, 0, 1);
}

void animateHero() {
  if (!imageUi || page != 0 || media != CardReady || !run || scene >= 2 ||
      !motionAvailable)
    return;
  if (motionScene != scene) {
    motionScene = scene;
    motionStart = millis();
    lastMotionFrame = 0;
  }
  const int frame =
      ((millis() - motionStart) / motionIntervalMs) % motionFrames;
  if (frame == lastMotionFrame)
    return;
  const char *path =
      scene == 0 ? "/STILL/UI/rain.frames" : "/STILL/UI/fire.frames";
  motionAvailable =
      tileFrame(path, 0, 47, motionWidth, motionHeight, frame, motionFrames);
  lastMotionFrame = frame;
}

const char *const pageAsset[] = {"scene", "mixer", "settings"};
const uint8_t themeLight[][3] = {
    {112, 216, 255}, {255, 186, 117}, {198, 237, 134}, {255, 224, 163}};
const uint8_t themeShade[][3] = {
    {9, 35, 59}, {61, 26, 16}, {26, 48, 39}, {43, 38, 61}};

uint16_t themeColor(const uint8_t colors[][3]) {
  const uint8_t *color = colors[scene];
  return t.color565(color[0], color[1], color[2]);
}

void themeStatus(const String &value, int x, int y, int w) {
  const uint16_t shade = themeColor(themeShade);
  t.fillRoundRect(x, y, w, 14, 5, shade);
  t.loadFont(interRegular);
  t.setTextColor(TFT_WHITE, shade, true);
  t.drawCentreString(value, x + w / 2, y, 1);
  t.unloadFont();
}

void imageSlider(int y, int v, bool restore = true) {
  constexpr int railLeft = 170;
  constexpr int railWidth = 91;
  if (restore) {
    char path[44];
    snprintf(path, sizeof(path), "/STILL/UI/%s%d-rails.raw", pageAsset[page],
             scene);
    if (!tileFrame(path, 164, y + 20, 144, 14, page == 1 ? (y - 33) / 40 : 0,
                   page == 1 ? 4 : 1))
      return;
  }
  const uint16_t highlight = themeColor(themeLight);
  const int offset = v * railWidth / 100;
  t.fillRoundRect(railLeft, y + 24, railWidth, 4, 2, t.color565(138, 149, 153));
  if (offset > 0)
    t.fillRoundRect(railLeft, y + 24, offset, 4, 2, highlight);
  t.fillCircle(railLeft + offset, y + 26, 4, highlight);
  themeStatus(String(v) + "%", 269, y + 20, 38);
}

bool drawImageUi() {
  char path[44];
  snprintf(path, sizeof(path), "/STILL/UI/%s%d.raw", pageAsset[page], scene);
  if (!tile(path, 0, 0, 320, 240))
    return false;
  snprintf(path, sizeof(path), "/STILL/UI/%s%d-%s.raw", pageAsset[page], scene,
           run ? "pause" : "play");
  if (!tile(path, 240, 202, 76, 34))
    return false;
  if (page == 1) {
    for (int i = 0; i < 4; i++)
      imageSlider(33 + i * 40, level[i], false);
  } else if (page == 2) {
    imageSlider(32, master, false);
    themeStatus(timer ? String(timer) + " min" : String("Off"), 245, 88, 62);
  }
  const char *notice = nullptr;
  switch (media.load()) {
  case CardOpening:
    notice = "Checking SD...";
    break;
  case CardMountFailed:
    notice = "Check SD card";
    break;
  case LibraryIncomplete:
    notice = "Audio files missing";
    break;
  case CardReadFailed:
    notice = "SD read error";
    break;
  case OutputFailed:
    notice = "Audio output error";
    break;
  case CardReady:
    break;
  }
  if (notice) {
    themeStatus(notice, 167, 7, 145);
    if (media != OutputFailed && media != CardOpening)
      themeStatus("Retry SD", 241, 212, 70);
  }
  return true;
}

void slider(int y, String name, int v) {
  if (imageUi) {
    imageSlider(y, v);
    return;
  }
  t.fillRoundRect(5, y, 310, 36, 8, white);
  label(name, 12, y + 9, white);
  t.fillRoundRect(75, y + 16, 180, 6, 3, sky);
  if (v)
    t.fillRoundRect(75, y + 16, v * 180 / 100, 6, 3, blue);
  t.fillCircle(75 + v * 180 / 100, y + 19, 6, blue);
  label(String(v) + "%", 267, y + 9, white);
}
void draw() {
  imageUi = false;
  motionScene = -1;
  motionAvailable = true;
  if ((imageUi = drawImageUi()))
    return;
  t.fillScreen(sky);
  label(page == 0   ? "Still  /  Soundscapes"
        : page == 1 ? "Mix your sounds"
                    : "Session settings",
        10, 4, sky);
  if (page == 0) {
    t.fillRoundRect(6, 30, 308, 69, 12, mint);
    t.fillCircle(254, 66, 21, white);
    t.fillCircle(275, 63, 17, white);
    t.fillCircle(239, 72, 13, white);
    t.fillCircle(252, 68, 2, ink);
    t.fillCircle(265, 68, 2, ink);
    label(scenes[scene], 16, 38, mint);
    label(run ? "Playing" : "Paused", 16, 65, mint);
    for (int i = 0; i < 4; i++)
      pill(6 + (i % 2) * 155, 104 + (i / 2) * 44, 150, 40, scenes[i],
           scene == i ? mint : white);
  }
  if (page == 1)
    for (int i = 0; i < 4; i++)
      slider(33 + i * 40, names[i], level[i]);
  if (page == 2) {
    slider(32, "Master", master);
    pill(6, 77, 308, 36,
         timer ? "Sleep timer: " + String(timer) + " min" : "Sleep timer: off",
         white);
    pill(6, 118, 308, 36, "Save levels to My mix", white);
    pill(6, 159, 308, 36, "Calibrate touch", white);
  }
  const char *tabs[] = {"Scenes", "Mixer", "Settings"};
  for (int i = 0; i < 3; i++)
    pill(i * 80, 200, 79, 40, tabs[i], i == page ? mint : white);
  pill(240, 200, 80, 40, run ? "Pause" : "Play", white);
}
void setup() {
  Serial.begin(115200);
  Serial.println("Still: flash graphics");
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);
  t.init();
  t.setRotation(1);
  t.fillScreen(sky);
  label("Opening library...", 70, 100, sky);
  for (int p : {25, 32, 33})
    pinMode(p, OUTPUT);
  digitalWrite(33, HIGH);
  pinMode(39, INPUT);
  pinMode(36, INPUT);
  prefs.begin("cyd-audio", false);
  prefs.getBytes("touch", &cal, sizeof cal);
  while (cal.magic != 0x43594431)
    calibrate();
  prefs.getBytes("stillPreset", presets[3], sizeof presets[3]);
  scene = constrain(prefs.getInt("stillScene", 0), 0, 3);
  for (int i = 0; i < 4; i++)
    level[i] = prefs.getInt(("s" + String(i)).c_str(), presets[scene][i]);
  master = prefs.getInt("stillMaster", 55);
  xTaskCreatePinnedToCore(audioTask, "sounds", 6000, nullptr, 3, nullptr, 0);
  draw();
}
void loop() {
  static int last = -99, drag = -1;
  static bool held = false;
  if (media != last) {
    last = media;
    draw();
  }
  if (until && (int32_t)(millis() - until) >= 0) {
    run = false;
    timer = 0;
    until = 0;
    draw();
  }
  if (saveAt && (int32_t)(millis() - saveAt) >= 0) {
    for (int i = 0; i < 4; i++)
      prefs.putInt(("s" + String(i)).c_str(), level[i]);
    prefs.putInt("stillMaster", master);
    prefs.putInt("stillScene", scene);
    saveAt = 0;
  }
  while (Serial.available()) {
    const char command = Serial.read();
    if (command == 'p') {
      if (media == CardReady)
        run = !run;
      else if (media != OutputFailed && media != CardOpening) {
        playAfterRetry = true;
        retry = true;
      }
      draw();
    } else if (command == '?') {
      Serial.printf("Still status: card=%d run=%d blocks=%lu artwork=%d\n",
                    static_cast<int>(media.load()), run.load(),
                    static_cast<unsigned long>(audioBlocks.load()), imageUi);
    }
  }
  float a, b;
  if (!raw(a, b)) {
    animateHero();
    held = false;
    drag = -1;
    delay(10);
    return;
  }
  int x = lround(cal.a * a + cal.b * b + cal.c),
      y = lround(cal.d * a + cal.e * b + cal.f);
  if (!held) {
    held = true;
    if (y >= 200) {
      if (x >= 240) {
        if (media == CardReady)
          run = !run;
        else if (media != OutputFailed && media != CardOpening) {
          playAfterRetry = true;
          retry = true;
        }
      } else {
        page = constrain(x / 80, 0, 2);
      }
      draw();
      return;
    }
    if (page == 0 && (imageUi ? (y >= 156 && y < 198 && x >= 0 && x < 320)
                              : (y >= 104 && y < 190))) {
      scene = imageUi ? x / 80 : (y - 104) / 44 * 2 + (x >= 160);
      for (int i = 0; i < 4; i++)
        level[i] = presets[scene][i];
      run = media == CardReady;
      saveAt = millis() + 1200;
      draw();
      return;
    }
    if (page == 1 && (!imageUi || x >= 158) && y >= 33 && y < 193)
      drag = (y - 33) / 40;
    if (page == 2 && (!imageUi || x >= 158)) {
      if (y >= 32 && y < 72)
        drag = 4;
      else if (y >= 77 && y < 115) {
        timer = timer == 0 ? 15 : timer == 15 ? 30 : timer == 30 ? 60 : 0;
        until = timer ? millis() + timer * 60000UL : 0;
        draw();
      } else if (y >= 118 && y < 155) {
        for (int i = 0; i < 4; i++)
          presets[3][i] = level[i];
        prefs.putBytes("stillPreset", presets[3], sizeof presets[3]);
        scene = 3;
        page = 0;
        saveAt = millis() + 1200;
        draw();
      } else if (y >= 159 && y < 196) {
        calibrate();
        draw();
      }
    }
  }
  if (drag >= 0) {
    int v = imageUi ? constrain((x - 170) * 100 / 91, 0, 100)
                    : constrain((x - 75) * 100 / 180, 0, 100);
    if (drag == 4) {
      if (master != v) {
        master = v;
        slider(32, "Master", v);
      }
    } else if (level[drag] != v) {
      level[drag] = v;
      slider(33 + drag * 40, names[drag], v);
    }
    saveAt = millis() + 1200;
  }
  delay(15);
}
