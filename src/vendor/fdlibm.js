// fdlibm's sin, cos, atan, atan2, acos, exp and pow in JavaScript.
//
// A line-for-line translation of c/fdlibm.c, which is V8's copy of fdlibm
// (src/base/ieee754.cc) converted to C. fishdraw uses these instead of
// Math.sin and friends because JavaScript engines don't agree on those last
// bits: Node's V8 uses fdlibm, Chrome's V8 uses glibc's sin and cos, and
// other engines use their own. A fish is sensitive to those bits, so with
// these the same name draws the same fish in every browser, in Node and in
// the C port. They return exactly what Node's Math functions return.
//
// ====================================================
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//
// Developed at SunSoft, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this
// software is freely granted, provided that this notice
// is preserved.
// ====================================================
//
// V8 is Copyright the V8 project authors, BSD 3-Clause licence (see
// LICENSE-v8).

// --- reading and writing the two 32-bit halves of a double
const F64 = new Float64Array(1);
const U32 = new Uint32Array(F64.buffer);
const LITTLE = new Uint8Array(new Uint16Array([1]).buffer)[0] === 1;
const HI = LITTLE ? 1 : 0;
const LO = LITTLE ? 0 : 1;

const highWord = (d) => { F64[0] = d; return U32[HI] | 0; }; // int32
const lowWord = (d) => { F64[0] = d; return U32[LO]; }; // uint32
const fromWords = (hi, lo) => { U32[HI] = hi >>> 0; U32[LO] = lo >>> 0; return F64[0]; };
const setLowWord = (d, lo) => { F64[0] = d; U32[LO] = lo >>> 0; return F64[0]; };
const setHighWord = (d, hi) => { F64[0] = d; U32[HI] = hi >>> 0; return F64[0]; };

// fdlibm's scalbn (x * 2^n by adjusting the exponent), as in c/fdlibm.c.
const two54 = 1.80143985094819840000e+16;
const twom54 = 5.55111512312578270212e-17;
const hugeS = 1.0e+300;
const tinyS = 1.0e-300;
function scalbn(x, n) {
  let hx = highWord(x);
  const lx = lowWord(x);
  let k = (hx & 0x7ff00000) >> 20;
  if (k === 0) {
    if ((lx | (hx & 0x7fffffff)) === 0) return x;
    x *= two54;
    hx = highWord(x);
    k = ((hx & 0x7ff00000) >> 20) - 54;
    if (n < -50000) return tinyS * x;
  }
  if (k === 0x7ff) return x + x;
  k = k + n;
  if (k > 0x7fe) return hugeS * (x < 0 ? -hugeS : hugeS);
  if (k > 0) return setHighWord(x, (hx & 0x800fffff) | (k << 20));
  if (k <= -54) {
    if (n > 50000) return hugeS * (x < 0 ? -hugeS : hugeS);
    return tinyS * (x < 0 ? -tinyS : tinyS);
  }
  k += 54;
  x = setHighWord(x, (hx & 0x800fffff) | (k << 20));
  return x * twom54;
}

// ---------------------------------------------------------------------------
// Argument reduction for sin and cos: x = n*pi/2 + (y[0] + y[1]).

const two_over_pi = [
  0xA2F983, 0x6E4E44, 0x1529FC, 0x2757D1, 0xF534DD, 0xC0DB62, 0x95993C,
  0x439041, 0xFE5163, 0xABDEBB, 0xC561B7, 0x246E3A, 0x424DD2, 0xE00649,
  0x2EEA09, 0xD1921C, 0xFE1DEB, 0x1CB129, 0xA73EE8, 0x8235F5, 0x2EBB44,
  0x84E99C, 0x7026B4, 0x5F7E41, 0x3991D6, 0x398353, 0x39F49C, 0x845F8B,
  0xBDF928, 0x3B1FF8, 0x97FFDE, 0x05980F, 0xEF2F11, 0x8B5A0A, 0x6D1F6D,
  0x367ECF, 0x27CB09, 0xB74F46, 0x3F669E, 0x5FEA2D, 0x7527BA, 0xC7EBE5,
  0xF17B3D, 0x0739F7, 0x8A5292, 0xEA6BFB, 0x5FB11F, 0x8D5D08, 0x560330,
  0x46FC7B, 0x6BABF0, 0xCFBC20, 0x9AF436, 0x1DA9E3, 0x91615E, 0xE61B08,
  0x659985, 0x5F14A0, 0x68408D, 0xFFD880, 0x4D7327, 0x310606, 0x1556CA,
  0x73A8C9, 0x60E27B, 0xC08C6B,
];

const npio2_hw = [
  0x3FF921FB, 0x400921FB, 0x4012D97C, 0x401921FB, 0x401F6A7A, 0x4022D97C,
  0x4025FDBB, 0x402921FB, 0x402C463A, 0x402F6A7A, 0x4031475C, 0x4032D97C,
  0x40346B9C, 0x4035FDBB, 0x40378FDB, 0x403921FB, 0x403AB41B, 0x403C463A,
  0x403DD85A, 0x403F6A7A, 0x40407E4C, 0x4041475C, 0x4042106C, 0x4042D97C,
  0x4043A28C, 0x40446B9C, 0x404534AC, 0x4045FDBB, 0x4046C6CB, 0x40478FDB,
  0x404858EB, 0x404921FB,
];

const invpio2 = 6.36619772367581382433e-01;
const pio2_1 = 1.57079632673412561417e+00;
const pio2_1t = 6.07710050650619224932e-11;
const pio2_2 = 6.07710050630396597660e-11;
const pio2_2t = 2.02226624879595063154e-21;
const pio2_3 = 2.02226624871116645580e-21;
const pio2_3t = 8.47842766036889956997e-32;
const two24 = 1.67772160000000000000e+07;
const twon24 = 5.96046447753906250000e-08;

function rem_pio2(x, y) {
  let z = 0;
  let w, t, r, fn, i, j, n;
  const hx = highWord(x);
  const ix = hx & 0x7FFFFFFF;
  if (ix <= 0x3FE921FB) {
    y[0] = x;
    y[1] = 0;
    return 0;
  }
  if (ix < 0x4002D97C) {
    if (hx > 0) {
      z = x - pio2_1;
      if (ix !== 0x3FF921FB) {
        y[0] = z - pio2_1t;
        y[1] = (z - y[0]) - pio2_1t;
      } else {
        z -= pio2_2;
        y[0] = z - pio2_2t;
        y[1] = (z - y[0]) - pio2_2t;
      }
      return 1;
    }
    z = x + pio2_1;
    if (ix !== 0x3FF921FB) {
      y[0] = z + pio2_1t;
      y[1] = (z - y[0]) + pio2_1t;
    } else {
      z += pio2_2;
      y[0] = z + pio2_2t;
      y[1] = (z - y[0]) + pio2_2t;
    }
    return -1;
  }
  if (ix <= 0x413921FB) {
    t = Math.abs(x);
    n = (t * invpio2 + 0.5) | 0;
    fn = n;
    r = t - fn * pio2_1;
    w = fn * pio2_1t;
    if (n < 32 && ix !== npio2_hw[n - 1]) {
      y[0] = r - w;
    } else {
      j = ix >> 20;
      y[0] = r - w;
      let high = highWord(y[0]);
      i = j - ((high >> 20) & 0x7FF);
      if (i > 16) {
        t = r;
        w = fn * pio2_2;
        r = t - w;
        w = fn * pio2_2t - ((t - r) - w);
        y[0] = r - w;
        high = highWord(y[0]);
        i = j - ((high >> 20) & 0x7FF);
        if (i > 49) {
          t = r;
          w = fn * pio2_3;
          r = t - w;
          w = fn * pio2_3t - ((t - r) - w);
          y[0] = r - w;
        }
      }
    }
    y[1] = (r - y[0]) - w;
    if (hx < 0) {
      y[0] = -y[0];
      y[1] = -y[1];
      return -n;
    }
    return n;
  }
  if (ix >= 0x7FF00000) {
    y[0] = y[1] = x - x;
    return 0;
  }
  // set z = scalbn(|x|, ilogb(x) - 23)
  z = setLowWord(z, lowWord(x));
  const e0 = (ix >> 20) - 1046;
  z = setHighWord(z, ix - (e0 << 20));
  const tx = [0, 0, 0];
  for (i = 0; i < 2; i++) {
    tx[i] = z | 0;
    z = (z - tx[i]) * two24;
  }
  tx[2] = z;
  let nx = 3;
  while (tx[nx - 1] === 0) nx--;
  n = kernel_rem_pio2(tx, y, e0, nx, 2, two_over_pi);
  if (hx < 0) {
    y[0] = -y[0];
    y[1] = -y[1];
    return -n;
  }
  return n;
}

const init_jk = [2, 3, 4, 6];
const PIo2 = [
  1.57079625129699707031e+00, 7.54978941586159635335e-08,
  5.39030252995776476554e-15, 3.28200341580791294123e-22,
  1.27065575308067607349e-29, 1.22933308981111328932e-36,
  2.73370053816464559624e-44, 2.16741683877804819444e-51,
];

function kernel_rem_pio2(x, y, e0, nx, prec, ipio2) {
  const iq = new Int32Array(20);
  const f = new Float64Array(20);
  const fq = new Float64Array(20);
  const q = new Float64Array(20);
  let i, j, k, n, z, fw, carry, ih;

  const jk = init_jk[prec];
  const jp = jk;
  const jx = nx - 1;
  let jv = ((e0 - 3) / 24) | 0; // C integer division truncates toward zero
  if (jv < 0) jv = 0;
  let q0 = e0 - 24 * (jv + 1);

  j = jv - jx;
  const m = jx + jk;
  for (i = 0; i <= m; i++, j++) f[i] = j < 0 ? 0 : ipio2[j];

  for (i = 0; i <= jk; i++) {
    for (j = 0, fw = 0.0; j <= jx; j++) fw += x[j] * f[jx + i - j];
    q[i] = fw;
  }

  let jz = jk;
  for (;;) {
    // recompute:
    for (i = 0, j = jz, z = q[jz]; j > 0; i++, j--) {
      fw = (twon24 * z) | 0;
      iq[i] = (z - two24 * fw) | 0;
      z = q[j - 1] + fw;
    }

    z = scalbn(z, q0);
    z -= 8.0 * Math.floor(z * 0.125);
    n = z | 0;
    z -= n;
    ih = 0;
    if (q0 > 0) {
      i = iq[jz - 1] >> (24 - q0);
      n += i;
      iq[jz - 1] -= i << (24 - q0);
      ih = iq[jz - 1] >> (23 - q0);
    } else if (q0 === 0) {
      ih = iq[jz - 1] >> 23;
    } else if (z >= 0.5) {
      ih = 2;
    }

    if (ih > 0) {
      n += 1;
      carry = 0;
      for (i = 0; i < jz; i++) {
        j = iq[i];
        if (carry === 0) {
          if (j !== 0) {
            carry = 1;
            iq[i] = 0x1000000 - j;
          }
        } else {
          iq[i] = 0xFFFFFF - j;
        }
      }
      if (q0 > 0) {
        if (q0 === 1) iq[jz - 1] &= 0x7FFFFF;
        else if (q0 === 2) iq[jz - 1] &= 0x3FFFFF;
      }
      if (ih === 2) {
        z = 1.0 - z;
        if (carry !== 0) z -= scalbn(1.0, q0);
      }
    }

    if (z === 0) {
      j = 0;
      for (i = jz - 1; i >= jk; i--) j |= iq[i];
      if (j === 0) {
        for (k = 1; jk >= k && iq[jk - k] === 0; k++) { /* k = terms needed */ }
        for (i = jz + 1; i <= jz + k; i++) {
          f[jx + i] = ipio2[jv + i];
          for (j = 0, fw = 0.0; j <= jx; j++) fw += x[j] * f[jx + i - j];
          q[i] = fw;
        }
        jz += k;
        continue;
      }
    }
    break;
  }

  if (z === 0.0) {
    jz -= 1;
    q0 -= 24;
    while (iq[jz] === 0) {
      jz--;
      q0 -= 24;
    }
  } else {
    z = scalbn(z, -q0);
    if (z >= two24) {
      fw = (twon24 * z) | 0;
      iq[jz] = z - two24 * fw; // Int32Array store truncates like C
      jz += 1;
      q0 += 24;
      iq[jz] = fw;
    } else {
      iq[jz] = z;
    }
  }

  fw = scalbn(1.0, q0);
  for (i = jz; i >= 0; i--) {
    q[i] = fw * iq[i];
    fw *= twon24;
  }

  for (i = jz; i >= 0; i--) {
    for (fw = 0.0, k = 0; k <= jp && k <= jz - i; k++) fw += PIo2[k] * q[i + k];
    fq[jz - i] = fw;
  }

  // prec is always 2 here (53-bit result in y[0] + y[1])
  fw = 0.0;
  for (i = jz; i >= 0; i--) fw += fq[i];
  y[0] = ih === 0 ? fw : -fw;
  fw = fq[0] - fw;
  for (i = 1; i <= jz; i++) fw += fq[i];
  y[1] = ih === 0 ? fw : -fw;
  return n & 7;
}

// ---------------------------------------------------------------------------
// Kernels on [-pi/4, pi/4]

const C1 = 4.16666666666666019037e-02;
const C2 = -1.38888888888741095749e-03;
const C3 = 2.48015872894767294178e-05;
const C4 = -2.75573143513906633035e-07;
const C5 = 2.08757232129817482790e-09;
const C6 = -1.13596475577881948265e-11;

function kernel_cos(x, y) {
  const ix = highWord(x) & 0x7FFFFFFF;
  if (ix < 0x3E400000) {
    if ((x | 0) === 0) return 1.0;
  }
  const z = x * x;
  const r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
  if (ix < 0x3FD33333) return 1.0 - (0.5 * z - (z * r - x * y));
  const qx = ix > 0x3FE90000 ? 0.28125 : fromWords(ix - 0x00200000, 0);
  const iz = 0.5 * z - qx;
  const a = 1.0 - qx;
  return a - (iz - (z * r - x * y));
}

const S1 = -1.66666666666666324348e-01;
const S2 = 8.33333333332248946124e-03;
const S3 = -1.98412698298579493134e-04;
const S4 = 2.75573137070700676789e-06;
const S5 = -2.50507602534068634195e-08;
const S6 = 1.58969099521155010221e-10;

function kernel_sin(x, y, iy) {
  const ix = highWord(x) & 0x7FFFFFFF;
  if (ix < 0x3E400000) {
    if ((x | 0) === 0) return x;
  }
  const z = x * x;
  const v = z * x;
  const r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
  if (iy === 0) return x + v * (S1 + z * r);
  return x - ((z * (0.5 * y - v * r) - y) - v * S1);
}

// ---------------------------------------------------------------------------

const yy = [0, 0];

export function sin(x) {
  const ix = highWord(x) & 0x7FFFFFFF;
  if (ix <= 0x3FE921FB) return kernel_sin(x, 0.0, 0);
  if (ix >= 0x7FF00000) return x - x;
  const n = rem_pio2(x, yy);
  switch (n & 3) {
    case 0: return kernel_sin(yy[0], yy[1], 1);
    case 1: return kernel_cos(yy[0], yy[1]);
    case 2: return -kernel_sin(yy[0], yy[1], 1);
    default: return -kernel_cos(yy[0], yy[1]);
  }
}

export function cos(x) {
  const ix = highWord(x) & 0x7FFFFFFF;
  if (ix <= 0x3FE921FB) return kernel_cos(x, 0.0);
  if (ix >= 0x7FF00000) return x - x;
  const n = rem_pio2(x, yy);
  switch (n & 3) {
    case 0: return kernel_cos(yy[0], yy[1]);
    case 1: return -kernel_sin(yy[0], yy[1], 1);
    case 2: return -kernel_cos(yy[0], yy[1]);
    default: return kernel_sin(yy[0], yy[1], 1);
  }
}

// ---------------------------------------------------------------------------

const pi = 3.14159265358979311600e+00;
const pio2_hi = 1.57079632679489655800e+00;
const pio2_lo = 6.12323399573676603587e-17;
const pS0 = 1.66666666666666657415e-01;
const pS1 = -3.25565818622400915405e-01;
const pS2 = 2.01212532134862925881e-01;
const pS3 = -4.00555345006794114027e-02;
const pS4 = 7.91534994289814532176e-04;
const pS5 = 3.47933107596021167570e-05;
const qS1 = -2.40339491173441421878e+00;
const qS2 = 2.02094576023350569471e+00;
const qS3 = -6.88283971605453293030e-01;
const qS4 = 7.70381505559019352791e-02;

export function acos(x) {
  let z, p, q, r, w, s, c, df;
  const hx = highWord(x);
  const ix = hx & 0x7FFFFFFF;
  if (ix >= 0x3FF00000) {
    const lx = lowWord(x);
    if (((ix - 0x3FF00000) | lx) === 0) {
      return hx > 0 ? 0.0 : pi + 2.0 * pio2_lo;
    }
    return NaN;
  }
  if (ix < 0x3FE00000) {
    if (ix <= 0x3C600000) return pio2_hi + pio2_lo;
    z = x * x;
    p = z * (pS0 + z * (pS1 + z * (pS2 + z * (pS3 + z * (pS4 + z * pS5)))));
    q = 1.0 + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4)));
    r = p / q;
    return pio2_hi - (x - (pio2_lo - x * r));
  } else if (hx < 0) {
    z = (1.0 + x) * 0.5;
    p = z * (pS0 + z * (pS1 + z * (pS2 + z * (pS3 + z * (pS4 + z * pS5)))));
    q = 1.0 + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4)));
    s = Math.sqrt(z);
    r = p / q;
    w = r * s - pio2_lo;
    return pi - 2.0 * (s + w);
  }
  z = (1.0 - x) * 0.5;
  s = Math.sqrt(z);
  df = setLowWord(s, 0);
  c = (z - df * df) / (s + df);
  p = z * (pS0 + z * (pS1 + z * (pS2 + z * (pS3 + z * (pS4 + z * pS5)))));
  q = 1.0 + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4)));
  r = p / q;
  w = r * s + c;
  return 2.0 * (df + w);
}

// ---------------------------------------------------------------------------

const atanhi = [
  4.63647609000806093515e-01, 7.85398163397448278999e-01,
  9.82793723247329054082e-01, 1.57079632679489655800e+00,
];
const atanlo = [
  2.26987774529616870924e-17, 3.06161699786838301793e-17,
  1.39033110312309984516e-17, 6.12323399573676603587e-17,
];
const aT = [
  3.33333333333329318027e-01, -1.99999999998764832476e-01,
  1.42857142725034663711e-01, -1.11111104054623557880e-01,
  9.09088713343650656196e-02, -7.69187620504482999495e-02,
  6.66107313738753120669e-02, -5.83357013379057348645e-02,
  4.97687799461593236017e-02, -3.65315727442169155270e-02,
  1.62858201153657823623e-02,
];

export function atan(x) {
  let id;
  const hx = highWord(x);
  const ix = hx & 0x7FFFFFFF;
  if (ix >= 0x44100000) {
    const low = lowWord(x);
    if (ix > 0x7FF00000 || (ix === 0x7FF00000 && low !== 0)) return x + x;
    return hx > 0 ? atanhi[3] + atanlo[3] : -atanhi[3] - atanlo[3];
  }
  if (ix < 0x3FDC0000) {
    if (ix < 0x3E400000) {
      if (1.0e300 + x > 1.0) return x;
    }
    id = -1;
  } else {
    x = Math.abs(x);
    if (ix < 0x3FF30000) {
      if (ix < 0x3FE60000) {
        id = 0;
        x = (2.0 * x - 1.0) / (2.0 + x);
      } else {
        id = 1;
        x = (x - 1.0) / (x + 1.0);
      }
    } else if (ix < 0x40038000) {
      id = 2;
      x = (x - 1.5) / (1.0 + 1.5 * x);
    } else {
      id = 3;
      x = -1.0 / x;
    }
  }
  let z = x * x;
  const w = z * z;
  const s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
  const s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
  if (id < 0) return x - x * (s1 + s2);
  z = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
  return hx < 0 ? -z : z;
}

const tiny = 1.0e-300;
const pi_o_4 = 7.8539816339744827900E-01;
const pi_o_2 = 1.5707963267948965580E+00;
const pi_lo = 1.2246467991473531772E-16;

export function atan2(y, x) {
  let z;
  const hx = highWord(x);
  const lx = lowWord(x);
  const ix = hx & 0x7FFFFFFF;
  const hy = highWord(y);
  const ly = lowWord(y);
  const iy = hy & 0x7FFFFFFF;
  if ((ix | ((lx | (-lx | 0)) >>> 31)) > 0x7FF00000 || (iy | ((ly | (-ly | 0)) >>> 31)) > 0x7FF00000) {
    return x + y;
  }
  if ((((hx - 0x3FF00000) | 0) | lx) === 0) return atan(y);
  let m = ((hy >> 31) & 1) | ((hx >> 30) & 2);

  if ((iy | ly) === 0) {
    switch (m) {
      case 0:
      case 1: return y;
      case 2: return pi + tiny;
      default: return -pi - tiny;
    }
  }
  if ((ix | lx) === 0) return hy < 0 ? -pi_o_2 - tiny : pi_o_2 + tiny;

  if (ix === 0x7FF00000) {
    if (iy === 0x7FF00000) {
      switch (m) {
        case 0: return pi_o_4 + tiny;
        case 1: return -pi_o_4 - tiny;
        case 2: return 3.0 * pi_o_4 + tiny;
        default: return -3.0 * pi_o_4 - tiny;
      }
    }
    switch (m) {
      case 0: return 0.0;
      case 1: return -0.0;
      case 2: return pi + tiny;
      default: return -pi - tiny;
    }
  }
  if (iy === 0x7FF00000) return hy < 0 ? -pi_o_2 - tiny : pi_o_2 + tiny;

  const k = (iy - ix) >> 20;
  if (k > 60) {
    z = pi_o_2 + 0.5 * pi_lo;
    m &= 1;
  } else if (hx < 0 && k < -60) {
    z = 0.0;
  } else {
    z = atan(Math.abs(y / x));
  }
  switch (m) {
    case 0: return z;
    case 1: return -z;
    case 2: return pi - (z - pi_lo);
    default: return (z - pi_lo) - pi;
  }
}

// ---------------------------------------------------------------------------

const halF = [0.5, -0.5];
const o_threshold = 7.09782712893383973096e+02;
const u_threshold = -7.45133219101941108420e+02;
const ln2HI = [6.93147180369123816490e-01, -6.93147180369123816490e-01];
const ln2LO = [1.90821492927058770002e-10, -1.90821492927058770002e-10];
const invln2 = 1.44269504088896338700e+00;
const P1 = 1.66666666666666019037e-01;
const P2 = -2.77777777770155933842e-03;
const P3 = 6.61375632143793436117e-05;
const P4 = -1.65339022054652515390e-06;
const P5 = 4.13813679705723846039e-08;
const E = 2.718281828459045;
const huge = 1.0e+300;
const twom1000 = 9.33263618503218878990e-302;
const two1023 = 8.988465674311579539e307;

export function exp(x) {
  let y, c, t, twopk;
  let hi = 0.0;
  let lo = 0.0;
  let k = 0;
  let hx = highWord(x) >>> 0;
  const xsb = (hx >>> 31) & 1;
  hx &= 0x7FFFFFFF;

  if (hx >= 0x40862E42) {
    if (hx >= 0x7FF00000) {
      const lx = lowWord(x);
      if (((hx & 0xFFFFF) | lx) !== 0) return x + x;
      return xsb === 0 ? x : 0.0;
    }
    if (x > o_threshold) return huge * huge;
    if (x < u_threshold) return twom1000 * twom1000;
  }

  if (hx > 0x3FD62E42) {
    if (hx < 0x3FF0A2B2) {
      if (x === 1.0) return E;
      hi = x - ln2HI[xsb];
      lo = ln2LO[xsb];
      k = 1 - xsb - xsb;
    } else {
      k = (invln2 * x + halF[xsb]) | 0;
      t = k;
      hi = x - t * ln2HI[0];
      lo = t * ln2LO[0];
    }
    x = hi - lo;
  } else if (hx < 0x3E300000) {
    if (huge + x > 1.0) return 1.0 + x;
  } else {
    k = 0;
  }

  t = x * x;
  if (k >= -1021) twopk = fromWords(0x3FF00000 + (k << 20), 0);
  else twopk = fromWords(0x3FF00000 + ((k + 1000) << 20), 0);
  c = x - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
  if (k === 0) return 1.0 - ((x * c) / (c - 2.0) - x);
  y = 1.0 - ((lo - (x * c) / (2.0 - c)) - hi);
  if (k >= -1021) {
    if (k === 1024) return y * 2.0 * two1023;
    return y * twopk;
  }
  return y * twopk * twom1000;
}

// ---------------------------------------------------------------------------

const bp = [1.0, 1.5];
const dp_h = [0.0, 5.84962487220764160156e-01];
const dp_l = [0.0, 1.35003920212974897128e-08];
const two53 = 9007199254740992.0;
const tinyP = 1.0e-300;
const L1 = 5.99999999999994648725e-01;
const L2 = 4.28571428578550184252e-01;
const L3 = 3.33333329818377432918e-01;
const L4 = 2.72728123808534006489e-01;
const L5 = 2.30660745775561754067e-01;
const L6 = 2.06975017800338417784e-01;
const lg2 = 6.93147180559945286227e-01;
const lg2_h = 6.93147182464599609375e-01;
const lg2_l = -1.90465429995776804525e-09;
const ovt = 8.0085662595372944372e-0017;
const cp = 9.61796693925975554329e-01;
const cp_h = 9.61796700954437255859e-01;
const cp_l = -7.02846165095275826516e-09;
const ivln2 = 1.44269504088896338700e+00;
const ivln2_h = 1.44269502162933349609e+00;
const ivln2_l = 1.92596299112661746887e-08;

export function pow(x, y) {
  let z, ax, z_h, z_l, p_h, p_l, y1, t1, t2, r, s, t, u, v, w;
  let i, j, k, n;

  const hx = highWord(x);
  const lx = lowWord(x);
  const hy = highWord(y);
  const ly = lowWord(y);
  let ix = hx & 0x7fffffff;
  const iy = hy & 0x7fffffff;

  if ((iy | ly) === 0) return 1.0;

  if (ix > 0x7ff00000 || (ix === 0x7ff00000 && lx !== 0) || iy > 0x7ff00000 || (iy === 0x7ff00000 && ly !== 0)) {
    return x + y;
  }

  let yisint = 0;
  if (hx < 0) {
    if (iy >= 0x43400000) {
      yisint = 2;
    } else if (iy >= 0x3ff00000) {
      k = (iy >> 20) - 0x3ff;
      if (k > 20) {
        j = ly >>> (52 - k);
        // C: j is int, so (j << (52 - k)) is compared with (int)ly
        if ((j << (52 - k)) === (ly | 0)) yisint = 2 - (j & 1);
      } else if (ly === 0) {
        j = iy >> (20 - k);
        if ((j << (20 - k)) === iy) yisint = 2 - (j & 1);
      }
    }
  }

  if (ly === 0) {
    if (iy === 0x7ff00000) {
      if (((ix - 0x3ff00000) | lx) === 0) return y - y;
      if (ix >= 0x3ff00000) return hy >= 0 ? y : 0.0;
      return hy < 0 ? -y : 0.0;
    }
    if (iy === 0x3ff00000) return hy < 0 ? 1.0 / x : x;
    if (hy === 0x40000000) return x * x;
    if (hy === 0x3fe00000) {
      if (hx >= 0) return Math.sqrt(x);
    }
  }

  ax = Math.abs(x);
  if (lx === 0) {
    if (ix === 0x7ff00000 || ix === 0 || ix === 0x3ff00000) {
      z = ax;
      if (hy < 0) z = 1.0 / z;
      if (hx < 0) {
        if (((ix - 0x3ff00000) | yisint) === 0) z = NaN;
        else if (yisint === 1) z = -z;
      }
      return z;
    }
  }

  n = (hx >> 31) + 1;
  if ((n | yisint) === 0) return NaN;

  s = 1.0;
  if ((n | (yisint - 1)) === 0) s = -1.0;

  if (iy > 0x41e00000) {
    if (iy > 0x43f00000) {
      if (ix <= 0x3fefffff) return hy < 0 ? huge * huge : tinyP * tinyP;
      if (ix >= 0x3ff00000) return hy > 0 ? huge * huge : tinyP * tinyP;
    }
    if (ix < 0x3fefffff) return hy < 0 ? s * huge * huge : s * tinyP * tinyP;
    if (ix > 0x3ff00000) return hy > 0 ? s * huge * huge : s * tinyP * tinyP;
    t = ax - 1.0;
    w = (t * t) * (0.5 - t * (0.3333333333333333333333 - t * 0.25));
    u = ivln2_h * t;
    v = t * ivln2_l - w * ivln2;
    t1 = setLowWord(u + v, 0);
    t2 = v - (t1 - u);
  } else {
    let ss, s2, s_h, s_l, t_h, t_l;
    n = 0;
    if (ix < 0x00100000) {
      ax *= two53;
      n -= 53;
      ix = highWord(ax);
    }
    n += (ix >> 20) - 0x3ff;
    j = ix & 0x000fffff;
    ix = j | 0x3ff00000;
    if (j <= 0x3988E) {
      k = 0;
    } else if (j < 0xBB67A) {
      k = 1;
    } else {
      k = 0;
      n += 1;
      ix -= 0x00100000;
    }
    ax = setHighWord(ax, ix);

    u = ax - bp[k];
    v = 1.0 / (ax + bp[k]);
    ss = u * v;
    s_h = setLowWord(ss, 0);
    t_h = setHighWord(0.0, ((ix >> 1) | 0x20000000) + 0x00080000 + (k << 18));
    t_l = ax - (t_h - bp[k]);
    s_l = v * ((u - s_h * t_h) - s_h * t_l);
    s2 = ss * ss;
    r = s2 * s2 * (L1 + s2 * (L2 + s2 * (L3 + s2 * (L4 + s2 * (L5 + s2 * L6)))));
    r += s_l * (s_h + ss);
    s2 = s_h * s_h;
    t_h = setLowWord(3.0 + s2 + r, 0);
    t_l = r - ((t_h - 3.0) - s2);
    u = s_h * t_h;
    v = s_l * t_h + t_l * ss;
    p_h = setLowWord(u + v, 0);
    p_l = v - (p_h - u);
    z_h = cp_h * p_h;
    z_l = cp_l * p_h + p_l * cp + dp_l[k];
    t = n;
    t1 = setLowWord((((z_h + z_l) + dp_h[k]) + t), 0);
    t2 = z_l - (((t1 - t) - dp_h[k]) - z_h);
  }

  y1 = setLowWord(y, 0);
  p_l = (y - y1) * t1 + y * t2;
  p_h = y1 * t1;
  z = p_l + p_h;
  j = highWord(z);
  i = lowWord(z) | 0; // C: int i
  if (j >= 0x40900000) {
    if (((j - 0x40900000) | i) !== 0) return s * huge * huge;
    if (p_l + ovt > z - p_h) return s * huge * huge;
  } else if ((j & 0x7fffffff) >= 0x4090cc00) {
    if ((((j - 0xc090cc00) | 0) | i) !== 0) return s * tinyP * tinyP;
    if (p_l <= z - p_h) return s * tinyP * tinyP;
  }

  i = j & 0x7fffffff;
  k = (i >> 20) - 0x3ff;
  n = 0;
  if (i > 0x3fe00000) {
    n = (j + (0x00100000 >> (k + 1))) | 0;
    k = ((n & 0x7fffffff) >> 20) - 0x3ff;
    t = setHighWord(0.0, n & ~(0x000fffff >> k));
    n = ((n & 0x000fffff) | 0x00100000) >> (20 - k);
    if (j < 0) n = -n;
    p_h -= t;
  }
  t = setLowWord(p_l + p_h, 0);
  u = t * lg2_h;
  v = (p_l - (t - p_h)) * lg2 + t * lg2_l;
  z = u + v;
  w = v - (z - u);
  t = z * z;
  t1 = z - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
  r = (z * t1) / ((t1 - 2.0) - (w + z * w));
  z = 1.0 - (r - z);
  j = highWord(z);
  j = (j + (n << 20)) | 0;
  if ((j >> 20) <= 0) {
    z = scalbn(z, n);
  } else {
    z = setHighWord(z, (highWord(z) + (n << 20)) | 0);
  }
  return s * z;
}

// Math.hypot as V8 computes it (src/builtins/math.tq): scale by the largest
// magnitude and sum the squares with Kahan compensation. fishdraw only ever
// calls it with two arguments.
export function hypot(a, b) {
  if (Number.isNaN(a) || Number.isNaN(b)) {
    return Math.abs(a) === Infinity || Math.abs(b) === Infinity ? Infinity : NaN;
  }
  const absA = Math.abs(a);
  const absB = Math.abs(b);
  let max = absA > 0 ? absA : 0;
  if (absB > max) max = absB;
  if (max === Infinity) return Infinity;
  if (max === 0) return 0;
  let sum = 0;
  let compensation = 0;
  for (const v of [absA, absB]) {
    const n = v / max;
    const summand = n * n - compensation;
    const preliminary = sum + summand;
    compensation = (preliminary - sum) - summand;
    sum = preliminary;
  }
  return Math.sqrt(sum) * max;
}
