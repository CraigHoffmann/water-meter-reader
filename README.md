# Water Meter Reader
## Remote reading of a South Australian mains water meter

I made this water meter reader specifically for use with my home assistant monitoring system.  (If you haven't heard of [home assistant](https://www.home-assistant.io/) I suggest you check it out!)  After already designing a successful electricity meter reader this was my next requirement for home monitoring and automation.
I have read that some water meters have a rotating magnet inside the meter that can be detected by a hall effect sensor but couldn't find any specific information about the meter at my house (a typical South Australian mains water meter) so I decided to sense the rotating needle on the main dial.

![SA Water Meter](https://github.com/CraigHoffmann/water-meter-reader/blob/readme-edits/watermeter2.jpg?raw=true) ![Meter Dial](https://github.com/CraigHoffmann/water-meter-reader/blob/readme-edits/watermeter3.JPG?raw=true)

## Electronics

The meter reader uses a WeMOS D1 Mini (R3) but would work with any ESP8266 with a few conditions.

* It uses the ADC Input for the sensor reading so a variant that has access to that pin is required.  If the module doesn't have a resistot voltage divider on the ADC input you will need to add it/adjust the resistors.
* Setup is via the Serial Port so if the module doesn't have built in serial USB you will need an adaptor
* The WeMOS has a buil in 3.3v regulator


## Firmware

The Arduino IDE was used to develop and program the code.  If you have used this before with the ESP8266 devices you should have no problems.  If you're new to this then there are 100's of pages on the internet detailing how to setup the environment and program devices - I suggest you start by looking for one that suits your level of background knowledge.

## Case

The cover had been broken off the meter for years so I decided to make a new 3D printed cover that doubled as the case for the sensor.  If you don't have access to a 3D printer you could probably use the bottom of a plastic container or tin - anything that fits snug over the meter to keep the sensor aligned to the dial.

The case is a 3D printed case that doubles as a lid for the meter.  Files are available in the github repository todownload and print your own.

## Setup
