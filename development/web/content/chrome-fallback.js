(function () {
    "use strict";

    const params = new URLSearchParams(window.location.search);
    const forceFallback = params.has("chrome-http-fallback") || params.has("chrome-reset");
    const snapshotUrl = "/api/ctrl-snapshot";
    const pollMs = 1200;
    const takeoverDelayMs = 2200;
    const preferredModelOrder = [
        "YoloV11",
        "OsnetX025Reid",
        "Ultraface",
        "CameraContact",
        "GazeDetection",
    ];

    let fallbackActive = forceFallback;
    let inFlight = false;
    let controlSocket = null;
    let controlQueue = [];
    let latestMetricsText = "";
    let latestInferenceText = "";
    let pipelineById = new Map();
    let currentPipelineId = "";
    let pipelineActionBusy = false;
    let lastModelsSignature = "";
    let infoPanel = null;
    let activeInfoButton = null;
    let activeInfoKey = null;
    let infoRequestId = 0;
    const descriptionCache = new Map();
    const modelInfoByKey = new Map();
    window.PEK_FEED_PAUSED = Boolean(window.PEK_FEED_PAUSED);
    let currentPlaying = !window.PEK_FEED_PAUSED;
    let lastPauseToggleAt = 0;

    function byId(id) {
        return document.getElementById(id);
    }

    function escapeHtml(value) {
        return String(value ?? "")
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/"/g, "&quot;")
            .replace(/'/g, "&#039;");
    }

    function modelKey(modelOrName) {
        return String(modelOrName?.name || modelOrName || "").toLowerCase();
    }

    function modelStateKey(model) {
        return modelKey(model?.element_name || model?.name);
    }

    function dependencyTokensForModel(modelName) {
        const lowered = modelKey(modelName);

        if (lowered.includes("cameracontact") || lowered.includes("gazedetection")) {
            return ["ultraface"];
        }

        if (lowered.includes("osnetx025reid")) {
            return ["yolov11"];
        }

        return [];
    }

    function modelNameMatchesDependency(name, token) {
        const lowered = modelKey(name);

        if (lowered === token) {
            return true;
        }

        if (!lowered.startsWith(token) || lowered.length <= token.length) {
            return false;
        }

        const separator = lowered[token.length];
        return separator === " " || separator === "-" || separator === "_";
    }

    function buildForcedDependencyState(models) {
        const forcedBy = new Map();

        models.forEach((model) => {
            if (!model.active) {
                return;
            }

            dependencyTokensForModel(model.name).forEach((dependencyToken) => {
                models.forEach((candidate) => {
                    if (
                        modelStateKey(candidate) !== modelStateKey(model) &&
                        modelNameMatchesDependency(candidate.name, dependencyToken)
                    ) {
                        const key = modelStateKey(candidate);
                        const children = forcedBy.get(key) || [];
                        children.push(model.name);
                        forcedBy.set(key, children);
                    }
                });
            });
        });

        return forcedBy;
    }

    function isStillEmpty() {
        const models = byId("models-container");
        const metrics = byId("performanceMetricsBody");
        const inference = byId("inferenceOutputBody");
        const pipelineSelect = byId("pipelineSelect");
        const pipelineButton = byId("restartPipelineBtn");

        return Boolean(
            models?.querySelector(".models-loading, .models-empty, .models-error") ||
            metrics?.querySelector(".performance-metrics-empty") ||
            inference?.querySelector(".inference-output-empty") ||
            pipelineSelect?.disabled ||
            !pipelineSelect?.options?.length ||
            pipelineButton?.disabled
        );
    }

    function normalizeSnapshot(data) {
        if (!data || data.error) return null;

        if (data.perception_data) {
            data.performance = data.performance || data.perception_data.performance;
            data.inference_output = data.inference_output || data.perception_data.inference_output;
        }

        return data;
    }

    function selectedPipelineChanged() {
        const select = byId("pipelineSelect");
        return Boolean(select?.value && currentPipelineId && select.value !== currentPipelineId);
    }

    function setPipelineStatus(message, tone = "muted") {
        const status = byId("pipelineSwitchStatus");
        if (!status) return;

        status.textContent = message || "";
        status.dataset.tone = tone;
    }

    function setNormalStreamStatus() {
        const statusLine = byId("status-line");
        const statusText = statusLine?.querySelector(".status-line-text");
        const overlay = byId("video-overlay");
        const overlayText = byId("overlay-text");

        statusLine?.classList.remove("connecting", "reconnecting", "disconnected");
        statusLine?.classList.add("connected");

        if (statusText && /^(Restarting|Switching) Pipeline$/.test(statusText.textContent || "")) {
            statusText.textContent = "WebRTC connected";
        }

        if (overlayText?.textContent === "Restarting" || overlayText?.textContent === "Switching") {
            overlay?.classList.add("hidden");
            overlayText.textContent = "Connecting…";
        }
    }

    function setPipelineBusy(busy, mode = selectedPipelineChanged() ? "switch" : "restart") {
        pipelineActionBusy = busy;
        const button = byId("restartPipelineBtn");
        const text = button?.querySelector(".video-control-text");
        const icon = button?.querySelector(".video-control-icon");
        if (!button || !text || !icon) return;

        document.body.classList.toggle("is-pipeline-restarting", busy);
        button.disabled = busy || !byId("pipelineSelect")?.value;
        button.style.opacity = busy ? "0.7" : "";

        const isSwitch = mode === "switch";
        button.setAttribute("aria-label", isSwitch ? "Switch pipeline" : "Restart pipeline");
        text.textContent = busy
            ? (isSwitch ? "Switching" : "Restarting")
            : (isSwitch ? "Switch" : "Restart");
        icon.className = busy
            ? "video-control-icon restart-button-spinner"
            : (isSwitch ? "video-control-icon fa-solid fa-right-left" : "video-control-icon fa-solid fa-rotate-right");

        const statusText = document.querySelector("#status-line .status-line-text");
        const overlay = byId("video-overlay");
        const overlayText = byId("overlay-text");
        if (busy) {
            const label = isSwitch ? "Switching" : "Restarting";
            if (statusText) statusText.textContent = `${label} Pipeline`;
            overlay?.classList.remove("hidden");
            if (overlayText) overlayText.textContent = label;
        } else if (overlayText?.textContent === "Restarting" || overlayText?.textContent === "Switching") {
            setNormalStreamStatus();
        }
    }

    function updatePipelineAction() {
        const select = byId("pipelineSelect");
        const button = byId("restartPipelineBtn");
        const text = button?.querySelector(".video-control-text");
        const icon = button?.querySelector(".video-control-icon");
        if (!select || !button || !text || !icon) return;

        const isSwitch = selectedPipelineChanged();
        button.disabled = pipelineActionBusy || select.disabled || !select.value;
        button.setAttribute("aria-label", isSwitch ? "Switch pipeline" : "Restart pipeline");
        text.textContent = isSwitch ? "Switch" : "Restart";
        icon.className = isSwitch
            ? "video-control-icon fa-solid fa-right-left"
            : "video-control-icon fa-solid fa-rotate-right";
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

    async function loadPipelines() {
        const select = byId("pipelineSelect");
        const button = byId("restartPipelineBtn");
        if (!select || !button) return;

        try {
            const response = await fetch(`/api/pipelines?classic=${Date.now()}`, { cache: "no-store" });
            if (!response.ok) throw new Error(`Pipeline list failed: ${response.status}`);
            const payload = await response.json();
            const pipelines = Array.isArray(payload?.pipelines) ? payload.pipelines : [];
            pipelineById = new Map(pipelines.map((pipeline) => [pipeline.id, pipeline]));
            select.innerHTML = "";

            if (!pipelines.length) {
                const option = document.createElement("option");
                option.textContent = "No pipelines found";
                select.append(option);
                select.disabled = true;
                button.disabled = true;
                setPipelineStatus("No pipeline presets were found.", "warning");
                return;
            }

            pipelines.forEach((pipeline) => {
                const option = document.createElement("option");
                option.value = pipeline.id;
                option.textContent = pipeline.label || pipeline.id;
                option.title = pipeline.description || pipeline.path || "";
                select.append(option);
            });

            const current = payload.current || "";
            const currentOption = [...select.options].find((option) => optionMatchesCurrent(option, current));
            if (currentOption) {
                select.value = currentOption.value;
                currentPipelineId = currentOption.value;
            } else {
                currentPipelineId = select.value;
            }

            select.disabled = false;
            setPipelineStatus(pipelineById.get(select.value)?.description || "");
            updatePipelineAction();
        } catch (error) {
            select.innerHTML = "<option>Pipeline list unavailable</option>";
            select.disabled = true;
            button.disabled = true;
            setPipelineStatus("Pipeline presets are unavailable from this WebUI launch.", "warning");
            console.warn(error);
        }
    }

    function renderPlayPause(isPlaying) {
        const button = byId("playPauseBtn");
        const icon = byId("playPauseIcon");
        const text = button?.querySelector(".video-control-text");
        if (!button || !icon || !text) return;

        currentPlaying = Boolean(isPlaying);
        window.PEK_FEED_PAUSED = !currentPlaying;
        document.body.classList.toggle("is-feed-paused", !currentPlaying);
        button.disabled = false;
        button.style.opacity = "";
        icon.classList.toggle("fa-pause", currentPlaying);
        icon.classList.toggle("fa-play", !currentPlaying);
        text.textContent = currentPlaying ? "Pause" : "Resume";
        button.setAttribute("aria-label", currentPlaying ? "Pause feed" : "Resume feed");
    }

    function freezeFeedFrame() {
        const video = byId("video");
        if (!video) return;

        const wrapper = video.closest(".video-wrapper");
        if (!wrapper) return;

        let canvas = byId("videoFreezeFrame");
        if (!canvas) {
            canvas = document.createElement("canvas");
            canvas.id = "videoFreezeFrame";
            canvas.className = "video-freeze-frame";
            canvas.setAttribute("aria-hidden", "true");
            wrapper.appendChild(canvas);
        }

        const width = video.videoWidth || Math.max(1, Math.round(video.clientWidth || wrapper.clientWidth || 1280));
        const height = video.videoHeight || Math.max(1, Math.round(video.clientHeight || wrapper.clientHeight || 720));
        canvas.width = width;
        canvas.height = height;

        try {
            canvas.getContext("2d")?.drawImage(video, 0, 0, width, height);
        } catch (error) {
            console.warn("Video freeze frame failed", error);
        }

        canvas.classList.add("is-visible");
    }

    async function resumeFeedFrame() {
        byId("videoFreezeFrame")?.classList.remove("is-visible");

        try {
            const video = byId("video");
            if (video?.paused) {
                await video.play();
            }
        } catch (error) {
            console.warn("Video resume failed", error);
        }
    }

    function orderModels(models) {
        const preferred = new Map(preferredModelOrder.map((name, index) => [modelKey(name), index]));

        return [...models].sort((a, b) => {
            const aOrder = preferred.get(modelKey(a));
            const bOrder = preferred.get(modelKey(b));

            if (aOrder !== undefined || bOrder !== undefined) {
                return (aOrder ?? Number.MAX_SAFE_INTEGER) - (bOrder ?? Number.MAX_SAFE_INTEGER);
            }

            return String(a.name || "").localeCompare(String(b.name || ""));
        });
    }

    function toggleMarkup(model, forcedBy = []) {
        const isForced = forcedBy.length > 0;
        const displayActive = Boolean(model.active || isForced);
        const ariaLabel = isForced
            ? `${model.name} is required by ${forcedBy.join(", ")}`
            : `Toggle ${model.name}`;

        return `
            <label class="model-toggle-switch" aria-label="${escapeHtml(ariaLabel)}" ${isForced ? 'data-tooltip="Cannot be disabled as this model is required by an enabled model."' : ""}>
                <input type="checkbox" role="switch" ${displayActive ? "checked" : ""} ${isForced ? "disabled aria-disabled=\"true\"" : ""}>
                <span class="model-toggle-track" aria-hidden="true">
                    <span class="model-toggle-thumb"></span>
                </span>
            </label>
        `;
    }

    function infoMarkup(model) {
        return `<button class="model-info-button" type="button" data-model-key="${escapeHtml(modelKey(model))}" data-info-text="${escapeHtml(model.description || "Loading information...")}" aria-label="Information for ${escapeHtml(model.name)}" aria-expanded="false">i</button>`;
    }

    function getInfoPanel() {
        if (infoPanel)
            return infoPanel;

        infoPanel = byId("model-info-panel");
        if (!infoPanel) {
            infoPanel = document.createElement("div");
            infoPanel.id = "model-info-panel";
            document.body.appendChild(infoPanel);
        }

        infoPanel.className = "model-info-panel";
        infoPanel.style.position = "fixed";
        infoPanel.style.zIndex = "1000";
        infoPanel.style.display = "none";
        infoPanel.setAttribute("aria-hidden", "true");
        return infoPanel;
    }

    function hideModelInfo() {
        const panel = getInfoPanel();
        panel.style.display = "none";
        panel.setAttribute("aria-hidden", "true");

        if (activeInfoButton) {
            activeInfoButton.setAttribute("aria-expanded", "false");
        }
        activeInfoButton = null;
        activeInfoKey = null;
        infoRequestId += 1;
    }

    function positionModelInfo(button, text) {
        const panel = getInfoPanel();
        panel.textContent = text || "No description available";
        panel.style.display = "block";
        panel.setAttribute("aria-hidden", "false");

        const rect = button.getBoundingClientRect();
        const panelRect = panel.getBoundingClientRect();
        const gap = 10;

        let left = rect.right + gap;
        let top = rect.top + (rect.height - panelRect.height) / 2;

        if (left + panelRect.width > window.innerWidth - 12) {
            left = Math.max(8, rect.left - panelRect.width - gap);
        }
        if (top < 8) {
            top = 8;
        }
        if (top + panelRect.height > window.innerHeight - 8) {
            top = Math.max(8, window.innerHeight - panelRect.height - 8);
        }

        panel.style.left = `${left}px`;
        panel.style.top = `${top}px`;
    }

    async function fetchModelDescription(model) {
        const cacheKey = `${model.name || ""}::${model.element_name || ""}`;
        if (descriptionCache.has(cacheKey)) {
            return descriptionCache.get(cacheKey);
        }

        const lookupCandidates = [model.name, model.element_name].filter(
            (value, index, array) => value && array.indexOf(value) === index
        );

        for (const lookupName of lookupCandidates) {
            try {
                const response = await fetch(`/api/model-info?name=${encodeURIComponent(lookupName)}`, { cache: "no-store" });
                if (!response.ok) continue;

                const payload = await response.json();
                const description = payload.description || payload.opchain?.description || payload.model?.description || "";
                if (description) {
                    descriptionCache.set(cacheKey, description);
                    return description;
                }
            } catch {
                // Fall back to inline model description below.
            }
        }

        const fallbackDescription = model.description || model.desc || "";
        descriptionCache.set(cacheKey, fallbackDescription);
        return fallbackDescription;
    }

    async function showModelInfo(infoButton, model) {
        if (!infoButton || !model)
            return;

        const key = modelKey(model);
        if (activeInfoKey === key && getInfoPanel().style.display !== "none") {
            positionModelInfo(infoButton, getInfoPanel().textContent);
            return;
        }

        if (activeInfoButton) {
            activeInfoButton.setAttribute("aria-expanded", "false");
        }

        const requestId = ++infoRequestId;
        activeInfoButton = infoButton;
        activeInfoKey = key;
        infoButton.setAttribute("aria-expanded", "true");

        const description = await fetchModelDescription(model);
        if (requestId !== infoRequestId || activeInfoKey !== key) {
            return;
        }

        activeInfoButton = infoButton;
        infoButton.setAttribute("aria-expanded", "true");
        positionModelInfo(infoButton, description);
    }

    function modelForInfoButton(infoButton) {
        return modelInfoByKey.get(infoButton?.dataset.modelKey || "") || null;
    }

    function attachModelInfoDelegates(container) {
        if (!container || container.dataset.classicInfoHover === "true")
            return;

        container.dataset.classicInfoHover = "true";
        container.addEventListener("mouseover", (event) => {
            const infoButton = event.target.closest?.(".model-info-button");
            if (!infoButton || !container.contains(infoButton)) return;
            if (infoButton.contains(event.relatedTarget)) return;
            showModelInfo(infoButton, modelForInfoButton(infoButton));
        });
        container.addEventListener("mouseout", (event) => {
            const infoButton = event.target.closest?.(".model-info-button");
            if (!infoButton || !container.contains(infoButton)) return;
            if (infoButton.contains(event.relatedTarget)) return;
            if (activeInfoKey === infoButton.dataset.modelKey) hideModelInfo();
        });
        container.addEventListener("click", (event) => {
            const infoButton = event.target.closest?.(".model-info-button");
            if (!infoButton || !container.contains(infoButton)) return;
            event.preventDefault();
            event.stopPropagation();
        });
    }

    function primeModelInfoText(infoButton, model) {
        if (!infoButton)
            return;

        fetchModelDescription(model).then((description) => {
            if (infoButton.isConnected) {
                infoButton.dataset.infoText = description || "No description available";
            }
        });
    }

    function renderModels(models) {
        const container = byId("models-container");
        if (!container || !Array.isArray(models)) return;
        attachModelInfoDelegates(container);

        const forcedBy = buildForcedDependencyState(models);
        const nextSignature = JSON.stringify(models.map((model) => ({
            active: Boolean(model.active),
            element_name: model.element_name || "",
            forcedBy: forcedBy.get(modelStateKey(model)) || [],
            name: model.name || "",
        })));
        if (nextSignature === lastModelsSignature) {
            return;
        }
        lastModelsSignature = nextSignature;
        hideModelInfo();

        if (!models.length) {
            container.innerHTML = '<div class="models-empty">No models registered yet</div>';
            return;
        }

        container.innerHTML = "";
        modelInfoByKey.clear();
        orderModels(models).forEach((model) => {
            modelInfoByKey.set(modelKey(model), model);
            const item = document.createElement("div");
            item.className = "model-item";
            const forcedChildren = forcedBy.get(modelStateKey(model)) || [];
            if (forcedChildren.length) {
                item.classList.add("model-item--dependency-forced");
            }

            item.innerHTML = `
                <div class="model-info"><div class="model-name">${escapeHtml(model.name)}</div></div>
                <div class="model-actions">${toggleMarkup(model, forcedChildren)}${infoMarkup(model)}</div>
            `;

            const toggle = item.querySelector("input[type='checkbox']");
            toggle?.addEventListener("change", () => {
                if (toggle.disabled) {
                    return;
                }

                sendControl({
                    type: "model_toggle",
                    name: model.element_name || model.name,
                    active: toggle.checked,
                });
            });

            primeModelInfoText(item.querySelector(".model-info-button"), model);

            container.appendChild(item);
        });
    }

    function updateControlsFromSnapshot(data) {
        if (data.pipeline_state) {
            if (!pipelineActionBusy) {
                setNormalStreamStatus();
            }
        }
    }

    function parseMetricLine(line) {
        const text = String(line || "").replace(/[═]+/g, "").trim();
        if (!text) return null;

        const fps = text.match(/^Pipeline\s*:?\s*([0-9.]+)\s+FPS$/);
        if (fps) {
            return { stage: "Pipeline", current: `${fps[1]} FPS`, p95: "" };
        }

        const metric = text.match(/^(.*?)\s*:\s*([0-9.]+ms)\s*(?:\(p95:\s*([0-9.]+ms)\))?$/);
        if (!metric) return null;

        return {
            stage: metric[1].trim(),
            current: metric[2],
            p95: metric[3] || "",
        };
    }

    function setCopyAvailable(button, available) {
        if (!button) return;
        button.disabled = !available;
        button.setAttribute("aria-disabled", available ? "false" : "true");
        button.classList.toggle("is-disabled", !available);
    }

    function renderPerformance(performance) {
        const body = byId("performanceMetricsBody");
        if (!body) return;

        const rows = (Array.isArray(performance?.lines) ? performance.lines : [])
            .map(parseMetricLine)
            .filter(Boolean);

        latestMetricsText = rows.length
            ? ["Stage\tCurrent\tP95", ...rows.map((row) => `${row.stage}\t${row.current}\t${row.p95}`)].join("\n")
            : "";
        setCopyAvailable(byId("copyPerformanceMetricsBtn"), rows.length > 0);

        if (!rows.length) {
            body.innerHTML = '<tr><td colspan="3" class="performance-metrics-empty">No metrics yet</td></tr>';
            return;
        }

        body.innerHTML = rows.map((row) => `
            <tr>
                <td>${escapeHtml(row.stage)}</td>
                <td>${escapeHtml(row.current)}</td>
                <td>${escapeHtml(row.p95)}</td>
            </tr>
        `).join("");
    }

    function formatNumber(value, digits = 2) {
        return typeof value === "number" && Number.isFinite(value) ? value.toFixed(digits) : "";
    }

    function detectionSummary(detection) {
        const type = detection?.type || "Detection";
        const data = detection?.data || {};

        if (type === "Rect") {
            return {
                title: data.text || `Class ${data.classId ?? "-"}`,
                detail: `box x ${formatNumber(data.x)} y ${formatNumber(data.y)} w ${formatNumber(data.width)} h ${formatNumber(data.height)}`,
                meta: formatNumber(data.confidence),
            };
        }

        if (type === "YawPitch") {
            return {
                title: "Yaw / pitch",
                detail: `yaw ${formatNumber(data.yaw)} pitch ${formatNumber(data.pitch)}`,
                meta: formatNumber(data.confidence),
            };
        }

        if (type === "Classification") {
            const best = Array.isArray(data.candidates) ? data.candidates[0] : null;
            return {
                title: best?.text || `Class ${best?.classId ?? "-"}`,
                detail: "classification",
                meta: formatNumber(best?.confidence),
            };
        }

        return {
            title: type,
            detail: Object.keys(data).slice(0, 4).join(", ") || "output",
            meta: "",
        };
    }

    function layerTitle(layer) {
        return layer.model || layer.contentType || layer.engine || "Layer";
    }

    function renderInference(output) {
        const body = byId("inferenceOutputBody");
        if (!body) return;

        const layers = Array.isArray(output?.layers)
            ? output.layers.filter((layer) => layer.model || layer.contentType || layer.engine)
            : [];

        const copyable = layers.filter((layer) => {
            const detections = Array.isArray(layer.detections) ? layer.detections : [];
            return detections.length > 0 || Number(layer.count || 0) > 0;
        });

        latestInferenceText = copyable.map((layer) => {
            const detections = Array.isArray(layer.detections) ? layer.detections : [];
            const rows = detections.map(detectionSummary);
            return [
                `${layerTitle(layer)} (${layer.contentType || layer.labelFamily || "output"}): ${layer.count || 0}`,
                ...rows.map((row) => `- ${row.title}${row.meta ? ` [${row.meta}]` : ""}: ${row.detail}`),
            ].join("\n");
        }).join("\n\n");
        setCopyAvailable(byId("copyInferenceOutputBtn"), Boolean(latestInferenceText));

        if (!layers.length) {
            body.innerHTML = '<div class="inference-output-empty">No inference output yet, enable a model to see inference here.</div>';
            return;
        }

        body.innerHTML = layers.map((layer) => {
            const detections = Array.isArray(layer.detections) ? layer.detections : [];
            const rows = detections.map(detectionSummary);
            return `
                <div class="inference-layer">
                    <div class="inference-layer-header">
                        <span class="inference-layer-title">${escapeHtml(layerTitle(layer))}</span>
                        <span class="inference-layer-count">${escapeHtml(layer.count || 0)}</span>
                    </div>
                    <div class="inference-layer-kind">${escapeHtml(layer.contentType || layer.labelFamily || "output")}</div>
                    ${rows.length ? `
                        <div class="inference-detections">
                            ${rows.map((row) => `
                                <div class="inference-detection">
                                    <div class="inference-detection-main">
                                        <span class="inference-detection-title">${escapeHtml(row.title)}</span>
                                        ${row.meta ? `<span class="inference-detection-meta">${escapeHtml(row.meta)}</span>` : ""}
                                    </div>
                                    <div class="inference-detection-detail">${escapeHtml(row.detail)}</div>
                                </div>
                            `).join("")}
                        </div>
                    ` : '<div class="inference-output-empty">No detections</div>'}
                </div>
            `;
        }).join("");
    }

    async function copyText(button, text) {
        if (!text) return;
        try {
            await navigator.clipboard.writeText(text);
            flashCopyButton(button);
        } catch (error) {
            const buffer = byId("debugLogCopyBuffer") || document.createElement("textarea");
            buffer.value = text;
            document.body.appendChild(buffer);
            buffer.select();
            document.execCommand("copy");
            flashCopyButton(button);
        }
    }

    async function requestPipelineAction() {
        const select = byId("pipelineSelect");
        if (!select?.value || pipelineActionBusy) return;

        const isSwitch = selectedPipelineChanged();
        setPipelineBusy(true, isSwitch ? "switch" : "restart");
        setPipelineStatus(isSwitch ? "Requesting pipeline switch..." : "Requesting pipeline restart...");

        try {
            const response = await fetch(isSwitch ? "/api/pipeline-switch" : "/api/pipeline-restart", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: isSwitch ? JSON.stringify({ pipeline: select.value }) : "{}",
            });
            const detail = await response.json().catch(() => ({}));
            setPipelineStatus(
                detail.message || (response.ok ? (isSwitch ? "Pipeline switched." : "Pipeline restarted.") : "Pipeline action failed."),
                response.ok && detail.complete ? "muted" : "warning",
            );

            if (response.ok && detail.complete) {
                currentPipelineId = isSwitch ? (detail.pipeline || select.value) : currentPipelineId;
                window.dispatchEvent(new CustomEvent("pipeline-layout-reset"));
            }
        } catch (error) {
            setPipelineStatus("Pipeline action failed because the supervisor could not be reached.", "warning");
            console.warn(error);
        } finally {
            setPipelineBusy(false, selectedPipelineChanged() ? "switch" : "restart");
            updatePipelineAction();
            setTimeout(() => {
                loadPipelines();
                pollSnapshot();
            }, 900);
        }
    }

    async function requestPlayPause(event) {
        if (!fallbackActive) return;

        event?.preventDefault();
        event?.stopImmediatePropagation();

        const now = Date.now();
        if (now - lastPauseToggleAt < 350) return;
        lastPauseToggleAt = now;

        const nextPlaying = !currentPlaying;
        if (nextPlaying) {
            await resumeFeedFrame();
        } else {
            freezeFeedFrame();
        }

        window.PEK_FEED_PAUSED = !nextPlaying;
        renderPlayPause(nextPlaying);
    }

    function attachControlFallbacks() {
        const select = byId("pipelineSelect");
        if (select && !select.dataset.classicFallbackControl) {
            select.dataset.classicFallbackControl = "true";
            select.addEventListener("change", () => {
                if (!fallbackActive) return;
                setPipelineStatus(pipelineById.get(select.value)?.description || "");
                updatePipelineAction();
            }, true);
        }

        const pipelineButton = byId("restartPipelineBtn");
        if (pipelineButton && !pipelineButton.dataset.classicFallbackControl) {
            pipelineButton.dataset.classicFallbackControl = "true";
            pipelineButton.addEventListener("click", (event) => {
                if (!fallbackActive) return;
                event.preventDefault();
                event.stopImmediatePropagation();
                requestPipelineAction();
            }, true);
        }

        const playButton = byId("playPauseBtn");
        if (playButton && !playButton.dataset.classicFallbackControl) {
            playButton.dataset.classicFallbackControl = "true";
            ["pointerdown", "mousedown", "pointerup", "click"].forEach((eventName) => {
                playButton.addEventListener(eventName, requestPlayPause, true);
            });
        }
    }

    function flashCopyButton(button) {
        const icon = button?.querySelector("i");
        if (!button || !icon) return;

        const previousLabel = button.getAttribute("aria-label") || button.title || "Copy";
        icon.className = "fa-solid fa-check";
        button.dataset.copyState = "copied";
        button.setAttribute("aria-label", "Copied");
        button.title = "Copied";
        setTimeout(() => {
            icon.className = "fa-solid fa-copy";
            button.dataset.copyState = "idle";
            button.setAttribute("aria-label", previousLabel);
            button.title = previousLabel;
        }, 1500);
    }

    function attachCopyFallbacks() {
        const metricsButton = byId("copyPerformanceMetricsBtn");
        if (metricsButton && !metricsButton.dataset.classicFallbackCopy) {
            metricsButton.dataset.classicFallbackCopy = "true";
            metricsButton.addEventListener("click", (event) => {
                if (!fallbackActive) return;
                event.preventDefault();
                event.stopImmediatePropagation();
                copyText(metricsButton, latestMetricsText);
            }, true);
        }

        const inferenceButton = byId("copyInferenceOutputBtn");
        if (inferenceButton && !inferenceButton.dataset.classicFallbackCopy) {
            inferenceButton.dataset.classicFallbackCopy = "true";
            inferenceButton.addEventListener("click", (event) => {
                if (!fallbackActive) return;
                event.preventDefault();
                event.stopImmediatePropagation();
                copyText(inferenceButton, latestInferenceText);
            }, true);
        }
    }

    function sendControl(payload) {
        const serialized = JSON.stringify(payload);

        if (controlSocket?.readyState === WebSocket.OPEN) {
            controlSocket.send(serialized);
            return;
        }

        controlQueue.push(serialized);
        if (controlSocket?.readyState === WebSocket.CONNECTING) return;

        const protocol = location.protocol === "https:" ? "wss" : "ws";
        const proxyPath = window.PEK_CONFIG?.ctrlProxyPath || "/ctrl-ws";
        controlSocket = new WebSocket(`${protocol}://${location.host}${proxyPath}`);
        controlSocket.onopen = () => {
            while (controlQueue.length && controlSocket?.readyState === WebSocket.OPEN) {
                controlSocket.send(controlQueue.shift());
            }
        };
        controlSocket.onclose = () => {
            controlSocket = null;
        };
    }

    function applySnapshot(snapshot) {
        const data = normalizeSnapshot(snapshot);
        if (!data) return;

        window.dispatchEvent(new CustomEvent("ctrl-message", { detail: data }));

        if (!fallbackActive && isStillEmpty()) {
            fallbackActive = true;
            document.documentElement.dataset.chromeFallback = "active";
        }

        if (!fallbackActive) return;

        updateControlsFromSnapshot(data);
        if (data.models) renderModels(data.models);
        if (data.performance) renderPerformance(data.performance);
        if (data.inference_output) renderInference(data.inference_output);
    }

    async function pollSnapshot() {
        if (inFlight) return;
        inFlight = true;
        try {
            const response = await fetch(`${snapshotUrl}?classic=${Date.now()}`, { cache: "no-store" });
            if (response.ok) {
                applySnapshot(await response.json());
            }
        } catch (error) {
            console.warn("Classic control fallback failed", error);
        } finally {
            inFlight = false;
        }
    }

    attachCopyFallbacks();
    attachControlFallbacks();
    loadPipelines();
    setTimeout(() => {
        if (isStillEmpty()) fallbackActive = true;
        if (fallbackActive) {
            attachControlFallbacks();
            loadPipelines();
        }
        pollSnapshot();
    }, takeoverDelayMs);
    setInterval(pollSnapshot, pollMs);
})();
