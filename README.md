# smart-dc-load-controller
'ESP32-based DC load controller with overcurrent protection
# Smart DC Load Controller with Overcurrent Protection

An ESP32-based "smart circuit breaker" that monitors DC current in real time and automatically cuts power to a load when it exceeds a safe threshold — with a live OLED display and a web dashboard for monitoring and control.

Built as a hands-on embedded systems project using an ESP32, a current sensor, a relay module, and an OLED display. No mains AC or transformer required — runs entirely on a low-voltage DC supply.

## Features

- Real-time DC current monitoring via ACS712-5A
- Automatic overcurrent protection with a trip → cooldown → retry → lockout state machine
- Live readings on a 0.96" OLED display
- Web dashboard for remote monitoring and relay control
- Runtime-adjustable trip threshold — no re-uploading code needed (set it via Serial Monitor or the web dashboard)
- Manual reset after lockout

## Hardware Used

| Component | Purpose |
|---|---|
| ESP32 Dev Kit | Main controller, runs the state machine and web server |
| ACS712-5A | Measures DC current through the load |
| 4-Channel 5V Relay Module | Switches the load on/off; channel 1 is the protected channel |
| 0.96" I2C OLED Display | Shows live current, state, and trip count |
| Breadboard + jumper wires | Prototyping |
| 12V DC PSU | Powers the test load |

## Wiring

**ESP32 control wiring (low voltage):**

| Component | Pin | ESP32 |
|---|---|---|
| OLED | VCC / GND | 3.3V / GND |
| OLED | SDA / SCL | GPIO21 / GPIO22 |
| Relay | VCC / GND | 5V / GND |
| Relay | IN1–IN4 | GPIO32 / GPIO33 / GPIO25 / GPIO26 |
| ACS712 | VCC / GND | 5V / GND |
| ACS712 | OUT | GPIO34 |

**DC load loop (the actual power path — separate from the wiring above):**

```
PSU(+) → ACS712 IP+ → ACS712 IP- → Load(+) → Load(-) → Relay COM → Relay NO → PSU(-)
```

> Note: the relay's **NO** (normally open) terminal must be used, not NC — NO stays open by default and only closes when the relay is energized, which is what the code expects.
>
> Most 4-channel relay boards have a **JD-VCC jumper** that separates the logic side from the relay coil power. If it's missing, the LED indicator lights up but the coil never actually energizes — bridge it to VCC if your relay isn't physically clicking.

![Block diagram](smart_dc_load_controller_diagram.png)

## How It Works

The protection logic is a simple state machine:

- **NORMAL** — load runs, current is checked every loop
- **TRIPPED** — current exceeded the threshold for several consecutive readings (debounced to avoid false trips from noise), relay is forced off immediately
- **COOLDOWN** — waits a few seconds before attempting to restore power
- **LOCKOUT** — if it trips again shortly after a retry, auto-retries stop and it waits for a manual reset from the web dashboard or Serial Monitor

## Bugs I Hit and Fixed

This ended up being a better lesson in embedded debugging than in wiring:

- **Current readings looked like random noise, completely ignoring the real load.** I was using `EmonLib`'s `calcIrms()`, which is built for AC — it treats the average signal level as "DC offset" and subtracts it out. Since my load was steady DC, that's exactly the value I needed, and the library was throwing it away. Fixed by reading the ADC directly and converting to amps using the sensor's known sensitivity (185mV/A for the 5A version), with an auto-zero calibration step at startup.
- **Relay showed "ON" but the load never ran.** Turned out to be the JD-VCC jumper — the opto-isolator LED lights up off the logic-side VCC, but the relay coil itself needs a separate power connection that the jumper provides.
- **Load ran constantly and only stopped when the relay activated.** I had wired into the **NC** terminal instead of **NO** — backwards from what the code expects.

## Setup

1. Install the Arduino IDE and add ESP32 board support (Boards Manager → search "esp32").
2. Install these libraries via Library Manager: `Adafruit GFX Library`, `Adafruit SSD1306`.
3. Open the `.ino` file, set your `WIFI_SSID` and `WIFI_PASSWORD`.
4. Wire everything as described above. **Keep the load loop disconnected for the first couple seconds after upload** — that's when the sensor auto-calibrates its zero-current baseline.
5. Upload, then open Serial Monitor (115200 baud) to find the ESP32's IP address once it connects to WiFi.
6. Open that IP in a browser for the live dashboard, or use Serial Monitor commands:
   - Type a number (e.g. `1.5`) + Enter to set the trip current
   - Type `zero` + Enter to recalibrate the zero-current baseline (with no load connected)
   - Type `reset` + Enter to clear a lockout

## Demo

Tested with an LED fog light as the load — trip threshold set to 0.15A to reliably demonstrate the protection cycle: trip → cooldown → auto-retry → lockout after repeated faults.

## License

Free to use, modify, and build on for your own projects.
