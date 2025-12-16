// ===== UI ELEMENT REFERENCES =====
const video = document.getElementById('video');
const statusPill = document.getElementById('status-pill');
const statusLabelEl = document.getElementById('status-label');
const statusSubtextEl = document.getElementById('status-subtext');
const statusLineEl = document.getElementById('status-line');
const overlay = document.getElementById('video-overlay');
const overlayText = document.getElementById('overlay-text');
const logEl = document.getElementById('log');

// ===== UI HELPERS =====
function setStatus(state, label, subtext) {
    statusPill.classList.remove('connecting', 'connected', 'reconnecting', 'disconnected');
    statusPill.classList.add(state);
    statusLabelEl.textContent = label.toUpperCase();
    if (subtext)
        statusSubtextEl.textContent = subtext;

    switch (state) {
    case 'connecting':
    case 'reconnecting':
        overlay.classList.remove('hidden');
        overlayText.textContent = 'Connecting…';
        break;
    case 'connected':
        overlay.classList.add('hidden');
        break;
    case 'disconnected':
        overlay.classList.remove('hidden');
        overlayText.textContent = 'Disconnected – waiting for stream…';
        break;
    }
}

function setStatusLine(text) {
    statusLineEl.innerHTML = text;
}

function appendLog(message, type = 'info') {
    const div = document.createElement('div');
    div.className = 'log-line' + (type === 'error' ? ' error' : '');
    const time = new Date().toLocaleTimeString();
    div.innerHTML = `<span>[${time}]</span> <span class="log-tag">${
        type === 'error' ? 'ERR' : 'LOG'}</span>${message}`;
    logEl.appendChild(div);
    logEl.scrollTop = logEl.scrollHeight;

    console[type === 'error' ? 'error' : 'log']('[WebRTC UI]', message);
}

// ===== WEBRTC + SIGNALING LOGIC =====
const WS_PROTO = location.protocol === 'https:' ? 'wss' : 'ws';
const WS_HOST = location.hostname;

// Prefer configured wsPort, fallback to page port if missing
const WS_PORT = (window.AMP_CONFIG && window.AMP_CONFIG.wsPort) ||
                (location.port || (location.protocol === 'https:' ? 443 : 80));

const SIGNALING_URL = `${WS_PROTO}://${WS_HOST}:${WS_PORT}/ws`;

let signaling = null;
let pc = null;

let wsReconnectDelay = 1000;
const WS_RECONNECT_DELAY_MAX = 15000;
const BACKOFF_FACTOR = 1.1;

let pcRestartTimer = null;

function createPeerConnection() {
    if (pc) {
        try {
            pc.close();
        } catch (e) {
            appendLog('Error closing old RTCPeerConnection: ' + e, 'error');
        }
    }

    appendLog('Creating new RTCPeerConnection');
    pc = new RTCPeerConnection({iceServers : [ {urls : 'stun:stun.l.google.com:19302'} ]});

    pc.onicecandidate = (event) => {
        if (event.candidate && signaling && signaling.readyState === WebSocket.OPEN) {
            appendLog('Sending ICE candidate');
            signaling.send(JSON.stringify({type : 'candidate', ice : event.candidate}));
        }
    };

    pc.ontrack = (event) => {
        appendLog('Received track kind=' + event.track.kind);
        if (event.track.kind === 'video') {
            video.srcObject = event.streams[0];
            video.play()
                .then(() => {
                          // ok
                      })
                .catch((err) => { appendLog('Error playing video: ' + err, 'error'); });

            setStatus('connected', 'Connected', 'Receiving video stream');
            setStatusLine('<strong>WebRTC connected.</strong> Video stream should be visible.');
        }
    };

    pc.oniceconnectionstatechange = () => {
        appendLog('ICE connection state: ' + pc.iceConnectionState);
        if (pc.iceConnectionState === 'connected') {
            setStatus('connected', 'Connected', 'Peer connection is stable.');
        } else if (pc.iceConnectionState === 'failed' || pc.iceConnectionState === 'disconnected') {
            setStatus('disconnected', 'Disconnected', 'Trying to recover connection…');
            setStatusLine('<strong>ICE state:</strong> ' + pc.iceConnectionState +
                          ' – will try to restart WebRTC.');
            schedulePeerRestart();
        }
    };

    pc.onicegatheringstatechange =
        () => { appendLog('ICE gathering state: ' + pc.iceGatheringState); };

    pc.onsignalingstatechange = () => { appendLog('Signaling state: ' + pc.signalingState); };
}

function schedulePeerRestart() {
    if (pcRestartTimer)
        return;
    appendLog('Scheduling PeerConnection restart in 2s…');
    pcRestartTimer = setTimeout(() => {
        pcRestartTimer = null;
        restartWebRTC();
    }, 2000);
}

async function startWebRTC() {
    if (!pc) {
        createPeerConnection();
    }

    if (!signaling || signaling.readyState !== WebSocket.OPEN) {
        appendLog('Signaling not open, delaying offer.');
        setStatus('connecting', 'Connecting', 'Waiting for signaling server…');
        return;
    }

    try {
        setStatus('connecting', 'Connecting', 'Creating offer and sending to server…');
        setStatusLine('<strong>Creating offer</strong> and sending it to the signaling server…');

        pc.addTransceiver('video', {direction : 'recvonly'});
        const offer = await pc.createOffer();
        appendLog('Created offer');
        await pc.setLocalDescription(offer);
        appendLog('Set local description with offer');

        signaling.send(JSON.stringify({type : 'offer', sdp : pc.localDescription.sdp}));
    } catch (err) {
        appendLog('Error during startWebRTC: ' + err, 'error');
        schedulePeerRestart();
    }
}

function restartWebRTC() {
    appendLog('Restarting WebRTC: new RTCPeerConnection and new offer…');
    setStatus('reconnecting', 'Reconnecting', 'Re-establishing WebRTC connection…');
    createPeerConnection();
    startWebRTC();
}

function connectSignaling(manual = false) {
    if (signaling && (signaling.readyState === WebSocket.OPEN ||
                      signaling.readyState === WebSocket.CONNECTING)) {
        if (manual) {
            appendLog('Signaling already open or connecting; ignoring manual reconnect.');
        }
        return;
    }

    if (manual) {
        wsReconnectDelay = 1000;
        appendLog('Manual reconnect triggered.');
    }

    setStatus('connecting', 'Connecting', 'Connecting to signaling server…');
    setStatusLine('<strong>Connecting to signaling server…</strong>');

    appendLog('Connecting to signaling: ' + SIGNALING_URL);
    signaling = new WebSocket(SIGNALING_URL);

    signaling.onopen = () => {
        appendLog('Signaling WebSocket open');
        wsReconnectDelay = 1000;
        setStatus('connecting', 'Connecting', 'Signaling connected – creating offer…');
        if (!pc)
            createPeerConnection();
        startWebRTC();
    };

    signaling.onmessage = async (event) => {
        const data = JSON.parse(event.data);
        appendLog('Received signaling message: ' + data.type);

        if (!pc) {
            appendLog('No RTCPeerConnection, creating before handling message.');
            createPeerConnection();
        }

        try {
            if (data.type === 'answer') {
                appendLog('Setting remote description with answer');
                await pc.setRemoteDescription(
                    new RTCSessionDescription({type : 'answer', sdp : data.sdp}));
                setStatus('connected', 'Connected', 'Answer received from server.');
                setStatusLine('<strong>Answer received.</strong> Waiting for video track…');
            } else if (data.type === 'candidate' && data.ice) {
                appendLog('Adding ICE candidate');
                await pc.addIceCandidate(new RTCIceCandidate(data.ice));
            }
        } catch (err) {
            appendLog('Error handling signaling message: ' + err, 'error');
            schedulePeerRestart();
        }
    };

    signaling.onerror =
        (err) => { appendLog('Signaling WebSocket error: ' + (err.message || err), 'error'); };

    signaling.onclose = () => {
        appendLog('Signaling WebSocket closed. Scheduling reconnect.');
        setStatus('disconnected', 'Disconnected', 'Signaling closed – will retry…');
        setStatusLine(
            '<strong>Signaling connection closed.</strong> Will retry automatically, or click Reconnect.');

        if (pc) {
            try {
                pc.close();
            } catch (e) {
                appendLog('Error closing pc on ws close: ' + e, 'error');
            }
            pc = null;
        }

        setTimeout(() => {
            wsReconnectDelay = Math.min(wsReconnectDelay * BACKOFF_FACTOR, WS_RECONNECT_DELAY_MAX);
            appendLog(`Reconnecting signaling in ${Math.round(wsReconnectDelay / 1000)}s…`);
            connectSignaling();
        }, wsReconnectDelay);
    };
}


// Initial startup
setStatus('connecting', 'Connecting', 'Initializing…');
setStatusLine('Starting WebRTC client & signaling…');
createPeerConnection();
connectSignaling();
