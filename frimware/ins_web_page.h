/**
 * @file ins_web_page.h
 * @brief Fully documented web dashboard for the INS ESP32 firmware.
 * 
 * This file contains the complete HTML, CSS, and JavaScript code that powers
 * the INS's web interface. It is stored in PROGMEM (flash) and served by the
 * ESP32's web server. The dashboard provides real-time navigation data,
 * calibration controls, and diagnostic information.
 * 
 * ⚠️ THIS FILE IS FOR EDUCATIONAL PURPOSES ONLY – NOT FOR DIRECT UPLOAD.
 * It is heavily commented to explain every line, every UI element, and every
 * interaction with the ESP32 firmware. Use the original ins_web_page.h for
 * actual deployment.
 * 
 * @author Jubair Al Berune
 * @version v16.0
 * @date 2025
 * 
 * @section Architecture Overview
 * 
 * The web page is a single-page application (SPA) that communicates with the
 * ESP32 via HTTP REST endpoints:
 * 
 * - GET  /         -> serves this HTML page (chunked streaming)
 * - GET  /status   -> returns {running, calDone, uptime}
 * - GET  /caldata  -> returns {done, p} (calibration progress)
 * - GET  /env      -> returns {al, tc, pp} (baro alt, temp, pressure)
 * - GET  /data     -> returns JSON with all EKF states, diagnostics, GPS, etc.
 * - POST /start    -> starts INS with origin lat/lon/alt
 * - POST /reset    -> resets the system
 * - POST /bump_start, /bump_return -> bump test
 * - POST /cal_*    -> calibration wizard endpoints
 * 
 * The page has three main views:
 * 1. LD (Loading) – shown while connecting to the ESP32.
 * 2. SU (Setup) – shown before INS starts; includes calibration wizard.
 * 3. DA (Dashboard) – shown when INS is running; shows live data.
 * 
 * All drawing (horizon, compass, turn indicator, trail map) is done on HTML5
 * canvas elements, offloading computation to the browser and minimising
 * ESP32 load.
 * 
 * The page polls /data every 400ms, /status and /caldata every second, and
 * updates the UI with the latest data.
 */

#pragma once

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  PROGMEM STRING – stored in flash, not RAM
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

/**
 * @brief The complete HTML page stored in PROGMEM.
 * 
 * Using PROGMEM keeps the ~28KB HTML string out of the ESP32's limited RAM.
 * The string is sent to the client in chunks (512 bytes) via handleRoot() to
 * avoid the 16KB TCP buffer limit on ESP32 Core 3.x.
 * 
 * @note This is a raw string literal using R"rawliteral(...)" to avoid escaping.
 */
static const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
    <title>INS by Jubair</title>

    <!-- ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ -->
    <!--  STYLE – Dark theme, monospace, cyber‑punk aesthetic          -->
    <!-- ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ -->
    <style>
        /* Reset and base */
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            background: #060c10;          /* Very dark blue/grey */
            color: #b8dce8;               /* Soft cyan text */
            font-family: monospace;       /* Monospace for technical look */
            font-size: 13px;
            padding: 6px;
        }
        h1 {
            color: #00cfff;               /* Bright cyan */
            text-align: center;
            font-size: 14px;
            letter-spacing: 3px;
            padding: 8px 0 2px;
        }
        .sub {
            text-align: center;
            font-size: 10px;
            color: #1e3a4a;               /* Dark cyan – subtle */
            margin-bottom: 8px;
        }

        /* Cards – containers for each section */
        .p {
            background: #09141c;           /* Slightly lighter than body */
            border: 1px solid #0c3050;     /* Dark blue border */
            border-radius: 3px;
            padding: 7px;
            margin-bottom: 5px;
        }
        .ph {
            font-size: 10px;
            color: #00cfff;
            letter-spacing: 2px;
            border-bottom: 1px solid #0c3050;
            padding-bottom: 3px;
            margin-bottom: 5px;
        }

        /* Rows – key-value pairs */
        .rw {
            display: flex;
            justify-content: space-between;
            padding: 2px 0;
        }
        .lb { font-size: 10px; color: #1e3a4a; }    /* Label */
        .vl { font-size: 12px; font-weight: bold; color: #00ff88; } /* Value */
        .vc { color: #00cfff; }                     /* Cyan value */
        .va { color: #ffb800; }                     /* Amber value (warning) */
        .vr { color: #ff3355; }                     /* Red value (error) */

        /* Status bar */
        .sb {
            display: flex;
            justify-content: space-between;
            align-items: center;
            background: #09141c;
            border: 1px solid #0c3050;
            border-radius: 3px;
            padding: 5px 8px;
            margin-bottom: 5px;
            font-size: 10px;
            gap: 4px;
            flex-wrap: wrap;
        }
        .bd {
            padding: 2px 7px;
            border-radius: 2px;
            font-size: 10px;
        }
        .ok { background: #002a18; color: #00ff88; border: 1px solid #00ff88; }  /* OK status */
        .wt { background: #150f00; color: #ffb800; border: 1px solid #ffb800; }  /* Warning */
        .jm { background: #150005; color: #ff3355; border: 1px solid #ff3355; }  /* Jammed */

        /* Grid layouts */
        .g2 { display: grid; grid-template-columns: 1fr 1fr; gap: 4px; }

        /* Buttons */
        .btn {
            width: 100%;
            padding: 10px;
            margin: 4px 0;
            background: transparent;
            font-family: monospace;
            font-size: 11px;
            border-radius: 3px;
            cursor: pointer;
        }
        .bc { color: #00cfff; border: 1px solid #00cfff; } /* Cyan button */
        .bg { color: #00ff88; border: 1px solid #00ff88; font-weight: bold; } /* Green button */
        .ba { color: #ffb800; border: 1px solid #ffb800; } /* Amber button */
        .br { color: #ff3355; border: 1px solid #ff3355; } /* Red button */

        /* Inputs */
        input {
            width: 100%;
            background: #09141c;
            color: #00ff88;
            border: 1px solid #0c3050;
            border-radius: 3px;
            padding: 7px;
            margin: 3px 0;
            font-family: monospace;
            font-size: 12px;
        }

        /* Progress bar */
        .cb { height: 4px; background: #0c3050; border-radius: 2px; margin: 4px 0; }
        .cf { height: 100%; background: #00ff88; border-radius: 2px; transition: width .3s; }

        hr { border: none; border-top: 1px solid #0c3050; margin: 5px 0; }

        #msg { text-align: center; font-size: 10px; color: #ffb800; min-height: 14px; margin-top: 3px; }
        .warn { background: #150005; border: 1px solid #ff3355; color: #ff3355; border-radius: 3px; text-align: center; padding: 5px; font-size: 10px; margin-bottom: 5px; }

        canvas { display: block; }   /* Canvas elements are block-level */

        #LD { text-align: center; padding: 30px; color: #00cfff; font-size: 11px; } /* Loading screen */

        /* LEDs – small circle indicators */
        .leds { display: flex; gap: 6px; justify-content: center; margin-bottom: 5px; font-size: 9px; }
        .led {
            padding: 3px 8px;
            border-radius: 2px;
            border: 1px solid #0c3050;
            color: #1e3a4a;
        }
        .led.on { border-color: #00ff88; color: #00ff88; background: #002a18; } /* ON state */
    </style>
</head>
<body>

<!-- ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ -->
<!--  PAGE TITLE                                                          -->
<!-- ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ -->
<h1>Inertial Navigation System</h1>
<div class="sub">// UAV || MISSILE || ROV \\ v15.3</div>

<!-- ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ -->
<!--  LD – LOADING SCREEN (visible while connecting)                     -->
<!-- ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ -->
<div id="LD">CONNECTING...</div>

<!-- ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ -->
<!--  SU – SETUP PANEL (visible before INS starts)                       -->
<!-- ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ -->
<div id="SU" style="display:none">

    <!-- GPS STATUS -->
    <div class="p">
        <div class="ph">NEO-6M GPS</div>
        <div class="warn">VCC → 5V/VIN (NOT 3.3V!) AMS1117 board needs 4–5V input</div>
        <div class="rw"><span class="lb">SERIAL</span><span class="vl" id="gMod">SCANNING</span></div>
        <div class="rw"><span class="lb">SAT FIX</span><span class="vl" id="gFx">--</span></div>
        <div class="rw"><span class="lb">BAUD</span><span class="vl vc" id="gBd">--</span></div>
        <div class="rw"><span class="lb">BYTES</span><span class="vl" id="gBy">0</span></div>
        <div class="rw"><span class="lb">SATS</span><span class="vl" id="gSa">--</span></div>
        <div style="font-size:9px;color:#1e3a4a;margin-top:4px;word-break:break-all" id="gLn">--</div>
        <div style="font-size:9px;color:#ffb800;margin-top:3px">TX→GPIO16 RX→GPIO17 GND→GND</div>
    </div>

    <!-- 60-SECOND AUTO-LEVEL CALIBRATION -->
    <div class="p">
        <div class="ph">CALIBRATION</div>
        <div class="warn" id="ws">KEEP FLAT. DO NOT TOUCH OR VIBRATE.</div>
        <div class="rw"><span class="lb">STATUS</span><span class="vl va" id="cs">PENDING</span></div>
        <div class="cb"><div class="cf" id="cf" style="width:0%"></div></div>
        <div class="rw"><span class="lb">BARO</span><span class="vl vc" id="bL">--</span></div>
        <div class="rw"><span class="lb">TEMP</span><span class="vl" id="tL">--</span></div>
        <div class="rw"><span class="lb">PRESS</span><span class="vl" id="pL">--</span></div>
    </div>

    <!-- DATE / TIME (auto-sync from browser) -->
    <div class="p">
        <div class="ph">DATE / TIME</div>
        <div style="display:flex;gap:4px">
            <input id="dtI" placeholder="YYYYMMDD_HHMMSS" style="flex:1;margin:0">
            <button class="btn bc" style="width:auto;padding:7px 10px;margin:0;font-size:10px" onclick="syncDT()">AUTO</button>
        </div>
    </div>

    <!-- TARGET (optional) -->
    <div class="p">
        <div class="ph">TARGET (OPTIONAL)</div>
        <input id="tgLat" placeholder="Target Lat" type="number" step="any">
        <input id="tgLon" placeholder="Target Lon" type="number" step="any">
        <input id="tgRad" placeholder="Radius m (default 10)" type="number" step="any" value="10">
        <div style="font-size:10px;color:#1e3a4a;margin-top:3px">Buzzer GPIO26 + LED on arrival</div>
    </div>

    <!-- ORIGIN COORDINATES INPUT -->
    <button class="btn bc" onclick="phoneGPS()">USE PHONE GPS FOR ORIGIN</button>

    <!-- EXTENDED CALIBRATION WIZARD (collapsible, appears after 60s cal) -->
    <div class="p" id="extCalPanel">
        <div class="ph">EXTENDED CALIBRATION <span style="color:#1e3a4a;font-size:9px">ALL OPTIONAL — RUN AFTER 60s CAL</span></div>
        <div id="calWiz" style="display:none">
            <div id="calStepTitle" style="color:#00cfff;font-size:11px;margin-bottom:4px;font-weight:bold"></div>
            <div id="calStepInstr" style="color:#b8dce8;font-size:10px;line-height:1.5;margin-bottom:6px"></div>
            <div class="cb"><div class="cf" id="calWizProg" style="width:0%"></div></div>
            <div id="calStepBtns" style="display:flex;gap:4px;margin-top:4px;flex-wrap:wrap"></div>
            <div id="calStepResult" style="font-size:10px;color:#00ff88;margin-top:4px;min-height:14px"></div>
        </div>
        <div id="calLaunchers">
            <div style="display:grid;grid-template-columns:1fr 1fr;gap:4px">
                <button class="btn bc" style="padding:7px;margin:0;font-size:10px" onclick="startCal('accel')">1. TILT CAL (6 POS)</button>
                <button class="btn bc" style="padding:7px;margin:0;font-size:10px" onclick="startCal('mag')">2. COMPASS CAL</button>
                <button class="btn bc" style="padding:7px;margin:0;font-size:10px" onclick="startCal('vel')">3. SPEED CAL (1m)</button>
                <button class="btn bc" style="padding:7px;margin:0;font-size:10px" onclick="startCal('baro')">4. ALTITUDE CAL</button>
                <button class="btn bc" style="padding:7px;margin:0;font-size:10px;grid-column:span 2" onclick="startCal('gps')">5. GPS POSITION SYNC (needs satellite fix)</button>
            </div>
            <div id="calResults" style="font-size:9px;color:#1e3a4a;margin-top:5px;line-height:1.6">Tap any step above to calibrate</div>
            <div style="font-size:9px;color:#1e3a4a;margin-top:4px;border-top:1px solid #0c3050;padding-top:4px;line-height:1.5">
                TIP: Step 1 (Tilt) and Step 2 (Compass) give the biggest improvement.<br>
                All steps are optional — skip any you cannot do.<br>
                You can redo any step at any time before pressing START INS.
            </div>
        </div>
    </div>

    <button class="btn bc" style="margin-top:0" onclick="openMaps()">PICK IN GOOGLE MAPS</button>
    <input id="la" placeholder="Origin Latitude" type="number" step="any">
    <input id="lo" placeholder="Origin Longitude" type="number" step="any">
    <input id="al" placeholder="Altitude m MSL" type="number" step="any">
    <hr>
    <button class="btn bg" onclick="startINS()">START INS</button>
    <div id="msg"></div>
</div>

<!-- ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ -->
<!--  DA – DASHBOARD PANEL (visible when INS is running)                -->
<!-- ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ -->
<div id="DA" style="display:none">

    <!-- Status bar (top) -->
    <div class="sb">
        <span id="sbd" class="bd wt">INIT</span>
        <span id="gst" style="color:#ffb800">GPS:--</span>
        <span id="clk" style="color:#00cfff">--:--:--</span>
        <span id="upt" style="color:#1e3a4a">T+0s</span>
        <button class="btn br" id="rB" style="width:auto;padding:3px 9px;margin:0;font-size:10px" onclick="doReset()">RESET</button>
    </div>

    <!-- LEDs -->
    <div class="leds">
        <span class="led" id="lG">GPS</span>
        <span class="led" id="lS">ZUPT</span>
        <span class="led" id="lD">DRIFT</span>
    </div>

    <!-- Bump test & View map buttons -->
    <div class="g2" style="margin-bottom:4px">
        <button class="btn ba" id="bumpBtn" onclick="startBumpTest()" style="margin:0;padding:8px;font-size:10px">BUMP TEST</button>
        <button class="btn bc" onclick="viewMap()" style="margin:0;padding:8px;font-size:10px">VIEW MAP</button>
    </div>

    <!-- Horizon + Compass canvases -->
    <div class="g2" style="margin-bottom:4px">
        <div class="p" style="padding:4px"><div class="ph">HORIZON</div><canvas id="cH" width="148" height="112"></canvas></div>
        <div class="p" style="padding:4px"><div class="ph">COMPASS</div><canvas id="cC" width="148" height="112"></canvas></div>
    </div>

    <!-- Turn rate indicator -->
    <div class="p" style="padding:4px;margin-bottom:4px"><div class="ph">TURN RATE</div><canvas id="cT" width="290" height="36"></canvas></div>

    <!-- Live trail map -->
    <div class="p" style="padding:4px;margin-bottom:4px">
        <div class="ph" style="display:flex;justify-content:space-between">
            <span>LIVE TRAIL MAP</span>
            <span style="display:flex;gap:6px">
                <button onclick="mapZoomIn()" style="background:transparent;color:#00cfff;border:1px solid #0c3050;border-radius:2px;padding:1px 7px;cursor:pointer;font-size:11px">+</button>
                <button onclick="mapZoomOut()" style="background:transparent;color:#00cfff;border:1px solid #0c3050;border-radius:2px;padding:1px 7px;cursor:pointer;font-size:11px">-</button>
                <button onclick="mapClear()" style="background:transparent;color:#ff3355;border:1px solid #ff3355;border-radius:2px;padding:1px 7px;cursor:pointer;font-size:10px">CLR</button>
            </span>
        </div>
        <canvas id="cMap" width="290" height="200" style="width:100%;border:1px solid #0c3050;border-radius:2px;margin-top:3px"></canvas>
        <div id="mapScale" style="font-size:9px;color:#1e3a4a;text-align:right;margin-top:2px">scale: --</div>
    </div>

    <!-- Target panel (shown if target set) -->
    <div id="tgtPanel" class="p" style="margin-bottom:4px;display:none">
        <div class="ph">TARGET</div>
        <div class="rw"><span class="lb">DISTANCE</span><span class="vl va" id="tgtDist">--</span></div>
        <div class="rw"><span class="lb">STATUS</span><span class="vl" id="tgtStat">EN ROUTE</span></div>
    </div>

    <!-- Position / Velocity / Attitude / Environment -->
    <div class="g2">
        <div class="p"><div class="ph">POSITION</div>
            <div class="rw"><span class="lb">LAT</span><span class="vl vc" id="dA" style="font-size:10px">--</span></div>
            <div class="rw"><span class="lb">LON</span><span class="vl vc" id="dO" style="font-size:10px">--</span></div>
            <div class="rw"><span class="lb">ALT MSL</span><span class="vl" id="dL">--</span></div>
            <div class="rw"><span class="lb">BARO</span><span class="vl vc" id="dB">--</span></div>
            <div class="rw"><span class="lb">GPS ERR</span><span class="vl va" id="dE">--</span></div>
        </div>
        <div class="p"><div class="ph" style="display:flex;justify-content:space-between;align-items:center"><span>VELOCITY</span>
            <button id="unitBtn" onclick="togUnit()" style="background:transparent;color:#00cfff;border:1px solid #0c3050;border-radius:2px;padding:1px 7px;cursor:pointer;font-size:9px">m/s</button>
        </div>
            <div class="rw"><span class="lb">NORTH</span><span class="vl" id="dVn">--</span></div>
            <div class="rw"><span class="lb">EAST</span><span class="vl" id="dVe">--</span></div>
            <div class="rw"><span class="lb">DOWN</span><span class="vl vc" id="dVd">--</span></div>
            <div class="rw"><span class="lb">H-SPD</span><span class="vl" id="dSp">--</span></div>
            <div class="rw"><span class="lb">3D-SPD</span><span class="vl" id="dS3">--</span></div>
        </div>
        <div class="p"><div class="ph">ATTITUDE</div>
            <div class="rw"><span class="lb">ROLL</span><span class="vl" id="dRo">--</span></div>
            <div class="rw"><span class="lb">PITCH</span><span class="vl" id="dPi">--</span></div>
            <div class="rw"><span class="lb">YAW</span><span class="vl vc" id="dYa">--</span></div>
            <div class="rw"><span class="lb">R-RATE</span><span class="vl" id="dRr">--</span></div>
            <div class="rw"><span class="lb">P-RATE</span><span class="vl" id="dPr">--</span></div>
        </div>
        <div class="p"><div class="ph">ENVIRONMENT</div>
            <div class="rw"><span class="lb">TEMP</span><span class="vl" id="dTe">--</span></div>
            <div class="rw"><span class="lb">hPa</span><span class="vl vc" id="dHp">--</span></div>
            <div class="rw"><span class="lb">mmHg</span><span class="vl" id="dMh">--</span></div>
            <div class="rw"><span class="lb">STATIC</span><span class="vl" id="dSt">--</span></div>
        </div>
    </div>

    <!-- ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ -->
    <!--  EKF DIAGNOSTICS PANEL (collapsible)                         -->
    <!-- ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ -->
    <div class="p" style="margin-top:4px">
        <div class="ph" style="display:flex;justify-content:space-between;cursor:pointer" onclick="toggleEKF()">
            <span>🔬 EKF DIAGNOSTICS <span style="color:#1e3a4a;font-size:9px">(click to expand)</span></span>
            <span id="ekfHealthBadge" style="color:#00ff88">● HEALTHY</span>
        </div>
        <div id="ekfPanel" style="display:none;font-size:10px">

            <!-- State Vector (15 values) -->
            <div style="margin:4px 0"><span class="lb">STATE VECTOR</span>
                <div style="display:grid;grid-template-columns:1fr 1fr 1fr 1fr 1fr;gap:2px;margin-top:2px">
                    <div><span class="lb">pN</span> <span id="s0" class="vc">0</span></div>
                    <div><span class="lb">pE</span> <span id="s1" class="vc">0</span></div>
                    <div><span class="lb">pD</span> <span id="s2" class="vc">0</span></div>
                    <div><span class="lb">vN</span> <span id="s3" class="vc">0</span></div>
                    <div><span class="lb">vE</span> <span id="s4" class="vc">0</span></div>
                    <div><span class="lb">vD</span> <span id="s5" class="vc">0</span></div>
                    <div><span class="lb">φ</span> <span id="s6" class="vc">0</span></div>
                    <div><span class="lb">θ</span> <span id="s7" class="vc">0</span></div>
                    <div><span class="lb">ψ</span> <span id="s8" class="vc">0</span></div>
                    <div><span class="lb">baX</span> <span id="s9" class="va">0</span></div>
                    <div><span class="lb">baY</span> <span id="s10" class="va">0</span></div>
                    <div><span class="lb">baZ</span> <span id="s11" class="va">0</span></div>
                    <div><span class="lb">bgX</span> <span id="s12" class="va">0</span></div>
                    <div><span class="lb">bgY</span> <span id="s13" class="va">0</span></div>
                    <div><span class="lb">bgZ</span> <span id="s14" class="va">0</span></div>
                </div>
            </div>

            <!-- Covariance Diagonal -->
            <div style="margin:4px 0"><span class="lb">COVARIANCE (diagonal)</span>
                <div style="display:grid;grid-template-columns:1fr 1fr 1fr 1fr 1fr;gap:2px;margin-top:2px;font-size:8px">
                    <div><span id="c0">0</span></div><div><span id="c1">0</span></div><div><span id="c2">0</span></div>
                    <div><span id="c3">0</span></div><div><span id="c4">0</span></div><div><span id="c5">0</span></div>
                    <div><span id="c6">0</span></div><div><span id="c7">0</span></div><div><span id="c8">0</span></div>
                    <div><span id="c9">0</span></div><div><span id="c10">0</span></div><div><span id="c11">0</span></div>
                    <div><span id="c12">0</span></div><div><span id="c13">0</span></div><div><span id="c14">0</span></div>
                </div>
            </div>

            <!-- Innovations & Kalman Gains -->
            <div class="g2" style="margin:4px 0">
                <div><span class="lb">GPS INNOV</span><br><span id="innGPS" class="vl">0, 0</span></div>
                <div><span class="lb">BARO INNOV</span><br><span id="innBaro" class="vl">0</span></div>
                <div><span class="lb">ZUPT INNOV</span><br><span id="innZUPT" class="vl">0, 0, 0</span></div>
                <div><span class="lb">KALMAN GAINS</span><br><span id="kgDisplay" class="vl" style="font-size:9px">GPS:0 Baro:0 ZUPT:0</span></div>
            </div>

            <!-- ZUPT Details -->
            <div class="g2" style="margin:4px 0">
                <div><span class="lb">ZUPT VARIANCE</span><br><span id="zuptVar" class="vl">--</span></div>
                <div><span class="lb">ZUPT MEAN</span><br><span id="zuptMean" class="vl">--</span></div>
                <div><span class="lb">CONFIDENCE</span><br><span id="zuptConf" class="vl">--</span></div>
                <div><span class="lb">STATIC COUNT</span><br><span id="zuptCnt" class="vl">--</span></div>
            </div>
        </div>
    </div>

    <!-- GPS Receiver Details -->
    <div class="p" style="margin-top:4px">
        <div class="ph">GPS RECEIVER</div>
        <div id="gNC" style="display:none"><span class="vl vr">NOT CONNECTED (no serial)</span></div>
        <div id="gNF" style="display:none"><span class="vl va">CONNECTED — searching satellites...</span></div>
        <div id="gFX" style="display:none">
            <div class="g2">
                <div>
                    <div class="rw"><span class="lb">SATS</span><span class="vl" id="gSt">--</span></div>
                    <div class="rw"><span class="lb">HDOP</span><span class="vl" id="gHd">--</span></div>
                    <div class="rw"><span class="lb">SPD</span><span class="vl" id="gSp">--</span></div>
                </div>
                <div>
                    <div class="rw"><span class="lb">UTC</span><span class="vl vc" id="gUt">--</span></div>
                    <div class="rw"><span class="lb">BST+6</span><span class="vl" id="gLc">--</span></div>
                    <div class="rw"><span class="lb">COURSE</span><span class="vl" id="gCr">--</span></div>
                </div>
            </div>
            <div class="rw"><span class="lb">RAW LAT</span><span class="vl vc" id="gLa">--</span></div>
            <div class="rw"><span class="lb">RAW LON</span><span class="vl vc" id="gLo">--</span></div>
        </div>
    </div>

    <!-- Bump Test Result (initially hidden) -->
    <div id="bumpResult" class="p" style="display:none;margin-top:4px">
        <div class="ph">BUMP TEST RESULT</div>
        <div class="rw"><span class="lb">DRIFT ERROR</span><span class="vl va" id="bumpError">--</span><span class="lb"> m</span>
            <span class="vl" id="bumpRating" style="margin-left:8px"></span></div>
        <button class="btn bc" onclick="closeBumpResult()" style="padding:5px;margin-top:4px;font-size:10px">CLOSE</button>
    </div>
</div>

<!-- ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ -->
<!--  JAVASCRIPT – The brains of the web UI                              -->
<!-- ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ -->
<script>
// ──────────────────────────────────────────────────────────────────────
// GLOBAL STATE VARIABLES
// ──────────────────────────────────────────────────────────────────────

var t0 = null,            // Timestamp when INS started (for uptime)
    calPoll = null,       // Interval handle for calibration polling
    rT = null,            // Interval handle for reset countdown
    clkInt = null,        // Interval handle for clock updates
    gpsSynced = false,    // Whether GPS time has been synced to clock
    cLat = 0, cLon = 0,   // Current fused latitude/longitude
    bumpState = 0;        // 0 = idle, 1 = waiting for return

// Unit conversion: index into arrays
var uIdx = 0,
    uN = ['m/s', 'km/h', 'mph', 'kn'],
    uM = [1, 3.6, 2.23694, 1.94384];

// Origin and target (set by user)
var originLat = 0, originLon = 0, originSet = false,
    targetLat2 = 0, targetLon2 = 0, targetSet2 = false, targetRad2 = 10;

// Trail map data (local NED coordinates)
var trail = [],           // Array of {n, e} points
    mapScale = 5,         // Pixels per metre (zooming)
    mapOriginN = 0, mapOriginE = 0;  // Offset for centering

// ──────────────────────────────────────────────────────────────────────
// MAP FUNCTIONS (zoom, clear, draw)
// ──────────────────────────────────────────────────────────────────────

function mapZoomIn() {
    mapScale = Math.min(mapScale * 1.5, 200);
    drawMap();
}
function mapZoomOut() {
    mapScale = Math.max(mapScale / 1.5, 0.1);
    drawMap();
}
function mapClear() {
    trail = [];
    mapOriginN = 0;
    mapOriginE = 0;
    drawMap();
}
function addTrailPoint(n, e) {
    trail.push({n: n, e: e});
    if (trail.length > 2000) trail.shift();  // Limit trail length
}

function drawMap() {
    var cv = document.getElementById('cMap'),
        c = cv.getContext('2d'),
        W = cv.offsetWidth || 290,
        H = 200;
    cv.width = W;
    cv.height = H;
    c.fillStyle = '#060c10';
    c.fillRect(0, 0, W, H);

    var cx2 = W / 2, cy2 = H / 2,
        gridM = 10,
        gridPx = gridM * mapScale;  // 10 metre grid spacing in pixels

    // Draw grid lines
    c.strokeStyle = '#0c1a28';
    c.lineWidth = 1;
    for (var x = cx2 % gridPx; x < W; x += gridPx) {
        c.beginPath(); c.moveTo(x, 0); c.lineTo(x, H); c.stroke();
    }
    for (var y = cy2 % gridPx; y < H; y += gridPx) {
        c.beginPath(); c.moveTo(0, y); c.lineTo(W, y); c.stroke();
    }

    // Draw crosshair at origin
    c.strokeStyle = '#1e3a4a';
    c.beginPath();
    c.moveTo(cx2 - 8, cy2);
    c.lineTo(cx2 + 8, cy2);
    c.moveTo(cx2, cy2 - 8);
    c.lineTo(cx2, cy2 + 8);
    c.stroke();

    // Draw target (if set)
    if (targetSet2) {
        var tdN = (targetLat2 - originLat) * 111111,
            tdE = (targetLon2 - originLon) * 111111 * Math.cos(originLat * Math.PI / 180);
        var tx = cx2 + (tdE - mapOriginE) * mapScale,
            ty = cy2 - (tdN - mapOriginN) * mapScale;
        c.beginPath();
        c.arc(tx, ty, 5, 0, 2 * Math.PI);
        c.fillStyle = '#ff3355';
        c.fill();
        c.strokeStyle = 'rgba(255,51,85,0.3)';
        c.beginPath();
        c.arc(tx, ty, targetRad2 * mapScale, 0, 2 * Math.PI);
        c.stroke();
    }

    // Draw trail (path line)
    if (trail.length > 1) {
        c.beginPath();
        c.strokeStyle = '#00ff88';
        c.lineWidth = 1.5;
        c.lineJoin = 'round';
        var p0 = trail[0];
        c.moveTo(cx2 + (p0.e - mapOriginE) * mapScale, cy2 - (p0.n - mapOriginN) * mapScale);
        for (var i = 1; i < trail.length; i++) {
            var p = trail[i];
            c.lineTo(cx2 + (p.e - mapOriginE) * mapScale, cy2 - (p.n - mapOriginN) * mapScale);
        }
        c.stroke();
    }

    // Draw current position (last point + heading vector)
    if (trail.length > 0) {
        var lp = trail[trail.length - 1],
            px = cx2 + (lp.e - mapOriginE) * mapScale,
            py = cy2 - (lp.n - mapOriginN) * mapScale;
        c.beginPath();
        c.arc(px, py, 4, 0, 2 * Math.PI);
        c.fillStyle = '#00cfff';
        c.fill();
        var hdRad = (parseFloat(document.getElementById('dYa').textContent) || 0) * Math.PI / 180;
        c.strokeStyle = '#00cfff';
        c.lineWidth = 1.5;
        c.beginPath();
        c.moveTo(px, py);
        c.lineTo(px + Math.sin(hdRad) * 12, py - Math.cos(hdRad) * 12);
        c.stroke();
    }

    // Update scale label
    document.getElementById('mapScale').textContent = 'scale: ' + Math.max(1, Math.round(50 / mapScale)) + 'm / 50px';
}

// ──────────────────────────────────────────────────────────────────────
// CLOCK FUNCTIONS (auto‑sync from browser)
// ──────────────────────────────────────────────────────────────────────

function p2(n) { return n < 10 ? '0' + n : '' + n; }

function startClock(h, m, s) {
    if (clkInt) clearInterval(clkInt);
    var tot = h * 3600 + m * 60 + s;
    clkInt = setInterval(function() {
        tot++;
        document.getElementById('clk').textContent =
            p2(Math.floor(tot / 3600) % 24) + ':' +
            p2(Math.floor((tot % 3600) / 60)) + ':' +
            p2(tot % 60);
    }, 1000);
}

function syncDT() {
    var n = new Date();
    startClock(n.getHours(), n.getMinutes(), n.getSeconds());
    document.getElementById('dtI').value =
        n.getFullYear() + p2(n.getMonth() + 1) + p2(n.getDate()) + '_' +
        p2(n.getHours()) + p2(n.getMinutes()) + p2(n.getSeconds());
}

// ──────────────────────────────────────────────────────────────────────
// PAGE LOAD – determine whether to show Setup or Dashboard
// ──────────────────────────────────────────────────────────────────────

window.addEventListener('load', function() {
    syncDT();
    fetch('/status')
        .then(r => r.json())
        .then(d => {
            document.getElementById('LD').style.display = 'none';
            if (d.running) {
                // INS already running – go straight to dashboard
                document.getElementById('DA').style.display = 'block';
                t0 = Date.now() - d.uptime;
                setInterval(poll, 400);
            } else {
                // Show setup panel and start calibration polling
                document.getElementById('SU').style.display = 'block';
                startCalPoll();
            }
        })
        .catch(function() {
            // Fallback if ESP32 not responding
            document.getElementById('LD').style.display = 'none';
            document.getElementById('SU').style.display = 'block';
            startCalPoll();
        });
});

// ──────────────────────────────────────────────────────────────────────
// GPS STATUS UPDATE (setup panel)
// ──────────────────────────────────────────────────────────────────────

function updGpsSetup(d) {
    var el = document.getElementById('gMod');
    if (d.conn) {
        el.textContent = 'SERIAL OK';
        el.className = 'vl vc';
    } else {
        el.textContent = 'NO SERIAL DATA';
        el.className = 'vl vr';
    }
    document.getElementById('gBd').textContent = d.baud + ' swap:' + (d.swap ? 'Y' : 'N');
    document.getElementById('gBy').textContent = d.bytes;
    var fx = document.getElementById('gFx');
    if (d.fix) {
        fx.textContent = 'YES — satellites locked';
        fx.className = 'vl vc';
    } else if (d.conn) {
        fx.textContent = 'NO — need open sky';
        fx.className = 'vl va';
    } else {
        fx.textContent = 'NO MODULE';
        fx.className = 'vl vr';
    }
    document.getElementById('gSa').textContent = d.sats;
    document.getElementById('gLn').textContent = d.line || 'waiting $GPGGA...';
}

// ──────────────────────────────────────────────────────────────────────
// CALIBRATION POLLING (60‑second auto‑level progress)
// ──────────────────────────────────────────────────────────────────────

function startCalPoll() {
    calPoll = setInterval(function() {
        // Get calibration progress
        fetch('/caldata')
            .then(r => r.json())
            .then(d => {
                var p = Math.min(100, Math.round(d.p / 3000 * 100));
                document.getElementById('cf').style.width = p + '%';
                document.getElementById('cs').textContent = d.done ? 'COMPLETE' : 'RUNNING ' + p + '%';
                if (d.done) {
                    document.getElementById('ws').style.display = 'none';
                    document.getElementById('cs').className = 'vl';
                    clearInterval(calPoll);
                }
            })
            .catch(function() {});

        // Get environment data (baro alt, temp, pressure)
        fetch('/env')
            .then(r => r.json())
            .then(d => {
                document.getElementById('bL').textContent = d.al.toFixed(1) + ' m';
                document.getElementById('tL').textContent = d.tc.toFixed(1) + ' C';
                document.getElementById('pL').textContent = (d.pp / 100).toFixed(1) + ' hPa';
            })
            .catch(function() {});

        // Get GPS status
        fetch('/gps_status')
            .then(r => r.json())
            .then(updGpsSetup)
            .catch(function() {});
    }, 1000);
}

// ──────────────────────────────────────────────────────────────────────
// PHONE GPS – uses browser geolocation to fill origin fields
// ──────────────────────────────────────────────────────────────────────

function phoneGPS() {
    if (!navigator.geolocation) {
        document.getElementById('msg').textContent = 'No geolocation';
        return;
    }
    document.getElementById('msg').textContent = 'Acquiring...';
    navigator.geolocation.getCurrentPosition(
        function(p) {
            document.getElementById('la').value = p.coords.latitude.toFixed(8);
            document.getElementById('lo').value = p.coords.longitude.toFixed(8);
            if (p.coords.altitude != null)
                document.getElementById('al').value = p.coords.altitude.toFixed(1);
            document.getElementById('msg').textContent = 'GPS acquired.';
        },
        function(e) {
            document.getElementById('msg').textContent = 'Err:' + e.message;
        }, {
            enableHighAccuracy: true,
            timeout: 15000
        }
    );
}

function openMaps() {
    window.open('https://maps.google.com', '_blank');
    document.getElementById('msg').textContent = 'Long-press in Maps, copy coords.';
}

// ──────────────────────────────────────────────────────────────────────
// START INS – POST origin and target to ESP32
// ──────────────────────────────────────────────────────────────────────

function startINS() {
    var la = document.getElementById('la').value.trim(),
        lo = document.getElementById('lo').value.trim(),
        al = document.getElementById('al').value.trim();
    if (!la || !lo || !al) {
        document.getElementById('msg').textContent = 'Fill origin fields.';
        return;
    }
    originLat = parseFloat(la);
    originLon = parseFloat(lo);
    originSet = true;

    var tgLa = document.getElementById('tgLat').value.trim(),
        tgLo = document.getElementById('tgLon').value.trim(),
        tgRd = document.getElementById('tgRad').value.trim() || '10';
    var body = 'lat=' + la + '&lon=' + lo + '&alt=' + al;
    if (tgLa && tgLo) {
        targetLat2 = parseFloat(tgLa);
        targetLon2 = parseFloat(tgLo);
        targetSet2 = true;
        targetRad2 = parseFloat(tgRd) || 10;
        body += '&tgLat=' + tgLa + '&tgLon=' + tgLo + '&tgRad=' + tgRd;
        document.getElementById('tgtPanel').style.display = 'block';
    }

    document.getElementById('msg').textContent = 'Starting...';
    fetch('/start', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: body
    })
    .then(r => r.text())
    .then(t => {
        if (t === 'OK') {
            if (calPoll) clearInterval(calPoll);
            document.getElementById('SU').style.display = 'none';
            document.getElementById('DA').style.display = 'block';
            t0 = Date.now();
            setInterval(poll, 400);
        } else {
            document.getElementById('msg').textContent = 'Err:' + t;
        }
    });
}

// ──────────────────────────────────────────────────────────────────────
// RESET – with 5‑second countdown
// ──────────────────────────────────────────────────────────────────────

function doReset() {
    if (rT) {
        clearInterval(rT);
        rT = null;
        document.getElementById('rB').textContent = 'RESET';
        return;
    }
    document.getElementById('rB').textContent = 'CANCEL(5s)';
    var cnt = 5;
    rT = setInterval(function() {
        cnt--;
        if (cnt <= 0) {
            clearInterval(rT);
            rT = null;
            fetch('/reset', { method: 'POST' })
                .then(function() { location.reload(); });
        } else {
            document.getElementById('rB').textContent = 'CANCEL(' + cnt + 's)';
        }
    }, 1000);
}

// ──────────────────────────────────────────────────────────────────────
// UNIT TOGGLE (m/s ↔ km/h ↔ mph ↔ kn)
// ──────────────────────────────────────────────────────────────────────

function togUnit() {
    uIdx = (uIdx + 1) % 4;
    document.getElementById('unitBtn').textContent = uN[uIdx];
}
function fV(v) {
    return (v * uM[uIdx]).toFixed(3) + ' ' + uN[uIdx];
}

// ──────────────────────────────────────────────────────────────────────
// BUMP TEST – measure drift error
// ──────────────────────────────────────────────────────────────────────

function startBumpTest() {
    var btn = document.getElementById('bumpBtn');
    if (bumpState === 0) {
        fetch('/bump_start', { method: 'POST' })
            .then(r => r.text())
            .then(t => {
                if (t === 'OK') {
                    bumpState = 1;
                    btn.textContent = 'RETURN TO START';
                    btn.style.color = '#00ff88';
                    btn.style.borderColor = '#00ff88';
                    document.getElementById('msg').textContent = 'Move ~1m then press RETURN';
                }
            });
    } else {
        fetch('/bump_return', { method: 'POST' })
            .then(r => r.json())
            .then(d => {
                bumpState = 0;
                btn.textContent = 'BUMP TEST';
                btn.style.color = '';
                btn.style.borderColor = '';
                document.getElementById('bumpResult').style.display = 'block';
                document.getElementById('bumpError').textContent = d.error.toFixed(3);
                var e = d.error,
                    rat = e < 0.1 ? 'EXCELLENT' : e < 0.3 ? 'GOOD' : e < 0.5 ? 'ACCEPTABLE' : 'FIX ZUPT/Q';
                document.getElementById('bumpRating').textContent = rat;
                document.getElementById('bumpRating').className = 'vl' + (e < 0.3 ? ' vc' : e < 0.5 ? ' va' : ' vr');
            })
            .catch(function() {
                bumpState = 0;
                btn.textContent = 'BUMP TEST';
            });
    }
}
function closeBumpResult() {
    document.getElementById('bumpResult').style.display = 'none';
}

// ──────────────────────────────────────────────────────────────────────
// POLL – fetch /data every 400ms and update dashboard
// ──────────────────────────────────────────────────────────────────────

function poll() {
    fetch('/data')
        .then(r => r.json())
        .then(upd)
        .catch(function() {});
    if (t0)
        document.getElementById('upt').textContent = 'T+' + Math.floor((Date.now() - t0) / 1000) + 's';
}

// ──────────────────────────────────────────────────────────────────────
// upd(d) – main UI update from JSON data
// ──────────────────────────────────────────────────────────────────────

function upd(d) {
    // ─── Position & Trail ───
    cLat = d.la;
    cLon = d.lo;
    if (originSet) addTrailPoint(d.pn, d.pe);
    drawMap();

    // ─── Position Panel ───
    document.getElementById('dA').textContent = d.la.toFixed(7);
    document.getElementById('dO').textContent = d.lo.toFixed(7);
    document.getElementById('dL').textContent = d.al.toFixed(1) + ' m';
    document.getElementById('dB').textContent = d.ba.toFixed(1) + ' m';
    document.getElementById('dE').textContent = d.ge.toFixed(1) + ' m';

    // ─── Velocity Panel (with unit conversion) ───
    document.getElementById('dVn').textContent = fV(d.vn);
    document.getElementById('dVe').textContent = fV(d.ve);
    document.getElementById('dVd').textContent = fV(d.vd);
    document.getElementById('dSp').textContent = fV(Math.sqrt(d.vn * d.vn + d.ve * d.ve));
    document.getElementById('dS3').textContent = fV(Math.sqrt(d.vn * d.vn + d.ve * d.ve + d.vd * d.vd));

    // ─── Attitude ───
    document.getElementById('dRo').textContent = d.ro.toFixed(1) + ' deg';
    document.getElementById('dPi').textContent = d.pi.toFixed(1) + ' deg';
    document.getElementById('dYa').textContent = d.ya.toFixed(1) + ' deg';
    document.getElementById('dRr').textContent = d.gx.toFixed(2) + ' d/s';
    document.getElementById('dPr').textContent = d.gy.toFixed(2) + ' d/s';

    // ─── Environment ───
    document.getElementById('dTe').textContent = d.tc.toFixed(1) + ' C';
    document.getElementById('dHp').textContent = (d.pp / 100).toFixed(1);
    document.getElementById('dMh').textContent = (d.pp * 0.00750062).toFixed(1);

    // ─── Static status ───
    var stEl = document.getElementById('dSt');
    if (d.st) {
        stEl.textContent = 'YES';
        stEl.className = 'vl vc';
    } else {
        stEl.textContent = 'NO';
        stEl.className = 'vl va';
    }

    // ─── LEDs ───
    document.getElementById('lG').className = 'led' + (d.lg ? ' on' : '');
    document.getElementById('lS').className = 'led' + (d.ls ? ' on' : '');
    document.getElementById('lD').className = 'led' + (d.ld ? ' on' : '');

    // ─── Status bar (GPS/INS mode) ───
    var sb = document.getElementById('sbd'),
        gs = document.getElementById('gst');
    if (d.jam) {
        sb.textContent = 'GPS JAMMED';
        sb.className = 'bd jm';
        gs.textContent = 'GPS:JAM';
        gs.style.color = '#ff3355';
    } else if (d.fx && d.gfix) {
        sb.textContent = 'GPS+INS';
        sb.className = 'bd ok';
        gs.textContent = 'GPS:FIX';
        gs.style.color = '#00ff88';
    } else if (d.gconn) {
        sb.textContent = 'INS ONLY';
        sb.className = 'bd wt';
        gs.textContent = 'GPS:SRCH';
        gs.style.color = '#ffb800';
    } else {
        sb.textContent = 'INS ONLY';
        sb.className = 'bd wt';
        gs.textContent = 'GPS:OFF';
        gs.style.color = '#ff3355';
    }

    // ─── GPS Receiver Panel ───
    var nc = document.getElementById('gNC'),
        nf = document.getElementById('gNF'),
        fx = document.getElementById('gFX');
    if (!d.gconn) {
        nc.style.display = 'block';
        nf.style.display = 'none';
        fx.style.display = 'none';
    } else if (!d.gfix) {
        nc.style.display = 'none';
        nf.style.display = 'block';
        fx.style.display = 'none';
    } else {
        nc.style.display = 'none';
        nf.style.display = 'none';
        fx.style.display = 'block';
        document.getElementById('gSt').textContent = d.gs;
        document.getElementById('gHd').textContent = d.gh.toFixed(1);
        document.getElementById('gSp').textContent = d.gsk.toFixed(1) + ' km/h';
        document.getElementById('gCr').textContent = d.gco.toFixed(1) + ' deg';
        var ut = d.gut,
            uh = Math.floor(ut / 10000),
            um2 = Math.floor((ut % 10000) / 100),
            us2 = ut % 100;
        document.getElementById('gUt').textContent = p2(uh) + ':' + p2(um2) + ':' + p2(us2) + ' UTC';
        document.getElementById('gLc').textContent = p2((uh + 6) % 24) + ':' + p2(um2) + ':' + p2(us2) + ' BST';
        document.getElementById('gLa').textContent = d.glat ? d.glat.toFixed(7) : '--';
        document.getElementById('gLo').textContent = d.glon ? d.glon.toFixed(7) : '--';
        if (!gpsSynced && d.gfix) {
            gpsSynced = true;
            startClock((uh + 6) % 24, um2, us2);
        }
    }

    // ─── Target Panel ───
    if (d.tset) {
        document.getElementById('tgtDist').textContent = d.tdist.toFixed(1) + ' m';
        document.getElementById('tgtStat').textContent = d.treach ? 'ARRIVED!' : 'EN ROUTE';
        document.getElementById('tgtStat').className = 'vl' + (d.treach ? ' vc' : ' va');
    }

    // ─── Draw instruments ───
    drawH(d.ro, d.pi);
    drawC(d.hd);
    drawT(d.gz);

    // ─── EKF DIAGNOSTICS ───
    if (d.state) {
        for (var i = 0; i < 15; i++) {
            var el = document.getElementById('s' + i);
            if (el) {
                var val = d.state[i];
                if (i >= 9) el.textContent = val.toFixed(4);
                else if (i >= 6) el.textContent = val.toFixed(1) + '°';
                else el.textContent = val.toFixed(2);
                el.style.color = i < 3 ? '#00cfff' : i < 6 ? '#00ff88' : i < 9 ? '#ff88ff' : '#ffb800';
            }
        }
    }
    if (d.cov) {
        for (var i = 0; i < 15; i++) {
            var el = document.getElementById('c' + i);
            if (el) {
                var val = d.cov[i];
                el.textContent = val.toFixed(4);
                el.style.color = val < 0.1 ? '#00ff88' : val < 1.0 ? '#ffb800' : '#ff3355';
            }
        }
    }
    if (d.innGPS) document.getElementById('innGPS').textContent = d.innGPS[0].toFixed(3) + ', ' + d.innGPS[1].toFixed(3);
    if (d.innBaro !== undefined) document.getElementById('innBaro').textContent = d.innBaro.toFixed(3);
    if (d.innZUPT) document.getElementById('innZUPT').textContent = d.innZUPT[0].toFixed(3) + ', ' + d.innZUPT[1].toFixed(3) + ', ' + d.innZUPT[2].toFixed(3);
    if (d.kgPS) {
        document.getElementById('kgDisplay').textContent =
            'GPS:' + d.kgPS[0].toFixed(2) + '/' + d.kgPS[1].toFixed(2) +
            ' Baro:' + d.kgBaro.toFixed(2) +
            ' ZUPT:' + d.kgZUPT[0].toFixed(2) + '/' + d.kgZUPT[1].toFixed(2) + '/' + d.kgZUPT[2].toFixed(2);
    }
    if (d.zuptVar !== undefined) {
        document.getElementById('zuptVar').textContent = d.zuptVar.toFixed(5);
        document.getElementById('zuptVar').style.color = d.zuptVar < 0.005 ? '#00ff88' : '#ffb800';
        document.getElementById('zuptMean').textContent = d.zuptMean.toFixed(3) + ' m/s²';
        document.getElementById('zuptConf').textContent = d.zuptConf.toFixed(0) + '%';
        document.getElementById('zuptConf').style.color = d.zuptConf > 80 ? '#00ff88' : '#ffb800';
        document.getElementById('zuptCnt').textContent = d.zuptCnt;
    }
    if (d.ekfHealth !== undefined) {
        var badge = document.getElementById('ekfHealthBadge');
        if (d.ekfHealth > 95) { badge.textContent = '● HEALTHY'; badge.style.color = '#00ff88'; }
        else if (d.ekfHealth > 80) { badge.textContent = '● DEGRADED'; badge.style.color = '#ffb800'; }
        else { badge.textContent = '● UNSTABLE'; badge.style.color = '#ff3355'; }
    }
}

// ──────────────────────────────────────────────────────────────────────
// VIEW MAP – open Google Maps with current position
// ──────────────────────────────────────────────────────────────────────

function viewMap() {
    if (!cLat && !cLon) { alert('No position yet.'); return; }
    window.open('https://maps.google.com/?q=' + cLat.toFixed(7) + ',' + cLon.toFixed(7), '_blank');
}

// ──────────────────────────────────────────────────────────────────────
// INSTRUMENT DRAWING FUNCTIONS (Canvas)
// ──────────────────────────────────────────────────────────────────────

/**
 * drawH(roll, pitch) – Artificial horizon
 * 
 * The horizon is a canvas element that shows roll (rotation) and pitch
 * (vertical offset). A sky/ground split line rotates with roll and shifts
 * with pitch. The aircraft symbol (fixed) is represented by the crosshair.
 */
function drawH(roll, pitch) {
    var cv = document.getElementById('cH'),
        c = cv.getContext('2d'),
        W = cv.width,
        H = cv.height,
        cx = W / 2,
        cy = H / 2;

    c.clearRect(0, 0, W, H);
    c.save();
    c.beginPath();
    c.rect(0, 0, W, H);
    c.clip();
    c.save();

    // Translate to centre, rotate by roll
    c.translate(cx, cy);
    c.rotate(roll * Math.PI / 180);
    var ph = pitch * 1.5;  // pitch scaling

    // Sky (blue) and ground (brown)
    c.fillStyle = '#003355';
    c.fillRect(-W, -H * 3, W * 2, H * 3 - ph);
    c.fillStyle = '#2a1500';
    c.fillRect(-W, -ph, W * 2, H * 3);

    // Horizon line
    c.strokeStyle = '#00ff88';
    c.lineWidth = 1.5;
    c.beginPath();
    c.moveTo(-W, -ph);
    c.lineTo(W, -ph);
    c.stroke();

    // Pitch scale (ticks every 10°)
    c.font = '8px monospace';
    c.textAlign = 'left';
    c.textBaseline = 'middle';
    for (var deg = -20; deg <= 20; deg += 10) {
        if (deg === 0) continue;
        var py = -ph - deg * 1.5,
            lw = deg % 20 === 0 ? 30 : 20;
        c.strokeStyle = 'rgba(0,255,136,0.45)';
        c.lineWidth = 1;
        c.beginPath();
        c.moveTo(-lw, py);
        c.lineTo(lw, py);
        c.stroke();
        c.fillStyle = 'rgba(184,220,232,0.65)';
        c.fillText((deg > 0 ? '+' : '') + deg, lw + 3, py);
    }

    c.restore();

    // Fixed aircraft symbol (crosshair)
    c.strokeStyle = '#ffb800';
    c.lineWidth = 2;
    c.beginPath();
    c.moveTo(cx - 30, cy);
    c.lineTo(cx - 12, cy);
    c.moveTo(cx + 12, cy);
    c.lineTo(cx + 30, cy);
    c.moveTo(cx, cy - 5);
    c.lineTo(cx, cy + 5);
    c.stroke();

    // Nose up/down text
    c.font = 'bold 9px monospace';
    c.textAlign = 'center';
    c.textBaseline = 'top';
    if (pitch > 2) {
        c.fillStyle = '#00ff88';
        c.fillText('NOSE UP +' + pitch.toFixed(1) + 'deg', cx, 2);
    } else if (pitch < -2) {
        c.fillStyle = '#ff3355';
        c.fillText('NOSE DN ' + pitch.toFixed(1) + 'deg', cx, 2);
    } else {
        c.fillStyle = 'rgba(30,58,74,0.8)';
        c.fillText('LEVEL', cx, 2);
    }

    c.restore();
    c.strokeStyle = '#0c3050';
    c.lineWidth = 1;
    c.strokeRect(0, 0, W, H);
}

/**
 * drawC(mag) – Compass rose
 * 
 * Shows a 360° compass with cardinal directions (N,E,S,W), tick marks
 * every 10°, and a red needle pointing to magnetic north.
 */
function drawC(mag) {
    var cv = document.getElementById('cC'),
        c = cv.getContext('2d'),
        W = cv.width,
        H = cv.height,
        cx = W / 2,
        cy = H / 2 + 4,
        R = Math.min(cx, cy - 4) - 2;

    c.clearRect(0, 0, W, H);
    c.beginPath();
    c.arc(cx, cy, R, 0, 2 * Math.PI);
    c.fillStyle = '#09141c';
    c.fill();
    c.strokeStyle = '#0c3050';
    c.lineWidth = 1.5;
    c.stroke();

    // Tick marks (10° intervals)
    for (var i = 0; i < 36; i++) {
        var a = i * 10 * Math.PI / 180,
            r1 = R - (i % 3 === 0 ? 9 : 4),
            r2 = R - 1;
        c.beginPath();
        c.moveTo(cx + Math.sin(a) * r1, cy - Math.cos(a) * r1);
        c.lineTo(cx + Math.sin(a) * r2, cy - Math.cos(a) * r2);
        c.strokeStyle = i % 3 === 0 ? '#1e3a4a' : '#0c2030';
        c.lineWidth = 1;
        c.stroke();
    }

    // Cardinal labels (N,E,S,W)
    var cd = ['N', 'E', 'S', 'W'];
    c.font = 'bold 9px monospace';
    c.textAlign = 'center';
    c.textBaseline = 'middle';
    for (var j = 0; j < 4; j++) {
        var a2 = j * 90 * Math.PI / 180,
            lr = R - 14;
        c.fillStyle = j === 0 ? '#ff3355' : '#b8dce8';
        c.fillText(cd[j], cx + Math.sin(a2) * lr, cy - Math.cos(a2) * lr);
    }

    // North needle (red)
    var na = mag * Math.PI / 180;
    c.beginPath();
    c.moveTo(cx, cy);
    c.lineTo(cx + Math.sin(na) * (R - 16), cy - Math.cos(na) * (R - 16));
    c.strokeStyle = '#ff3355';
    c.lineWidth = 2.5;
    c.lineCap = 'round';
    c.stroke();

    // South tail (grey)
    c.beginPath();
    c.moveTo(cx, cy);
    c.lineTo(cx - Math.sin(na) * 13, cy + Math.cos(na) * 13);
    c.strokeStyle = '#1e3a4a';
    c.lineWidth = 2;
    c.stroke();

    // Centre dot
    c.beginPath();
    c.arc(cx, cy, 3, 0, 2 * Math.PI);
    c.fillStyle = '#00cfff';
    c.fill();

    // Heading value
    c.fillStyle = '#00ff88';
    c.font = '9px monospace';
    c.textAlign = 'center';
    c.textBaseline = 'top';
    c.fillText(Math.round(mag) + ' deg', cx, 2);
}

/**
 * drawT(yr) – Turn rate indicator
 * 
 * A horizontal bar showing the gyro Z‑axis rate. The indicator moves left
 * (turn left) or right (turn right), colour changes when rate > 0.5 °/s.
 */
function drawT(yr) {
    var cv = document.getElementById('cT'),
        c = cv.getContext('2d'),
        W = cv.width,
        H = cv.height,
        cx = W / 2,
        cy = H / 2;

    if (!W || !H) return;
    c.clearRect(0, 0, W, H);
    var mx = 6;  // ±6 °/s full scale

    // Tick marks every 1 °/s
    for (var i = -3; i <= 3; i++) {
        var x = cx + i * (W / 2 / mx) * 2;
        c.beginPath();
        c.moveTo(x, cy - 7);
        c.lineTo(x, cy + 7);
        c.strokeStyle = i === 0 ? '#00ff88' : '#1e3a4a';
        c.lineWidth = i === 0 ? 1.5 : 1;
        c.stroke();
    }

    // Indicator bar
    var cl = Math.max(-mx, Math.min(mx, yr)),
        nx = cx + cl * (W / 2) / mx;
    c.fillStyle = Math.abs(yr) > 0.5 ? '#ffb800' : '#00ff88';
    c.fillRect(nx - 2, cy - 9, 4, 18);

    // Labels
    c.fillStyle = '#1e3a4a';
    c.font = '9px monospace';
    c.textAlign = 'center';
    c.fillText('L', 10, cy + 4);
    c.fillText('R', W - 10, cy + 4);

    // Value
    c.fillStyle = '#00cfff';
    c.fillText(yr.toFixed(1) + ' d/s', cx, H - 1);
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  EXTENDED CALIBRATION WIZARD
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

var calMode = null;
var accelPosIdx = 0, magHdgIdx = 0;

var calConfig = {
    accel: {
        title: 'STEP 1 OF 5 — ACCELEROMETER',
        steps: [
            'POSITION 1: Lay device FLAT on table, screen facing up. Hold perfectly still.',
            'POSITION 2: Flip device UPSIDE DOWN on table. Hold perfectly still.',
            'POSITION 3: Stand device on its BACK EDGE so the top points up. Hold still.',
            'POSITION 4: Stand device on its FRONT EDGE so the bottom points up. Hold still.',
            'POSITION 5: Stand device on its LEFT SIDE edge pointing up. Hold still.',
            'POSITION 6: Stand device on its RIGHT SIDE edge pointing up. Hold still.'
        ],
        instr: 'This teaches the sensor its exact tilt in each direction. Hold VERY STILL for 3 seconds after positioning, then tap DONE FOR THIS POSITION.',
        btn: 'DONE FOR THIS POSITION'
    },
    mag: {
        title: 'STEP 2 OF 5 — COMPASS CALIBRATION',
        steps: [
            'Turn device so the arrow or USB port faces TRUE NORTH. Use a compass app to find North. Hold still.',
            'Turn device so the arrow or USB port faces EAST (right of north). Hold still.',
            'Turn device so the arrow or USB port faces SOUTH (opposite of north). Hold still.',
            'Turn device so the arrow or USB port faces WEST (left of north). Hold still.'
        ],
        instr: 'This teaches the compass which way is North. Open a compass app on another phone to find North direction. Point the device USB connector toward that direction, hold still, then tap DONE.',
        btn: 'DONE FOR THIS DIRECTION'
    },
    vel: {
        title: 'STEP 3 OF 5 — SPEED CALIBRATION',
        steps: [
            'Put device on the floor. Tap START, then pick it up and walk EXACTLY 1 metre (about 3 big adult steps) in a straight line.',
            'You are now 1 metre away from the start. Stand still for 2 seconds.',
            'Tap MARK 1 METRE to record where you stopped.'
        ],
        instr: 'This helps the system measure distances accurately. You need a tape measure or know a 1 metre distance on the floor. Walk slowly and smoothly.',
        btn: 'MARK 1 METRE'
    },
    baro: {
        title: 'STEP 4 OF 5 — ALTITUDE CALIBRATION',
        steps: [
            'Hold device at waist height (about 1 metre from floor). Tap MARK GROUND LEVEL.',
            'Raise device straight up above your head — about 1 metre higher than before. Hold still.',
            'Tap MARK HIGH POINT to record the 1 metre rise.'
        ],
        instr: 'This calibrates the air pressure sensor for accurate altitude. Raise the device smoothly by exactly 1 metre (approximately from waist to above your head).',
        btn: 'MARK HIGH POINT'
    },
    gps: {
        title: 'STEP 5 OF 5 — GPS COORDINATE SYNC',
        steps: [
            'Make sure GPS shows CONNECTED and satellites found. You entered your exact position above. Tap SYNC to align GPS to your exact position.'
        ],
        instr: 'This aligns the GPS to your exact known starting position for maximum accuracy. GPS must show green (connected + satellites). If GPS shows searching, wait a few minutes outdoors and try again.',
        btn: 'SYNC GPS TO MY POSITION'
    }
};

function startCal(mode) {
    calMode = mode;
    accelPosIdx = 0;
    magHdgIdx = 0;
    document.getElementById('calLaunchers').style.display = 'none';
    document.getElementById('calWiz').style.display = 'block';
    var cfg = calConfig[mode];
    document.getElementById('calStepTitle').textContent = cfg.title;
    document.getElementById('calStepInstr').textContent = cfg.instr;
    updateCalStep(mode, 0);
    // Start server-side calibration if needed
    if (mode === 'accel') fetch('/cal_accel_start', { method: 'POST' }).catch(function() {});
    else if (mode === 'mag') fetch('/cal_mag_start', { method: 'POST' }).catch(function() {});
    else if (mode === 'baro') fetch('/cal_baro_ref', { method: 'POST' }).catch(function() {});
}

function updateCalStep(mode, step) {
    var cfg = calConfig[mode];
    var steps = cfg.steps;
    var prog = Math.round((step / steps.length) * 100);
    document.getElementById('calWizProg').style.width = prog + '%';
    var total = steps.length;
    var stepLabel = step < steps.length ? steps[step] : 'ALL DONE!';
    var header = calConfig[mode].title;
    if (step < total) {
        document.getElementById('calStepTitle').textContent = header;
        document.getElementById('calStepInstr').textContent =
            '[ ' + (step + 1) + ' of ' + total + ' ] ' + stepLabel + '\n\n' + calConfig[mode].instr;
    } else {
        document.getElementById('calStepTitle').textContent = 'CALIBRATION COMPLETE';
        document.getElementById('calStepInstr').textContent = 'This calibration step is done. Tap FINISH or choose another step.';
    }
    var btns = document.getElementById('calStepBtns');
    btns.innerHTML = '';
    if (step < steps.length) {
        var b = document.createElement('button');
        b.className = 'btn bg';
        b.style.margin = '0';
        b.style.padding = '8px';
        b.style.fontSize = '10px';
        b.textContent = cfg.btn + ' (' + steps[step] + ')';
        b.onclick = function() { doCalCapture(mode, step); };
        btns.appendChild(b);
    }
    var skip = document.createElement('button');
    skip.className = 'btn br';
    skip.style.margin = '0';
    skip.style.padding = '8px';
    skip.style.fontSize = '10px';
    skip.textContent = step < steps.length ? 'SKIP STEP' : 'FINISH';
    skip.onclick = function() { finishCal(); };
    btns.appendChild(skip);
}

function doCalCapture(mode, step) {
    document.getElementById('calStepResult').textContent = 'Capturing 2s of data...';
    if (mode === 'accel') {
        fetch('/cal_accel_pos', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: 'pos=' + accelPosIdx
        })
        .then(function(r) { return r.json(); })
        .then(function(d) {
            document.getElementById('calStepResult').textContent = 'Position ' + accelPosIdx + ' captured. Prog:' + Math.round(d.prog) + '%';
            accelPosIdx++;
            if (accelPosIdx < 6) updateCalStep(mode, accelPosIdx);
            else { document.getElementById('calWizProg').style.width = '100%'; fetchCalResults(); finishCal(); }
        })
        .catch(function(e) { document.getElementById('calStepResult').textContent = 'Error capturing'; });
    } else if (mode === 'mag') {
        fetch('/cal_mag_pos', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: 'hdg=' + magHdgIdx
        })
        .then(function(r) { return r.json(); })
        .then(function(d) {
            document.getElementById('calStepResult').textContent = 'Heading ' + ['N', 'E', 'S', 'W'][magHdgIdx] + ' captured.';
            magHdgIdx++;
            if (magHdgIdx < 4) updateCalStep(mode, magHdgIdx);
            else { document.getElementById('calWizProg').style.width = '100%'; fetchCalResults(); finishCal(); }
        })
        .catch(function(e) { document.getElementById('calStepResult').textContent = 'Error capturing'; });
    } else if (mode === 'vel') {
        if (step === 0) {
            fetch('/cal_vel_start', { method: 'POST' })
                .then(function() {
                    document.getElementById('calStepResult').textContent = 'Start point saved! Now walk exactly 1 metre forward and stand still.';
                    updateCalStep(mode, 1);
                });
        } else if (step === 1) {
            document.getElementById('calStepResult').textContent = 'Good! Hold still at the 1m mark...';
            setTimeout(function() { updateCalStep(mode, 2); }, 1500);
        } else {
            fetch('/cal_vel_mark', { method: 'POST' })
                .then(function(r) { return r.json(); })
                .then(function(d) {
                    document.getElementById('calStepResult').textContent = 'Done! System measured ' + d.measured.toFixed(2) + 'm. Scale factor: ' + d.scale.toFixed(3) + '. Accuracy improved!';
                    fetchCalResults(); finishCal();
                });
        }
    } else if (mode === 'baro') {
        if (step === 0) {
            fetch('/cal_baro_ref', { method: 'POST' })
                .then(function() {
                    document.getElementById('calStepResult').textContent = 'Ground ref marked. Now lift device exactly 1m up and hold still.';
                    updateCalStep(mode, 1);
                });
        } else {
            fetch('/cal_baro_mark', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: 'actual=1.0'
            })
            .then(function(r) { return r.json(); })
            .then(function(d) {
                document.getElementById('calStepResult').textContent = 'Done! Pressure sensor measured ' + d.measured.toFixed(2) + 'm rise. Scale: ' + d.scale.toFixed(3) + 'x corrected!';
                fetchCalResults(); finishCal();
            });
        }
    } else if (mode === 'gps') {
        fetch('/cal_gps_sync', { method: 'POST' })
            .then(function(r) { return r.json(); })
            .then(function(d) {
                document.getElementById('calStepResult').textContent = 'GPS synced! Position offset corrected by ' + d.shift.toFixed(1) + 'm. You are now pinned to your exact coordinates.';
                fetchCalResults(); finishCal();
            })
            .catch(function(e) {
                document.getElementById('calStepResult').textContent = 'Cannot sync: ' + e.message + '. Make sure GPS is connected and has satellite fix.';
            });
    }
}

function finishCal() {
    setTimeout(function() {
        document.getElementById('calWiz').style.display = 'none';
        document.getElementById('calLaunchers').style.display = 'block';
    }, 1500);
}

function fetchCalResults() {
    fetch('/cal_status')
        .then(function(r) { return r.json(); })
        .then(function(d) {
            var s = '';
            s += 'AccScl:' + d.aScX.toFixed(3) + '/' + d.aScY.toFixed(3) + '/' + d.aScZ.toFixed(3) + '  ';
            s += 'VelSc:' + d.velScN.toFixed(3) + '/' + d.velScE.toFixed(3) + '  ';
            s += 'BaroSc:' + d.baroSc.toFixed(3) + '  ';
            s += 'GPS:' + (d.gpsSyn ? 'SYNCED' : 'NOT SYNCED');
            document.getElementById('calResults').textContent = s;
        })
        .catch(function() {});
}

// ──────────────────────────────────────────────────────────────────────
// EKF TOGGLE (collapsible panel)
// ──────────────────────────────────────────────────────────────────────

function toggleEKF() {
    var panel = document.getElementById('ekfPanel');
    if (panel.style.display === 'none' || panel.style.display === '') {
        panel.style.display = 'block';
    } else {
        panel.style.display = 'none';
    }
}
window.toggleEKF = toggleEKF;

// Auto‑fetch calibration results on load
window.addEventListener('load', function() {
    setTimeout(fetchCalResults, 2000);
});

</script>
</body>
</html>
)rawliteral";