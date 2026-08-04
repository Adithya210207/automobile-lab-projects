const throttleInput = document.getElementById('throttle');
const speedValueEl = document.getElementById('speed-value');
const throttlePctEl = document.getElementById('throttle-pct');
const modeLabelEl = document.getElementById('mode-label');
const arcFg = document.getElementById('arc-fg');
const needle = document.getElementById('needle');

const btnConnect = document.getElementById('btn-connect');
const btnDisconnect = document.getElementById('btn-disconnect');
const connStatus = document.getElementById('conn-status');
const inputSource = document.getElementById('input-source');

const modeEco = document.getElementById('mode-eco');
const modeNormal = document.getElementById('mode-normal');
const modeSport = document.getElementById('mode-sport');

const dirLabelEl = document.getElementById('dir-label');
const dirForwardBtn = document.getElementById('dir-forward');
const dirReverseBtn = document.getElementById('dir-reverse');

const cruisePillEl = document.getElementById('cruise-pill');
let cruiseActive = false;

function clamp(n, min, max) {
    return Math.min(max, Math.max(min, n));
}

// --- Speed model ---
let maxSpeed = 60;
let targetThrottle01 = 0;   // 0..1
let throttle01 = 0;         // smoothed
let speed = 0;              // smoothed

const MAX_REVERSE_SPEED = 5; // km/h, fixed regardless of gear
let direction = 'F'; // 'F' | 'R'

// The needle/arc position is drawn against this fixed scale, not against
// whatever the *current* mode's max happens to be. That way the gauge
// always shows where you actually are on the real speed range: Eco's
// top speed sits partway up the dial, Sport's fills most of it, instead
// of every mode looking identical at the same throttle percentage.
//
// Set to 200 (not Sport's 180) so the dial's 5 tick marks land on
// round, evenly-spaced numbers — 0 / 50 / 100 / 150 / 200 — exactly
// 50 km/h apart. With this scale, Sport's 180 km/h cap sits at 90% of
// the arc rather than the very end.
const GAUGE_MAX_SPEED = 200;

function setMode(newMaxSpeed, label, activeBtn) {
    maxSpeed = newMaxSpeed;
    modeLabelEl.textContent = label;

    for (const b of [modeEco, modeNormal, modeSport]) b.classList.remove('is-active');
    activeBtn.classList.add('is-active');
}

// Effective speed cap for the current direction: gear-based max when
// going forward, fixed slow cap when in reverse.
function effectiveMaxSpeed() {
    return direction === 'R' ? MAX_REVERSE_SPEED : maxSpeed;
}

function setDirection(newDirection, activeBtn) {
    direction = newDirection;

    if (direction === 'R') {
        dirLabelEl.textContent = 'Reverse';
        dirLabelEl.classList.remove('dir-label--fwd');
        dirLabelEl.classList.add('dir-label--rev');
    } else {
        dirLabelEl.textContent = 'Forward';
        dirLabelEl.classList.remove('dir-label--rev');
        dirLabelEl.classList.add('dir-label--fwd');
    }

    for (const b of [dirForwardBtn, dirReverseBtn]) b.classList.remove('is-active');
    if (activeBtn) activeBtn.classList.add('is-active');
}

function setSourceLabel(label) {
    inputSource.textContent = `Source: ${label}`;
}

function setModeButtonsDisabled(isDisabled) {
    for (const b of [modeEco, modeNormal, modeSport]) b.disabled = isDisabled;
}

function setDirButtonsDisabled(isDisabled) {
    for (const b of [dirForwardBtn, dirReverseBtn]) b.disabled = isDisabled;
}

function setConnectedUI(isConnected, detailText) {
    btnConnect.disabled = isConnected;
    btnDisconnect.disabled = !isConnected;
    throttleInput.disabled = isConnected;
    // Gear and direction now come from the handlebar rocker switches over
    // serial, so the on-screen buttons are just a fallback when not connected.
    setModeButtonsDisabled(isConnected);
    setDirButtonsDisabled(isConnected);

    if (isConnected) {
        connStatus.textContent = detailText || 'Connected';
        connStatus.classList.remove('pill--warn', 'pill--bad');
        connStatus.classList.add('pill--good');
        setSourceLabel('serial');
    } else {
        connStatus.textContent = detailText || 'Not connected';
        connStatus.classList.remove('pill--good', 'pill--bad');
        connStatus.classList.add('pill--warn');
        setSourceLabel('slider');
        applyCruiseFromSerial(0); // no serial source anymore, so cruise can't be active
    }
}

// Three-stop colour ramp for the gauge arc, like a real speedometer's
// redline: green at rest, sliding through amber, ending red as speed
// approaches GAUGE_MAX_SPEED. `pct` is 0..1 (speed / GAUGE_MAX_SPEED).
const ARC_COLOR_STOPS = [
    { stop: 0.0, rgb: [0, 255, 153], alpha: 0.85 },  // green
    { stop: 0.5, rgb: [255, 190, 60], alpha: 0.88 },  // amber
    { stop: 1.0, rgb: [255, 70, 70], alpha: 0.92 },  // red
];

function lerp(a, b, t) {
    return a + (b - a) * t;
}

function colorForPct(pct) {
    const p = clamp(pct, 0, 1);

    // Find which two stops p falls between, then lerp within that segment.
    let lower = ARC_COLOR_STOPS[0];
    let upper = ARC_COLOR_STOPS[ARC_COLOR_STOPS.length - 1];
    for (let i = 0; i < ARC_COLOR_STOPS.length - 1; i++) {
        if (p >= ARC_COLOR_STOPS[i].stop && p <= ARC_COLOR_STOPS[i + 1].stop) {
            lower = ARC_COLOR_STOPS[i];
            upper = ARC_COLOR_STOPS[i + 1];
            break;
        }
    }

    const span = upper.stop - lower.stop;
    const t = span <= 0 ? 0 : (p - lower.stop) / span;

    const r = Math.round(lerp(lower.rgb[0], upper.rgb[0], t));
    const g = Math.round(lerp(lower.rgb[1], upper.rgb[1], t));
    const b = Math.round(lerp(lower.rgb[2], upper.rgb[2], t));
    const a = lerp(lower.alpha, upper.alpha, t);

    return { r, g, b, a };
}

function updateUI() {
    speedValueEl.textContent = String(Math.round(speed));
    throttlePctEl.textContent = String(Math.round(throttle01 * 100));

    // Arc uses pathLength=100. We draw the fill as a dash whose length
    // equals the percentage. At exactly 0%, a zero-length dash with a
    // rounded linecap can still render as a tiny dot at the path's
    // start/end in some browsers — so we just hide the arc entirely
    // when there's nothing to show.
    //
    // Position against the fixed GAUGE_MAX_SPEED scale, not the current
    // mode's own max — so the needle reflects the real speed, and Eco
    // visibly can't swing as far round the dial as Sport can.
    const pct = clamp(speed / GAUGE_MAX_SPEED, 0, 1);
    if (pct <= 0.002) {
        arcFg.style.opacity = '0';
    } else {
        arcFg.style.opacity = '1';
        const dashLength = pct * 100;
        arcFg.style.strokeDasharray = `${dashLength} 100`;
        arcFg.style.strokeDashoffset = '0';

        // Slide the arc's colour from green -> amber -> red as speed
        // climbs, like a real speedometer's redline.
        const { r, g, b, a } = colorForPct(pct);
        arcFg.style.stroke = `rgba(${r}, ${g}, ${b}, ${a.toFixed(2)})`;
        arcFg.style.filter = `drop-shadow(0 0 10px rgba(${r}, ${g}, ${b}, 0.35))`;
    }

    // Needle rotation: -90deg (0, pointing at the left tick) to +90deg
    // (max, pointing at the right tick) — matches the gauge arc, which
    // is a true 180-degree semicircle (see arc-bg's path: it goes from
    // directly left of centre to directly right of centre, through the
    // top). The needle sweep must match that 180 degrees exactly, or it
    // visibly overshoots past where the green arc ends.
    const deg = -90 + pct * 180;
    needle.setAttribute('transform', `translate(180 190) rotate(${deg.toFixed(1)})`);
}

function animationLoop() {
    // Smooth throttle + speed for stable needle
    const alpha = 0.12;
    throttle01 += (targetThrottle01 - throttle01) * alpha;

    const targetSpeed = throttle01 * effectiveMaxSpeed();
    speed += (targetSpeed - speed) * 0.18;

    updateUI();
    requestAnimationFrame(animationLoop);
}

// Slider fallback input
throttleInput.addEventListener('input', () => {
    if (throttleInput.disabled) return;
    const pct = clamp(Number(throttleInput.value) / 100, 0, 1);
    targetThrottle01 = pct;
    setSourceLabel('slider');
});

// --- Web Serial (STM32 over USB/UART) ---
let port = null;
let reader = null;
let cancelRead = false;

function parseIncomingValue(line) {
    // Accept: "1234", "ADC=1234", "THR=45.2", "PWM=0.42"
    const s = String(line).trim();
    if (!s) return null;

    const m = s.match(/(-?\d+(?:\.\d+)?)/);
    if (!m) return null;
    const v = Number(m[1]);
    if (!Number.isFinite(v)) return null;

    // Heuristics:
    // - If it looks like an ADC reading (0..4095), map to throttle using your 1120..4095 range.
    // - If 0..1, treat as normalized duty/throttle.
    // - If 0..100, treat as percent.
    if (v >= 0 && v <= 4095 && (s.toUpperCase().includes('ADC') || v > 1.5)) {
        // Calibrated from Serial Monitor on the real throttle hardware:
        // idle sits around 792-800, full throttle around 3398-3402. The
        // old 1120..4095 range assumed the pot swings all the way to 5V,
        // which this one doesn't - hence throttle capping at ~77%.
        const adcMin = 750;
        const adcMax = 3400;
        return clamp((v - adcMin) / (adcMax - adcMin), 0, 1);
    }
    if (v >= 0 && v <= 1) return clamp(v, 0, 1);
    if (v >= 0 && v <= 100) return clamp(v / 100, 0, 1);

    return null;
}

// Maps the GEAR=1|2|3 value sent by the Arduino (from the handlebar
// rocker switch) to the matching drive mode.
function applyGearFromSerial(gearNum) {
    if (gearNum === 1) setMode(60, 'Eco', modeEco);
    else if (gearNum === 3) setMode(180, 'Sport', modeSport);
    else setMode(120, 'Normal', modeNormal); // default / gear 2
}

// Maps the DIR=1|2 value sent by the Arduino (from the handlebar F/R
// rocker switch) to the matching direction. 1=Forward, 2=Reverse.
function applyDirectionFromSerial(dirNum) {
    if (dirNum === 2) setDirection('R', dirReverseBtn);
    else setDirection('F', dirForwardBtn);
}

// Reflects the CRUISE=0|1 flag sent by the Arduino. When cruise is on,
// the Arduino is sending a latched throttle value instead of the live
// reading, so this is purely a status indicator here.
function applyCruiseFromSerial(cruiseNum) {
    cruiseActive = cruiseNum === 1;
    if (!cruisePillEl) return;

    if (cruiseActive) {
        cruisePillEl.textContent = 'Cruise: ON';
        cruisePillEl.classList.remove('pill--warn');
        cruisePillEl.classList.add('pill--good');
    } else {
        cruisePillEl.textContent = 'Cruise: off';
        cruisePillEl.classList.remove('pill--good');
        cruisePillEl.classList.add('pill--warn');
    }
}

// A serial line can carry throttle, gear, direction, or all three, e.g.
// "ADC=1234,GEAR=2,DIR=1". This pulls out whichever fields are present
// and applies them.
function parseIncomingLine(line) {
    const s = String(line).trim();
    if (!s) return;

    const gearMatch = s.match(/GEAR\s*=\s*(\d+)/i);
    if (gearMatch) {
        applyGearFromSerial(Number(gearMatch[1]));
    }

    const dirMatch = s.match(/DIR\s*=\s*(\d+)/i);
    if (dirMatch) {
        applyDirectionFromSerial(Number(dirMatch[1]));
    }

    const cruiseMatch = s.match(/CRUISE\s*=\s*(\d+)/i);
    if (cruiseMatch) {
        applyCruiseFromSerial(Number(cruiseMatch[1]));
    }

    // Throttle/ADC value lives before any comma-separated GEAR/DIR fields.
    const throttlePart = s.split(',')[0];
    const t01 = parseIncomingValue(throttlePart);
    if (t01 != null) {
        targetThrottle01 = t01;
    }
}

function lineBreakTransformer() {
    let buffer = '';
    return new TransformStream({
        transform(chunk, controller) {
            buffer += chunk;
            const lines = buffer.split(/\r?\n/);
            buffer = lines.pop() || '';
            for (const line of lines) controller.enqueue(line);
        },
        flush(controller) {
            if (buffer) controller.enqueue(buffer);
        }
    });
}

async function readLoop() {
    cancelRead = false;
    try {
        const textDecoder = new TextDecoderStream();
        const readableStreamClosed = port.readable.pipeTo(textDecoder.writable);
        const inputStream = textDecoder.readable.pipeThrough(lineBreakTransformer());
        reader = inputStream.getReader();

        while (!cancelRead) {
            const { value, done } = await reader.read();
            if (done) break;

            parseIncomingLine(value);
        }

        await readableStreamClosed.catch(() => {});
    } catch (err) {
        setConnectedUI(false, 'Disconnected');
        console.error(err);
    } finally {
        try { await reader?.cancel(); } catch {}
        try { reader?.releaseLock(); } catch {}
        reader = null;
    }
}

async function connectSerial() {
    if (!('serial' in navigator)) {
        setConnectedUI(false, 'Web Serial unsupported (use Chrome/Edge)');
        connStatus.classList.remove('pill--warn', 'pill--good');
        connStatus.classList.add('pill--bad');
        return;
    }

    try {
        port = await navigator.serial.requestPort();
        await port.open({ baudRate: 115200 });
        setConnectedUI(true, 'Connected (115200 baud)');
        await readLoop();
    } catch (err) {
        setConnectedUI(false, 'Connect cancelled/failed');
        console.error(err);
        try { await port?.close(); } catch {}
        port = null;
    }
}

async function disconnectSerial() {
    cancelRead = true;
    try { await reader?.cancel(); } catch {}
    try { await port?.close(); } catch {}
    port = null;
    setConnectedUI(false, 'Not connected');
}

btnConnect.addEventListener('click', connectSerial);
btnDisconnect.addEventListener('click', disconnectSerial);

// Modes
modeEco.addEventListener('click', () => setMode(60, 'Eco', modeEco));
modeNormal.addEventListener('click', () => setMode(120, 'Normal', modeNormal));
modeSport.addEventListener('click', () => setMode(180, 'Sport', modeSport));

// Direction (fallback, disabled while connected — serial DIR= drives it then)
dirForwardBtn.addEventListener('click', () => setDirection('F', dirForwardBtn));
dirReverseBtn.addEventListener('click', () => setDirection('R', dirReverseBtn));

// Init
setMode(60, 'Eco', modeEco);
setDirection('F', dirForwardBtn);
setConnectedUI(false);
updateUI();
requestAnimationFrame(animationLoop);