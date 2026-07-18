let display = require("display");
let input = require("input");
let timer = require("timer");
let settings = require("settings");
let status = require("status");
let sys = require("./system");
let anim = require("./anim");
let block1 = require("./block1");
let block2 = require("./block2");
let block3 = require("./block3");

let blocks = [block1, block2, block3];
let currentBlock = 0;

let STATE_LOADING = 0;
let STATE_TRANSITION = 1;
let STATE_BLOCK = 2;
let STATE_SETTINGS = 3;

let appState = STATE_LOADING;

let transition = {
  loadingOpa: 1.0,
  showBlock: false,
  animScale: 0.3,
  animOpa: 0.0,
  textOpa: 0.0,
  wasLoading: true,
  tweenIds: [],
};

let blockFade = {
  active: false,
  opa: 1.0,
  nextBlock: 0,
  tweenId: null,
};

let needsDraw = true;
let currentMainRate = 0.1;
let lastLedColor = null;
let mainTimerId = null;
let startCooldown = 0;

let RATE_NORMAL = 0.1;
let RATE_FAST = 0.035;

function requestDraw() {
  needsDraw = true;
}

function setMainRate(sec) {
  if (mainTimerId !== null && currentMainRate === sec) return;
  if (mainTimerId !== null) timer.cancel(mainTimerId);
  mainTimerId = timer.every(sec, mainLoop);
  currentMainRate = sec;
}

function draw() {
  try {
    display.clear();

    if (appState === STATE_SETTINGS) {
      display.show();
      return;
    }

    if (appState === STATE_TRANSITION) {
      if (transition.loadingOpa > 0.01) {
        display.text("Loading...", {
          font: "small",
          align: "center",
          color: sys.COLOR_WHITE,
          opacity: transition.loadingOpa,
        });
      }
      if (transition.showBlock) {
        block1.drawFrontAnimated(0, transition);
      }
      display.show();
      needsDraw = false;
      return;
    }

    if (appState === STATE_LOADING) {
      if (sys.noInternet) {
        display.text("No data", {
          font: "small",
          align: "center",
          color: sys.COLOR_WHITE,
        });
      } else {
        display.text("Loading...", {
          font: "small",
          align: "center",
          color: sys.COLOR_WHITE,
        });
      }
      display.show();
      needsDraw = false;
      return;
    }

    if (blockFade.active) {
      blocks[currentBlock].drawFront(0, blockFade.opa);
      display.show();
      needsDraw = false;
      return;
    }

    blocks[currentBlock].drawFront(0);
    display.show();
    needsDraw = false;
  } catch (e) {
    print("weather: draw error", e);
  }
}

function cancelTweenIds(ids) {
  for (let i = 0; i < ids.length; i++) {
    anim.cancel(ids[i]);
  }
}

function stopTransition() {
  if (appState !== STATE_TRANSITION && transition.tweenIds.length === 0) return;
  cancelTweenIds(transition.tweenIds);
  transition.tweenIds = [];
  transition.loadingOpa = 0.0;
  transition.showBlock = false;
  transition.animScale = 1.0;
  transition.animOpa = 1.0;
  transition.textOpa = 1.0;
  if (appState === STATE_TRANSITION) {
    appState = STATE_BLOCK;
  }
}

function stopBlockFade() {
  if (blockFade.tweenId !== null) {
    anim.cancel(blockFade.tweenId);
    blockFade.tweenId = null;
  }
  blockFade.active = false;
  blockFade.opa = 1.0;
}

function startTransition() {
  stopTransition();
  appState = STATE_TRANSITION;
  transition.loadingOpa = 1.0;
  transition.showBlock = false;
  transition.animScale = 0.3;
  transition.animOpa = 0.0;
  transition.textOpa = 0.0;
  transition.tweenIds = [];

  setMainRate(RATE_FAST);

  anim.sequence([
    function(next) {
      let fadeOutId = anim.tween(1.0, 0.0, 500, "easeInOutCubic", function(v) {
        transition.loadingOpa = v;
        requestDraw();
      }, next);
      transition.tweenIds.push(fadeOutId);
    },
    function(next) {
      transition.showBlock = true;
      let animOpaId = anim.tween(0.0, 1.0, 2100, "easeOutQuint", function(v) {
        transition.animOpa = v;
        requestDraw();
      }, function() {
        transition.tweenIds = [];
        appState = STATE_BLOCK;
        setMainRate(RATE_NORMAL);
        requestDraw();
      });
      let animScaleId = anim.tween(0.3, 1.0, 2100, "easeOutQuint", function(v) {
        transition.animScale = v;
        requestDraw();
      });
      let textOpaId = anim.tween(0.0, 1.0, 700, "easeInOutCubic", function(v) {
        transition.textOpa = v;
        requestDraw();
      });
      transition.tweenIds.push(animOpaId);
      transition.tweenIds.push(animScaleId);
      transition.tweenIds.push(textOpaId);
      next();
    },
  ]);
}

function updateLed() {
  try {
    let nextColor = null;
    if (appState === STATE_SETTINGS) {
      nextColor = "off";
    } else if (!sys.loading && sys.currentTemp !== null) {
      if (currentBlock === 1) {
        let ct = block2.cursorTempNum();
        nextColor = sys.tempToColor(ct !== null ? ct : sys.currentTemp);
      } else {
        nextColor = sys.tempToColor(sys.currentTemp);
      }
    }

    if (nextColor === lastLedColor) return;
    lastLedColor = nextColor;

    if (nextColor === "off") {
      status.off();
    } else if (nextColor !== null) {
      status.set(nextColor);
    }
  } catch (e) {
    print("weather: updateLed error", e);
  }
}

sys.requestDraw = requestDraw;
sys.onFirstData = function() {
  if (transition.wasLoading) {
    transition.wasLoading = false;
    startTransition();
  } else {
    requestDraw();
  }
};

sys.loadConfig();
block1.update();
sys.fetchWeather();

function mainLoop() {
  try {
    if (startCooldown > 0) startCooldown = startCooldown - 1;
    if (appState === STATE_SETTINGS) return;

    let currentChanged = sys.refreshCurrent();
    let blockChanged = blocks[currentBlock].update();
    let shouldDraw = appState === STATE_TRANSITION || blockFade.active || needsDraw || currentChanged || blockChanged;
    if (shouldDraw) draw();
    updateLed();
  } catch (e) {
    print("weather: mainLoop error", e);
  }
}

mainTimerId = timer.every(RATE_NORMAL, mainLoop);

timer.every(1800, function() {
  sys.fetchWeather();
});

input.on("start", "short", function() {
  try {
    if (appState === STATE_LOADING || appState === STATE_SETTINGS) return;
    if (startCooldown > 0) return;
    startCooldown = 3;

    if (appState === STATE_TRANSITION) {
      stopTransition();
    }
    if (blockFade.active) {
      stopBlockFade();
      currentBlock = blockFade.nextBlock;
    }
    if (block2.cancelReveal) block2.cancelReveal(anim);
    let next = (currentBlock + 1) % blocks.length;

    if (currentBlock === 0) {
      blockFade.active = true;
      blockFade.opa = 1.0;
      blockFade.nextBlock = next;
      setMainRate(RATE_FAST);
      blockFade.tweenId = anim.tween(0.0, 1.0, 200, "easeInCubic", function(v) {
        blockFade.opa = 1.0 - v;
        requestDraw();
      }, function() {
        currentBlock = blockFade.nextBlock;
        blockFade.opa = 1.0;
        blockFade.active = false;
        blockFade.tweenId = null;
        block2.resetCursor();
        setMainRate(RATE_NORMAL);
        block2.startReveal(anim, function() {
          requestDraw();
        });
        requestDraw();
      });
    } else {
      currentBlock = next;
      setMainRate(RATE_NORMAL);
      requestDraw();
      updateLed();
    }
  } catch (e) {
    print("weather: start handler error", e);
  }
});

input.on("ok", "short", function() {
  try {
    if (appState === STATE_SETTINGS) return;

    if (appState === STATE_TRANSITION) {
      stopTransition();
    }
    if (blockFade.active) {
      stopBlockFade();
      currentBlock = blockFade.nextBlock;
    }
    if (block2.cancelReveal) block2.cancelReveal(anim);

    appState = STATE_SETTINGS;
    display.clear();
    display.show();
    let names = [];
    for (let i = 0; i < sys.cities.length; i++) names.push(sys.cities[i].name);
    settings.show([
      { label: "City", options: names, value: sys.config.city }
    ], function(values) {
      appState = STATE_BLOCK;
      currentBlock = 0;
      let newCity = values[0];
      if (newCity !== sys.config.city) {
        sys.config.city = newCity;
        sys.saveConfig();
        sys.currentTemp = null;
        sys.currentCode = null;
        sys.hourlyTemps = [];
        sys.dailyForecast = [];
        sys.dataStale = false;
        sys.tzOffsetSec = sys.cities[newCity - 1].tz * 3600;
        appState = STATE_LOADING;
        transition.wasLoading = true;
        sys.fetchWeather();
      }
      requestDraw();
    });
  } catch (e) {
    print("weather: ok handler error", e);
    appState = STATE_BLOCK;
  }
});

input.on("up", "press", function() {
  try {
    if (appState !== STATE_BLOCK) return;
    let b = blocks[currentBlock];
    if (b.onUp) {
      let scrolled = b.onUp();
      if (scrolled) {
        requestDraw();
      }
    }
  } catch (e) {
    print("weather: up handler error", e);
  }
});

input.on("down", "press", function() {
  try {
    if (appState !== STATE_BLOCK) return;
    let b = blocks[currentBlock];
    if (b.onDown) {
      let scrolled = b.onDown();
      if (scrolled) {
        requestDraw();
      }
    }
  } catch (e) {
    print("weather: down handler error", e);
  }
});

__runLoop();
