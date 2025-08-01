#include <Arduino.h>
#include "TFT_eSPI.h"
#include "Tulip Landscape 480x320.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <vector>
#include "Untitled-5.h"
#include "Test.h"
#include "Home Menu 1 .h"
#include "Home Menu 2 .h"
#include "NimbusSanL_Bol20pt7b.h"
//#include "texgyreheros_bold12pt7b.h"
#define BL 4
int SCREEN_W;
int SCREEN_H;

int dividerW;
int dividerH;

int TILE_W;
int TILE_H;

int NUM_COLS;
int NUM_ROWS;
bool renderVertically = true; // default: render downwards like a column


#define NUM_SECTIONS 4



struct TextItem {
  String text; 
  int x;
  int y;
};

struct BoxItem {
  int x, y, w, h,r;
  uint16_t color;
};
struct CircleItem {
  int x, y, r;
  uint16_t color;
};
struct BorderItem {
  int x, y, w, h, r;
  uint16_t color;
  uint8_t thickness;
};

std::vector<BorderItem> borderQueue;
std::vector<TextItem> textQueue;
std::vector<BoxItem> boxQueue;
std::vector<CircleItem> CircleQueue;

const uint16_t *currentBackground = nullptr;

TFT_eSPI tft = TFT_eSPI();

TFT_eSprite sprites[NUM_SECTIONS] = {
  TFT_eSprite(&tft),
  TFT_eSprite(&tft),
  TFT_eSprite(&tft),
  TFT_eSprite(&tft)
};

TaskHandle_t tileTaskHandles[NUM_SECTIONS];
SemaphoreHandle_t renderCompleteSemaphore;
SemaphoreHandle_t tftMutex;

float fps = 0.0;

struct TileTaskData {
  int sectionId;
  int start;
  int end;
};

void setRenderDirection(bool vertical) {
  renderVertically = vertical;
}
TileTaskData taskData[NUM_SECTIONS];

// === Utility ===
uint16_t getTileColor(int tileX, int tileY) {
  uint8_t r = (tileX * 70) % 256;
  uint8_t g = (tileY * 120) % 256;
  uint8_t b = (tileX * tileY * 45) % 256;
  return tft.color565(r, g, b);
}

uint16_t hexTo565(uint32_t hexColor) {
  uint8_t r = (hexColor >> 16) & 0xFF;
  uint8_t g = (hexColor >> 8) & 0xFF;
  uint8_t b = hexColor & 0xFF;

  return tft.color565(r, g, b);
}



void drawBackground(const uint16_t *image) {
  currentBackground = image;
}

void drawText(String text, int x, int y) {
  textQueue.push_back({text, x, y});
}

void drawBox(int x, int y, int w, int h,int r, uint16_t color) {
  boxQueue.push_back({x, y, w, h,r, color});
}
void drawCircle(int x, int y, int r, uint16_t color) {
  CircleQueue.push_back({x, y, r, color});
}
void drawBorder(int x, int y, int w, int h, int r, uint16_t color, uint8_t thickness = 1) {
  borderQueue.push_back({x, y, w, h, r, color, thickness});
}

int menuOffsetX = 0;
int menuOffsetY = 0;
void drawMenuSet(int offsetX, int offsetY) {
  menuOffsetX = offsetX;
  menuOffsetY = offsetY;
}
int selectedOption = 0;
int borderThickness = 0;
uint16_t borderColorGlobal= TFT_YELLOW;
uint16_t fillColorGlobal= TFT_BLACK;

void drawMenuHighlight( uint16_t fillColor,uint16_t borderColor,int select){

  borderColorGlobal = borderColor;
  fillColorGlobal  = fillColor;
  selectedOption = select;
}
void drawMenu(int rows, int cols,
              int tileW, int tileH,
              int cornerR,
              uint16_t fillColor,
              uint16_t borderColor,
              uint8_t borderThickness,
              int spacingX, int spacingY,
              const std::vector<String> &titles) {
  
  int totalItems = rows * cols;

  int menuWidth = cols * tileW + (cols - 1) * spacingX;
  int menuHeight = rows * tileH + (rows - 1) * spacingY;

  int startX = -menuWidth / 2 + tileW / 2 + menuOffsetX;
  int startY = -menuHeight / 2 + tileH / 2 + menuOffsetY;

  for (int i = 0; i < totalItems; i++) {
    int row = i / cols;
    int col = i % cols;

    int centerX = startX + col * (tileW + spacingX);
    int centerY = startY + row * (tileH + spacingY);
    int topLeftX = centerX - (tileW / 2);
    int topLeftY = centerY - (tileH / 2);
    
    drawBox(topLeftX, topLeftY, tileW, tileH, cornerR, fillColor);
    drawBorder(topLeftX, topLeftY, tileW, tileH, cornerR, borderColor, borderThickness);
    if (i == selectedOption){
    drawBox(topLeftX, topLeftY, tileW, tileH, cornerR, fillColorGlobal);
    drawBorder(topLeftX, topLeftY, tileW, tileH, cornerR, borderColorGlobal, borderThickness);
    }

    if (i < titles.size()) {
      drawText(titles[i], centerX, centerY);
    }
  }
}

int globalDatum;
uint16_t globalColorText;
const GFXfont* globalFontStyle;

void setTextStyle(int datum,uint16_t colorText,const GFXfont* fontStyle){
  globalDatum = datum;
  globalColorText = colorText;
  globalFontStyle = fontStyle;
}

void sendGraphics(int id, int baseX, int baseY) {
  // Draw text
  sprites[id].setTextDatum(globalDatum);
  sprites[id].setFreeFont(globalFontStyle);
  sprites[id].setTextColor(globalColorText);

  // Draw boxes
  for (auto &box : boxQueue) {
    sprites[id].fillRoundRect(baseX + box.x, baseY + box.y, box.w, box.h, box.r, box.color);
  }

    for (auto &circle : CircleQueue) {
    sprites[id].fillCircle(baseX + circle.x, baseY + circle.y, circle.r, circle.color);
  }
  // Draw borders
// Draw borders with fixed rounded corner gaps
for (auto &border : borderQueue) {
  for (int i = 0; i < border.thickness; ++i) {
    int shrink = i;
    int radius = max(border.r - i, 0);  // Ensure radius doesn't go negative

    sprites[id].drawRoundRect(
      baseX + border.x + shrink,
      baseY + border.y + shrink,
      border.w - 2 * shrink,
      border.h - 2 * shrink,
      radius,
      border.color
    );
  }
}

  for (auto &item : textQueue) {
    sprites[id].drawString(item.text, baseX + item.x, baseY + item.y);
  }

}

// === Parallel Tile Rendering Task (Vertical) ===
void tileRenderTask(void *parameter) {
  TileTaskData *data = (TileTaskData *)parameter;
  int sectionId = data->sectionId;

  sprites[sectionId].setColorDepth(16);
  sprites[sectionId].createSprite(TILE_W, TILE_H);

  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    if (renderVertically) {
      for (int ty = data->start; ty < data->end; ty++) {
        for (int tx = 0; tx < NUM_COLS; tx++) {
          int screenX = tx * TILE_W;
          int screenY = ty * TILE_H;

          uint16_t *buf = (uint16_t *)sprites[sectionId].getPointer();

          if (currentBackground) {
            const uint16_t *src = &currentBackground[(ty * TILE_H) * SCREEN_W + (tx * TILE_W)];
            for (int y = 0; y < TILE_H; y++) {
              for (int x = 0; x < TILE_W; x++) {
                uint16_t pixel = src[y * SCREEN_W + x];
                buf[y * TILE_W + x] = (pixel >> 8) | (pixel << 8);
              }
            }
          } else {
            for (int y = 0; y < TILE_H; y++) {
              for (int x = 0; x < TILE_W; x++) {
                buf[y * TILE_W + x] = getTileColor(screenX + x, screenY + y);
              }
            }
          }

          int centerX = (SCREEN_W / 2) - screenX;
          int centerY = (SCREEN_H / 2) - screenY;
          if (abs(centerX) < SCREEN_W && abs(centerY) < SCREEN_H) {
            sendGraphics(sectionId, centerX, centerY);
          }

          if (xSemaphoreTake(tftMutex, portMAX_DELAY) == pdTRUE) {
            sprites[sectionId].pushSprite(screenX, screenY);
            xSemaphoreGive(tftMutex);
          }
        }
      }
    } else {
      for (int tx = data->start; tx < data->end; tx++) {
        for (int ty = 0; ty < NUM_ROWS; ty++) {
          int screenX = tx * TILE_W;
          int screenY = ty * TILE_H;

          uint16_t *buf = (uint16_t *)sprites[sectionId].getPointer();

          if (currentBackground) {
            const uint16_t *src = &currentBackground[(ty * TILE_H) * SCREEN_W + (tx * TILE_W)];
            for (int y = 0; y < TILE_H; y++) {
              for (int x = 0; x < TILE_W; x++) {
                uint16_t pixel = src[y * SCREEN_W + x];
                buf[y * TILE_W + x] = (pixel >> 8) | (pixel << 8);
              }
            }
          } else {
            for (int y = 0; y < TILE_H; y++) {
              for (int x = 0; x < TILE_W; x++) {
                buf[y * TILE_W + x] = getTileColor(screenX + x, screenY + y);
              }
            }
          }

          int centerX = (SCREEN_W / 2) - screenX;
          int centerY = (SCREEN_H / 2) - screenY;
          if (abs(centerX) < SCREEN_W && abs(centerY) < SCREEN_H) {
            sendGraphics(sectionId, centerX, centerY);
          }

          if (xSemaphoreTake(tftMutex, portMAX_DELAY) == pdTRUE) {
            sprites[sectionId].pushSprite(screenX, screenY);
            xSemaphoreGive(tftMutex);
          }
        }
      }
    }

    xSemaphoreGive(renderCompleteSemaphore);
  }
}


void drawRender() {
  for (int i = 0; i < NUM_SECTIONS; i++) {
    xTaskNotifyGive(tileTaskHandles[i]);
  }

  for (int i = 0; i < NUM_SECTIONS; i++) {
    xSemaphoreTake(renderCompleteSemaphore, portMAX_DELAY);
  }

  
  textQueue.clear();
  boxQueue.clear();
  CircleQueue.clear();
  borderQueue.clear();


}

void drawSetup(int screenW , int screenH, int divW , int divH ) {
  SCREEN_W = screenW;
  SCREEN_H = screenH;
  dividerW = divW;
  dividerH = divH;

  TILE_W = SCREEN_W / dividerW;
  TILE_H = SCREEN_H / dividerH;

  NUM_COLS = SCREEN_W / TILE_W;
  NUM_ROWS = SCREEN_H / TILE_H;

  renderCompleteSemaphore = xSemaphoreCreateCounting(NUM_SECTIONS, 0);
  tftMutex = xSemaphoreCreateMutex();

  int totalTiles = renderVertically ? NUM_ROWS : NUM_COLS;
  int perSection = totalTiles / NUM_SECTIONS;
  int extra = totalTiles % NUM_SECTIONS;

  for (int i = 0; i < NUM_SECTIONS; i++) {
    taskData[i].sectionId = i;
    taskData[i].start = i * perSection;
    taskData[i].end = (i + 1) * perSection;
    if (i == NUM_SECTIONS - 1) {
      taskData[i].end += extra;
    }

    String taskName = "TileTask" + String(i);
    xTaskCreatePinnedToCore(
      tileRenderTask,
      taskName.c_str(),
      4096,
      &taskData[i],
      1,
      &tileTaskHandles[i],
      i % 2
    );

    Serial.printf("Created %s for %s %d - %d\n",
      taskName.c_str(),
      renderVertically ? "rows" : "cols",
      taskData[i].start,
      taskData[i].end - 1
    );
  }
}



void setup() {
  Serial.begin(115200);

  pinMode(BL, OUTPUT);
  digitalWrite(BL, HIGH);

  tft.init();
  tft.setRotation(0);
  tft.setSwapBytes(true);
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Starting vertical tile rendering...", 10, 10, 2);

   setRenderDirection(false); // true = vertical rendering
  drawSetup(320,480,4,2);

  delay(500);
  Serial.println("Vertical tile rendering system initialized!");
}

//uint16_t myColor = tft.color565(120, 200, 150);

//extern const GFXfont texgyreheros_bold12pt7b;
void loop() {
 // String myHex = "FCB32C"; 
uint16_t myColor = hexTo565(0xFCB32C);// or "#FCB32C"


  unsigned long frameStart = millis();
  setTextStyle(MC_DATUM,TFT_CYAN,&NimbusSanL_Bol20pt7b);
  drawBackground(image_data_HomeMenu1);
  //drawText("WORLD", 0, 40);
    std::vector<String> menuItems = {};
  drawMenuSet(-50,-50);

  drawMenuHighlight(NULL,NULL,-1);

  drawMenu(
    2, 2,            // rows, columns
    80, 80,          // width, height
    12,              // corner radius
    TFT_NAVY,        // fill color
    NULL,         // border color
    0,               // border thickness
    10, 10,          // spacingX, spacingY
    std::vector<String>()        // titles
  );

  //  drawMenuSet(100,100);
  //    drawMenuHighlight(TFT_CYAN,TFT_BLUE,2);
  //   drawMenu(
  //   2, 2,            // rows, columns
  //   30, 30,          // width, height
  //   12,              // corner radius
  //   TFT_RED,        // fill color
  //   myColor,         // border color
  //   5,               // border thickness
  //   10, 10,          // spacingX, spacingY
  //   menuItems        // titles
  // );

  // Draw a red box centered at screen
 // drawBox(-40, -40, 80, 80,10, TFT_RED);
 // drawBox(40, 40, 80, 80,0, TFT_BLUE);
 // drawCircle(0,0,10,TFT_DARKGREEN);
  //drawCircle(100,100,30,myColor);
 //   drawBorder(0,0,250,100,50,myColor,5);
  // Optional: display FPS text
  char fpsText[32];
  snprintf(fpsText, sizeof(fpsText), "FPS: %.4f", fps);
  drawText(fpsText, 0, -120);  // adjust as needed

  drawRender();

  unsigned long frameEnd = millis();
  unsigned long frameTime = frameEnd - frameStart;
  fps = 1000.0 / (float)frameTime;
  Serial.printf("Vertical rendering: %.4f   FPS\n", fps);

  // vTaskDelay(pdMS_TO_TICKS(50));
}
