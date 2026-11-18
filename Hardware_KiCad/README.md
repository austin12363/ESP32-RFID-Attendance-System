# ESP32 Standalone RFID Attendance System - KiCad Hardware Project (v2.0)

This directory contains the complete, production-ready **KiCad 10 / 9 / 8** schematic files, circuit design documentation, bill of materials (BOM), and assembly instructions for the **ESP32 Standalone RFID Attendance System**.

---

## Project Files

| File | Description |
| :--- | :--- |
| [`ESP32_Attendance_System.kicad_pro`](./ESP32_Attendance_System.kicad_pro) | Native KiCad Project File |
| [`ESP32_Attendance_System.kicad_sch`](./ESP32_Attendance_System.kicad_sch) | Complete KiCad Schematic (Self-contained symbols, nets, and designators) |
| [`ESP32_Attendance_System_Schematic_v2.0.pdf`](./ESP32_Attendance_System_Schematic_v2.0.pdf) | High-Resolution Vector Schematic Sheet (v2.0) |
| [`ESP32_Attendance_System.svg`](./ESP32_Attendance_System.svg) | Scalable Vector Graphics (SVG) Schematic Drawing |
| [`ESP32_Attendance_System_Schematic.png`](./ESP32_Attendance_System_Schematic.png) | 300 DPI Raster Schematic Preview |
| [`generate_kicad_project.py`](../generate_kicad_project.py) | Standalone Python builder script to re-generate/export all schematic artifacts |

---

## Power Subsystem Routing (v2.0 — MH-CD42 Integrated Architecture)

The system replaces the legacy dual-board setup (TP4056 + MT3608) with a single **MH-CD42** integrated power module (featuring synchronous 5V 2.1A step-up boost + lithium battery management):

```text
[External 5V USB] ───► VIN / GND_IN ───┐
                                       │
[18650 Battery (3.7V)] ──► BAT+ / BAT- ──► [MH-CD42 Integrated Module]
                                              │ (Pass-through + Synchronous Boost)
                                              ├─ VOUT (5.0V) ──► [Main Switch SW1] ──► +5V Rail (ESP32 VIN & LCD VCC)
                                              ├─ GND_OUT ─────────────────────────────► System GND Rail
                                              └─ KEY (Unconnected / Auto-wake)

[ESP32 Internal 3.3V LDO] (Pin 1 3V3) ───────────────────────────────────────────────► +3V3 Rail (RC522 Pin 1 VCC, RGB Pin 2 Anode)
```

### Advantages of MH-CD42 over TP4056 + MT3608:
1. **Single Compact Module:** Replaces two separate PCBs, eliminating interconnect wires and reducing enclosure space by over 50%.
2. **Synchronous Rectification:** Higher efficiency (up to 96%) with lower heat dissipation compared to MT3608 asynchronous diode boost.
3. **Pass-Through Power Path:** The module simultaneously charges the 18650 battery via `VIN` and supplies 5V to `VOUT`.
4. **Isolating Main Switch (`SW1`):** Placed on `VOUT` so toggling `SW1` OFF completely powers down the ESP32 and peripherals while still allowing the 18650 battery to recharge safely via USB.

---

## ESP32 DevKit V1 (30-Pin Standard Footprint) Pin Mapping

### Left Header (Pins 1–15, Top to Bottom)

| Pin # | Silkscreen / Chip GPIO | Function / Net Label | Connected To | Notes |
| :---: | :--- | :--- | :--- | :--- |
| **1** | `3V3` | `+3V3` | RC522 VCC & RGB LED Common Anode | Regulated 3.3V power output (from onboard AMS1117) |
| **2** | `GND` | `GND` | Common Ground | System Ground Return |
| **3** | `D15 / GPIO 15` | *NC* | No Connection | Available GPIO (Strapping: MTDO) |
| **4** | `D2 / GPIO 2` | `STATUS_LED` | Internal Blue LED | Flashes upon successful card scan |
| **5** | `D4 / GPIO 4` | `RFID_RST` | RC522 Reset (`RST`) | Software-driven reset pin |
| **6** | `RX2 / GPIO 16` | *NC* | No Connection | Hardware Serial 2 RX (reserved) |
| **7** | `TX2 / GPIO 17` | *NC* | No Connection | Hardware Serial 2 TX (reserved) |
| **8** | `D5 / GPIO 5` | `RFID_CS` | RC522 `SDA (SS)` | VSPI Chip Select / Slave Select |
| **9** | `D18 / GPIO 18` | `RFID_SCK` | RC522 `SCK` | VSPI Clock |
| **10** | `D19 / GPIO 19` | `RFID_MISO` | RC522 `MISO` | VSPI Master In Slave Out |
| **11** | `D21 / GPIO 21` | `I2C_SDA` | LCD1602 `SDA` | Hardware I2C Serial Data |
| **12** | `RX0 / GPIO 3` | *NC* | USB-to-UART Bridge | Programming / Debug Serial RX |
| **13** | `TX0 / GPIO 1` | *NC* | USB-to-UART Bridge | Programming / Debug Serial TX |
| **14** | `D22 / GPIO 22` | `I2C_SCL` | LCD1602 `SCL` | Hardware I2C Serial Clock |
| **15** | `D23 / GPIO 23` | `RFID_MOSI` | RC522 `MOSI` | VSPI Master Out Slave In |

### Right Header (Pins 16–30, Top to Bottom)

| Pin # | Silkscreen / Chip GPIO | Function / Net Label | Connected To | Notes |
| :---: | :--- | :--- | :--- | :--- |
| **16** | `EN` | *NC* | Reset Button | Active-low chip enable / reset |
| **17** | `VP / GPIO 36` | *NC* | No Connection | ADC1_CH0 (Input only) |
| **18** | `VN / GPIO 39` | *NC* | No Connection | ADC1_CH3 (Input only) |
| **19** | `D34 / GPIO 34` | *NC* | No Connection | ADC1_CH6 (Input only) |
| **20** | `D35 / GPIO 35` | *NC* | No Connection | ADC1_CH7 (Input only) |
| **21** | `D32 / GPIO 32` | *NC* | No Connection | ADC1_CH4 / Touch 9 |
| **22** | `D33 / GPIO 33` | *NC* | No Connection | ADC1_CH5 / Touch 8 |
| **23** | `D25 / GPIO 25` | `RGB_RED` | Resistor R1 (220 $\Omega$) $\rightarrow$ Red Cathode | Active LOW (LOW = ON, HIGH = OFF) |
| **24** | `D26 / GPIO 26` | `RGB_GREEN` | Resistor R2 (220 $\Omega$) $\rightarrow$ Green Cathode | Active LOW (LOW = ON, HIGH = OFF) |
| **25** | `D27 / GPIO 27` | `RGB_BLUE` | Resistor R3 (220 $\Omega$) $\rightarrow$ Blue Cathode | Active LOW (LOW = ON, HIGH = OFF) |
| **26** | `D14 / GPIO 14` | `GPIO14_BACKLIGHT` | LCD Pin 5 (`BACKLIGHT (GPIO 14)`) | Hardware PWM Brightness & Sleep Dimming (Firmware line 76: `#define LCD_BACKLIGHT_PWM_PIN 14`) |
| **27** | `D12 / GPIO 12` | *NC* | No Connection | HSPI MISO (Strapping: MTDI) |
| **28** | `D13 / GPIO 13` | *NC* | No Connection | HSPI MOSI |
| **29** | `GND` | `GND` | Common Ground | System Ground Return |
| **30** | `VIN(5V)` | `+5V` | MH-CD42 `VOUT` (via Main Switch `SW1`) | Main 5V Power Input |

---

## LCD1602 I2C Backlight PWM Instructions

On standard PCF8574 I2C backpack modules:
1. Locate the **2-pin header** with the black shunt jumper labeled **`LED`** or **`BL`**.
2. **Remove the black jumper shunt**.
3. Connect ESP32 **Pin 26 (D14 / GPIO 14)** (`GPIO14_BACKLIGHT`) to the **control pin** of this 2-pin header (LCD Pin 5: `BACKLIGHT (GPIO 14)`).
4. This enables full 0–255 software brightness control, auto-dimming warning (10 seconds before timeout), and sleep mode in the firmware without extra hardware components!

---

## RGB LED (Common Anode 5mm) Physical Pinout

Standard through-hole Common Anode 5mm RGB LED (holding with flat edge facing you):
* **Pin 1:** Red Cathode (-) $\rightarrow$ 220 $\Omega$ Resistor R1 $\rightarrow$ ESP32 Pin 23 (`GPIO 25`)
* **Pin 2:** Common Anode (+) (Longest Lead) $\rightarrow$ `+3V3` Power Rail
* **Pin 3:** Green Cathode (-) $\rightarrow$ 220 $\Omega$ Resistor R2 $\rightarrow$ ESP32 Pin 24 (`GPIO 26`)
* **Pin 4:** Blue Cathode (-) $\rightarrow$ 220 $\Omega$ Resistor R3 $\rightarrow$ ESP32 Pin 25 (`GPIO 27`)
