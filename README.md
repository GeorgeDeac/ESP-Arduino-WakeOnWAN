# ESP-Arduino-WakeOnWAN
WakeOnWAN for esp arduino environment

Note: 
-Don't use it on a WAN yet, since this version communicates over http, either implement an https client or wait for update
-If you don't remember the last local ip the machine had send the magic packets over xxx.xxx.xxx.255 to broadcast it to the whole network
-Make sure your ethernet card supports magic packets

TODO:
-HTTPS tls or ssl
-Add rtc or ntp support
-Add scheduled wake intervals
-New decent interface (bootstrap maybe)
