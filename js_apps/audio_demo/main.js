let audio = require("audio");
let radio = require("radio");
let display = require("display");
let input = require("input");
let fs = require("fs_extra");

let volume = audio.getVolume();
let appDir = __dirname;
let tracks = [];
let currentTrack = 0;
let playing = false;

function scanTracks() {
  let entries = fs.readdir();
  let found = [];
  for (let i = 0; i < entries.length; i++) {
    let name = entries[i].name;
    let lower = name.toLowerCase();
    let isAudio = lower.lastIndexOf(".mp3") === lower.length - 4
      || lower.lastIndexOf(".snd") === lower.length - 4;
    if (!entries[i].isDirectory && isAudio) {
      found.push(name);
    }
  }
  return found;
}

function isMp3(name) {
  let lower = name.toLowerCase();
  return lower.lastIndexOf(".mp3") === lower.length - 4;
}

function displayName(filename) {
  let dot = filename.lastIndexOf(".");
  if (dot > 0) return filename.substring(0, dot);
  return filename;
}

function clamp(value, min, max) {
  if (value < min) return min;
  if (value > max) return max;
  return value;
}

function stopCurrent() {
  audio.stop();
  radio.stop();
  playing = false;
}

function playCurrent() {
  if (tracks.length === 0) return false;
  let name = tracks[currentTrack];
  let ok;
  if (isMp3(name)) {
    radio.setVolume(volume);
    ok = radio.playFile(appDir + "/" + name);
  } else {
    ok = audio.play(name);
  }
  return ok;
}

function render(message) {
  display.clear();
  display.text("Music Player", {
    x: 2, y: 0, align: "left", font: "small", color: "#ffffff",
  });

  if (tracks.length > 0) {
    display.text(displayName(tracks[currentTrack]), {
      x: 2, y: 5, align: "left", font: "small", color: "#00ff88",
    });
    display.text("vol " + str(Math.floor(volume * 100)) + "%", {
      x: 50, y: 5, align: "left", font: "small", color: "#808080",
    });
  }

  display.text(message, {
    x: 2, y: 10, align: "left", font: "small", color: "#ffaa00",
  });
  display.show();
}

tracks = scanTracks();

if (tracks.length === 0) {
  render("no tracks found");
} else {
  render(str(tracks.length) + " tracks / OK play");
}

input.on("ok", "short", function () {
  if (tracks.length === 0) return;
  if (playing) {
    stopCurrent();
    render("stopped");
  } else {
    let ok = playCurrent();
    playing = ok;
    render(ok ? "playing" : "play failed");
  }
});

input.on("start", "short", function () {
  if (tracks.length === 0) return;
  stopCurrent();
  currentTrack = (currentTrack + 1) % tracks.length;
  render("track: " + displayName(tracks[currentTrack]));
});

input.on("up", "short", function () {
  volume = clamp(volume + 0.1, 0, 1);
  audio.setVolume(volume);
  radio.setVolume(volume);
  render("vol " + str(Math.floor(volume * 100)) + "%");
});

input.on("down", "short", function () {
  volume = clamp(volume - 0.1, 0, 1);
  audio.setVolume(volume);
  radio.setVolume(volume);
  render("vol " + str(Math.floor(volume * 100)) + "%");
});

__runLoop();
