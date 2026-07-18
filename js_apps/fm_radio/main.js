let radio = require("radio");
let display = require("display");
let input = require("input");
let timer = require("timer");
let settings = require("settings");
let storage = require("storage");
let json = require("json");
let configModule = require("config");
let anim = require("./anim");

let COLOR_HI = "#ffffff";
let COLOR_LABEL = "#606060";
/** Название города на заднем экране (отдельно от префикса «City:»). */
let COLOR_CITY_NAME = "#ffffff";

/**
 * Задний экран 160×80 — координаты из Figma node 1853:58 (figma-bsb-firmware-design).
 * left = calc(50% − N) → x = 80 − N при ширине 160. Радиовышка: фрейм iconfm left 3, top 14.
 */
let BACK_SCREEN_W = 160;
let BACK_SYS_RESERVED_X = 14;
let BACK_LABEL_X = 3; // calc(50% - 77px): RADIO, CITY:, hint
let BACK_VALUE_X = 33; // calc(50% - 47px): STATION / CURRENT TRACK / значения
let BACK_VOL_LABEL_X = 44; // calc(50% - 36px): «VOLUME:»
let BACK_VOL_PCT_X = 78; // +2 px вправо от макета Figma
let BACK_CITY_NAME_X = 23; // calc(50% - 57px): город
let BACK_LINE_W = BACK_SCREEN_W - BACK_VALUE_X - BACK_SYS_RESERVED_X;
let BACK_SCROLL_MS = 4800;
let BACK_Y_HEADER = 1; // top-px
let BACK_Y_STATION_LABEL = 15;
let BACK_Y_STATION_VAL = 23;
let BACK_Y_TRACK_LABEL = 38;
let BACK_Y_TRACK_VAL = 46;
let BACK_Y_CITY = 62;
let BACK_Y_HINT = 70;
/** Иконка 8×8 перед «VOLUME:» — Figma 1862:194. */
let BACK_STATUS_SOUND_X = 33;
let BACK_STATUS_SOUND_Y = 2;
/** Радиовышка — Figma 1865:270 (контейнер 22×42 @ 3,14). */
let BACK_LEFT_ICON_X = 3;
let BACK_LEFT_ICON_Y = 14;

let PLAY_ICON_FRAMES = [
  "iconfm_play_01.png",
  "iconfm_play_02.png",
  "iconfm_play_03.png",
];
/** Кадры по кругу: 1→2→3→2→1→2→3→2→… (индексы 0,1,2 в массиве выше). */
let PLAY_ICON_PATTERN = [0, 1, 2, 1];
let PLAY_ICON_FRAME_S = 1.2;

/**
 * Винтажный фронт 72×16 — Figma node 1866:602 (figma-bsb-firmware-design).
 * Левая колонка Vol, правая «шкала» + MHz/FM.
 */
let FRONT_FM_PANEL_X = 20;
let FRONT_FM_PANEL_W = 52;
/** Шкала (fm_line4.png 51×2); пикер ездит по X от минимума до максимума, Y фиксирован. */
let FM_LINE_X = 20;
let FM_LINE_Y = 13;
let FM_PICKER_X_MIN = 21;
let FM_PICKER_X_MAX = 66;
let FM_PICKER_Y = 13;
/** Анимация пикера: easeOutExpo; траектория float → Math.round при отрисовке. */
let PICKER_TWEEN_MS = 200;
let pickerTweenId = null;
/** Текущая X в долях пикселя во время твина; вне твина не используется. */
let pickerTweenX = null;
/** fm_smallbox4.png — ширина колонки Vol (px); текст центрируем вручную (align center = середина экрана). */
let FRONT_VOL_BOX_W = 19;
/** Сдвиг подписи «Vol» вправо относительно центра в колонке. */
let FRONT_VOL_LABEL_DX = 1;
/** Сдвиг всего блока MHz+«FM» вправо относительно центра панели (подгонка к макету). */
let FM_MHZ_BLOCK_DX = 1;
/** Фронт: Vol/% и MHz — #FFFFFF; без фокуса 50%, в фокусе 100%. Подпись «FM» всегда 50%. */
let COLOR_FRONT_TEXT = "#ffffff";
let OPACITY_FRONT_DIM = 0.5;
let OPACITY_FRONT_FOCUS = 1.0;
/** Зазор между числом MHz и подписью «FM» (px). */
let GAP_MHZ_FM = 1;
/**
 * Ширина подписи «FM» в medium 7px: F adv_w 96, M adv_w 128 → 6+8 px (lv_font_busy_regular_7.c).
 */
let WIDTH_FM_LABEL = 14;

/** Базовая линия MHz + «FM»; fm_bigbox4 остаётся y=0, текст ниже фона на 1px. */
let FRONT_FM_ROW_Y = 1;

/**
 * Ширина строки в px для font "medium" (busy regular 7px), только [0–9.].
 * Совпадает с lv_font_busy_regular_7.c: для каждого глифа width = glyph_dsc[].adv_w / 16.
 */
function approxWidthMedium7(text) {
  let w = 0;
  for (let i = 0; i < text.length; i++) {
    let c = text.charAt(i);
    if (c === ".") {
      w += 4;
    } else if (c >= "0" && c <= "9") {
      w += 6;
    } else {
      w += 6;
    }
  }
  return w;
}

/**
 * Ширина глифа font "small" (busy_regular_5) в px: adv_w / 16 из lv_font_busy_regular_5.c.
 */
function charWidthSmall5(c) {
  let code = c.charCodeAt(0);
  if (code === 32) {
    return 2;
  }
  if (code >= 48 && code <= 57) {
    return code === 49 ? 2 : 4;
  }
  if (code === 37) {
    return 5;
  }
  if (code === 58 || code === 46) {
    return 4;
  }
  if (code === 86 || code === 118) {
    return 4;
  }
  if (code === 111 || code === 79) {
    return 4;
  }
  if (code === 108 || code === 76) {
    return 3;
  }
  return 4;
}

function approxWidthSmall5(text) {
  let w = 0;
  for (let i = 0; i < text.length; i++) {
    w += charWidthSmall5(text.charAt(i));
  }
  return w;
}

/** Левый X для строки в колонке Vol — центр по ширине FRONT_VOL_BOX_W, align left. */
function volColumnTextLeftX(line) {
  let inner = approxWidthSmall5(line);
  let pad = Math.floor((FRONT_VOL_BOX_W - inner) / 2);
  if (pad < 0) {
    pad = 0;
  }
  return pad;
}

/** MHz как число для позиции пикера и min/max по городу. */
function stationMhzNumber(st) {
  let v = parseFloat(st.mhz);
  if (isNaN(v)) {
    return 0;
  }
  return v;
}

/**
 * X пикера (целый пиксель): доля частоты текущей станции между min/max MHz города.
 * Диапазон [FM_PICKER_X_MIN .. FM_PICKER_X_MAX] = 100%; одна станция или все на одной частоте — центр шкалы.
 */
function fmPickerX() {
  let slot = citySlot();
  let list = CITIES[slot].stations;
  let n = list.length;
  let span = FM_PICKER_X_MAX - FM_PICKER_X_MIN;
  if (n === 0) {
    return FM_PICKER_X_MIN;
  }
  let fMin = stationMhzNumber(list[0]);
  let fMax = fMin;
  for (let i = 1; i < n; i++) {
    let f = stationMhzNumber(list[i]);
    if (f < fMin) {
      fMin = f;
    }
    if (f > fMax) {
      fMax = f;
    }
  }
  if (fMax <= fMin) {
    return FM_PICKER_X_MIN + Math.round(span / 2);
  }
  let fCur = stationMhzNumber(currentStation());
  let t = (fCur - fMin) / (fMax - fMin);
  if (t < 0) {
    t = 0;
  }
  if (t > 1) {
    t = 1;
  }
  let x = FM_PICKER_X_MIN + Math.round(t * span);
  if (x < FM_PICKER_X_MIN) {
    x = FM_PICKER_X_MIN;
  }
  if (x > FM_PICKER_X_MAX) {
    x = FM_PICKER_X_MAX;
  }
  return x;
}

/** X пикера для отрисовки (целый пиксель): во время твина — округлённое pickerTweenX, иначе fmPickerX(). */
function pickerDrawPixelX() {
  if (pickerTweenId !== null && pickerTweenX !== null) {
    return Math.round(pickerTweenX);
  }
  return fmPickerX();
}

/** Текущая позиция пикера по X (float) для старта следующего твина. */
function currentPickerAnimX() {
  if (pickerTweenId !== null && pickerTweenX !== null) {
    return pickerTweenX;
  }
  return fmPickerX();
}

function startPickerTween(fromX, toX) {
  if (pickerTweenId !== null) {
    anim.cancel(pickerTweenId);
  }
  pickerTweenId = null;
  pickerTweenX = null;
  if (Math.abs(toX - fromX) < 0.01) {
    return;
  }
  pickerTweenX = fromX;
  let tid = anim.tween(fromX, toX, PICKER_TWEEN_MS, "easeOutExpo", function (v) {
    pickerTweenX = v;
    render();
  }, function () {
    if (pickerTweenId === tid) {
      pickerTweenId = null;
      pickerTweenX = null;
      render();
    }
  });
  pickerTweenId = tid;
}

/** Левый край блока MHz+«FM» внутри правой панели (центрирование). */
function fmTextBlockLeftX(blockW) {
  let hi = FRONT_FM_PANEL_W - blockW;
  if (hi < 0) {
    return FRONT_FM_PANEL_X;
  }
  return FRONT_FM_PANEL_X + Math.floor(hi / 2);
}

/**
 * Станции: только mhz + url; название на заднем экране — icy-name из потока (radio.getStreamName).
 * В каждом городе массив отсортирован по возрастанию MHz: колесо «вверх» идёт по шкале слева направо,
 * с последней станции снова на первую (перелёт с max MHz на min).
 */
let CITIES = [
  {
    name: "Moscow",
    stations: [
      { mhz: "96.0", url: "http://emgregion.hostingradio.ru:8064/moscow.dorognoe.mp3" },
      { mhz: "101.2", url: "http://dfm.hostingradio.ru/dfm128.mp3" },
      { mhz: "101.8", url: "http://nashe1.hostingradio.ru/nashe-128.mp3" },
      { mhz: "103.7", url: "http://maximum.hostingradio.ru/maximum128.mp3" },
      { mhz: "104.3", url: "http://europaplus.hostingradio.ru:8014/retro320.mp3" },
      { mhz: "105.7", url: "http://rusradio.hostingradio.ru/rusradio128.mp3" },
      { mhz: "106.2", url: "http://ep128.hostingradio.ru:8030/ep128" },
    ],
  },
  {
    name: "London",
    stations: [
      { mhz: "95.8", url: "http://media-ice.musicradio.com/CapitalMP3" },
      { mhz: "96.7", url: "http://media-ice.musicradio.com/CapitalXTRALondonMP3" },
      { mhz: "97.3", url: "http://media-ice.musicradio.com/LBC973MP3Low" },
      { mhz: "102", url: "http://media-ice.musicradio.com/ClassicFMMP3" },
      { mhz: "102.2", url: "http://media-ice.musicradio.com/SmoothLondonMP3" },
      { mhz: "104.9", url: "http://media-ice.musicradio.com/RadioXUKMP3" },
      { mhz: "106.2", url: "http://media-ice.musicradio.com/HeartLondonMP3" },
    ],
  },
];

/** Шаг громкости колесом (фокус Vol). */
let VOLUME_STEP_PCT = 5;
/** Включить лог каждого тика poll (дорого на устройстве). */
let DEBUG_FM_POLL = false;
/** Задержка перед записью config на flash после смены громкости (сек). */
let VOLUME_SAVE_DEBOUNCE_S = 0.4;

let config = {
  city: 1,
  station: [0, 0],
  volume: 100,
};

let focus = "fm";
let state = "idle";
let pollId = null;
let pollCount = 0;
let POLL_TIMEOUT = 30;
let inSettings = false;
let trackTitle = "";
/** Кэш icy-name с потока; обновляется в poll/title poll. */
let streamNameCache = "";
let titlePollId = null;
let playIconAnimId = null;
let playIconFrame = 0;
let playIconPatternIdx = 0;
let volumeSaveTimerId = null;

function citySlot() {
  return config.city - 1;
}

function currentStation() {
  let c = CITIES[citySlot()];
  let i = config.station[citySlot()];
  if (i < 0) i = 0;
  if (i >= c.stations.length) i = c.stations.length - 1;
  return c.stations[i];
}

function refreshStreamNameFromRadio() {
  try {
    let sn = radio.getStreamName();
    if (sn && sn !== streamNameCache) {
      streamNameCache = sn;
      return true;
    }
  } catch (e) {
    print("[FM] getStreamName error: " + str(e));
  }
  return false;
}

/** Строка под STATION: на заднем экране. */
function stationBackLabel() {
  let st = currentStation();
  let mhzPart = " (" + st.mhz + ")";
  if (state === "playing" || state === "connecting") {
    if (streamNameCache && streamNameCache.length > 0) {
      return streamNameCache + mhzPart;
    }
    if (state === "connecting") {
      return "..." + mhzPart;
    }
  }
  if (st.label && st.label.length > 0) {
    return st.label + mhzPart;
  }
  return "\u2014" + mhzPart;
}

function loadConfig() {
  try {
    let o = configModule.load();
    if (typeof o.city === "number" && o.city >= 1 && o.city <= CITIES.length) {
      config.city = o.city;
    }
    if (o.station && o.station.length === 2) {
      for (let s = 0; s < 2; s++) {
        let max = CITIES[s].stations.length - 1;
        let v = o.station[s];
        if (typeof v === "number" && v >= 0 && v <= max) {
          config.station[s] = v;
        }
      }
    }
    if (typeof o.volume === "number") {
      if (o.volume < 0) o.volume = 0;
      if (o.volume > 100) o.volume = 100;
      config.volume = o.volume;
    }
  } catch (e) {
    print("[FM] loadConfig: " + str(e));
  }
}

function saveConfig() {
  try {
    configModule.save(config);
  } catch (e) {
    print("[FM] saveConfig: " + str(e));
  }
}

/** Сохранить конфиг сразу; отменяет отложенную запись после смены громкости. */
function saveConfigImmediate() {
  if (volumeSaveTimerId !== null) {
    timer.cancel(volumeSaveTimerId);
    volumeSaveTimerId = null;
  }
  saveConfig();
}

function scheduleVolumeConfigSave() {
  if (volumeSaveTimerId !== null) {
    timer.cancel(volumeSaveTimerId);
  }
  volumeSaveTimerId = timer.once(VOLUME_SAVE_DEBOUNCE_S, function () {
    volumeSaveTimerId = null;
    saveConfig();
  });
}

function pauseFmTimersForSettings() {
  stopPolling();
  stopTitlePolling();
  stopPlayIconAnim();
  if (pickerTweenId !== null) {
    anim.cancel(pickerTweenId);
    pickerTweenId = null;
    pickerTweenX = null;
  }
}

function resumeFmTimersAfterSettings() {
  if (state === "connecting") {
    startPolling();
  } else if (state === "playing") {
    startTitlePolling();
    startPlayIconAnim();
  }
}

function applyVolume() {
  radio.setVolume(config.volume);
}

function stopPolling() {
  if (pollId !== null) {
    timer.cancel(pollId);
    pollId = null;
  }
  pollCount = 0;
}

function startPolling() {
  stopPolling();
  pollCount = 0;
  pollId = timer.every(0.5, function () {
    if (inSettings) return;
    pollCount = pollCount + 1;
    let isP = radio.isPlaying();
    if (DEBUG_FM_POLL) {
      print("[FM] poll #" + pollCount + ": isPlaying=" + isP + " state=" + state);
    }

    if (state === "connecting") {
      if (refreshStreamNameFromRadio()) {
        render();
      }
    }

    if (state === "connecting" && isP) {
      state = "playing";
      refreshStreamNameFromRadio();
      stopPolling();
      startTitlePolling();
      startPlayIconAnim();
      render();
      return;
    }

    if (state === "connecting" && pollCount >= POLL_TIMEOUT) {
      print("[FM] TIMEOUT: no audio");
      state = "error";
      radio.stop();
      stopPolling();
      stopPlayIconAnim();
      streamNameCache = "";
      render();
      return;
    }

    if (state === "playing" && !isP) {
      state = "idle";
      stopPolling();
      stopTitlePolling();
      stopPlayIconAnim();
      trackTitle = "";
      streamNameCache = "";
      render();
    }
  });
}

function stopTitlePolling() {
  if (titlePollId !== null) {
    timer.cancel(titlePollId);
    titlePollId = null;
  }
}

function stopPlayIconAnim() {
  if (playIconAnimId !== null) {
    timer.cancel(playIconAnimId);
    playIconAnimId = null;
  }
  playIconFrame = 0;
  playIconPatternIdx = 0;
}

function startPlayIconAnim() {
  stopPlayIconAnim();
  playIconPatternIdx = 0;
  playIconFrame = PLAY_ICON_PATTERN[0];
  playIconAnimId = timer.every(PLAY_ICON_FRAME_S, function () {
    if (inSettings || state !== "playing") return;
    playIconPatternIdx = (playIconPatternIdx + 1) % PLAY_ICON_PATTERN.length;
    playIconFrame = PLAY_ICON_PATTERN[playIconPatternIdx];
    render();
  });
}

function startTitlePolling() {
  stopTitlePolling();
  titlePollId = timer.every(3, function () {
    if (inSettings) return;
    if (state !== "playing") return;
    let needRender = false;
    try {
      let t = radio.getTitle();
      if (t && t !== trackTitle) {
        trackTitle = t;
        needRender = true;
      }
    } catch (e) {
      print("[FM] getTitle error: " + str(e));
    }
    if (refreshStreamNameFromRadio()) {
      needRender = true;
    }
    if (needRender) {
      render();
    }
  });
}

function render() {
  if (inSettings) return;

  display.clear();

  display.image("fm_smallbox4.png", {
    x: 0,
    y: 0,
    z: 0,
    display: "front",
    background: true,
  });
  display.image("fm_bigbox4.png", {
    x: FRONT_FM_PANEL_X,
    y: 0,
    z: 0,
    display: "front",
  });
  display.image("fm_line4.png", {
    x: FM_LINE_X,
    y: FM_LINE_Y,
    z: 1,
    display: "front",
  });

  let volFrontOpacity = focus === "vol" ? OPACITY_FRONT_FOCUS : OPACITY_FRONT_DIM;
  let fmFrontOpacity = focus === "fm" ? OPACITY_FRONT_FOCUS : OPACITY_FRONT_DIM;

  let volPctLine = str(config.volume) + "%";
  display.text("Vol", {
    x: volColumnTextLeftX("Vol") + FRONT_VOL_LABEL_DX,
    y: 0,
    align: "left",
    font: "small",
    color: COLOR_FRONT_TEXT,
    opacity: volFrontOpacity,
    display: "front",
    z: 2,
  });
  display.text(volPctLine, {
    x: volColumnTextLeftX(volPctLine),
    y: 7,
    align: "left",
    font: "small",
    color: COLOR_FRONT_TEXT,
    opacity: volFrontOpacity,
    display: "front",
    z: 2,
  });

  let st = currentStation();
  let mhzStr = st.mhz;
  let mhzW = approxWidthMedium7(mhzStr);
  let blockW = mhzW + GAP_MHZ_FM + WIDTH_FM_LABEL;
  let mhzX = fmTextBlockLeftX(blockW);
  let panelEnd = FRONT_FM_PANEL_X + FRONT_FM_PANEL_W;
  if (mhzX + blockW > panelEnd) {
    mhzX = panelEnd - blockW;
  }
  if (mhzX < FRONT_FM_PANEL_X) {
    mhzX = FRONT_FM_PANEL_X;
  }
  mhzX = mhzX + FM_MHZ_BLOCK_DX;
  if (mhzX + blockW > panelEnd) {
    mhzX = panelEnd - blockW;
  }
  let fmLabelX = mhzX + mhzW + GAP_MHZ_FM;

  display.text(mhzStr, {
    x: mhzX,
    y: FRONT_FM_ROW_Y,
    align: "left",
    font: "medium",
    color: COLOR_FRONT_TEXT,
    opacity: fmFrontOpacity,
    display: "front",
    z: 2,
  });
  display.text("FM", {
    x: fmLabelX,
    y: FRONT_FM_ROW_Y,
    align: "left",
    font: "medium",
    color: COLOR_FRONT_TEXT,
    opacity: OPACITY_FRONT_DIM,
    display: "front",
    z: 2,
  });

    display.image("fm_picker_v2.png", {
    x: pickerDrawPixelX(),
    y: FM_PICKER_Y,
    z: 3,
    display: "front",
  });

  let stationLabel = stationBackLabel();

  let trackDisplay = "--";
  if (state === "connecting") trackDisplay = "connecting...";
  else if (state === "error") trackDisplay = "error";
  else if (state === "playing" && trackTitle) trackDisplay = trackTitle;
  else if (state === "playing") trackDisplay = "...";

  let cityName = CITIES[citySlot()].name;

  if (state === "playing") {
    display.image(PLAY_ICON_FRAMES[playIconFrame], {
      x: BACK_LEFT_ICON_X,
      y: BACK_LEFT_ICON_Y,
      display: "back",
      z: 0,
    });
  } else {
    display.image("iconfm_stopped.png", {
      x: BACK_LEFT_ICON_X,
      y: BACK_LEFT_ICON_Y,
      display: "back",
      z: 0,
    });
  }

  display.image("8px-status-sound.png", {
    x: BACK_STATUS_SOUND_X,
    y: BACK_STATUS_SOUND_Y,
    display: "back",
    z: 0,
  });

  display.text("RADIO", {
    x: BACK_LABEL_X,
    y: BACK_Y_HEADER,
    align: "left",
    font: "small",
    color: COLOR_HI,
    display: "back",
  });
  display.text("VOLUME: ", {
    x: BACK_VOL_LABEL_X,
    y: BACK_Y_HEADER,
    align: "left",
    font: "small",
    color: COLOR_LABEL,
    display: "back",
  });
  display.text(str(config.volume) + "%", {
    x: config.volume >= 100 ? BACK_VOL_PCT_X - 5 : BACK_VOL_PCT_X,
    y: BACK_Y_HEADER,
    align: "left",
    font: "small",
    color: COLOR_HI,
    display: "back",
  });
  display.text("STATION:", {
    x: BACK_VALUE_X,
    y: BACK_Y_STATION_LABEL,
    align: "left",
    font: "small",
    color: COLOR_LABEL,
    display: "back",
  });
  display.text(stationLabel, {
    x: BACK_VALUE_X,
    y: BACK_Y_STATION_VAL,
    align: "left",
    font: "condensed_7",
    color: COLOR_HI,
    display: "back",
    width: BACK_LINE_W,
    longMode: "scroll",
    scrollDurationMs: BACK_SCROLL_MS,
  });
  display.text("CURRENT TRACK:", {
    x: BACK_VALUE_X,
    y: BACK_Y_TRACK_LABEL,
    align: "left",
    font: "small",
    color: COLOR_LABEL,
    display: "back",
  });
  display.text(trackDisplay, {
    x: BACK_VALUE_X,
    y: BACK_Y_TRACK_VAL,
    align: "left",
    font: "condensed_7",
    color: COLOR_HI,
    display: "back",
    width: BACK_LINE_W,
    longMode: "scroll",
    scrollDurationMs: BACK_SCROLL_MS,
  });
  display.text("CITY: ", {
    x: BACK_LABEL_X,
    y: BACK_Y_CITY,
    align: "left",
    font: "small",
    color: COLOR_LABEL,
    display: "back",
  });
  display.text(cityName.toUpperCase(), {
    x: BACK_CITY_NAME_X,
    y: BACK_Y_CITY,
    align: "left",
    font: "small",
    color: COLOR_CITY_NAME,
    display: "back",
  });
  display.text("To change city: long press \"Ok\"", {
    x: BACK_LABEL_X,
    y: BACK_Y_HINT,
    align: "left",
    font: "small",
    color: COLOR_LABEL,
    display: "back",
  });

  display.show();
}

function doPlay() {
  let st = currentStation();
  print("[FM] doPlay url=" + st.url);
  applyVolume();
  trackTitle = "";
  streamNameCache = "";
  stopPlayIconAnim();
  state = "connecting";
  render();

  let ok = radio.play(st.url);
  print("[FM] radio.play()=" + ok);

  if (!ok) {
    state = "error";
    stopPlayIconAnim();
    streamNameCache = "";
    render();
    return;
  }

  startPolling();
}

function doStop() {
  state = "stopping";
  render();
  stopPolling();
  stopTitlePolling();
  stopPlayIconAnim();
  radio.stop();
  trackTitle = "";
  streamNameCache = "";
  state = "idle";
  render();
}

function restartStreamIfPlaying() {
  if (state === "playing" || state === "connecting") {
    stopPolling();
    stopTitlePolling();
    stopPlayIconAnim();
    trackTitle = "";
    streamNameCache = "";
    radio.stop();
    state = "connecting";
    render();
    applyVolume();
    let st = currentStation();
    let ok = radio.play(st.url);
    if (!ok) {
      state = "error";
      stopPlayIconAnim();
      streamNameCache = "";
      render();
      return;
    }
    startPolling();
  } else {
    render();
  }
}

loadConfig();
applyVolume();

print("[FM] === FM Radio started === city=" + config.city);

render();

input.on("start", "short", function () {
  if (inSettings) return;
  print("[FM] START state=" + state);
  if (state === "idle" || state === "error") {
    doPlay();
  } else if (state === "playing" || state === "connecting" || state === "stopping") {
    doStop();
  }
});

input.on("ok", "short", function () {
  if (inSettings) return;
  focus = focus === "fm" ? "vol" : "fm";
  print("[FM] focus=" + focus);
  render();
});

input.on("ok", "long", function () {
  if (inSettings) return;
  saveConfigImmediate();
  pauseFmTimersForSettings();
  inSettings = true;
  display.clear();
  display.show();
  let names = [];
  for (let i = 0; i < CITIES.length; i++) names.push(CITIES[i].name);
  settings.show([
    { label: "City", options: names, value: config.city }
  ], function (values) {
    inSettings = false;
    resumeFmTimersAfterSettings();
    let newCity = values[0];
    if (newCity !== config.city) {
      let fromX = currentPickerAnimX();
      config.city = newCity;
      saveConfigImmediate();
      startPickerTween(fromX, fmPickerX());
      restartStreamIfPlaying();
    } else {
      render();
    }
  });
});

input.on("up", "short", function () {
  if (inSettings) return;
  let slot = citySlot();
  if (focus === "fm") {
    let fromX = currentPickerAnimX();
    let n = CITIES[slot].stations.length;
    config.station[slot] = (config.station[slot] + 1) % n;
    saveConfigImmediate();
    startPickerTween(fromX, fmPickerX());
    restartStreamIfPlaying();
  } else {
    if (config.volume < 100) {
      config.volume = config.volume + VOLUME_STEP_PCT;
      if (config.volume > 100) config.volume = 100;
      applyVolume();
      scheduleVolumeConfigSave();
    }
    render();
  }
});

input.on("down", "short", function () {
  if (inSettings) return;
  let slot = citySlot();
  if (focus === "fm") {
    let fromX = currentPickerAnimX();
    let n = CITIES[slot].stations.length;
    config.station[slot] = (config.station[slot] + n - 1) % n;
    saveConfigImmediate();
    startPickerTween(fromX, fmPickerX());
    restartStreamIfPlaying();
  } else {
    if (config.volume > 0) {
      config.volume = config.volume - VOLUME_STEP_PCT;
      if (config.volume < 0) config.volume = 0;
      applyVolume();
      scheduleVolumeConfigSave();
    }
    render();
  }
});

__runLoop();
