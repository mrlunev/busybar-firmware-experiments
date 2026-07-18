let sys = require("./system");
let display = require("display");

let b1DateHH = "";
let b1DateMM = "";
let b1DatePart = "";
let b1WeekdayPart = "";
let b1DateStr = "";
let b1DateColonVisible = true;

let b1ShowDate = true;
let b1DateToggleTick = 0;

function b1UpdateDateTime() {
  let t = sys.cityTime();
  b1DateHH = sys.pad2(t.hour);
  b1DateMM = sys.pad2(t.minute);
  b1DatePart = str(t.day) + " " + sys.months[t.month - 1];
  b1WeekdayPart = sys.weekdays[t.weekday];
  b1DateStr = b1DateHH + ":" + b1DateMM + " " + b1DatePart + " " + b1WeekdayPart;
  b1DateColonVisible = (t.second % 2 === 0);
}

function b1UpdateDateToggle() {
  b1DateToggleTick++;
  if (b1DateToggleTick >= 30) {
    b1DateToggleTick = 0;
    b1ShowDate = !b1ShowDate;
  }
}

function drawFront(yOffset, opa) {
  if (opa === undefined) opa = 1.0;
  let code = sys.currentCode || "clear";
  let animFile = sys.conditionAnims[code] || "w_rain.anim";
  display.anim(animFile, { x: 0, y: yOffset, opacity: opa });

  let tempStr = sys.formatTemp(sys.currentTemp);
  display.text(tempStr, {
    font: "bold_7",
    x: 20,
    y: -1 + yOffset,
    align: "left",
    color: sys.COLOR_WHITE,
    opacity: opa,
  });

  let colonStr = b1DateColonVisible ? ":" : " ";
  let dayPart = b1ShowDate ? b1DatePart : b1WeekdayPart;
  let dateTimeStr = b1DateHH + colonStr + b1DateMM + ", " + dayPart;
  display.text(dateTimeStr, {
    font: "small",
    x: 20,
    y: 8 + yOffset,
    align: "left",
    color: sys.COLOR_DATE,
    opacity: opa,
  });
}

function drawFrontAnimated(yOffset, tr) {
  let code = sys.currentCode || "clear";
  let animFile = sys.conditionAnims[code] || "w_rain.anim";
  display.anim(animFile, { x: 0, y: yOffset, opacity: tr.animOpa, scale: tr.animScale });

  let tempStr = sys.formatTemp(sys.currentTemp);
  display.text(tempStr, {
    font: "bold_7",
    x: 20,
    y: -1 + yOffset,
    align: "left",
    color: sys.COLOR_WHITE,
    opacity: tr.textOpa,
  });

  let colonStr = b1DateColonVisible ? ":" : " ";
  let dayPart = b1ShowDate ? b1DatePart : b1WeekdayPart;
  let dateTimeStr = b1DateHH + colonStr + b1DateMM + ", " + dayPart;
  display.text(dateTimeStr, {
    font: "small",
    x: 20,
    y: 8 + yOffset,
    align: "left",
    color: sys.COLOR_DATE,
    opacity: tr.textOpa,
  });
}

let block1 = {
  drawFront: drawFront,
  drawFrontAnimated: drawFrontAnimated,
  update: function() {
    let prevDateStr = b1DateStr;
    let prevColon = b1DateColonVisible;
    let prevShowDate = b1ShowDate;
    b1UpdateDateTime();
    b1UpdateDateToggle();
    return prevDateStr !== b1DateStr || prevColon !== b1DateColonVisible || prevShowDate !== b1ShowDate;
  },
};

block1;
