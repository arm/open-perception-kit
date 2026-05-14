const GAME_WIDTH = 1280;
const GAME_HEIGHT = 720;
const HALF_WIDTH = GAME_WIDTH / 2;
const BALL_RADIUS = 10;
const PADDLE_WIDTH = 16;
const PADDLE_MARGIN = 26;
const MAX_BOUNCE_ANGLE = Math.PI / 3;
const DEFAULT_FRAME_WIDTH = 1280;
const DEFAULT_FRAME_HEIGHT = 720;
const FACE_Y_FILTER_ALPHA = 0.2;

const elements = {
  connectForm: document.getElementById("connect-form"),
  wsUrl: document.getElementById("ws-url"),
  disconnectButton: document.getElementById("disconnect-button"),
  statusLine: document.getElementById("status-line"),
  canvas: document.getElementById("game-canvas"),
  gameNote: document.getElementById("game-note"),
  toggleGame: document.getElementById("toggle-game"),
  resetGame: document.getElementById("reset-game"),
  scoreLeft: document.getElementById("score-left"),
  scoreRight: document.getElementById("score-right"),
  messagesValue: document.getElementById("messages-value"),
  frameValue: document.getElementById("frame-value"),
  facesValue: document.getElementById("faces-value"),
  ballValue: document.getElementById("ball-value"),
  leftPresence: document.getElementById("left-presence"),
  rightPresence: document.getElementById("right-presence"),
  leftY: document.getElementById("left-y"),
  rightY: document.getElementById("right-y"),
  leftRawY: document.getElementById("left-raw-y"),
  rightRawY: document.getElementById("right-raw-y"),
  leftCalibration: document.getElementById("left-calibration"),
  rightCalibration: document.getElementById("right-calibration"),
  leftConfidence: document.getElementById("left-confidence"),
  rightConfidence: document.getElementById("right-confidence"),
  leftResetCalibration: document.getElementById("left-reset-calibration"),
  rightResetCalibration: document.getElementById("right-reset-calibration"),
  ballSpeed: document.getElementById("ball-speed"),
  ballSpeedValue: document.getElementById("ball-speed-value"),
  paddleHeight: document.getElementById("paddle-height"),
  paddleHeightValue: document.getElementById("paddle-height-value"),
  holdTime: document.getElementById("hold-time"),
  holdTimeValue: document.getElementById("hold-time-value"),
  mirrorSides: document.getElementById("mirror-sides"),
};

const context = elements.canvas.getContext("2d");

const state = {
  socket: null,
  messagesReceived: 0,
  frameCounter: "n/a",
  lastMessageAt: null,
  latestFaceCount: 0,
  frameDimensions: {
    width: DEFAULT_FRAME_WIDTH,
    height: DEFAULT_FRAME_HEIGHT,
  },
  players: {
    left: createPlayer("left"),
    right: createPlayer("right"),
  },
  scores: {
    left: 0,
    right: 0,
  },
  game: {
    running: true,
    ballSpeed: Number(elements.ballSpeed.value),
    paddleHeight: Number(elements.paddleHeight.value),
    holdTimeMs: Number(elements.holdTime.value),
    mirrorSides: elements.mirrorSides.checked,
    lastTickMs: performance.now(),
    ball: {
      x: GAME_WIDTH / 2,
      y: GAME_HEIGHT / 2,
      vx: 0,
      vy: 0,
    },
  },
};

function createPlayer(side) {
  return {
    side,
    centerY: GAME_HEIGHT / 2,
    targetY: GAME_HEIGHT / 2,
    rawFaceYNormalized: null,
    filteredFaceYNormalized: null,
    faceYNormalized: null,
    faceConfidence: null,
    lastSeenMs: 0,
    calibration: {
      top: null,
      bottom: null,
    },
  };
}

function setStatus(text, online) {
  elements.statusLine.textContent = text;
  elements.statusLine.classList.toggle("offline", !online);
}

function setGameNote(text) {
  elements.gameNote.textContent = text;
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function lowPass(previousValue, nextValue, alpha) {
  if (previousValue === null || previousValue === undefined) {
    return nextValue;
  }

  return previousValue + (nextValue - previousValue) * alpha;
}

function formatNumber(value, digits = 2) {
  if (value === null || value === undefined || Number.isNaN(value)) {
    return "n/a";
  }
  return Number(value).toFixed(digits);
}

function formatPercent(value) {
  if (value === null || value === undefined || Number.isNaN(value)) {
    return "n/a";
  }
  return `${Math.round(value * 100)}%`;
}

function normalizeMessage(message) {
  if (message && typeof message === "object" && message.perception && typeof message.perception === "object") {
    return {
      frameCounter: message.frame_counter ?? "n/a",
      perception: message.perception,
    };
  }

  if (message && typeof message === "object" && Array.isArray(message.layers)) {
    return {
      frameCounter: "n/a",
      perception: message,
    };
  }

  return {
    frameCounter: "n/a",
    perception: { perfdata: [], layers: [] },
  };
}

function extractRectDetections(perception) {
  const layers = Array.isArray(perception?.layers) ? perception.layers : [];
  const faces = [];
  const frameCandidates = [];

  for (const layer of layers) {
    const detections = Array.isArray(layer?.detections) ? layer.detections : [];

    for (const detection of detections) {
      if (detection?.type === "Rect" && layer?.contentType === "humanFace" && detection.data) {
        faces.push({
          ...detection.data,
          contentType: layer.contentType,
        });
      }

      if (detection?.type === "VideoFrame" && detection.data) {
        frameCandidates.push(detection.data);
      }
    }
  }

  return { faces, frameCandidates };
}

function deriveFrameDimensions(faces, frameCandidates) {
  if (frameCandidates.length > 0) {
    const withSize = frameCandidates.find((frame) => frame.originalWidth > 0 && frame.originalHeight > 0);
    if (withSize) {
      return {
        width: withSize.originalWidth,
        height: withSize.originalHeight,
      };
    }
  }

  if (faces.length === 0) {
    return state.frameDimensions;
  }

  const maxExtentX = Math.max(...faces.map((face) => Number(face.x) + Number(face.width)));
  const maxExtentY = Math.max(...faces.map((face) => Number(face.y) + Number(face.height)));
  const seemsNormalized = maxExtentX <= 2 && maxExtentY <= 2;

  if (seemsNormalized) {
    return { width: 1, height: 1 };
  }

  return {
    width: Math.max(state.frameDimensions.width, maxExtentX, DEFAULT_FRAME_WIDTH),
    height: Math.max(state.frameDimensions.height, maxExtentY, DEFAULT_FRAME_HEIGHT),
  };
}

function chooseFaceForSide(faces, side, frameWidth) {
  const splitX = frameWidth / 2;
  const controlledHalf = state.game.mirrorSides
    ? side === "left"
      ? "right"
      : "left"
    : side;
  const candidates = faces.filter((face) => {
    const centerX = Number(face.x) + Number(face.width) / 2;
    return controlledHalf === "left" ? centerX < splitX : centerX >= splitX;
  });

  if (candidates.length === 0) {
    return null;
  }

  return candidates.reduce((best, current) =>
    Number(current.confidence ?? 0) > Number(best.confidence ?? 0) ? current : best
  );
}

function getCalibratedFaceY(player, rawNormalizedY) {
  const { top, bottom } = player.calibration;
  if (top === null || bottom === null) {
    return rawNormalizedY;
  }

  const span = bottom - top;
  if (Math.abs(span) < 0.01) {
    return rawNormalizedY;
  }

  return clamp((rawNormalizedY - top) / span, 0, 1);
}

function describeCalibration(player) {
  const { top, bottom } = player.calibration;
  if (top === null && bottom === null) {
    return "Learning";
  }
  if (top === null) {
    return `Top pending, ${formatPercent(bottom)}`;
  }
  if (bottom === null) {
    return `${formatPercent(top)}, bottom pending`;
  }
  if (Math.abs(bottom - top) < 0.01) {
    return `Learning from ${formatPercent(top)}`;
  }
  return `${formatPercent(top)} to ${formatPercent(bottom)}`;
}

function updatePlayersFromFaces(faces) {
  const now = performance.now();
  const dimensions = deriveFrameDimensions(faces.faces, faces.frameCandidates);
  state.frameDimensions = dimensions;

  for (const side of ["left", "right"]) {
    const player = state.players[side];
    const face = chooseFaceForSide(faces.faces, side, dimensions.width);

    if (!face) {
      continue;
    }

    const faceCenterY = Number(face.y) + Number(face.height) / 2;
    const rawNormalizedY = clamp(faceCenterY / dimensions.height, 0, 1);
    player.calibration.top =
      player.calibration.top === null ? rawNormalizedY : Math.min(player.calibration.top, rawNormalizedY);
    player.calibration.bottom =
      player.calibration.bottom === null ? rawNormalizedY : Math.max(player.calibration.bottom, rawNormalizedY);
    player.filteredFaceYNormalized = lowPass(
      player.filteredFaceYNormalized,
      rawNormalizedY,
      FACE_Y_FILTER_ALPHA,
    );
    const calibratedY = getCalibratedFaceY(player, player.filteredFaceYNormalized);

    player.rawFaceYNormalized = rawNormalizedY;
    player.faceYNormalized = calibratedY;
    player.targetY = calibratedY * GAME_HEIGHT;
    player.faceConfidence = Number(face.confidence ?? 0);
    player.lastSeenMs = now;
  }
}

function updatePlayerFallbacks(now) {
  const holdTimeMs = state.game.holdTimeMs;

  for (const player of Object.values(state.players)) {
    if (now - player.lastSeenMs > holdTimeMs) {
      player.targetY = GAME_HEIGHT / 2;
      player.rawFaceYNormalized = null;
      player.filteredFaceYNormalized = null;
      player.faceYNormalized = null;
      player.faceConfidence = null;
    }
  }
}

function resetCalibration(side) {
  const player = state.players[side];
  player.calibration.top = null;
  player.calibration.bottom = null;

  const activeFaceY = player.filteredFaceYNormalized ?? player.rawFaceYNormalized;
  if (activeFaceY !== null) {
    player.faceYNormalized = activeFaceY;
    player.targetY = activeFaceY * GAME_HEIGHT;
  }

  renderSidebar(state.latestFaceCount);
  setGameNote(`${side === "left" ? "Left" : "Right"} auto-calibration reset. Move through your range again.`);
}

function connect(url) {
  disconnect();
  setStatus(`Connecting to ${url}...`, false);

  const socket = new WebSocket(url);
  state.socket = socket;

  socket.onopen = () => {
    setStatus(`Connected to ${url}`, true);
    setGameNote("Metadata live. Put one face in each half to control both rackets.");
  };

  socket.onmessage = (event) => {
    state.messagesReceived += 1;
    state.lastMessageAt = performance.now();

    try {
      const message = JSON.parse(event.data);
      const normalized = normalizeMessage(message);
      state.frameCounter = normalized.frameCounter;
      const faces = extractRectDetections(normalized.perception);
      state.latestFaceCount = faces.faces.length;
      updatePlayersFromFaces(faces);
      renderSidebar(faces.faces.length);
    } catch (error) {
      setStatus(`Parse error: ${error instanceof Error ? error.message : String(error)}`, false);
      setGameNote("Latest metadata message could not be parsed.");
    }
  };

  socket.onerror = () => {
    setStatus(`WebSocket error on ${url}`, false);
  };

  socket.onclose = () => {
    state.socket = null;
    setStatus(`Disconnected from ${url}`, false);
    setGameNote("Metadata disconnected. Rackets will drift back to center after the hold timeout.");
  };
}

function disconnect() {
  if (!state.socket) {
    return;
  }

  state.socket.onopen = null;
  state.socket.onmessage = null;
  state.socket.onerror = null;
  state.socket.onclose = null;
  state.socket.close();
  state.socket = null;
  setStatus("Offline", false);
}

function syncBallVelocity() {
  const ball = state.game.ball;
  const magnitude = Math.hypot(ball.vx, ball.vy);

  if (magnitude === 0) {
    const direction = Math.random() > 0.5 ? 1 : -1;
    ball.vx = direction * state.game.ballSpeed;
    ball.vy = (Math.random() * 2 - 1) * state.game.ballSpeed * 0.4;
    return;
  }

  const scale = state.game.ballSpeed / magnitude;
  ball.vx *= scale;
  ball.vy *= scale;
}

function resetBall(direction = Math.random() > 0.5 ? 1 : -1) {
  const angle = (Math.random() * 0.8 - 0.4) * MAX_BOUNCE_ANGLE;
  state.game.ball.x = GAME_WIDTH / 2;
  state.game.ball.y = GAME_HEIGHT / 2;
  state.game.ball.vx = Math.cos(angle) * state.game.ballSpeed * direction;
  state.game.ball.vy = Math.sin(angle) * state.game.ballSpeed;
}

function resetRound() {
  state.scores.left = 0;
  state.scores.right = 0;
  state.players.left.centerY = GAME_HEIGHT / 2;
  state.players.left.targetY = GAME_HEIGHT / 2;
  state.players.right.centerY = GAME_HEIGHT / 2;
  state.players.right.targetY = GAME_HEIGHT / 2;
  resetBall();
  renderSidebar(0);
}

function toggleRunning() {
  state.game.running = !state.game.running;
  elements.toggleGame.textContent = state.game.running ? "Pause" : "Resume";
  state.game.lastTickMs = performance.now();
}

function resizeCanvasForDpr() {
  const dpr = window.devicePixelRatio || 1;
  const width = Math.round(GAME_WIDTH * dpr);
  const height = Math.round(GAME_HEIGHT * dpr);

  if (elements.canvas.width !== width || elements.canvas.height !== height) {
    elements.canvas.width = width;
    elements.canvas.height = height;
    context.setTransform(dpr, 0, 0, dpr, 0, 0);
  }
}

function updatePaddles(dt, now) {
  updatePlayerFallbacks(now);

  for (const player of Object.values(state.players)) {
    const smoothing = clamp(dt * 10, 0, 1);
    player.centerY += (player.targetY - player.centerY) * smoothing;
    const halfHeight = state.game.paddleHeight / 2;
    player.centerY = clamp(player.centerY, halfHeight, GAME_HEIGHT - halfHeight);
  }
}

function getPaddleRect(side) {
  const player = state.players[side];
  return {
    x: side === "left" ? PADDLE_MARGIN : GAME_WIDTH - PADDLE_MARGIN - PADDLE_WIDTH,
    y: player.centerY - state.game.paddleHeight / 2,
    width: PADDLE_WIDTH,
    height: state.game.paddleHeight,
  };
}

function reflectFromPaddle(side, paddle) {
  const ball = state.game.ball;
  const paddleCenter = paddle.y + paddle.height / 2;
  const impact = clamp((ball.y - paddleCenter) / (paddle.height / 2), -1, 1);
  const angle = impact * MAX_BOUNCE_ANGLE;
  const direction = side === "left" ? 1 : -1;

  ball.vx = Math.cos(angle) * state.game.ballSpeed * direction;
  ball.vy = Math.sin(angle) * state.game.ballSpeed;
}

function updateBall(dt) {
  if (!state.game.running) {
    return;
  }

  const ball = state.game.ball;
  ball.x += ball.vx * dt;
  ball.y += ball.vy * dt;

  if (ball.y - BALL_RADIUS <= 0) {
    ball.y = BALL_RADIUS;
    ball.vy = Math.abs(ball.vy);
  } else if (ball.y + BALL_RADIUS >= GAME_HEIGHT) {
    ball.y = GAME_HEIGHT - BALL_RADIUS;
    ball.vy = -Math.abs(ball.vy);
  }

  const leftPaddle = getPaddleRect("left");
  const rightPaddle = getPaddleRect("right");

  if (
    ball.vx < 0 &&
    ball.x - BALL_RADIUS <= leftPaddle.x + leftPaddle.width &&
    ball.x + BALL_RADIUS >= leftPaddle.x &&
    ball.y >= leftPaddle.y &&
    ball.y <= leftPaddle.y + leftPaddle.height
  ) {
    ball.x = leftPaddle.x + leftPaddle.width + BALL_RADIUS;
    reflectFromPaddle("left", leftPaddle);
  }

  if (
    ball.vx > 0 &&
    ball.x + BALL_RADIUS >= rightPaddle.x &&
    ball.x - BALL_RADIUS <= rightPaddle.x + rightPaddle.width &&
    ball.y >= rightPaddle.y &&
    ball.y <= rightPaddle.y + rightPaddle.height
  ) {
    ball.x = rightPaddle.x - BALL_RADIUS;
    reflectFromPaddle("right", rightPaddle);
  }

  if (ball.x + BALL_RADIUS < 0) {
    state.scores.right += 1;
    resetBall(1);
  } else if (ball.x - BALL_RADIUS > GAME_WIDTH) {
    state.scores.left += 1;
    resetBall(-1);
  }
}

function drawCourt() {
  context.clearRect(0, 0, GAME_WIDTH, GAME_HEIGHT);

  const leftGradient = context.createLinearGradient(0, 0, HALF_WIDTH, GAME_HEIGHT);
  leftGradient.addColorStop(0, "#071923");
  leftGradient.addColorStop(1, "#09283a");
  context.fillStyle = leftGradient;
  context.fillRect(0, 0, HALF_WIDTH, GAME_HEIGHT);

  const rightGradient = context.createLinearGradient(HALF_WIDTH, 0, GAME_WIDTH, GAME_HEIGHT);
  rightGradient.addColorStop(0, "#26130d");
  rightGradient.addColorStop(1, "#3a190e");
  context.fillStyle = rightGradient;
  context.fillRect(HALF_WIDTH, 0, HALF_WIDTH, GAME_HEIGHT);

  context.strokeStyle = "rgba(216, 255, 98, 0.35)";
  context.lineWidth = 3;
  context.setLineDash([10, 14]);
  context.beginPath();
  context.moveTo(HALF_WIDTH, 22);
  context.lineTo(HALF_WIDTH, GAME_HEIGHT - 22);
  context.stroke();
  context.setLineDash([]);

  context.strokeStyle = "rgba(216, 255, 98, 0.16)";
  context.strokeRect(10, 10, GAME_WIDTH - 20, GAME_HEIGHT - 20);
}

function drawPaddle(side) {
  const paddle = getPaddleRect(side);
  const color = side === "left" ? "#49d5ff" : "#ff7d4d";

  context.fillStyle = color;
  context.shadowColor = color;
  context.shadowBlur = 22;
  context.fillRect(paddle.x, paddle.y, paddle.width, paddle.height);
  context.shadowBlur = 0;

  const player = state.players[side];
  if (player.faceYNormalized !== null) {
    const markerY = player.faceYNormalized * GAME_HEIGHT;
    context.fillStyle = "rgba(236, 248, 255, 0.88)";
    context.beginPath();
    context.arc(side === "left" ? HALF_WIDTH / 2 : HALF_WIDTH + HALF_WIDTH / 2, markerY, 6, 0, Math.PI * 2);
    context.fill();
  }
}

function drawBall() {
  const ball = state.game.ball;

  context.fillStyle = "#f7fff9";
  context.shadowColor = "#d8ff62";
  context.shadowBlur = 26;
  context.beginPath();
  context.arc(ball.x, ball.y, BALL_RADIUS, 0, Math.PI * 2);
  context.fill();
  context.shadowBlur = 0;
}

function drawLabels() {
  context.fillStyle = "rgba(236, 248, 255, 0.86)";
  context.font = "700 18px 'Avenir Next', 'Segoe UI', sans-serif";
  context.fillText("LEFT FACE", 28, 34);
  context.fillText("RIGHT FACE", GAME_WIDTH - 138, 34);
}

function renderSidebar(faceCount) {
  elements.messagesValue.textContent = String(state.messagesReceived);
  elements.frameValue.textContent = String(state.frameCounter);
  elements.facesValue.textContent = String(faceCount);
  elements.ballValue.textContent = `${state.game.ballSpeed} px/s`;
  elements.scoreLeft.textContent = String(state.scores.left);
  elements.scoreRight.textContent = String(state.scores.right);

  renderPlayerCard("left");
  renderPlayerCard("right");
}

function renderPlayerCard(side) {
  const player = state.players[side];
  const presenceEl = side === "left" ? elements.leftPresence : elements.rightPresence;
  const yEl = side === "left" ? elements.leftY : elements.rightY;
  const rawYEl = side === "left" ? elements.leftRawY : elements.rightRawY;
  const calibrationEl = side === "left" ? elements.leftCalibration : elements.rightCalibration;
  const confidenceEl = side === "left" ? elements.leftConfidence : elements.rightConfidence;
  const hasFace = player.faceYNormalized !== null;

  presenceEl.textContent = hasFace ? "Tracking" : "No face";
  presenceEl.classList.toggle("online", hasFace);
  presenceEl.classList.toggle("offline", !hasFace);
  yEl.textContent = hasFace ? formatPercent(player.faceYNormalized) : "n/a";
  rawYEl.textContent = player.rawFaceYNormalized !== null ? formatPercent(player.rawFaceYNormalized) : "n/a";
  calibrationEl.textContent = describeCalibration(player);
  confidenceEl.textContent = hasFace ? formatNumber(player.faceConfidence, 3) : "n/a";
}

function renderScene() {
  resizeCanvasForDpr();
  drawCourt();
  drawLabels();
  drawPaddle("left");
  drawPaddle("right");
  drawBall();
}

function tick(now) {
  const dt = Math.min((now - state.game.lastTickMs) / 1000, 0.05);
  state.game.lastTickMs = now;

  updatePaddles(dt, now);
  updateBall(dt);
  renderSidebar(state.latestFaceCount);
  renderScene();

  requestAnimationFrame(tick);
}

elements.connectForm.addEventListener("submit", (event) => {
  event.preventDefault();
  connect(elements.wsUrl.value.trim());
});

elements.disconnectButton.addEventListener("click", () => {
  disconnect();
});

elements.toggleGame.addEventListener("click", () => {
  toggleRunning();
});

elements.resetGame.addEventListener("click", () => {
  resetRound();
});

elements.ballSpeed.addEventListener("input", () => {
  state.game.ballSpeed = Number(elements.ballSpeed.value);
  elements.ballSpeedValue.textContent = `${state.game.ballSpeed} px/s`;
  syncBallVelocity();
  renderSidebar(state.latestFaceCount);
});

elements.paddleHeight.addEventListener("input", () => {
  state.game.paddleHeight = Number(elements.paddleHeight.value);
  elements.paddleHeightValue.textContent = `${state.game.paddleHeight} px`;
});

elements.holdTime.addEventListener("input", () => {
  state.game.holdTimeMs = Number(elements.holdTime.value);
  elements.holdTimeValue.textContent = `${(state.game.holdTimeMs / 1000).toFixed(1)} s`;
});

elements.mirrorSides.addEventListener("change", () => {
  state.game.mirrorSides = elements.mirrorSides.checked;
  setGameNote(
    state.game.mirrorSides
      ? "Mirror mode is on. Left player follows the right half of the camera image and vice versa."
      : "Mirror mode is off. Each player follows the matching half of the camera image."
  );
});

elements.leftResetCalibration.addEventListener("click", () => {
  resetCalibration("left");
});

elements.rightResetCalibration.addEventListener("click", () => {
  resetCalibration("right");
});

setStatus("Offline", false);
setGameNote("Waiting for metadata. Put one detected face in each half of the source image.");
elements.ballSpeedValue.textContent = `${state.game.ballSpeed} px/s`;
elements.paddleHeightValue.textContent = `${state.game.paddleHeight} px`;
elements.holdTimeValue.textContent = `${(state.game.holdTimeMs / 1000).toFixed(1)} s`;
resetBall();
renderSidebar(0);
requestAnimationFrame(tick);
