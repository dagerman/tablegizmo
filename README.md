# tablegizmo
A wireless device for displaying light and sound effects using MQTT messaging

The device is based on the ESP8622 and the DFPlayer mini. Current testing has been done using the WeMOS D1 mini WiFi module.
On startup, the device connects to a private WiFi network and discovers the MQTT broker IP address by querying MDNS for the mqtt service on host "gizmobroker".
The broker host is a Raspberry Pi that runs MQTT and NodeRED and has a second network interface for internet connectivity.

After connecting to the broker, the device publishes its MAC address to a registration topic to enable targeting individual devices with effect command messages.
Subsequently, it subscribes to a topic named gizmo/<macaddr>/command and is ready to respond to publications.
It will also subscribe to a topic named gizmo/all/command to react to broadcast messages intended for all devices. 

The bootup sequence is indicated via the following diagnostic light effects:
Solid RED   : Starting Up
Solid BLUE  : Attempting to connect to WiFi
Solid ORANGE: Attempting to connect to MQTT Server & Topic
Solid GREEN : Connected to WiFi & MQTT, awaiting command message

TBD: more docs
