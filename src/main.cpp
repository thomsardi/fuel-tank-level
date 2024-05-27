#include <Arduino.h>
#include <WiFi.h>
#include <LiquidCrystal_I2C.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>

TaskHandle_t lcdTask;
QueueHandle_t levelValueQueue = xQueueCreate(10, sizeof(int));

const char* TAG = "tank level program";

const char* ssid = "TAPE MAS PUNGKI";
const char* pass = "tapegosong";

const char* apSsid = "esp32-ap";
const char* apPass = "esp32-pass";

IPAddress apIp(192, 168, 4, 1);
IPAddress apGateway(192, 168, 4, 1);
IPAddress apDns(255, 255, 255, 0);

unsigned long lastChecked = 0;
unsigned long lastReconnectMillis = 0;

int internalLed = 2;
const int analogPin = 34;
int analogValue = 0;

int lcdColumn = 16;
int lcdRow = 2;

LiquidCrystal_I2C lcd(0x27, lcdColumn, lcdRow);
AsyncWebServer server(80);

void createDisplay(LiquidCrystal_I2C &_lcd)
{
  _lcd.setCursor(0,0);
  _lcd.print("Tank Info");
  _lcd.setCursor(0, 1);
  _lcd.print("Level = 0%");
}

void updateDisplay(LiquidCrystal_I2C &_lcd, int value)
{
  String text = "%d%";
  _lcd.setCursor(8, 1);
  text.replace("%d", String(value));
  _lcd.print(text);
}

void lcdRoutine(void *pvParameter)
{
  while (1)
  {
    int buff;
    if (xQueueReceive(levelValueQueue, &buff, portMAX_DELAY) == pdTRUE)
    {
      updateDisplay(lcd, buff);
    }
  }
  
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info){
  ESP_LOGI(TAG, "Connected to %s\n", WiFi.SSID().c_str());
  ESP_LOGI(TAG, "IP Address : %s\n", WiFi.localIP().toString().c_str());
  ESP_LOGI(TAG, "Subnet : %s\n", WiFi.subnetMask().toString().c_str());
  ESP_LOGI(TAG, "Gateway : %s\n", WiFi.gatewayIP().toString().c_str());
  ESP_LOGI(TAG, "DNS 1 : %s\n", WiFi.dnsIP(0).toString().c_str());
  ESP_LOGI(TAG, "DNS 2 : %s\n", WiFi.dnsIP(1).toString().c_str());
  ESP_LOGI(TAG, "Hostname : %s\n", WiFi.getHostname());
  digitalWrite(internalLed, HIGH);
}

void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info){
  ESP_LOGI(TAG, "Wifi Connected");
}


void setup() {
  // put your setup code here, to run once:
  pinMode(internalLed, OUTPUT);
  Serial.begin(115200);
  xTaskCreate(lcdRoutine, "lcd task", 1024, NULL, 1, &lcdTask);
  WiFi.mode(WIFI_MODE_APSTA);
  WiFi.softAPConfig(apIp, apGateway, apDns);
  WiFi.softAP(apSsid, apPass);
  ESP_LOGI(TAG, "Access Point created");
  ESP_LOGI(TAG, "Access Point IP : %s\n", WiFi.softAPIP().toString().c_str());
  WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    ESP_LOGI(TAG, ".");
    delay(1000);
  }

  server.on("/api/get-data", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    const int maxCapacity = 4;
    const int kmPerLitre = 40;
    float valueInLitre = (analogValue / 2048) * maxCapacity;
    int percentage = map(analogValue, 0, 2048, 0, 100);
    JsonDocument doc;

    doc["max_capacity_in_litre"] = maxCapacity;
    doc["km_per_litre"] = kmPerLitre;
    doc["value_in_litre"] = valueInLitre;
    doc["raw_value"] = analogValue;
    doc["percentage"] = percentage;

    String output;

    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Initializing");
  delay(1000);
  lcd.clear();
  createDisplay(lcd);
  server.begin();
  lastChecked = millis();
}

void loop() {
  // put your main code here, to run repeatedly:
  analogValue = analogRead(analogPin);
  int percentageValue = map(analogValue, 0, 2048, 0, 100);

  if ((WiFi.status() != WL_CONNECTED) && (millis() - lastReconnectMillis >= 5000)) 
  {
    digitalWrite(internalLed, LOW);
    ESP_LOGI(TAG, "===============Reconnecting to WiFi...========================\n");
    if (WiFi.reconnect())
    {
      lastReconnectMillis = millis();
    }
  }
  if (millis() - lastChecked > 100)
  {
    xQueueSend(levelValueQueue, &percentageValue, 100);
    lastChecked = millis();
  }
  delay(10);
}
