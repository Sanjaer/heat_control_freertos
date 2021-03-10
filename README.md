# Heat controller with FreeRTOS and ESP8266

## Motivation
In my house the thermostat is purely mechanical with an expansion plate that acts as relay.
This project implements a heat controller to be attached to this old relay, as this house is not mine and I cannot change the thermostat this may be helpful for other makers who are stuck in a similar situation.

## Coder discretion is advised
This project has two big caveats:
1. I used it as a mean to try many features that both FreeRTOS and the ESP-IDF offer so many things may be (are) very over-engineered
2. The web page presentation is temporary and will be removed and integrated into a central server, to which all my devices will communicate. Although ugly, it makes the project standalone.

## Materials
- ESP8266 D1 Mini
- SHT30 Temperature sensor
- D1 Relay shield

## Keywords
ESP8266 ESP32 ESP-IDF FreeRTOS Temperature Control Domotics Home Automation IoT
