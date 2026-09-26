#include "display.h"

#ifndef XEBRO_DISPLAY_WS19
namespace display {
void start() {}
}  // namespace display
#else

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app.h"
#include "config.h"
#include "fonts.h"
#include "speedtest.h"
#include "store.h"

static const char* TAG = "lcd";

// ---- Waveshare ESP32-S3-Touch-LCD-1.9 wiring (from the vendor demo) -----------
#define LCD_HOST      SPI3_HOST
#define PIN_LCD_SCLK  10
#define PIN_LCD_MOSI  13
#define PIN_LCD_CS    12
#define PIN_LCD_DC    11
#define PIN_LCD_RST   9
#define PIN_LCD_BL    14
#define LCD_PCLK_HZ   (40 * 1000 * 1000)
#define BL_PWM_HZ     20000
#define BL_PWM_BITS   LEDC_TIMER_10_BIT
#define BL_PERCENT    90

// Panel is 170x320 portrait on a 240x320 ST7789; used rotated to landscape.
#define LCD_W   320
#define LCD_H   170
#define LCD_GAP_X 0
#define LCD_GAP_Y 35

#define BAND_LINES 20  // lines per DMA transfer from the bounce buffers
#define REFRESH_MS 1000

// ---- Colours (RGB565, byte-swapped for the SPI panel) --------------------------
static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  return (c >> 8) | (c << 8);
}
static const uint16_t C_BG = rgb(0, 0, 0);
static const uint16_t C_FG = rgb(235, 235, 235);
static const uint16_t C_DIM = rgb(140, 150, 165);
static const uint16_t C_LINE = rgb(60, 70, 85);
static const uint16_t C_OK = rgb(60, 200, 110);
static const uint16_t C_WARN = rgb(240, 180, 60);
static const uint16_t C_BAD = rgb(240, 80, 80);
static const uint16_t C_ACC = rgb(90, 170, 255);

// ---- Framebuffer + primitives --------------------------------------------------
static uint16_t* s_fb = nullptr;  // LCD_W * LCD_H, in PSRAM
static esp_lcd_panel_handle_t s_panel = nullptr;
static uint16_t* s_bounce[2] = {nullptr, nullptr};
static SemaphoreHandle_t s_txDone = nullptr;
static int s_outstanding = 0;

static void fillRect(int x, int y, int w, int h, uint16_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > LCD_W) w = LCD_W - x;
  if (y + h > LCD_H) h = LCD_H - y;
  if (w <= 0 || h <= 0) return;
  for (int j = 0; j < h; j++) {
    uint16_t* row = s_fb + (y + j) * LCD_W + x;
    for (int i = 0; i < w; i++) row[i] = c;
  }
}

static inline void pixel(int x, int y, uint16_t c) {
  if (x >= 0 && x < LCD_W && y >= 0 && y < LCD_H) s_fb[y * LCD_W + x] = c;
}

static void glyph(int x, int y, const Font& f, uint16_t c, char ch) {
  if (ch < 32 || ch > 126) ch = '?';
  const uint8_t* g = f.data + (ch - 32) * f.height * f.rowBytes;
  for (int j = 0; j < f.height; j++) {
    const uint8_t* row = g + j * f.rowBytes;
    for (int i = 0; i < f.width; i++) {
      if (row[i >> 3] & (0x80 >> (i & 7))) pixel(x + i, y + j, c);
    }
  }
}

// Draws text; returns x after the last glyph. Clips to maxChars (0 = none).
static int text(int x, int y, const Font& f, uint16_t c, const std::string& s, int maxChars = 0) {
  int n = 0;
  for (char ch : s) {
    if (maxChars && n >= maxChars) break;
    glyph(x, y, f, c, ch);
    x += f.width;
    n++;
  }
  return x;
}

static int textW(const Font& f, const std::string& s) { return (int)s.size() * f.width; }

static void textCentre(int cx, int y, const Font& f, uint16_t c, const std::string& s, int maxChars = 0) {
  std::string t = (maxChars && (int)s.size() > maxChars) ? s.substr(0, maxChars) : s;
  text(cx - textW(f, t) / 2, y, f, c, t);
}

// Largest of the four faces that fits s in w px (then clipping).
static const Font& fitFont(const std::string& s, int w) {
  if (textW(FONT_XL, s) <= w) return FONT_XL;
  if (textW(FONT_LARGE, s) <= w) return FONT_LARGE;
  if (textW(FONT_MED, s) <= w) return FONT_MED;
  return FONT_SMALL;
}

// Filled triangle on a flat base of width w centred at cx; apex h away.
// dir -1 = apex above the base, +1 = apex below.
static void triangle(int cx, int baseY, int w, int h, int dir, uint16_t c) {
  for (int j = 0; j < h; j++) {
    int half = (w / 2) - (w / 2) * j / h;
    fillRect(cx - half, baseY + dir * j, half * 2 + 1, 1, c);
  }
}

// Down / up arrow: a stem with a solid head, total w x h.
static void arrow(int x, int y, int w, int h, bool down, uint16_t c) {
  int cx = x + w / 2;
  int head = h / 2;
  int stem = w / 3;
  if (down) {
    fillRect(cx - stem / 2, y, stem, h - head, c);
    triangle(cx, y + h - head, w, head, +1, c);
  } else {
    fillRect(cx - stem / 2, y + head, stem, h - head, c);
    triangle(cx, y + head, w, head, -1, c);
  }
}

// "No internet": a ring with a diagonal bar through it.
static void noEntry(int x, int y, int d, uint16_t c) {
  int r = d / 2, cx = x + r, cy = y + r;
  int inner = (r - 3) * (r - 3);
  for (int dy = -r; dy <= r; dy++) {
    for (int dx = -r; dx <= r; dx++) {
      int rr = dx * dx + dy * dy;
      if (rr <= r * r && rr >= inner) pixel(cx + dx, cy + dy, c);
    }
  }
  for (int i = -r + 3; i <= r - 3; i++) {
    for (int t = -1; t <= 1; t++) pixel(cx + i, cy + i + t, c);
  }
}

static bool onTxDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(s_txDone, &woken);
  return woken == pdTRUE;
}

// Push the framebuffer to the panel in bands through internal DMA buffers.
static void flush() {
  int buf = 0;
  for (int y = 0; y < LCD_H; y += BAND_LINES) {
    int lines = (y + BAND_LINES <= LCD_H) ? BAND_LINES : LCD_H - y;
    // Wait until this bounce buffer's previous transfer has finished.
    while (s_outstanding >= 2) {
      xSemaphoreTake(s_txDone, portMAX_DELAY);
      s_outstanding--;
    }
    memcpy(s_bounce[buf], s_fb + y * LCD_W, lines * LCD_W * sizeof(uint16_t));
    esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_W, y + lines, s_bounce[buf]);
    s_outstanding++;
    buf ^= 1;
  }
  while (s_outstanding > 0) {
    xSemaphoreTake(s_txDone, portMAX_DELAY);
    s_outstanding--;
  }
}

// ---- Panel bring-up ---------------------------------------------------------------
// Panel tuning from Waveshare's ESP32-S3-LCD-1.9 demo (08_LVGL_Test/lcd_bsp.c).
// Sleep-out / MADCTL / COLMOD / display-on are done by the IDF ST7789 driver;
// these set porch, gate, VCOM, power and gamma for this particular glass.
struct TuneCmd { uint8_t cmd; uint8_t n; uint8_t p[14]; };
static const TuneCmd kTuning[] = {
    {0xB2, 5, {0x0c, 0x0c, 0x00, 0x33, 0x33}},  // porch control
    {0xB7, 1, {0x35}},                          // gate control
    {0xBB, 1, {0x13}},                          // VCOM
    {0xC0, 1, {0x2c}},                          // LCM control
    {0xC2, 1, {0x01}},                          // VDV/VRH enable
    {0xC3, 1, {0x0b}},                          // VRH
    {0xC4, 1, {0x20}},                          // VDV
    {0xC6, 1, {0x0f}},                          // frame rate 60 Hz
    {0xD0, 2, {0xa4, 0xa1}},                    // power control 1
    {0xD6, 1, {0xa1}},
    {0xE0, 14, {0x00, 0x03, 0x07, 0x08, 0x07, 0x15, 0x2A, 0x44, 0x42, 0x0A, 0x17, 0x18, 0x25, 0x27}},  // gamma +
    {0xE1, 14, {0x00, 0x03, 0x08, 0x07, 0x07, 0x23, 0x2A, 0x43, 0x42, 0x09, 0x18, 0x17, 0x25, 0x27}},  // gamma -
};

static void backlight(int percent) {
  uint32_t maxDuty = (1u << BL_PWM_BITS) - 1;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)percent * maxDuty / 100);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static bool initPanel() {
  ledc_timer_config_t tcfg = {};
  tcfg.speed_mode = LEDC_LOW_SPEED_MODE;
  tcfg.duty_resolution = BL_PWM_BITS;
  tcfg.timer_num = LEDC_TIMER_0;
  tcfg.freq_hz = BL_PWM_HZ;
  tcfg.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&tcfg);
  ledc_channel_config_t ccfg = {};
  ccfg.gpio_num = PIN_LCD_BL;
  ccfg.speed_mode = LEDC_LOW_SPEED_MODE;
  ccfg.channel = LEDC_CHANNEL_0;
  ccfg.timer_sel = LEDC_TIMER_0;
  ccfg.duty = 0;
  ledc_channel_config(&ccfg);

  spi_bus_config_t bus = {};
  bus.sclk_io_num = PIN_LCD_SCLK;
  bus.mosi_io_num = PIN_LCD_MOSI;
  bus.miso_io_num = -1;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = LCD_W * BAND_LINES * sizeof(uint16_t);
  if (spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

  esp_lcd_panel_io_handle_t io = nullptr;
  esp_lcd_panel_io_spi_config_t ioCfg = {};
  ioCfg.dc_gpio_num = PIN_LCD_DC;
  ioCfg.cs_gpio_num = PIN_LCD_CS;
  ioCfg.pclk_hz = LCD_PCLK_HZ;
  ioCfg.lcd_cmd_bits = 8;
  ioCfg.lcd_param_bits = 8;
  ioCfg.spi_mode = 0;
  ioCfg.trans_queue_depth = 4;
  ioCfg.on_color_trans_done = onTxDone;
  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &ioCfg, &io) != ESP_OK) return false;

  esp_lcd_panel_dev_config_t panelCfg = {};
  panelCfg.reset_gpio_num = PIN_LCD_RST;
  panelCfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panelCfg.bits_per_pixel = 16;
  if (esp_lcd_new_panel_st7789(io, &panelCfg, &s_panel) != ESP_OK) return false;

  esp_lcd_panel_reset(s_panel);
  esp_lcd_panel_init(s_panel);
  for (const TuneCmd& c : kTuning) esp_lcd_panel_io_tx_param(io, c.cmd, c.p, c.n);
  esp_lcd_panel_invert_color(s_panel, true);  // IPS panel: vendor init sends INVON
  esp_lcd_panel_swap_xy(s_panel, true);       // landscape (vendor MADCTL 0x70)
  esp_lcd_panel_mirror(s_panel, true, false);
  esp_lcd_panel_set_gap(s_panel, LCD_GAP_X, LCD_GAP_Y);
  esp_lcd_panel_disp_on_off(s_panel, true);
  return true;
}

// ---- Status page -------------------------------------------------------------------
// Top half: the uplink SSID on the left, the speed test in a column on the
// right. Bottom half: whether the named host is on the AP.
#define MID_Y    88
#define RIGHT_W  104
#define RIGHT_X  (LCD_W - RIGHT_W)
#define LEFT_X   6
#define LEFT_W   (RIGHT_X - LEFT_X - 6)

// One "arrow + figure" row of the speed column. Both rows start at the same
// x so the arrows line up whatever the figures' widths.
static void speedRow(int y, bool down, float mbps, uint16_t arrowCol) {
  char val[16];
  snprintf(val, sizeof(val), "%.1f", mbps);
  const int aw = 14, ah = 22, x = RIGHT_X + 6;
  arrow(x, y, aw, ah, down, arrowCol);
  text(x + aw + 5, y - 3, FONT_LARGE, C_FG, val, (LCD_W - 4 - (x + aw + 5)) / FONT_LARGE.width);
}

static void render(const StatusSnapshot& s, const speedtest::Result& sp) {
  fillRect(0, 0, LCD_W, LCD_H, C_BG);
  const int cx = LCD_W / 2;
  const int rcx = RIGHT_X + RIGHT_W / 2;

  // ---------------- top half ----------------
  if (!s.visibleOverride.empty()) {
    // Over-ride takes the whole width: there is nothing to report speeds for.
    textCentre(cx, 12, FONT_SMALL, C_BAD, "Over-ridden by");
    const Font& f = fitFont(s.visibleOverride, LCD_W - 12);
    textCentre(cx, 34, f, C_FG, s.visibleOverride, (LCD_W - 12) / f.width);
  } else {
    text(LEFT_X, 8, FONT_SMALL, C_DIM, "Connected to:");
    std::string ssid = s.staConnected ? s.staSsid : "Not connected";
    const Font& f = fitFont(ssid, LEFT_W);
    text(LEFT_X, 32, f, s.staConnected ? C_FG : C_DIM, ssid, LEFT_W / f.width);

    bool online = s.staConnected && s.nat;
    if (online && sp.valid) {
      speedRow(10, true, sp.downMbps, C_OK);
      speedRow(48, false, sp.upMbps, C_ACC);
    } else if (online && sp.running) {
      textCentre(rcx, 30, FONT_SMALL, C_WARN, "Speed");
      textCentre(rcx, 50, FONT_SMALL, C_WARN, "test...");
    } else {
      // Two lines so the words stay clear of the screen edge.
      const int d = 28;
      noEntry(rcx - d / 2, 6, d, C_BAD);
      textCentre(rcx, 40, FONT_SMALL, C_BAD, "No");
      textCentre(rcx, 60, FONT_SMALL, C_BAD, "Internet");
    }
  }

  // ---------------- bottom half ----------------
  fillRect(0, MID_Y, LCD_W, 1, C_LINE);

  appLock();
  std::string hostMac = store.hostMac(), hostName = store.hostName();
  appUnlock();

  if (hostMac.empty()) {
    textCentre(cx, 120, FONT_SMALL, C_DIM, "No named host set");
    return;
  }
  bool here = false;
  std::string hostIp;
  for (const auto& c : s.clients) {
    if (c.mac == hostMac) {
      here = true;
      hostIp = c.ip;
    }
  }
  std::string line = hostName + (here ? " Connected" : " Not Connected");
  const Font& hf = fitFont(line, LCD_W - 12);
  int y = here ? 98 : 110;
  textCentre(cx, y, hf, here ? C_OK : C_DIM, line, (LCD_W - 12) / hf.width);
  if (here && y + hf.height + 2 < LCD_H - FONT_SMALL.height) {
    textCentre(cx, y + hf.height + 2, FONT_SMALL, C_DIM, hostIp.empty() ? hostMac : hostIp);
  }
}

static void displayTask(void*) {
  std::string lastKey;
  int64_t lastFlush = 0;
  for (;;) {
    StatusSnapshot s = app::snapshot();
    speedtest::Result sp = speedtest::last();

    // Only redraw when something visible changed (or every 10 s regardless).
    appLock();
    std::string key = store.hostMac() + "/" + store.hostName();
    appUnlock();
    key += std::string(modeName(s.mode)) + "|" + s.staSsid + "|" + (s.nat ? "1" : "0") + "|" + s.visibleOverride + "|" +
           std::to_string(sp.running) + std::to_string(sp.valid) + std::to_string((int)(sp.downMbps * 10)) + "/" +
           std::to_string((int)(sp.upMbps * 10));
    for (const auto& c : s.clients) key += "|" + c.mac + "=" + c.ip;

    int64_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (key != lastKey || now - lastFlush > 10000) {
      render(s, sp);
      flush();
      lastKey = key;
      lastFlush = now;
    }
    vTaskDelay(pdMS_TO_TICKS(REFRESH_MS));
  }
}

namespace display {
void start() {
  s_fb = (uint16_t*)heap_caps_malloc(LCD_W * LCD_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_bounce[0] = (uint16_t*)heap_caps_malloc(LCD_W * BAND_LINES * sizeof(uint16_t), MALLOC_CAP_DMA);
  s_bounce[1] = (uint16_t*)heap_caps_malloc(LCD_W * BAND_LINES * sizeof(uint16_t), MALLOC_CAP_DMA);
  s_txDone = xSemaphoreCreateCounting(8, 0);
  if (!s_fb || !s_bounce[0] || !s_bounce[1] || !s_txDone) {
    ESP_LOGE(TAG, "buffer allocation failed; display disabled");
    return;
  }
  if (!initPanel()) {
    ESP_LOGE(TAG, "panel init failed; display disabled");
    return;
  }
  // Splash on a solid colour (so a driving fault and a backlight fault look
  // different), then backlight on so the first thing seen is a drawn frame.
  fillRect(0, 0, LCD_W, LCD_H, rgb(10, 40, 90));
  textCentre(LCD_W / 2, 60, FONT_LARGE, C_FG, AP_SSID);
  textCentre(LCD_W / 2, 95, FONT_SMALL, C_FG, "starting...");
  flush();
  backlight(BL_PERCENT);
  ESP_LOGI(TAG, "ST7789 %dx%d up", LCD_W, LCD_H);
  xTaskCreatePinnedToCore(displayTask, "lcd", 6144, nullptr, 3, nullptr, 1);
}
}  // namespace display

#endif  // XEBRO_DISPLAY_WS19
