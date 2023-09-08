/*

  DMX Sniffer

  This sketch allows you to read and get detailed metadata DMX from a DMX
  controller using a standard DMX shield, such SparkFun ESP32 Thing Plus
  DMX to LED Shield. This sketch was made for the Arduino framework!

  Created 10 September 2021
  By Mitch Weisbrod

  https://github.com/someweisguy/esp_dmx

*/
#include <Arduino.h>
#include <esp_dmx.h>
#include <dmx/sniffer.h>

/* First, lets define the hardware pins that we are using with our ESP32. We
  need to define which pin is transmitting data and which pin is receiving data.
  DMX circuits also often need to be told when we are transmitting and when we
  are receiving data. We can do this by defining an enable pin. Since we are
  using the sniffer, we also need to define which pin to use to sniff DMX
  frames. It is best to use the lowest number pin as possible. The lowest number
  pin available on the Adafruit ESP32 Feather is 4, so lets use that. */
int transmitPin = 17;
int receivePin = 16;
int enablePin = 21;
int snifferPin = 4;
/* Make sure to double-check that these pins are compatible with your ESP32!
  Some ESP32s, such as the ESP32-WROVER series, do not allow you to read or
  write data on pins 16 or 17, so it's always good to read the manuals. */

/* Next, lets decide which DMX port to use. The ESP32 has either 2 or 3 ports.
  Port 0 is typically used to transmit serial data back to your Serial Monitor,
  so we shouldn't use that port. Lets use port 1! */
dmx_port_t dmxPort = 1;

/* Now we want somewhere to store our DMX data. Since a single packet of DMX
  data can be up to 513 bytes long, we want our array to be at least that long.
  This library knows that the max DMX packet size is 513, so we can fill in the
  array size with `DMX_PACKET_SIZE`. */
byte data[DMX_PACKET_SIZE];

/* The last two variables will allow us to know if DMX has been connected and
  also to update our packet and print to the Serial Monitor at a regular
  interval. */
bool dmxIsConnected = false;
unsigned long lastUpdate = millis();

void setup() {
  /* Start the serial connection back to the computer so that we can log
    messages to the Serial Monitor. Lets set the baud rate to 115200. */
  Serial.begin(115200);

  /* Now we will install the DMX driver! We'll tell it which DMX port to use, 
    what device configure to use, and which interrupt priority it should have. 
    If you aren't sure which configuration or interrupt priority to use, you can
    use the macros `DMX_CONFIG_DEFAULT` and `DMX_INTR_FLAGS_DEFAULT` to set the
    configuration and interrupt to their default settings. */
  dmx_config_t config = DMX_CONFIG_DEFAULT;
  dmx_driver_install(dmxPort, &config, DMX_INTR_FLAGS_DEFAULT);

  /* Now set the DMX hardware pins to the pins that we want to use and setup
    will be complete! */
  dmx_set_pin(dmxPort, transmitPin, receivePin, enablePin);

  /* In this example, we are using the DMX sniffer. The sniffer uses an
    interrupt service routine (ISR) to create metadata about the DMX we
    receive. We'll install the ESP32 GPIO ISR using the macro
    `DMX_DEFAULT_SNIFFER_INTR_FLAGS`. Then we can enable the sniffer on the 
    hardware pin that we specified. */
  gpio_install_isr_service(DMX_DEFAULT_SNIFFER_INTR_FLAGS);
  dmx_sniffer_enable(dmxPort, snifferPin);
}

void loop() {
  /* We need a place to store metadata about the DMX packet we receive. We
    will use a dmx_metadata_t to store that packet metadata. */
  dmx_metadata_t metadata;

  /* And now we wait! The DMX standard defines the amount of time until DMX
    officially times out. That amount of time is converted into ESP32 clock
    ticks using the constant `DMX_TIMEOUT_TICK`. If it takes longer than that
    amount of time to receive data, this if statement will evaluate to false. */
  if (dmx_sniffer_get_data(dmxPort, &metadata, DMX_TIMEOUT_TICK)) {
    /* If this code gets called, it means we've received DMX metadata! */

    /* Get the current time since boot in milliseconds so that we can find out
      how long it has been since we last updated data and printed to the Serial
      Monitor. */
    unsigned long now = millis();

    /* If this is the first DMX data we've received, lets log it! */
    if (!dmxIsConnected) {
      Serial.println("DMX connected!");
      dmxIsConnected = true;
    }

    if (now - lastUpdate >= 1000) {
      /* Print the received DMX break length and mark-after-break length. */
      Serial.printf("Break: %ius, MAB: %ius\n", metadata.break_len,
                    metadata.mab_len);
      lastUpdate = now;
    }
  } else if (dmxIsConnected) {
    /* If DMX times out after having been connected, it likely means that the
      DMX cable was unplugged. When that happens in this example sketch, we'll
      uninstall the DMX driver. */
    Serial.println("DMX was disconnected.");
    dmx_driver_delete(dmxPort);

    /* Stop the program. */
    while (true) yield();
  }
}
