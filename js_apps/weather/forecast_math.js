function sampleAtHour(points, absHour) {
  if (!points || points.length === 0) return null;

  let beforeIdx = -1;
  for (let i = 0; i < points.length; i++) {
    if (points[i].timepoint <= absHour) beforeIdx = i;
  }

  if (beforeIdx < 0) {
    return { temp: points[0].temp, code: points[0].code };
  }

  if (beforeIdx >= points.length - 1) {
    let last = points[points.length - 1];
    return { temp: last.temp, code: last.code };
  }

  let left = points[beforeIdx];
  let right = points[beforeIdx + 1];
  let span = right.timepoint - left.timepoint;
  if (span <= 0) return { temp: left.temp, code: left.code };

  let frac = (absHour - left.timepoint) / span;
  return {
    temp: left.temp * (1 - frac) + right.temp * frac,
    code: frac < 0.5 ? left.code : right.code,
  };
}

let forecastMath = {
  sampleAtHour: sampleAtHour,
};

forecastMath;
