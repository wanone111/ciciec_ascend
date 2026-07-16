const camera = document.querySelector("#camera-stream");
const cameraPlaceholder = document.querySelector("#camera-placeholder");
const cameraState = document.querySelector("#camera-state");
const video = document.querySelector("#anime-video");
const wsDot = document.querySelector("#ws-dot");
const wsState = document.querySelector("#ws-state");
const eventPill = document.querySelector("#event-pill");
const eventState = document.querySelector("#event-state");
const eventValue = document.querySelector("#event-value");
const sourceValue = document.querySelector("#source-value");
const systemValue = document.querySelector("#system-value");

let config;
let socket;
let isIdle = true;
let retryTimer;
let cameraRetryTimer;

function setVideo(src, { idle = false, label = "IDLE" } = {}) {
  if (!src) return;
  isIdle = idle;
  video.loop = idle;
  video.src = src;
  video.load();
  video.play().catch(() => {
    systemValue.textContent = "点击页面以开始播放";
  });
  eventPill.textContent = label.toUpperCase();
  eventValue.textContent = label.toUpperCase();
  eventState.textContent = idle ? "等待互动" : "正在播放互动";
}

function playIdle() {
  setVideo(config.idle, { idle: true, label: "IDLE" });
}

function handleEvent(message) {
  const key = message.dialogue_id || message.scene;
  const src = config.commands[key];
  sourceValue.textContent = message.source_value || message.source_type || "—";
  if (!src || !isIdle) return;
  setVideo(src, { idle: false, label: key });
}

function connectWebSocket() {
  const protocol = location.protocol === "https:" ? "wss" : "ws";
  socket = new WebSocket(`${protocol}://${location.host}/ws`);
  socket.onopen = () => {
    wsDot.classList.add("online");
    wsState.textContent = "系统在线";
    systemValue.textContent = "Ascend 已连接";
  };
  socket.onmessage = (event) => {
    try { handleEvent(JSON.parse(event.data)); } catch (_) { /* ignore */ }
  };
  socket.onclose = () => {
    wsDot.classList.remove("online");
    wsState.textContent = "连接中断，正在重连";
    systemValue.textContent = "重连中";
    clearTimeout(retryTimer);
    retryTimer = setTimeout(connectWebSocket, 2000);
  };
}

async function boot() {
  config = await fetch("/frontend-config").then((response) => response.json());

  function connectCamera() {
    cameraPlaceholder.classList.remove("hidden");
    cameraState.textContent = "正在验证视频源";
    const snapshot = new Image();
    snapshot.onload = () => {
      camera.src = `${config.camera_stream_url}?t=${Date.now()}`;
      cameraPlaceholder.classList.add("hidden");
      cameraState.textContent = "310B 画面在线";
    };
    snapshot.onerror = () => {
      cameraState.textContent = "310B 视频流重连中";
      clearTimeout(cameraRetryTimer);
      cameraRetryTimer = setTimeout(connectCamera, 2000);
    };
    snapshot.src = `${config.camera_snapshot_url}?t=${Date.now()}`;
  }
  camera.onerror = connectCamera;
  video.addEventListener("ended", playIdle);
  document.addEventListener("click", () => video.play().catch(() => {}), { once: true });
  playIdle();
  connectCamera();
  connectWebSocket();
}

boot().catch((error) => {
  systemValue.textContent = `启动失败: ${error.message}`;
});
