let display = require("display");
let input = require("input");
let timer = require("timer");
let system = require("system");

let SCREEN_W = 160;
let FULL_H = 80;
let VIEW_H = 66;
let HUD_Y = 66;
let COL_W = 2;
let NUM_RAYS = 80;
let FOV = 60;
let HALF_FOV = 30;
let MAP_S = 16;
let MOVE_SPEED = 0.12;
let ROT_SPEED = 3;
let PI = 3.14159265;
let TICK_S = 0.035;
let EN_SPEED = 0.015;
let EN_CHASE = 0.035;
let EN_ATK_INT = 50;
let EN_ATK_DMG = 8;
let EN_LOS_INT = 15;
let HEX = "0123456789abcdef";

let LEVELS = [
  {
    m: "1111111111111111" +
       "1000000100000001" +
       "1000000000000001" +
       "1000001000001001" +
       "1000001050001001" +
       "1000000000001001" +
       "1000000000000001" +
       "1001100500000021" +
       "1001100000000001" +
       "1000000000110001" +
       "1000000000110001" +
       "1000002000000031" +
       "1000000000000001" +
       "1000100000100001" +
       "1000000000000001" +
       "1111111211131111",
    px: 1.5, py: 1.5, pa: 0,
    en: [8.5,3.5,3, 12.5,7.5,2, 5.5,11.5,2]
  },
  {
    m: "1111111111111111" +
       "1000000000030001" +
       "1011100001030001" +
       "1010000001000001" +
       "1010011100000001" +
       "1000010050000001" +
       "1000010000011101" +
       "1000000000010001" +
       "1050000000010001" +
       "1011110000000001" +
       "1000010000000001" +
       "1000000000111101" +
       "1000000500000001" +
       "1011101001010001" +
       "1000000001000001" +
       "1111211131111111",
    px: 1.5, py: 1.5, pa: 90,
    en: [5.5,2.5,3, 10.5,6.5,3, 3.5,10.5,3, 13.5,12.5,2]
  },
  {
    m: "1111111111111111" +
       "1000000500020001" +
       "1011100000020001" +
       "1010000100000001" +
       "1010050100111101" +
       "1000000100000001" +
       "1000000000050031" +
       "1011110000011101" +
       "1000010500010001" +
       "1000010000010001" +
       "1050000001000001" +
       "1011110001000001" +
       "1000050000000001" +
       "1000010001011101" +
       "1000000000000001" +
       "1111311121111111",
    px: 1.5, py: 14.5, pa: 0,
    en: [6.5,1.5,4, 13.5,4.5,3, 3.5,7.5,4, 10.5,9.5,3, 7.5,13.5,4]
  }
];

let map = [];
function mapGet(x, y) {
  if (x < 0 || x >= MAP_S || y < 0 || y >= MAP_S) return 1;
  return map[y * MAP_S + x];
}
function mapSet(x, y, v) {
  if (x >= 0 && x < MAP_S && y >= 0 && y < MAP_S) map[y * MAP_S + x] = v;
}

let sinT = [];
let cosT = [];
for (let i = 0; i < 360; i++) {
  let r = i * PI / 180;
  sinT.push(Math.sin(r));
  cosT.push(Math.cos(r));
}
function sinDeg(a) { return sinT[Math.floor(((a % 360) + 360) % 360)]; }
function cosDeg(a) { return cosT[Math.floor(((a % 360) + 360) % 360)]; }

function toHex(v) {
  v = Math.max(0, Math.min(255, Math.floor(v)));
  return HEX.charAt(Math.floor(v / 16)) + HEX.charAt(v % 16);
}
function grayColor(b) { let h = toHex(b); return "#" + h + h + h; }

let pX = 1.5, pY = 1.5, pA = 0;
let playerHP = 100;
let shooting = 0;
let gameState = 0;
let menuOpen = 0;
let menuChoice = 0;
let currentLevel = 0;
let enemies = [];
let totalEn = 0;
let kills = 0;
let aiTick = 0;
let stateTimer = 0;
let rotQueue = 0;
let heldStart = 0;
let heldOk = 0;
let wantShoot = 0;
let zbuf = [];
for (let i = 0; i < NUM_RAYS; i++) zbuf.push(0);

function loadLevel(idx) {
  let lv = LEVELS[idx];
  map = [];
  for (let i = 0; i < MAP_S * MAP_S; i++) map.push(lv.m.charCodeAt(i) - 48);
  pX = lv.px; pY = lv.py; pA = lv.pa;
  playerHP = 100;
  enemies = [];
  for (let i = 0; i < lv.en.length; i += 3) {
    enemies.push({
      x: lv.en[i], y: lv.en[i + 1], hp: lv.en[i + 2],
      st: 0, dir: Math.floor(Math.random() * 4) * 90, atk: 0
    });
  }
  totalEn = enemies.length;
  kills = 0;
  gameState = 0;
  shooting = 0;
  aiTick = 0;
  stateTimer = 0;
  rotQueue = 0;
  heldStart = 0;
  heldOk = 0;
  wantShoot = 0;
}

let rayR = { d: 0, s: 0, w: 0 };
function castRay(angle) {
  let ra = ((angle % 360) + 360) % 360;
  let ri = Math.floor(ra);
  let rs = sinT[ri], rc = cosT[ri];
  let mx = Math.floor(pX), my = Math.floor(pY);
  let ddx = Math.abs(1 / (rc === 0 ? 0.0001 : rc));
  let ddy = Math.abs(1 / (rs === 0 ? 0.0001 : rs));
  let sx, sy, sdx, sdy;
  if (rc < 0) { sx = -1; sdx = (pX - mx) * ddx; }
  else { sx = 1; sdx = (mx + 1 - pX) * ddx; }
  if (rs < 0) { sy = -1; sdy = (pY - my) * ddy; }
  else { sy = 1; sdy = (my + 1 - pY) * ddy; }
  let side = 0, wt = 1;
  for (let i = 0; i < 64; i++) {
    if (sdx < sdy) { sdx += ddx; mx += sx; side = 0; }
    else { sdy += ddy; my += sy; side = 1; }
    let c = mapGet(mx, my);
    if (c > 0) { wt = c; break; }
  }
  let d = side === 0 ? sdx - ddx : sdy - ddy;
  if (d < 0.01) d = 0.01;
  rayR.d = d; rayR.s = side; rayR.w = wt;
}

function hasLOS(x1, y1, x2, y2) {
  let dx = x2 - x1, dy = y2 - y1;
  let dist = Math.sqrt(dx * dx + dy * dy);
  if (dist < 0.1) return 1;
  let steps = Math.ceil(dist * 3);
  for (let i = 1; i < steps; i++) {
    let t = i / steps;
    if (mapGet(Math.floor(x1 + dx * t), Math.floor(y1 + dy * t)) > 0) return 0;
  }
  return 1;
}

function updateEnemies() {
  aiTick++;
  for (let i = 0; i < enemies.length; i++) {
    let e = enemies[i];
    if (e.st === 3) continue;
    let dx = pX - e.x, dy = pY - e.y;
    let dist = Math.sqrt(dx * dx + dy * dy);

    if (e.st === 0) {
      let nx = e.x + cosDeg(e.dir) * EN_SPEED;
      let ny = e.y + sinDeg(e.dir) * EN_SPEED;
      if (mapGet(Math.floor(nx), Math.floor(ny)) > 0) {
        e.dir = (e.dir + 180) % 360;
      } else {
        e.x = nx; e.y = ny;
      }
      if (aiTick % EN_LOS_INT === 0 && dist < 10 && hasLOS(e.x, e.y, pX, pY)) {
        e.st = 1;
      }
    } else if (e.st === 1) {
      if (dist < 1.5) {
        e.st = 2; e.atk = 0;
      } else if (dist > 0.5) {
        let spd = EN_CHASE;
        let nx = e.x + (dx / dist) * spd;
        let ny = e.y + (dy / dist) * spd;
        if (mapGet(Math.floor(nx), Math.floor(ny)) === 0) {
          e.x = nx; e.y = ny;
        }
      }
      if (aiTick % (EN_LOS_INT * 2) === 0 && !hasLOS(e.x, e.y, pX, pY)) {
        e.st = 0;
      }
    } else if (e.st === 2) {
      e.atk++;
      if (e.atk >= EN_ATK_INT) {
        e.atk = 0;
        if (hasLOS(e.x, e.y, pX, pY)) playerHP -= EN_ATK_DMG;
      }
      if (dist > 3.0) e.st = 1;
    }
  }
}

function tryShoot() {
  shooting = 5;
  let bestD = 999, bestI = -1;
  for (let i = 0; i < enemies.length; i++) {
    let e = enemies[i];
    if (e.st === 3) continue;
    let dx = e.x - pX, dy = e.y - pY;
    let dist = Math.sqrt(dx * dx + dy * dy);
    if (dist > 12) continue;
    let ang = Math.atan2(dy, dx) * 180 / PI;
    let rel = ang - pA;
    while (rel > 180) rel -= 360;
    while (rel < -180) rel += 360;
    if (Math.abs(rel) < 6 && hasLOS(pX, pY, e.x, e.y)) {
      if (dist < bestD) { bestD = dist; bestI = i; }
    }
  }
  if (bestI >= 0) {
    enemies[bestI].hp--;
    if (enemies[bestI].hp <= 0) {
      enemies[bestI].st = 3;
      kills++;
      if (kills >= totalEn) {
        if (currentLevel >= LEVELS.length - 1) gameState = 3;
        else { gameState = 2; stateTimer = 80; }
      }
    }
  }
}

function tryMove(dx, dy) {
  let nx = pX + dx, ny = pY + dy, m = 0.2, blocked = 0;
  let cx, cy, c;
  cx = Math.floor(nx + m); cy = Math.floor(ny + m); c = mapGet(cx, cy);
  if (c === 5) { mapSet(cx, cy, 0); blocked = 1; } else if (c > 0) blocked = 1;
  cx = Math.floor(nx - m); cy = Math.floor(ny + m); c = mapGet(cx, cy);
  if (c === 5) { mapSet(cx, cy, 0); blocked = 1; } else if (c > 0) blocked = 1;
  cx = Math.floor(nx + m); cy = Math.floor(ny - m); c = mapGet(cx, cy);
  if (c === 5) { mapSet(cx, cy, 0); blocked = 1; } else if (c > 0) blocked = 1;
  cx = Math.floor(nx - m); cy = Math.floor(ny - m); c = mapGet(cx, cy);
  if (c === 5) { mapSet(cx, cy, 0); blocked = 1; } else if (c > 0) blocked = 1;
  if (!blocked) { pX = nx; pY = ny; }
}

function moveForward() { tryMove(cosDeg(pA) * MOVE_SPEED, sinDeg(pA) * MOVE_SPEED); }
function moveBackward() { tryMove(-cosDeg(pA) * MOVE_SPEED, -sinDeg(pA) * MOVE_SPEED); }
function rotateLeft() { pA -= ROT_SPEED; if (pA < 0) pA += 360; }
function rotateRight() { pA += ROT_SPEED; if (pA >= 360) pA -= 360; }

function renderWalls() {
  let halfV = Math.floor(VIEW_H / 2);
  display.rect(0, 0, SCREEN_W, halfV, { color: "#111111", filled: true, display: "back" });
  display.rect(0, halfV, SCREEN_W, VIEW_H - halfV, { color: "#222222", filled: true, display: "back" });

  for (let col = 0; col < NUM_RAYS; col++) {
    let ra = pA - HALF_FOV + col * FOV / NUM_RAYS;
    castRay(ra);
    let cd = rayR.d * cosDeg(pA - ra);
    if (cd < 0.01) cd = 0.01;
    zbuf[col] = cd;
    let wh = Math.floor(VIEW_H / cd);
    if (wh > VIEW_H) wh = VIEW_H;
    let wy = Math.floor((VIEW_H - wh) / 2);
    let br = 255 - Math.floor(cd * 16);
    if (br < 30) br = 30;
    if (rayR.s === 1) br = Math.floor(br * 0.65);
    if (rayR.w === 2) br = Math.floor(br * 0.85);
    else if (rayR.w === 3) br = Math.floor(br * 0.7);
    else if (rayR.w === 4) br = Math.floor(br * 0.55);
    else if (rayR.w === 5) br = Math.min(255, br + 50);
    display.rect(col * COL_W, wy, COL_W, wh, { color: grayColor(br), filled: true, display: "back" });
  }
}

function renderEnemies() {
  for (let i = 0; i < enemies.length; i++) {
    let e = enemies[i];
    if (e.st === 3) continue;
    let dx = e.x - pX, dy = e.y - pY;
    let dist = Math.sqrt(dx * dx + dy * dy);
    if (dist < 0.3 || dist > 14) continue;
    let ang = Math.atan2(dy, dx) * 180 / PI;
    let rel = ang - pA;
    while (rel > 180) rel -= 360;
    while (rel < -180) rel += 360;
    if (Math.abs(rel) > HALF_FOV + 5) continue;
    let sx = Math.floor((rel / FOV + 0.5) * SCREEN_W);
    let sh = Math.floor(VIEW_H * 0.7 / dist);
    if (sh > VIEW_H) sh = VIEW_H;
    if (sh < 3) sh = 3;
    let sw = Math.floor(sh * 0.35);
    if (sw < 2) sw = 2;
    let cc = Math.floor(sx / COL_W);
    if (cc < 0 || cc >= NUM_RAYS || zbuf[cc] < dist) continue;
    let bx = sx - Math.floor(sw / 2);
    let by = Math.floor((VIEW_H - sh) / 2);
    let hh = Math.floor(sh * 0.2);
    if (hh < 1) hh = 1;
    let br = 220 - Math.floor(dist * 14);
    if (br < 60) br = 60;
    let ec = grayColor(br);
    let bodyX = bx, bodyW = sw, bodyY = by + hh, bodyH = sh - hh;
    if (bodyX < 0) { bodyW += bodyX; bodyX = 0; }
    if (bodyX + bodyW > SCREEN_W) bodyW = SCREEN_W - bodyX;
    if (bodyW > 0 && bodyH > 0)
      display.rect(bodyX, bodyY, bodyW, bodyH, { color: ec, filled: true, display: "back" });
    let hx = bx + Math.floor(sw / 4);
    let hw = Math.max(2, Math.floor(sw / 2));
    if (hx < 0) { hw += hx; hx = 0; }
    if (hx + hw > SCREEN_W) hw = SCREEN_W - hx;
    if (hw > 0 && hh > 0)
      display.rect(hx, by, hw, hh, { color: ec, filled: true, display: "back" });
  }
}

function drawMinimap() {
  let mx0 = 2, my0 = 2, cs = 3, rng = 5, half = 2;
  let ca = cosDeg(pA), sa = sinDeg(pA);
  let sz = rng * cs;
  display.rect(mx0 - 1, my0 - 1, sz + 2, sz + 2, { color: "#000000", filled: true, display: "back" });
  for (let my = 0; my < rng; my++) {
    for (let mx = 0; mx < rng; mx++) {
      let ox = mx - half, oy = my - half;
      let wx = Math.floor(pX - ox * sa - oy * ca);
      let wy = Math.floor(pY + ox * ca - oy * sa);
      let c = mapGet(wx, wy);
      if (c > 0) {
        display.rect(mx0 + mx * cs, my0 + my * cs, cs, cs, {
          color: c === 5 ? "#999999" : "#666666", filled: true, display: "back",
        });
      }
    }
  }
  let pcx = mx0 + half * cs;
  let pcy = my0 + half * cs;
  display.rect(pcx, pcy, 2, 2, { color: "#ffffff", filled: true, display: "back" });
  display.line(pcx + 1, pcy, pcx + 1, pcy - 3, { color: "#ffffff", display: "back" });

  for (let i = 0; i < enemies.length; i++) {
    let e = enemies[i];
    if (e.st === 3) continue;
    let edx = e.x - pX, edy = e.y - pY;
    let emx = -edx * sa + edy * ca;
    let emy = -edx * ca - edy * sa;
    let esx = Math.floor(mx0 + (half + emx) * cs);
    let esy = Math.floor(my0 + (half + emy) * cs);
    if (esx >= mx0 && esx < mx0 + sz - 1 && esy >= my0 && esy < my0 + sz - 1) {
      display.rect(esx, esy, 2, 2, { color: "#cccccc", filled: true, display: "back" });
    }
  }
}

function drawCrosshair() {
  let cx = 80, cy = Math.floor(VIEW_H / 2);
  display.line(cx - 8, cy, cx - 3, cy, { color: "#ffffff", display: "back" });
  display.line(cx + 3, cy, cx + 8, cy, { color: "#ffffff", display: "back" });
  display.line(cx, cy - 6, cx, cy - 2, { color: "#ffffff", display: "back" });
  display.line(cx, cy + 2, cx, cy + 6, { color: "#ffffff", display: "back" });
  if (shooting > 3) {
    display.rect(cx - 1, cy - 1, 3, 3, { color: "#ffffff", filled: true, display: "back" });
  }
}

function drawHUD() {
  display.rect(0, HUD_Y, SCREEN_W, FULL_H - HUD_Y, { color: "#000000", filled: true, display: "back" });
  display.line(0, HUD_Y, SCREEN_W, HUD_Y, { color: "#444444", display: "back" });
  let hpW = Math.max(0, Math.floor(40 * playerHP / 100));
  display.rect(2, HUD_Y + 3, 40, 8, { color: "#222222", filled: true, display: "back" });
  if (hpW > 0) {
    let hpC = playerHP > 50 ? "#aaaaaa" : (playerHP > 25 ? "#666666" : "#444444");
    display.rect(2, HUD_Y + 3, hpW, 8, { color: hpC, filled: true, display: "back" });
  }
  display.text(str(Math.floor(playerHP)), {
    x: 14, y: HUD_Y + 3, align: "left", font: "small", color: "#ffffff", display: "back",
  });
  display.text("L" + str(currentLevel + 1), {
    x: 50, y: HUD_Y + 3, align: "left", font: "small", color: "#ffffff", display: "back",
  });
  display.text(str(kills) + "/" + str(totalEn), {
    x: 75, y: HUD_Y + 3, align: "left", font: "small", color: "#ffffff", display: "back",
  });
}

function renderOverlay() {
  if (gameState === 1) {
    display.rect(30, 16, 100, 34, { color: "#000000", filled: true, display: "back" });
    display.rect(30, 16, 100, 34, { color: "#ffffff", filled: false, display: "back" });
    display.text("GAME OVER", {
      x: 44, y: 22, align: "left", font: "medium", color: "#ffffff", display: "back",
    });
    display.text("Press BACK", {
      x: 48, y: 38, align: "left", font: "small", color: "#aaaaaa", display: "back",
    });
  } else if (gameState === 2) {
    display.rect(15, 18, 130, 28, { color: "#000000", filled: true, display: "back" });
    display.rect(15, 18, 130, 28, { color: "#ffffff", filled: false, display: "back" });
    display.text("LEVEL COMPLETE", {
      x: 28, y: 27, align: "left", font: "medium", color: "#ffffff", display: "back",
    });
  } else if (gameState === 3) {
    display.rect(30, 14, 100, 40, { color: "#000000", filled: true, display: "back" });
    display.rect(30, 14, 100, 40, { color: "#ffffff", filled: false, display: "back" });
    display.text("YOU WIN!", {
      x: 50, y: 20, align: "left", font: "medium", color: "#ffffff", display: "back",
    });
    display.text("Press BACK", {
      x: 48, y: 40, align: "left", font: "small", color: "#aaaaaa", display: "back",
    });
  }
}

function renderMenu() {
  display.rect(40, 14, 80, 42, { color: "#000000", filled: true, display: "back" });
  display.rect(40, 14, 80, 42, { color: "#ffffff", filled: false, display: "back" });
  display.text("PAUSED", {
    x: 56, y: 18, align: "left", font: "medium", color: "#ffffff", display: "back",
  });
  display.text("> Resume", {
    x: 50, y: 32, align: "left", font: "small",
    color: menuChoice === 0 ? "#ffffff" : "#666666", display: "back",
  });
  display.text("> Quit", {
    x: 50, y: 42, align: "left", font: "small",
    color: menuChoice === 1 ? "#ffffff" : "#666666", display: "back",
  });
}

function render() {
  display.clear();
  renderWalls();
  renderEnemies();
  drawMinimap();
  if (shooting > 0) { drawCrosshair(); shooting--; }
  drawHUD();
  if (gameState !== 0) renderOverlay();
  if (menuOpen) renderMenu();
  display.show();
}

function tick() {
  if (menuOpen) return;
  if (gameState === 2) {
    stateTimer--;
    if (stateTimer <= 0) { currentLevel++; loadLevel(currentLevel); }
    render();
    return;
  }
  if (gameState !== 0) return;

  let dirty = 0;
  while (rotQueue > 0) { rotateRight(); rotQueue--; dirty = 1; }
  while (rotQueue < 0) { rotateLeft(); rotQueue++; dirty = 1; }
  if (heldStart) { moveForward(); dirty = 1; }
  if (heldOk) { moveBackward(); dirty = 1; }
  if (wantShoot) { tryShoot(); wantShoot = 0; dirty = 1; }

  updateEnemies();
  if (playerHP <= 0) { playerHP = 0; gameState = 1; dirty = 1; }

  if (dirty || shooting > 0 || aiTick % 3 === 0) render();
}

input.on("down", "short", function () {
  if (menuOpen) { menuChoice = menuChoice === 0 ? 1 : 0; render(); return; }
  rotQueue -= 2;
});
input.on("up", "short", function () {
  if (menuOpen) { menuChoice = menuChoice === 0 ? 1 : 0; render(); return; }
  rotQueue += 2;
});
input.on("start", "press", function () { if (gameState === 0 && !menuOpen) heldStart = 1; });
input.on("start", "release", function () { heldStart = 0; });
input.on("ok", "press", function () {
  if (gameState === 1) { loadLevel(currentLevel); render(); return; }
  if (gameState === 3) { currentLevel = 0; loadLevel(0); render(); return; }
  if (gameState === 0 && !menuOpen) heldOk = 1;
});
input.on("ok", "release", function () { heldOk = 0; });
input.on("back", "short", function () {
  if (gameState === 1) { loadLevel(currentLevel); render(); return; }
  if (gameState === 3) { currentLevel = 0; loadLevel(0); render(); return; }
  if (menuOpen) {
    if (menuChoice === 0) menuOpen = 0;
    else system.exit();
    render();
    return;
  }
  wantShoot = 1;
});
input.on("back", "long", function () {
  if (gameState === 0 && !menuOpen) {
    menuOpen = 1; menuChoice = 0;
    rotQueue = 0; heldStart = 0; heldOk = 0;
    render();
  }
});

loadLevel(0);
render();
timer.every(TICK_S, tick);
__runLoop();
