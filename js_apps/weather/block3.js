let sys = require("./system");
let display = require("display");

let cursorDay = 0;
let NUM_DAYS = 7;

function textAt(text, x, floatY, color, opaVal) {
  let yi = Math.round(floatY);
  display.text(text, { font: "small", x: x, y: yi, align: "left", color: color, opacity: opaVal });
}

function drawFront(yOffset, opa) {
  if (opa === undefined) opa = 1.0;
  let fc = sys.dailyForecast;
  if (!fc || fc.length === 0) {
    display.text("No forecast", {
      font: "small",
      align: "center",
      y: 4 + yOffset,
      color: sys.COLOR_GRAY,
      opacity: opa,
    });
    return;
  }

  if (cursorDay >= fc.length) cursorDay = fc.length - 1;

  let centerY = 4 + yOffset;

  for (let i = -1; i <= 1; i++) {
    let dayIdx = cursorDay + i;
    if (dayIdx < 0 || dayIdx >= fc.length) continue;
    let item = fc[dayIdx];
    let dateStr = item.weekday + " " + str(item.day);
    let y = centerY + i * 8;
    if (y < -8 || y > 20) continue;
    let color = (i === 0) ? sys.COLOR_WHITE : sys.COLOR_GRAY;
    textAt(dateStr, 0, y, color, opa);
  }

  let cur = fc[cursorDay];
  let dayX = 29;
  let nightX = 51;

  display.image("ic_day.png", { x: dayX, y: 2 + yOffset, opacity: opa });

  let dayIconFile = sys.conditionIcons[cur.dayCode] || "ic_clear.png";
  display.image(dayIconFile, { x: dayX, y: 7 + yOffset, opacity: opa });

  display.text(sys.formatTempShort(cur.dayTemp), {
    font: "small",
    x: dayX + 8,
    y: 6 + yOffset,
    align: "left",
    color: sys.COLOR_WHITE,
    opacity: opa,
  });

  display.image("ic_night.png", { x: nightX, y: 2 + yOffset, opacity: opa });

  let nightIconFile = sys.conditionIcons[cur.nightCode] || "ic_clear.png";
  display.image(nightIconFile, { x: nightX, y: 7 + yOffset, opacity: opa });

  display.text(sys.formatTempShort(cur.nightTemp), {
    font: "small",
    x: nightX + 8,
    y: 6 + yOffset,
    align: "left",
    color: sys.COLOR_WHITE,
    opacity: opa,
  });
}

let block3 = {
  drawFront: drawFront,
  update: function() {
    let fc = sys.dailyForecast;
    if (!fc || fc.length === 0) {
      if (cursorDay !== 0) {
        cursorDay = 0;
        return true;
      }
      return false;
    }
    if (cursorDay >= fc.length) {
      cursorDay = fc.length - 1;
      return true;
    }
    return false;
  },
  onUp: function(doneCb) {
    let maxDay = sys.dailyForecast.length - 1;
    if (cursorDay >= maxDay) return false;
    cursorDay = cursorDay + 1;
    return true;
  },
  onDown: function(doneCb) {
    if (cursorDay <= 0) return false;
    cursorDay = cursorDay - 1;
    return true;
  },
};

block3;
