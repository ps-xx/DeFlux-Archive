(() => {
  const statusEl = document.getElementById('status');
  const logEl = document.getElementById('log');
  const pingBtn = document.getElementById('ping');

  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  const wsUrl = `${proto}://${location.host}/nui`;
  const ws = new WebSocket(wsUrl);

  function log(line) {
    const stamp = new Date().toISOString().slice(11, 19);
    logEl.textContent = `[${stamp}] ${line}\n` + logEl.textContent;
  }

  ws.addEventListener('open', () => {
    statusEl.textContent = 'Connected to DeFlux NUI bridge';
    log('WebSocket open');
  });

  ws.addEventListener('close', () => {
    statusEl.textContent = 'Disconnected';
    log('WebSocket closed');
  });

  ws.addEventListener('message', (ev) => {
    log(`← ${ev.data}`);
  });

  pingBtn.addEventListener('click', () => {
    if (ws.readyState !== WebSocket.OPEN) {
      log('Socket not open');
      return;
    }
    ws.send(JSON.stringify({ type: 'ping', t: Date.now() }));
    log('→ ping');
  });
})();
