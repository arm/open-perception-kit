import { ctrlSend } from "./ctrlws.js?v=flat-model-toggles-20260608";

const select = document.getElementById("pipelineSelect");
const actionButton = document.getElementById("restartPipelineBtn");
const actionText = actionButton?.querySelector(".video-control-text");
const actionIcon = actionButton?.querySelector(".video-control-icon");
const status = document.getElementById("pipelineSwitchStatus");
const statusLine = document.getElementById("status-line");
const statusLineText = statusLine?.querySelector(".status-line-text");
const videoOverlay = document.getElementById("video-overlay");
const videoOverlayText = document.getElementById("overlay-text");

let pipelineById = new Map();
let currentPipelineId = "";
let actionInProgress = false;
let restartInProgress = false;
let restartRecoveryTimer = null;
const RESTART_SESSION_KEY = "pekPipelineRestarting";

function selectedPipelineChanged() {
    return !!select?.value && !!currentPipelineId && select.value !== currentPipelineId;
}

function currentMode() {
    return selectedPipelineChanged() ? "switch" : "restart";
}

function setStatus(message, tone = "muted") {
    if (!status) return;

    status.textContent = message || "";
    status.dataset.tone = tone;
}

function setActionVisual(mode, busy = false) {
    if (!actionButton || !actionText || !actionIcon) return;

    const isSwitch = mode === "switch";
    actionButton.setAttribute("aria-label", isSwitch ? "Switch pipeline" : "Restart pipeline");
    actionText.textContent = busy
        ? (isSwitch ? "Switching" : "Restarting")
        : (isSwitch ? "Switch" : "Restart");

    if (busy) {
        actionIcon.className = "video-control-icon restart-button-spinner";
    } else {
        actionIcon.className = isSwitch
            ? "video-control-icon fa-solid fa-right-left"
            : "video-control-icon fa-solid fa-rotate-right";
    }
}

function updateActionState() {
    if (!actionButton) return;

    actionButton.disabled = actionInProgress || !select?.value || select.disabled;
    actionButton.style.opacity = actionInProgress ? "0.7" : "";
    setActionVisual(currentMode(), actionInProgress);
}

function setBusy(busy, mode = currentMode()) {
    actionInProgress = busy;
    document.body.classList.toggle("is-pipeline-restarting", busy);
    if (busy) {
        sessionStorage.setItem(RESTART_SESSION_KEY, "true");
    } else {
        sessionStorage.removeItem(RESTART_SESSION_KEY);
    }

    updateActionState();
    setActionVisual(mode, busy);

    if (busy) {
        const label = mode === "switch" ? "Switching" : "Restarting";
        statusLine?.classList.remove("connected", "disconnected");
        statusLine?.classList.add("reconnecting");
        if (statusLineText) statusLineText.textContent = `${label} Pipeline`;
        videoOverlay?.classList.remove("hidden");
        if (videoOverlayText) videoOverlayText.textContent = label;
    } else if (videoOverlayText?.textContent === "Restarting" ||
               videoOverlayText?.textContent === "Switching") {
        videoOverlay?.classList.add("hidden");
        if (statusLineText && /^(Restarting|Switching) Pipeline$/.test(statusLineText.textContent || "")) {
            statusLineText.textContent = "Connecting";
        }
    }
}

function optionMatchesCurrent(option, current) {
    if (!current) return false;

    const pipeline = pipelineById.get(option.value);
    const currentBase = current.split("/").pop()?.replace(/\.json$/, "") || "";
    return option.value === current ||
        pipeline?.path === current ||
        current.endsWith(`/${option.value}.json`) ||
        currentBase === option.value ||
        currentBase.startsWith(`${option.value}-`);
}

function renderPipelines(payload) {
    if (!select || !actionButton) return;

    const pipelines = Array.isArray(payload?.pipelines) ? payload.pipelines : [];
    pipelineById = new Map(pipelines.map((pipeline) => [pipeline.id, pipeline]));
    select.innerHTML = "";

    if (!pipelines.length) {
        const option = document.createElement("option");
        option.textContent = "No pipelines found";
        select.append(option);
        select.disabled = true;
        actionButton.disabled = true;
        setStatus("No pipeline presets were found.", "warning");
        return;
    }

    for (const pipeline of pipelines) {
        const option = document.createElement("option");
        option.value = pipeline.id;
        option.textContent = pipeline.label || pipeline.id;
        option.title = pipeline.description || pipeline.path || "";
        select.append(option);
    }

    const current = payload.current || "";
    const currentOption = [...select.options].find((option) => optionMatchesCurrent(option, current));
    if (currentOption) {
        select.value = currentOption.value;
        currentPipelineId = currentOption.value;
    } else {
        currentPipelineId = select.value;
    }

    select.disabled = false;
    setStatus(pipelineById.get(select.value)?.description || "");
    updateActionState();
}

async function loadPipelines() {
    if (!select) return;

    try {
        const response = await fetch("/api/pipelines", { cache: "no-store" });
        if (!response.ok) throw new Error(`Pipeline list failed: ${response.status}`);
        renderPipelines(await response.json());
    } catch (error) {
        select.innerHTML = "<option>Pipeline list unavailable</option>";
        select.disabled = true;
        if (actionButton) actionButton.disabled = true;
        setStatus("Pipeline presets are unavailable from this WebUI launch.", "warning");
        console.warn(error);
    }
}

function updateSelectedPipelineDescription() {
    const pipeline = pipelineById.get(select?.value);
    setStatus(pipeline?.description || "");
    updateActionState();
}

async function requestSwitch() {
    if (!select?.value || actionInProgress) return;

    setBusy(true, "switch");
    setStatus("Requesting pipeline switch...");

    try {
        const response = await fetch("/api/pipeline-switch", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ pipeline: select.value }),
        });
        const detail = await response.json();

        setStatus(
            detail.message || (response.ok ? "Pipeline switched." : "Pipeline switch failed."),
            response.ok && detail.complete ? "muted" : "warning",
        );

        if (response.ok && detail.complete) {
            currentPipelineId = detail.pipeline || select.value;
            window.dispatchEvent(new CustomEvent("pipeline-layout-reset"));
            window.dispatchEvent(new CustomEvent("pipeline-restart", {
                detail: { available: true, requested: false, complete: true },
            }));
        }
    } catch (error) {
        setStatus("Pipeline switch failed because the supervisor could not be reached.", "warning");
        console.warn(error);
    } finally {
        setBusy(false, currentMode());
        updateActionState();
    }
}

function showRestartUnavailable() {
    if (!actionButton || !actionText) return;

    actionButton.disabled = true;
    actionButton.style.opacity = "0.7";
    actionText.textContent = "Unavailable";

    setTimeout(() => {
        setBusy(false, "restart");
    }, 2200);
}

async function requestSupervisorRestart() {
    setBusy(true, "restart");
    restartInProgress = true;
    setStatus("Requesting pipeline restart...");

    if (restartRecoveryTimer) {
        clearTimeout(restartRecoveryTimer);
    }

    restartRecoveryTimer = setTimeout(() => {
        restartInProgress = false;
        setBusy(false, "restart");
        restartRecoveryTimer = null;
    }, 90000);

    try {
        const response = await fetch("/api/pipeline-restart", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: "{}",
        });
        const detail = await response.json();

        setStatus(
            detail.message || (response.ok ? "Pipeline restarted." : "Pipeline restart failed."),
            response.ok && detail.complete ? "muted" : "warning",
        );

        if (response.ok && detail.complete) {
            window.dispatchEvent(new CustomEvent("pipeline-layout-reset"));
            window.dispatchEvent(new CustomEvent("pipeline-restart", {
                detail: { available: true, requested: false, complete: true },
            }));
            return;
        }
    } catch (error) {
        setStatus("Pipeline restart failed because the supervisor could not be reached.", "warning");
        console.warn(error);
    }

    if (restartRecoveryTimer) {
        clearTimeout(restartRecoveryTimer);
        restartRecoveryTimer = null;
    }
    restartInProgress = false;
    setBusy(false, "restart");
}

function requestRestart() {
    if (window.PEK_CONFIG?.supervised) {
        requestSupervisorRestart();
        return;
    }

    setBusy(true, "restart");
    restartInProgress = true;
    ctrlSend({ type: "pipeline_restart" });

    if (restartRecoveryTimer) {
        clearTimeout(restartRecoveryTimer);
    }

    restartRecoveryTimer = setTimeout(() => {
        restartInProgress = false;
        setBusy(false, "restart");
        restartRecoveryTimer = null;
    }, 90000);
}

function requestPipelineAction() {
    if (currentMode() === "switch") {
        requestSwitch();
        return;
    }

    requestRestart();
}

select?.addEventListener("change", updateSelectedPipelineDescription);
actionButton?.addEventListener("click", requestPipelineAction);

window.addEventListener("pipeline-restart", (event) => {
    const detail = event.detail || {};

    if (detail.complete) {
        if (restartRecoveryTimer) {
            clearTimeout(restartRecoveryTimer);
            restartRecoveryTimer = null;
        }
        restartInProgress = false;
        setBusy(false, currentMode());
        return;
    }

    if (!detail.requested) {
        if (restartRecoveryTimer) {
            clearTimeout(restartRecoveryTimer);
            restartRecoveryTimer = null;
        }
        restartInProgress = false;
        showRestartUnavailable();
        return;
    }

    setBusy(true, "restart");
});

window.addEventListener("ctrl-connection", (event) => {
    if (!restartInProgress || event.detail?.state !== "connected") {
        return;
    }

    if (restartRecoveryTimer) {
        clearTimeout(restartRecoveryTimer);
        restartRecoveryTimer = null;
    }

    restartInProgress = false;
    setBusy(false, currentMode());
});

loadPipelines();
