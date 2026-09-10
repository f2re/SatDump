// Допускаются только HTTP-ресурсы текущего сервера, включая необязательные декоративные файлы.
export function localURL(value, base) {
  if (typeof value !== 'string' || !value.trim()) return null;
  try {
    const url = new URL(value, base);
    const root = new URL(base);
    if (!['http:', 'https:'].includes(url.protocol) || url.origin !== root.origin || url.username || url.password) return null;
    return url.href;
  } catch (_) { return null; }
}

export function normalizeManifest(data, base) {
  if (!data || !Array.isArray(data.frames)) throw new Error('Некорректный манифест');
  const seen = new Set();
  const frames = [];
  for (const raw of data.frames) {
    if (!raw || typeof raw.id !== 'string' || seen.has(raw.id)) continue;
    const display = localURL(raw.image?.url || raw.display || raw.urls?.imagery, base);
    const start = raw.start || raw.acquisition_start_utc;
    const end = raw.end || raw.acquisition_end_utc || raw.start || raw.acquisition_start_utc;
    const zoned = value => typeof value === 'string' && /T.*(?:Z|[+-]\d{2}:\d{2})$/i.test(value);
    const startMs = zoned(start) ? Date.parse(start) : NaN;
    const endMs = zoned(end) ? Date.parse(end) : NaN;
    const width = Number(raw.image?.width || raw.width);
    const height = Number(raw.image?.height || raw.height);
    // Только граница безопасности URL. Состав кадров и размеры определяет сервер.
    if (!display) continue;
    const timeKnown = Number.isFinite(startMs) && Number.isFinite(endMs) && endMs >= startMs;
    const product = typeof raw.product === 'object' ? raw.product?.code : raw.product;
    frames.push({ ...raw, raw, display, startMs, endMs, timeKnown, start: timeKnown ? new Date(startMs).toISOString() : null, end: timeKnown ? new Date(endMs).toISOString() : null,
      product: String(product || ''), title: raw.product?.title || raw.productTitle || '', description: raw.product?.description || '', purpose: raw.product?.purpose || '',
      family: raw.product?.family || raw.family || '', eventId: raw.eventId || `${raw.satellite}:${startMs}`,
      width, height, ambient: localURL(raw.ambient || raw.urls?.ambient, base),
      pass: raw.pass?.direction || raw.pass_direction || raw.pass || '',
      legend: { ...raw.legend, display: localURL(raw.urls?.legend || raw.legend?.display || raw.legend?.url, base) }
    });
    seen.add(raw.id);
  }
  if (data.frames.length && !frames.length) throw new Error('В манифесте нет допустимых кадров');
  return { generatedAt: data.generatedAt || null, asOf: data.asOf || data.sampleTime || data.referenceTime || null, display: data.display || null, undatedCount: data.undatedCount || 0, frames };
}

export class ManifestSource {
  constructor(url, onConfig = () => {}) { this.onConfig = onConfig; this.url = url; this.etag = null; this.controller = null; this.clockOffset = 0; }
  async refresh() {
    if (this.controller) return null;
    this.controller = new AbortController();
    const timeout = setTimeout(() => this.controller?.abort(), 15000);
    try {
      const response = await fetch(this.url, { cache: 'no-cache', credentials: 'same-origin', redirect: 'error',
        headers: this.etag ? { 'If-None-Match': this.etag } : {}, signal: this.controller.signal });
      const serverTime = Date.parse(response.headers.get('Date'));
      if (Number.isFinite(serverTime)) this.clockOffset = serverTime - Date.now();
      if (response.status === 304) return null;
      if (!response.ok) throw new Error(`Манифест: HTTP ${response.status}`);
      const normalized = normalizeManifest(await response.json(), this.url);
      this.onConfig(normalized.display);
      this.etag = response.headers.get('ETag');
      return normalized;
    } finally { clearTimeout(timeout); this.controller = null; }
  }
  stop() { this.controller?.abort(); }
}
