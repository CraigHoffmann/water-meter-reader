# Water Meter Reader
## Remote reading of a South Australian mains water meter

I made this water meter reader specifically for use with my home assistant monitoring system.  (If you haven't heard of [home assistant](https://www.home-assistant.io/) I suggest you check it out!)  After already designing a successful electricity meter reader this was my next requirement for home monitoring and automation.
I have read that some water meters have a rotating magnet inside the meter that can be detected by a hall effect sensor but couldn't find any specific information about the meter at my house (a typical South Australian mains water meter) so I decided to sense the rotating needle on the main dial.

![SA Water Meter](https://github.com/CraigHoffmann/water-meter-reader/blob/master/Images/watermeter2.jpg?raw=true) ![Meter Dial](https://github.com/CraigHoffmann/water-meter-reader/blob/master/Images/watermeter3.JPG?raw=true)

## Electronics

The meter reader uses a [WeMOS D1 Mini (R3)](https://docs.wemos.cc/en/latest/d1/d1_mini.html) but would work with any ESP8266 with a few conditions.

* It uses the ADC Input for the sensor reading so a variant that has access to that pin is required.  If the module doesn't have a resistor voltage divider on the ADC input you will need to add it/adjust the resistors to the sensor.
* The WeMOS has a built in 3.3v regulator for the ESP8266 and the sensor runs at 5V so suitable supply electronics are required.

An optical reflectance sensor (TCRT5000) is used to detect the IR reflected light and is monitored using the analog input.  As the dial turns the reflectance changes as the needle passes by.  Trigger levels are set on the analog signal to count the revolutions of the dial.  Its simple but works reliably for my meter.

I have the Wemos D1 in the garage and run a 3wire cable out to the meter with for the sensor.  Alternatively all electrics could be placed at the meter and run power to the meter (or maybe use solar and batteries?)

| <img src="https://github.com/CraigHoffmann/water-meter-reader/blob/master/Circuit/watermeter-cct.jpg?raw=true" alt="Circuit" width="75%"> |
:-------------------------:

| <img src="https://github.com/CraigHoffmann/water-meter-reader/blob/master/Circuit/watermeter-wiring.jpg?raw=true" alt="Wiring" width="75%"> |
:-------------------------:

## Firmware

The Arduino IDE was used to develop and program the code.  If you have used this before with the ESP8266 devices you should have no problems.  If you're new to arduino or ESP8266's then there are hundreds of pages on the internet detailing how to setup the environment and program devices - I suggest you start by looking for one that suits your level of background knowledge to get up to speed.



## Case

My water meter cover had been broken off for years so I decided to make a new 3D printed cover that doubled as the case for the sensor.  The lid is "keyed" with the old lid hinge mounting so that the sensor is always aligned to the rotating dial. Files are available in the github repository to download and print your own. The electronics are just hot glued to the inside plate. 

If you don't have access to a 3D printer you could probably use the bottom of a plastic container or tin - anything that fits snug over the meter to keep the sensor aligned to the dial.  Use your imagination and maker skills.

## Setup

First step is to configure the wifi to connect to your network.  If the device wifi has not been setup previously WiFiManager will automatically setup an access point called **watermeter**. Connect to the access point with password **wifisetup** and then load the default page 192.168.4.1 and enter your network wifi details.   **If you want to force the erasure of the wifi settings for some reason power up the device with pin D7 (GPIO13) pulled to ground.**  This will erase the wifi settings and go stright to the WiFiManager access point config.

If you have a serial monitor plugged in when powering up the device it will display the assigned IP address on startup.  If you don't have an easy way of finding device IP addresses on your network then I suggest allocating an IP address in your router.  

* The main water meter setup page can be found at http://\<device-ip\>/  
* Alternatively if your system supports multicast DNS then you can use http://watermeter.local/ 

The default landing page is shows the cumulative count of kLiters used as well as the usage in the past 1 minute.  Both the MQTT setup and Sensor setup can be accessed from the home page. 

| ![Homepage](https://github.com/CraigHoffmann/water-meter-reader/blob/master/Images/homepage.jpg?raw=true) |
:-------------------------:

### Setup the MQTT connection.  

| ![MQTT Setup](https://github.com/CraigHoffmann/water-meter-reader/blob/master/Images/mqttsetup.jpg?raw=true) | 
:-------------------------:

### Setup the sensor trigger thresholds

It is necessary to setup the **trigger thresholds** for the sensor for the counter to work properly.  The best way to do this is to turn on a water tap full so you get a resonableflow rate then refresh the sensor setup page.  You should see the sensor signal rise and fall as the meter dial rotates as shown below.  Set the thresholds a bit below the maximum and a bit above the minimum as can be seen below.

| ![Setup Trigger Thresholds](https://github.com/CraigHoffmann/water-meter-reader/blob/master/Images/setupchart.jpg?raw=true) |
:-------------------------:

Next set the **Counts per Liter**.  This is the number of times the needle rotates for 1 Liter of water use.  In my case the dial rotates once per liter so the value is set to 1.  

Set the **Meter Reading (kL)** to match the current water meter reading and **click the update button**.  All settings will be saved to non-volatile memory on the Wemos.

**Note:** the counter will not start "counting" until the kLiters reading is set to a value greater than zero.  The kLiteres is set as "reatined" in MQTT so this is how the value is maintained through reset or power fail. 



## Home Assistant

Home assistant configuration is pretty straight forward, just add an MQTT sensor to the configuration.yaml file as follows: (this assumes you already have MQTT setup and working in home assistant)

```YAML
sensor:
  # Wemos Water Meter kLiters Reading
  - platform: mqtt
    state_topic: "sensor/water-meter/kLiters"
    name: "Meter Reading"
    unit_of_measurement: "kLiters"

  # Wemos Water Meter Flowrate Reading
  - platform: mqtt
    state_topic: "sensor/water-meter/FlowRate"
    name: "Flowrate"
    unit_of_measurement: "L/min"
 ```

