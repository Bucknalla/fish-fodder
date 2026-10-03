// Bundle the simulator into one self-contained HTML file (dist/simulator.html)
// that opens straight from disk, with no server.
//   --fragment   write the page without <html>/<head>/<body>, for hosts that
//                supply their own document skeleton

import { build } from 'esbuild';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('..', import.meta.url));
const fragment = process.argv.includes('--fragment');

const { outputFiles } = await build({
  entryPoints: [`${root}simulator/app.js`],
  bundle: true,
  minify: true,
  format: 'iife',
  write: false,
});
const js = outputFiles[0].text.replaceAll('</script', '<\\/script');

let html = await readFile(`${root}simulator/index.html`, 'utf8');
html = html
  .replace('<script type="module" src="app.js"></script>', () => `<script>${js}</script>`)
  // Saving goes through the local server's page only; drop it from the bundle.
  .replace(/\s*<script type="module" src="save.js"><\/script>/, '');
if (fragment) {
  html = html
    .replace(/<!doctype html>\s*<html[^>]*>\s*<head>\s*/i, '')
    .replace(/<meta charset[^>]*>\s*<meta name="viewport"[^>]*>\s*/i, '')
    .replace(/<\/head>\s*<body>\s*/i, '')
    .replace(/<\/body>\s*<\/html>\s*$/i, '\n');
}
await mkdir(`${root}dist`, { recursive: true });
const out = `${root}dist/simulator${fragment ? '-fragment' : ''}.html`;
await writeFile(out, html);
console.log(`wrote ${out} (${(html.length / 1024).toFixed(0)} KB)`);
