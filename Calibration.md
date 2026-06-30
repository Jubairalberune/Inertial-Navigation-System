# INS – Technical Notes

## Hardware
- MCU | IMU(0x68,AD0→GND) | BARO(0x76/77) | MAGNETO(0x0D) | GPS 
- Pins: SDA=21 SCL=22 GPS_RX=16 GPS_TX=17 PIX_RX=32 PIX_TX=33
- LEDs: GPIO2=drift GPIO4=GPS GPIO5=static BUZZER=GPIO26
- 47µF cap on IMU VCC/GND (reduces power noise corrupt reads)
- NEO-6M VCC→5V/VIN not 3.3V

## EKF Tuning (v16.0 best values)
ekf_Q[0-2] = 1e-9 pos: frozen
ekf_Q[3-5] = 5e-7 vel: tight
ekf_Q[6-8] = 1e-5 att
ekf_Q[9-11]= 1e-9 accel bias
ekf_Q[12-14]=1e-10 gyro bias
ZUPT R = {0.0001, 0.0001, 0.0005}
ZARU R = {0.0008, 0.0008, 0.0004}
MAG R = 0.01 (yaw measurement)
GPS VEL R = 0.5 * HDOP²
GPS COURSE R = 0.05
CF_ALPHA = 0.98

text

## Sensor Fusion Logic
- **Magnetometer** → yaw correction when accel < 0.4g and gyro < 0.15 rad/s
- **GPS course** → yaw correction when speed > 1 m/s
- **GPS velocity** → velocity correction when fix and speed > 0.5 m/s
- **Barometer** → altitude correction always
- **ZUPT** → velocity correction when stationary (variance < 0.003)
- **ZARU** → gyro bias correction when stationary

## Web Dashboard Routes
- `/` → HTML page (chunked 512B streaming)
- `/status` → running/calDone/uptime
- `/caldata` → calibration progress
- `/env` → baro alt, temp, pressure
- `/data` → JSON with EKF state, covariance, innovations, gains, ZUPT diagnostics
- `/start` → start INS with origin lat/lon/alt
- `/reset` → reset system
- `/bump_start` / `/bump_return` → bump test
- `/gps_align` → realign GPS offset
- `/cal_accel_*` → 6‑position accel cal
- `/cal_mag_*` → compass N/S/E/W cal
- `/cal_vel_*` → velocity cal
- `/cal_baro_*` → barometer cal
- `/cal_gps_sync` → GPS sync
- `/cal_status` → all cal results

## NMEA Output
- GPGGA + GPRMC + GPVTG at 5Hz on SerialPix (GPIO33 TX)
- fix=1 when GPS aided, fix=6 when dead‑reckoning/jammed
- ArduPilot params: GPS_TYPE=1 SERIAL_BAUD=38400

## WiFi
- SSID: "YOUR WIFI SSID" Pass: "YOUR WIFI PASSWORD"
- AP IP: 192.168.4.1 ( Default gateway )