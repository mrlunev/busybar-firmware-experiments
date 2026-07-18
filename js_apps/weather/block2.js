let sys = require("./system");
let display = require("display");
let time = require("time");
let forecastMath = require("./forecast_math");

let cursorHour = 0;
let NUM_HOURS = 24;

let cachedWindow = null;
let cachedWindowKey = "";
let revealTweenId = null;
let GRAPH_PAD = 1 / 9;

function buildWindowKey(startHour, pts) {
  let lastHour = pts.length > 0 ? pts[pts.length - 1].timepoint : -1;
  return str(sys.initUnix) + "|" + str(startHour) + "|" + str(pts.length) + "|" + str(lastHour);
}

function getWindow() {
  let pts = sys.hourlyTemps;
  if (!pts || pts.length < 2 || sys.initUnix === 0) {
    cachedWindow = null;
    cachedWindowKey = "";
    return null;
  }

  let nowSec = time.now();
  let elapsed = (nowSec - sys.initUnix) / 3600;

  let startHour = Math.ceil(elapsed);
  let endHour = startHour + NUM_HOURS;
  let nextKey = buildWindowKey(startHour, pts);

  if (cachedWindow !== null && cachedWindowKey === nextKey) {
    return cachedWindow;
  }

  let filtered = [];
  for (let i = 0; i < pts.length; i++) {
    let tp = pts[i].timepoint;
    if (tp < startHour) continue;
    if (tp > endHour) break;
    filtered.push(pts[i]);
  }

  if (filtered.length < 2) {
    cachedWindow = null;
    cachedWindowKey = nextKey;
    return null;
  }

  let minT = filtered[0].temp;
  let maxT = filtered[0].temp;
  for (let i = 1; i < filtered.length; i++) {
    if (filtered[i].temp < minT) minT = filtered[i].temp;
    if (filtered[i].temp > maxT) maxT = filtered[i].temp;
  }
  let range = maxT - minT;
  if (range < 1) range = 1;

  let values = [];
  for (let i = 0; i < filtered.length; i++) {
    values.push(GRAPH_PAD + (filtered[i].temp - minT) / range * (1 - GRAPH_PAD * 2));
  }

  cachedWindow = {
    values: values,
    pts: filtered,
    startHour: startHour,
    minT: minT,
    maxT: maxT,
  };
  cachedWindowKey = nextKey;
  return cachedWindow;
}

function cursorToPixel() {
  return Math.round(cursorHour * 71 / NUM_HOURS);
}

function interpAtHour(absHour) {
  let w = cachedWindow;
  if (!w || w.pts.length < 2) return null;
  return forecastMath.sampleAtHour(w.pts, absHour);
}

let graphReveal = 1.0;

function drawFront(yOffset, opa) {
  if (opa === undefined) opa = 1.0;
  let data = getWindow();
  let cursorData = null;
  let timeStr = "--:--";
  let tempStr = "--";
  let code = "";

  if (data) {
    let absHour = data.startHour + cursorHour;
    cursorData = interpAtHour(absHour);
    let cursorUnix = sys.initUnix + absHour * 3600;
    let t = sys.unixToLocal(cursorUnix);
    timeStr = sys.months[t.month - 1] + " " + str(t.day) + ", " + sys.pad2(t.hour) + ":00";
    if (cursorData) {
      tempStr = sys.formatTempShort(cursorData.temp);
      code = cursorData.code;
    }
  }

  display.text(timeStr, {
    font: "small",
    x: 1,
    y: -1 + yOffset,
    align: "left",
    color: sys.COLOR_DATE,
    opacity: opa,
  });

  let iconFile = sys.conditionIcons[code] || "ic_clear.png";
  let tempW = sys.estWidth(tempStr);
  let iconX = 72 - tempW - 9;
  display.image(iconFile, { x: iconX, y: 0 + yOffset, opacity: opa });

  display.text(tempStr, {
    font: "small",
    x: -1,
    y: -1 + yOffset,
    align: "right",
    color: sys.COLOR_WHITE,
    opacity: opa,
  });

  if (data) {
    display.graph(data.values, {
      x: 0,
      y: 7 + yOffset,
      width: 72,
      height: 9,
      tempMin: data.minT,
      tempMax: data.maxT,
      cursorX: cursorToPixel(),
      cursorColor: "#ffffff",
      minCurveY: 8,
      z: 0,
      revealX: graphReveal * 72,
      revealFade: 5,
      revealSlideY: 9.0 * (1.0 - graphReveal),
    });
  }
}

let block2 = {
  drawFront: drawFront,
  update: function() {
    let prevKey = cachedWindowKey;
    let hadWindow = cachedWindow !== null;
    getWindow();
    return prevKey !== cachedWindowKey || hadWindow !== (cachedWindow !== null);
  },
  resetCursor: function() {
    cursorHour = 0;
  },
  startReveal: function(animMod, onDone) {
    if (revealTweenId !== null && animMod.cancel) {
      animMod.cancel(revealTweenId);
      revealTweenId = null;
    }
    graphReveal = 0.0;
    revealTweenId = animMod.tween(0.0, 1.0, 2000, "easeOutCubic", function(v) {
      graphReveal = v;
      if (sys.requestDraw) sys.requestDraw();
    }, function() {
      revealTweenId = null;
      if (onDone) onDone();
    });
  },
  cancelReveal: function(animMod) {
    if (revealTweenId !== null && animMod.cancel) {
      animMod.cancel(revealTweenId);
      revealTweenId = null;
    }
    graphReveal = 1.0;
  },
  cursorTempNum: function() {
    let w = cachedWindow;
    if (!w) return null;
    let d = interpAtHour(w.startHour + cursorHour);
    if (!d) return null;
    return d.temp;
  },
  onUp: function(doneCb) {
    if (cursorHour >= NUM_HOURS) return false;
    cursorHour = cursorHour + 1;
    return true;
  },
  onDown: function(doneCb) {
    if (cursorHour <= 0) return false;
    cursorHour = cursorHour - 1;
    return true;
  },
};

block2;
