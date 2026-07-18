let timer = require("timer");

let easing = {
  linear: function (t) {
    return t;
  },
  easeInCubic: function (t) {
    return t * t * t;
  },
  easeOutCubic: function (t) {
    let u = t - 1;
    return u * u * u + 1;
  },
  easeInOutCubic: function (t) {
    return t < 0.5 ? 4 * t * t * t : (t - 1) * (2 * t - 2) * (2 * t - 2) + 1;
  },
  /** Quart ease in-out — для пикера FM (0→1 за duration). */
  easeInOutQuart: function (t) {
    return t < 0.5 ? 8 * t * t * t * t : 1 - Math.pow(-2 * t + 2, 4) / 2;
  },
  /** Expo ease-out (Robert Penner style). */
  easeOutExpo: function (t) {
    return t >= 1 ? 1 : 1 - Math.pow(2, -10 * t);
  },
  /** Elastic ease-out (easings.net / Penner). Коэффициент 0→1; может слегка >1 до финального кадра. */
  easeOutElastic: function (t) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    let c4 = (2 * Math.PI) / 3;
    return Math.pow(2, -10 * t) * Math.sin((t * 10 - 0.75) * c4) + 1;
  },
  easeOutQuint: function (t) {
    let u = t - 1;
    return u * u * u * u * u + 1;
  },
  easeInQuint: function (t) {
    return t * t * t * t * t;
  },
  easeOutQuad: function (t) {
    return t * (2 - t);
  },
  easeInQuad: function (t) {
    return t * t;
  },
};

/** Частота тика твина (мс); меньше — плавнее шаги при округлении x до пикселя. */
let TICK_MS = 16;

let tickSec = TICK_MS / 1000;
let nextTweenId = 1;
let schedulerId = null;
let tweens = [];

function stopScheduler() {
  if (schedulerId !== null && tweens.length === 0) {
    timer.cancel(schedulerId);
    schedulerId = null;
  }
}

function startScheduler() {
  if (schedulerId !== null) return;
  schedulerId = timer.every(tickSec, function () {
    if (tweens.length === 0) {
      stopScheduler();
      return;
    }

    let remaining = [];
    let completed = [];

    for (let i = 0; i < tweens.length; i++) {
      let tweenState = tweens[i];
      tweenState.elapsed = tweenState.elapsed + TICK_MS;
      let t = tweenState.elapsed / tweenState.durationMs;
      if (t >= 1) t = 1;
      let v = tweenState.from + (tweenState.to - tweenState.from) * tweenState.fn(t);
      tweenState.onUpdate(v, t);
      if (t >= 1) {
        completed.push(tweenState);
      } else {
        remaining.push(tweenState);
      }
    }

    tweens = remaining;
    stopScheduler();

    for (let i = 0; i < completed.length; i++) {
      if (completed[i].onDone) completed[i].onDone();
    }
  });
}

function tween(from, to, durationMs, easingName, onUpdate, onDone) {
  let tweenState = {
    id: nextTweenId++,
    from: from,
    to: to,
    durationMs: durationMs > 0 ? durationMs : TICK_MS,
    elapsed: 0,
    fn: easing[easingName] || easing.linear,
    onUpdate: onUpdate,
    onDone: onDone,
  };

  tweens.push(tweenState);
  startScheduler();
  return tweenState.id;
}

function cancel(id) {
  if (id === null || id === undefined) return false;
  let kept = [];
  let cancelled = false;

  for (let i = 0; i < tweens.length; i++) {
    if (tweens[i].id === id) {
      cancelled = true;
      continue;
    }
    kept.push(tweens[i]);
  }

  tweens = kept;
  stopScheduler();
  return cancelled;
}

let anim = {
  easing: easing,
  tween: tween,
  cancel: cancel,
  TICK_MS: TICK_MS,
};

anim;
