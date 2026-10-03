// A small HTTP server: the simulator, plus /frame.png for anything that wants
// to fetch the current frame (handy for ESP32-style frames that poll a URL).

import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { join, normalize, extname, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { renderFrame } from '../render.js';
import { encodePNG } from './png.js';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '../..');
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.png': 'image/png', '.svg': 'image/svg+xml' };
// Only these directories are served.
const PUBLIC = ['simulator', 'src'];

function frameOptions(params) {
  const num = (k) => (params.has(k) ? Number(params.get(k)) : undefined);
  const opts = {};
  for (const k of ['width', 'height', 'rotate']) if (num(k) !== undefined) opts[k] = num(k);
  for (const k of ['clock', 'precision', 'salt']) if (params.has(k)) opts[k] = params.get(k);
  return opts;
}

export function startServer({ port = 8080, host = '0.0.0.0', defaults = {}, weather = null } = {}) {
  const server = createServer(async (req, res) => {
    const url = new URL(req.url, 'http://localhost');
    try {
      if (url.pathname === '/frame.png') {
        const at = url.searchParams.has('at') ? new Date(url.searchParams.get('at')) : new Date();
        if (Number.isNaN(at.getTime())) {
          res.writeHead(400, { 'content-type': 'text/plain' });
          return res.end('bad ?at= date');
        }
        const forecast = weather && url.searchParams.get('weather') !== 'none' ? await weather.forecast(at) : null;
        const { bitmap, catch: c } = renderFrame(at, { ...defaults, ...frameOptions(url.searchParams), weather: forecast });
        res.writeHead(200, { 'content-type': 'image/png', 'cache-control': 'no-store', 'x-fish-name': c.name });
        return res.end(encodePNG(bitmap));
      }
      if (url.pathname === '/') {
        res.writeHead(302, { location: '/simulator/' });
        return res.end();
      }
      let path = normalize(decodeURIComponent(url.pathname)).replace(/^[/\\]+/, '');
      if (path.endsWith('/')) path += 'index.html';
      if (!PUBLIC.some((dir) => path.startsWith(`${dir}/`))) throw Object.assign(new Error(), { code: 'ENOENT' });
      const body = await readFile(join(ROOT, path));
      res.writeHead(200, { 'content-type': TYPES[extname(path)] ?? 'application/octet-stream' });
      res.end(body);
    } catch (err) {
      res.writeHead(err.code === 'ENOENT' || err.code === 'EISDIR' ? 404 : 500, { 'content-type': 'text/plain' });
      res.end(err.code ? 'not found' : String(err.message));
    }
  });
  return new Promise((resolve) => server.listen(port, host, () => resolve(server)));
}
