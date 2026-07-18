let display = require("display");
let input = require("input");
let timer = require("timer");

let W = 36;
let H = 8;
let CELL = 2;

let snake = [];
let direction = { dx: 1, dy: 0 };
let food = { x: 0, y: 0 };
let state = "countdown";
let countdownNum = 3;
let speed = 1 / 3;
let timerRef = null;
let blinkRef = null;
let blinkVisible = true;

function colorHex(r, g, b) {
  function hexByte(value) {
    let clamped = Math.max(0, Math.min(255, Math.floor(value)));
    let hex = clamped.toString(16);
    return hex.length === 1 ? "0" + hex : hex;
  }

  return "#" + hexByte(r) + hexByte(g) + hexByte(b);
}

function snakeColor(index) {
  let n = snake.length;
  if (n <= 1) return colorHex(0, 255, 0);
  let t = (index - 1) / (n - 1);
  let g = Math.floor(255 - (255 - 60) * t);
  return colorHex(0, g, 0);
}

function randomFood() {
  let used = {};
  let candidates = [];

  for (let i = 0; i < snake.length; i++) {
    let segment = snake[i];
    used[str(segment.x) + "," + str(segment.y)] = true;
  }

  for (let x = 0; x < W; x++) {
    for (let y = 0; y < H; y++) {
      let key = str(x) + "," + str(y);
      if (!used[key]) {
        candidates.push({ x: x, y: y });
      }
    }
  }

  if (candidates.length === 0) return;
  let choice = candidates[Math.floor(Math.random() * candidates.length)];
  food.x = choice.x;
  food.y = choice.y;
}

function resetGame() {
  snake = [
    { x: 2, y: 4 },
    { x: 1, y: 4 },
    { x: 0, y: 4 },
  ];
  direction = { dx: 1, dy: 0 };
  state = "countdown";
  countdownNum = 3;
  speed = 1 / 3;
  blinkVisible = true;
  randomFood();
}

function drawCell(cellX, cellY, color) {
  display.rect(cellX * CELL, cellY * CELL, CELL, CELL, {
    color: color,
    filled: true,
  });
}

function drawGame() {
  display.clear();
  display.image("background.png", {
    x: 0,
    y: 0,
    background: true,
  });

  drawCell(food.x, food.y, "#ff3333");

  for (let i = 0; i < snake.length; i++) {
    let segment = snake[i];
    drawCell(segment.x, segment.y, snakeColor(i + 1));
  }

  if (state === "countdown") {
    display.text(str(countdownNum), {
      font: "big",
      align: "left",
      x: 32,
      y: 0,
      color: "#ffffff",
    });
  }

  display.show();
}

function drawDeath() {
  display.clear();
  display.rect(0, 0, 72, 16, {
    color: "#6a0000",
    filled: true,
  });
  display.text("you're dead", {
    font: "small",
    align: "left",
    x: 14,
    y: 0,
    color: "#ffffff",
  });
  if (blinkVisible) {
    display.text("restart", {
      font: "small",
      align: "left",
      x: 22,
      y: 7,
      color: "#ffffff",
    });
  }
  display.show();
}

function stopTimer(ref) {
  if (ref !== null && ref !== undefined) {
    timer.cancel(ref);
  }
}

function enterDead() {
  stopTimer(timerRef);
  timerRef = null;
  stopTimer(blinkRef);
  blinkRef = null;
  blinkVisible = true;
  drawDeath();
  blinkRef = timer.every(0.5, function() {
    blinkVisible = !blinkVisible;
    drawDeath();
  });
}

function moveSnake() {
  if (state !== "play") return;

  let head = snake[0];
  let nx = head.x + direction.dx;
  let ny = head.y + direction.dy;

  if (nx < 0 || nx >= W || ny < 0 || ny >= H) {
    state = "dead";
    enterDead();
    return;
  }

  for (let i = 0; i < snake.length; i++) {
    let segment = snake[i];
    if (segment.x === nx && segment.y === ny) {
      state = "dead";
      enterDead();
      return;
    }
  }

  snake.unshift({ x: nx, y: ny });

  if (nx === food.x && ny === food.y) {
    randomFood();
    speed = Math.max(0.1, speed * 0.92);
    drawGame();
    startTimer();
  } else {
    snake.pop();
    drawGame();
  }
}

function countdownTick() {
  if (state !== "countdown") return;
  countdownNum--;
  if (countdownNum <= 0) {
    state = "play";
    drawGame();
    startTimer();
    return;
  }
  drawGame();
}

function gameTick() {
  if (state === "countdown") {
    countdownTick();
  } else if (state === "play") {
    moveSnake();
  }
}

function startTimer() {
  stopTimer(timerRef);
  timerRef = timer.every(state === "countdown" ? 1.0 : speed, gameTick);
}

function stopBlinkTimer() {
  stopTimer(blinkRef);
  blinkRef = null;
}

input.on("up", "short", function() {
  if (state === "play") {
    let dx = direction.dx;
    let dy = direction.dy;
    direction.dx = -dy;
    direction.dy = dx;
  }
});

input.on("down", "short", function() {
  if (state === "play") {
    let dx = direction.dx;
    let dy = direction.dy;
    direction.dx = dy;
    direction.dy = -dx;
  }
});

input.on("ok", "short", function() {
  if (state === "dead") {
    stopBlinkTimer();
    resetGame();
    drawGame();
    startTimer();
  }
});

resetGame();
drawGame();
startTimer();
__runLoop();
