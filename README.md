Airthing MQTT Bridge via an ESP32
==========

* Author: Stephen Beechen 
* Copyright (C) 2019 Stephen Beechen.
* Released under the MIT license.

 An ESP32 arduino sketch that searches for a compatible Airthings device and publishes the radon level, temperature, and humidity to an MQTT server.

 The sketch was created with the intention to allow Airthings devices to cheaply integrate with Home Assistant.

How to Use
----------
* Set up your Airthings following the manufacter's instructions.
* Install the [PubSubClient library](https://pubsubclient.knolleary.net/).
* Set your WiFi credentials in the sketch.
* Set your MQTT server/credentials in the sketch.
* Update the published topics in the sketch, if desired.
* Flash to any ESP32 board.  NOTE: The Bluetooth library is HUGE, so in the Arduino IDE you might need to change "Tools" -> Partition Scheme" to something like "No OTA" to create enough space for the compiled sketch.
* Watch the Serial output to make sure it works.

Usage Details
---------------------
* The Airthings only reports the 24 hour and lifetime average radon concentrations over Bluetooth.
* The library runs once an hour to take a reading and deep sleeps in between, so feasibly this could run on a battery for a very long time.
* The library will attempt to find any airthings device to read from, picking the first it finds.  The Airthings BLE API is unauthenticated so no device configuration or pairing is necessary on the Airthings.
* The library will not interfere with your Airthings' normal upload to a phone or the cloud.
* If it fails to read, it will attempt again after 30 seconds.
* I only have an Airthings Wave to test this with, though presumably it would also work with the Wave Plus.
* The ESP32's bluetooth stack is a little unstable IMHO so expect this to hang for a few minutes, restart prematurely, and report errors often.


License
-------

MIT License

Copyright (c) 2019 Stephen Beechen

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
