// Client for config/station/board-openapi.json. Credentials never leave this origin.
export class ApiError extends Error {
  constructor(status, data) { super(data.detail || data.error || `HTTP ${status}`); this.status = status; this.data = data; }
}
export class StationAPI {
  constructor() { this.token = ''; }
  async request(path, options = {}) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 15000);
    try {
      const response = await fetch(`/api/v1/control/${path}`, {
        ...options, cache: 'no-store', credentials: 'omit', redirect: 'error', signal: controller.signal,
        headers: { Authorization: `Bearer ${this.token}`, ...(options.body ? {'Content-Type': 'application/json'} : {}), ...options.headers }
      });
      const data = await response.json();
      if (!response.ok) throw new ApiError(response.status, data);
      return { data, etag: response.headers.get('ETag'), status: response.status };
    } finally { clearTimeout(timer); }
  }
  config() { return this.request('config'); }
  capabilities() { return this.request('capabilities'); }
  status() { return this.request('status'); }
  validate(settings) { return this.request('validate', {method:'POST', body:JSON.stringify({settings})}); }
  save(settings, etag, confirm) { return this.request('config', {method:'PUT', headers:{'If-Match':etag}, body:JSON.stringify({settings,confirm_reprocess:confirm})}); }
}
