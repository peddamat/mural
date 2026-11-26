# Mural v2 on HW-649 (UNO+WiFi R3) + Arduino CNC Shield V3.0

This guide explains how to build, wire, flash, and run **Mural v2** using:

- **HW-649** (`UNO+WiFi R3 ATmega328P + ESP8266 32Mb/4MB CH340G`)
- **Arduino CNC Shield V3.0** with two **A4988** stepper drivers
- Standard Mural v2 hardware (2× NEMA 17 pancake steppers, MG90S servo, optional 0.91" I2C OLED display)
- **No SD Card** — drawing commands stream live from your web browser over WiFi!

---

## How the Dual-Chip Architecture Works

The HW-649 puts both an **ATmega328P** (Arduino Uno) and an **ESP8266** (WiFi + 4 MB flash) on a single Uno-shaped PCB connected via an internal hardware serial bus (controlled by the onboard 8-way DIP switch):

1. **ESP8266 (`firmware/mural_esp8266/`)**:
   - Hosts WiFi / hotspot (`Mural`), mDNS (`http://mural.local`), and serves the React Web UI from LittleFS.
   - Runs the full 64-bit Mural v2 kinematic model (belt angles, torque equilibrium solver, belt stretch compensation, and 1 mm linear interpolation).
   - Buffers commands streamed live from the web browser over WiFi (`POST /stream`, 64-command ring buffer with sequence numbers, backpressure, automatic retries, pause/resume, and stop).
   - Sends compact, framed step-space & servo commands (`57600` baud) to the ATmega328P.

2. **ATmega328P (`firmware/mural_uno/`)**:
   - Plugs directly into the **CNC Shield V3.0**.
   - Runs real-time `AccelStepper` pulse generation with a 10-slot local FIFO motion/pen queue so WiFi bursts never stutter the motors.
   - Drives the **MG90S pen servo** smoothly without blocking stepper pulses.
   - Drives the optional **0.91" SSD1306 I2C OLED** on `A4/A5` using a zero-framebuffer driver (auto-detected; works fine with or without an OLED connected).

---

## 1. Hardware Wiring & Jumpers

### CNC Shield V3.0 Pin Mapping

| Component | CNC Shield V3.0 Header | ATmega328P Pin | Notes |
|---|---|---|---|
| **Left Stepper Motor** | **X** motor slot | STEP=`D2`, DIR=`D5` | Left motor direction is inverted in software (`INVERT_LEFT_DIR = true`) |
| **Right Stepper Motor** | **Y** motor slot | STEP=`D3`, DIR=`D6` | `INVERT_RIGHT_DIR = false` |
| **Stepper Driver Enable** | Onboard shared EN | `D8` (Active LOW) | Held LOW on boot so carriage stays suspended |
| **MG90S Pen Servo** | **`Z+` (or `Z-`) Endstop** + `5V` + `GND` | Signal=`D11` | `Z+` silkscreen header connects to Arduino `D11` |
| **0.91" I2C OLED** *(optional)* | `SDA` + `SCL` + `5V` + `GND` | `SDA=A4`, `SCL=A5` | Address `0x3C`, 128×32 SSD1306. Auto-detected on boot |

> **Servo Power Tip:** If the MG90S servo causes the board to brown out or reset when moving, power the servo's red `5V` wire from an external 5V buck converter (sharing `GND` with the CNC Shield).

### Microstepping Jumpers (CRITICAL!)

Mural v2 expects **1/8 microstepping** (`1600` steps/revolution for standard 1.8° NEMA 17 steppers).

Under **both** the **X** and **Y** A4988 driver sockets on the CNC Shield V3.0, install jumpers as follows:

| Pin | Jumper | State |
|---|---|---|
| **M0 / MS1** | **Installed** | HIGH |
| **M1 / MS2** | **Installed** | HIGH |
| **M2 / MS3** | **Removed** | LOW |

*(If you prefer 1/16 microstepping with all 3 jumpers installed, change `MICROSTEPS` from `8` to `16` in `firmware/mural_esp8266/config.h`.)*

### A4988 Current Limit (Vref)

The BOM NEMA 17 pancake steppers are rated for **1.0 A**. Before attaching the motors, adjust the small trim pot on each A4988 driver using a multimeter (probe metal screwdriver on pot vs `GND`):

$$V_{\text{ref}} = I_{\text{max}} \times 8 \times R_{\text{sense}}$$

- For common green/red A4988 boards with **`R100` (0.10 Ω)** sense resistors: set **`Vref ≈ 0.56 V – 0.72 V`** (0.7–0.9 A max, runs cool and strong).
- For **`R050` (0.05 Ω)** sense resistors: set **`Vref ≈ 0.28 V – 0.36 V`**.
- For **`R068` (0.068 Ω)** sense resistors: set **`Vref ≈ 0.38 V – 0.49 V`**.

### Power Supply Warning

> **IMPORTANT:** Do **NOT** connect a 20 V USB-C PD trigger directly to the HW-649 barrel jack or `VIN` pin — the HW-649's onboard voltage regulator is rated for **7–9 V max**!
>
> - Connect **12 V – 20 V** motor power **only** to the **CNC Shield V3.0 blue screw terminal** (`12–36V` / `PWR`).
> - Power the HW-649 logic either via its **Micro-USB port (5 V)** or a small 5 V / 7–9 V buck converter stepped down from your main supply.

---

## 2. HW-649 DIP Switch Reference

The HW-649 has an 8-position DIP switch that routes the CH340G USB-Serial chip between the ATmega328P and ESP8266. **Always press the board's Reset button after changing DIP switches.**

| Mode | SW1 | SW2 | SW3 | SW4 | SW5 | SW6 | SW7 | SW8 | Purpose |
|---|---|---|---|---|---|---|---|---|---|
| **1. Flash ATmega328P** | OFF | OFF | **ON** | **ON** | OFF | OFF | OFF | OFF | Upload `mural_uno` via USB |
| **2. Flash ESP8266** | OFF | OFF | OFF | OFF | **ON** | **ON** | **ON** | OFF | Upload `mural_esp8266` & LittleFS via USB |
| **3. Run Mural (Normal)** | **ON** | **ON** | OFF | OFF | OFF | OFF | OFF | OFF | Connect ATmega328P ↔ ESP8266 |

*(Remove the CNC Shield or make sure nothing blocks access to the DIP switches while flashing; you can also flip them gently with a toothpick/tweezers.)*

---

## 3. Building & Flashing

### Option A: PlatformIO (Recommended — builds firmware + web UI automatically)

Make sure Node.js (`npm`) and PlatformIO (`pio`) are installed, and install JS dependencies once:

```bash
cd worker && npm install && cd ..
cd ui && npm install && cd ..
```

1. **Flash ATmega328P**:
   - Set DIP switches **3 & 4 ON** (all others OFF), plug in USB.
   - Run:
     ```bash
     pio run -e hw649_uno -t upload
     ```
2. **Flash ESP8266 firmware + Web UI (LittleFS)**:
   - Set DIP switches **5, 6, 7 ON** (all others OFF), press **Reset**.
   - Run:
     ```bash
     pio run -e hw649_esp8266 -t upload
     pio run -e hw649_esp8266 -t uploadfs
     ```
3. **Switch to Run Mode**:
   - Set DIP switches **1 & 2 ON** (all others OFF) and press **Reset**.

---

### Option B: Arduino IDE 2.x

1. **Install Board Packages & Libraries in Arduino IDE**:
   - Add ESP8266 Board URL in *Preferences → Additional Boards Manager URLs*:
     `https://arduino.esp8266.com/stable/package_esp8266com_index.json`
   - In *Boards Manager*, install **esp8266** (ESP8266 Community) and **Arduino AVR Boards**.
   - In *Library Manager*, install:
     - **AccelStepper** (by Mike McCauley)
     - **WiFiManager** (by tzapu, v2.0.17+)

2. **Flash ATmega328P (`firmware/mural_uno/mural_uno.ino`)**:
   - Set DIP switches **3 & 4 ON** (all others OFF).
   - Open `firmware/mural_uno/mural_uno.ino`.
   - Select Board: **Arduino Uno**, choose the CH340 COM port, click **Upload**.

3. **Flash ESP8266 (`firmware/mural_esp8266/mural_esp8266.ino`)**:
   - Set DIP switches **5, 6, 7 ON** (all others OFF), press **Reset**.
   - Open `firmware/mural_esp8266/mural_esp8266.ino`.
   - Select Board: **Generic ESP8266 Module** with:
     - Flash Size: **`4MB (FS:2MB OTA:~1019KB)`**
     - Upload Speed: **`460800`** (or `115200` if unstable)
   - Click **Upload**.

4. **Upload the Web UI to ESP8266 LittleFS** *(or use Option C below to serve the UI from your PC)*:
   - Build the compressed web assets:
     ```bash
     cd worker && npm install && cd ../ui && npm install && cd ..
     python build.py
     ```
     This populates `firmware/mural_esp8266/data/www/*.gz`.
   - With DIP switches **5, 6, 7 ON**, run the Arduino IDE LittleFS Uploader (`Ctrl+Shift+P` → *Upload LittleFS to Pico/ESP8266/ESP32*).

5. **Switch to Run Mode**:
   - Set DIP switches **1 & 2 ON** (all others OFF) and press **Reset**.

---

### Option C: Run the Web UI from Your Laptop/PC Browser (Skip LittleFS Upload!)

If you prefer not to upload the web UI to ESP8266 LittleFS, the ESP8266 API includes full CORS headers:

```bash
cd worker && npm install && npm run build
cd ../ui && npm install && npm run dev
```

Then open the local URL in your browser with `?host=http://mural.local` (or `?host=http://<HW649_IP>`), for example:

```
http://localhost:5173/?host=http://mural.local
```

---

## 4. First-Time WiFi Setup & Live Browser Streaming

1. Power on your HW-649 with DIP switches **1 & 2 ON**.
2. On first boot, it creates a WiFi hotspot named **`Mural`**. Connect to it from your phone or laptop and configure your home WiFi (or select *Use without WiFi* to stay in hotspot mode at `http://192.168.4.1`).
3. Once connected, open **`http://mural.local`** (or the IP address shown on the OLED display).
4. Walk through the standard Mural v2 workflow:
   - **Set Top Distance** (use the `⚙` Tools menu first to check motor directions and park the servo!)
   - **Choose SVG or Photo → Preview → Accept** (commands stay in your browser — no SD upload wait!)
   - **Retract Belts → Extend to Home → Pen Calibration**
   - **Begin Live Streaming**: keep the browser tab open while Mural draws! You get a live progress bar, buffer meter, **Pause / Resume**, and **Stop** controls, plus automatic Screen Wake Lock and WiFi retry recovery.

---

## 5. Troubleshooting

- **Warning banner `"ATmega328P not responding"`**:
  - Verify DIP switches **1 & 2 are ON** and **3–8 are OFF**, then press the Reset button.
  - Confirm both `mural_uno` and `mural_esp8266` were flashed at the same `LINK_BAUD_RATE` (`57600`).
- **A stepper motor spins the wrong direction**:
  - Either flip the 4-pin stepper motor plug 180° on the CNC Shield (with power OFF!), or toggle `INVERT_LEFT_DIR` / `INVERT_RIGHT_DIR` in `firmware/mural_uno/config.h` and re-flash the Uno.
- **Motors don't move or buzz/skip**:
  - Check that 12–20 V power is connected to the CNC Shield blue terminal.
  - Check A4988 orientation (enable pin `EN` aligns with `EN` on the CNC Shield!) and Vref potentiometer setting.
  - Verify MS1 + MS2 jumpers are installed under both drivers.
