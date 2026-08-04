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

function clamp(n, min, max) {
    return Math.min(max, Math.max(min, n));
}

// --- Speed model ---
let maxSpeed = 60;
let targetThrottle01 = 0;   // 0..1
let throttle01 = 0;         // smoothed
let speed = 0;              // smoothed

function setMode(newMaxSpeed, label, activeBtn) {
    maxSpeed = newMaxSpeed;
    modeLabelEl.textContent = label;

    for (const b of [modeEco, modeNormal, modeSport]) b.classList.remove('is-active');
    activeBtn.classList.add('is-active');
}

function setSourceLabel(label) {
    inputSource.textContent = `Source: ${label}`;
}

function setConnectedUI(isConnected, detailText) {
    btnConnect.disabled = isConnected;
    btnDisconnect.disabled = !isConnected;
    throttleInput.disabled = isConnected;

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
    }
}

function updateUI() {
    speedValueEl.textContent = String(Math.round(speed));
    throttlePctEl.textContent = String(Math.round(throttle01 * 100));

    // Arc uses pathLength=100. We draw the fill as a dash whose length
    // equals the percentage. At exactly 0%, a zero-length dash with a
    // rounded linecap can still render as a tiny dot at the path's
    // start/end in some browsers — so we just hide the arc entirely
    // when there's nothing to show.
    const pct = clamp(speed / maxSpeed, 0, 1);
    if (pct <= 0.002) {
        arcFg.style.opacity = '0';
    } else {
        arcFg.style.opacity = '1';
        const dashLength = pct * 100;
        arcFg.style.strokeDasharray = `${dashLength} 100`;
        arcFg.style.strokeDashoffset = '0';
    }

    // Needle rotation: -120deg (0) to +120deg (max)
    const deg = -120 + pct * 240;
    needle.setAttribute('transform', `translate(180 190) rotate(${deg.toFixed(1)})`);
}

function animationLoop() {
    // Smooth throttle + speed for stable needle
    const alpha = 0.12;
    throttle01 += (targetThrottle01 - throttle01) * alpha;

    const targetSpeed = throttle01 * maxSpeed;
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
        const adcMin = 1120;
        const adcMax = 4095;
        return clamp((v - adcMin) / (adcMax - adcMin), 0, 1);
    }
    if (v >= 0 && v <= 1) return clamp(v, 0, 1);
    if (v >= 0 && v <= 100) return clamp(v / 100, 0, 1);

    return null;
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

            const t01 = parseIncomingValue(value);
            if (t01 == null) continue;

            targetThrottle01 = t01;
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

// Init
setMode(60, 'Eco', modeEco);
setConnectedUI(false);
updateUI();
requestAnimationFrame(animationLoop);