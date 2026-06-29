const GAME_WIDTH = 1280;
const GAME_HEIGHT = 720;
const HALF_WIDTH = GAME_WIDTH / 2;
const BALL_RADIUS = 10;
const PADDLE_WIDTH = 16;
const PADDLE_MARGIN = 26;
const MAX_BOUNCE_ANGLE = Math.PI / 3;
const DEFAULT_FRAME_WIDTH = 1280;
const DEFAULT_FRAME_HEIGHT = 720;
const RANDOM_UINT32_RANGE = 0x100000000;

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
  leftTrackId: document.getElementById("left-track-id"),
  rightTrackId: document.getElementById("right-track-id"),
  leftResetCalibration: document.getElementById("left-reset-calibration"),
  rightResetCalibration: document.getElementById("right-reset-calibration"),
  leftResetMapping: document.getElementById("left-reset-mapping"),
  rightResetMapping: document.getElementById("right-reset-mapping"),
  ballSpeed: document.getElementById("ball-speed"),
  ballSpeedValue: document.getElementById("ball-speed-value"),
  paddleHeight: document.getElementById("paddle-height"),
  paddleHeightValue: document.getElementById("paddle-height-value"),
  holdTime: document.getElementById("hold-time"),
  holdTimeValue: document.getElementById("hold-time-value"),
  faceFilterAlpha: document.getElementById("face-filter-alpha"),
  faceFilterAlphaValue: document.getElementById("face-filter-alpha-value"),
  mirrorSides: document.getElementById("mirror-sides"),
  debugOverlay: document.getElementById("debug-overlay"),
};

const context = elements.canvas.getContext("2d");
const randomValues = new Uint32Array(1);

const state = {
  socket: null,
  messagesReceived: 0,
  frameCounter: "n/a",
  lastMessageAt: null,
  latestFaceCount: 0,
  latestFaces: [],
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
    faceFilterAlpha: Number(elements.faceFilterAlpha.value),
    mirrorSides: elements.mirrorSides.checked,
    showDebugOverlay: elements.debugOverlay.checked,
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
    faceCenterX: null,
    activeTrackId: null,
    lockedTrackId: null,
    assignedFaceIndex: null,
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

function gameplayRandom() {
  window.crypto.getRandomValues(randomValues);
  return randomValues[0] / RANDOM_UINT32_RANGE;
}

function trimTextTokenValue(value) {
  let end = value.length;
  while (end > 0 && ",;)]}".includes(value[end - 1])) {
    end -= 1;
  }
  return value.slice(0, end);
}

function parseTextNumberToken(text, prefixes) {
  let token = "";

  for (const char of text) {
    if (char <= " ") {
      const value = parseNumberFromToken(token, prefixes);
      if (value !== null) {
        return value;
      }
      token = "";
      continue;
    }

    token += char;
  }

  return parseNumberFromToken(token, prefixes);
}

function parseNumberFromToken(token, prefixes) {
  for (const prefix of prefixes) {
    if (token.startsWith(prefix)) {
      const value = Number(trimTextTokenValue(token.slice(prefix.length)));
      return Number.isFinite(value) ? value : null;
    }
  }

  return null;
}

function getControlledHalf(side) {
  if (!state.game.mirrorSides) {
    return side;
  }
  return side === "left" ? "right" : "left";
}

function getDefaultRegionBounds(side, frameWidth) {
  const splitX = frameWidth / 2;
  const controlledHalf = getControlledHalf(side);
  return controlledHalf === "left"
    ? { minX: 0, maxX: splitX }
    : { minX: splitX, maxX: frameWidth };
}

function enrichFaces(faces) {
  return faces.map((face, index) => {
    const width = Math.max(Number(face.width ?? 0), 1);
    const height = Math.max(Number(face.height ?? 0), 1);
    const attributes = face && typeof face.attributes === "object" ? face.attributes : {};
    const text = typeof face.text === "string" ? face.text : "";
    const textTrackId = parseTextNumberToken(text, ["ID:"]);
    const textSimilarity = parseTextNumberToken(text, ["REID:", "REID-R:"]);
    const rawTrackId =
      attributes.trackId ??
      face.trackId ??
      textTrackId;
    const rawSimilarity =
      attributes.similarityIndex ??
      attributes.similarity ??
      attributes.reidSimilarity ??
      face.similarityIndex ??
      textSimilarity;
    return {
      ...face,
      index,
      width,
      height,
      centerX: Number(face.x) + width / 2,
      centerY: Number(face.y) + height / 2,
      confidence: Number(face.confidence ?? 0),
      trackId: Number.isFinite(Number(rawTrackId)) ? Number(rawTrackId) : null,
      similarityIndex: Number.isFinite(Number(rawSimilarity)) ? Number(rawSimilarity) : null,
    };
  });
}

function chooseBestFace(candidates) {
  if (candidates.length === 0) {
    return null;
  }

  return candidates.reduce((best, current) => {
    if (current.confidence !== best.confidence) {
      return current.confidence > best.confidence ? current : best;
    }
    return current.centerY < best.centerY ? current : best;
  });
}

function chooseInitialLockFace(candidates) {
  return chooseBestFace(candidates.filter((face) => face.trackId !== null));
}

function assignFacesToPlayers(faces, dimensions) {
  const assignments = new Map();
  const orderedSides = ["left", "right"].sort((sideA, sideB) =>
    Number(state.players[sideB].lockedTrackId !== null) - Number(state.players[sideA].lockedTrackId !== null)
  );

  for (const side of orderedSides) {
    const player = state.players[side];
    const regionBounds = getDefaultRegionBounds(side, dimensions.width);
    const initialLockCandidates = faces.filter((face) =>
      face.centerX >= regionBounds.minX &&
      face.centerX <= regionBounds.maxX
    );

    let selectedFace = null;

    if (player.lockedTrackId !== null) {
      selectedFace = chooseBestFace(
        faces.filter((face) => face.trackId !== null && face.trackId === player.lockedTrackId)
      );
    } else {
      selectedFace = chooseInitialLockFace(initialLockCandidates);
      if (selectedFace) {
        player.lockedTrackId = selectedFace.trackId;
      }
    }

    if (!selectedFace) {
      player.assignedFaceIndex = null;
      player.activeTrackId = null;
      continue;
    }

    player.assignedFaceIndex = selectedFace.index;
    player.activeTrackId = selectedFace.trackId;
    assignments.set(side, selectedFace);
  }

  return assignments;
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
          attributes: detection.data.attributes ?? null,
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

  const enrichedFaces = enrichFaces(faces.faces);
  const assignments = assignFacesToPlayers(enrichedFaces, dimensions);
  const assignedSidesByIndex = new Map();

  for (const [side, face] of assignments.entries()) {
    assignedSidesByIndex.set(face.index, side);
  }

  state.latestFaces = enrichedFaces.map((face) => ({
    ...face,
    assignedSide: assignedSidesByIndex.get(face.index) ?? null,
  }));

  for (const side of ["left", "right"]) {
    const player = state.players[side];
    const face = assignments.get(side);

    if (!face) {
      continue;
    }

    const rawNormalizedY = clamp(face.centerY / dimensions.height, 0, 1);
    player.calibration.top =
      player.calibration.top === null ? rawNormalizedY : Math.min(player.calibration.top, rawNormalizedY);
    player.calibration.bottom =
      player.calibration.bottom === null ? rawNormalizedY : Math.max(player.calibration.bottom, rawNormalizedY);
    player.filteredFaceYNormalized = lowPass(
      player.filteredFaceYNormalized,
      rawNormalizedY,
      state.game.faceFilterAlpha,
    );
    const calibratedY = getCalibratedFaceY(player, player.filteredFaceYNormalized);

    player.rawFaceYNormalized = rawNormalizedY;
    player.faceYNormalized = calibratedY;
    player.faceCenterX = face.centerX;
    player.targetY = calibratedY * GAME_HEIGHT;
    player.faceConfidence = face.confidence;
    player.activeTrackId = face.trackId;
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
      player.faceCenterX = null;
      player.activeTrackId = null;
      player.assignedFaceIndex = null;
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

function resetPlayerMapping(side) {
  const player = state.players[side];
  player.lockedTrackId = null;
  player.activeTrackId = null;
  player.assignedFaceIndex = null;

  state.latestFaces = state.latestFaces.map((face) =>
    face.assignedSide === side ? { ...face, assignedSide: null } : face
  );

  renderSidebar(state.latestFaceCount);
  setGameNote(`${side === "left" ? "Left" : "Right"} face lock reset. The next tracked face in that half can claim the paddle.`);
}

function connect(url) {
  disconnect();
  setStatus(`Connecting to ${url}...`, false);

  const socket = new WebSocket(url);
  state.socket = socket;

  socket.onopen = () => {
    setStatus(`Connected to ${url}`, true);
    setGameNote("Metadata live. Put one tracked face in each half to lock both paddles.");
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
    state.latestFaces = [];
    state.latestFaceCount = 0;
    setStatus(`Disconnected from ${url}`, false);
    setGameNote("Metadata disconnected. Paddle IDs stay locked until you reset the round.");
  };
}

function disconnect() {
  state.latestFaces = [];
  state.latestFaceCount = 0;
  if (!state.socket) {
    setStatus("Offline", false);
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
    const direction = gameplayRandom() > 0.5 ? 1 : -1;
    ball.vx = direction * state.game.ballSpeed;
    ball.vy = (gameplayRandom() * 2 - 1) * state.game.ballSpeed * 0.4;
    return;
  }

  const scale = state.game.ballSpeed / magnitude;
  ball.vx *= scale;
  ball.vy *= scale;
}

function resetBall(direction = gameplayRandom() > 0.5 ? 1 : -1) {
  const angle = (gameplayRandom() * 0.8 - 0.4) * MAX_BOUNCE_ANGLE;
  state.game.ball.x = GAME_WIDTH / 2;
  state.game.ball.y = GAME_HEIGHT / 2;
  state.game.ball.vx = Math.cos(angle) * state.game.ballSpeed * direction;
  state.game.ball.vy = Math.sin(angle) * state.game.ballSpeed;
}

function resetRound() {
  state.scores.left = 0;
  state.scores.right = 0;
  resetBall();
  renderSidebar(state.latestFaceCount);
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

function projectXToGame(x) {
  return (x / Math.max(state.frameDimensions.width, 1)) * GAME_WIDTH;
}

function projectYToGame(y) {
  return (y / Math.max(state.frameDimensions.height, 1)) * GAME_HEIGHT;
}

function drawTrackingRegion(side) {
  const regionBounds = getDefaultRegionBounds(side, state.frameDimensions.width);
  const x = projectXToGame(regionBounds.minX);
  const width = projectXToGame(regionBounds.maxX) - x;
  const player = state.players[side];
  const calibrationTop = player.calibration.top ?? 0;
  const calibrationBottom = player.calibration.bottom ?? 1;
  const y = projectYToGame(calibrationTop * state.frameDimensions.height);
  const bottomY = projectYToGame(calibrationBottom * state.frameDimensions.height);
  const height = Math.max(bottomY - y, 4);
  const fillColor = side === "left" ? "rgba(73, 213, 255, 0.5)" : "rgba(255, 125, 77, 0.5)";
  const strokeColor = side === "left" ? "rgba(73, 213, 255, 0.9)" : "rgba(255, 125, 77, 0.9)";

  context.fillStyle = fillColor;
  context.fillRect(x, y, width, height);
  context.strokeStyle = strokeColor;
  context.lineWidth = 2;
  context.strokeRect(x, y, width, height);
}

function drawDetectedFace(face) {
  const x = projectXToGame(Number(face.x));
  const y = projectYToGame(Number(face.y));
  const width = projectXToGame(Number(face.width));
  const height = projectYToGame(Number(face.height));
  const locked = face.assignedSide !== null;

  context.strokeStyle = locked ? "rgba(84, 255, 140, 0.96)" : "rgba(255, 72, 72, 0.96)";
  context.lineWidth = locked ? 3 : 2;
  context.strokeRect(x, y, width, height);

  const identityLabel = face.trackId !== null ? `ID ${face.trackId}` : "ID ?";
  const similarityLabel = face.similarityIndex !== null
    ? `SIM ${Math.round(face.similarityIndex * 100)}%`
    : "SIM n/a";
  const label = locked
    ? `${face.assignedSide.toUpperCase()} ${identityLabel} ${similarityLabel}`
    : `${identityLabel} ${similarityLabel}`;
  context.font = "700 14px 'Avenir Next', 'Segoe UI', sans-serif";
  context.fillStyle = locked ? "rgba(84, 255, 140, 0.96)" : "rgba(255, 90, 90, 0.96)";
  context.fillText(label, x + 4, Math.max(16, y - 8));
}

function drawDebugOverlay() {
  if (!state.game.showDebugOverlay) {
    return;
  }

  drawTrackingRegion("left");
  drawTrackingRegion("right");

  for (const face of state.latestFaces) {
    drawDetectedFace(face);
  }
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
  const trackIdEl = side === "left" ? elements.leftTrackId : elements.rightTrackId;
  const hasFace = player.faceYNormalized !== null;

  presenceEl.textContent = hasFace ? "Tracking" : "No face";
  presenceEl.classList.toggle("online", hasFace);
  presenceEl.classList.toggle("offline", !hasFace);
  yEl.textContent = hasFace ? formatPercent(player.faceYNormalized) : "n/a";
  rawYEl.textContent = player.rawFaceYNormalized !== null ? formatPercent(player.rawFaceYNormalized) : "n/a";
  calibrationEl.textContent = describeCalibration(player);
  confidenceEl.textContent = hasFace ? formatNumber(player.faceConfidence, 3) : "n/a";
  trackIdEl.textContent = player.lockedTrackId !== null ? String(player.lockedTrackId) : "Unlocked";
}

function renderScene() {
  resizeCanvasForDpr();
  drawCourt();
  drawDebugOverlay();
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

elements.faceFilterAlpha.addEventListener("input", () => {
  state.game.faceFilterAlpha = Number(elements.faceFilterAlpha.value);
  elements.faceFilterAlphaValue.textContent = state.game.faceFilterAlpha.toFixed(2);
});

elements.mirrorSides.addEventListener("change", () => {
  state.game.mirrorSides = elements.mirrorSides.checked;
  setGameNote(
    state.game.mirrorSides
      ? "Mirror mode is on. Initial face-ID locks use the opposite camera half for each paddle."
      : "Mirror mode is off. Initial face-ID locks use the matching camera half for each paddle."
  );
});

elements.debugOverlay.addEventListener("change", () => {
  state.game.showDebugOverlay = elements.debugOverlay.checked;
});

elements.leftResetCalibration.addEventListener("click", () => {
  resetCalibration("left");
});

elements.rightResetCalibration.addEventListener("click", () => {
  resetCalibration("right");
});

elements.leftResetMapping.addEventListener("click", () => {
  resetPlayerMapping("left");
});

elements.rightResetMapping.addEventListener("click", () => {
  resetPlayerMapping("right");
});

setStatus("Offline", false);
setGameNote("Waiting for metadata. Put one tracked face in each half of the source image to lock each paddle.");
elements.ballSpeedValue.textContent = `${state.game.ballSpeed} px/s`;
elements.paddleHeightValue.textContent = `${state.game.paddleHeight} px`;
elements.holdTimeValue.textContent = `${(state.game.holdTimeMs / 1000).toFixed(1)} s`;
elements.faceFilterAlphaValue.textContent = state.game.faceFilterAlpha.toFixed(2);
resetBall();
renderSidebar(0);
requestAnimationFrame(tick);
