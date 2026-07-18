let display = require("display");
let timer = require("timer");
let time = require("time");
let configModule = require("config");

let settings = configModule.load();
let city1Name = settings.city_1_name || "London";
let city2Name = settings.city_2_name || "Moscow";
let city1Tz = typeof settings.city_1_tz === "string" ? settings.city_1_tz : "London";
let city2Tz = typeof settings.city_2_tz === "string" ? settings.city_2_tz : "Moscow";

let colonVisible = true;

function pad2(value) {
  if (value < 10) return "0" + str(value);
  return str(value);
}

function getTimeInTimezone(timestamp, timezone, fallback) {
  let local;
  try {
    local = time.inTimezone(timestamp, timezone);
  } catch (error) {
    print("world_clock: invalid timezone", timezone, error);
    local = time.inTimezone(timestamp, fallback);
  }
  return {
    hours: local.hour,
    minutes: local.minute,
  };
}

function drawClockColumn(title, titleColor, x, hourX, colonX, minuteX, value) {
  display.text(title, {
    x: x,
    y: -1,
    align: "left",
    font: "small",
    color: titleColor,
  });

  display.text(pad2(value.hours), {
    x: hourX,
    y: 6,
    align: "left",
    font: "medium",
    color: "#ffffff",
  });

  if (colonVisible) {
    display.text(":", {
      x: colonX,
      y: 6,
      align: "left",
      font: "medium",
      color: "#ffffff",
    });
  }

  display.text(pad2(value.minutes), {
    x: minuteX,
    y: 6,
    align: "left",
    font: "medium",
    color: "#ffffff",
  });
}

function render() {
  let now = time.now();
  let time1 = getTimeInTimezone(now, city1Tz, "London");
  let time2 = getTimeInTimezone(now, city2Tz, "Moscow");

  let label1 = city1Name.toUpperCase();
  let label2 = city2Name.toUpperCase();

  display.clear();
  drawClockColumn(label1, "#4487e0", 1, 1, 13, 15, time1);
  drawClockColumn(label2, "#f32424", 38, 38, 50, 52, time2);
  display.show();
}

render();

timer.every(1, function() {
  colonVisible = !colonVisible;
  render();
});

__runLoop();
