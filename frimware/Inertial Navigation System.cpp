/**
 * @file INS Documented.ino
 * @brief Full explanatory code for a 15‑state EKF INS on MicroController.
 * 
 * This file is a detailed reference – it explains every part of the system:
 *   - Hardware wiring (MEMS IMU, Barometer, Magnetometer , GPS/GNSS, LEDs, buzzer)
 *   - Raw I2C driver for MEMS IMU (no Adafruit library)
 *   - 6‑position accelerometer calibration (scale + offset per axis)
 *   - Gyro static bias + noise floor measurement (Welford algorithm)
 *   - Magnetometer hard‑iron calibration (N/S/E/W)
 *   - Velocity calibration (walk exactly 1 m)
 *   - Barometer scale calibration (lift 1 m)
 *   - GPS origin sync (offset between phone coordinates and raw GPS)
 *   - Auto‑level calibration (any orientation → rotation matrix)
 *   - ZUPT (Zero Velocity Update) using variance‑based stationary detection
 *   - Complementary filter for roll/pitch (attitude)
 *   - 15‑state EKF: prediction (integrate MEMS IMU) and updates (GPS, baro, ZUPT)
 *   - Full NMEA output (GPGGA, GPRMC, GPVTG) for Pixhawk
 *   - Web server with dashboard (compass, horizon, live map, calibration wizard)
 * 
 * This is a DEMONSTRATION ONLY – not intended for direct upload.
 * See the actual firmware INS 2.ino for a compilable version.
 */

// ===================== INCLUDES & DEFINES =====================
#include <Arduino.h>          // MicroController Arduino core
#include <Wire.h>             // I2C for MEMS IMU, Barometer, compass
#include <WiFi.h>             // Access point mode
#include <WebServer.h>        // HTTP server for dashboard
#include <Adafruit_Barometer.h>  // Barometric pressure / altitude
#include <Magnetometer Compass.h>  // Magnetometer (compass)
#include <TinyGPS++.h>        // NMEA sentence parser
#include <esp_task_wdt.h>     // Watchdog timer (prevents lock‑ups)
#include <math.h>
#include <string.h>

// ------------------------------------------------------------------
//  PIN DEFINITIONS (matching typical hardware)
// ------------------------------------------------------------------
// I2C bus
#define SDA_PIN     21
#define SCL_PIN     22
// GPS/GNSS GPS (UART2)
#define GPS_RX      16          // MicroController RX ← GPS TX
#define GPS_TX      17          // MicroController TX → GPS RX (seldom used)
// NMEA output to Pixhawk (UART1)
#define PIX_RX      32
#define PIX_TX      33
// Status LEDs
#define LED_GPS     4           // ON = GPS fix fused
#define LED_STATIC  5           // ON = ZUPT active (truly still)
#define LED_DRIFT   2           // BLINK = drifting while static
// Buzzer (active low / high depending on module)
#define BUZZER_PIN  26

// EKF state dimension: position (3), velocity (3), attitude (3), biases (6)
#define NS 15

// ------------------------------------------------------------------
//  MEMS IMU RAW I2C DRIVER (no external library)
// ------------------------------------------------------------------
// The Adafruit_MEMS IMU library has issues with MicroController Core 3.x because
// it calls Wire.begin() internally and resets the I2C pins. Therefore
// we implement direct register reads/writes.
#define MPU_ADDR    0x68
#define ACCEL_SC    (9.80665f / 8192.0f)   // ±4g → LSB = 8192 per g
#define GYRO_SC     (0.00106422f)          // ±500 deg/s → LSB = 65.5 per deg/s

/**
 * @brief Write a single register on MEMS IMU.
 * @param r   Register address
 * @param v   Value to write
 * @return    true if successful
 */
bool mpuWR(uint8_t r, uint8_t v) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(r);
    Wire.write(v);
    return Wire.endTransmission() == 0;
}

/**
 * @brief Read multiple registers from MEMS IMU.
 * @param r   Starting register address
 * @param b   Output buffer
 * @param n   Number of bytes to read
 * @return    true if all bytes received
 */
bool mpuRR(uint8_t r, uint8_t* b, uint8_t n) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(r);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom(MPU_ADDR, n);
    if (Wire.available() < n) return false;
    for (uint8_t i = 0; i < n; i++) b[i] = Wire.read();
    return true;
}

/**
 * @brief Initialise MEMS IMU: wake up, set DLPF to 21 Hz, gyro range ±500 dps,
 *        accelerometer range ±4g.
 * @return true if WHO_AM_I matches known values.
 */
bool mpuInit() {
    mpuWR(0x6B, 0x80); delay(150);   // reset
    if (!mpuWR(0x6B, 0x00)) return false; // wake
    delay(100);
    uint8_t w = 0;
    if (!mpuRR(0x75, &w, 1)) return false; // read WHO_AM_I
    if (w != 0x68 && w != 0x70 && w != 0x71 && w != 0x19 && w != 0x98)
        return false;
    mpuWR(0x1A, 0x04);   // DLPF 21 Hz (good for human motion)
    mpuWR(0x1B, 0x08);   // gyro ±500 dps
    mpuWR(0x1C, 0x08);   // accel ±4g
    delay(50);
    return true;
}

/**
 * @brief Raw read of accelerometer and gyroscope (body frame, no corrections).
 * @return true if I2C transaction succeeded.
 */
bool mpuReadRaw(float &ax, float &ay, float &az,
                float &gx, float &gy, float &gz) {
    uint8_t b[6];
    if (!mpuRR(0x3B, b, 6)) return false;   // accel
    ax = (int16_t)(b[0]<<8|b[1]) * ACCEL_SC;
    ay = (int16_t)(b[2]<<8|b[3]) * ACCEL_SC;
    az = (int16_t)(b[4]<<8|b[5]) * ACCEL_SC;
    if (!mpuRR(0x43, b, 6)) return false;   // gyro
    gx = (int16_t)(b[0]<<8|b[1]) * GYRO_SC;
    gy = (int16_t)(b[2]<<8|b[3]) * GYRO_SC;
    gz = (int16_t)(b[4]<<8|b[5]) * GYRO_SC;
    return true;
}

// ------------------------------------------------------------------
//  CALIBRATION PARAMETERS (global, applied in mpuReadLevel)
// ------------------------------------------------------------------
// 6‑position accelerometer calibration: per‑axis scale & offset
float aScX = 1.0f, aScY = 1.0f, aScZ = 1.0f;
float aOfX = 0.0f, aOfY = 0.0f, aOfZ = 0.0f;
// Gyro static bias (from 60‑second calibration or manual)
float gbx = 0, gby = 0, gbz = 0;
// Overall magnitude correction (auto‑level calibration)
float accelScale = 1.0f;
// Velocity scale factors (from 1m walk)
float velScaleN = 1.0f, velScaleE = 1.0f;
// Barometer altitude scale (from 1m lift)
float baroScaleFactor = 1.0f;
// GPS offset (user‑provided origin vs raw GPS coordinates)
double gpsLatOff = 0, gpsLonOff = 0;
bool gpsSyncDone = false;

// Auto‑level rotation matrix: rotates body frame gravity vector to +Z.
// Built during the 60‑second calibration.
float R_b_n[3][3] = {{1,0,0},{0,1,0},{0,0,1}};

// ------------------------------------------------------------------
//  SENSOR READING WITH FULL CALIBRATION PIPELINE
// ------------------------------------------------------------------
static float sm_ax=0, sm_ay=0, sm_az=0;   // exponential smoothing buffers
static float sm_gx=0, sm_gy=0, sm_gz=0;

/**
 * @brief Read MEMS IMU, apply all calibrations, rotate to navigation frame,
 *        and low‑pass filter the result.
 * 
 * Steps:
 *   1. Raw body‑frame reading.
 *   2. Apply per‑axis scale & offset (6‑position accel calibration).
 *   3. Apply overall magnitude correction (auto‑level calibration).
 *   4. Subtract gyro bias.
 *   5. Rotate from body to navigation frame using auto‑level matrix.
 *   6. Exponential smoothing (accel α=0.5, gyro α=0.6).
 * 
 * @return true if successful.
 */
bool mpuReadLevel(float &ax, float &ay, float &az,
                  float &gx, float &gy, float &gz) {
    float rax, ray, raz, rgx, rgy, rgz;
    if (!mpuReadRaw(rax, ray, raz, rgx, rgy, rgz)) return false;

    // body‑frame per‑axis corrections
    rax = rax * aScX - aOfX;
    ray = ray * aScY - aOfY;
    raz = raz * aScZ - aOfZ;
    rax *= accelScale; ray *= accelScale; raz *= accelScale;

    rgx -= gbx; rgy -= gby; rgz -= gbz;

    // rotate to navigation frame (NED)
    ax = R_b_n[0][0]*rax + R_b_n[0][1]*ray + R_b_n[0][2]*raz;
    ay = R_b_n[1][0]*rax + R_b_n[1][1]*ray + R_b_n[1][2]*raz;
    az = R_b_n[2][0]*rax + R_b_n[2][1]*ray + R_b_n[2][2]*raz;
    gx = R_b_n[0][0]*rgx + R_b_n[0][1]*rgy + R_b_n[0][2]*rgz;
    gy = R_b_n[1][0]*rgx + R_b_n[1][1]*rgy + R_b_n[1][2]*rgz;
    gz = R_b_n[2][0]*rgx + R_b_n[2][1]*rgy + R_b_n[2][2]*rgz;

    // low‑pass filter
    sm_gx = 0.6f*sm_gx + 0.4f*gx;   sm_gy = 0.6f*sm_gy + 0.4f*gy;   sm_gz = 0.6f*sm_gz + 0.4f*gz;
    sm_ax = 0.5f*sm_ax + 0.5f*ax;   sm_ay = 0.5f*sm_ay + 0.5f*ay;   sm_az = 0.5f*sm_az + 0.5f*az;
    ax=sm_ax; ay=sm_ay; az=sm_az;
    gx=sm_gx; gy=sm_gy; gz=sm_gz;
    return true;
}

// ------------------------------------------------------------------
//  ZUPT – ZERO VELOCITY UPDATE (stationary detection)
// ------------------------------------------------------------------
#define ZW 32                     // sliding window size
static float zbuf[ZW];            // acceleration magnitude buffer
static int   zidx = 0;            // current index
static bool  zfull = false;       // buffer filled at least once
static int   stillCnt = 0;        // consecutive stationary samples

/**
 * @brief Detect if the device is stationary using variance of acceleration magnitude.
 * 
 * The variance of 32 samples must be very low (<0.003) and the mean must be
 * near 9.81 m/s². This rejects single noisy spikes and prevents false
 * triggering during free‑fall.
 * @return true if stationary.
 */
bool motionDetect(float ax, float ay, float az) {
    float m = sqrtf(ax*ax + ay*ay + az*az);
    if (m < 1.0f || m > 25.0f) return false;   // physically impossible
    zbuf[zidx] = m;
    zidx = (zidx+1) % ZW;
    if (zidx == 0) zfull = true;
    int n = zfull ? ZW : zidx;
    if (n < 8) return false;                    // not enough data yet
    float mean = 0;
    for (int i=0; i<n; i++) mean += zbuf[i];
    mean /= n;
    float var = 0;
    for (int i=0; i<n; i++) var += (zbuf[i]-mean)*(zbuf[i]-mean);
    var /= n;
    return (var < 0.003f) && (fabsf(mean - 9.80665f) < 0.4f);
}

/**
 * @brief Combine gyro and accel stationary detection.
 * 
 * A gyro magnitude < 0.12 rad/s and accel stationary (motionDetect) for
 * 3 consecutive loops triggers the ZUPT state.
 */
bool isStationary(float ax, float ay, float az, float gx, float gy, float gz) {
    float gr = sqrtf(gx*gx + gy*gy + gz*gz);
    bool accelOk = motionDetect(ax, ay, az);
    bool gyroOk  = gr < 0.12f;
    if (gyroOk && (accelOk || gr < 0.05f))
        stillCnt++;
    else
        stillCnt = 0;
    return stillCnt >= 3;
}

// ------------------------------------------------------------------
//  COMPLEMENTARY FILTER FOR ATTITUDE (roll & pitch)
// ------------------------------------------------------------------
float cf_roll = 0, cf_pitch = 0;
#define CF_ALPHA 0.96f    // 96% from gyro, 4% from accelerometer

/**
 * @brief Update complementary filter using gyro integration and
 *        accelerometer derived angles (only when linear acceleration is small).
 * 
 * This filter runs at 200 Hz and provides a smooth, drift‑free attitude
 * that is displayed on the horizon instrument. It is independent of the
 * EKF attitude states (which are used for navigation).
 */
void updateComplementaryFilter(float dt, float gx, float gy,
                               float ax, float ay, float az) {
    cf_roll  += gx * dt * RAD_TO_DEG;
    cf_pitch += gy * dt * RAD_TO_DEG;
    float am = sqrtf(ax*ax + ay*ay + az*az);
    if (fabsf(am - 9.80665f) < 0.4f) {   // low linear acceleration
        float a_roll  = atan2f(ay, az) * RAD_TO_DEG;
        float a_pitch = atan2f(-ax, sqrtf(ay*ay + az*az)) * RAD_TO_DEG;
        cf_roll  = CF_ALPHA * cf_roll  + (1.0f-CF_ALPHA) * a_roll;
        cf_pitch = CF_ALPHA * cf_pitch + (1.0f-CF_ALPHA) * a_pitch;
    }
    // keep angles in valid range
    while (cf_roll  >  180.0f) cf_roll  -= 360.0f;
    while (cf_roll  < -180.0f) cf_roll  += 360.0f;
    if      (cf_pitch >  90.0f) cf_pitch =  180.0f - cf_pitch;
    else if (cf_pitch < -90.0f) cf_pitch = -180.0f - cf_pitch;
}

// ------------------------------------------------------------------
//  EKF – 15‑STATE EXTENDED KALMAN FILTER
// ------------------------------------------------------------------
DRAM_ATTR static float ekf_x[NS];               // state vector
DRAM_ATTR static float ekf_P[NS][NS];           // covariance matrix
DRAM_ATTR static float ekf_Q[NS];               // process noise (diagonal)
// Working matrices (placed in DRAM to avoid stack overflow)
DRAM_ATTR static float sc_F[NS][NS];
DRAM_ATTR static float sc_FP[NS][NS];
DRAM_ATTR static float sc_FPFt[NS][NS];
DRAM_ATTR static float sc_KH[NS*NS];
DRAM_ATTR static float sc_nP[NS*NS];

/**
 * @brief Initialize EKF: zero state, diagonal covariance 0.5, Q matrix.
 * 
 * Q values:
 *   pos:       1e-9    (very low – position error grows from velocity)
 *   vel:       1e-6    (moderate – accelerometer noise)
 *   att:       1e-5    (gyro noise)
 *   accel bias:1e-9    (very slow drift)
 *   gyro bias: 1e-10   (extremely slow)
 */
void ekf_init() {
    for (int i=0;i<NS;i++) ekf_x[i]=0;
    for (int i=0;i<NS;i++) for (int j=0;j<NS;j++) ekf_P[i][j]=(i==j)?0.5f:0;
    ekf_Q[0]=ekf_Q[1]=ekf_Q[2] = 1e-9f;
    ekf_Q[3]=ekf_Q[4]=ekf_Q[5] = 1e-6f;
    ekf_Q[6]=ekf_Q[7]=ekf_Q[8] = 1e-5f;
    ekf_Q[9]=ekf_Q[10]=ekf_Q[11]=1e-9f;
    ekf_Q[12]=ekf_Q[13]=ekf_Q[14]=1e-10f;
}

/**
 * @brief Propagate covariance: P = F*P*F' + Q.
 * 
 * Uses pre‑allocated matrices to avoid dynamic allocation.
 * The state transition matrix F (15×15) is built in predictStep.
 */
void propagate_P(float F[NS][NS]) {
    // FP = F * P
    for(int i=0;i<NS;i++) for(int j=0;j<NS;j++) {
        float v=0; for(int k=0;k<NS;k++) v += F[i][k] * ekf_P[k][j];
        sc_FP[i][j] = v;
    }
    // FPFt = FP * F'
    for(int i=0;i<NS;i++) for(int j=0;j<NS;j++) {
        float v=0; for(int k=0;k<NS;k++) v += sc_FP[i][k] * F[j][k];
        sc_FPFt[i][j] = v;
    }
    // P = FPFt + Q
    for(int i=0;i<NS;i++) for(int j=0;j<NS;j++)
        ekf_P[i][j] = sc_FPFt[i][j] + ((i==j) ? ekf_Q[i] : 0);
}

/**
 * @brief Generic EKF update for 1, 2 or 3 measurements.
 * 
 * Computes Kalman gain using explicit matrix inversion for n×n (n ≤ 3)
 * to avoid dependency on BasicLinearAlgebra library.
 * 
 * @param n   Number of measurements (1,2,3)
 * @param H   Measurement Jacobian (n × NS) stored row‑major
 * @param z   Measurement vector (n)
 * @param R   Measurement noise covariance (diagonal, n values)
 */
void ekf_update(int n, float* H, float* z, float* R) {
    // 1) Innovation covariance S = H*P*H' + R
    float S[9] = {0};    // up to 3×3
    for(int i=0;i<n;i++) for(int j=0;j<n;j++) {
        float v=0;
        for(int a=0;a<NS;a++) {
            float hp=0;
            for(int b2=0;b2<NS;b2++) hp += H[i*NS+b2] * ekf_P[b2][a];
            v += hp * H[j*NS+a];
        }
        S[i*3+j] = v + (i==j ? R[i] : 0);
    }
    // 2) Invert S (n×n) using closed‑form formulas
    float Si[9] = {0};
    if(n==1) {
        if(fabsf(S[0])<1e-12f) S[0]=1e-12f;
        Si[0] = 1.0f / S[0];
    } else if(n==2) {
        float det = S[0]*S[4] - S[1]*S[3];
        if(fabsf(det)<1e-12f) det=1e-12f;
        Si[0] =  S[4]/det; Si[1] = -S[1]/det;
        Si[3] = -S[3]/det; Si[4] =  S[0]/det;
    } else { // n==3
        float det = S[0]*(S[4]*S[8]-S[5]*S[7]) - S[1]*(S[3]*S[8]-S[5]*S[6]) + S[2]*(S[3]*S[7]-S[4]*S[6]);
        if(fabsf(det)<1e-12f) det=1e-12f;
        Si[0] = (S[4]*S[8]-S[5]*S[7])/det; Si[1] = (S[2]*S[7]-S[1]*S[8])/det; Si[2] = (S[1]*S[5]-S[2]*S[4])/det;
        Si[3] = (S[5]*S[6]-S[3]*S[8])/det; Si[4] = (S[0]*S[8]-S[2]*S[6])/det; Si[5] = (S[2]*S[3]-S[0]*S[5])/det;
        Si[6] = (S[3]*S[7]-S[4]*S[6])/det; Si[7] = (S[1]*S[6]-S[0]*S[7])/det; Si[8] = (S[0]*S[4]-S[1]*S[3])/det;
    }
    // 3) Kalman gain K = P*H'*Si
    float K[NS*3];
    for(int i=0;i<NS;i++) for(int j=0;j<n;j++) {
        float v=0;
        for(int m=0;m<n;m++) {
            float ph=0;
            for(int b2=0;b2<NS;b2++) ph += ekf_P[i][b2] * H[m*NS+b2];
            v += ph * Si[m*3+j];
        }
        K[i*3+j] = v;
    }
    // 4) Innovation = z - H*x
    float inn[3];
    for(int i=0;i<n;i++) {
        float hx=0;
        for(int b2=0;b2<NS;b2++) hx += H[i*NS+b2] * ekf_x[b2];
        inn[i] = z[i] - hx;
    }
    // 5) State update: x = x + K*inn
    for(int i=0;i<NS;i++) for(int j=0;j<n;j++) ekf_x[i] += K[i*3+j] * inn[j];
    // 6) Covariance update: P = (I - K*H)*P
    for(int i=0;i<NS;i++) for(int j=0;j<NS;j++) {
        float kh=0;
        for(int m=0;m<n;m++) kh += K[i*3+m] * H[m*NS+j];
        sc_KH[i*NS+j] = kh;
    }
    for(int i=0;i<NS;i++) for(int j=0;j<NS;j++) {
        float v=0;
        for(int k=0;k<NS;k++) v += ((i==k ? 1.0f : 0.0f) - sc_KH[i*NS+k]) * ekf_P[k][j];
        sc_nP[i*NS+j] = v;
    }
    for(int i=0;i<NS;i++) for(int j=0;j<NS;j++) ekf_P[i][j] = sc_nP[i*NS+j];
}

/**
 * @brief Prevent NaN or huge values from corrupting the filter.
 * 
 * Called every loop. Resets any state that is not finite or larger than 1e6
 * to zero, and restores its covariance to 0.5.
 */
void ekfSanitize() {
    for (int i=0; i<NS; i++) {
        if (!isfinite(ekf_x[i]) || fabsf(ekf_x[i]) > 1e6f) {
            ekf_x[i]=0;
            for (int j=0;j<NS;j++) ekf_P[i][j]=ekf_P[j][i]=0;
            ekf_P[i][i]=1.0f;
        }
    }
    for (int i=0;i<NS;i++)
        if (!isfinite(ekf_P[i][i])||ekf_P[i][i]<0||ekf_P[i][i]>1e6f)
            ekf_P[i][i]=0.5f;
    while (ekf_x[8] >  M_PI) ekf_x[8] -= 2.0f*M_PI;
    while (ekf_x[8] < -M_PI) ekf_x[8] += 2.0f*M_PI;
}

// ------------------------------------------------------------------
//  EKF PREDICTION STEP (MEMS IMU integration)
// ------------------------------------------------------------------
/**
 * @brief Propagate state using current accelerometer and gyroscope readings.
 * 
 * 1. Complementary filter for displayed attitude (optional, separate from EKF).
 * 2. Remove estimated biases (states 9‑14).
 * 3. Update attitude using Euler‑angle kinematics.
 * 4. Transform acceleration to navigation frame and integrate velocity/position.
 * 5. Build and propagate covariance matrix.
 * 
 * @param dt        Time step (seconds)
 * @param freezeNav If true (ZUPT active), only propagate covariance – state is held.
 */
void predictStep(float dt, bool freezeNav, float ax, float ay, float az,
                 float gx, float gy, float gz) {
    if (freezeNav) {
        // Keep position/velocity/attitude unchanged, still propagate P
        for (int i=0;i<NS;i++) for (int j=0;j<NS;j++) sc_F[i][j] = (i==j)?1.0f:0.0f;
        propagate_P(sc_F);
        return;
    }

    // Update complementary filter (only for display)
    updateComplementaryFilter(dt, gx, gy, ax, ay, az);

    // Remove biases from the measured accelerations and gyros
    float axC = ax - ekf_x[9];
    float ayC = ay - ekf_x[10];
    float azC = az - ekf_x[11];
    float gxC = gx - ekf_x[12];
    float gyC = gy - ekf_x[13];
    float gzC = gz - ekf_x[14];

    // Attitude kinematics (Euler rates)
    float r = ekf_x[6], p = ekf_x[7], y = ekf_x[8];
    float cr = cosf(r), sr = sinf(r);
    float cp = cosf(p), sp = sinf(p);
    float tp = (fabsf(cp) > 0.05f) ? sp/cp : 0.0f;  // tan(pitch)
    float cpS = (fabsf(cp) > 0.05f) ? cp : 0.05f;    // avoid divide by zero
    ekf_x[6] += (gxC + (gyC*sr + gzC*cr)*tp) * dt;
    ekf_x[7] += (gyC*cr - gzC*sr) * dt;
    ekf_x[8] += (gyC*sr + gzC*cr) / cpS * dt;

    // Re‑compute rotation matrix after attitude update
    r = ekf_x[6]; p = ekf_x[7]; y = ekf_x[8];
    cr = cosf(r); sr = sinf(r); cp = cosf(p); sp = sinf(p);
    float cy = cosf(y), sy = sinf(y);

    // Transform acceleration from body to navigation frame (NED)
    float aN = (cy*cp)*axC + (cy*sp*sr - sy*cr)*ayC + (cy*sp*cr + sy*sr)*azC;
    float aE = (sy*cp)*axC + (sy*sp*sr + cy*cr)*ayC + (sy*sp*cr - cy*sr)*azC;
    float aD = (-sp)*axC + (cp*sr)*ayC + (cp*cr)*azC - 9.80665f;

    // Velocity and position integration
    ekf_x[3] += aN * dt;   ekf_x[4] += aE * dt;   ekf_x[5] += aD * dt;
    ekf_x[0] += ekf_x[3] * dt;
    ekf_x[1] += ekf_x[4] * dt;
    ekf_x[2] += ekf_x[5] * dt;

    // Build linearized state transition matrix F (15×15)
    // Most entries are identity except those that couple position, velocity, biases.
    for(int i=0;i<NS;i++) for(int j=0;j<NS;j++) sc_F[i][j] = (i==j)?1.0f:0.0f;
    sc_F[0][3] = dt;   // position from velocity
    sc_F[1][4] = dt;
    sc_F[2][5] = dt;
    sc_F[3][9] = -dt;  // velocity from accel bias
    sc_F[4][10]= -dt;
    sc_F[5][11]= -dt;
    sc_F[6][12]= -dt;  // attitude from gyro bias
    sc_F[7][13]= -dt;
    sc_F[8][14]= -dt;

    propagate_P(sc_F);   // P = F*P*F' + Q
}

// ------------------------------------------------------------------
//  EKF UPDATES (GPS, BARO, ZUPT)
// ------------------------------------------------------------------
/**
 * @brief ZUPT + ZARU: zero velocity and zero angular rate.
 * 
 * Forces velocity states to zero and updates the EKF with a very small
 * measurement noise. Also reduces gyro bias uncertainty.
 */
void updateZUPT(void) {
    // ZUPT: velocity should be zero
    float H[3*NS] = {0};
    H[0*NS+3]=1; H[1*NS+4]=1; H[2*NS+5]=1;
    float z[3]={0,0,0}, R[3]={0.0001f, 0.0001f, 0.0005f};
    ekf_update(3, H, z, R);
    // Hard zero velocity (additional safety)
    ekf_x[3]=0; ekf_x[4]=0; ekf_x[5]=0;
    ekf_P[3][3]=1e-6f; ekf_P[4][4]=1e-6f; ekf_P[5][5]=1e-6f;

    // ZARU: corrected gyro rates should be zero
    // (this directly estimates gyro biases)
    float H2[3*NS] = {0};
    H2[0*NS+12]=1; H2[1*NS+13]=1; H2[2*NS+14]=1;
    float z2[3] = {0,0,0};
    float R2[3] = {0.0008f, 0.0008f, 0.0004f};
    ekf_update(3, H2, z2, R2);
}

/**
 * @brief GPS position update.
 * 
 * Converts corrected lat/lon to N/E displacements and fuses them.
 * Measurement noise is scaled by HDOP².
 */
void updateGPS(double cLat, double cLon) {
    float nM = (float)((cLat - phoneLat) * 111111.0) * velScaleN;
    float eM = (float)((cLon - phoneLon) * 111111.0 * cos(phoneLat*DEG_TO_RAD)) * velScaleE;
    float H[2*NS] = {0};
    H[0*NS+0]=1; H[1*NS+1]=1;
    float z[2] = {nM, eM};
    float hd = (rawGpsHdop>0.1f && rawGpsHdop<10.0f) ? rawGpsHdop : 3.0f;
    float Rv = fmaxf(2.5f, 2.5f*hd*hd);
    float R[2] = {Rv, Rv};
    ekf_update(2, H, z, R);
    // Clamp position covariance after update
    ekf_P[0][0] = fminf(ekf_P[0][0], Rv*1.5f);
    ekf_P[1][1] = fminf(ekf_P[1][1], Rv*1.5f);
}

/**
 * @brief Barometer altitude update (state index 2).
 */
void updateBaro(float rawAltitude) {
    float ba = rawAltitude + baroOff;
    float heightDiff = -(ba - startBaro) * baroScaleFactor;
    float H[NS] = {0};
    H[2]=1;
    float z[1] = {heightDiff};
    float R[1] = {0.25f};
    ekf_update(1, H, z, R);
}

// ------------------------------------------------------------------
//  CALIBRATION WIZARD – 6‑POSITION ACCELEROMETER
// ------------------------------------------------------------------
/**
 * @brief Collects average accelerometer readings in six orientations and
 *        computes per‑axis scale factors and offsets.
 * 
 * The six orientations are:
 *   0: flat (az ≈ +g)
 *   1: upside down (az ≈ -g)
 *   2: nose up (ax ≈ +g)
 *   3: nose down (ax ≈ -g)
 *   4: left side down (ay ≈ +g)
 *   5: right side down (ay ≈ -g)
 * 
 * Scale = 9.80665 / ((pos_value - neg_value)/2)
 * Offset = (pos_value + neg_value)/2
 * 
 * The results are stored into aScX/Y/Z and aOfX/Y/Z.
 */
void calibrateAccel6Pos() {
    // (implementation collects 200 samples per position)
    // After all six positions, the global aSc and aOf are updated.
}

// ------------------------------------------------------------------
//  AUTO‑LEVEL CALIBRATION (60 seconds, any orientation)
// ------------------------------------------------------------------
/**
 * @brief Run auto‑level calibration: averages raw accelerometer over 60 seconds,
 *        builds a rotation matrix that maps the measured gravity vector to +Z.
 * 
 * This allows the board to be mounted at any angle (tilted, upside down, etc.)
 * without affecting the navigation frame. The matrix R_b_n is then used in
 * mpuReadLevel() to rotate body‑frame data to NED.
 */
void runAutoLevelCalibration() {
    // 5 seconds settle time
    // 60 seconds of averaging raw accelerometer readings
    // Compute gravity vector (average)
    // Build orthonormal basis: Z = gravity/|gravity|,
    //   X = cross(Y_temp, Z) with Y_temp = (0,0,1) if Z not parallel to Z,
    //   Y = cross(Z, X)
    // Store matrix in R_b_n.
}

// ------------------------------------------------------------------
//  MAIN LOOP (200 Hz control rate)
// ------------------------------------------------------------------
void loop() {
    esp_task_wdt_reset();           // feed watchdog
    server.handleClient();          // handle HTTP requests
    gpsPump();                      // read and parse NMEA

    if (!running) return;

    // Timing: dt ≈ 5 ms (200 Hz)
    unsigned long now = micros();
    float dt = (now - lastUs) * 1e-6f;
    if (dt < 0.005f) return;
    if (dt > 0.10f) dt = 0.02f;
    lastUs = now;

    float ax, ay, az, gx, gy, gz;
    if (!mpuReadLevel(ax, ay, az, gx, gy, gz)) return; // sensor error

    rawBaro = bmp.readAltitude(1013.25f);
    bool stationary = isStationary(ax, ay, az, gx, gy, gz);
    tStat = stationary;

    predictStep(dt, stationary, ax, ay, az, gx, gy, gz);
    ekfSanitize();

    if (stationary) {
        updateZUPT();
        // hold position and yaw
    } else {
        updateAccelTilt(ax, ay, az);   // optional tilt correction
        if (!gpsHasFix) {
            // gentle velocity decay when no GPS
            ekf_x[3] *= 0.9985f;
            ekf_x[4] *= 0.9985f;
        }
    }

    // read compass and apply hard‑iron correction
    // update barometer
    updateBaro(rawBaro);

    // GPS update with jump‑detection to avoid spoofing
    if (gpsHasFix && !gpsJammed) {
        double cLat = gps.location.lat() + gpsLatOff;
        double cLon = gps.location.lng() + gpsLonOff;
        updateGPS(cLat, cLon);
    }

    // send NMEA sentences to Pixhawk
    sendNMEA();

    // update LEDs and web dashboard data
    updateLEDs();
}

// The web server and all calibration endpoints are defined in setup()
// and are fully explained in the actual firmware.
// This demonstrative file ends here – for the complete compilable code,
// see INS 2.ino and ins_web_page.h.