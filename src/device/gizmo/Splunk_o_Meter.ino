#include <Adafruit_NeoPixel.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Splunk-o-Meter LED Controller Code
// Created By: Jeff Champagne, Splunk, Inc. - jchampagne@splunk.com and Stefan Sievert, Splunk, Inc. - ssievert@splunk.com
// Version: 1.0 - November, 5, 2017
// Description: This code was created to manage LED special effects for a device used during BOTS & BOTN games. 
//
// Credits: Special effects were adapted from the excellent examples provided by Hans Luijten at https://www.tweaking4all.com/hardware/arduino/adruino-led-strip-effects,
//          ArduinoJSON library: https://github.com/bblanchon/ArduinoJson
//          Adafruit NeoPixel Library: https://github.com/adafruit/Adafruit_NeoPixel
//          Arduino core for ESP8266 WiFi Chip libraries: https://github.com/esp8266/Arduino
            
// AVAILABLE EFFECTS:
// 1) Solid(red,green,blue)                               - Solid LEDs in whatever color you specify
// 2) Fill(red,green,blue,speed)                          - LEDs "fill" from the bottom to the top at whatever speed you specify
// 3) Pulse(red,green,blue,speed,minBright,maxBright)     - LEDs pulse between bright and dim using the brightness and speed parameters specified
// 4) LavaFlow(red,green,blue,speed)                      - LEDs chase in a pulsing pattern
// 5) LavaPop(red,green,blue,sparkleLength,speed)         - Same effect as LavaFlow with an added "sparkle" effect in white
// 6) Cylon(red,green,blue,speed,returnDelay)             - Think Battlestar and/or Knight Rider...you get the idea
// 7) Arrow(red,green,blue,speed,returnDelay)             - Dots start at the ends of the LED stip and meet in the middle, then go back to the ends
// 8) CenterToOutside(red,green,blue,speed,returnDelay)   - The effects below are used to create the Cylon and Arrow effects. However, they can also be called independently
// 9) OutsideToCenter(red,green,blue,speed,returnDelay)
// 10) LeftToRight(red,green,blue,speed,returnDelay)
// 11) RightToLeft(red,green,blue,speed,returnDelay)

// SAMPLE MQTT MESSAGE FORMAT (JSON):
// "{"effectType":0,"repeatCount":10,"effect":"3","red":40,"green":100,"blue":0,"sparkleLength":5,"speed":20,"minBright":50,"maxBright":150}"
//    ALL VALUES ARE INTEGERS! Order does not matter.
//    REQUIRED VALUES: effectType, effect
//    NOTES: 'effect' is a numeric field mapped to the effects listed above.
//            Effect Types: 0 = Primary effect, these effects play continuously until replaced by another primary effect.
//                          1 = Special effect, these effects repeat for the 'repeatCount' specified. The 'repeatCount' is the number of times the effect is looped, it does not equal time.
//            Only the parameters needed for a particular effect are required in the JSON message. If others are provided, they will be ignored.
//            If you specify a special effect (1) and don't provide a 'repeatCount', it will not run.

// DIAGNOSTIC COLORS
// RED: Starting Up
// BLUE: Attempting to connect to WiFi
// Orange: Attempting to connect to MQTT Server & Topic
// GREEN: Connected to WiFi & MQTT, awaiting primary effect

//===============================================================================
// Sound stuff, STS 11/13 (may want to externalize this into a header file
#include <SoftwareSerial.h>
#include <DFMiniMp3.h>
class Mp3Notify
{
public:
  static void OnError(uint16_t errorCode)
  {
    // see DfMp3_Error for code meaning
    Serial.println();
    Serial.print("Com Error ");
    Serial.println(errorCode);
  }

  static void OnPlayFinished(uint16_t globalTrack)
  {
    Serial.println();
    Serial.print("Play finished for #");
    Serial.println(globalTrack);   
  }

  static void OnCardOnline(uint16_t code)
  {
    Serial.println();
    Serial.print("Card online ");
    Serial.println(code);     
  }

  static void OnCardInserted(uint16_t code)
  {
    Serial.println();
    Serial.print("Card inserted ");
    Serial.println(code); 
  }

  static void OnCardRemoved(uint16_t code)
  {
    Serial.println();
    Serial.print("Card removed ");
    Serial.println(code);  
  }
};

// instance a DFMiniMp3 object, 
// defined with the above notification class and the hardware serial class
//
// DFMiniMp3<HardwareSerial, Mp3Notify> mp3(Serial1);

// Some arduino boards only have one hardware serial port, so a software serial port is needed instead.
// comment out the above definition and uncomment these lines
SoftwareSerial secondarySerial(D7, D4); // RX, TX
DFMiniMp3<SoftwareSerial, Mp3Notify> mp3(secondarySerial);

//===============================================================================
// END Sound stuff, STS 11/13



// YOU CAN EDIT THESE VARIABLES

// WiFi and MQTT Information
const char* ssid = "GizmoNet";
const char* password = "splunkometer";
IPAddress   mqtt_server;
String      mqtt_server_host = "gizmobroker";
const char* mqtt_topic = "gizmo";
const char* mqtt_user = "esp8622";
const char* mqtt_pass = "tablegizmo";

// DO NOT MODIFY CODE BELOW THIS LINE

// Configure the controller
#define PIN D2
#define NUM_LEDS 18

// Create a structure for the visual MQTT messages
struct effect {
    int effect;
    int repeatCount;
    int red;
    int green;
    int blue;
    int speed;
    int minBright;
    int maxBright;
    int returnDelay;
    int sparkleLength;
};
// ...and one for the sound effect messages...
struct SoundEffect {
    int function;
    int track;
    int duration;
    int volume;
};

#define MP3_FUNC_PLAY   1
#define MP3_FUNC_PAUSE  2
#define MP3_FUNC_NEXT   3
#define MP3_FUNC_PREV   4
#define MP3_VOL_UP      5
#define MP3_VOL_DOWN    6
#define MP3_FUNC_STOP   7

// Declare a global variable for our "active" primary effect
effect primaryEffect;

WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, PIN, NEO_GRB + NEO_KHZ800);

// MQTT Callback function
void callback(char* topic, byte* payload, unsigned int length) {
 Serial.print("Message arrived [");
 Serial.print(topic);
 Serial.print("] Length[ ");
 Serial.print(length);
 Serial.print(" Bytes ]==> ");

 for (int i=0;i<length;i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  //Parse JSON message from MQTT
  StaticJsonBuffer<512> jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject((char *)payload);
  int effectType = root["eType"];
  Serial.print("Effect Type: ");
  Serial.println(effectType);
  // Check to see if the JSON message is a Primary Effect
  if (effectType == 0){
    // Assign variables from parsed JSON
    primaryEffect.effect = root["eNum"];
    primaryEffect.red = root["R"];
    primaryEffect.green = root["G"];
    primaryEffect.blue = root["B"];
    primaryEffect.speed = root["Speed"];
    primaryEffect.minBright = root["minLum"];
    primaryEffect.maxBright = root["maxLum"];
    primaryEffect.returnDelay = root["delay"];
    primaryEffect.sparkleLength = root["sLen"];
    // Print to the serial port for debugging
    Serial.print("Effect Number: ");
    Serial.println(primaryEffect.effect);
    Serial.print("Red: ");
    Serial.println(primaryEffect.red);
    Serial.print("Green: ");
    Serial.println(primaryEffect.green);
    Serial.print("Blue: ");
    Serial.println(primaryEffect.blue);
    Serial.print("Speed: ");
    Serial.println(primaryEffect.speed);
    Serial.print("MinBright: ");
    Serial.println(primaryEffect.minBright);
    Serial.print("MaxBright: ");
    Serial.println(primaryEffect.maxBright);
    Serial.print("Return Delay: ");
    Serial.println(primaryEffect.returnDelay);
    Serial.print("Sparkle Length: ");
    Serial.println(primaryEffect.sparkleLength);
  };
  // Check to see if the JSON message is a special effect
  if (effectType == 1){
    // Assign variables from parsed JSON
    effect specialEffect;
    specialEffect.effect = root["eNum"];
    specialEffect.repeatCount = root["repCount"];
    specialEffect.red = root["R"];
    specialEffect.green = root["G"];
    specialEffect.blue = root["B"];
    specialEffect.speed = root["Speed"];
    specialEffect.minBright = root["minLum"];
    specialEffect.maxBright = root["maxLum"];
    specialEffect.returnDelay = root["delay"];
    specialEffect.sparkleLength = root["sLen"];
    // Print to the serial port for debugging
    Serial.print("Effect Number: ");
    Serial.println(specialEffect.effect);
    Serial.print("Repeat Count: ");
    Serial.println(specialEffect.repeatCount);
    Serial.print("Red: ");
    Serial.println(specialEffect.red);
    Serial.print("Green: ");
    Serial.println(specialEffect.green);
    Serial.print("Blue: ");
    Serial.println(specialEffect.blue);
    Serial.print("Speed: ");
    Serial.println(specialEffect.speed);
    Serial.print("MinBright: ");
    Serial.println(specialEffect.minBright);
    Serial.print("MaxBright: ");
    Serial.println(specialEffect.maxBright);
    Serial.print("Return Delay: ");
    Serial.println(specialEffect.returnDelay);
    Serial.print("Sparkle Length: ");
    Serial.println(specialEffect.sparkleLength);
    Serial.print("Looping special effect "); Serial.print(specialEffect.repeatCount); Serial.println(" times...");
    // Since this is a special effect, loop through it the specified # of times
    for(int i=0; i<specialEffect.repeatCount; i++) {
      Serial.print(i+1); Serial.print("...");
      callEffect(specialEffect);
      Solid(0,0,0);
    }
  };
  
  // Check to see if the JSON message is a sound effect, STS 11/13
  if (effectType == 99){
    // Assign variables from parsed JSON
    SoundEffect soundEffect;
    soundEffect.function = root["function"];
    soundEffect.track = root["track"];
    soundEffect.duration = root["duration"]; 
    soundEffect.volume= root["volume"];
    Serial.print("Executing MP3 function...");     

    // Apply volume setting if provided, otherwise use previous.
    if( soundEffect.volume > 0 ) mp3.setVolume(soundEffect.volume);
    
    // Let's play some tunes, or pause them
    switch(soundEffect.function) {
      case MP3_FUNC_PLAY:
          Serial.println("PLAY");     
          mp3.playMp3FolderTrack(soundEffect.track);
          break;
      case MP3_FUNC_PAUSE:
          Serial.println("PAUSE");     
          mp3.pause();
          break;
      case MP3_FUNC_STOP:
          Serial.println("STOP");     
          mp3.stop();
          break;
      case MP3_FUNC_NEXT:
          Serial.println("NEXT");     
          mp3.stop();
          mp3.nextTrack();
          break;
      case MP3_FUNC_PREV:
          Serial.println("PREVIOUS");     
          mp3.prevTrack();
          break;
      case MP3_VOL_UP:
          Serial.println("VOLUME UP");     
          mp3.increaseVolume();
          break;
      case MP3_VOL_DOWN:
          Serial.println("VOLUME DOWN");     
          mp3.decreaseVolume();
          break;
      default:
          Serial.print(soundEffect.function);
          Serial.print(" (Unsupported MP3 function)"); 
          break;
    }
    waitMilliseconds(soundEffect.duration);
  };
  // END additions for: Check to see if the JSON message is a sound effect, STS 11/13

  
 Serial.println();
}


// MQTT Reconnect 
void reconnect() {
  unsigned char clientId[6];
  WiFi.macAddress(clientId);
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect((const char*)&clientId[0], mqtt_user, mqtt_pass)) {
       Serial.print("connected with clientID: [");
       for(int i=0; i<6; i++) { Serial.print(clientId[i]); }
       Serial.println("]");
       // ... and subscribe to topic
       client.subscribe(mqtt_topic);
      // Set LEDs to Green
      Solid(0,180,0);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Turn LEDs Orange
      Solid(255,60,0);
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  // Initialize serial port for writing
  Serial.begin(115200);
  // Inititialize LED Strip and turn all pixels off
  strip.begin();
  strip.show();
  // Set LEDs to red
  Solid(180,0,0);
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.println("");
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    // Set LEDs to Blue for WiFI connect
    Solid(0,0,180);
  }
  // Display WiFi Information
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Setup MQTT Connection
  if( doMDNSMagic() ) {
      client.setServer(mqtt_server, 1883);
      client.setCallback(callback);
  }
  // Initialize MP3 player, STS 11/13
  mp3.begin();
  mp3.setVolume(5);  // Tone it down for starters
  Serial.print("MP3 initialized. Number of songs on card: "); 
  Serial.println(mp3.getTotalTrackCount());

}

void loop() {
  // Reconnect to MQTT if disconnected
  if (!client.connected()){
    reconnect();
  }
  // Trigger the "actice" primary effect
  callEffect(primaryEffect);
  // Check for new MQTT messages
  client.loop();
}

// This function matches the effect number from the MQTT JSON to an effect function
void callEffect(effect calledEffect){
  switch(calledEffect.effect) {
    case 1 :
      Solid(calledEffect.red,calledEffect.green,calledEffect.blue);
      break;
    case 2 :
      Fill(calledEffect.red,calledEffect.green,calledEffect.blue,calledEffect.speed);
      break;
    case 3 :
      Pulse(calledEffect.red,calledEffect.green,calledEffect.blue,calledEffect.speed,calledEffect.minBright,calledEffect.maxBright);
      break;
    case 4 :
      LavaFlow(calledEffect.red,calledEffect.green,calledEffect.blue,calledEffect.speed);
      break;
    case 5 :
      LavaPop(calledEffect.red,calledEffect.green,calledEffect.blue,calledEffect.sparkleLength,calledEffect.speed);
      break;
    case 6 :
      Cylon(calledEffect.red,calledEffect.green,calledEffect.blue,calledEffect.speed,calledEffect.returnDelay);
      break;
    case 7 :
      Arrow(calledEffect.red,calledEffect.green,calledEffect.blue,calledEffect.speed,calledEffect.returnDelay);
      break;
    case 8 :
      CenterToOutside(calledEffect.red,calledEffect.green,calledEffect.blue,calledEffect.speed,calledEffect.returnDelay);
      break;
    case 9 :
      OutsideToCenter(calledEffect.red,calledEffect.green,calledEffect.blue,calledEffect.speed,calledEffect.returnDelay);
      break;
    case 10 :
      LeftToRight(calledEffect.red,calledEffect.green,calledEffect.blue,calledEffect.speed,calledEffect.returnDelay);
      break;
    case 11 :
      RightToLeft(calledEffect.red,calledEffect.green,calledEffect.blue,calledEffect.speed,calledEffect.returnDelay);
      break;
  }
}

// EFFECT FUNCTIONS START HERE
// EFFECT: Solid
void Solid(byte red, byte green, byte blue){
  setAll(red,green,blue);
  showStrip();
}

// EFFECT: Fill
void Fill(byte red, byte green, byte blue, int speedDelay){
    for (int i = 0; i < NUM_LEDS; i++){
      setPixel(i, red, green, blue);
      showStrip();
      delay(speedDelay);
    }
    setAll(0,0,0);
    delay(speedDelay);
}

// EFFECT: Pulse
void Pulse(byte red, byte green, byte blue, int speedDelay, int minBright, int maxBright){
  float r, g, b;
      
  for(int k = minBright; k < maxBright+1; k=k+1) { 
    r = (k/256.0)*red;
    g = (k/256.0)*green;
    b = (k/256.0)*blue;
    setAll(r,g,b);
    showStrip();
    delay(speedDelay);
  }
     
  for(int k = maxBright; k >= minBright; k=k-2) {
    r = (k/256.0)*red;
    g = (k/256.0)*green;
    b = (k/256.0)*blue;
    setAll(r,g,b);
    showStrip();
    delay(speedDelay);
  }
}

// EFFECT: LavaFlow
void LavaFlow(byte red, byte green, byte blue, int flowSpeed) {
  int Position=0;
  
  for(int i=0; i<NUM_LEDS; i++)
  {
      Position++; // = 0; //Position + Rate;
      for(int i=0; i<NUM_LEDS; i++) {
        // sine wave, 3 offset waves make a rainbow!
        //float level = sin(i+Position) * 127 + 128;
        //setPixel(i,level,0,0);
        //float level = sin(i+Position) * 127 + 128;
        setPixel(i,((sin(i+Position) * 80 + 150)/255)*red,
                   ((sin(i+Position) * 80 + 150)/255)*green,
                   ((sin(i+Position) * 80 + 150)/255)*blue);
      }
      
      showStrip();
      delay(flowSpeed);
  }
}

// EFFECT: LavaPop
void LavaPop(byte red, byte green, byte blue, int sparkleTime, int flowSpeed) {
  int Position=0;
  
  for(int i=0; i<NUM_LEDS; i++)
  {
      Position++; // = 0; //Position + Rate;
      for(int i=0; i<NUM_LEDS; i++) {
        // sine wave, 3 offset waves make a rainbow!
        //float level = sin(i+Position) * 127 + 128;
        //setPixel(i,level,0,0);
        //float level = sin(i+Position) * 127 + 128;
        setPixel(i,((sin(i+Position) * 80 + 150)/255)*red,
                   ((sin(i+Position) * 80 + 150)/255)*green,
                   ((sin(i+Position) * 80 + 150)/255)*blue);
      }
      int Pixel = random(NUM_LEDS);
      setPixel(Pixel,0xff,0xff,0xff);
      showStrip();
      delay(sparkleTime);
      setPixel(Pixel,red,green,blue);
      showStrip();
      delay(flowSpeed);
  }
}

// EFFECT: Cylon
void Cylon(byte red, byte green, byte blue, int speedDelay, int returnDelay){
  LeftToRight(red,green,blue,speedDelay,returnDelay);
  RightToLeft(red,green,blue,speedDelay,returnDelay);
}

// EFFECT: Arrow
void Arrow(byte red, byte green, byte blue, int speedDelay, int returnDelay){
  OutsideToCenter(red,green,blue,speedDelay,returnDelay);
  CenterToOutside(red,green,blue,speedDelay,returnDelay);
}

// EFFECT: CenterToOutside
void CenterToOutside(byte red, byte green, byte blue, int speedDelay, int returnDelay) {
  int eyeSize = 1;
  for(int i =((NUM_LEDS-1)/2); i>=0; i--) {
    setAll(0,0,0);
    
    setPixel(i, red/10, green/10, blue/10);
    for(int j = 1; j <= eyeSize; j++) {
      setPixel(i+j, red, green, blue); 
    }
    setPixel(i+eyeSize+1, red/10, green/10, blue/10);
    
    setPixel(NUM_LEDS-i, red/10, green/10, blue/10);
    for(int j = 1; j <= eyeSize; j++) {
      setPixel(NUM_LEDS-i-j, red, green, blue); 
    }
    setPixel(NUM_LEDS-i-eyeSize-1, red/10, green/10, blue/10);
    
    showStrip();
    delay(speedDelay);
  }
  delay(returnDelay);
}

// EFFECT: OutsideToCenter
void OutsideToCenter(byte red, byte green, byte blue, int speedDelay, int returnDelay) {
  int eyeSize = 1;
  for(int i = 0; i<=((NUM_LEDS-eyeSize)/2); i++) {
    setAll(0,0,0);
    
    setPixel(i, red/10, green/10, blue/10);
    for(int j = 1; j <= eyeSize; j++) {
      setPixel(i+j, red, green, blue); 
    }
    setPixel(i+eyeSize+1, red/10, green/10, blue/10);
    
    setPixel(NUM_LEDS-i, red/10, green/10, blue/10);
    for(int j = 1; j <= eyeSize; j++) {
      setPixel(NUM_LEDS-i-j, red, green, blue); 
    }
    setPixel(NUM_LEDS-i-eyeSize-1, red/10, green/10, blue/10);
    
    showStrip();
    delay(speedDelay);
  }
  delay(returnDelay);
}

// EFFECT: LeftToRight
void LeftToRight(byte red, byte green, byte blue, int speedDelay, int returnDelay) {
  int eyeSize = 1;
  for(int i = 0; i < NUM_LEDS-eyeSize-2; i++) {
    setAll(0,0,0);
    setPixel(i, red/10, green/10, blue/10);
    for(int j = 1; j <= eyeSize; j++) {
      setPixel(i+j, red, green, blue); 
    }
    setPixel(i+eyeSize+1, red/10, green/10, blue/10);
    showStrip();
    delay(speedDelay);
  }
  delay(returnDelay);
}

// EFFECT: RightToLeft
void RightToLeft(byte red, byte green, byte blue, int speedDelay, int returnDelay) {
  int eyeSize = 1;
  for(int i = NUM_LEDS-eyeSize-2; i > 0; i--) {
    setAll(0,0,0);
    setPixel(i, red/10, green/10, blue/10);
    for(int j = 1; j <= eyeSize; j++) {
      setPixel(i+j, red, green, blue); 
    }
    setPixel(i+eyeSize+1, red/10, green/10, blue/10);
    showStrip();
    delay(speedDelay);
  }
  delay(returnDelay);
}

// CONTROL FUNCTIONS START HERE
void showStrip() {
 #ifdef ADAFRUIT_NEOPIXEL_H 
   // NeoPixel
   strip.show();
 #endif
 #ifndef ADAFRUIT_NEOPIXEL_H
   // FastLED
   FastLED.show();
 #endif
}

void setPixel(int Pixel, byte red, byte green, byte blue) {
 #ifdef ADAFRUIT_NEOPIXEL_H 
   // NeoPixel
   strip.setPixelColor(Pixel, strip.Color(red, green, blue));
 #endif
 #ifndef ADAFRUIT_NEOPIXEL_H 
   // FastLED
   leds[Pixel].r = red;
   leds[Pixel].g = green;
   leds[Pixel].b = blue;
 #endif
}

void setAll(byte red, byte green, byte blue) {
  for(int i = 0; i < NUM_LEDS; i++ ) {
    setPixel(i, red, green, blue); 
  }
  showStrip();
}

bool doMDNSMagic() {
  char hostString[16] = {0};
  bool resolved = false;
  sprintf(hostString, "ESP_%06X", ESP.getChipId());
  Serial.print("Hostname: "); Serial.println(hostString);

  if (!MDNS.begin(hostString)) {
    Serial.println("Error setting up MDNS responder!");
  }
  Serial.println("mDNS responder started");
  MDNS.addService("esp", "tcp", 8080); // Announce esp tcp service on port 8080

  Serial.println("Sending mDNS query to find MQTT broker");
  int n = MDNS.queryService("mqtt", "tcp"); // Send out query for mqtt tcp services
  if (n == 0) {
    Serial.println("No MQTT services found");
  }
  else {
    // Never assume that we only find one, so we are searching for 
    // const char* mqtt_server_host = "gizmobroker";

    Serial.print(n);
    Serial.println(" service(s) found");
    for (int i = 0; i < n; ++i) {
      // Print details for each service found
      if( MDNS.hostname(i).equals(mqtt_server_host )) {
        Serial.println("Broker hostname match found in MDNS response.");
        mqtt_server = IPAddress(MDNS.IP(i));
        Serial.print("Broker IP address discovered as: "); Serial.println(mqtt_server);
        resolved = true;
      }
    }
  }
  return resolved;  
}

// Added for sound functions, STS 11/13
void waitMilliseconds(uint16_t msWait)
{
  uint32_t start = millis();
  
  while ((millis() - start) < msWait)
  {
    // calling mp3.loop() periodically allows for notifications 
    // to be handled without interrupts
    mp3.loop(); 
    delay(1);
  }
}


