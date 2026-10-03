// Display backends for the clock runner. Each one has:
//   width, height   panel size (null = use the configured size)
//   partial         true if it can update without a full-screen flash
//   show(bitmap, mode)  mode is 'full' or 'partial'
//   close()

import { spawn, execFile } from 'node:child_process';
import { createInterface } from 'node:readline';
import { writeFile, rename, mkdtemp } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { encodePNG } from './png.js';

const BRIDGE = join(dirname(fileURLToPath(import.meta.url)), '../../hardware/epd_bridge.py');

async function writeAtomic(path, buf) {
  await writeFile(`${path}.tmp`, buf);
  await rename(`${path}.tmp`, path);
}

// Write each frame to a PNG (e.g. for a web server or another program).
function fileDisplay({ out }) {
  return {
    width: null,
    height: null,
    partial: true,
    async show(bitmap) {
      await writeAtomic(out, encodePNG(bitmap));
    },
    close() {},
  };
}

// Write the frame, then run a command with {path} and {mode} filled in.
function execDisplay({ out, exec }) {
  return {
    width: null,
    height: null,
    partial: exec.includes('{mode}'),
    async show(bitmap, mode) {
      await writeAtomic(out, encodePNG(bitmap));
      const cmd = exec.replaceAll('{path}', out).replaceAll('{mode}', mode);
      await new Promise((resolve, reject) =>
        execFile('/bin/sh', ['-c', cmd], (err, stdout, stderr) => {
          if (stdout) process.stdout.write(stdout);
          if (stderr) process.stderr.write(stderr);
          err ? reject(err) : resolve();
        }),
      );
    },
    close() {},
  };
}

// Talk to hardware/epd_bridge.py, which drives the panel with the vendor's
// Python library. It speaks one JSON object per line on stdin/stdout.
async function bridgeDisplay({ driver, python = 'python3' }) {
  const dir = await mkdtemp(join(tmpdir(), 'fish-fodder-'));
  const frame = join(dir, 'frame.png');
  const child = spawn(python, [BRIDGE, '--driver', driver], { stdio: ['pipe', 'pipe', 'inherit'] });
  const lines = createInterface({ input: child.stdout });
  const replies = [];
  const waiting = [];
  lines.on('line', (line) => {
    let msg;
    try {
      msg = JSON.parse(line);
    } catch {
      return; // not ours
    }
    const w = waiting.shift();
    w ? w(msg) : replies.push(msg);
  });
  const exited = new Promise((resolve) => child.on('exit', (code) => resolve({ error: `bridge exited (${code})` })));
  const next = () => Promise.race([new Promise((r) => (replies.length ? r(replies.shift()) : waiting.push(r))), exited]);
  const request = async (msg) => {
    child.stdin.write(`${JSON.stringify(msg)}\n`);
    const reply = await next();
    if (reply.error) throw new Error(`epd_bridge: ${reply.error}`);
    return reply;
  };

  const hello = await next();
  if (hello.error) throw new Error(`epd_bridge: ${hello.error}`);
  return {
    width: hello.width,
    height: hello.height,
    partial: hello.partial,
    async show(bitmap, mode) {
      await writeAtomic(frame, encodePNG(bitmap));
      await request({ cmd: 'show', path: frame, mode });
    },
    async close() {
      try {
        await request({ cmd: 'sleep' });
      } finally {
        child.stdin.end();
      }
    },
  };
}

export async function openDisplay(opts) {
  if (opts.driver === 'file') return fileDisplay(opts);
  if (opts.driver === 'exec') return execDisplay(opts);
  return bridgeDisplay(opts);
}
