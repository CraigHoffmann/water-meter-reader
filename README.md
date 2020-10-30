# Water Meter Reader
## Remote reading of a South Australian mains water meter

I made this water meter reader specifically for use with my home assistant monitoring system.  (If you haven't heard of [home assistant](https://www.home-assistant.io/) I suggest you check it out!)  After already designing a successful electricity meter reader this was my next requirement for home monitoring and automation.
I have read that some water meters have a rotating magnet inside the meter that can be detected by a hall effect sensor but couldn't find any specific information about the meter at my house (a typical South Australian mains water meter) so I decided to sense the rotating needle on the main dial.

![SA Water Meter](https://github.com/CraigHoffmann/water-meter-reader/blob/master/Images/watermeter2.jpg?raw=true) ![Meter Dial](https://github.com/CraigHoffmann/water-meter-reader/blob/master/Images/watermeter3.JPG?raw=true)

## Electronics

The meter reader uses a [WeMOS D1 Mini (R3)](https://docs.wemos.cc/en/latest/d1/d1_mini.html) but would work with any ESP8266 with a few conditions.

* It uses the ADC Input for the sensor reading so a variant that has access to that pin is required.  If the module doesn't have a resistor voltage divider on the ADC input you will need to add it/adjust the resistors to the sensor.
* The WeMOS has a built in 3.3v regulator for the ESP8266 and the sensor runs at 5V so suitable supply electronics are required.


![Circuit](https://github.com/CraigHoffmann/water-meter-reader/blob/master/Circuit/watermeter-cct.jpg?raw=true)

![Wiring](https://github.com/CraigHoffmann/water-meter-reader/blob/master/Circuit/watermeter-wiring.jpg?raw=true)


## Firmware

The Arduino IDE was used to develop and program the code.  If you have used this before with the ESP8266 devices you should have no problems.  If you're new to arduino or ESP8266's then there are hundreds of pages on the internet detailing how to setup the environment and program devices - I suggest you start by looking for one that suits your level of background knowledge to get up to speed.

If you want the temperature sensor functionality then the **DallasTemperature** and **OneWire** libraries need to be installed in the IDE.

## Case

My water meter cover had been broken off for years so I decided to make a new 3D printed cover that doubled as the case for the sensor.  I  The lid is "keyed" with the old lid hinge mounting so that the sensor is always aligned to the rotating dial. Files are available in the github repository to download and print your own.

If you don't have access to a 3D printer you could probably use the bottom of a plastic container or tin - anything that fits snug over the meter to keep the sensor aligned to the dial.  Use your imagination and maker skills.

## Setup

First step is to configure the wifi to connect to your network.  If the device wifi has not been setup previously WiFiManager will automatically setup an access point called **watermeter**. Connect to the access point with password **wifisetup** and then load the default page 192.168.4.1 and enter your network wifi details.   **If you want to force the erasure of the wifi settings for some reason power up the device with pin D7 (GPIO13) pulled to ground.**  This will erase the wifi settings and go stright to the WiFiManager access point config.

If you have a serial monitor plugged in when powering up the device it will display the assigned IP address on startup.  If you don't have an easy way of finding device IP addresses on your network then I suggest allocating an IP address in your router.  

* The main water meter setup page can be found at http://\<device-ip\>/  
* Alternatively if your system supports multicast DNS then you can use http://watermeter.local/ 
  
watermeter.local    |  watermeter.local/mqttsetup
:-------------------------:|:-------------------------:  
![Homepage](https://github.com/CraigHoffmann/water-meter-reader/blob/master/Images/homepage.jpg?raw=true) | ![MQTT Setup](https://github.com/CraigHoffmann/water-meter-reader/blob/master/Images/mqttsetup.jpg?raw=true)  
  
![Setup Trigger Thresholds](https://github.com/CraigHoffmann/water-meter-reader/blob/master/Images/setupchart.jpg?raw=true)

