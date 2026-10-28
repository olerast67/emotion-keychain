// Снимки страницы просмотра для README: брелок версии S в сборе и в разборе.
// node shots.js out/keychain-3d.html DIR — viewer.py --shots потом обрезает их по брелоку.
// Если Chromium не достаёт до CDN, THREE_DIR — распакованный пакет npm three@0.128.0
// (та же сборка r128, что грузит страница).
const path = require("path");
const { chromium } = require("playwright");

const [html, outDir] = process.argv.slice(2);
const THREE_DIR = process.env.THREE_DIR;
const SHOTS = [["case-3d", "assembled", 1000, 1340], ["case-exploded", "exploded", 1560, 1260]];

(async () => {
  const browser = await chromium.launch({ args: ["--ignore-gpu-blocklist"] });
  for (const [name, shot, width, height] of SHOTS) {
    const page = await browser.newPage({ viewport: { width, height } });
    page.on("pageerror", (e) => { console.error(e); process.exit(1); });
    if (THREE_DIR) {
      await page.route("**/three.min.js", (r) => r.fulfill({ path: path.join(THREE_DIR, "build/three.min.js") }));
      await page.route("**/OrbitControls.js",
        (r) => r.fulfill({ path: path.join(THREE_DIR, "examples/js/controls/OrbitControls.js") }));
      await page.route("https://fonts.googleapis.com/**", (r) => r.abort());
    }
    await page.goto("file://" + path.resolve(html) + `#v=s&shot=${shot}`);
    await page.waitForFunction(() => window.__shotReady === true, null, { timeout: 60000 });
    await page.locator("#scene").screenshot({ path: path.join(outDir, name + ".png") });
    await page.close();
  }
  await browser.close();
})().catch((e) => { console.error(e); process.exit(1); });
