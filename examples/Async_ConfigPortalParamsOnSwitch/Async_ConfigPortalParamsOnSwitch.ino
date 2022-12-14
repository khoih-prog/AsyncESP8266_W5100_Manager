/****************************************************************************************************************************
  Async_ConfigPortalParamsOnSwitch.ino
  For Ethernet shields using ESP8266_W5100 (ESP8266 + LwIP W5100, W5100S)

  WebServer_ESP8266_W5100 is a library for the ESP8266 with Ethernet W5100, W5100S to run WebServer

  Modified from
  1. Tzapu               (https://github.com/tzapu/WiFiManager)
  2. Ken Taylor          (https://github.com/kentaylor)
  3. Alan Steremberg     (https://github.com/alanswx/ESPAsyncWiFiManager)
  4. Khoi Hoang          (https://github.com/khoih-prog/ESPAsync_WiFiManager)

  Built by Khoi Hoang https://github.com/khoih-prog/AsyncESP8266_W5100_Manager
  Licensed under MIT license
 *****************************************************************************************************************************/
/****************************************************************************************************************************
   This example will open a configuration portal when a predetermined button is pressed

   You then can modify ConfigPortal Parameters.
   This example will open a configuration portal when no configuration has been previously entered or when a button is pushed.

   Also in this example, a configurable password is required to connect to the configuration portal
   network. This is inconvenient but means that only those who know the password or those
   already connected to the target network can access the configuration portal and
   the network credentials will be sent from the browser over an encrypted connection and
   can not be read by observers.
 *****************************************************************************************************************************/

#if !( defined(ESP8266) )
  #error This code is intended to run on the (ESP8266 + W5100) platform! Please check your Tools->Board setting.
#endif

//////////////////////////////////////////////////////////////

// Use from 0 to 4. Higher number, more debugging messages and memory usage.
#define _ESPASYNC_ETH_MGR_LOGLEVEL_    4

// To not display stored SSIDs and PWDs on Config Portal, select false. Default is true
// Even the stored Credentials are not display, just leave them all blank to reconnect and reuse the stored Credentials
//#define DISPLAY_STORED_CREDENTIALS_IN_CP        false

//////////////////////////////////////////////////////////////
// Using GPIO4, GPIO16, or GPIO5
#define CSPIN             16

//////////////////////////////////////////////////////////

#include <FS.h>

// Now support ArduinoJson 6.0.0+ ( tested with v6.19.4 )
#include <ArduinoJson.h>      // get it from https://arduinojson.org/ or install via Arduino library manager

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
//needed for library
#include <ESPAsyncDNSServer.h>

#define USE_LITTLEFS      true

#if USE_LITTLEFS
  #include <LittleFS.h>
  FS* filesystem =      &LittleFS;
  #define FileFS        LittleFS
  #define FS_Name       "LittleFS"
#else
  FS* filesystem =      &SPIFFS;
  #define FileFS        SPIFFS
  #define FS_Name       "SPIFFS"
#endif

//////////////////////////////////////////////////////////

#define ESP_getChipId()   (ESP.getChipId())

#define LED_ON      LOW
#define LED_OFF     HIGH

//////////////////////////////////////////////////////////

// Onboard LED I/O pin on NodeMCU board
#define LED_BUILTIN       2         // Pin D4 mapped to pin GPIO2/TXD1 of ESP8266, NodeMCU and WeMoS, control on-board LED

//PIN_D0 can't be used for PWM/I2C
#define PIN_D0            16        // Pin D0 mapped to pin GPIO16/USER/WAKE of ESP8266. This pin is also used for Onboard-Blue LED. PIN_D0 = 0 => LED ON
#define PIN_D1            5         // Pin D1 mapped to pin GPIO5 of ESP8266
#define PIN_D2            4         // Pin D2 mapped to pin GPIO4 of ESP8266
#define PIN_D3            0         // Pin D3 mapped to pin GPIO0/FLASH of ESP8266
#define PIN_D4            2         // Pin D4 mapped to pin GPIO2/TXD1 of ESP8266
#define PIN_D5            14        // Pin D5 mapped to pin GPIO14/HSCLK of ESP8266
#define PIN_D6            12        // Pin D6 mapped to pin GPIO12/HMISO of ESP8266
#define PIN_D7            13        // Pin D7 mapped to pin GPIO13/RXD2/HMOSI of ESP8266
#define PIN_D8            15        // Pin D8 mapped to pin GPIO15/TXD2/HCS of ESP8266

//Don't use pins GPIO6 to GPIO11 as already connected to flash, etc. Use them can crash the program
//GPIO9(D11/SD2) and GPIO11 can be used only if flash in DIO mode ( not the default QIO mode)
#define PIN_D11           9         // Pin D11/SD2 mapped to pin GPIO9/SDD2 of ESP8266
#define PIN_D12           10        // Pin D12/SD3 mapped to pin GPIO10/SDD3 of ESP8266
#define PIN_SD2           9         // Pin SD2 mapped to pin GPIO9/SDD2 of ESP8266
#define PIN_SD3           10        // Pin SD3 mapped to pin GPIO10/SDD3 of ESP8266

#define PIN_D9            3         // Pin D9 /RX mapped to pin GPIO3/RXD0 of ESP8266
#define PIN_D10           1         // Pin D10/TX mapped to pin GPIO1/TXD0 of ESP8266
#define PIN_RX            3         // Pin RX mapped to pin GPIO3/RXD0 of ESP8266
#define PIN_TX            1         // Pin RX mapped to pin GPIO1/TXD0 of ESP8266

//////////////////////////////////////////////////////////////

/* Trigger for inititating config mode is Pin D1 and also flash button on NodeMCU
  Flash button is convenient to use but if it is pressed it will stuff up the serial port device driver
  until the computer is rebooted on windows machines.
*/
const int TRIGGER_PIN = PIN_D1; // D1 on NodeMCU and WeMos.
/*
  Alternative trigger pin. Needs to be connected to a button to use this pin. It must be a momentary connection
  not connected permanently to ground. Either trigger pin will work.
*/
const int TRIGGER_PIN2 = PIN_D2; // D2 on NodeMCU and WeMos.

int pinSda        = PIN_D2;     // Pin D2 mapped to pin GPIO4 of ESP8266
int pinScl        = PIN_D1;     // Pin D1 mapped to pin GPIO5 of ESP8266

//////////////////////////////////////////////////////////////

const char* JSON_CONFIG_FILE = "/ConfigSW.json";

//////////////////////////////////////////////////////////////

// You only need to format the filesystem once
//#define FORMAT_FILESYSTEM       true
#define FORMAT_FILESYSTEM         false

//////////////////////////////////////////////////////////////

// Variables

// Assuming max 49 chars
#define TZNAME_MAX_LEN            50
#define TIMEZONE_MAX_LEN          50

typedef struct
{
  char TZ_Name[TZNAME_MAX_LEN];     // "America/Toronto"
  char TZ[TIMEZONE_MAX_LEN];        // "EST5EDT,M3.2.0,M11.1.0"
  uint16_t checksum;
} EthConfig;

EthConfig         Ethconfig;

#define  CONFIG_FILENAME              F("/eth_cred.dat")

//////////////////////////////////////////////////////////////

// Indicates whether ESP has credentials saved from previous session
bool initialConfig = false;

// Use false if you don't like to display Available Pages in Information Page of Config Portal
// Comment out or use true to display Available Pages in Information Page of Config Portal
// Must be placed before #include <AsyncESP8266_W5100_Manager.h>
#define USE_AVAILABLE_PAGES     true

// From v1.0.10 to permit disable/enable StaticIP configuration in Config Portal from sketch. Valid only if DHCP is used.
// You'll loose the feature of dynamically changing from DHCP to static IP, or vice versa
// You have to explicitly specify false to disable the feature.
//#define USE_STATIC_IP_CONFIG_IN_CP          false

// Use false to disable NTP config. Advisable when using Cellphone, Tablet to access Config Portal.
// See Issue 23: On Android phone ConfigPortal is unresponsive (https://github.com/khoih-prog/ESP_WiFiManager/issues/23)
#define USE_ESP_ETH_MANAGER_NTP     true

// Just use enough to save memory. On ESP8266, can cause blank ConfigPortal screen
// if using too much memory
#define USING_AFRICA        false
#define USING_AMERICA       true
#define USING_ANTARCTICA    false
#define USING_ASIA          false
#define USING_ATLANTIC      false
#define USING_AUSTRALIA     false
#define USING_EUROPE        false
#define USING_INDIAN        false
#define USING_PACIFIC       false
#define USING_ETC_GMT       false

// Use true to enable CloudFlare NTP service. System can hang if you don't have Internet access while accessing CloudFlare
// See Issue #21: CloudFlare link in the default portal (https://github.com/khoih-prog/ESP_WiFiManager/issues/21)
#define USE_CLOUDFLARE_NTP          false

// New in v1.0.11
#define USING_CORS_FEATURE          true

//////////////////////////////////////////////////////////////

// Use USE_DHCP_IP == true for dynamic DHCP IP, false to use static IP which you have to change accordingly to your network
#if (defined(USE_STATIC_IP_CONFIG_IN_CP) && !USE_STATIC_IP_CONFIG_IN_CP)
  // Force DHCP to be true
  #if defined(USE_DHCP_IP)
    #undef USE_DHCP_IP
  #endif
  #define USE_DHCP_IP     true
#else
  // You can select DHCP or Static IP here
  //#define USE_DHCP_IP     true
  #define USE_DHCP_IP     false
#endif

#if ( USE_DHCP_IP )
  // Use DHCP

  #if (_ESPASYNC_ETH_MGR_LOGLEVEL_ > 3)
    #warning Using DHCP IP
  #endif

  IPAddress stationIP   = IPAddress(0, 0, 0, 0);
  IPAddress gatewayIP   = IPAddress(192, 168, 2, 1);
  IPAddress netMask     = IPAddress(255, 255, 255, 0);

#else
  // Use static IP

  #if (_ESPASYNC_ETH_MGR_LOGLEVEL_ > 3)
    #warning Using static IP
  #endif

  IPAddress stationIP   = IPAddress(192, 168, 2, 232);
  IPAddress gatewayIP   = IPAddress(192, 168, 2, 1);
  IPAddress netMask     = IPAddress(255, 255, 255, 0);
#endif

//////////////////////////////////////////////////////////////

#define USE_CONFIGURABLE_DNS      true

IPAddress dns1IP      = gatewayIP;
IPAddress dns2IP      = IPAddress(8, 8, 8, 8);

#include <AsyncESP8266_W5100_Manager.h>               //https://github.com/khoih-prog/AsyncESP8266_W5100_Manager

#define HTTP_PORT     80

//////////////////////////////////////////////////////////////

/******************************************
   // Defined in AsyncESP8266_W5100_Manager.hpp
  typedef struct
  {
    IPAddress _sta_static_ip;
    IPAddress _sta_static_gw;
    IPAddress _sta_static_sn;
    #if USE_CONFIGURABLE_DNS
    IPAddress _sta_static_dns1;
    IPAddress _sta_static_dns2;
    #endif
  }  ETH_STA_IPConfig;
******************************************/

ETH_STA_IPConfig EthSTA_IPconfig;

//////////////////////////////////////////////////////////////

void initSTAIPConfigStruct(ETH_STA_IPConfig &in_EthSTA_IPconfig)
{
  in_EthSTA_IPconfig._sta_static_ip   = stationIP;
  in_EthSTA_IPconfig._sta_static_gw   = gatewayIP;
  in_EthSTA_IPconfig._sta_static_sn   = netMask;
#if USE_CONFIGURABLE_DNS
  in_EthSTA_IPconfig._sta_static_dns1 = dns1IP;
  in_EthSTA_IPconfig._sta_static_dns2 = dns2IP;
#endif
}

//////////////////////////////////////////////////////////////

void displayIPConfigStruct(ETH_STA_IPConfig in_EthSTA_IPconfig)
{
  LOGERROR3(F("stationIP ="), in_EthSTA_IPconfig._sta_static_ip, ", gatewayIP =", in_EthSTA_IPconfig._sta_static_gw);
  LOGERROR1(F("netMask ="), in_EthSTA_IPconfig._sta_static_sn);
#if USE_CONFIGURABLE_DNS
  LOGERROR3(F("dns1IP ="), in_EthSTA_IPconfig._sta_static_dns1, ", dns2IP =", in_EthSTA_IPconfig._sta_static_dns2);
#endif
}

//////////////////////////////////////////////////////////////

void toggleLED()
{
  //toggle state
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
}

//////////////////////////////////////////////////////////////

#if USE_ESP_ETH_MANAGER_NTP
void printLocalTime()
{
  static time_t now;

  now = time(nullptr);

  if ( now > 1451602800 )
  {
    Serial.print("Local Date/Time: ");
    Serial.print(ctime(&now));
  }
}
#endif

void heartBeatPrint()
{
#if USE_ESP_ETH_MANAGER_NTP
  printLocalTime();
#else
  static int num = 1;

  if (eth.connected())
    Serial.print(F("H"));        // H means connected to Ethernet
  else
    Serial.print(F("F"));        // F means not connected to Ethernet

  if (num == 80)
  {
    Serial.println();
    num = 1;
  }
  else if (num++ % 10 == 0)
  {
    Serial.print(F(" "));
  }

#endif
}

//////////////////////////////////////////////////////////////

void check_status()
{
  static ulong checkstatus_timeout  = 0;
  static ulong LEDstatus_timeout    = 0;

  static ulong current_millis;

#if USE_ESP_ETH_MANAGER_NTP
#define HEARTBEAT_INTERVAL    60000L
#else
#define HEARTBEAT_INTERVAL    10000L
#endif

#define LED_INTERVAL          2000L

  current_millis = millis();

  if ((current_millis > LEDstatus_timeout) || (LEDstatus_timeout == 0))
  {
    // Toggle LED at LED_INTERVAL = 2s
    toggleLED();
    LEDstatus_timeout = current_millis + LED_INTERVAL;
  }

  // Print hearbeat every HEARTBEAT_INTERVAL (10) seconds.
  if ((current_millis > checkstatus_timeout) || (checkstatus_timeout == 0))
  {
    heartBeatPrint();
    checkstatus_timeout = current_millis + HEARTBEAT_INTERVAL;
  }
}

//////////////////////////////////////////////////////////////

int calcChecksum(uint8_t* address, uint16_t sizeToCalc)
{
  uint16_t checkSum = 0;

  for (uint16_t index = 0; index < sizeToCalc; index++)
  {
    checkSum += * ( ( (byte*) address ) + index);
  }

  return checkSum;
}

//////////////////////////////////////////////////////////////

bool loadConfigData()
{
  File file = FileFS.open(CONFIG_FILENAME, "r");
  LOGERROR(F("LoadCfgFile "));

  memset((void *) &Ethconfig,       0, sizeof(Ethconfig));
  memset((void *) &EthSTA_IPconfig, 0, sizeof(EthSTA_IPconfig));

  if (file)
  {
    file.readBytes((char *) &Ethconfig,   sizeof(Ethconfig));
    file.readBytes((char *) &EthSTA_IPconfig, sizeof(EthSTA_IPconfig));
    file.close();

    LOGERROR(F("OK"));

    if ( Ethconfig.checksum != calcChecksum( (uint8_t*) &Ethconfig, sizeof(Ethconfig) - sizeof(Ethconfig.checksum) ) )
    {
      LOGERROR(F("Ethconfig checksum wrong"));

      return false;
    }

    displayIPConfigStruct(EthSTA_IPconfig);

    return true;
  }
  else
  {
    LOGERROR(F("failed"));

    return false;
  }
}

//////////////////////////////////////////////////////////////

void saveConfigData()
{
  File file = FileFS.open(CONFIG_FILENAME, "w");
  LOGERROR(F("SaveCfgFile "));

  if (file)
  {
    Ethconfig.checksum = calcChecksum( (uint8_t*) &Ethconfig, sizeof(Ethconfig) - sizeof(Ethconfig.checksum) );

    file.write((uint8_t*) &Ethconfig, sizeof(Ethconfig));

    displayIPConfigStruct(EthSTA_IPconfig);

    file.write((uint8_t*) &EthSTA_IPconfig, sizeof(EthSTA_IPconfig));
    file.close();

    LOGERROR(F("OK"));
  }
  else
  {
    LOGERROR(F("failed"));
  }
}

//////////////////////////////////////////////////////////////

bool readConfigFile()
{
  // this opens the config file in read-mode
  File f = FileFS.open(JSON_CONFIG_FILE, "r");

  if (!f)
  {
    Serial.println(F("Configuration file not found"));

    return false;
  }
  else
  {
    // we could open the file
    size_t size = f.size();
    // Allocate a buffer to store contents of the file.
    std::unique_ptr<char[]> buf(new char[size + 1]);

    // Read and store file contents in buf
    f.readBytes(buf.get(), size);
    // Closing file
    f.close();
    // Using dynamic JSON buffer which is not the recommended memory model, but anyway
    // See https://github.com/bblanchon/ArduinoJson/wiki/Memory%20model

#if (ARDUINOJSON_VERSION_MAJOR >= 6)
    DynamicJsonDocument json(1024);
    auto deserializeError = deserializeJson(json, buf.get());

    if ( deserializeError )
    {
      Serial.println(F("JSON parseObject() failed"));

      return false;
    }

    serializeJson(json, Serial);
#else
    DynamicJsonBuffer jsonBuffer;
    // Parse JSON string
    JsonObject& json = jsonBuffer.parseObject(buf.get());

    // Test if parsing succeeds.
    if (!json.success())
    {
      Serial.println(F("JSON parseObject() failed"));
      return false;
    }

    json.printTo(Serial);
#endif
  }

  Serial.println(F("\nConfig file was successfully parsed"));

  return true;
}

//////////////////////////////////////////////////////////////

bool writeConfigFile()
{
  Serial.println(F("Saving config file"));

#if (ARDUINOJSON_VERSION_MAJOR >= 6)
  DynamicJsonDocument json(1024);
#else
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
#endif

  // Open file for writing
  File f = FileFS.open(JSON_CONFIG_FILE, "w");

  if (!f)
  {
    Serial.println(F("Failed to open config file for writing"));

    return false;
  }

#if (ARDUINOJSON_VERSION_MAJOR >= 6)
  serializeJsonPretty(json, Serial);
  // Write data to file and close it
  serializeJson(json, f);
#else
  json.prettyPrintTo(Serial);
  // Write data to file and close it
  json.printTo(f);
#endif

  f.close();

  Serial.println(F("\nConfig file was successfully saved"));

  return true;
}

//////////////////////////////////////////////////////////////

void initEthernet()
{
  SPI.begin();
  SPI.setClockDivider(SPI_CLOCK_DIV4);
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE0);

  LOGWARN(F("Default SPI pinout:"));
  LOGWARN1(F("MOSI:"), MOSI);
  LOGWARN1(F("MISO:"), MISO);
  LOGWARN1(F("SCK:"),  SCK);
  LOGWARN1(F("CS:"),   CSPIN);
  LOGWARN(F("========================="));

#if !USING_DHCP
  //eth.config(localIP, gateway, netMask, gateway);
  eth.config(EthSTA_IPconfig._sta_static_ip, EthSTA_IPconfig._sta_static_gw, EthSTA_IPconfig._sta_static_sn,
             EthSTA_IPconfig._sta_static_dns1);
#endif

  eth.setDefault();

  if (!eth.begin())
  {
    Serial.println("No Ethernet hardware ... Stop here");

    while (true)
    {
      delay(1000);
    }
  }
  else
  {
    Serial.print("Connecting to network : ");

    while (!eth.connected())
    {
      Serial.print(".");
      delay(1000);
    }
  }

  Serial.println();

#if USING_DHCP
  Serial.print("Ethernet DHCP IP address: ");
#else
  Serial.print("Ethernet Static IP address: ");
#endif

  Serial.println(eth.localIP());
}

//////////////////////////////////////////////////////////////

// Setup function
void setup()
{
  // Initialize the LED digital pin as an output.
  pinMode(LED_BUILTIN, OUTPUT);

  // Initialize trigger pins
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(TRIGGER_PIN2, INPUT_PULLUP);

  // Put your setup code here, to run once
  Serial.begin(115200);

  while (!Serial && millis() < 5000);

  delay(200);

  Serial.print(F("\nStarting Async_ConfigPortalParamsOnSwitch using "));
  Serial.print(FS_Name);
  Serial.print(F(" on "));
  Serial.print(ARDUINO_BOARD);
  Serial.print(F(" with "));
  Serial.println(SHIELD_TYPE);
  Serial.println(ASYNC_ESP8266_W5100_MANAGER_VERSION);

  Serial.setDebugOutput(false);

#if FORMAT_FILESYSTEM
  Serial.println(F("Forced Formatting."));
  FileFS.format();
#endif

  // Format FileFS if not yet
  if (!FileFS.begin())
  {
    FileFS.format();

    Serial.println(F("SPIFFS/LittleFS failed! Already tried formatting."));

    if (!FileFS.begin())
    {
      // prevents debug info from the library to hide err message.
      delay(100);

#if USE_LITTLEFS
      Serial.println(F("LittleFS failed!. Please use SPIFFS or EEPROM. Stay forever"));
#else
      Serial.println(F("SPIFFS failed!. Please use LittleFS or EEPROM. Stay forever"));
#endif

      while (true)
      {
        delay(1);
      }
    }
  }

  if (!readConfigFile())
  {
    Serial.println(F("Failed to read configuration file, using default values"));
  }

  unsigned long startedAt = millis();

  initSTAIPConfigStruct(EthSTA_IPconfig);

  //Local intialization. Once its business is done, there is no need to keep it around
  // Use this to default DHCP hostname to ESP8266-XXXXXX
  //AsyncESP8266_W5100_Manager AsyncESP8266_W5100_manager(&webServer, &dnsServer);
  // Use this to personalize DHCP hostname (RFC952 conformed)
  AsyncWebServer webServer(HTTP_PORT);

  AsyncDNSServer dnsServer;

  AsyncESP8266_W5100_Manager AsyncESP8266_W5100_manager(&webServer, &dnsServer, "AsyncCP-ParamsOnSW");

#if !USE_DHCP_IP
  // Set (static IP, Gateway, Subnetmask, DNS1 and DNS2) or (IP, Gateway, Subnetmask)
  AsyncESP8266_W5100_manager.setSTAStaticIPConfig(EthSTA_IPconfig);
#endif

#if USING_CORS_FEATURE
  AsyncESP8266_W5100_manager.setCORSHeader("Your Access-Control-Allow-Origin");
#endif

  bool configDataLoaded = false;

  if (loadConfigData())
  {
    configDataLoaded = true;

    //If no access point name has been previously entered disable timeout
    AsyncESP8266_W5100_manager.setConfigPortalTimeout(120);

    Serial.println(F("Got stored Credentials. Timeout 120s for Config Portal"));

#if USE_ESP_ETH_MANAGER_NTP

    if ( strlen(Ethconfig.TZ_Name) > 0 )
    {
      LOGERROR3(F("Saving current TZ_Name ="), Ethconfig.TZ_Name, F(", TZ = "), Ethconfig.TZ);

      //configTzTime(Ethconfig.TZ, "pool.ntp.org" );
      configTzTime(Ethconfig.TZ, "time.nist.gov", "0.pool.ntp.org", "1.pool.ntp.org");
    }
    else
    {
      Serial.println(F("Current Timezone is not set. Enter Config Portal to set."));
    }

#endif
  }
  else
  {
    // Enter CP only if no stored SSID on flash and file
    Serial.println(F("Open Config Portal without Timeout: No stored Credentials."));
    initialConfig = true;
  }

  //////////////////////////////////

  // Connect ETH now if using STA
  initEthernet();

  //////////////////////////////////

  if (initialConfig)
  {
    Serial.println(F("We haven't got any access point credentials, so get them now"));

    Serial.print(F("Starting configuration portal @ "));
    Serial.println(eth.localIP());

    digitalWrite(LED_BUILTIN, LED_ON); // Turn led on as we are in configuration mode.

    //sets timeout in seconds until configuration portal gets turned off.
    //If not specified device will remain in configuration mode until
    //switched off via webserver or device is restarted.
    //AsyncESP8266_W5100_manager.setConfigPortalTimeout(600);

    // Starts an access point
    if (!AsyncESP8266_W5100_manager.startConfigPortal())
      Serial.println(F("Not connected to ETH network but continuing anyway."));
    else
    {
      Serial.println(F("ETH network connected...yeey :)"));
    }

#if USE_ESP_ETH_MANAGER_NTP
    String tempTZ   = AsyncESP8266_W5100_manager.getTimezoneName();

    if (strlen(tempTZ.c_str()) < sizeof(Ethconfig.TZ_Name) - 1)
      strcpy(Ethconfig.TZ_Name, tempTZ.c_str());
    else
      strncpy(Ethconfig.TZ_Name, tempTZ.c_str(), sizeof(Ethconfig.TZ_Name) - 1);

    const char * TZ_Result = AsyncESP8266_W5100_manager.getTZ(Ethconfig.TZ_Name);

    if (strlen(TZ_Result) < sizeof(Ethconfig.TZ) - 1)
      strcpy(Ethconfig.TZ, TZ_Result);
    else
      strncpy(Ethconfig.TZ, TZ_Result, sizeof(Ethconfig.TZ_Name) - 1);

    if ( strlen(Ethconfig.TZ_Name) > 0 )
    {
      LOGERROR3(F("Saving current TZ_Name ="), Ethconfig.TZ_Name, F(", TZ = "), Ethconfig.TZ);

      //configTzTime(Ethconfig.TZ, "pool.ntp.org" );
      configTzTime(Ethconfig.TZ, "time.nist.gov", "0.pool.ntp.org", "1.pool.ntp.org");
    }
    else
    {
      LOGERROR(F("Current Timezone Name is not set. Enter Config Portal to set."));
    }

#endif

    AsyncESP8266_W5100_manager.getSTAStaticIPConfig(EthSTA_IPconfig);

    saveConfigData();
  }

  digitalWrite(LED_BUILTIN, LED_OFF); // Turn led off as we are not in configuration mode.

  startedAt = millis();

  Serial.print(F("After waiting "));
  Serial.print((float) (millis() - startedAt) / 1000);
  Serial.print(F(" secs more in setup(), connection result is "));

  if (eth.connected())
  {
    Serial.print(F("connected. Local IP: "));
    Serial.println(eth.localIP());
  }
}

//////////////////////////////////////////////////////////////

void loop()
{
  // is configuration portal requested?
  if ((digitalRead(TRIGGER_PIN) == LOW) || (digitalRead(TRIGGER_PIN2) == LOW))
  {
    Serial.println(F("\nConfiguration portal requested."));
    digitalWrite(LED_BUILTIN, LED_ON); // turn the LED on by making the voltage LOW to tell us we are in configuration mode.

    //Local intialization. Once its business is done, there is no need to keep it around
    // Use this to default DHCP hostname to ESP8266-XXXXXX
    //AsyncESP8266_W5100_Manager AsyncESP8266_W5100_manager(&webServer, &dnsServer);
    // Use this to personalize DHCP hostname (RFC952 conformed)
    AsyncWebServer webServer(HTTP_PORT);
    AsyncDNSServer dnsServer;

    AsyncESP8266_W5100_Manager AsyncESP8266_W5100_manager(&webServer, &dnsServer, "AsyncCP-ParamsOnSW");

    //Check if there is stored credentials.
    //If not found, device will remain in configuration mode until switched off via webserver.
    Serial.println(F("Opening configuration portal"));

    if (loadConfigData())
    {
      //If no access point name has been previously entered disable timeout
      AsyncESP8266_W5100_manager.setConfigPortalTimeout(120);

      Serial.println(F("Got stored Credentials. Timeout 120s for Config Portal"));
    }
    else
    {
      // Enter CP only if no stored SSID on flash and file
      Serial.println(F("Open Config Portal without Timeout: No stored Credentials."));
      initialConfig = true;
    }

    // Extra parameters to be configured
    // After connecting, parameter.getValue() will get you the configured value
    // Format: <ID> <Placeholder text> <default value> <length> <custom HTML> <label placement>

    // Sets timeout in seconds until configuration portal gets turned off.
    // If not specified device will remain in configuration mode until
    // switched off via webserver or device is restarted.
    //AsyncESP8266_W5100_manager.setConfigPortalTimeout(120);

#if !USE_DHCP_IP
#if USE_CONFIGURABLE_DNS
    // Set static IP, Gateway, Subnetmask, DNS1 and DNS2
    AsyncESP8266_W5100_manager.setSTAStaticIPConfig(stationIP, gatewayIP, netMask, dns1IP, dns2IP);
#else
    // Set static IP, Gateway, Subnetmask, Use auto DNS1 and DNS2.
    AsyncESP8266_W5100_manager.setSTAStaticIPConfig(stationIP, gatewayIP, netMask);
#endif
#endif

#if USING_CORS_FEATURE
    AsyncESP8266_W5100_manager.setCORSHeader("Your Access-Control-Allow-Origin");
#endif

    // Start an access point and goes into a blocking loop awaiting configuration.
    // Once the user leaves the portal with the exit button
    // processing will continue

    if (!AsyncESP8266_W5100_manager.startConfigPortal())
      Serial.println(F("Not connected to ETH network but continuing anyway."));
    else
    {
      Serial.println(F("ETH network connected...yeey :)"));
      Serial.print(F("Local IP: "));
      Serial.println(eth.localIP());
    }

#if USE_ESP_ETH_MANAGER_NTP
    String tempTZ   = AsyncESP8266_W5100_manager.getTimezoneName();

    if (strlen(tempTZ.c_str()) < sizeof(Ethconfig.TZ_Name) - 1)
      strcpy(Ethconfig.TZ_Name, tempTZ.c_str());
    else
      strncpy(Ethconfig.TZ_Name, tempTZ.c_str(), sizeof(Ethconfig.TZ_Name) - 1);

    const char * TZ_Result = AsyncESP8266_W5100_manager.getTZ(Ethconfig.TZ_Name);

    if (strlen(TZ_Result) < sizeof(Ethconfig.TZ) - 1)
      strcpy(Ethconfig.TZ, TZ_Result);
    else
      strncpy(Ethconfig.TZ, TZ_Result, sizeof(Ethconfig.TZ_Name) - 1);

    if ( strlen(Ethconfig.TZ_Name) > 0 )
    {
      LOGERROR3(F("Saving current TZ_Name ="), Ethconfig.TZ_Name, F(", TZ = "), Ethconfig.TZ);

      configTime(Ethconfig.TZ, "pool.ntp.org");
    }
    else
    {
      LOGERROR(F("Current Timezone Name is not set. Enter Config Portal to set."));
    }

#endif

    AsyncESP8266_W5100_manager.getSTAStaticIPConfig(EthSTA_IPconfig);

    saveConfigData();

    // Getting posted form values and overriding local variables parameters
    // Config file is written regardless the connection state

    // Writing JSON config file to flash for next boot
    writeConfigFile();

    digitalWrite(LED_BUILTIN, LED_OFF); // Turn LED off as we are not in configuration mode.
  }

  // Put your main code here, to run repeatedly...
  check_status();
}
