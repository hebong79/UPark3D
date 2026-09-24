// ===== 주차 기동 계획기 (시안) =====
// 좌표: 미터, 화면 좌표계(y 아래 +). 자세 = 뒷바퀴 축 중심 (x, y, th). 진행 방향 = (cos th, sin th).
// 기구학 자전거 모델: dth/ds = k * g (g = +1 전진, -1 후진), 조향각 = atan(k * WB). 제자리 선회 없음.
const CAR = { L: 4.7, W: 1.85, WB: 2.8, RO: 0.95 };          // 전장, 전폭, 축거, 뒷오버행
const RMIN = 4.2;                                           // 뒷축 중심 최소 회전반경(조향 33.7°)
const D2R = Math.PI / 180;
const CLEAR_OK = 0.25;                                      // 이보다 가까우면 다른 기동을 찾는다(m)
const V = { cruise: 3.0, man: 1.1, rev: 0.7, slow: 0.5 };   // m/s

function step(p, seg, len) {
  const { g, k } = seg;
  if (!k) return { x: p.x + g * len * Math.cos(p.th), y: p.y + g * len * Math.sin(p.th), th: p.th };
  const th2 = p.th + k * g * len;
  return { x: p.x + (Math.sin(th2) - Math.sin(p.th)) / k, y: p.y + (Math.cos(p.th) - Math.cos(th2)) / k, th: th2 };
}
function run(p, segs) { for (const s of segs) p = step(p, s, s.len); return p; }
function inverse(end, segs) {
  let p = end;
  for (let i = segs.length - 1; i >= 0; i--) p = step(p, { g: -segs[i].g, k: segs[i].k }, segs[i].len);
  return p;
}
function footprint(p) {
  const c = Math.cos(p.th), s = Math.sin(p.th), hw = CAR.W / 2;
  return [[-CAR.RO, -hw], [CAR.L - CAR.RO, -hw], [CAR.L - CAR.RO, hw], [-CAR.RO, hw]]
    .map(([u, v]) => [p.x + u * c - v * s, p.y + u * s + v * c]);
}
function rectPoly(cx, cy, th, len, wid) {
  const c = Math.cos(th), s = Math.sin(th), a = len / 2, b = wid / 2;
  return [[-a, -b], [a, -b], [a, b], [-a, b]].map(([u, v]) => [cx + u * c - v * s, cy + u * s + v * c]);
}
const OFF = CAR.L / 2 - CAR.RO;
function raFromCenter(cx, cy, th) { return { x: cx - OFF * Math.cos(th), y: cy - OFF * Math.sin(th), th }; }
function centerFromRa(p) { return [p.x + OFF * Math.cos(p.th), p.y + OFF * Math.sin(p.th)]; }

function segDist(p, a, b) {
  const dx = b[0] - a[0], dy = b[1] - a[1], L2 = dx * dx + dy * dy;
  let t = L2 ? ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / L2 : 0; t = Math.max(0, Math.min(1, t));
  return Math.hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dy);
}
function overlap(A, B) {
  for (const P of [A, B]) for (let i = 0; i < P.length; i++) {
    const a = P[i], b = P[(i + 1) % P.length], nx = b[1] - a[1], ny = a[0] - b[0];
    let amin = 1e9, amax = -1e9, bmin = 1e9, bmax = -1e9;
    for (const q of A) { const d = q[0] * nx + q[1] * ny; amin = Math.min(amin, d); amax = Math.max(amax, d); }
    for (const q of B) { const d = q[0] * nx + q[1] * ny; bmin = Math.min(bmin, d); bmax = Math.max(bmax, d); }
    if (amax < bmin || bmax < amin) return false;
  }
  return true;
}
function polyDist(A, B) {
  if (overlap(A, B)) return 0;
  let m = 1e9;
  for (const [P, Q] of [[A, B], [B, A]]) for (const p of P) for (let i = 0; i < Q.length; i++) m = Math.min(m, segDist(p, Q[i], Q[(i + 1) % Q.length]));
  return m;
}
function sample(start, segs, ds = 0.05) {
  const out = []; let p = start;
  segs.forEach((seg, si) => {
    const n = Math.max(1, Math.ceil(seg.len / ds));
    for (let i = 0; i < n; i++) { out.push({ ...p, g: seg.g, k: seg.k, si, ds: seg.len / n }); p = step(p, seg, seg.len / n); }
  });
  const last = segs[segs.length - 1];
  out.push({ ...p, g: last.g, k: last.k, si: segs.length - 1, ds: 0, end: true });
  return out;
}
// 접근·이탈 직진 구간(통로)은 멀리 있는 차만 스치므로 거리 계산을 건너뛰어도 되지만, 정확도를 위해 모두 본다.
function clearance(samples, obstacles) {
  let m = 1e9, at = null;
  for (let i = 0; i < samples.length; i += 2) {
    const F = footprint(samples[i]);
    for (const o of obstacles) { const d = polyDist(F, o.poly); if (d < m) { m = d; at = i; } }
  }
  return { c: m, at };
}
function gearChanges(segs) { let n = 0; for (let i = 1; i < segs.length; i++) if (segs[i].g !== segs[i - 1].g && segs[i].len > 0.01) n++; return n; }
function pathLen(segs) { return segs.reduce((a, s) => a + s.len, 0); }

// 후보 중 고르기: 간격 CLEAR_OK 이상 → 기어 전환 적은 것 → 짧은 것. 전부 모자라면 간격 최대.
function pick(cands) {
  const ok = cands.filter(c => c.c >= CLEAR_OK);
  if (ok.length) {
    ok.sort((a, b) => a.gears - b.gears || (a.psiD || 0) - (b.psiD || 0) || a.man - b.man);
    return ok[0];
  }
  cands.sort((a, b) => b.c - a.c);
  return cands[0] ? { ...cands[0], fail: true } : null;
}

// ===== 배치 =====
function makeLayout(type) {
  const L = { type, slots: [], walls: [] };
  if (type === 'parallel') {                        // 도로변(평행): 면 6.15 × 2.40 (객리단길 1~5번)
    const SL = 6.15, SW = 2.4, N = 7;
    L.target = 3; L.x0 = -2; L.x1 = N * SL + 2;
    for (let i = 0; i < N; i++) L.slots.push({ i, cx: i * SL + SL / 2, cy: -SW / 2, th: 0, len: SL, wid: SW });
    L.walls.push({ poly: [[-40, 0.15], [N * SL + 40, 0.15], [N * SL + 40, 3], [-40, 3]], kind: 'curb' });
    L.laneMin = L.laneMax = -SW - 0.9 - CAR.W / 2;
    L.road = { y0: -SW - 6.6, y1: 0, center: -SW - 3.3 };
    L.slotW = SW; L.slotL = SL;
  } else if (type === 'perp') {                     // 정형(직각): 면 2.64 × 5.14 (서신지구대), 통로 6.0
    const SW = 2.64, SL = 5.14, N = 9, AIS = 6.0;
    L.target = 4; L.aisle = AIS; L.slotW = SW; L.slotL = SL;
    for (let i = 0; i < N; i++) L.slots.push({ i, cx: i * SW + SW / 2, cy: SL / 2, th: Math.PI / 2, len: SL, wid: SW });
    for (let i = 0; i < N; i++) L.slots.push({ i: 100 + i, cx: i * SW + SW / 2, cy: -AIS - SL / 2, th: -Math.PI / 2, len: SL, wid: SW });
    L.walls.push({ poly: [[-40, SL + 0.3], [N * SW + 40, SL + 0.3], [N * SW + 40, SL + 2], [-40, SL + 2]], kind: 'wall' });
    L.walls.push({ poly: [[-40, -AIS - SL - 0.3], [N * SW + 40, -AIS - SL - 0.3], [N * SW + 40, -AIS - SL - 2], [-40, -AIS - SL - 2]], kind: 'wall' });
    L.x0 = -2; L.x1 = N * SW + 2;
    L.laneMin = -AIS + 0.35 + CAR.W / 2; L.laneMax = -0.35 - CAR.W / 2;
  } else {                                           // 사선 60°: 면 2.5 × 5.0, 일방 통로 5.0
    const PSI = 60 * D2R, SW = 2.5, SL = 5.0, AIS = (globalThis.ANG_AISLE || 5.0), PITCH = SW / Math.sin(PSI), N = 9;
    L.target = 4; L.aisle = AIS; L.psi = PSI; L.pitch = PITCH; L.slotW = SW; L.slotL = SL;
    const depth = SL * Math.sin(PSI) + SW * Math.cos(PSI);
    for (let i = 0; i < N; i++) L.slots.push({ i, cx: i * PITCH + PITCH / 2 + 1.5, cy: depth / 2, th: PSI, len: SL, wid: SW });
    L.depth = depth;
    L.walls.push({ poly: [[-40, -AIS - 0.2], [N * PITCH + 40, -AIS - 0.2], [N * PITCH + 40, -AIS - 2], [-40, -AIS - 2]], kind: 'wall' });
    L.walls.push({ poly: [[-40, depth + 0.3], [N * PITCH + 40, depth + 0.3], [N * PITCH + 40, depth + 2], [-40, depth + 2]], kind: 'wall' });
    L.x0 = -2; L.x1 = N * PITCH + 4;
    L.laneMin = -AIS + 0.35 + CAR.W / 2; L.laneMax = -0.35 - CAR.W / 2;
  }
  return L;
}
function parkedCars(L) {
  const cars = [];
  L.slots.forEach((s, n) => {
    if (s.i === L.target) return;
    let th = s.th;
    if (L.type === 'perp' && n % 3 === 1) th += Math.PI;          // 정형은 전면·후면 자세가 섞여 있다
    if (L.type === 'angled' && n % 4 === 2) th += Math.PI;
    cars.push({ cx: s.cx, cy: s.cy, th, poly: rectPoly(s.cx, s.cy, th, CAR.L, CAR.W) });
  });
  return cars;
}

// ===== 입차 기동 후보 =====
const APPROACH = '입구에서 통로를 따라 전진';
function withApproach(L, man, fin) {
  const p0 = inverse(fin, man);
  if (Math.abs(p0.th) > 1e-6 || p0.y < L.laneMin - 1e-6 || p0.y > L.laneMax + 1e-6) return null;
  const x0 = L.x0 - 5;
  if (p0.x < x0 + 3) return null;
  const approach = { g: +1, k: 0, len: p0.x - x0, vmax: V.cruise, label: L.type === 'parallel' ? '입구에서 차로를 따라 전진 — 빈 면 앞차 옆에 정지' : APPROACH, approach: true };
  return { start: { x: x0, y: p0.y, th: 0 }, segs: [approach, ...man] };
}
function candidatesEnter(L, mode) {
  const t = L.slots.find(s => s.i === L.target);
  const R = RMIN, out = [];
  if (L.type === 'parallel') {
    const fin = raFromCenter(t.cx, t.cy, 0);
    const dy = fin.y - L.laneMin;                    // 차로 → 면 가로 이동량(+)
    for (let phiD = 25; phiD <= 60; phiD += 1) for (const adj of [0, 0.3, 0.6]) {
      const phi = phiD * D2R, arc = 2 * R * (1 - Math.cos(phi));
      if (arc > dy) continue;
      const d = (dy - arc) / Math.sin(phi);
      const man = [
        { g: -1, k: +1 / R, len: R * phi, vmax: V.rev, label: '후진 — 핸들 오른쪽 끝까지, 뒤를 연석 쪽으로' },
        { g: -1, k: 0, len: d, vmax: V.rev, label: '핸들 풀고 사선으로 후진' },
        { g: -1, k: -1 / R, len: R * phi, vmax: V.rev, label: '후진 — 핸들 왼쪽 끝까지, 차체를 연석과 나란히' },
      ];
      if (adj) man.push({ g: +1, k: 0, len: adj, vmax: V.slow, label: '전진 — 면 가운데로 살짝 당김' });
      const w = withApproach(L, man.filter(s => s.len > 0.005), fin);
      if (w) out.push({ ...w, fin, desc: `φ ${phiD}°` });
    }
    return out;
  }
  if (mode === 'front') {
    const PSI = t.th, fin = raFromCenter(t.cx, t.cy, PSI);
    for (let s1 = 0; s1 <= 4.6; s1 += 0.1) {
      out.push({ man: [
        { g: +1, k: +1 / R, len: R * PSI, vmax: V.man, label: `핸들 오른쪽 끝까지 — ${Math.round(PSI / D2R)}° 돌며 면으로` },
        { g: +1, k: 0, len: s1, vmax: V.slow, label: '핸들 풀고 곧게 전진 — 면 가운데 정지' }], fin, desc: '한 번에' });
      for (let a2D = 8; a2D <= Math.min(45, PSI / D2R - 10); a2D += 2) {
        const a2 = a2D * D2R, a1 = PSI - a2;
        out.push({ man: [
          { g: +1, k: +1 / R, len: R * a1, vmax: V.man, label: `핸들 오른쪽 — ${Math.round(a1 / D2R)}° 까지 돌며 면 입구로` },
          { g: -1, k: -1 / R, len: R * a2, vmax: V.rev, label: `옆 차가 가까움 — 핸들 왼쪽 후진으로 ${a2D}° 보정` },
          { g: +1, k: 0, len: s1, vmax: V.slow, label: '핸들 풀고 곧게 전진 — 면 가운데 정지' }], fin, desc: `보정 ${a2D}°` });
      }
      // 면을 조금 지나쳐 핸들 왼쪽으로 비스듬히 후진(코가 면 쪽으로 돌아간다) → 핸들 오른쪽 전진으로 넣기.
      for (let a1D = 10; a1D <= PSI / D2R - 10; a1D += 2) {
        const a1 = a1D * D2R;
        out.push({ man: [
          { g: -1, k: -1 / R, len: R * a1, vmax: V.rev, label: `면을 지나쳐 정지 → 핸들 왼쪽 후진, 코를 면 쪽으로 ${a1D}°` },
          { g: +1, k: +1 / R, len: R * (PSI - a1), vmax: V.man, label: '핸들 오른쪽 전진 — 면 입구로 꺾어 들어감' },
          { g: +1, k: 0, len: s1, vmax: V.slow, label: '핸들 풀고 곧게 전진 — 면 가운데 정지' }], fin, desc: `후진 셋업 ${a1D}°` });
      }
    }
  } else {
    const PHI = t.th - Math.PI, TOT = -PHI, fin = raFromCenter(t.cx, t.cy, PHI);
    for (let alD = 0; alD <= Math.min(70, TOT / D2R - 10); alD += 2) for (let s1 = 0.3; s1 <= 4.0; s1 += 0.1) {
      const al = alD * D2R;
      const man = [];
      if (alD) man.push({ g: +1, k: -1 / R, len: R * al, vmax: V.man, label: `면을 지나치며 핸들 왼쪽 — ${alD}° 틀어 후진 준비` });
      man.push({ g: -1, k: +1 / R, len: R * (TOT - al), vmax: V.rev, label: '후진 — 핸들 오른쪽 끝까지, 뒤를 면으로 돌려 넣음' });
      man.push({ g: -1, k: 0, len: s1, vmax: V.slow, label: '핸들 풀고 곧게 후진 — 면 가운데 정지' });
      out.push({ man, fin, desc: `셋업 ${alD}°` });
      // 셋업 호 뒤에 비스듬히 조금 더 전진해 후진 공간을 번다.
      if (alD >= 20) for (const d of [0.5, 1.0, 1.5, 2.0]) {
        const m2 = [man[0], { g: +1, k: 0, len: d, vmax: V.man, label: '비스듬히 조금 더 전진 — 후진 공간 확보' }, ...man.slice(1)];
        out.push({ man: m2, fin, desc: `셋업 ${alD}° + ${d} m` });
      }
    }
  }
  return out.map(c => { const w = withApproach(L, c.man, c.fin); return w && { ...w, fin: c.fin, desc: c.desc }; }).filter(Boolean);
}

// ===== 출차 기동 후보 =====
function withLeave(L, fin, man) {
  const p = run(fin, man);
  if (Math.abs(p.th) > 1e-6 || p.y < L.laneMin - 1e-6 || p.y > L.laneMax + 1e-6) return null;
  const leave = { g: +1, k: 0, len: Math.max(2, L.x1 + 6 - p.x), vmax: V.cruise, label: '통로를 따라 출구로', approach: true };
  return { start: fin, segs: [...man, leave] };
}
function candidatesExit(L, fin) {
  const R = RMIN, out = [];
  if (L.type === 'parallel') {
    const dy = fin.y - L.laneMin;
    for (const b of [0, 0.2, 0.4, 0.6]) for (let psiD = 15; psiD <= 45; psiD += 1) {
      const psi = psiD * D2R, arc = 2 * R * (1 - Math.cos(psi));
      if (arc > dy) continue;
      const d = (dy - arc) / Math.sin(psi);
      const man = [];
      if (b) man.push({ g: -1, k: 0, len: b, vmax: 0.4, label: '앞차 여유가 모자람 — 뒤로 살짝' });
      man.push({ g: +1, k: -1 / R, len: R * psi, vmax: V.man, label: `핸들 왼쪽 — ${psiD}° 살짝 틀어 앞차를 비껴 나감` });
      man.push({ g: +1, k: 0, len: d, vmax: V.man, label: '비스듬히 차로로 나감' });
      man.push({ g: +1, k: +1 / R, len: R * psi, vmax: V.man, label: '핸들 풀어 차로와 나란히' });
      const w = withLeave(L, fin, man);
      if (w) out.push({ ...w, psiD, desc: b ? `후진 ${b} m + ${psiD}°` : `${psiD}°` });
    }
    return out;
  }
  if (fin.th > 0) {
    // 전진주차 상태 → 후진으로 빼며 돌린다(필요하면 전진으로 한 번 더 꺾는다).
    const PSI = fin.th;
    for (let s = 0; s <= 5; s += 0.1) {
      out.push({ man: [
        { g: -1, k: 0, len: s, vmax: V.slow, label: '곧게 후진 — 면에서 빠져나옴' },
        { g: -1, k: +1 / R, len: R * PSI, vmax: V.rev, label: '후진하며 핸들 오른쪽 — 코를 출구 쪽으로' }], desc: '후진 1회' });
      for (let a2D = 10; a2D <= PSI / D2R - 10; a2D += 2) {
        const a2 = a2D * D2R;
        out.push({ man: [
          { g: -1, k: 0, len: s, vmax: V.slow, label: '곧게 후진 — 면에서 빠져나옴' },
          { g: -1, k: +1 / R, len: R * (PSI - a2), vmax: V.rev, label: '후진하며 핸들 오른쪽' },
          { g: +1, k: -1 / R, len: R * a2, vmax: V.man, label: `전진하며 핸들 왼쪽 — ${a2D}° 더 틀어 통로와 나란히` }], desc: `후진+전진 ${a2D}°` });
      }
    }
  } else {
    const TOT = -fin.th;
    for (let s = 0; s <= 5; s += 0.1) {
      out.push({ man: [
        { g: +1, k: 0, len: s, vmax: V.slow, label: '곧게 전진 — 면에서 나옴' },
        { g: +1, k: +1 / R, len: R * TOT, vmax: V.man, label: '핸들 오른쪽 — 출구 방향으로' }], desc: '전진 1회' });
      for (let a2D = 10; a2D <= TOT / D2R - 20; a2D += 4) {
        const a2 = a2D * D2R;
        out.push({ man: [
          { g: +1, k: 0, len: s, vmax: V.slow, label: '곧게 전진 — 면에서 나옴' },
          { g: +1, k: +1 / R, len: R * (TOT - a2), vmax: V.man, label: '핸들 오른쪽 — 출구 방향으로' },
          { g: -1, k: -1 / R, len: R * a2, vmax: V.rev, label: `통로 폭이 모자람 — 핸들 왼쪽 후진으로 ${a2D}° 보정` }], desc: `전진+후진 ${a2D}°` });
      }
    }
  }
  return out.map(c => { const w = withLeave(L, fin, c.man.filter(q => q.len > 0.005)); return w && { ...w, desc: c.desc }; }).filter(Boolean);
}

function evaluate(cands, obstacles) {
  return cands.map(c => {
    const man = c.segs.filter(s => !s.approach);
    const cl = clearance(sample(c.start, c.segs), obstacles);
    return { ...c, c: cl.c, gears: gearChanges(c.segs), man: pathLen(man) };
  });
}

function plan(type, mode) {
  const L = makeLayout(type);
  const cars = parkedCars(L);
  const obstacles = [...cars.map(c => ({ poly: c.poly })), ...L.walls];
  const enter = pick(evaluate(candidatesEnter(L, type === 'parallel' ? 'rear' : mode), obstacles));
  const fin = run(enter.start, enter.segs);
  const exit = pick(evaluate(candidatesExit(L, fin), obstacles));
  return { L, cars, obstacles, enter, exit, fin };
}

if (typeof module !== 'undefined') module.exports = { plan, sample, step, run, CAR, RMIN, footprint, centerFromRa, clearance };
if (typeof module !== 'undefined') Object.assign(module.exports, { makeLayout, parkedCars, candidatesEnter, candidatesExit, evaluate });
