/*


   Jota 2019 iButton reader firmware for PCB designed by Gijs Neerhof 2019
   The sketch uploads all scanned iButtons to a PHP script using HTTP headers in JSON format.
   The script and MySQL database run on a Raspberry Pi 3B+, making the project mobile
   Another JSON object on the raspberry pi can alternate the change in score and event type of each device,


   Compile using ESP8266 NodeMCU v1.0, with unmodified libraries mentioned below.
*/

#include <OneWire.h>
#include <FastLED.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>

#include <WiFiUdp.h>
#include <NTPClient.h>

#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <Int64String.h>

//#include <ArduinoJson.h>
//#include "FS.h"

#define DEBUG


#define WIFI
#define LEDSTRIP
#define MAX_TAGS 50
#define NUM_AFSCHIJNERS 10 //labeled L0 - L9

#ifdef WIFI
WiFiUDP ntpUDP;

// You can specify the time server pool and the offset, (in seconds)
// additionaly you can specify the update interval (in milliseconds).
// NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);

NTPClient timeClient(ntpUDP, "10.20.30.40", 7200, 600000);
#endif

//Events
#define UNKOWN 0
#define SIGN_IN 1
#define TAGGER 2
#define CHILD 3
#define START_GAME 4
#define MISC 5
#define UPDATE_UID 6
#define LIFE+ 7
#define LIFE- 8
#define STARTGAME 9
#define ENDGAME 10

//#define NUMTEAMS 10


//iButton section
#define IBUTTON_PIN D1

//RGB LED SECTION
#define NUMLEDS 1
#define BRIGHTNESS 30
#define LED_TYPE WS2812B
#define COLOR_ORDER RGB
#define RGB_PIN 4 //4 is D2

//reconnection interval
#define WIFI_INTERVAL 10000
#define SCAN_INTERVAL 2000
//#define LED_PIN LED_BUILTIN




//DO NOT CHANGE SSID
const char* ssid = "Jota2019";
String uid;


uint64_t tags[MAX_TAGS] = {0xA}; //make room to store NUM_TAGS iButton keys
int update_uid[MAX_TAGS] = {0};
unsigned long timestamp[MAX_TAGS] = {0};
int savedTags = 0;
//int maxSavedTags = 1;

#if MAX_TAGS > 10
int maxSavedTags = MAX_TAGS;
#else
int maxSavedTags = 1;
#endif


//defined from L0 - L10
uint64_t afschijners[NUM_AFSCHIJNERS] = {0x01fd2a0101000064, 0x017f7fa40100008f, 0x0158081401000011, 0x017241bf0100004f, 0x01620b560100009a, 0x01558a6d01000028, 0x0154d6a50100008e, 0x0156b8000100007b, 0x015810af01000056, 0x01fcff6d0100005d};
//define lijst met afschijners van te voren :\
//long lastTime = 0;
OneWire onewire(IBUTTON_PIN);


//unsigned long ibutton_reading_interval_start = 0;
unsigned long last_wifi_scan = 0;
unsigned long last_ibutton_scan = 0;
boolean scanning_wifi = false;
CRGB leds[NUMLEDS];


boolean adminFlag = false;


void setup(void) {
  Serial.begin(115200);

#ifdef DEBUG
  Serial.println();
  Serial.println("[setup]");
  Serial.print("Sketch name: ");
  Serial.println(__FILE__);
  Serial.print("Compiled on: ");
  Serial.print(__DATE__);
  Serial.print(", ");
  Serial.println(__TIME__);
  Serial.println("\n[iButton WiFi Reader] v1.0_kids \n");
#endif

  uid = "ESP-Jota2019-" + int64String(0ULL + ESP.getChipId(), HEX);
  WiFi.hostname(uid);
  //maybe define a settings file to load,
#ifdef DEBUG
  Serial.print("[setup] uid: ");
  Serial.println(uid);
#endif


  //autoconnect

#ifdef DEBUG
  Serial.println("[setup] setup credentials");
#endif
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid);
  WiFi.persistent(true);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  //WiFi.setOutputPower(20);

  scanning_wifi = true;
  timeClient.begin();

  pinMode(IBUTTON_PIN, INPUT_PULLUP);

  FastLED.addLeds<LED_TYPE, RGB_PIN, COLOR_ORDER>(leds, NUMLEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);

  //setup done, flash GRB led
  leds[0] = CRGB::Green;
  FastLED.show();
  //Serial.println("GREEN LED");
  delay(1000);
  leds[0] = CRGB::Red;
  FastLED.show();
  //Serial.println("RED LED");
  delay(1000);
  leds[0] = CRGB::Blue;
  FastLED.show();
#ifdef DEBUG
  Serial.println("[setup] BLUE LED");
#endif
  delay(1000);
  leds[0] = CRGB::Black;
  FastLED.show();

}

void loop(void) {


#ifdef WIFI
  if (WiFi.status() == WL_CONNECTED) {
    //make LED solid blue
    leds[0] = CRGB::Blue;
    FastLED.show();
#ifdef DEBUG
    Serial.println("[wifi] WiFi connected");
#endif

    //Timesync
    //timeClient.update();
#ifdef DEBUG
    Serial.print("[wifi] TimeSync: ");
    Serial.println(timeClient.getFormattedTime());
#endif


    //upload saved tags
    if (uploadTags()) {
      //upload tags was successful
      if (maxSavedTags < 10) //only reset maxSavedTags if it is less than 10, differentiate afschijners and kids
        maxSavedTags = 1;
    } else {
      //could not upload tags
      //server probably offline, no connection or error 500, or satellite post (etc)
      if (savedTags == maxSavedTags && maxSavedTags <= MAX_TAGS) {
#ifdef DEBUG
        Serial.println("[wifi] checking satelite post \n another slot is allowed, trying to get it");
#endif
        checkSatellite();
      } else {
#ifdef DEBUG
        Serial.println("[wifi] Another tagslot is not necessary");
#endif
      }
    }
    //finally, check for an update
    //httpUpdate(); //check for updates if WiFi connected
#ifdef DEBUG
    Serial.println("[wifi] Turning WiFi off");
    Serial.println("[wifi] Switching on Modem Sleep mode");
#endif
    //WiFi.disconnect();
    delay(100);
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.forceSleepBegin();
  } else {
    leds[0] = CRGB::Black;
    FastLED.show();
    delay(500);
    Serial.print(".");

    if (last_wifi_scan + WIFI_INTERVAL < millis()) {
      last_wifi_scan = millis();

#ifdef DEBUG
      Serial.print("[wifi] No WiFi connection ");
      //timeClient.update();
      Serial.print("[wifi] TimeSync: ");
      Serial.println(timeClient.getFormattedTime());
#endif


      if (!scanning_wifi) {
        //double blink blue led
        WiFi.forceSleepWake();
        WiFi.mode(WIFI_STA);
        WiFi.begin();
        leds[0] = CRGB::Blue;
        FastLED.show();
        delay(250);
        leds[0] = CRGB::Black;
        FastLED.show();
        delay(250);
        leds[0] = CRGB::Blue;
        FastLED.show();
        delay(250);
        leds[0] = CRGB::Black;
        FastLED.show();
        delay(250);

#ifdef DEBUG
        Serial.print("[wifi] Woke up, scanning for WiFi Network: ");
        Serial.println(WiFi.SSID());
#endif

        scanning_wifi = true;
      } else {

        last_wifi_scan = millis();
        leds[0] = CRGB::Black;
        FastLED.show();
        //WiFi.disconnect();
        delay(100);
        WiFi.mode(WIFI_OFF);
        delay(100);
        WiFi.forceSleepBegin();
#ifdef DEBUG
        Serial.println("[wifi] Turning WiFi off");
#endif

        scanning_wifi = false;

      }
    }

  }
#endif

  readTags();

}

boolean uploadTags() {
  if (savedTags > 0) {
#ifdef DEBUG
    Serial.println("[upload tags] Ready to upload saved tag!");
#endif

    //HTTPClient http;
    //send JSON of read keys
    String JSONtxt = "{\"esp_uid\":\"" + uid + "\", \"keys\":[";

    for (int i = 0; i < savedTags - 1; i++) {
      JSONtxt += "{\"ibutton_key\":";
      JSONtxt += "\"" + int64String(tags[i], 10) + "\",";
      JSONtxt += "\"update_uid\":" + String(update_uid[i]) + ", ";
      JSONtxt += "\"unix_time\":" + String(timestamp[i]) + "}, ";
      //JSONtxt += "\"event\":" + String(event[i]) + "},";
    }
    JSONtxt += "{\"ibutton_key\":";
    JSONtxt += "\"" + int64String(tags[savedTags - 1], 10) + "\",";
    JSONtxt += "\"update_uid\":" + String(update_uid[savedTags - 1]) + ", ";
    JSONtxt += "\"unix_time\":" + String(timestamp[savedTags - 1]) + "} ";
    //JSONtxt += "\"event\":" + String(event[savedTags - 1]) + "}";
    JSONtxt += "]}";


    //String JSONtxt = "{\"Test\": {\"first\": \"try\"}}" ;
#ifdef DEBUG
    Serial.print("[upload tags] JSON: ");
    Serial.println(JSONtxt);
#endif
    WiFiClient client;
    HTTPClient http;
#ifdef DEBUG
    Serial.println("[upload tags] generated WiFi and HTTP clients");
#endif

    http.begin(client, "http://10.20.30.40/Jota2019/insert_event.php");
    http.addHeader("Content-Type", "application/json");
    int httpCode = http.POST(JSONtxt);
    String payload = http.getString();
#ifdef DEBUG
    Serial.print("[upload tags] HTTP Code: ");
    Serial.println(httpCode);
    Serial.println(payload);
#endif

    http.end();
    if (httpCode == 200) {
      //succesfully uploaded
#ifdef DEBUG
      Serial.println("[upload tags] sucessfully uploaded tags");
#endif
      savedTags = 0;
      tags[savedTags] = 0x0;
      timestamp[savedTags] = 0;
      //delta_score[savedTags] = 0;
      CRGB led = leds[0];
      leds[0] = CRGB::Green;
      FastLED.show();
      delay(250);
      leds[0] = led;
      FastLED.show();

      return true;
    } else {
      //try to connect to esp8266.local
#ifdef DEBUG
      Serial.println("[upload tags] HTTP Error code, might be satelite post?");
#endif
      CRGB led = leds[0];
      leds[0] = CRGB::Red;
      FastLED.show();
      delay(250);
      leds[0] = led;
      FastLED.show();

    }
  } else {
#ifdef DEBUG
    Serial.println("[upload tags] No tags to upload");
#endif
  }
  return false;
}

boolean checkSatellite() {
  WiFiClient client;
  HTTPClient http;
#ifdef DEBUG
  Serial.println("[check satellite] Checking WiFi network for satellite post");
  Serial.println("[check satellite] Generated WiFi and HTTP clients");

#endif

  http.begin(client, "http://192.168.4.1");
  http.addHeader("Request-Slot", "yes");

  int httpCode = http.GET();
  String payload = http.getString();
  http.end();
#ifdef DEBUG
  Serial.print("[check satellite] HTTP Code: ");
  Serial.println(httpCode);
  Serial.println(payload);
#endif

  if (httpCode == 200) {
    //succesful
#ifdef DEBUG
    Serial.println("[check satellite] WiFi Network is a satellite post");
#endif
    if (payload.indexOf("yes") >= 0) {
      maxSavedTags++;
      if (maxSavedTags > MAX_TAGS) {
        maxSavedTags = MAX_TAGS;
      }
      //maxSavedTags %= MAX_TAGS+1;
      //flash green twice
      CRGB led = leds[0];
      leds[0] = CRGB::Green;
      FastLED.show();
      delay(250);
      leds[0] = led;
      FastLED.show();
      return true;
    } /*else {
                   //flash red twice
                   leds[0] = CRGB::Red;
                   FastLED.show();
                   delay(250);
                   leds[0] = CRGB::Black;
                   FastLED.show();
                   delay(250);
                   leds[0] = CRGB::Red;
                   FastLED.show();
                   delay(250);
                   leds[0] = CRGB::Black;
                   FastLED.show();
                 }*/

  } else {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());

  }
  return false;
}

void readTags() {

  byte addr[8];
  if (!onewire.search(addr)) {
    onewire.reset_search();
    delay(250);
    return;
  }
  /*
    Serial.print("ROM =");
    for (int i = 0; i < 8; i++) {
    Serial.write(' ');
    Serial.print(addr[i], HEX);

    }
    Serial.println();
  */

  if (OneWire::crc8(addr, 7) != addr[7]) {

#ifdef DEBUG
    Serial.println("[iButton scanner] CRC is not valid !");
#endif
  }

  uint64_t tmp = 0ULL;
  for ( int i = 0; i < 8; i++)
  {
    tmp = tmp << 8;
    tmp |= (uint64_t) addr[i];
  }
  //uint64_t tmp = addr[0] * 2 ^ 56 + addr[1] * 2 ^ 48 + addr[2] * 2 ^ 40 + addr[3] * 2 ^ 32 + addr[4] * 2 ^ 24 + addr[5] * 2 ^ 16 + addr[6] * 2 ^ 8 + addr[7]; //check calculation scheme
  if ((last_ibutton_scan + SCAN_INTERVAL) < millis()) {
    last_ibutton_scan = millis();
    if (tmp == 0x013a880e000000ff || tmp == 0x01743203000000f6) { //admin tags
#ifdef DEBUG
      Serial.println("[iButton scanner] Admin tag scanned");
#endif
      //next scanned tag is owner of this uid, update entry in users table with uid
      adminFlag = true; //set event tag and use
      //saved tag, single green blink, long for admin tag
      CRGB led = leds[0];
      leds[0] = CRGB::Green;
      FastLED.show();
      delay(1000);
      leds[0] = led;
      FastLED.show();
      return;
    }
    for (int i = 0; i < NUM_AFSCHIJNERS; i++) {
      if (tmp == afschijners[i]) {
        //gescande button is afschijner, verwijder laatst gescande button
        if (savedTags > 0) {
          savedTags--;
          if (maxSavedTags > 0) {
            maxSavedTags--;
#ifdef DEBUG
            Serial.printf("[iButton scanner] scanned afschijner tag %d, removing tag %d\n", i, savedTags + 1);
#endif
            CRGB led = leds[0];
            leds[0] = CRGB::White;
            FastLED.show();
            delay(250);
            leds[0] = led;
            FastLED.show();
          }
        } else {
#ifdef DEBUG
          Serial.printf("[iButton scanner] scanned afschijner tag %d, no tags to remove :(", i, savedTags + 1);
#endif
          CRGB led = leds[0];
          leds[0] = CRGB::Red;
          FastLED.show();
          delay(250);
          leds[0] = led;
          FastLED.show();

        }





        return;
      }
    }

    if (savedTags < maxSavedTags && savedTags < MAX_TAGS ) {
#ifdef DEBUG
      Serial.print("[iButton scanner] ");
#endif
      Serial.printf("%02x%02x%02x%02x%02x%02x%02x%02x\n", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], addr[6], addr[7]);

      //save it in tagslist
      tags[savedTags] = tmp;
#ifdef DEBUG
      Serial.printf("[iButton scanner] total slots: %d, total used: %d, max slots %d \n", maxSavedTags, savedTags + 1, MAX_TAGS);
#endif

#ifdef WIFI
      //delta_score[savedTags] = 0;
      timestamp[savedTags] = timeClient.getEpochTime();
      update_uid[savedTags] = 0;

      //edit this line somewhat to acommodate different events
      if (adminFlag) {
        adminFlag = false;
        update_uid[savedTags] = 1;
        //do admin flag thing!!
#ifdef DEBUG
        Serial.println("[iButton reader] This tag will be coupled to this boards uid!");
#endif
      }
      savedTags++;
#endif
      //Serial.println(tags[savedTags -1], HEX);

      //saved tag, single green blink
      CRGB led = leds[0];
      leds[0] = CRGB::Green;
      FastLED.show();
      delay(250);
      leds[0] = led;
      FastLED.show();
    } else {
      //could not save tag, triple red blink
#ifdef DEBUG
      Serial.println("[iButton scanner] could not save tag, no free slots");
#endif
      CRGB led = leds[0];
      leds[0] = CRGB::Red;
      FastLED.show();
      delay(250);
      leds[0] = CRGB::Black;
      FastLED.show();
      delay(100);
      leds[0] = CRGB::Red;
      FastLED.show();
      delay(250);
      if (maxSavedTags == MAX_TAGS) {
#ifdef DEBUG
        Serial.println("[iButton scanner] could not save tag, filled up to the max");
#endif
        leds[0] = CRGB::Black;
        FastLED.show();
        delay(100);
        leds[0] = CRGB::Red;
        FastLED.show();
        delay(250);
      }
      leds[0] = led;
      FastLED.show();
    }
  }
}

void httpUpdate() {
#ifdef DEBUG
  Serial.println("[update] trying to run update");
#endif
  //also call esphttp spiffs update to update settings.json
  //differentiate users by their hostname in the settings file, with different settings per type
  //like ledstrip, afschijner, kind, post, etc.


  t_httpUpdate_return ret = ESPhttpUpdate.update("10.20.30.40", 80, "/Jota2019/update/update_firmware.php", "version");
  switch (ret) {
    case HTTP_UPDATE_FAILED:
#ifdef DEBUG
      Serial.println("[update] Update failed.");
#endif
      break;
    case HTTP_UPDATE_NO_UPDATES:
#ifdef DEBUG
      Serial.println("[update] Update no Update.");
#endif
      break;
    case HTTP_UPDATE_OK:
#ifdef DEBUG
      Serial.println("[update] Update ok."); // may not called we reboot the ESP
#endif
      break;

  }
  leds[0] = CRGB::Blue;
  FastLED.show();
  Serial.println("GREEN LED");
  delay(1000);
  leds[0] = CRGB::Red;
  FastLED.show();
}
