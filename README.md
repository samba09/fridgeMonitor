# Fridge Monitor

*[Deutsche Version](README.de.md)*

ESP32 with two DS18B20 temperature probes and a 4-digit 7-segment display (TM1637). No soldering required.
Parts and wiring: [partlist.md](partlist.md) (German). Illustrated build guide: [guide.html](guide.html) (English), [anleitung.html](anleitung.html) (German).

## What the firmware does

- **Display:** shows the two probes alternately every 3 s, in whole degrees (`  4°`, `-18°`). `----` means the probe is not delivering readings.
- **Own WiFi hotspot for setup:** network **`Kuehlschrank`**, default password **`kuehlschrank`** (change it on the web page). After joining, the page usually opens by itself; otherwise open **http://192.168.4.1** in a browser.
- **Web page:** current temperatures and a 24-hour history per probe (one value per minute) with min, max and average. Also probe names, display brightness, swapping probes, WiFi and firmware update.
- **Home WiFi (optional):** can be entered on the web page. The page is then reachable on the home network (`http://kuehlschrank.local` or the IP address shown on the display), and the clock is set from the internet. Without internet, the ESP takes the time from the phone as soon as the page is opened.
- **Hotspot only for setup (default):** once the home WiFi has been connected for a minute and no phone is on the hotspot, the hotspot switches off. If the home WiFi is gone for more than 2 minutes, the hotspot comes back on by itself. Can be switched to “always on” on the web page.
- **Probe assignment:** new probes are detected automatically. A probe below −5 °C is named “Gefrierfach” (freezer), otherwise “Kühlschrank” (fridge). Names can be changed on the web page.
- **Short press on BOOT:** the display shows the IP address (`AP` = hotspot address, `IP` = address on the home WiFi).
- **Factory reset:** hold BOOT for 10 s (the display counts down `rES7` … `rES1`), or use the button on the web page. Clears home WiFi, hotspot password, probe names and brightness.
- **Limitation:** the history is kept in RAM only and is lost after a power cut.

The web page and the display texts are in German.

## Flashing (once, via USB)

Ready-made firmware is available under [Releases](https://github.com/samba09/fridgeMonitor/releases/latest): `kuehlschrank-full.bin` for the first flash, `kuehlschrank-update.bin` for updates via the web page. If you build it yourself, both files are in `firmware/` after `pio run`.

### Option A: in the browser (Windows, Chrome or Edge, nothing to install)
1. Connect the ESP32 to the PC via USB-C.
2. Open https://espressif.github.io/esptool-js/.
3. Set **Baudrate** to 460800, click **Connect** and pick the ESP32's COM port.
   - If no port shows up, the USB driver is missing: CP210x from Silicon Labs (or CH340 from WCH, depending on the board).
   - If it hangs on “Connecting…”: hold the **BOOT** button until it connects.
4. Enter `0x0` as **Flash Address** and select `kuehlschrank-full.bin`.
5. Click **Program**, wait, then press **EN/RST** or unplug USB briefly.

### Option B: with PlatformIO
```bash
pio run -t upload        # build and flash
pio device monitor       # serial output (115200 baud)
```
On WSL2 the USB port has to be passed through first. In PowerShell as admin:
```powershell
usbipd list
usbipd bind --busid <BUSID>
usbipd attach --wsl --busid <BUSID>
```

## Later updates (no USB)
1. `pio run` produces `firmware/kuehlschrank-update.bin` (or download it from Releases).
2. Open the web page, go to **Info & Update**, choose the file and click **Firmware aktualisieren**.

## Building
```bash
pipx install platformio
pio run
```
Output:
- `firmware/kuehlschrank-full.bin`: complete image for the first flash, address `0x0`.
- `firmware/kuehlschrank-update.bin`: app only, for the web update.

## Pins
| Function | GPIO | Label on the ELEGOO board |
|---|---|---|
| DS18B20 data | 4 | D4 |
| DS18B20 power (VCC) | 16 | RX2 |
| DS18B20 GND | – | GND (right) |
| TM1637 DIO | 23 | D23 |
| TM1637 CLK | 18 | D18 |
| TM1637 VCC | – | 3V3 |
| TM1637 GND | – | GND (left) |

The display runs on 3.3 V so that its pull-ups never put 5 V on the ESP32 pins. Since the board has only one 3V3 pin, the probes are powered from GPIO16.
