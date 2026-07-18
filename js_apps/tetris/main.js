let audio = require("audio");
let display = require("display");
let input = require("input");
let json = require("json");
let storage = require("storage");
let timer = require("timer");

let CELL = 2;
let COLS = 22;
let ROWS = 8;
let FIELD_X = 28;
let BASE_SPEED = 0.35;
let FAST_SPEED = 0.05;

let SCORE_TABLE = [0, 10, 25, 35, 45];
let SHAPES = [
  [[0, 0], [0, 1], [0, 2], [0, 3]],
  [[0, 0], [0, 1], [1, 0], [1, 1]],
  [[0, 0], [0, 1], [0, 2], [1, 1]],
  [[0, 1], [0, 2], [1, 0], [1, 1]],
  [[0, 0], [0, 1], [1, 1], [1, 2]],
  [[0, 0], [0, 1], [0, 2], [1, 0]],
  [[0, 0], [0, 1], [0, 2], [1, 2]],
];
let COLORS = [
  "#00cccc",
  "#cccc00",
  "#cc00cc",
  "#00cc00",
  "#cc0000",
  "#cc6600",
  "#0066cc",
];

let board = [];
let current = null;
let score = 0;
let best = 0;
let lines = 0;
let state = "play";
let timerRef = null;
let fastFall = false;

function playSound(name) {
  try {
    audio.play(name + ".snd");
  } catch (error) {
    print("tetris sound failed", name, error);
  }
}

function cloneShape(shape) {
  let copy = [];
  for (let i = 0; i < shape.length; i++) {
    copy.push([shape[i][0], shape[i][1]]);
  }
  return copy;
}

function safeReadJson(path, fallbackValue) {
  try {
    let raw = storage.read(path);
    if (!raw) return fallbackValue;
    let parsed = json.parse(raw);
    return parsed || fallbackValue;
  } catch (error) {
    return fallbackValue;
  }
}

function safeWriteJson(path, value) {
  try {
    storage.write(path, json.stringify(value));
  } catch (error) {
    print("tetris save failed", error);
  }
}

function loadBest() {
  if (storage.exists("best.json")) {
    let data = safeReadJson("best.json", null);
    if (data && data.best !== undefined && data.best !== null) {
      best = data.best;
    }
  }
}

function saveBest() {
  safeWriteJson("best.json", { best: best });
}

function updateBest() {
  if (score > best) {
    best = score;
    saveBest();
  }
}

function getSpeed() {
  let level = Math.floor(lines / 5);
  return Math.max(0.08, BASE_SPEED - level * 0.03);
}

function initBoard() {
  board = [];
  for (let c = 0; c < COLS; c++) {
    board.push([]);
  }
}

function pieceBlocks(shape, col, row) {
  let blocks = [];
  for (let i = 0; i < shape.length; i++) {
    blocks.push([col + shape[i][0], row + shape[i][1]]);
  }
  return blocks;
}

function validPos(shape, col, row) {
  for (let i = 0; i < shape.length; i++) {
    let c = col + shape[i][0];
    let r = row + shape[i][1];
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return false;
    if (board[c][r]) return false;
  }
  return true;
}

function rotateShape(shape) {
  let next = [];
  for (let i = 0; i < shape.length; i++) {
    next.push([shape[i][1], -shape[i][0]]);
  }

  let minCol = next[0][0];
  let minRow = next[0][1];
  for (let i = 1; i < next.length; i++) {
    minCol = Math.min(minCol, next[i][0]);
    minRow = Math.min(minRow, next[i][1]);
  }

  for (let i = 0; i < next.length; i++) {
    next[i][0] -= minCol;
    next[i][1] -= minRow;
  }

  return next;
}

function spawnPiece() {
  let index = Math.floor(Math.random() * SHAPES.length);
  let shape = cloneShape(SHAPES[index]);
  let maxCol = 0;
  let maxRow = 0;

  for (let i = 0; i < shape.length; i++) {
    maxCol = Math.max(maxCol, shape[i][0]);
    maxRow = Math.max(maxRow, shape[i][1]);
  }

  let col = COLS - maxCol - 1;
  let row = Math.floor((ROWS - maxRow - 1) / 2);

  if (!validPos(shape, col, row)) {
    state = "gameover";
    updateBest();
    playSound("game-over");
    return;
  }

  current = {
    shape: shape,
    color: COLORS[index],
    col: col,
    row: row,
  };
}

function lockPiece() {
  let blocks = pieceBlocks(current.shape, current.col, current.row);
  for (let i = 0; i < blocks.length; i++) {
    let block = blocks[i];
    board[block[0]][block[1]] = current.color;
  }
  current = null;
}

function clearLines() {
  let cleared = 0;
  let c = 0;

  while (c < COLS) {
    let full = true;
    for (let r = 0; r < ROWS; r++) {
      if (!board[c][r]) {
        full = false;
        break;
      }
    }

    if (full) {
      cleared++;
      for (let cc = c; cc < COLS - 1; cc++) {
        board[cc] = board[cc + 1];
      }
      board[COLS - 1] = [];
    } else {
      c++;
    }
  }

  return cleared;
}

function drawBoardRun(row) {
  let runStart = null;
  let runColor = null;

  for (let c = 0; c < COLS; c++) {
    let cell = board[c][row];
    if (cell && cell === runColor) {
      continue;
    }

    if (runStart !== null) {
      display.rect(FIELD_X + runStart * CELL, row * CELL, (c - runStart) * CELL, CELL, {
        color: runColor,
        filled: true,
      });
    }

    if (cell) {
      runStart = c;
      runColor = cell;
    } else {
      runStart = null;
      runColor = null;
    }
  }

  if (runStart !== null) {
    display.rect(FIELD_X + runStart * CELL, row * CELL, (COLS - runStart) * CELL, CELL, {
      color: runColor,
      filled: true,
    });
  }
}

function drawFrontInfo() {
  display.image("best.png", {
    x: 1,
    y: 1,
  });

  let bestStr = str(Math.floor(best));
  for (let i = 0; i < bestStr.length; i++) {
    display.text(bestStr.slice(i, i + 1), {
      font: "small",
      x: 6,
      y: i * 5 + 1,
      align: "left",
      rotation: 90,
      color: "#ffffff",
    });
  }

  display.image("score.png", {
    x: 14,
    y: 1,
  });

  let scoreStr = str(Math.floor(score));
  for (let i = 0; i < scoreStr.length; i++) {
    display.text(scoreStr.slice(i, i + 1), {
      font: "small",
      x: 19,
      y: i * 5 + 1,
      align: "left",
      rotation: 90,
      color: "#ffffff",
    });
  }
}

function drawBackInfo() {
  let level = Math.floor(lines / 5);

  display.text("Tetris", {
    font: "medium",
    align: "left",
    x: 0,
    y: 0,
    display: "back",
    color: "#ffffff",
  });
  display.text("Score: " + str(Math.floor(score)), {
    font: "small",
    align: "left",
    x: 0,
    y: 14,
    display: "back",
    color: "#00ff88",
  });
  display.text("Best:  " + str(Math.floor(best)), {
    font: "small",
    align: "left",
    x: 0,
    y: 22,
    display: "back",
    color: "#ffffff",
  });
  display.text("Lines: " + str(Math.floor(lines)), {
    font: "small",
    align: "left",
    x: 0,
    y: 30,
    display: "back",
    color: "#ffaa00",
  });
  display.text("Level: " + str(level), {
    font: "small",
    align: "left",
    x: 0,
    y: 38,
    display: "back",
    color: "#ffffff",
  });
}

function drawGameOverOverlay() {
  display.rect(FIELD_X + 2, 2, 40, 12, {
    color: "#000000",
    filled: true,
  });
  display.text("GAME OVER", {
    font: "small",
    align: "left",
    x: FIELD_X + 6,
    y: 2,
    color: "#ffffff",
  });
  display.text(str(Math.floor(score)), {
    font: "small",
    align: "left",
    x: FIELD_X + 14,
    y: 9,
    color: "#ffaa00",
  });
}

function draw() {
  display.clear();

  display.rect(FIELD_X, 0, COLS * CELL, ROWS * CELL, {
    color: "#161616",
    filled: true,
  });

  for (let row = 0; row < ROWS; row++) {
    drawBoardRun(row);
  }

  if (current) {
    let blocks = pieceBlocks(current.shape, current.col, current.row);
    for (let i = 0; i < blocks.length; i++) {
      display.rect(FIELD_X + blocks[i][0] * CELL, blocks[i][1] * CELL, CELL, CELL, {
        color: current.color,
        filled: true,
      });
    }
  }

  drawFrontInfo();

  if (state === "gameover") {
    drawGameOverOverlay();
  }

  drawBackInfo();
  display.show();
}

function stopTimer(ref) {
  if (ref !== null && ref !== undefined) {
    timer.cancel(ref);
  }
}

function tick() {
  if (state !== "play" || !current) return;

  if (validPos(current.shape, current.col - 1, current.row)) {
    current.col--;
  } else {
    lockPiece();
    let cleared = clearLines();
    if (cleared > 0) {
      lines += cleared;
      score += SCORE_TABLE[cleared] || cleared * 10;
      playSound("row-done");
    } else {
      score += 1;
      playSound("mounted");
    }

    updateBest();
    spawnPiece();

    if (state === "gameover") {
      stopTimer(timerRef);
      timerRef = null;
    } else {
      startTimer();
    }
  }

  draw();
}

function startTimer() {
  stopTimer(timerRef);
  let interval = fastFall ? FAST_SPEED : getSpeed();
  timerRef = timer.every(interval, tick);
}

input.on("up", "short", function() {
  if (current && state === "play" && validPos(current.shape, current.col, current.row + 1)) {
    current.row++;
    draw();
  }
});

input.on("down", "short", function() {
  if (current && state === "play" && validPos(current.shape, current.col, current.row - 1)) {
    current.row--;
    draw();
  }
});

input.on("up", "repeat", function() {
  if (current && state === "play" && validPos(current.shape, current.col, current.row + 1)) {
    current.row++;
    draw();
  }
});

input.on("down", "repeat", function() {
  if (current && state === "play" && validPos(current.shape, current.col, current.row - 1)) {
    current.row--;
    draw();
  }
});

input.on("ok", "short", function() {
  if (state === "gameover") {
    initBoard();
    score = 0;
    lines = 0;
    state = "play";
    fastFall = false;
    spawnPiece();
    draw();
    startTimer();
    return;
  }

  if (current && state === "play") {
    let rotated = rotateShape(current.shape);
    if (validPos(rotated, current.col, current.row)) {
      current.shape = rotated;
      playSound("rotate");
      draw();
    }
  }
});

input.on("ok", "long", function() {
  if (state === "play") {
    fastFall = true;
    startTimer();
  }
});

input.on("ok", "release", function() {
  if (fastFall) {
    fastFall = false;
    if (state === "play") {
      startTimer();
    }
  }
});

loadBest();
initBoard();
spawnPiece();
draw();
startTimer();
__runLoop();
