/**
 * AI Status - ESP32-C6 Firmware
 *
 * Physical status indicator for Claude Code.
 * Displays current state (Idle/Working/Approval/Error) on 1.47" ST7789 screen.
 * Receives state updates via HTTP from Claude Code hooks.
 *
 * Hardware: Waveshare ESP32-C6 + 1.47" ST7789 (172x320, SPI)
 * Network: WiFi + mDNS (ai-status.local)
 * Provisioning: AP hotspot + Web page
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>

// =============================================================================
// Pin Configuration (Waveshare ESP32-C6 + 1.47" ST7789)
// =============================================================================
#define TFT_MOSI  6
#define TFT_SCK   7
#define TFT_CS    14
#define TFT_DC    15
#define TFT_RST   21
#define TFT_BL    22

// Screen dimensions
#define SCREEN_WIDTH  172
#define SCREEN_HEIGHT 320

// =============================================================================
// State Machine
// =============================================================================
enum DeviceState {
    STATE_IDLE,
    STATE_WORKING,
    STATE_APPROVAL,
    STATE_ERROR
};

#define COLOR_WHITE 0xFFFF

// State background colors (24-bit; converted to RGB565 at draw time)
#define BG_IDLE     0x1a1a2e
#define BG_WORKING  0x1565c0
#define BG_APPROVAL 0xe65100
#define BG_ERROR    0xb71c1c
#define BG_CONNECTED 0x0d5016

// =============================================================================
// Configuration
// =============================================================================
#define AP_SSID          "AI-Status-Setup"
#define AP_PASSWORD      "12345678"  // WPA2; min 8 chars required by 802.11
#define MDNS_HOSTNAME    "ai-status"
#define IDLE_TIMEOUT_MS  10000  // 10 seconds to return to idle
#define PREFS_NAMESPACE  "ai-status"

// =============================================================================
// Global Objects
// =============================================================================
Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, -1 /* MISO */);
Arduino_GFX *gfx = new Arduino_ST7789(
    bus, TFT_RST, 0 /* rotation */, true /* IPS */,
    SCREEN_WIDTH, SCREEN_HEIGHT,
    34 /* col offset 1 */, 0 /* row offset 1 */,
    34 /* col offset 2 */, 0 /* row offset 2 */
);

WebServer server(80);
Preferences prefs;

DeviceState currentState = STATE_IDLE;
unsigned long lastEventTime = 0;
bool wifiConnected = false;
bool inProvisioningMode = false;

// =============================================================================
// Display Functions
// =============================================================================

/**
 * Convert 24-bit hex color to RGB565
 */
uint16_t hexToRGB565(uint32_t hex) {
    uint8_t r = (hex >> 16) & 0xFF;
    uint8_t g = (hex >> 8) & 0xFF;
    uint8_t b = hex & 0xFF;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

/**
 * Draw current state on screen
 */
// GFX built-in font glyph: 6x8 pixels at size 1.
static const int GLYPH_W = 6;
static const int GLYPH_H = 8;

// Draw `text` horizontally centered at the given `y`, autoshrinking the text
// size if `preferredSize` would overflow the screen width.
static void drawCenteredText(const char* text, int y, int preferredSize) {
    int len = strlen(text);
    int size = preferredSize;
    while (size > 1 && len * GLYPH_W * size > SCREEN_WIDTH) {
        size--;
    }
    int width = len * GLYPH_W * size;
    int x = (SCREEN_WIDTH - width) / 2;
    if (x < 0) x = 0;
    gfx->setTextSize(size);
    gfx->setCursor(x, y);
    gfx->print(text);
}

void drawState(DeviceState state) {
    uint32_t bgHex;
    const char* icon;
    const char* label;

    switch (state) {
        case STATE_WORKING:
            bgHex = BG_WORKING;  icon = "*"; label = "Working"; break;
        case STATE_APPROVAL:
            bgHex = BG_APPROVAL; icon = "!"; label = "Approval"; break;
        case STATE_ERROR:
            bgHex = BG_ERROR;    icon = "X"; label = "Error"; break;
        case STATE_IDLE:
        default:
            bgHex = BG_IDLE;     icon = "~"; label = "Idle"; break;
    }

    gfx->fillScreen(hexToRGB565(bgHex));
    gfx->setTextColor(COLOR_WHITE);

    const int iconSize = 8;
    const int labelSize = 3;
    int16_t iconWidth = strlen(icon) * GLYPH_W * iconSize;
    int16_t labelWidth = strlen(label) * GLYPH_W * labelSize;

    gfx->setTextSize(iconSize);
    gfx->setCursor((SCREEN_WIDTH - iconWidth) / 2, SCREEN_HEIGHT / 2 - 60);
    gfx->print(icon);

    gfx->setTextSize(labelSize);
    gfx->setCursor((SCREEN_WIDTH - labelWidth) / 2, SCREEN_HEIGHT / 2 + 20);
    gfx->print(label);
}

/**
 * Draw provisioning screen (AP mode)
 * `apStatus` is shown at the bottom so we can debug AP broadcast failures
 * without needing a serial monitor — values: "AP OK", "softAP FAILED",
 * "AP IP=0.0.0.0" etc.
 */
void drawProvisioningScreen(const char* apStatus) {
    gfx->fillScreen(hexToRGB565(BG_IDLE));
    gfx->setTextColor(COLOR_WHITE);

    drawCenteredText("WiFi Setup", 30, 2);
    drawCenteredText("Connect to WiFi:", 80, 1);
    drawCenteredText(AP_SSID, 100, 2);
    drawCenteredText("Password:", 140, 1);
    drawCenteredText(AP_PASSWORD, 160, 2);
    drawCenteredText("Then open browser:", 200, 1);
    drawCenteredText("192.168.4.1", 220, 2);
    drawCenteredText(apStatus, 290, 1);
}

/**
 * Draw connected screen (briefly show IP)
 */
void drawConnectedScreen(String ip) {
    gfx->fillScreen(hexToRGB565(BG_CONNECTED));
    gfx->setTextColor(COLOR_WHITE);

    drawCenteredText("Connected!", 100, 2);
    drawCenteredText("IP:", 150, 1);
    drawCenteredText(ip.c_str(), 165, 2);
    drawCenteredText("ai-status.local", 210, 1);
}

// =============================================================================
// WiFi Provisioning (AP + Web)
// =============================================================================

// HTML page for WiFi configuration
const char PROVISIONING_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>AI Status - WiFi Setup</title>
    <style>
        body { font-family: -apple-system, sans-serif; background: #1a1a2e; color: #fff;
               margin: 0; padding: 20px; min-height: 100vh; }
        .container { max-width: 400px; margin: 0 auto; padding-top: 40px; }
        h1 { text-align: center; color: #e65100; }
        label { display: block; margin-top: 20px; font-size: 14px; color: #aaa; }
        input { width: 100%; padding: 12px; margin-top: 6px; border: 1px solid #333;
                border-radius: 8px; background: #16213e; color: #fff; font-size: 16px;
                box-sizing: border-box; }
        button { width: 100%; padding: 14px; margin-top: 30px; border: none;
                 border-radius: 8px; background: #e65100; color: #fff; font-size: 16px;
                 cursor: pointer; }
        button:hover { background: #ff6d00; }
        .status { text-align: center; margin-top: 20px; color: #aaa; font-size: 14px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>AI Status</h1>
        <p class="status">Configure WiFi to get started</p>
        <form action="/save" method="POST">
            <label>WiFi Network (SSID)</label>
            <input type="text" name="ssid" required placeholder="Your WiFi name">
            <label>Password</label>
            <input type="password" name="password" placeholder="WiFi password">
            <button type="submit">Connect</button>
        </form>
    </div>
</body>
</html>
)rawliteral";

const char SAVE_SUCCESS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>AI Status - Saved</title>
    <style>
        body { font-family: -apple-system, sans-serif; background: #1a1a2e; color: #fff;
               margin: 0; padding: 20px; min-height: 100vh; display: flex;
               align-items: center; justify-content: center; }
        .container { text-align: center; }
        h1 { color: #4caf50; }
        p { color: #aaa; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Saved!</h1>
        <p>Device is connecting to your WiFi...<br>This page will stop working shortly.</p>
    </div>
</body>
</html>
)rawliteral";

void handleProvisioningRoot() {
    server.send(200, "text/html", PROVISIONING_HTML);
}

void handleProvisioningSave() {
    String ssid = server.arg("ssid");
    String password = server.arg("password");

    if (ssid.length() == 0) {
        server.send(400, "text/plain", "SSID is required");
        return;
    }

    // Save credentials
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putString("ssid", ssid);
    prefs.putString("password", password);
    prefs.end();

    server.send(200, "text/html", SAVE_SUCCESS_HTML);

    Serial.printf("[Config] WiFi credentials saved: %s\n", ssid.c_str());

    // Restart after a short delay to connect
    delay(2000);
    ESP.restart();
}

// startProvisioningMode brings up the SoftAP and HTTP server. It does NOT
// touch the display — that happens later in setup() after WiFi has settled,
// so display init's SPI burst / backlight current draw can't interfere with
// WiFi RF calibration (which reads from flash and is timing-sensitive).
// On return, `statusBuf` is filled with a one-line diagnostic for the
// provisioning screen ("AP 192.168.4.1 mode=2" / "softAP FAILED" / ...).
void startProvisioningMode(char* statusBuf, size_t statusBufLen) {
    inProvisioningMode = true;
    Serial.println("[AP] Starting provisioning mode...");

    // Fully restart the WiFi driver before creating the SoftAP. This avoids a
    // half-initialized radio after USB power cycles or failed STA attempts.
    WiFi.persistent(false);
    WiFi.disconnect(true, false);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(500);
    WiFi.mode(WIFI_AP);
    delay(200);

    // ESP32-C6 ships with 802.11ax in the default protocol bitmap. Some
    // arduino-esp32 / IDF combos fail to emit AP beacons when AX is on for
    // SoftAP. Restrict to B/G/N so the AP actually broadcasts.
    esp_err_t protoErr = esp_wifi_set_protocol(
        WIFI_IF_AP,
        WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    if (protoErr != ESP_OK) {
        Serial.printf("[AP] set_protocol failed: %d\n", protoErr);
    } else {
        Serial.println("[AP] Protocol restricted to BGN (no AX)");
    }

    // Maximum TX power improves SoftAP visibility on small PCB-antenna boards.
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    IPAddress apIp(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    bool configOk = WiFi.softAPConfig(apIp, gateway, subnet);
    if (!configOk) {
        Serial.println("[AP] softAPConfig() failed");
    }

    // Pin to channel 1 so we don't end up on an auto-selected unstable channel.
    bool ok = WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, 4);
    delay(500);  // let the AP come up before we read its IP

    // setSleep must be called AFTER WiFi is active (per arduino-esp32 docs),
    // otherwise it's a no-op. Moved after softAP() so it actually takes effect.
    WiFi.setSleep(false);

    if (!ok) {
        Serial.println("[AP] softAP() returned false — broadcast may have failed");
    }

    apIp = WiFi.softAPIP();
    Serial.printf("[AP] Hotspot: %s (channel 1, WPA2)\n", AP_SSID);
    Serial.printf("[AP] Password: %s\n", AP_PASSWORD);
    Serial.printf("[AP] IP: %s\n", apIp.toString().c_str());
    Serial.printf("[AP] MAC: %s\n", WiFi.softAPmacAddress().c_str());

    if (!ok) {
        snprintf(statusBuf, statusBufLen, "softAP FAILED");
    } else if (!configOk) {
        snprintf(statusBuf, statusBufLen, "AP CFG FAIL mode=%d", (int)WiFi.getMode());
    } else {
        snprintf(statusBuf, statusBufLen, "AP %s mode=%d",
                 apIp.toString().c_str(), (int)WiFi.getMode());
    }

    // Setup provisioning web server
    server.on("/", handleProvisioningRoot);
    server.on("/save", HTTP_POST, handleProvisioningSave);
    server.begin();
}

// =============================================================================
// Normal Mode HTTP Handlers
// =============================================================================

void handleStatusPost() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));

    if (error) {
        server.send(400, "application/json", "{\"error\":\"invalid json\"}");
        return;
    }

    const char* state = doc["state"];
    if (!state) {
        server.send(400, "application/json", "{\"error\":\"missing state field\"}");
        return;
    }

    DeviceState newState = currentState;

    if (strcmp(state, "idle") == 0) {
        newState = STATE_IDLE;
    } else if (strcmp(state, "working") == 0) {
        newState = STATE_WORKING;
    } else if (strcmp(state, "approval") == 0) {
        newState = STATE_APPROVAL;
    } else if (strcmp(state, "error") == 0) {
        newState = STATE_ERROR;
    } else {
        server.send(400, "application/json", "{\"error\":\"unknown state\"}");
        return;
    }

    currentState = newState;
    lastEventTime = millis();
    drawState(currentState);

    String response = "{\"status\":\"ok\",\"state\":\"" + String(state) + "\"}";
    server.send(200, "application/json", response);

    Serial.printf("[State] → %s\n", state);
}

void handleStatusGet() {
    const char* stateStr = "idle";
    switch (currentState) {
        case STATE_IDLE:     stateStr = "idle"; break;
        case STATE_WORKING:  stateStr = "working"; break;
        case STATE_APPROVAL: stateStr = "approval"; break;
        case STATE_ERROR:    stateStr = "error"; break;
    }

    String response = "{\"state\":\"" + String(stateStr) + "\",\"uptime\":" + String(millis() / 1000) + "}";
    server.send(200, "application/json", response);
}

void handleReset() {
    server.send(200, "application/json", "{\"status\":\"resetting\"}");
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
    delay(1000);
    ESP.restart();
}

void startNormalMode() {
    // Setup HTTP endpoints
    server.on("/status", HTTP_POST, handleStatusPost);
    server.on("/status", HTTP_GET, handleStatusGet);
    server.on("/reset", HTTP_POST, handleReset);
    server.begin();

    Serial.println("[HTTP] Server started on port 80");
}

// =============================================================================
// WiFi Connection
// =============================================================================

bool connectToWiFi() {
    prefs.begin(PREFS_NAMESPACE, true);
    String ssid = prefs.getString("ssid", "");
    String password = prefs.getString("password", "");
    prefs.end();

    if (ssid.length() == 0) {
        Serial.println("[WiFi] No saved credentials");
        return false;
    }

    Serial.printf("[WiFi] Connecting to: %s\n", ssid.c_str());

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // same modem-sleep fix as AP mode — keeps HTTP responsive when USB host is attached
    WiFi.begin(ssid.c_str(), password.c_str());

    // Wait for connection (timeout 15s)
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    } else {
        Serial.println("[WiFi] Connection failed");
        return false;
    }
}

bool startMDNS() {
    MDNS.end();  // safe to call even if not started; avoids dup registration on reconnect
    if (MDNS.begin(MDNS_HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("[mDNS] Registered: %s.local\n", MDNS_HOSTNAME);
        return true;
    }
    Serial.println("[mDNS] Failed to start");
    return false;
}

// =============================================================================
// Setup & Loop
// =============================================================================

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== AI Status Starting ===");

    // Keep backlight OFF during WiFi bring-up. The LCD backlight pulls ~60 mA
    // and the SPI init burst hammers flash — both interfere with WiFi RF
    // calibration (which itself reads timing-sensitive data from flash at
    // init). Doing WiFi first, display second eliminates the mutual exclusion
    // we were seeing where either the AP broadcast OR the screen would work
    // per boot, but not both.
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, LOW);

    char apStatus[64] = "";
    bool provisioning = false;

    // WiFi FIRST — touch nothing on the SPI bus until this returns.
    if (connectToWiFi()) {
        wifiConnected = true;
        startMDNS();
    } else {
        startProvisioningMode(apStatus, sizeof(apStatus));
        provisioning = true;
    }

    // Let WiFi settle before we kick the SPI bus.
    delay(500);

    // NOW initialize the display.
    gfx->begin();
    gfx->fillScreen(0x0000);
    gfx->setTextWrap(false);

    if (provisioning) {
        drawProvisioningScreen(apStatus);
    } else {
        drawConnectedScreen(WiFi.localIP().toString());
        delay(3000);
        startNormalMode();
        currentState = STATE_IDLE;
        lastEventTime = millis();
        drawState(currentState);
    }

    // Backlight on only after the screen has real content to show.
    digitalWrite(TFT_BL, HIGH);

    Serial.println("=== AI Status Ready ===");
}

void loop() {
    server.handleClient();

    // Periodic diagnostic heartbeat — confirms firmware is alive and reports
    // AP / WiFi state so issues can be diagnosed via the serial monitor.
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 5000) {
        lastHeartbeat = millis();
        if (inProvisioningMode) {
            Serial.printf("[HB] AP mode | clients=%d | IP=%s | mode=%d\n",
                          WiFi.softAPgetStationNum(),
                          WiFi.softAPIP().toString().c_str(),
                          (int)WiFi.getMode());
        } else {
            Serial.printf("[HB] STA | status=%d | IP=%s | RSSI=%d\n",
                          WiFi.status(),
                          WiFi.localIP().toString().c_str(),
                          WiFi.RSSI());
        }
    }

    // Only WORKING auto-times-out to IDLE. APPROVAL and ERROR are sticky:
    // they represent "user attention required" / "something broke" — losing
    // those signals to a timer defeats the whole point of the device.
    if (!inProvisioningMode && currentState == STATE_WORKING) {
        if (millis() - lastEventTime > IDLE_TIMEOUT_MS) {
            currentState = STATE_IDLE;
            drawState(currentState);
            Serial.println("[State] → idle (timeout)");
        }
    }

    // WiFi reconnection (normal mode)
    if (!inProvisioningMode && WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Disconnected, reconnecting...");
        WiFi.reconnect();
        delay(5000);
        if (WiFi.status() == WL_CONNECTED) {
            startMDNS();
        }
    }

    delay(10);
}
