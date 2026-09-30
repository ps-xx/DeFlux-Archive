'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');
const { WebSocketServer } = require('ws');

const HOST = process.env.DEFLUX_NUI_HOST || '127.0.0.1';
const PORT = Number(process.env.DEFLUX_NUI_PORT || 7010);
const NUI_ROOT = path.join(__dirname, '..', 'nui');

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.png': 'image/png',
  '.svg': 'image/svg+xml',
};

function sendFile(res, filePath) {
  const ext = path.extname(filePath).toLowerCase();
  const type = MIME[ext] || 'application/octet-stream';
  fs.readFile(filePath, (err, data) => {
    if (err) {
      res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' });
      res.end('Not found');
      return;
    }
    res.writeHead(200, { 'Content-Type': type });
    res.end(data);
  });
}

const server = http.createServer((req, res) => {
  const urlPath = decodeURIComponent((req.url || '/').split('?')[0]);
  const safeRel = urlPath === '/' ? 'index.html' : urlPath.replace(/^\/+/, '');
  const filePath = path.normalize(path.join(NUI_ROOT, safeRel));

  if (!filePath.startsWith(NUI_ROOT)) {
    res.writeHead(403, { 'Content-Type': 'text/plain; charset=utf-8' });
    res.end('Forbidden');
    return;
  }

  sendFile(res, filePath);
});

const wss = new WebSocketServer({ server, path: '/nui' });

wss.on('connection', (socket) => {
  socket.send(JSON.stringify({ type: 'hello', source: 'DeFlux_Launcher', protocol: 1 }));

  socket.on('message', (raw) => {
    let msg;
    try {
      msg = JSON.parse(String(raw));
    } catch {
      socket.send(JSON.stringify({ type: 'error', message: 'invalid_json' }));
      return;
    }

    if (msg.type === 'ping') {
      socket.send(JSON.stringify({ type: 'pong', t: Date.now() }));
      return;
    }

    // Broadcast NUI events to other local clients (CEF / overlay stubs).
    for (const client of wss.clients) {
      if (client !== socket && client.readyState === 1) {
        client.send(JSON.stringify({ type: 'nui_event', payload: msg }));
      }
    }
  });
});

server.listen(PORT, HOST, () => {
  console.log(`[DeFlux_Launcher] NUI HTTP  http://${HOST}:${PORT}/`);
  console.log(`[DeFlux_Launcher] NUI WS    ws://${HOST}:${PORT}/nui`);
});
