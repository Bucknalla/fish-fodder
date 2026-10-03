// Today's forecast from Open-Meteo (free, no API key), for wherever the
// device is. Location comes from, in order: --coords, --location (a place
// name, looked up with Open-Meteo's geocoder), or the network's IP address.
// Plain fetch(), so the simulator uses it in the browser too.

import { weatherKind } from './weather.js';

const IP_URL = 'https://ipinfo.io/json';
const GEOCODE_URL = 'https://geocoding-api.open-meteo.com/v1/search';
const FORECAST_URL = 'https://api.open-meteo.com/v1/forecast';

const pad = (n) => String(n).padStart(2, '0');
export const localDate = (d) => `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;

export function createWeather({
  location,
  coords, // [lat, lon]
  units = 'celsius', // or 'fahrenheit'
  fetch = globalThis.fetch,
  log = () => {},
  maxAgeMs = 55 * 60_000, // refetch at most hourly
  timeoutMs = 10_000,
} = {}) {
  let place = null;
  const cache = new Map(); // date → { value, at }
  const inflight = new Map();
  let lastError = null;

  async function getJSON(url) {
    const res = await fetch(url, { signal: AbortSignal.timeout(timeoutMs) });
    const body = await res.json().catch(() => ({}));
    if (!res.ok) throw new Error(body.reason ?? `HTTP ${res.status} from ${new URL(url).host}`);
    return body;
  }

  async function findPlace() {
    if (place) return place;
    if (coords) {
      const [lat, lon] = coords;
      place = { lat, lon, name: `${lat}, ${lon}`, source: 'coordinates' };
    } else if (location) {
      // "Bristol, GB" → name Bristol in country GB.
      const [name, country] = location.split(',').map((s) => s.trim());
      const q = new URLSearchParams({ name, count: '1', format: 'json' });
      if (/^[A-Za-z]{2}$/.test(country ?? '')) q.set('countryCode', country.toUpperCase());
      const hit = (await getJSON(`${GEOCODE_URL}?${q}`)).results?.[0];
      if (!hit) throw new Error(`couldn't find a place called "${location}"`);
      place = { lat: hit.latitude, lon: hit.longitude, name: [hit.name, hit.country_code].filter(Boolean).join(', '), source: 'location' };
    } else {
      const ip = await getJSON(IP_URL);
      const [ilat, ilon] = String(ip.loc ?? '').split(',').map(Number);
      if (!Number.isFinite(ilat) || !Number.isFinite(ilon)) throw new Error("couldn't work out a location from the IP address");
      place = { lat: ilat, lon: ilon, name: [ip.city, ip.country].filter(Boolean).join(', ') || 'unknown', source: 'ip' };
    }
    const how = { ip: 'estimated from IP address; set --location to correct it', location: 'from --location', coordinates: 'from --coords' };
    log(`weather: ${place.name} (${how[place.source]})`);
    return place;
  }

  async function fetchForecast(date) {
    const p = await findPlace();
    const q = new URLSearchParams({
      latitude: String(p.lat),
      longitude: String(p.lon),
      daily: 'weather_code,temperature_2m_max,temperature_2m_min',
      timezone: 'auto',
      start_date: date,
      end_date: date,
    });
    if (units === 'fahrenheit') q.set('temperature_unit', 'fahrenheit');
    const { daily } = await getJSON(`${FORECAST_URL}?${q}`);
    const i = daily?.time?.indexOf(date) ?? -1;
    const code = daily?.weather_code?.[i];
    const high = daily?.temperature_2m_max?.[i];
    const low = daily?.temperature_2m_min?.[i];
    if (i < 0 || code == null || high == null || low == null) throw new Error(`no forecast for ${date}`);
    return { date, code, kind: weatherKind(code), high, low, units, place: p.name };
  }

  return {
    // The forecast for `when`'s local date: fetched if the cached one is
    // missing or stale; the stale one (or null) if fetching fails. Never throws.
    async forecast(when = new Date()) {
      const date = localDate(when);
      const hit = cache.get(date);
      if (hit && Date.now() - hit.at < maxAgeMs) return hit.value;
      if (!inflight.has(date)) {
        inflight.set(date, fetchForecast(date)
          .then((value) => {
            cache.set(date, { value, at: Date.now() });
            for (const d of cache.keys()) if (d < date) cache.delete(d);
            lastError = null;
            return value;
          })
          .catch((err) => {
            if (err.message !== lastError) log(`weather: ${err.message}`);
            lastError = err.message;
            return hit?.value ?? null;
          })
          .finally(() => inflight.delete(date)));
      }
      return inflight.get(date);
    },
    // Whatever we already have for `when`'s date, without fetching.
    cached(when = new Date()) {
      return cache.get(localDate(when))?.value ?? null;
    },
    place: () => place,
  };
}
