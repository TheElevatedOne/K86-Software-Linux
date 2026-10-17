// Generates tests/golden/{v1,v2}.txt by running the ORIGINAL device classes from the
// Attack Shark driver (not redistributed here) against a recording HID stub.
//
//   node tests/js/run.mjs <extracted>/resources/app/dist/js tests/golden
//
// The scenario names and arguments must match tests/test_main.cpp.
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import url from "node:url";

const [srcDir, outDir] = process.argv.slice(2);
if (!srcDir || !outDir) {
  console.error("usage: node run.mjs <dist/js dir of the original app> <output dir>");
  process.exit(1);
}
const here = path.dirname(url.fileURLToPath(import.meta.url));
const work = fs.mkdtempSync(path.join(os.tmpdir(), "k86js-"));
fs.writeFileSync(path.join(work, "package.json"), '{"type":"module"}');
fs.copyFileSync(path.join(here, "stub_index.js"), path.join(work, "index.b078bf5f.js"));
// YC3121 K86: b74085e4 (+ base 438d24dc); YC3123 K86: 95287242 (+ 63fce7a0, 5e635fe2)
for (const f of ["438d24dc.js", "b74085e4.js", "5e635fe2.js", "63fce7a0.js", "95287242.js"])
  fs.copyFileSync(path.join(srcDir, f), path.join(work, f));

const stub = await import(url.pathToFileURL(path.join(work, "index.b078bf5f.js")));
const { Yc3121_sg8967_3m_soc } = await import(url.pathToFileURL(path.join(work, "b74085e4.js")));
const { Yc3123_sg_sg8967_3m_1k } = await import(url.pathToFileURL(path.join(work, "95287242.js")));

const deviceType = (pid, id) => ({
  id, vid: 12625, pid, type: "keyboard", connect: "usb", devAddr: "/dev/hidraw0",
  company: "AttackShark", version: {}, other: {}, layer: 3
});

const hex = r => [...r].map(v => v.toString(16).padStart(2, "0")).join("");

// Slot colours / matrix shared with test_main.cpp.
const colorOf = i => (i * 0x010203 + 0x405060) & 0xffffff;
function picArr(kb) {
  const out = [];
  const m = kb.defaultMatrix;
  for (let s = 0; s < m.length / 4; s++) {
    const e = m.slice(s * 4, s * 4 + 4);
    if (e.every(v => v === 0)) continue;
    let index = 0;
    for (let p = 0; p < s; p++) if (stub.ah(m.slice(p * 4, p * 4 + 4), e)) index++;
    out.push({ hid: stub.ag.matrixToHid(e), index, rgb: colorOf(s) });
  }
  return out;
}
const testMatrix = kb => { const m = [...kb.defaultMatrix]; m[9 * 4 + 2] = 5; return m; };

const macro = (events, repeatCount) => {
  const out = [];
  for (const [kind, value, delay, dx, dy] of events) {
    if (kind === "move") out.push({ type: "mouse_move", dx, dy });
    else if (kind === "mouse") out.push({ type: "mouse_button", action: value[0], value: value[1] });
    else out.push({ type: "keyboard", action: kind, value });
    out.push({ type: "delay", value: delay });
  }
  return { macro: out, repeatCount };
};

function scenarios(v2) {
  const A = 4;  // HID usage of the key in slot 9 on both models
  return {
    deviceId: kb => kb.getCommonDeviceId(),
    versions: async kb => {
      await kb.getUSBVersion();
      if (v2) await kb.getRFVersion();
      await kb.getMledVersion();
      await kb.getOledVersion();
    },
    setProfile: kb => kb.setCurrentProfile(2),
    getProfile: kb => kb.getCurrentProfile(),
    setDebounce: kb => kb.setDeBounce(10),
    getDebounce: kb => kb.getDeBounce(),
    setSleep: kb => kb.setSleepTime({ time_bt: 300, time_24: 600, deepTime_bt: 1800, deepTime_24: 3600 }),
    getSleep: kb => kb.getSleepTime(),
    setLightWave: kb => kb.setLightSetting({ type: "LightWave", option: 2, bright: 3, speed: 1, rgb: 0x12ab34, dazzle: false }),
    setLightRainbow: kb => kb.setLightSetting({ type: "LightBreath", option: 0, bright: 4, speed: 3, rgb: 0xffffff, dazzle: true }),
    setLightCustom: kb => kb.setLightSetting({ type: "LightUserPicture", option: 1, bright: 2, speed: 0, rgb: 0x00ff00, dazzle: false }),
    setLightMusic: kb => kb.setLightSetting({ type: "LightMusicFollow2", option: 2, bright: 4, speed: 0, rgb: 0x0000ff, dazzle: false }),
    getLight: kb => kb.getLightSetting(),
    setKey: kb => v2
      ? kb.setKeyConfigSimple({ type: "combo", original: A, index: 0, skey: 0xe0, key: 6, key2: 0 }, 1, 0, true)
      : kb.setKeyConfigSimple({ type: "combo", original: A, index: 0, skey: 0xe0, key: 6, key2: 0 }, 1),
    setKeyMatrix: kb => kb._setKeyConfig(testMatrix(kb), 1, v2 ? 0 : undefined),
    getKeyMatrix: kb => kb._getKeyMatrix(2, 0),
    setFnKey: kb => kb.setFnKeyConfigSimple({ type: "ConfigFunction", original: A, index: 0, key: "音量加", value: [3, 0, 233, 0] }, 0, "mac"),
    setFnMatrix: kb => kb._setFnKeyConfig(testMatrix(kb), 0, "mac"),
    getFnMatrix: kb => kb._getFnKeyMatrix(0, "mac"),
    setMacro: kb => kb.setMacro(macro([
      ["down", 0xe0, 5], ["down", 6, 200], ["up", 6, 200], ["up", 0xe0, 30],
      ["mouse", ["down", -1000], 1], ["mouse", ["up", -1000], 1], ["move", 0, 300, -5, 7]
    ], 2), 4),
    setMacroLong: kb => {
      const ev = [];
      for (let i = 0; i < 40; i++) ev.push(["down", 4, 20], ["up", 4, 20]);
      return kb.setMacro(macro(ev, 1), 7);
    },
    getMacro: kb => kb.getMacro(4),
    setUserPicture: kb => kb.setLightPic(picArr(kb), 2),
    getUserPicture: kb => kb._getLightPic(2),
    userAnimation: async kb => {
      await kb.setUserGifStart();
      await kb.setUserGif({ picArr: picArr(kb), currentFrame: 1, frameNum: 3, frameDelay: 300 });
    },
    screen: async kb => {
      const data = []; for (let i = 0; i < 130; i++) data.push((i * 7) & 255);
      const img = { currentFrame: 2, frameNum: 1, frameDelay: 0, data, left: 3, top: 4, right: 300, bottom: 120 };
      await kb.getTFTLCDDataRGBImg("16", img);
      await kb.setTFTLCDDataRGBImg("16", img);
    },
    sysinfo: kb => kb.setOLEDSysInfo({
      diskSpceAvailable: 123.4 * 2 ** 30, diskSpaceTotal: 512 * 2 ** 30, memUsed: 7.6 * 2 ** 30, memTotal: 32 * 2 ** 30,
      cpuUsage: 42, cpuTemperater: 57, netWorkTotalUp: 3.5 * 2 ** 30, netWorkTotalDown: 300.2 * 2 ** 30
    }),
    language: kb => kb.setOLEDLanguageSwitch(true),
    erase: kb => kb.setFlashChipErase(),
    reset: kb => kb.reset(),
    getOptions: async kb => {
      if (v2) { await kb.getKBOption(); await kb.getKbPowerSaveMode(); } else await kb.getOldKBOption(0);
    },
    setOptions: async kb => {
      if (v2) {
        await kb.getKBOption(); await kb.getKbPowerSaveMode();
        await kb.setKBOption({ fnIndex: 1, 防误触开关: true, RTStab: 50, WASDSwap: true });
      } else {
        await kb.getOldKBOption(0);
        await kb.setOldKBOption(0, { winKeyLockControl: true, system: "win", wasdKeyAndArrowKeyExchange: false, ledOff: true,
          sLedOff: false, keyboardMode: false, keyboardLockControl: false, keyboardFnKeyMatrix: false, powerSaveMode: false });
      }
    },
    ...(v2 ? {
      setSideLightWave: kb => kb.setSideLightSetting({ type: "LightWave", option: 0, bright: 3, speed: 2, rgb: 0x00ffff, dazzle: false }),
      setSideLightNeon: kb => kb.setSideLightSetting({ type: "LightNeon", option: 0, bright: 4, speed: 1, rgb: 0xff0000, dazzle: true }),
      getSideLight: kb => kb.getSideLightSetting(),
    } : {})
  };
}

fs.mkdirSync(outDir, { recursive: true });
for (const [tag, Cls, pid, id, v2] of [["v1", Yc3121_sg8967_3m_soc, 16405, 1168, false], ["v2", Yc3123_sg_sg8967_3m_1k, 20482, 2730, true]]) {
  const lines = [];
  for (const [name, fn] of Object.entries(scenarios(v2))) {
    const kb = new Cls(deviceType(pid, id));
    stub.sent.length = 0;
    await fn(kb);
    lines.push("# " + name, ...stub.sent.map(hex));
  }
  fs.writeFileSync(path.join(outDir, tag + ".txt"), lines.join("\n") + "\n");
  console.log(`${tag}: ${lines.filter(l => !l.startsWith("#")).length} reports`);
}
fs.rmSync(work, { recursive: true, force: true });
