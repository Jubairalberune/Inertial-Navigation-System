# INS   – Complete Technical Reference (not for compilation)

This document contains the **full annotated source code** of the INS firmware for MicroController.  
It is **not intended to be compiled** – instead, it serves as a **detailed educational resource** explaining every line, every mathematical model, and every design decision.

---

## Table of Contents

1. [System Overview](#system-overview)
2. [Hardware Pin Definitions](#hardware-pin-definitions)
3. [MEMS IMU Raw I2C Driver](#MEMS IMU-raw-i2c-driver)
4. [Calibration Parameters (Global)](#calibration-parameters-global)
5. [Sensor Reading Pipeline with Full Calibration](#sensor-reading-pipeline-with-full-calibration)
6. [ZUPT – Stationary Detection](#zupt--stationary-detection)
7. [Complementary Filter for Attitude Display](#complementary-filter-for-attitude-display)
8. [15‑State Extended Kalman Filter (EKF)](#15state-extended-kalman-filter-ekf)
   - State Vector & Covariance
   - Prediction Step (MEMS IMU Integration)
   - Update Steps (GPS, Baro, ZUPT)
9. [Calibration Wizard (6‑Position Accel, Gyro, Mag, Velocity, Baro, GPS Sync)](#calibration-wizard)
10. [Auto‑Level Calibration (Any Orientation)](#autolevel-calibration-any-orientation)
11. [Main Loop (200 Hz)](#main-loop-200-hz)
12. [NMEA Output for Pixhawk](#nmea-output-for-pixhawk)
13. [Web Server & Dashboard](#web-server--dashboard)
14. [Offline HTML Demo Reference](#offline-html-demo-reference)

---

## System Overview

This INS fuses:
- **MEMS IMU** (6‑axis MEMS IMU) – accelerometer & gyroscope
- **Barometer** – barometric altitude
- **Magnetometer ** – magnetometer (compass)
- **GPS/GNSS** – GPS

It computes **position (N/E/D), velocity, and attitude (roll/pitch/yaw)** using a **15‑state Extended Kalman Filter** that runs at ~200 Hz.  
The system works **even without GPS** (pure inertial navigation) and recovers when GPS returns.  
ZUPT (Zero Velocity Update) kills drift when stationary.  
A full **calibration wizard** corrects sensor scale, offset, hard‑iron, and even velocity/barometer scale factors.  
A **web dashboard** (Wi‑Fi access point) shows live instruments, trail map, and controls.

---

## Hardware Pin Definitions


// I2C bus
#define SDA_PIN     21
#define SCL_PIN     22

// GPS/GNSS GPS (UART2)
#define GPS_RX      16          // MicroController RX ← GPS TX
#define GPS_TX      17          // MicroController TX → GPS RX (rarely used)

// Pixhawk NMEA output (UART1)
#define PIX_RX      32
#define PIX_TX      33

// Status LEDs (active high)
#define LED_GPS     4           // ON when GPS fix is fused into EKF
#define LED_STATIC  5           // ON when ZUPT active (truly still)
#define LED_DRIFT   2           // BLINKS when drifting while static

// Buzzer (active high, or use tone())
#define BUZZER_PIN  26

// EKF state dimension
#define NS 15
Why these pins?

I2C pins 21/22 are the default on most MicroController boards.

UART2 (GPIO16/17) is free and does not conflict with USB.

UART1 (GPIO32/33) is used for NMEA output to Pixhawk.

LEDs on GPIO2,4,5 are standard – GPIO2 is the built‑in LED on many MicroController modules.

MEMS IMU Raw I2C Driver
The Adafruit_MEMS IMU library is not used because it calls Wire.begin() internally and resets the I2C pins on MicroController Core 3.x. Instead we implement direct register access.

cpp
#define MPU_ADDR    0x68
#define ACCEL_SC    (9.80665f / 8192.0f)   // ±4g → 8192 LSB/g
#define GYRO_SC     (0.00106422f)          // ±500 dps → 65.5 LSB/(dps)

bool mpuWR(uint8_t r, uint8_t v) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(r);
    Wire.write(v);
    return Wire.endTransmission() == 0;
}

bool mpuRR(uint8_t r, uint8_t* b, uint8_t n) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(r);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom(MPU_ADDR, n);
    if (Wire.available() < n) return false;
    for (uint8_t i = 0; i < n; i++) b[i] = Wire.read();
    return true;
}

bool mpuInit() {
    mpuWR(0x6B, 0x80); delay(150);          // reset device
    if (!mpuWR(0x6B, 0x00)) return false;   // wake up
    delay(100);
    uint8_t w = 0;
    if (!mpuRR(0x75, &w, 1)) return false;  // WHO_AM_I
    if (w != 0x68 && w != 0x70 && w != 0x71 && w != 0x19 && w != 0x98)
        return false;                       // unknown chip
    mpuWR(0x1A, 0x04);   // DLPF band 3 → 21 Hz cutoff (good for human motion)
    mpuWR(0x1B, 0x08);   // gyro range ±500 deg/s
    mpuWR(0x1C, 0x08);   // accel range ±4g
    delay(50);
    return true;
}

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
Notes on ranges:

±4g accel gives enough headroom for walking, drones, cars.

±500 dps gyro covers all human motion and most UAV manoeuvres.

DLPF 21 Hz rejects high‑frequency vibration but keeps response fast enough for navigation.

Calibration Parameters (Global)
These are applied in order inside mpuReadLevel().

cpp
// 6‑position accelerometer calibration (per‑axis scale & offset)
float aScX = 1.0f, aScY = 1.0f, aScZ = 1.0f;
float aOfX = 0.0f, aOfY = 0.0f, aOfZ = 0.0f;

// Gyro static bias (from 60‑second calibration or manual)
float gbx = 0, gby = 0, gbz = 0;

// Overall magnitude correction (auto‑level calibration)
float accelScale = 1.0f;

// Velocity scale factors (from 1‑metre walk)
float velScaleN = 1.0f, velScaleE = 1.0f;

// Barometer altitude scale (from 1‑metre lift)
float baroScaleFactor = 1.0f;

// GPS offset (user origin vs raw GPS coordinates)
double gpsLatOff = 0, gpsLonOff = 0;
bool gpsSyncDone = false;

// Auto‑level rotation matrix (body → navigation frame)
float R_b_n[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
Why separate scales for velocity N and E?
Because the INS may underestimate distance in one direction (e.g., due to uncalibrated accelerometer misalignment). Walking 1 m forward or sideways gives independent corrections.

Sensor Reading Pipeline with Full Calibration
cpp
static float sm_ax=0, sm_ay=0, sm_az=0;
static float sm_gx=0, sm_gy=0, sm_gz=0;

bool mpuReadLevel(float &ax, float &ay, float &az,
                  float &gx, float &gy, float &gz) {
    float rax, ray, raz, rgx, rgy, rgz;
    if (!mpuReadRaw(rax, ray, raz, rgx, rgy, rgz)) return false;

    // 1) Body‑frame per‑axis scale & offset (6‑position accel cal)
    rax = rax * aScX - aOfX;
    ray = ray * aScY - aOfY;
    raz = raz * aScZ - aOfZ;

    // 2) Overall magnitude correction (auto‑level cal)
    rax *= accelScale; ray *= accelScale; raz *= accelScale;

    // 3) Subtract gyro bias
    rgx -= gbx; rgy -= gby; rgz -= gbz;

    // 4) Rotate from body frame to navigation frame (NED)
    //    R_b_n is built during auto‑level calibration.
    ax = R_b_n[0][0]*rax + R_b_n[0][1]*ray + R_b_n[0][2]*raz;
    ay = R_b_n[1][0]*rax + R_b_n[1][1]*ray + R_b_n[1][2]*raz;
    az = R_b_n[2][0]*rax + R_b_n[2][1]*ray + R_b_n[2][2]*raz;
    gx = R_b_n[0][0]*rgx + R_b_n[0][1]*rgy + R_b_n[0][2]*rgz;
    gy = R_b_n[1][0]*rgx + R_b_n[1][1]*rgy + R_b_n[1][2]*rgz;
    gz = R_b_n[2][0]*rgx + R_b_n[2][1]*rgy + R_b_n[2][2]*rgz;

    // 5) Exponential smoothing (low‑pass filter)
    sm_gx = 0.6f*sm_gx + 0.4f*gx;   sm_gy = 0.6f*sm_gy + 0.4f*gy;   sm_gz = 0.6f*sm_gz + 0.4f*gz;
    sm_ax = 0.5f*sm_ax + 0.5f*ax;   sm_ay = 0.5f*sm_ay + 0.5f*ay;   sm_az = 0.5f*sm_az + 0.5f*az;
    ax=sm_ax; ay=sm_ay; az=sm_az;
    gx=sm_gx; gy=sm_gy; gz=sm_gz;
    return true;
}
Why smoothing?

Accelerometer α=0.5 gives a cutoff frequency ≈ 1/(2π*0.005*0.5) ≈ 64 Hz – enough to reject high‑frequency noise while keeping motion.

Gyro α=0.6 gives a slightly faster response (cutoff ~53 Hz) because gyro noise is lower.

ZUPT – Stationary Detection
cpp
#define ZW 32
static float zbuf[ZW];
static int zidx = 0;
static bool zfull = false;
static int stillCnt = 0;

bool motionDetect(float ax, float ay, float az) {
    float m = sqrtf(ax*ax + ay*ay + az*az);
    if (m < 1.0f || m > 25.0f) return false;   // invalid sample
    zbuf[zidx] = m;
    zidx = (zidx+1) % ZW;
    if (zidx == 0) zfull = true;
    int n = zfull ? ZW : zidx;
    if (n < 8) return false;                    // not enough data
    float mean = 0;
    for (int i=0;i<n;i++) mean += zbuf[i];
    mean /= n;
    float var = 0;
    for (int i=0;i<n;i++) var += (zbuf[i]-mean)*(zbuf[i]-mean);
    var /= n;
    // Stationary when variance < 0.003 and mean ≈ 1g
    return (var < 0.003f) && (fabsf(mean - 9.80665f) < 0.4f);
}

bool isStationary(float ax, float ay, float az, float gx, float gy, float gz) {
    float gr = sqrtf(gx*gx + gy*gy + gz*gz);
    bool accelOk = motionDetect(ax, ay, az);
    bool gyroOk  = gr < 0.12f;
    if (gyroOk && (accelOk || gr < 0.05f))
        stillCnt++;
    else
        stillCnt = 0;
    return stillCnt >= 3;       // require 3 consecutive stationary frames
}
Why a sliding window variance?
A single‑sample threshold would be fooled by noise. Variance over 32 samples reliably distinguishes true stillness from vibration.

Complementary Filter for Attitude Display
This filter is separate from the EKF and only used for the horizon instrument.

cpp
float cf_roll = 0, cf_pitch = 0;
#define CF_ALPHA 0.96f

void updateComplementaryFilter(float dt, float gx, float gy,
                               float ax, float ay, float az) {
    // Gyro integration
    cf_roll  += gx * dt * RAD_TO_DEG;
    cf_pitch += gy * dt * RAD_TO_DEG;

    // Accel derived angles (only when linear acceleration is low)
    float am = sqrtf(ax*ax + ay*ay + az*az);
    if (fabsf(am - 9.80665f) < 0.4f) {
        float a_roll  = atan2f(ay, az) * RAD_TO_DEG;
        float a_pitch = atan2f(-ax, sqrtf(ay*ay + az*az)) * RAD_TO_DEG;
        cf_roll  = CF_ALPHA * cf_roll  + (1.0f-CF_ALPHA) * a_roll;
        cf_pitch = CF_ALPHA * cf_pitch + (1.0f-CF_ALPHA) * a_pitch;
    }

    // Wrap angles to valid ranges
    while (cf_roll  >  180.0f) cf_roll  -= 360.0f;
    while (cf_roll  < -180.0f) cf_roll  += 360.0f;
    if      (cf_pitch >  90.0f) cf_pitch =  180.0f - cf_pitch;
    else if (cf_pitch < -90.0f) cf_pitch = -180.0f - cf_pitch;
}
Why 0.96?
Gyro is very accurate in the short term but drifts; accelerometer is noisy but stable long‑term. 96% gyro weight gives smooth motion while 4% accel slowly corrects drift.

15‑State Extended Kalman Filter (EKF)
State Vector
Index	Symbol	Description	Units
0	pN	North position	m
1	pE	East position	m
2	pD	Down position	m
3	vN	North velocity	m/s
4	vE	East velocity	m/s
5	vD	Down velocity	m/s
6	roll	Roll angle	rad
7	pitch	Pitch angle	rad
8	yaw	Yaw angle	rad
9-11	baN/E/D	Accelerometer biases	m/s²
12-14	bgX/Y/Z	Gyroscope biases	rad/s
The EKF propagates the state using the MEMS IMU (prediction step) and corrects it using GPS, barometer, and ZUPT (update steps).

EKF Matrices (Global, in DRAM)
cpp
DRAM_ATTR static float ekf_x[NS];               // state vector
DRAM_ATTR static float ekf_P[NS][NS];           // covariance matrix
DRAM_ATTR static float ekf_Q[NS];               // process noise (diagonal)
DRAM_ATTR static float sc_F[NS][NS];            // scratch matrices
DRAM_ATTR static float sc_FP[NS][NS];
DRAM_ATTR static float sc_FPFt[NS][NS];
DRAM_ATTR static float sc_KH[NS*NS];
DRAM_ATTR static float sc_nP[NS*NS];
Why placed in DRAM?
The MicroController’s default stack is only 8 KB. Placing large matrices (15×15 = 900 bytes each) in DRAM (static) avoids stack overflow.

EKF Initialisation
cpp
void ekf_init() {
    for (int i=0;i<NS;i++) ekf_x[i]=0;
    for (int i=0;i<NS;i++) for (int j=0;j<NS;j++) ekf_P[i][j]=(i==j)?0.5f:0;
    ekf_Q[0]=ekf_Q[1]=ekf_Q[2] = 1e-9f;
    ekf_Q[3]=ekf_Q[4]=ekf_Q[5] = 1e-6f;
    ekf_Q[6]=ekf_Q[7]=ekf_Q[8] = 1e-5f;
    ekf_Q[9]=ekf_Q[10]=ekf_Q[11]=1e-9f;
    ekf_Q[12]=ekf_Q[13]=ekf_Q[14]=1e-10f;
}
Process noise Q values reflect how fast each state is expected to change.

Position: very low (1e‑9) because position changes only via velocity.

Velocity: moderate (1e‑6) – accelerometer noise.

Attitude: higher (1e‑5) – gyro noise.

Biases: extremely low (1e‑9, 1e‑10) – they drift very slowly.

Covariance Propagation
cpp
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
Generic EKF Update (for 1,2 or 3 measurements)
cpp
void ekf_update(int n, float* H, float* z, float* R) {
    // 1) S = H*P*H' + R
    float S[9] = {0};
    for(int i=0;i<n;i++) for(int j=0;j<n;j++) {
        float v=0;
        for(int a=0;a<NS;a++) {
            float hp=0;
            for(int b2=0;b2<NS;b2++) hp += H[i*NS+b2] * ekf_P[b2][a];
            v += hp * H[j*NS+a];
        }
        S[i*3+j] = v + (i==j ? R[i] : 0);
    }
    // 2) Invert S analytically (1×1, 2×2 or 3×3) – no library needed
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
        float det = S[0]*(S[4]*S[8]-S[5]*S[7])
                  - S[1]*(S[3]*S[8]-S[5]*S[6])
                  + S[2]*(S[3]*S[7]-S[4]*S[6]);
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
    // 4) innovation = z - H*x
    float inn[3];
    for(int i=0;i<n;i++) {
        float hx=0;
        for(int b2=0;b2<NS;b2++) hx += H[i*NS+b2] * ekf_x[b2];
        inn[i] = z[i] - hx;
    }
    // 5) state update
    for(int i=0;i<NS;i++) for(int j=0;j<n;j++) ekf_x[i] += K[i*3+j] * inn[j];
    // 6) covariance update P = (I - K*H)*P
    for(int i=0;i<NS;i++) for(int j=0;j<NS;j++) {
        float kh=0;
        for(int m=0;m<n;m++) kh += K[i*3+m] * H[m*NS+j];
        sc_KH[i*NS+j] = kh;
    }
    for(int i=0;i<NS;i++) for(int j=0;j<NS;j++) {
        float v=0;
        for(int k=0;k<NS;k++) v += ((i==k?1.0f:0.0f) - sc_KH[i*NS+k]) * ekf_P[k][j];
        sc_nP[i*NS+j] = v;
    }
    for(int i=0;i<NS;i++) for(int j=0;j<NS;j++) ekf_P[i][j] = sc_nP[i*NS+j];
}
Why manual inversion?
The BasicLinearAlgebra library’s Invert() returns a proxy type that fails with operator*. Closed‑form inversion for n≤3 is fast and deterministic.

EKF Prediction Step (MEMS IMU Integration)
cpp
void predictStep(float dt, bool freezeNav, float ax, float ay, float az,
                 float gx, float gy, float gz) {
    if (freezeNav) {
        // During ZUPT: only propagate covariance, state unchanged
        for (int i=0;i<NS;i++) for (int j=0;j<NS;j++) sc_F[i][j] = (i==j)?1.0f:0.0f;
        propagate_P(sc_F);
        return;
    }

    // Update complementary filter for attitude display (optional, not part of EKF)
    updateComplementaryFilter(dt, gx, gy, ax, ay, az);

    // Remove estimated biases (states 9-14)
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
    float tp = (fabsf(cp) > 0.05f) ? sp/cp : 0.0f;   // tan(pitch), avoid singularity
    float cpS = (fabsf(cp) > 0.05f) ? cp : 0.05f;    // avoid division by zero
    ekf_x[6] += (gxC + (gyC*sr + gzC*cr)*tp) * dt;
    ekf_x[7] += (gyC*cr - gzC*sr) * dt;
    ekf_x[8] += (gyC*sr + gzC*cr) / cpS * dt;

    // Recompute rotation matrix after attitude update
    r = ekf_x[6]; p = ekf_x[7]; y = ekf_x[8];
    cr = cosf(r); sr = sinf(r); cp = cosf(p); sp = sinf(p);
    float cy = cosf(y), sy = sinf(y);

    // Transform acceleration to navigation frame (NED)
    float aN = (cy*cp)*axC + (cy*sp*sr - sy*cr)*ayC + (cy*sp*cr + sy*sr)*azC;
    float aE = (sy*cp)*axC + (sy*sp*sr + cy*cr)*ayC + (sy*sp*cr - cy*sr)*azC;
    float aD = (-sp)*axC + (cp*sr)*ayC + (cp*cr)*azC - 9.80665f;

    // Integrate velocity and position
    ekf_x[3] += aN * dt;   ekf_x[4] += aE * dt;   ekf_x[5] += aD * dt;
    ekf_x[0] += ekf_x[3] * dt;
    ekf_x[1] += ekf_x[4] * dt;
    ekf_x[2] += ekf_x[5] * dt;

    // Build linearised state transition matrix F
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

    propagate_P(sc_F);
}
Why freezeNav?
When ZUPT is active, we do not integrate MEMS IMU because the device is stationary. However, we still propagate the covariance to reflect that our uncertainty about biases grows slowly.

EKF Updates
ZUPT + ZARU
cpp
void updateZUPT(void) {
    // ZUPT: velocity should be zero
    float H[3*NS] = {0};
    H[0*NS+3]=1; H[1*NS+4]=1; H[2*NS+5]=1;
    float z[3]={0,0,0}, R[3]={0.0001f, 0.0001f, 0.0005f};
    ekf_update(3, H, z, R);
    // Hard‑zero velocity (additional safety)
    ekf_x[3]=0; ekf_x[4]=0; ekf_x[5]=0;
    ekf_P[3][3]=1e-6f; ekf_P[4][4]=1e-6f; ekf_P[5][5]=1e-6f;

    // ZARU: zero angular rate – corrects gyro biases
    float H2[3*NS] = {0};
    H2[0*NS+12]=1; H2[1*NS+13]=1; H2[2*NS+14]=1;
    float z2[3] = {0,0,0};
    float R2[3] = {0.0008f, 0.0008f, 0.0004f};
    ekf_update(3, H2, z2, R2);
}
GPS Position Update
cpp
void updateGPS(double cLat, double cLon) {
    // Convert corrected GPS coordinates to N/E displacement from origin
    float nM = (float)((cLat - phoneLat) * 111111.0) * velScaleN;
    float eM = (float)((cLon - phoneLon) * 111111.0 * cos(phoneLat*DEG_TO_RAD)) * velScaleE;
    float H[2*NS] = {0};
    H[0*NS+0]=1; H[1*NS+1]=1;   // measurement = position
    float z[2] = {nM, eM};
    // Scale R by HDOP² (bad GPS has larger uncertainty)
    float hd = (rawGpsHdop>0.1f && rawGpsHdop<10.0f) ? rawGpsHdop : 3.0f;
    float Rv = fmaxf(2.5f, 2.5f*hd*hd);
    float R[2] = {Rv, Rv};
    ekf_update(2, H, z, R);
    // Clamp position covariance after update
    ekf_P[0][0] = fminf(ekf_P[0][0], Rv*1.5f);
    ekf_P[1][1] = fminf(ekf_P[1][1], Rv*1.5f);
}
Barometer Altitude Update
cpp
void updateBaro(float rawAltitude) {
    float ba = rawAltitude + baroOff;
    float heightDiff = -(ba - startBaro) * baroScaleFactor;
    float H[NS] = {0}; H[2]=1;   // measurement = down position
    float z[1] = {heightDiff};
    float R[1] = {0.25f};
    ekf_update(1, H, z, R);
}
Calibration Wizard
Each calibration is a multi‑step process triggered by HTTP requests. Below is the conceptual explanation – the actual endpoints are implemented in setup().

6‑Position Accelerometer Calibration
Collects average acceleration in six orientations:

Position	Description	Expected dominant axis
0	Flat (normal upright)	az ≈ +g
1	Upside down	az ≈ -g
2	Nose up	ax ≈ +g
3	Nose down	ax ≈ -g
4	Left side down	ay ≈ +g
5	Right side down	ay ≈ -g
After all six:

Offset = (pos + neg) / 2 per axis

Scale = 9.80665 / ((pos - neg) / 2) per axis

Gyro Static Bias + Noise
Uses Welford’s online algorithm to compute mean (bias) and variance (noise) from 500 samples while the device is still. No large buffer needed.

Magnetometer N‑S‑E‑W
Points the device toward each cardinal direction. The North vector is stored, and hard‑iron offsets are derived from North/South and East/West pairs.

Velocity Calibration
User presses START and walks exactly 1 m in a straight line.

The EKF’s position change is measured.

Scale factor = 1.0 / measured_distance.

Applied to future acceleration integration (multiply aN and aE by velScaleN/E).

Barometer Calibration
User sets a reference altitude (ground).

Then lifts the device exactly 1 m and presses MARK.

Scale factor = 1.0 / (measured altitude change).

Applied to the height difference in updateBaro().

GPS Origin Sync
When GPS has a fix and the user enters precise origin coordinates,
offset = user_coords - raw_gps_coords.

Stored in gpsLatOff/gpsLonOff and used in all subsequent GPS updates.

Auto‑Level Calibration (Any Orientation)
cpp
void runAutoLevelCalibration() {
    // 5 seconds settle
    // Average raw accelerometer for 60 seconds
    // Compute gravity vector g = [gx_avg, gy_avg, gz_avg]
    // Build rotation matrix R_b_n such that R_b_n * g = [0, 0, 9.80665]
    // Also compute accelScale = 9.80665 / |g|
}
This allows the board to be mounted at any tilt – the navigation frame will still have Z pointing down.

Main Loop (200 Hz)
cpp
void loop() {
    esp_task_wdt_reset();           // prevent watchdog timeout
    server.handleClient();          // handle HTTP requests
    gpsPump();                      // parse NMEA sentences

    if (!running) return;

    // timing
    unsigned long now = micros();
    float dt = (now - lastUs) * 1e-6f;
    if (dt < 0.005f) return;        // limit to 200 Hz
    if (dt > 0.10f) dt = 0.02f;
    lastUs = now;

    float ax, ay, az, gx, gy, gz;
    if (!mpuReadLevel(ax, ay, az, gx, gy, gz)) return;

    rawBaro = bmp.readAltitude(1013.25f);
    bool stationary = isStationary(ax, ay, az, gx, gy, gz);
    tStat = stationary;

    predictStep(dt, stationary, ax, ay, az, gx, gy, gz);
    ekfSanitize();

    if (stationary) {
        updateZUPT();
        // also hold position and yaw (freeze)
    } else {
        if (!gpsHasFix) {
            // gentle velocity decay when no GPS (prevents runaway drift)
            ekf_x[3] *= 0.9985f;
            ekf_x[4] *= 0.9985f;
        }
    }

    updateBaro(rawBaro);

    if (gpsHasFix && !gpsJammed) {
        double cLat = gps.location.lat() + gpsLatOff;
        double cLon = gps.location.lng() + gpsLonOff;
        updateGPS(cLat, cLon);
    }

    sendNMEA();
    updateLEDs();
    // Telemetry for web dashboard is served on /data endpoint
}
Why velocity decay?
When GPS is absent, tiny accelerometer biases cause steady velocity growth. Multiplying by 0.9985 every 5 ms reduces velocity by ~30% per second – enough to kill drift but still allow true movement to be integrated.

NMEA Output for Pixhawk
Sent at 5 Hz on SerialPix (UART1). Sentences:

$GPGGA – time, position, fix quality, HDOP, altitude.

$GPRMC – time, position, speed over ground, course.

$GPVTG – velocity vector (used by ArduPilot).

Checksum is computed with nmea_ck() (XOR of all characters between $ and *).

Web Server & Dashboard
The MicroController acts as a Wi‑Fi access point (INS by J.A.B, password 12345678).
The web page is stored in PROGMEM as a single PAGE string (chunked to avoid 16 KB limit).
Endpoints:

Endpoint	Method	Description
/	GET	Serves the HTML dashboard (chunked)
/data	GET	Returns JSON with current EKF states, sensor data
/start	POST	Starts the INS with given origin lat/lon/alt
/reset	POST	Resets the system back to setup
/cal_accel_*	POST	6‑position accelerometer calibration endpoints
/cal_gyro	POST	Gyro bias + noise calibration
/cal_mag_*	POST	Magnetometer hard‑iron calibration
/cal_vel_*	POST	Velocity scale calibration
/cal_baro_*	POST	Barometer scale calibration
/cal_gps_sync	POST	GPS origin sync
/cal_status	GET	Returns which calibrations have been done
The dashboard (ins_web_page.h) uses canvas for horizon, compass, turn rate, and live trail map. It polls /data every 400 ms.