let timer = require("timer");

let easing = {
  linear: function(t) { return t; },
  easeInCubic: function(t) { return t * t * t; },
  easeOutCubic: function(t) { let u = t - 1; return u * u * u + 1; },
  easeInOutCubic: function(t) {
    return t < 0.5 ? 4 * t * t * t : (t - 1) * (2 * t - 2) * (2 * t - 2) + 1;
  },
  easeOutQuint: function(t) { let u = t - 1; return u * u * u * u * u + 1; },
  easeInQuint: function(t) { return t * t * t * t * t; },
  easeOutQuad: function(t) { return t * (2 - t); },
  easeInQuad: function(t) { return t * t; },
};

let TICK_MS = 35;
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

function runTick() {
  schedulerId = null;
  if (tweens.length === 0) return;

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

  for (let i = 0; i < completed.length; i++) {
    if (completed[i].onDone) completed[i].onDone();
  }

  if (tweens.length > 0) startScheduler();
}

function startScheduler() {
  if (schedulerId !== null || tweens.length === 0) return;
  schedulerId = timer.once(tickSec, runTick);
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

function sequence(steps) {
  let idx = 0;
  function next() {
    if (idx >= steps.length) return;
    let step = steps[idx];
    idx = idx + 1;
    step(next);
  }
  next();
}

let anim = {
  easing: easing,
  tween: tween,
  cancel: cancel,
  sequence: sequence,
  TICK_MS: TICK_MS,
};

anim;
