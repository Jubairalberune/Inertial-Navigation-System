# ⚠️ IMPORTANT WARNING – FIRMWARE CODE IS FOR EDUCATIONAL PURPOSES ONLY

[![Not For Production](https://img.shields.io/badge/Status-Educational_Only-red.svg)](https://github.com/Jubairalberune/Inertial-Navigation-System)
[![No Warranty](https://img.shields.io/badge/Warranty-None-red.svg)](https://github.com/Jubairalberune/Inertial-Navigation-System)
[![Use At Your Own Risk](https://img.shields.io/badge/Risk-Use_At_Your_Own_Risk-red.svg)](https://github.com/Jubairalberune/Inertial-Navigation-System)
[![Safety Critical](https://img.shields.io/badge/Safety_Critical-Do_Not_Use-yellow.svg)](https://github.com/Jubairalberune/Inertial-Navigation-System)

---

## ❗ READ THIS BEFORE USING ANY CODE IN THIS DIRECTORY

The firmware files in this directory (`*.ino`, `*.h`, `*.cpp`) are **heavily documented learning resources** – they are **NOT** production‑ready, **NOT** safety‑tested, and **NOT** intended for deployment on real hardware.

---

## 🎯 Purpose of This Code

| Purpose | Status |
|---------|--------|
| **Understanding INS algorithms** | ✅ YES – read and learn |
| **Learning EKF implementation** | ✅ YES – study the mathematics |
| **Understanding sensor fusion** | ✅ YES – see how sensors combine |
| **Research and experimentation** | ✅ YES – test in controlled environments |
| **Building your own INS** | ✅ YES – use as a reference |
| **University projects** | ✅ YES – for academic study |
| **Production deployment** | ❌ NO – do NOT use |
| **Safety‑critical applications** | ❌ NO – do NOT use |
| **Commercial products** | ❌ NO – do NOT use |
| **Drone flight controllers** | ❌ NO – do NOT use |
| **Weapons or military systems** | ❌ NO – do NOT use |

---

## 🚨 Why You Should NOT Use This Code in Production

### 1. It Is Unverified and Untested
- This code has **not** been tested for reliability, safety, or accuracy in real‑world conditions.
- There are **no formal quality assurance** or testing procedures.
- The code may contain **undiscovered bugs** that could cause catastrophic failures.
- **No hardware-in-the-loop (HITL) testing** has been performed.
- **No flight testing** has been conducted for drone applications.

### 2. It Is Intended for Education, Not Engineering
- Every line of code is heavily commented to **explain concepts**, not to be optimised for performance.
- The code prioritises **clarity and readability** over efficiency and robustness.
- Real‑time constraints, error handling, and edge cases may be incomplete.
- **Watchdog timers** are basic, not redundant.
- **Failsafe mechanisms** are minimal or absent.

### 3. Safety‑Critical Applications Are At Risk
- **Drones, missiles, and autonomous vehicles** are safety‑critical systems.
- A failure in the INS could lead to **property damage, injury, or loss of life**.
- **Do NOT** use this code in any system where failure could cause harm.
- This includes:
  - ✈️ Aircraft navigation
  - 🚗 Autonomous vehicles
  - 🤖 Industrial robotics
  - 🏥 Medical devices
  - 💰 Financial systems
  - ⚡ Power grid control

### 4. The Legal Implications
- By using this code in production, you accept **full responsibility** for any consequences.
- The author provides **no warranty, no support, and no liability**.
- You are **solely responsible** for testing, validating, and certifying your system.
- The MIT license does **not** provide any liability protection.

---

## 📋 Comparison – Educational vs Production Code

| Feature | This Code (Educational) | Production Code (Example) |
|---------|------------------------|---------------------------|
| **Comments** | Extensive – explains every line | Minimal – focuses on performance |
| **Error handling** | Basic – shows concepts | Robust – handles all edge cases |
| **Safety features** | None or minimal | Watchdogs, failsafes, redundancy |
| **Testing** | None | Unit tests, integration tests, HITL |
| **Certification** | None | DO‑178C, ISO 26262, etc. |
| **Performance** | Adequate for learning | Optimised for real‑time |
| **Documentation** | Educational | Technical specifications |
| **Maintenance** | Not maintained for production | Actively maintained and updated |
| **Support** | Community only | Professional support available |

---

## 🔧 What You SHOULD Do with This Code

### ✅ Recommended Uses:
1. **Learn the concepts** – Understand how an INS works.
2. **Study the EKF** – See a real implementation of a 15‑state EKF.
3. **Experiment** – Test in a controlled lab environment.
4. **Modify and improve** – Contribute back to the project.
5. **Build your own** – Use this as a reference for your own design.
6. **Teach others** – Share the knowledge.

### ❌ What You Should NOT Do:
1. **Fly a drone** – Do not use this for actual flight.
2. **Deploy in vehicles** – Do not use for navigation of real vehicles.
3. **Commercial products** – Do not include in any commercial product.
4. **Military applications** – Do not use in any weapons system.
5. **Safety-critical systems** – Do not use where failure could cause harm.

---

## 🏗️ If You Want to Build a Production INS

### Recommended Path:

1. **Learn from this code** – Understand the concepts thoroughly.
2. **Design your own system** – Based on your specific requirements.
3. **Use proper components** – Select sensors with appropriate specifications.
4. **Add safety features** – Watchdogs, redundancy, fail‑safe modes.
5. **Test extensively** – In simulation and on the bench.
6. **Validate** – Compare against ground truth (GPS, RTK, etc.).
7. **Certify** – If required for your application.
8. **Document** – Full technical documentation.

### Alternative: Use Proven Commercial Solutions

| Solution | Type | Reliability | Cost |
|----------|------|-------------|------|
| **Pixhawk / ArduPilot** | Open‑Source Autopilot | High – battle‑tested | Free |
| **VectorNav** | Commercial INS | Very High – certified | $$$ |
| **SBG Systems** | Commercial INS | Very High – certified | $$$ |
| **NovAtel** | Commercial INS | High – certified | $$$ |
| **MicroStrain** | Commercial INS | High – certified | $$$ |
| **Bosch BNO055** | Consumer IMU | Medium – good for prototyping | $ |

These solutions are **proven, tested, and supported** – use them if you need reliability.

---

## 📝 Understanding the Code – What You Can Learn

Despite the warnings, this code is **an excellent learning resource**. You can study:

| Topic | Where to Find It |
|-------|------------------|
| **15‑state EKF** | `INS_v16.0.ino` – `predictStep()`, `ekf_update()` |
| **ZUPT (Zero Velocity Update)** | `INS_v16.0.ino` – `updateZUPT()` |
| **ZARU (Zero Angular Rate Update)** | `INS_v16.0.ino` – `updateZUPT()` |
| **Complementary Filter** | `INS_v16.0.ino` – `updateComplementaryFilter()` |
| **Magnetometer Yaw Fusion** | `INS_v16.0.ino` – `updateMagnetometer()` |
| **GPS Velocity Fusion** | `INS_v16.0.ino` – `updateGPSVelocity()` |
| **GPS Course Fusion** | `INS_v16.0.ino` – `updateGPSCourse()` |
| **6‑Position Accel Calibration** | `INS_v16.0.ino` – `/cal_accel_pos` endpoint |
| **Compass Hard‑Iron Calibration** | `INS_v16.0.ino` – `/cal_mag_pos` endpoint |
| **Web Dashboard Architecture** | `ins_web_page.h` – full HTML/JS |
| **EKF Diagnostics** | `ins_web_page.h` – EKF panel |
| **NMEA Output** | `INS_v16.0.ino` – `sendNMEA()` |

---

## 🛑 Specific Warnings by Application

### Drones and UAVs
- **DO NOT** fly any drone using this code without extensive testing.
- A single bug could cause a **fly‑away, crash, or injury**.
- Always use a **kill switch** and **manual override**.
- Never fly over people or property.
- This code is **not airworthy**.

### Missiles and Weapons
- **DO NOT** use this code in any weapons system.
- This code is for **educational and research purposes only**.
- The author does not condone or support military applications.
- Use of this code in weapons is a **violation of the project's intent**.

### Autonomous Vehicles
- **DO NOT** use this code for navigation of real vehicles.
- Use **certified systems** with **safety features**.
- Autonomous vehicles require **redundant systems** and **fail‑safe mechanisms**.

### Commercial Products
- **DO NOT** include this code in any commercial product.
- Commercial use requires **proper licensing, testing, and certification**.
- The MIT license does not grant the right to use this code in safety‑critical applications.

---

## 📄 License and Liability

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
MIT License

Copyright (c) 2025 Jubair Al Berune

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

---

**The MIT license does NOT provide any warranty or liability protection.**

---

## 📞 Questions?

| Topic | Contact Method |
|-------|----------------|
| **Understanding the code** | Open a GitHub issue or discussion |
| **Reporting bugs** | Open a GitHub issue |
| **Suggesting improvements** | Open a pull request |
| **Commercial licensing** | Contact the author directly |
| **Production use** | Consult a professional engineer |
| **Safety questions** | Consult a certified safety engineer |

---

## ✅ Summary

| Do | Don't |
|----|-------|
| ✅ Read the code to learn | ❌ Use it in production |
| ✅ Understand the algorithms | ❌ Trust it for safety‑critical systems |
| ✅ Experiment in the lab | ❌ Fly a drone with it |
| ✅ Contribute improvements | ❌ Sell it as a product |
| ✅ Use it as a reference | ❌ Use it in weapons |
| ✅ Share your knowledge | ❌ Blame the author for failures |

---

## 🔗 Related Resources

If you want to build a production‑ready INS, consider these resources:

- [Pixhawk / ArduPilot](https://ardupilot.org/) – Open‑source autopilot
- [VectorNav](https://www.vectornav.com/) – Commercial INS systems
- [SBG Systems](https://www.sbg-systems.com/) – Commercial INS systems
- [NovAtel](https://novatel.com/) – Commercial GNSS/INS
- [MicroStrain](https://www.microstrain.com/) – Commercial IMU/INS
- [Bosch BNO055](https://www.bosch-sensortec.com/products/smart-sensors/bno055/) – Consumer IMU

---

**Remember: This code is a learning resource, not a product. Use it wisely, and always prioritise safety.**

🚀 **Learn, experiment, and build – but do it safely!**
