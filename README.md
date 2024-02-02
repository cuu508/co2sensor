# DIY CO2 Sensor

This is a hobby and learning project.

Parts list:

* [SenseAir Sunrise 006-0-0008 CO2 Sensor](https://senseair.com/product/sunrise/) (€62 from DigiKey)
* [ESP32C3 SuperMini dev board](https://www.aliexpress.com/w/wholesale-esp32c3-supermini.html) (€3 from AliExpress)
* [Afafruit eInk Friend](https://www.adafruit.com/product/4224) ($8.50 from Adafruit)
* [4.2" Waveshare eInk display](https://www.waveshare.com/product/displays/e-paper/epaper-2/4.2inch-e-paper.htm) (€33 from AliExpress)
* [TPL5110 timer breakout board](https://www.adafruit.com/product/3435) (€6 from AliExpress)
* Two resistors (220K and 1M) for measuring battery voltage
* A 3xAA battery holder (€ from AliExpress)
* A custom PCB to mount the above parts on

## Sensor

I experimented with other sensors, but ultimately picked SenseAir Sunrise because it
has low standby power usage, has no warm-up time, and tolerates unregulated input
current. It has good documentation too:

* [Product Specification](https://rmtplusstoragesenseair.blob.core.windows.net/docs/Dev/publicerat/PSP12440.pdf)
* [Handling manual](https://rmtplusstoragesenseair.blob.core.windows.net/docs/Dev/publicerat/ANO4947.pdf)
* [Customer Integration Guidelines](https://rmtplusstoragesenseair.blob.core.windows.net/docs/Market/publicerat/TDE7318.pdf) – includes sample schematics, power consumption stats, description of single/continuous measurement modes, description of calibration options
* [I2C Protocol Docs](https://rmtplusstoragesenseair.blob.core.windows.net/docs/Dev/publicerat/TDE5531.pdf)

I found [an Arduino library on Github](https://github.com/Niclas73/Sunrise-master) that implements I2C communication with Sunrise,
and got it to work with a couple small tweaks. [My fork is here](https://github.com/cuu508/sunrise).

## Display

I bought the display on AliExpress, and am not entirely sure
precisely what model I got, or what IC is inside. Adafruit's [EPD library](https://github.com/adafruit/Adafruit_EPD)
comes with different display drivers. I tried every driver until I found one that seemed
to work.

Black/white displays are a better choice than tri-color displays, as they take
significantly less time to refresh.

## TPL5110

TPL5110 is a timer and power management chip. It turns on power every 5 minutes
(the time period can be tweaked), and cuts power when the ESP32 sends an "I'm done"
signal over GPIO. I am using it as a brute-force way of making sure there is no
power drain between CO2 measurements (aside from the always-on power that the
Sunrise CO2 sensor needs).

## PCB

![PCB](/docs/pcb/pcb.png?raw=true "PCB")

I designed the PCB in EasyEDA. There is a jumper on the board to switch
between USB and battery power.

* [Schematic as PDF](/docs/pcb/schematic.pdf)
* [Schematic in EasyEDA export format](/docs/pcb/easyeda_schematic.json)
* [Board layout in EasyEDA export format](/docs/pcb/easyeda_pcb.json)
* [Gerber files](/docs/pcb/gerber.zip)

## Case

![Case](/docs/case/case_screenshot.png?raw=true "Case")

I designed the case in Fusion 360. The case consists of three parts: the front
part, a piece that the PCB and screen mounts on, and a backplate. The case
snaps together with no screws. There is no transparent protective layer for the
screen.

* [Case model .f3d file](/docs/case/co2sensor_case.f3d)

## Code

The Arduino sketch:

* reads sensor state from ESP32 flash
* reads battery voltage from ADC GPIO pin
* Measures CO2 level
* Writes the measurement to an append-only file on flash filesystem
* saves sensor state to flash
* displays CO2 measurement, battery level and measurement history graph on the
  eInk display
* sends a signal to TPL5110 to cut power

Here's how the UI currently looks:

![UI](/docs/display.jpg?raw=true "UI")
