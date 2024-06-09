#include <Arduino.h>
#include <WiFi.h>
#include <LiquidCrystal_I2C.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>

TaskHandle_t lcdTask;
QueueHandle_t levelValueQueue = xQueueCreate(10, sizeof(int));

const char* TAG = "tank level program";

const char* ssid = "tangki-123"; //ssid of hotspot
const char* pass = "tangki123"; //pass of hotspot

//ip, gateway, dns for station mode
IPAddress staIp(192, 168, 1, 45);
IPAddress staGateway(192, 168, 1, 1);
IPAddress staDns(255, 255, 255, 0);

const char* apSsid = "esp32-ap"; //ssid for esp32 access point
const char* apPass = "esp32-pass"; //pass for esp32 access point

//ip, gateway, dns for access point Mode
IPAddress apIp(192, 168, 4, 1);
IPAddress apGateway(192, 168, 4, 1);
IPAddress apDns(255, 255, 255, 0);

unsigned long lastChecked = 0;
unsigned long lastReconnectMillis = 0;

int internalLed = 2;
const int analogPin = 34;
int analogValue = 0;
int percentageValue = 0;

const int maxCapacity = 4; //tank maximum capacity in litre
const int kmPerLitre = 40; //fuel consumption, this is 1:40
float valueInLitre = 0;
const int minValue = 1900; //reading value at empty tank
const int maxValue = 3850; //reading value at max tank

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
  String text = "%d";
  _lcd.setCursor(8, 1);
  text.replace("%d", String(value));
  text += "%";
  _lcd.print(text);
  int leftOver = text.length();
  for(int i = 8 + text.length(); i < lcdColumn; i++)
  {
    _lcd.print(" ");
  }
}

void lcdRoutine(void *pvParameter)
{
  const char *_TAG = "lcd routine";
  while (1)
  {
    int buff;
    // ESP_LOGI(_TAG, "update display");
    if (xQueueReceive(levelValueQueue, &buff, portMAX_DELAY) == pdTRUE)
    {
      ESP_LOGI(_TAG, "update display");
      updateDisplay(lcd, buff);
    }
    // vTaskDelay(1000/portTICK_PERIOD_MS);
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
  // xTaskCreate(lcdRoutine, "lcd task", 2048, NULL, 1, &lcdTask);
  WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);  
  WiFi.mode(WIFI_MODE_APSTA);
  WiFi.softAPConfig(apIp, apGateway, apDns);
  WiFi.softAP(apSsid, apPass);
  ESP_LOGI(TAG, "Access Point created");
  ESP_LOGI(TAG, "Access Point IP : %s\n", WiFi.softAPIP().toString().c_str());
  // WiFi.config(staIp, staGateway, staDns);
  WiFi.begin(ssid, pass);
  uint8_t timeout = 0;
  while (WiFi.status() != WL_CONNECTED) {
    ESP_LOGI(TAG, ".");
    delay(1000);
    timeout++;
    if (timeout > 5)
    {
      break;
    }
  }

  server.on("/api/get-data", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    JsonDocument doc;

    doc["max_capacity_in_litre"] = maxCapacity;
    doc["km_per_litre"] = kmPerLitre;
    doc["value_in_litre"] = valueInLitre;
    doc["raw_value"] = analogValue;
    doc["percentage"] = percentageValue;

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
  // analogValue += 1;
  // analogValue = (analogValue>4095)? 2048 : analogValue;
  analogValue = analogRead(analogPin);
  percentageValue = map(analogValue, minValue, maxValue, 0, 100); //map value to 0 - 100 range
  percentageValue = constrain(percentageValue, 0, 100); //constrain value to  0 - 100, cap the value at 0 and 100
  valueInLitre = (float)percentageValue * maxCapacity / 100; //convert percent into litre
  ESP_LOGI(TAG, "raw value : %d\n", analogValue);
  ESP_LOGI(TAG, "percentage value : %d\n", percentageValue);
  ESP_LOGI(TAG, "value in litre : %.2f L\n", valueInLitre);

  if ((WiFi.status() != WL_CONNECTED) && (millis() - lastReconnectMillis >= 5000)) //reconnect to network
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
    // xQueueSend(levelValueQueue, &percentageValue, 100);
    updateDisplay(lcd, percentageValue);
    lastChecked = millis();
  }
  delay(10);
}
