# Set up Still

Still targets the ESP32-32E CYD with an ILI9341-compatible 2.8-inch display, XPT2046 resistive touch, 4 MB flash, and an onboard speaker amplifier. The interface uses 320×240 landscape orientation. Match the pinout before using another CYD variant.

| Part | GPIO |
| --- | --- |
| Display (HSPI) | MOSI 13, MISO 12, SCK 14, CS 15, DC 2, backlight 21 |
| Touch | MOSI 32, MISO 39, clock 25, CS 33, IRQ 36 |
| microSD | MOSI 23, MISO 19, SCK 18, CS 5 |
| Onboard speaker | DAC2 on GPIO26. Amplifier enable GPIO4, active low on the development board |

Plug a compatible passive speaker into the board's **Speaker** connector. For the audio library, extract the [SD audio pack](Still-SD-audio-v1.0.0.zip) and copy its `STILL` folder to the root of a FAT32 microSD card. The four files are mono unsigned 8-bit PCM at 16 kHz without WAV headers. The firmware reads the filenames shown in the pack.

## Build from source

Use Arduino CLI with Espressif's **ESP32 core 2.0.14** and **TFT_eSPI 2.5.43**. The sketch's `build_opt.h` supplies the display configuration.

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 Still
arduino-cli upload --fqbn esp32:esp32:esp32 --port YOUR_PORT Still
```

On macOS, Arduino CLI is also bundled in Arduino IDE. Replace `YOUR_PORT` with the correct serial port. The ESP32 Dev Module defaults use the 4 MB partition layout used for this build.

The repository includes generated artwork and font headers. To regenerate after design edits, install Pillow and run, in order:

```sh
python3 Design/build.py
python3 Design/pack_flash.py
```

## Install the ready-built firmware

Extract [Still-firmware-v1.0.0.zip](Still-firmware-v1.0.0.zip). With esptool installed, run this command in the extracted folder:

```sh
python -m esptool --chip esp32 --port YOUR_PORT --baud 115200 \
  write-flash --flash-mode dio --flash-freq 40m --flash-size 4MB \
  0x1000 Still.ino.bootloader.bin \
  0x8000 Still.ino.partitions.bin \
  0xe000 boot_app0.bin \
  0x10000 Still.ino.bin
```

For an existing Still installation on the same partition layout, write only `Still.ino.bin` at `0x10000` to keep your settings and touch calibration.

## Start a session

Follow the touch calibration prompts on first boot. Select a scene, then use **Mixer** for individual levels and **Settings** for master level, a sleep timer, or saving **My mix**.

If the audio library fails to open, Still keeps the interface visible and shows an SD status. Seat the card and tap **Retry SD**. If the screen reports missing audio, check that `STILL` is at the root of the card and contains all four files. For diagnosis, open the serial monitor at 115200 baud.
