# Netclock
Internet clock for esp32 and P3 64x32 RGB Matrix

Requires following libraries;
ESP32-HUB75-MatrixPanel-I2S-DMA.h
WiFiManager.h
ArduinoJson.h
HTTPClient.h
NTPClient.h
Preferences.h
time.h

Follow custom pinout in config

Shows time on the 7s and on the hour.

After firmware flashing,  display will show "confiugre me" and youll see an open NetClock AP.  Connect to it and open portal or goto 192.168.4.1

Confgure open weather map API key, location, then set Wifi.  To access config page again youll need to turn off wifi and force reconfig.  You can also connect to USB and send 'RESET" serial command.  

Working on GPIO reset button.  Original code was for tetris effects, but shifted directions. 
