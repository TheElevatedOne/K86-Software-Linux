// Stand-in for the original driver's main bundle (index.b078bf5f.js). It provides only the
// exports used by the keyboard device classes. The helpers are copied from the original
// bundle; the HID base class records reports instead of sending them.
export const sent = [];

// ramda equals() for the flat arrays used here
const ii = (a, b) => {
  if (a === b) return true;
  if (a == null || b == null || a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
};
const uh = arr => [...arr].reduce((s, v) => s + v, 0);
const bi = () => Promise.resolve();

const Ce = { Left: -1000, Right: -999, Middle: -998, Forward: -997, Back: -996 };
const ba = {
  [Ce.Left]: [1, 0, 240, 0], [Ce.Right]: [1, 0, 241, 0], [Ce.Middle]: [1, 0, 242, 0],
  [Ce.Forward]: [1, 0, 244, 0], [Ce.Back]: [1, 0, 243, 0]
};
const Xo = (e, t) => Object.keys(e).find(i => ii(e[i], t));
const GD = e => e > 4294967295 ? [0, 0, 0, 0] : [(e & 4294967295) >> 24, (e & 16777215) >> 16, (e & 65535) >> 8, e & 255];

const Zi = {
  hidToMatrix: t => t <= 255 ? [0, 0, t, 0] : [t >> 24 & 255, t >> 16 & 255, t >> 8 & 255, t & 255],
  matrixToHid: t => {
    if (t[0] === 0 && t[1] === 0 && t[3] === 0) return t[2];
    const r = Number(Xo(ba, t));
    return isNaN(r) ? t[0] << 24 | t[1] << 16 | t[2] << 8 | t[3] : r;
  },
  configToMatrix: t => {
    switch (t.type) {
      case "forbidden": return [0, 0, 0, 0];
      case "combo": return t.key <= 255 ? [0, t.skey, t.key, t.key2] : [...GD(t.key)];
      case "ConfigFunction": return [...t.value];
      case "ConfigUnknown": return [...t.value];
      default: throw new Error("not needed by the tests: " + t.type);
    }
  }
};

const Ks = {
  numToRgb: t => ({ r: (t & 16711680) >> 16, g: (t & 65280) >> 8, b: t & 255 }),
  rgbToNum: (t, i, r) => t << 16 | i << 8 | r
};

const Ca = {
  macroEventToByte: (t, i) => {
    const r = [], n = i.value;
    if (t.type === "keyboard" || t.type === "mouse_button") {
      switch (t.type) {
        case "keyboard": r.push(t.value); break;
        case "mouse_button": r.push(ba[t.value][2]); break;
      }
      n <= 127 ? r.push(t.action === "down" ? n + 128 : n) : (r.push(t.action === "down" ? 128 : 0), r.push(n & 255, n >> 8));
    }
    return t.type === "mouse_move" && (r.push(249), n <= 127 ? r.push(n) : r.push(0), r.push(t.dx), r.push(t.dy), n > 127 && r.push(n & 255, n >> 8)), new Uint8Array(r);
  }
};

const Bre = (e, t) => Object.keys(e).find(i => ii(e[i], t));
const xs = { Bit7: 0, Bit8: 1, None: 2, 0: "Bit7", 1: "Bit8", 2: "None" };

// Base HID device class (iA in the original): same framing as its WebHID sendFeature path.
class iA {
  defaultVendorSleepTime = 100;
  deviceKey = "";
  constructor(t) { this.deviceType = t; this.deviceKey = String(t.devAddr); }
  sendFeature(t, i) {
    const a = t.length < 8 ? [...t, ...new Array(8 - t.length).fill(0)] : [...t];
    if (i === 0) { const o = a.slice(0, 7), l = uh(o); a[7] = 255 - (l & 255); }
    if (i === 1) { const o = a.slice(0, 8), l = uh(o); a[8] = 255 - (l & 255); }
    const out = new Array(64).fill(0);
    a.forEach((o, l) => { l < 64 && (out[l] = o); });
    sent.push(Uint8Array.from(out));
    return true;
  }
  async sendMsg(t, i) { return this.sendFeature(t, i); }
  async sendMsgNOSleep(t, i) { return this.sendFeature(t, i); }
  async readMsg() { return new Uint8Array(64); }
  async commonMsg(t, i) { return await this.sendMsg(t, i) ? await this.readMsg() : void 0; }
  async getCommonDeviceId() {
    let t, i = 0, r;
    for (; t === void 0 && i < 3;) r = await this.commonMsg(new Uint8Array([143]), 0), r !== void 0 && r[0] === 143 && (t = new DataView(r.buffer, r.byteOffset, r.byteLength).getUint32(1, !0)), i++;
    return t;
  }
  async vendorSleep() {}
}

export {
  iA as ad, Ce as ae, Ca as aj, Zi as ag, ii as ah, xs as ai, Ks as ak, bi as al, uh as an, Bre as at,
  Xo as aE, ii as aD
};
export const N = { devlistList: [] };
export const am = { YZWBoot: 3 };
export const m = { getPlatform: () => "linux" };
export const aA = [], aB = () => 0, aC = {}, a8 = () => {};
