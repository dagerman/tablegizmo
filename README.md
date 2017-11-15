# tablegizmo
A wireless device for displaying light and sound effects using MQTT messaging and a high-density WS2812B LED light strip.

The device is based on the ESP8622 and the DFPlayer mini. Current testing has been done using the WeMOS D1 mini WiFi module.
On startup, the device connects to a private WiFi network and discovers the MQTT broker IP address by querying MDNS for the mqtt service on host "gizmobroker".
The broker host is a Raspberry Pi that runs MQTT and NodeRED and has a second network interface for internet connectivity.

<notImplementedyet>
After connecting to the broker, the device publishes its MAC address to the registration topic gizmo/register to enable targeting individual devices with effect command messages from a central location.
Subsequently, it subscribes to a topic named gizmo/<macaddr>/command and is ready to respond to publications.
It will also subscribe to a topic named gizmo/all/command to react to broadcast messages intended for all devices. 
</notImplementedyet>

The bootup sequence is indicated via the following diagnostic light effects:
Solid RED   : Starting Up
Solid BLUE  : Attempting to connect to WiFi
Solid ORANGE: Attempting to connect to MQTT Server & Topic
Solid GREEN : Connected to WiFi & MQTT, awaiting command message



Required Arduino libraries (tested version):
- Adafruit ESP8266 (1.0.0)
- Adafruit MQTT Library (0.17.0)
- Adafruit NeoPixel (1.1.2)
- ArduinoJson (5.11.2)
- DFPlayer Mini Mp3 by Makuna (1.0.1)
- PubSubClient (2.6.0)
- SoftwareSerial (1.0.0)
- ESP8266_mdns (1.1.6)
