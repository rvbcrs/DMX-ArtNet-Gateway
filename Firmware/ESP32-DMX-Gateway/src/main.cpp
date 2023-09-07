#define DMX_RX_TIMEOUT 50           // Instead of DMX_RX_PACKET_TOUT_TICK
#define ART_NET_TIMEOUT_MS 5000     // Time in ms to wait for ArtNet packet before declaring disconnect
#define WIFICHECK_INTERVAL 5000L    // How often to check WIFI connection
#define BUTTONCHECK_INTERVAL 10000L // How often to check for button press
#define VERSION "1.4.0"             // Version
#define DEVICE_NAME "ConnoDMX"      // Prefix of access point for inital config
#define ESP_DMX_VERSION "1.1.3"     // Version of DMX built-with
#define OTA_PASSWORD "12345678"     // Password for OTA updates
#define SCREEN_WIDTH 128            // OLED display width
#define SCREEN_HEIGHT 64            // OLED display height
#define SSD1306_I2C_ADDRESS 0x3C    // OLED display I2C address
#define OLED_RESET -1               // Reset pin # (or -1 if sharing Arduino reset pin)
#define DMX_MAX_PACKET_SIZE 513     // DMX max packet size

#include <Arduino.h>
#include <WiFiClient.h>
#include <ESPAsyncWebServer.h>
#include <ArtnetWifi.h>
#include <esp_dmx.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <WiFi.h>
extern "C"
{
#include "esp_wifi.h"
}

uint32_t ESP_getChipId()
{
  uint32_t id = 0;
  for (int i = 0; i < 17; i = i + 8)
  {
    id |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  Serial.printf("%08X\n", id);
  return id;
}
String unique_hostname = "RvBCrS-DMX_" + String(ESP_getChipId(), HEX); // Suffix AP name with Chip ID

char LOGO[] PROGMEM = R"(

d8888b. db    db d8888b.  .o88b. d8888b. .d8888.        d8888b. .88b  d88. db    db 
88  `8D 88    88 88  `8D d8P  Y8 88  `8D 88'  YP        88  `8D 88'YbdP`88 `8b  d8' 
88oobY' Y8    8P 88oooY' 8P      88oobY' `8bo.          88   88 88  88  88  `8bd8'  
88`8b   `8b  d8' 88~~~b. 8b      88`8b     `Y8b. C8888D 88   88 88  88  88  .dPYb.  
88 `88.  `8bd8'  88   8D Y8b  d8 88 `88. db   8D        88  .8D 88  88  88 .8P  Y8. 
88   YD    YP    Y8888P'  `Y88P' 88   YD `8888Y'        Y8888D' YP  YP  YP YP    YP  
                                                                                                       
)";

int transmitPin = 25;
int receivePin = 26;
int enablePin = 13;

int animatieIndex = 0;
char animatieTekens[4] = {'|', '/', '-', '\\'};

dmx_port_t dmxPort = 1;
byte data[DMX_PACKET_SIZE];

unsigned long lastUpdate = millis();

const char *ssid = unique_hostname.c_str();
char password[32];

// Persistent storage for configuration parameters
Preferences preferences;

// ArtNet instellingen
ArtnetWifi artnet;

byte dmxbuffer[DMX_MAX_PACKET_SIZE];

unsigned long previousMillis = 0;
const long interval = 10; // Interval in milliseconden

unsigned int timer = 0;
bool dmxIsConnected = false;

/* These variables track if we are receiving ArtNet packets, if so ignore DMX In
   otherwise forward DMX packets from input to DMX Out port. */
bool artNetIsConnected = false;
unsigned int lastArtNetPacket = 0;

static ulong checkbutton_timeout = 0;
static ulong checkwifi_timeout = 0;

bool dmxChanged = false;

wifi_sta_list_t stationList;
int currentConnectedClients = -1;

// Webserver instellingen
AsyncWebServer server(80);
String dmxDataString = "";

boolean ledState = false;

// Declare an instance of the SSD1306 display object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

void animateDmxTransfer()
{
  display.setCursor(110, 54); // Positie aanpassen naar wens
  display.print(animatieTekens[animatieIndex]);
  display.display();
  animatieIndex++;
  if (animatieIndex > 3)
  {
    animatieIndex = 0;
  }
}

void onArtNetFrame(uint16_t universe, uint16_t numberOfChannels, uint8_t sequence, uint8_t *dmxData)
{
  ledState = !ledState;
  digitalWrite(LED_BUILTIN, ledState);

  for (int i = 1; i < numberOfChannels && i < DMX_MAX_PACKET_SIZE; ++i)
  {
    dmxbuffer[i] = dmxData[i - 1];
  }

  Serial.printf("<< ArtNet Uni %u Seq %03u Chans %u => DMX: %03d - %03d %03d %03d %03d - %03d %03d %03d %03d \n", universe, sequence, numberOfChannels, dmxbuffer[0], dmxbuffer[1], dmxbuffer[2], dmxbuffer[3], dmxbuffer[4], dmxbuffer[5], dmxbuffer[6], dmxbuffer[7], dmxbuffer[8]);

  /* If this is the first ArtNet data we've received, lets log it! */
  lastArtNetPacket = millis();

  if (!artNetIsConnected)
  {
    Serial.println("ArtNet Connected and getting packets...");
    artNetIsConnected = true;
  }

  dmx_write(dmxPort, dmxbuffer, DMX_PACKET_SIZE);
  dmx_send(dmxPort, DMX_PACKET_SIZE);
  dmx_wait_sent(dmxPort, DMX_TIMEOUT_TICK);

  animateDmxTransfer();
}

String getDmxChannelStatusWebPage()
{
  String html = R"=====(
    <html>
    <head>
      <script>
        function fetchDMXData() {
          fetch('/dmx-data')
            .then(response => {
              if (response.status === 200) {
                return response.json();
              }
              throw new Error('No data changed');
            })
            .then(data => {
              for (let channel in data) {
                document.getElementById('value-' + channel).innerText = data[channel];
              }
            })
            .catch(error => {
              console.log(error);
            });
        }

        setInterval(fetchDMXData, 1000);  // Vraag elke seconde de data op
      </script>
    </head>
    <body>
      <h1>DMX Data Viewer</h1>
      <table border='1'>
        <tr>
          <th>Kanaal</th>
  )=====";

  for (int i = 0; i < 512; i++)
  {
    html += "<th>" + String(i + 1) + "</th>";
  }

  html += "</tr><tr><td>Waarde</td>";

  for (int i = 0; i < 512; i++)
  {
    html += "<td id='value-" + String(i + 1) + "'>" + String(dmxbuffer[i]) + "</td>";
  }

  html += R"=====(
        </tr>
      </table>
    </body>
    </html>
  )=====";

  return html;
}
void initOLED()
{
  // Start I2C Communication SDA = 5 and SCL = 4 on Wemos Lolin32 ESP32 with built-in SSD1306 OLED
  Wire.begin(5, 4);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, SSD1306_I2C_ADDRESS, false, false))
  {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }
  delay(2000); // Pause for 2 seconds
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("RvBCrS-DMX");
  display.println("Version " + String(VERSION));
  display.println("ESP_DMX: " + String(ESP_DMX_VERSION));
  display.println("SSID: " + String(ESP_getChipId(), HEX));
  display.println("IP: " + WiFi.localIP().toString());
  display.display();
}

void initOTA()
{
  ArduinoOTA.onStart([]()
                     {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
    } else { // U_SPIFFS
        type = "filesystem";
    }
    // NB: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type); });

  ArduinoOTA.onEnd([]()
                   { Serial.println("\nEnd"); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });

  ArduinoOTA.onError([](ota_error_t error)
                     {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
        Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
        Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
        Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
        Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
        Serial.println("End Failed");
    } });

  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.begin();
}

void initEspAsAP()
{
  // Laad het wachtwoord uit het NVS
  preferences.begin("wifi-config", false);
  String storedPassword = preferences.getString("password", "12345678"); // Gebruik standaard wachtwoord als er niets is opgeslagen
  storedPassword.toCharArray(password, sizeof(password));
  preferences.end();

  WiFi.softAP(ssid, password);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    String html = "<form action=\"/set-password\" method=\"post\">";
    html += "Nieuw wachtwoord: <input type=\"password\" name=\"password\">";
    html += "<input type=\"submit\" value=\"Verander wachtwoord\">";
    html += "</form>";
    request->send(200, "text/html", html); });

  server.on("/set-password", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    if (request->hasParam("password", true)) {
      String newPassword = request->getParam("password", true)->value();
      newPassword.toCharArray(password, sizeof(password));

      // Sla het nieuwe wachtwoord op in het NVS
      preferences.begin("wifi-config", false);
      preferences.putString("password", newPassword);
      preferences.end();

      WiFi.softAP(ssid, password);
      request->send(200, "text/plain", "Wachtwoord succesvol veranderd!");
    } else {
      request->send(400, "text/plain", "Fout: Geen wachtwoord opgegeven.");
    } });
}

void initDMXPort()
{
  dmx_config_t config = DMX_CONFIG_DEFAULT;
  dmx_driver_install(dmxPort, &config, DMX_INTR_FLAGS_DEFAULT);

  /* Now set the DMX hardware pins to the pins that we want to use and setup
    will be complete! */
  dmx_set_pin(dmxPort, transmitPin, receivePin, enablePin);
}

void initArtNet()
{
  // This will be called for each packet received
  artnet.setArtDmxCallback(onArtNetFrame);
  artnet.begin();
}

void artNetCheck()
{
  if (artNetIsConnected)
  {
    if (lastArtNetPacket + ART_NET_TIMEOUT_MS <= millis())
    {
      artNetIsConnected = false;
      Serial.println("No ArtNet packets within timeout, setting to disconnected!");
    }
  }
}

void initWifi()
{
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi verbonden");
  Serial.println("IP adres: ");
  Serial.println(WiFi.localIP());
}

void OLEDLoop()
{
  if (millis() - previousMillis >= interval)
  {
    previousMillis = millis();

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("SSID: ");
    display.println(ssid);
    display.print("Verbonden clients: ");
    display.println(stationList.num);
    display.print("DMX data: ");
    display.println(artNetIsConnected ? "Actief" : "Inactief");
    display.display();
  }
}

void artnetLoop()
{
  // Check for connected clients
  if (stationList.num > 0)
  {
    artnet.read();
    artNetCheck();
  }
}

void checkWifiLoop()
{
  static ulong current_millis;
  current_millis = millis();

  // Check WiFi every WIFICHECK_INTERVAL seconds.
  if ((current_millis > checkwifi_timeout) || (checkwifi_timeout == 0))
  {
    if ((WiFi.status() != WL_CONNECTED))
    {
      Serial.println(F("WIFI not connected"));
    }
    else
    {
      Serial.print(F("WIFI Connected. Local IP: "));
      Serial.println(WiFi.localIP());
      /*
         Once we are connected to WIFI we must configure OTA update
        otherwise we will be unable to update firmware, calling initOTA
        before having a WIFI connection will cause the ESP to crash
      */
    }
    checkwifi_timeout = current_millis + WIFICHECK_INTERVAL;
  }
}

void checkOTALoop()
{
  ArduinoOTA.handle();
}

void getConnectedClientsListLoop()
{
  esp_wifi_ap_get_sta_list(&stationList);

  if (currentConnectedClients != stationList.num)
  {
    currentConnectedClients = stationList.num;
    Serial.println("Aantal verbonden clients: " + String(currentConnectedClients));

    for (int i = 0; i < stationList.num; i++)
    {
      Serial.print("Client ");
      Serial.print(i + 1);
      Serial.print(" MAC-adres: ");

      for (int j = 0; j < 6; j++)
      {
        Serial.print(stationList.sta[j].mac[j], HEX);
        if (j < 5)
          Serial.print(":");
      }
      Serial.println();
    }
  }
}

void initWebserverRoutes()
{
  server.on("/dmx-status", HTTP_GET, [](AsyncWebServerRequest *request)
            {
  Serial.println("Root endpoint aangeroepen");
  request->send(200, "text/html", getDmxChannelStatusWebPage());
  Serial.println("Root endpoint voltooid"); });

  server.on("/dmx-data", HTTP_GET, [](AsyncWebServerRequest *request)
            {
  if (dmxChanged) {
    String jsonResponse = "{";
    for (int i = 0; i < 512; i++) {
      jsonResponse += "\"" + String(i + 1) + "\": " + String(dmxbuffer[i]);
      if (i < 511) {
        jsonResponse += ",";
      }
    }
    jsonResponse += "}";
    request->send(200, "application/json", jsonResponse);
    dmxChanged = false;  // Reset de wijzigingsindicator
  } else {
    request->send(204);  // No Content response als er geen wijzigingen zijn
  } });
}

void initWebserver()
{
  server.begin();
}

void setup()
{
  initOLED();

  // set-up serial for debug output
  Serial.begin(115200);
  while (!Serial)
    ;
  delay(200);
  Serial.println(LOGO);
  Serial.println("\nStarting ConnoDMX Gateway Version " + String(VERSION));
  // Serial.println("\t WIFI Manager: " + String(ESP_ASYNC_WIFIMANAGER_VERSION));
  Serial.println("\t ESP_DMX: " + String(ESP_DMX_VERSION));

  // Initialiseer ESP als Access Point
  initEspAsAP();

  // Verbind met WiFi
  // initWifi();

  // Start ArtNet
  initArtNet();

  // Webserver routes
  initWebserverRoutes();

  // Start webserver
  initWebserver();

  // Start DMX
  initDMXPort();

  // Start OTA
  initOTA();
}

void loop()
{
  // checkWifiLoop(); // Ensure WIFI connected
  artnetLoop();                  // Process incoming ArtNet
  getConnectedClientsListLoop(); // Get list of connected clients
  checkOTALoop();                // Check for OTA updates
  OLEDLoop();                    // Update OLED display
}