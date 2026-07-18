let fetch = require("fetch");
let json = require("json");
let configModule = require("config");
let timer = require("timer");
let time = require("time");
let status = require("status");
let forecastMath = require("./forecast_math");

let sys = {
  COLOR_GREEN: "#5DF230",
  COLOR_BG_DARK: "#1D1D1D",
  COLOR_WHITE: "#ffffff",
  COLOR_GRAY: "#808080",
  COLOR_DATE: "#868686",
  COLOR_CONDITION: "#00ff88",

  TEMP_UNIT: "C",

  cities: [
    { name: "Amsterdam", lat: 52.37, lon: 4.90, tz: 1, tzName: "Europe/Amsterdam" },
    { name: "Chicago", lat: 41.88, lon: -87.63, tz: -6, tzName: "America/Chicago" },
    { name: "London", lat: 51.51, lon: -0.13, tz: 0, tzName: "Europe/London" },
    { name: "New York", lat: 40.71, lon: -74.01, tz: -5, tzName: "America/New_York" },
    { name: "Paris", lat: 48.86, lon: 2.35, tz: 1, tzName: "Europe/Paris" },
    { name: "Toronto", lat: 43.65, lon: -79.38, tz: -5, tzName: "America/Toronto" },
  ],

  config: { city: 1 },
  currentTemp: null,
  currentCode: null,
  forecast: [],
  hourlyTemps: [],
  dailyForecast: [],
  initHour: 0,
  initUnix: 0,
  loading: true,
  noInternet: false,
  dataStale: false,
  retryTimer: null,
  retryDelaySec: 5,
  fetchGen: 0,
  failCount: 0,
  tzOffsetSec: 0,

  requestDraw: null,
  onFirstData: null,

  weekdays: ["", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"],
  months: [
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
  ],

  conditionNames: {
    clear: "Clear",
    pcloudy: "Partly cloudy",
    mcloudy: "Mostly cloudy",
    cloudy: "Cloudy",
    humid: "Humid",
    foggy: "Foggy",
    lightrain: "Light rain",
    oshower: "Showers",
    ishower: "Showers",
    rain: "Rain",
    lightsnow: "Light snow",
    snow: "Snow",
    rainsnow: "Rain + snow",
    ts: "Thunderstorm",
    tsrain: "Storm + rain",
  },

  conditionShort: {
    clear: "CLR", pcloudy: "P.C", mcloudy: "CLD", cloudy: "CLD",
    humid: "HUM", foggy: "FOG", lightrain: "DRZ", oshower: "SHR",
    ishower: "SHR", rain: "RAN", lightsnow: "SNW", snow: "SNW",
    rainsnow: "R/S", ts: "STM", tsrain: "STM",
  },

  conditionAnims: {
    clear: "w_clear.anim",
    pcloudy: "w_pcloudy.anim",
    mcloudy: "w_mcloudy.anim",
    cloudy: "w_cloudy.anim",
    humid: "w_humid.anim",
    foggy: "w_foggy.anim",
    lightrain: "w_lightrain.anim",
    oshower: "w_oshower.anim",
    ishower: "w_ishower.anim",
    rain: "w_rain.anim",
    lightsnow: "w_rain.anim",
    snow: "w_rain.anim",
    rainsnow: "w_rain.anim",
    ts: "w_rain.anim",
    tsrain: "w_rain.anim",
  },

  conditionIcons: {
    clear: "ic_clear.png",
    pcloudy: "ic_pcloudy.png",
    mcloudy: "ic_mcloudy.png",
    cloudy: "ic_cloudy.png",
    humid: "ic_humid.png",
    foggy: "ic_foggy.png",
    lightrain: "ic_lightrain.png",
    oshower: "ic_oshower.png",
    ishower: "ic_ishower.png",
    rain: "ic_rain.png",
    lightsnow: "ic_snow.png",
    snow: "ic_snow.png",
    rainsnow: "ic_rainsnow.png",
    ts: "ic_rain.png",
    tsrain: "ic_rain.png",
  },

  wmoToCode: function(wmo) {
    if (wmo === 0) return "clear";
    if (wmo === 1) return "pcloudy";
    if (wmo === 2) return "mcloudy";
    if (wmo === 3) return "cloudy";
    if (wmo === 45 || wmo === 48) return "foggy";
    if (wmo === 51 || wmo === 53 || wmo === 56) return "lightrain";
    if (wmo === 55 || wmo === 57) return "rain";
    if (wmo === 61 || wmo === 66) return "lightrain";
    if (wmo === 63 || wmo === 65 || wmo === 67) return "rain";
    if (wmo === 71 || wmo === 77) return "lightsnow";
    if (wmo === 73) return "snow";
    if (wmo === 75) return "snow";
    if (wmo === 80) return "oshower";
    if (wmo === 81) return "ishower";
    if (wmo === 82) return "rain";
    if (wmo === 85) return "lightsnow";
    if (wmo === 86) return "snow";
    if (wmo === 95) return "ts";
    if (wmo === 96 || wmo === 99) return "tsrain";
    return "cloudy";
  },

  tempToColor: function(celsius) {
    if (celsius === null || celsius === undefined) return "#808080";
    let t = (celsius + 30) / 60;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    let stops = [0.0, 0.35, 0.50, 0.65, 0.80, 1.0];
    let cs = [[0x20,0x00,0x8A],[0x67,0xA8,0xF5],[0xCF,0xCF,0xCF],[0xFF,0xC8,0x00],[0xFF,0x88,0x19],[0xFF,0x00,0x00]];
    let seg = 4;
    for (let i = 0; i < 5; i++) {
      if (t < stops[i + 1]) { seg = i; break; }
    }
    let f = (t - stops[seg]) / (stops[seg + 1] - stops[seg]);
    let r = Math.floor(cs[seg][0] + f * (cs[seg+1][0] - cs[seg][0]));
    let g = Math.floor(cs[seg][1] + f * (cs[seg+1][1] - cs[seg][1]));
    let b = Math.floor(cs[seg][2] + f * (cs[seg+1][2] - cs[seg][2]));
    let d = "0123456789abcdef";
    return "#" + d[(r>>4)&0xF] + d[r&0xF] + d[(g>>4)&0xF] + d[g&0xF] + d[(b>>4)&0xF] + d[b&0xF];
  },

  localTime: function() {
    return time.localTime();
  },

  unixToLocal: function(utcSec) {
    let local = utcSec + sys.tzOffsetSec;

    let daySec = local % 86400;
    if (daySec < 0) daySec = daySec + 86400;
    let hour = Math.floor(daySec / 3600);
    let minute = Math.floor((daySec % 3600) / 60);
    let second = Math.floor(daySec % 60);

    let z = Math.floor(local / 86400) + 719468;
    let era = Math.floor(z / 146097);
    let doe = z - era * 146097;
    let yoe = Math.floor((doe - Math.floor(doe / 1460) + Math.floor(doe / 36524) - Math.floor(doe / 146096)) / 365);
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + Math.floor(yoe / 4) - Math.floor(yoe / 100));
    let mp = Math.floor((5 * doy + 2) / 153);
    let day = doy - Math.floor((153 * mp + 2) / 5) + 1;
    let month = mp + (mp < 10 ? 3 : -9);
    if (month <= 2) y = y + 1;

    let dse = Math.floor(local / 86400);
    let wd = ((dse + 3) % 7) + 1;
    if (wd <= 0) wd = wd + 7;

    return { year: y, month: month, day: day, hour: hour, minute: minute, second: second, weekday: wd };
  },

  cityTime: function() {
    return sys.unixToLocal(time.now());
  },

  pad2: function(n) {
    if (n < 10) return "0" + str(n);
    return str(n);
  },

  estWidth: function(text) {
    let w = 0;
    for (let i = 0; i < text.length; i++) {
      let c = text[i];
      if (c === " ") w += 2;
      else if (c === ":" || c === ",") w += 2;
      else if (c === "1") w += 3;
      else if (c === "M" || c === "W") w += 5;
      else w += 4;
    }
    return w;
  },

  formatTempShort: function(celsius) {
    if (celsius === null || celsius === undefined) return "--";
    let v;
    if (sys.TEMP_UNIT === "F") {
      v = Math.floor(celsius * 9 / 5 + 32 + 0.5);
    } else {
      v = Math.floor(celsius + 0.5);
    }
    let sign = v > 0 ? "+" : "";
    return sign + str(v);
  },

  formatTemp: function(celsius) {
    let s = sys.formatTempShort(celsius);
    if (s === "--") return s;
    let unit = sys.TEMP_UNIT === "F" ? "\u00B0F " : "\u00B0C ";
    return s + unit;
  },

  normCode: function(code) {
    if (code === null || code === undefined) return "";
    return str(code).toLowerCase();
  },

  getCondition: function(code) {
    let key = sys.normCode(code);
    return sys.conditionNames[key] || "Unknown";
  },

  dateToSeconds: function(y, m, d, h) {
    let ya = y;
    let ma = m;
    if (ma <= 2) { ya = ya - 1; ma = ma + 12; }
    let A = Math.floor(ya / 100);
    let B = 2 - A + Math.floor(A / 4);
    let jd = Math.floor(365.25 * (ya + 4716)) + Math.floor(30.6001 * (ma + 1)) + d + B - 1524;
    let unixDays = jd - 2440588;
    return unixDays * 86400 + h * 3600;
  },

  normalizeDate: function(value) {
    let raw = str(value);
    if (raw.length === 8) {
      return raw.slice(0, 4) + "-" + raw.slice(4, 6) + "-" + raw.slice(6, 8);
    }
    return raw;
  },

  buildUrl: function() {
    let c = sys.cities[sys.config.city - 1];
    return "http://api.open-meteo.com/v1/forecast"
      + "?latitude=" + str(c.lat)
      + "&longitude=" + str(c.lon)
      + "&current=temperature_2m,weather_code"
      + "&hourly=temperature_2m,weather_code"
      + "&forecast_days=8&timezone=" + c.tzName;
  },

  parseIsoTime: function(isoStr) {
    let y = Number(isoStr.slice(0, 4));
    let m = Number(isoStr.slice(5, 7));
    let d = Number(isoStr.slice(8, 10));
    let h = Number(isoStr.slice(11, 13));
    return sys.dateToSeconds(y, m, d, h);
  },

  numAfterKey: function(s, key, from) {
    let idx = s.indexOf(key, from || 0);
    if (idx < 0) return null;
    idx += key.length;
    let end = idx;
    while (end < s.length && s.charAt(end) !== ',' && s.charAt(end) !== '}' && s.charAt(end) !== ']') end++;
    let v = Number(s.slice(idx, end));
    if (v !== v) return null;
    return v;
  },

  extractNumArray: function(s, key, from, step) {
    let result = [];
    let ai = s.indexOf(key, from || 0);
    if (ai < 0) return result;
    ai += key.length;
    let ae = s.indexOf(']', ai);
    if (ae < 0) return result;
    let pos = ai;
    let idx = 0;
    while (pos < ae) {
      let ns = pos;
      while (ns < ae && s.charAt(ns) !== ',') ns++;
      if (idx % step === 0) {
        result.push(Number(s.slice(pos, ns)));
      }
      pos = ns + 1;
      idx++;
    }
    return result;
  },

  parseWeather: function(body) {
    try {
      let utcOff = sys.numAfterKey(body, '"utc_offset_seconds":', 0);
      if (utcOff === null) utcOff = 0;

      let curTemp = null;
      let curCode = null;
      try {
        let ci = body.indexOf(',"current":{');
        if (ci < 0) ci = body.indexOf('"current":{');
        if (ci >= 0) {
          curTemp = sys.numAfterKey(body, '"temperature_2m":', ci + 12);
          curCode = sys.numAfterKey(body, '"weather_code":', ci + 12);
        }
      } catch (ce) {
        print("weather: current parse error", ce);
      }

      let hi = body.indexOf('"hourly":{');
      if (hi < 0) {
        print("weather: no hourly in response");
        if (curTemp !== null) {
          return {
            hourly: [{timepoint: 0, temp: curTemp, code: sys.wmoToCode(curCode || 0)}],
            initHour: 0, initUnix: 0, utcOff: utcOff,
            curTemp: curTemp, curCode: sys.wmoToCode(curCode || 0),
          };
        }
        return null;
      }

      let initUnix = 0;
      let initHour = 0;
      try {
        let tKey = '"time":["';
        let tIdx = body.indexOf(tKey, hi);
        if (tIdx >= 0) {
          let ts = tIdx + tKey.length;
          let te = body.indexOf('"', ts);
          if (te >= 0) {
            let ft = body.slice(ts, te);
            initUnix = sys.parseIsoTime(ft) - utcOff;
            initHour = Number(ft.slice(11, 13));
          }
        }
      } catch (te) {
        print("weather: time parse error", te);
      }

      let temps = sys.extractNumArray(body, '"temperature_2m":[', hi, 3);
      let codes = sys.extractNumArray(body, '"weather_code":[', hi, 3);

      let hourly = [];
      for (let i = 0; i < temps.length; i++) {
        let t = temps[i];
        if (t !== t || t < -200 || t > 200) continue;
        let wmo = (i < codes.length && codes[i] === codes[i]) ? codes[i] : 0;
        hourly.push({
          timepoint: i * 3,
          temp: t,
          code: sys.wmoToCode(wmo),
        });
      }

      print("weather: " + str(hourly.length) + " pts, cur=" + str(curTemp));
      if (hourly.length === 0 && curTemp === null) return null;
      if (hourly.length === 0) {
        hourly.push({timepoint: 0, temp: curTemp, code: sys.wmoToCode(curCode || 0)});
      }

      return {
        hourly: hourly,
        initHour: initHour,
        initUnix: initUnix,
        utcOff: utcOff,
        curTemp: curTemp,
        curCode: curTemp !== null ? sys.wmoToCode(curCode || 0) : null,
      };
    } catch (e) {
      print("weather: parseWeather fatal", e);
      return null;
    }
  },

  buildDailyForecast: function() {
    sys.dailyForecast = [];
    let pts = sys.hourlyTemps;
    if (!pts || pts.length === 0 || sys.initUnix === 0) return;

    try {
      let nowSec = time.now();
      let localNow = nowSec + sys.tzOffsetSec;
      let localDaySec = localNow % 86400;
      if (localDaySec < 0) localDaySec = localDaySec + 86400;
      let todayStartUtc = nowSec - localDaySec;

      for (let d = 0; d < 7; d++) {
        let dayStartUtc = todayStartUtc + (d + 1) * 86400;
        let dayUnix6 = dayStartUtc + 6 * 3600;
        let dayUnix18 = dayStartUtc + 18 * 3600;
        let nightUnix18 = dayStartUtc + 18 * 3600;
        let nightUnix30 = dayStartUtc + 30 * 3600;

        let dayWSum = 0, dayWTotal = 0, dayCodes = {};
        let nightWSum = 0, nightWTotal = 0, nightCodes = {};

        for (let i = 0; i < pts.length; i++) {
          let ptUnix = sys.initUnix + pts[i].timepoint * 3600;
          if (ptUnix >= dayUnix6 && ptUnix < dayUnix18) {
            let h = (ptUnix - dayStartUtc) / 3600;
            let w = 1 - (h > 12 ? h - 12 : 12 - h) / 6;
            if (w < 0.1) w = 0.1;
            dayWSum = dayWSum + pts[i].temp * w;
            dayWTotal = dayWTotal + w;
            let dc = pts[i].code;
            dayCodes[dc] = (dayCodes[dc] || 0) + 1;
          }
          if (ptUnix >= nightUnix18 && ptUnix < nightUnix30) {
            let h = (ptUnix - dayStartUtc) / 3600;
            let distMid = h > 24 ? h - 24 : 24 - h;
            let w = 1 - distMid / 6;
            if (w < 0.1) w = 0.1;
            nightWSum = nightWSum + pts[i].temp * w;
            nightWTotal = nightWTotal + w;
            let nc = pts[i].code;
            nightCodes[nc] = (nightCodes[nc] || 0) + 1;
          }
        }

        let dayTemp = dayWTotal > 0 ? dayWSum / dayWTotal : null;
        let nightTemp = nightWTotal > 0 ? nightWSum / nightWTotal : null;

        let dayCode = "clear", nightCode = "clear";
        let bestDC = 0, bestNC = 0;
        let dKeys = Object.keys(dayCodes);
        for (let k = 0; k < dKeys.length; k++) {
          if (dayCodes[dKeys[k]] > bestDC) { bestDC = dayCodes[dKeys[k]]; dayCode = dKeys[k]; }
        }
        let nKeys = Object.keys(nightCodes);
        for (let k = 0; k < nKeys.length; k++) {
          if (nightCodes[nKeys[k]] > bestNC) { bestNC = nightCodes[nKeys[k]]; nightCode = nKeys[k]; }
        }

        let dt = sys.unixToLocal(dayStartUtc + 12 * 3600);
        sys.dailyForecast.push({
          weekday: sys.weekdays[dt.weekday],
          day: dt.day,
          month: dt.month,
          dayTemp: dayTemp,
          dayCode: dayCode,
          nightTemp: nightTemp,
          nightCode: nightCode,
        });

        if (sys.dailyForecast.length >= 7) break;
      }
    } catch (e) {
      print("weather: buildDailyForecast error", e);
    }
  },

  refreshCurrent: function() {
    let pts = sys.hourlyTemps;
    if (!pts || pts.length === 0 || sys.initUnix === 0) return false;
    let prevTemp = sys.currentTemp;
    let prevCode = sys.currentCode;
    let nowElapsed = (time.now() - sys.initUnix) / 3600;
    let sample = forecastMath.sampleAtHour(pts, nowElapsed);
    if (!sample) return false;

    sys.currentTemp = sample.temp;
    sys.currentCode = sample.code;

    if (prevCode !== sys.currentCode) return true;
    if (prevTemp === null || sys.currentTemp === null) return prevTemp !== sys.currentTemp;
    return Math.abs(prevTemp - sys.currentTemp) >= 0.25;
  },

  stopRetry: function() {
    if (sys.retryTimer !== null) {
      timer.cancel(sys.retryTimer);
      sys.retryTimer = null;
    }
    sys.retryDelaySec = 5;
  },

  startRetry: function() {
    sys.stopRetry();
    let delaySec = sys.retryDelaySec;
    sys.retryTimer = timer.once(delaySec, function() {
      sys.retryTimer = null;
      sys.retryDelaySec = Math.min(sys.retryDelaySec * 2, 30);
      sys.fetchWeather();
    });
  },

  handleFetchFailure: function() {
    if (sys.currentTemp === null) {
      sys.failCount = sys.failCount + 1;
      if (sys.failCount >= 3) {
        sys.noInternet = true;
      }
      sys.startRetry();
    } else {
      sys.dataStale = true;
    }
  },

  fetchWeather: function() {
    sys.stopRetry();
    sys.fetchGen++;
    let gen = sys.fetchGen;
    sys.loading = true;
    sys.noInternet = false;
    try { status.set("#0088ff"); } catch (e) {}
    if (sys.requestDraw) sys.requestDraw();

    try {
      fetch.request({ url: sys.buildUrl() }, function(response, err) {
        if (gen !== sys.fetchGen) return;
        sys.loading = false;

        if (err) {
          print("weather fetch error", err);
          sys.handleFetchFailure();
          if (sys.requestDraw) sys.requestDraw();
          return;
        }

        if (!response || !response.ok) {
          print("weather HTTP error", response ? response.status : 0);
          sys.handleFetchFailure();
          if (sys.requestDraw) sys.requestDraw();
          return;
        }

        let result = sys.parseWeather(response.body);
        if (result) {
          sys.hourlyTemps = result.hourly;
          sys.initHour = result.initHour;
          sys.initUnix = result.initUnix;
          sys.tzOffsetSec = result.utcOff;
          sys.dataStale = false;
          sys.failCount = 0;
          sys.retryDelaySec = 5;
          let wasNull = sys.currentTemp === null;
          if (result.curTemp !== null) {
            sys.currentTemp = result.curTemp;
            sys.currentCode = result.curCode || result.hourly[0].code;
          } else {
            sys.currentTemp = result.hourly[0].temp;
            sys.currentCode = result.hourly[0].code;
          }
          sys.refreshCurrent();
          sys.buildDailyForecast();
          print("weather: ok, " + str(result.hourly.length) + " pts");
          sys.stopRetry();
          if (wasNull && sys.onFirstData) {
            sys.onFirstData();
            return;
          }
        } else {
          print("weather parse: no valid data");
          sys.handleFetchFailure();
        }

        if (sys.requestDraw) sys.requestDraw();
      });
    } catch (e) {
      print("weather: fetch.request error", e);
      sys.loading = false;
      sys.handleFetchFailure();
      if (sys.requestDraw) sys.requestDraw();
    }
  },

  loadConfig: function() {
    try {
      let cfg = configModule.load();
      if (cfg && cfg.city) {
        if (cfg.city >= 1 && cfg.city <= sys.cities.length) {
          sys.config.city = cfg.city;
        }
      }
    } catch (e) {
      print("weather: loadConfig error", e);
    }
    sys.tzOffsetSec = sys.cities[sys.config.city - 1].tz * 3600;
  },

  saveConfig: function() {
    try {
      configModule.set("city", sys.config.city);
    } catch (e) {}
  },
};

sys;
