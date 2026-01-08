#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <WiFiManager.h> 
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <Preferences.h>
#include <time.h> 

// ------------------- PIN DEFINITIONS -------------------
#define R1_PIN 25
#define G1_PIN 26
#define B1_PIN 27
#define R2_PIN 14
#define G2_PIN 12
#define B2_PIN 13
#define A_PIN 23
#define B_PIN 19
#define C_PIN 5
#define D_PIN 22
#define E_PIN 18
#define LAT_PIN 4
#define OE_PIN 15
#define CLK_PIN 33

#define PANEL_RES_X 64
#define PANEL_RES_Y 32
#define PANEL_CHAIN 1

// ------------------- GLOBALS -------------------
MatrixPanel_I2S_DMA *dma_display = nullptr;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
Preferences preferences;

// States
enum DisplayState { CLOCK_MODE, WEATHER_MODE };
DisplayState currentState = CLOCK_MODE;

// Config Variables
String openWeatherMapApiKey = ""; 
String cityLocation = "New York"; 
String timeFormatStr = "12"; 
String tempUnitStr = "F"; 
bool is12hFormat = true;

// Timer Variables
unsigned long lastSecondMillis = 0;
bool colonVisible = true;
int prevMinute = -1;
int prevHour = -1; 
unsigned long weatherStartTime = 0;
unsigned long weatherDuration = 60000; 

// Animation Randomizers
int currentAnimStyle = 0; // 0=Radial, 1=Gravity, 2=Wind, 3=Chaos
int currentAnimSteps = 20; // Random speed

// Weather Data
String currentTemp = "--";
String currentWeatherDesc = "Loading...";
String currentWeatherMain = ""; 

// Scrolling Variables
int weatherScrollX = 0;
int weatherTextWidth = 0;
unsigned long lastScrollMillis = 0;

// Date Names
String daysOfWeek[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
String months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// Colors
uint16_t COL_CYAN, COL_BLUE, COL_ORANGE, COL_YELLOW, COL_GREEN, COL_PURPLE, COL_RED, COL_WHITE, COL_BLACK, COL_GREY;
uint16_t COL_COLON_BRIGHT; 
uint16_t COL_TEXT; 

// Palette for Random Colon
uint16_t tetrisPalette[7];
uint16_t currentColonColor; 

// ------------------- TETRIS DIGIT MAP (3x5) -------------------
// 1 = Block, 0 = Empty
const byte digits[10][5][3] = {
  { {1,1,1}, {1,0,1}, {1,0,1}, {1,0,1}, {1,1,1} }, // 0
  { {0,1,0}, {1,1,0}, {0,1,0}, {0,1,0}, {1,1,1} }, // 1
  { {1,1,1}, {0,0,1}, {1,1,1}, {1,0,0}, {1,1,1} }, // 2
  { {1,1,1}, {0,0,1}, {1,1,1}, {0,0,1}, {1,1,1} }, // 3
  { {1,0,1}, {1,0,1}, {1,1,1}, {0,0,1}, {0,0,1} }, // 4
  { {1,1,1}, {1,0,0}, {1,1,1}, {0,0,1}, {1,1,1} }, // 5
  { {1,1,1}, {1,0,0}, {1,1,1}, {1,0,1}, {1,1,1} }, // 6
  { {1,1,1}, {0,0,1}, {0,0,1}, {0,0,1}, {0,0,1} }, // 7
  { {1,1,1}, {1,0,1}, {1,1,1}, {1,0,1}, {1,1,1} }, // 8
  { {1,1,1}, {1,0,1}, {1,1,1}, {0,0,1}, {0,0,1} }  // 9
};

// ------------------- INITIALIZATION -------------------

void initMatrix() {
  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);
  mxconfig.gpio.r1 = R1_PIN; mxconfig.gpio.g1 = G1_PIN; mxconfig.gpio.b1 = B1_PIN;
  mxconfig.gpio.r2 = R2_PIN; mxconfig.gpio.g2 = G2_PIN; mxconfig.gpio.b2 = B2_PIN;
  mxconfig.gpio.a = A_PIN;   mxconfig.gpio.b = B_PIN;   mxconfig.gpio.c = C_PIN;
  mxconfig.gpio.d = D_PIN;   mxconfig.gpio.e = E_PIN;
  mxconfig.gpio.lat = LAT_PIN; mxconfig.gpio.oe = OE_PIN; mxconfig.gpio.clk = CLK_PIN;

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(128); 

  // Colors
  COL_CYAN   = dma_display->color565(0, 255, 255);
  COL_BLUE   = dma_display->color565(0, 0, 255);
  COL_ORANGE = dma_display->color565(255, 165, 0);
  COL_YELLOW = dma_display->color565(255, 255, 0);
  COL_GREEN  = dma_display->color565(0, 255, 0);
  COL_PURPLE = dma_display->color565(128, 0, 128);
  COL_RED    = dma_display->color565(255, 0, 0);
  COL_WHITE  = dma_display->color565(255, 255, 255);
  COL_BLACK  = dma_display->color565(0, 0, 0);
  COL_GREY   = dma_display->color565(100, 100, 100);
  
  COL_COLON_BRIGHT = dma_display->color565(255, 255, 255);
  COL_TEXT = dma_display->color565(200, 200, 200); 

  // Setup Palette for Random Colon
  tetrisPalette[0] = COL_CYAN;
  tetrisPalette[1] = COL_BLUE;
  tetrisPalette[2] = COL_ORANGE;
  tetrisPalette[3] = COL_YELLOW;
  tetrisPalette[4] = COL_GREEN;
  tetrisPalette[5] = COL_PURPLE;
  tetrisPalette[6] = COL_RED;

  currentColonColor = COL_WHITE; 
}

// ------------------- DRAWING HELPERS -------------------

void drawTetrisBlock(int x, int y, uint16_t color) {
  dma_display->fillRect(x, y, 3, 3, color);
}

// Standard static draw
void drawDigit(int x, int y, int num, bool isBlank) {
  if (isBlank) return; 

  uint16_t color;
  switch(num) {
    case 0: color = COL_CYAN; break;
    case 1: color = COL_BLUE; break;
    case 2: color = COL_ORANGE; break;
    case 3: color = COL_YELLOW; break;
    case 4: color = COL_GREEN; break;
    case 5: color = COL_PURPLE; break;
    case 6: color = COL_RED; break;
    case 7: color = COL_CYAN; break;
    case 8: color = COL_BLUE; break;
    case 9: color = COL_ORANGE; break;
    default: color = COL_WHITE; break;
  }

  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 3; col++) {
      if (digits[num][row][col] == 1) {
        int drawX = x + (col * 4);
        int drawY = y + (row * 4);
        if (drawY >= -4 && drawY < 32) {
          drawTetrisBlock(drawX, drawY, color);
        }
      }
    }
  }
}

// Draw Digit with Randomized Physics
void drawDigitParticles(int x, int y, int num, float progress, bool isImploding) {
  uint16_t color;
  switch(num) {
    case 0: color = COL_CYAN; break;
    case 1: color = COL_BLUE; break;
    case 2: color = COL_ORANGE; break;
    case 3: color = COL_YELLOW; break;
    case 4: color = COL_GREEN; break;
    case 5: color = COL_PURPLE; break;
    case 6: color = COL_RED; break;
    case 7: color = COL_CYAN; break;
    case 8: color = COL_BLUE; break;
    case 9: color = COL_ORANGE; break;
    default: color = COL_WHITE; break;
  }

  // Iterate through the 3x5 grid of blocks
  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 3; col++) {
      if (digits[num][row][col] == 1) {
        
        float origX = x + (col * 4);
        float origY = y + (row * 4);
        
        float vecX = 0;
        float vecY = 0;

        // CALCULATE VECTORS BASED ON STYLE
        // We use pseudo-random math (row*col) so the particle 
        // follows a straight line every frame instead of jittering.
        
        if (currentAnimStyle == 0) { // RADIAL (Standard)
             vecX = (col - 1.0) * 8.0; 
             vecY = (row - 2.0) * 6.0; 
             vecX += (col * row) % 3 - 1; 
             vecY += (row + col) % 3 - 1;
             vecX *= 2.5; // Power
             vecY *= 2.5;
        } 
        else if (currentAnimStyle == 1) { // GRAVITY (Fall Down / Fly Up)
             vecX = (col - 1.0) * 2.0; // Slight spread X
             vecY = 40.0; // Hard Down
        }
        else if (currentAnimStyle == 2) { // WIND (Blow Right)
             vecX = 50.0; // Hard Right
             vecY = (row - 2.0) * 5.0; // Slight spread Y
        }
        else { // CHAOS (Random directions)
             // Use Sin/Cos to generate deterministic random-looking vectors
             vecX = sin((row + 1) * (col + 1) * num) * 30.0;
             vecY = cos((row + 1) * (col + 1) * num) * 30.0;
        }

        float drawX, drawY;

        if (!isImploding) {
            // EXPLODE: Move AWAY from origin based on progress (0.0 -> 1.0)
            drawX = origX + (vecX * progress);
            drawY = origY + (vecY * progress);
        } else {
            // IMPLODE: Move TOWARDS origin based on progress (0.0 -> 1.0)
            float inverse = (1.0 - progress);
            
            // For implosion, we exaggerate the distance slightly so they come from off screen
            drawX = origX + (vecX * inverse * 1.5); 
            drawY = origY + (vecY * inverse * 1.5);
        }

        drawTetrisBlock((int)drawX, (int)drawY, color);
      }
    }
  }
}

void drawColon(bool visible) {
    if (visible) {
       dma_display->fillRect(30, 6, 3, 3, currentColonColor);
       dma_display->fillRect(30, 16, 3, 3, currentColonColor);
    } else {
       dma_display->fillRect(30, 6, 3, 3, COL_BLACK);
       dma_display->fillRect(30, 16, 3, 3, COL_BLACK);
    }
}

void wipeScreen() {
  uint16_t wipeColor = dma_display->color565(100, 255, 255);
  for (int x = 0; x < PANEL_RES_X; x+=2) {
    dma_display->fillRect(x, 0, 2, PANEL_RES_Y, wipeColor);
    delay(5);
    dma_display->fillRect(x, 0, 2, PANEL_RES_Y, COL_BLACK);
  }
}

// --- GRAPHICS: Weather Icons (12x12 approx) ---
void drawWeatherIcon(String type, int x, int y) {
  type.toLowerCase();
  
  if (type == "clear") {
    dma_display->fillCircle(x+6, y+6, 5, COL_YELLOW);
    dma_display->fillCircle(x+6, y+6, 3, COL_ORANGE);
  } 
  else if (type == "clouds" || type == "mist" || type == "fog" || type == "haze") {
    dma_display->fillCircle(x+4, y+8, 4, COL_GREY);
    dma_display->fillCircle(x+8, y+8, 4, COL_GREY);
    dma_display->fillCircle(x+6, y+5, 4, COL_WHITE);
  }
  else if (type == "rain" || type == "drizzle") {
    dma_display->fillCircle(x+4, y+6, 4, COL_GREY);
    dma_display->fillCircle(x+8, y+6, 4, COL_GREY);
    dma_display->fillCircle(x+6, y+4, 4, COL_GREY);
    dma_display->drawLine(x+4, y+11, x+3, y+14, COL_BLUE);
    dma_display->drawLine(x+8, y+11, x+7, y+14, COL_BLUE);
  }
  else if (type == "thunderstorm") {
    dma_display->fillCircle(x+4, y+6, 4, COL_GREY);
    dma_display->fillCircle(x+8, y+6, 4, COL_GREY);
    dma_display->fillCircle(x+6, y+4, 4, COL_GREY);
    dma_display->drawLine(x+6, y+10, x+4, y+12, COL_YELLOW);
    dma_display->drawLine(x+4, y+12, x+7, y+12, COL_YELLOW);
    dma_display->drawLine(x+7, y+12, x+5, y+15, COL_YELLOW);
  }
  else if (type == "snow") {
    dma_display->drawPixel(x+6, y+6, COL_WHITE);
    dma_display->drawLine(x+3, y+3, x+9, y+9, COL_WHITE);
    dma_display->drawLine(x+9, y+3, x+3, y+9, COL_WHITE);
    dma_display->drawPixel(x+6, y+2, COL_WHITE);
    dma_display->drawPixel(x+6, y+10, COL_WHITE);
    dma_display->drawPixel(x+2, y+6, COL_WHITE);
    dma_display->drawPixel(x+10, y+6, COL_WHITE);
  }
  else {
     dma_display->drawCircle(x+6, y+6, 5, COL_WHITE);
  }
}

// ------------------- LOGIC -------------------

void fetchWeather() {
  if (WiFi.status() == WL_CONNECTED && openWeatherMapApiKey.length() > 0) {
    HTTPClient http;
    String apiUnit = (tempUnitStr == "C") ? "metric" : "imperial";
    String url = "http://api.openweathermap.org/data/2.5/weather?q=" + cityLocation + "&appid=" + openWeatherMapApiKey + "&units=" + apiUnit;
    
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode > 0) {
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload);
      
      long tzOffset = doc["timezone"];
      timeClient.setTimeOffset(tzOffset);
      
      float temp = doc["main"]["temp"];
      const char* desc = doc["weather"][0]["description"];
      const char* main = doc["weather"][0]["main"]; 
      
      currentTemp = String((int)temp) + tempUnitStr;
      currentWeatherDesc = String(desc);
      currentWeatherMain = String(main);
      
      currentWeatherDesc.setCharAt(0, toupper(currentWeatherDesc.charAt(0)));
    }
    http.end();
  } else {
    currentTemp = "Err";
  }
}

void enterWeatherMode() {
    wipeScreen();
    fetchWeather();
    
    dma_display->fillScreen(COL_BLACK);
    dma_display->setTextSize(1);
    
    drawWeatherIcon(currentWeatherMain, 2, 9);
    
    dma_display->setTextColor(COL_YELLOW); 
    dma_display->setCursor(20, 2);
    dma_display->print(cityLocation);
    
    dma_display->setTextColor(COL_WHITE);
    dma_display->setCursor(20, 12);
    dma_display->print(currentTemp);
    
    dma_display->setTextColor(COL_TEXT);

    weatherTextWidth = currentWeatherDesc.length() * 6; 
    
    if (weatherTextWidth <= 64) {
      int startX = (64 - weatherTextWidth) / 2;
      dma_display->setCursor(startX, 23);
      dma_display->print(currentWeatherDesc);
    } else {
      weatherScrollX = 64; 
    }
    
    currentState = WEATHER_MODE;
    weatherStartTime = millis();
}

// ------------------- WIFI CONFIG -------------------

void configModeCallback(WiFiManager *myWiFiManager) {
  dma_display->fillScreen(COL_BLACK);
  dma_display->setTextSize(1);
  dma_display->setTextColor(COL_COLON_BRIGHT);
  dma_display->setCursor(4, 8);
  dma_display->print("CONFIGURE");
  dma_display->setCursor(20, 18);
  dma_display->print("ME");
  Serial.println("AP Mode Active");
}

void saveConfigCallback() {
  Serial.println("Config Saved");
}

void drawDateText() {
    // Helper to draw date text during animation
    time_t epochTime = timeClient.getEpochTime();
    struct tm *ptm = gmtime ((time_t *)&epochTime); 
    int currentMonth = ptm->tm_mon; 
    int monthDay = ptm->tm_mday;
    String dayName = daysOfWeek[timeClient.getDay()];
    String dateStr = dayName + " " + months[currentMonth] + " " + String(monthDay);
    
    int textWidth = dateStr.length() * 6;
    if (textWidth > 64) textWidth = 64; 
    int textX = (PANEL_RES_X - textWidth) / 2;
    if (textX < 0) textX = 0;
    int textY = 24; 

    dma_display->setTextColor(COL_TEXT);
    dma_display->setCursor(textX, textY);
    dma_display->print(dateStr);
}

void animateTransition(int oldH, int oldM, int newH, int newM) {
    int startX = 1;
    int h1_x = startX;
    int h2_x = startX + 14;
    int m1_x = startX + 36;
    int m2_x = startX + 50;
    int digitY = 4; 

    // --- PHASE 1: EXPLODE OLD TIME ---
    if (oldH != -1) {
        int dispH = oldH;
        bool leadingZero = true;
        if (is12hFormat) {
           leadingZero = false;
           if (dispH == 0) dispH = 12;
           else if (dispH > 12) dispH -= 12;
        }
        int h1 = dispH / 10; 
        int h2 = dispH % 10;
        int m1 = oldM / 10; 
        int m2 = oldM % 10;

        // Use random steps from globals
        for (int i = 0; i <= currentAnimSteps; i++) {
            float progress = (float)i / (float)currentAnimSteps; 
            dma_display->fillScreen(COL_BLACK);
            
            if (leadingZero || h1 != 0) drawDigitParticles(h1_x, digitY, h1, progress, false);
            drawDigitParticles(h2_x, digitY, h2, progress, false);
            drawColon(true); 
            drawDigitParticles(m1_x, digitY, m1, progress, false);
            drawDigitParticles(m2_x, digitY, m2, progress, false);
            
            drawDateText();
            delay(10); // Base delay, speed controlled by steps
        }
    }

    // --- PHASE 2: IMPLODE NEW TIME ---
    int dispH = newH;
    bool leadingZero = true;
    if (is12hFormat) {
       leadingZero = false;
       if (dispH == 0) dispH = 12;
       else if (dispH > 12) dispH -= 12;
    }
    int h1 = dispH / 10; 
    int h2 = dispH % 10;
    int m1 = newM / 10; 
    int m2 = newM % 10;

    for (int i = 0; i <= currentAnimSteps; i++) {
        float progress = (float)i / (float)currentAnimSteps; 
        dma_display->fillScreen(COL_BLACK);
        
        if (leadingZero || h1 != 0) drawDigitParticles(h1_x, digitY, h1, progress, true); // true = Implode
        drawDigitParticles(h2_x, digitY, h2, progress, true);
        drawColon(true);
        drawDigitParticles(m1_x, digitY, m1, progress, true);
        drawDigitParticles(m2_x, digitY, m2, progress, true);
        
        drawDateText();
        delay(10);
    }
    
    // Final clean draw
    dma_display->fillScreen(COL_BLACK);
    if (leadingZero || h1 != 0) drawDigit(h1_x, digitY, h1, false);
    drawDigit(h2_x, digitY, h2, false);
    drawColon(true);
    drawDigit(m1_x, digitY, m1, false);
    drawDigit(m2_x, digitY, m2, false);
    drawDateText();
}

void animateImplosionOnly(int h, int m) {
    animateTransition(-1, -1, h, m);
}

// ------------------- MAIN -------------------

void setup() {
  Serial.begin(115200);
  initMatrix();
  randomSeed(analogRead(0)); 
  
  // 1. Load Config
  preferences.begin("clock-config", false);
  openWeatherMapApiKey = preferences.getString("apikey", "");
  cityLocation = preferences.getString("location", "London");
  timeFormatStr = preferences.getString("fmt", "12"); 
  tempUnitStr = preferences.getString("unit", "F");
  
  is12hFormat = (timeFormatStr == "12");
  preferences.end();

  // 2. Setup WiFiManager
  WiFiManager wm;
  wm.setAPCallback(configModeCallback); 
  wm.setSaveConfigCallback(saveConfigCallback);

  WiFiManagerParameter custom_apikey("apikey", "OWM API Key", openWeatherMapApiKey.c_str(), 50);
  WiFiManagerParameter custom_loc("location", "City", cityLocation.c_str(), 40);
  WiFiManagerParameter custom_fmt("fmt", "Format (12 or 24)", timeFormatStr.c_str(), 3);
  WiFiManagerParameter custom_unit("unit", "Units (C or F)", tempUnitStr.c_str(), 2);
  
  wm.addParameter(&custom_apikey);
  wm.addParameter(&custom_loc);
  wm.addParameter(&custom_fmt);
  wm.addParameter(&custom_unit);

  if (!wm.autoConnect("NetClockAP")) {
    Serial.println("Failed to connect");
  }

  // 3. Save Config if Changed
  if (String(custom_apikey.getValue()) != "") {
      preferences.begin("clock-config", false);
      preferences.putString("apikey", custom_apikey.getValue());
      preferences.putString("location", custom_loc.getValue());
      preferences.putString("fmt", custom_fmt.getValue());
      preferences.putString("unit", custom_unit.getValue());
      preferences.end();
      
      openWeatherMapApiKey = custom_apikey.getValue();
      cityLocation = custom_loc.getValue();
      timeFormatStr = custom_fmt.getValue();
      tempUnitStr = custom_unit.getValue();
      tempUnitStr.toUpperCase();
      is12hFormat = (timeFormatStr == "12");
  }

  timeClient.begin();
  
  // 4. Force Fetch Weather NOW to get TimeZone
  fetchWeather();
  timeClient.update();
  
  dma_display->fillScreen(COL_BLACK);
  
  // Initialize prev time vars
  prevHour = timeClient.getHours();
  prevMinute = timeClient.getMinutes();
  
  // Initial animation
  currentAnimSteps = 30; // Medium speed for startup
  currentAnimStyle = 0;  // Standard radial
  animateImplosionOnly(prevHour, prevMinute);
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "RESET") {
      WiFiManager wm;
      wm.resetSettings();
      preferences.begin("clock-config", false);
      preferences.clear();
      preferences.end();
      ESP.restart();
    }
    else if (cmd == "WEATHER") {
      weatherDuration = 60000;
      enterWeatherMode();
    }
  }

  timeClient.update();
  unsigned long currentMillis = millis();

  // Colon Blink
  if (currentMillis - lastSecondMillis >= 500) {
    lastSecondMillis = currentMillis;
    colonVisible = !colonVisible;
    
    if (colonVisible) {
        currentColonColor = tetrisPalette[random(0, 7)];
    }

    if (currentState == CLOCK_MODE) {
       drawColon(colonVisible);
    }
  }

  int currentH = timeClient.getHours();
  int currentM = timeClient.getMinutes();

  if (currentState == CLOCK_MODE) {
    if (currentM != prevMinute) {
      // 1. Top of the Hour (00) -> 60s Weather
      if (currentM == 0 && prevMinute != -1) { 
        weatherDuration = 60000;
        enterWeatherMode();
        prevMinute = currentM;
        prevHour = currentH;
        return; 
      }
      
      // 2. Minute ending in 7 -> 30s Weather
      if ((currentM % 10) == 7 && prevMinute != -1) { 
        weatherDuration = 30000;
        enterWeatherMode();
        prevMinute = currentM;
        prevHour = currentH;
        return; 
      }

      // 3. Normal Time Change -> Random Animation Style & Speed
      currentAnimStyle = random(0, 4);  // Randomize Style (0-3)
      currentAnimSteps = random(10, 50); // Randomize Speed (Fast 10 - Slow 50)
      
      animateTransition(prevHour, prevMinute, currentH, currentM);
      prevMinute = currentM;
      prevHour = currentH;
    }
  } 
  else if (currentState == WEATHER_MODE) {
    
    // Scroll logic for Bottom Description
    if (weatherTextWidth > 64) {
      if (millis() - lastScrollMillis > 50) { 
        lastScrollMillis = millis();
        weatherScrollX--;
        if (weatherScrollX < -weatherTextWidth) weatherScrollX = 64; 

        dma_display->fillRect(0, 23, 64, 8, COL_BLACK); 
        dma_display->setTextColor(COL_TEXT);
        dma_display->setCursor(weatherScrollX, 23);
        dma_display->print(currentWeatherDesc);
      }
    }

    if (millis() - weatherStartTime > weatherDuration) {
      wipeScreen();
      dma_display->fillScreen(COL_BLACK);
      currentState = CLOCK_MODE;
      // Return from Weather -> Implode Only
      currentAnimStyle = random(0, 4);
      currentAnimSteps = 30;
      animateImplosionOnly(currentH, currentM);
      prevMinute = currentM;
      prevHour = currentH;
    }
  }
}
