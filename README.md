# QUADRUPED-ROBOT-OLED
Quadruped robot powered by an Arduino Pro Micro (ATmega32U4). Operates on 5V power, utilizing 12 servo motors for inverse kinematics and an animated OLED display face through Software I2C.

##  What it does

* **Inverse Kinematics:** Calculates real-time 3D leg positions ($X, Y, Z$) across 12 servos (3 per leg) for smooth walking, turning, and custom body movements.
* **Animated OLED Face:** Runs an expression state machine on a 0.96 inch OLED display. The eyes pan side-to-side, blink every 3 seconds, and automatically close when the robot sits down.
* **Software I2C Communication:** Since the Pro Micro's hardware I2C pins (Pins 2 & 3) were already being used for servo signal lines, the OLED screen is driven via software I2C on analog pins `A0` (SDA) and `A1` (SCL).
* **50Hz Servo Interrupts:** Uses `FlexiTimer2` to keep servo updates running smoothly in the background without locking up the loop.
* **USB Serial Control:** Responds to string commands sent from the Arduino Serial Monitor at 9600 baud.

---

##  Hardware Setup & Pin Mapping

### Electronics
* **Board:** Arduino Pro Micro (ATmega32U4, 5V / 16MHz)
* **Servos:** 12x SG90 Micro Servos
* **Display:** 0.96" SSD1306 OLED Screen ($128 \times 64$, Yellow/Blue split)
* **Power:** External 5V power supply for the servos (common ground connected to the Arduino).
* **LM2596S Step Down Module** used to step down 12V power supply to 5V if needed

### Pinout Table

| Component | Function | Arduino Pro Micro Pin |
| :--- | :--- | :--- |
| **OLED Display** | **SDA** (Data) | **A0** *(Software I2C)* |
| **OLED Display** | **SCL** (Clock) | **A1** *(Software I2C)* |
| **Leg 0 (Front Left)** | Coxa / Femur / Tibia | Pins `2`, `3`, `4` |
| **Leg 1 (Front Right)**| Coxa / Femur / Tibia | Pins `5`, `6`, `7` |
| **Leg 2 (Back Right)** | Coxa / Femur / Tibia | Pins `8`, `9`, `10` |
| **Leg 3 (Back Left)**  | Coxa / Femur / Tibia | Pins `16`, `14`, `15` |

---

##  Serial Commands

Set your Serial Monitor to **9600 Baud** and **Newline**. Type any of the following commands:

* `w 0 1` – Stand Up (wakes up face display)
* `w 0 0` – Sit Down (closes eyes on display)
* `w 1 x` – Walk Forward x steps
* `w 2 x` – Walk Backward x steps
* `w 3 x` – Turn Right x steps
* `w 4 x` – Turn Left x steps
* `w 5 x` – Hand Shake emote (x times)
* `w 6 x` – Hand Wave emote (x times)
* `w 7 x` – Side-to-Side body sway (x times)
* `w 8 x` – Bounce body emote (x times)
* `w 9 x` – Up and Down stretch (x times)

---

## Libraries

Install these via the Arduino Library Manager:

* `U8g2` (for OLED screen rendering)
* `FlexiTimer2` (for 50Hz timer interrupts)
* `SerialCommand` (for parsing serial input strings)
* `Servo` (standard built-in library)

---

## 3D Printed Parts

The chassis and leg components are based on [Regis Hsu's Spider Robot Design on Thingiverse](https://www.thingiverse.com/thing:1009659).

---

## How to Run

1. **Center Servos:** Before attaching the legs, upload `calibration/servo_90.ino` to set all 12 servos to their $90^\circ$ midpoint.
2. **Assemble:** Mount the leg horns at $90^\circ$ angles relative to the chassis.
3. **Upload Firmware:** Open `Quadruped_Spider.ino`, select **Arduino Leonardo**, and flash the sketch.
4. **Test:** Open Serial Monitor, make sure Newline is enabled, and send `w 0 1` to stand the robot up!

---

## Demo
[Watch the Quadruped Robot Demo Video](https://www.youtube.com/shorts/cDyD5-1qRg4)

---

## Credits

* Original base code by panerqiang@sunfounder.com (2015).
* Bluetooth remote and emote modifications by **RegisHsu** (2015).
* Pro Micro migration, custom movement emotes, and Software I2C OLED face animation developed by **Ryan Hsieh**.
