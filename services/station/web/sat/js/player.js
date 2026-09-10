import { makeQueue, nextFrame, frameContext } from './queue.js';
import { decodeInto } from './preload.js';

// Выбирает время показа: свежий кадр виден дольше, сложные цветные продукты — не менее 40 секунд.
export function frameDuration(frame, context, config) {
  const kind = String(frame.legend?.kind || '').toLowerCase();
  const complexProducts = (config.complexProducts || []).map(value => String(value).toLowerCase());
  const complex = ['categorical', 'continuous', 'composite'].includes(kind) || frame.legend?.required || complexProducts.includes(String(frame.product || '').toLowerCase());
  return context.latest ? config.latestSeconds : complex ? config.complexDisplaySeconds : config.displaySeconds;
}

// Проигрыватель не прерывает текущий кадр: обновлённый манифест только перестраивает очередь.
export class SatellitePlayer {
  constructor(renderer, source, config) {
    this.render = renderer;
    this.source = source;
    this.config = config;
    this.config.messages = config.messages || {};
    this.frames = [];
    this.queue = [];
    this.current = null;
    this.active = 0;
    this.prepared = null;
    this.loading = false;
    this.paused = false;
    this.timer = null;
    this.pollTimer = null;
    this.transitionTimer = null;
    this.tourTimer = null;
    this.tour = null;
    this.failures = new Map();
    this.startedAt = Date.now();
    this.deadline = 0;
    this.lastError = '';
    this.refreshError = false;
    this.boundVisibility = () => this.visibility();
    this.boundKey = event => this.key(event);
  }
  start() {
    document.addEventListener('visibilitychange', this.boundVisibility);
    document.addEventListener('keydown', this.boundKey);
    this.render.images.forEach(image => { image.decoding = 'async'; image.loading = 'eager'; });
    this.render.ambientImages?.forEach(image => { image.decoding = 'async'; image.loading = 'eager'; });
    this.visibility();
  }
  async poll() {
    if (this.source.controller) return;
    clearTimeout(this.pollTimer);
    if (document.hidden || this.stopped) return;
    try {
      const manifest = await this.source.refresh();
      if (manifest) {
        if (this.config.demo) {
          const reference = Date.parse(manifest.asOf);
          if (!Number.isFinite(reference)) throw new Error('Для архивного примера требуется точное опорное время');
          this.referenceTime = reference;
        }
        this.frames = manifest.frames;
        this.generatedAt = manifest.generatedAt;
        const ids = new Set(this.frames.map(frame => frame.id));
        for (const id of this.failures.keys()) if (!ids.has(id)) this.failures.delete(id);
      }
      this.refreshError = false;
    } catch (error) {
      if (error.name !== 'AbortError') { this.lastError = error.message; this.refreshError = true; }
    } finally {
      if (!document.hidden && !this.stopped) {
        this.tick();
        this.pollTimer = setTimeout(() => this.poll(), this.config.pollSeconds * 1000);
      }
    }
  }
  rebuild() {
    this.queue = makeQueue(this.frames, this.config, this.now());
    if (this.prepared && !this.queue.some(frame => frame.id === this.prepared.id && frame.display === this.prepared.display)) {
      this.prepared = null;
      this.render.images[1 - this.active].removeAttribute('src');
      this.render.ambientImages?.[1 - this.active]?.removeAttribute('src');
    }
  }
  now() { return this.config.demo && this.referenceTime ? this.referenceTime : Date.now() + (this.source.clockOffset || 0); }
  schedule(milliseconds) {
    clearTimeout(this.timer);
    if (!this.stopped && !document.hidden) this.timer = setTimeout(() => this.tick(), Math.max(100, milliseconds));
  }
  tick() {
    clearTimeout(this.timer);
    if (document.hidden || this.stopped) return;
    this.rebuild();
    if (this.current && !this.queue.some(frame => this.isCurrent(frame)) && !this.loading && !this.transitionTimer) {
      const replacement = this.queue.find(frame => frame.id === this.current.id) || this.availableNext();
      if (replacement) { this.prepare(replacement, true); return; }
      this.current = null; this.prepared = null; this.stopPortraitTour();
      this.render.images.forEach(image => { image.classList.remove('is-active'); image.removeAttribute('src'); });
      this.render.ambientImages?.forEach(image => { image.classList.remove('is-active'); image.removeAttribute('src'); });
      this.render.clear?.();
    }
    this.render.clock?.(this.now());
    this.render.timeline(this.queue, this.current, this.now());
    this.render.diagnostics?.({ current: this.current?.raw, queueLength: this.queue.length, generatedAt: this.generatedAt,
      error: this.lastError, paused: this.paused, decodedBuffers: this.render.images.filter(image => image.hasAttribute('src')).length });
    const newest = this.frames.reduce((time, frame) => Math.max(time, Number.isFinite(frame.endMs) ? frame.endMs : 0), 0);
    const age = newest ? this.now() - newest : 0;
    const messages = this.config.messages;
    this.render.status(this.config.demo ? `${messages.archive} · ${new Date(this.now()).toLocaleDateString('ru-RU', { timeZone: 'UTC' })}` : this.refreshError ? messages.unavailable : !this.current ? messages.waiting : age > this.config.sourceDelayHours * 3600000 ? `${messages.delayed} · ${Math.floor(age / 3600000)} ${messages.hours} ${Math.floor(age / 60000) % 60} ${messages.minutes}` : '');
    if (this.paused || this.loading || this.transitionTimer) { this.schedule(15000); return; }
    if (!this.current) {
      const first = this.availableNext();
      if (first) this.prepare(first, true); else this.schedule(15000);
      return;
    }
    if (Date.now() >= this.deadline && this.prepared) { this.commit(); return; }
    const remaining = this.deadline - Date.now();
    if (!this.prepared && remaining <= this.config.preloadSeconds * 1000) {
      const next = this.availableNext();
      if (next && !this.isCurrent(next)) { this.prepare(next, false); return; }
      if (next && remaining <= 0) {
        if (this.shouldReload() && this.queue.length === 1) { location.reload(); return; }
        this.deadline = Date.now() + this.config.latestSeconds * 1000;
      }
    }
    this.schedule(this.prepared ? Math.min(15000, Math.max(100, remaining)) : Math.min(15000, Math.max(1000, remaining - this.config.preloadSeconds * 1000)));
  }
  availableNext(direction = 1) {
    const eligible = this.queue.filter(frame => (this.failures.get(frame.id) || 0) < Date.now());
    return nextFrame(eligible, this.current, direction);
  }
  isCurrent(frame) { return frame.id === this.current?.id && frame.display === this.current?.display && frame.version === this.current?.version; }
  shouldReload() {
    const hour = new Date(this.now()).getUTCHours();
    return !this.config.demo && this.config.dailyReload && Date.now() - this.startedAt > 20 * 3600000 && hour >= this.config.reloadHoursUTC[0] && hour < this.config.reloadHoursUTC[1];
  }
  // Тур делит доступное время кадра на три равные части: верх, середина и низ.
  stopPortraitTour() {
    clearTimeout(this.tourTimer);
    this.tourTimer = null;
    this.tour = null;
  }
  startPortraitTour(image, durationSeconds, enabled) {
    this.stopPortraitTour();
    this.render.setFrameView?.(image, 'top');
    if (!enabled || !this.config.portraitTourEnabled || matchMedia('(prefers-reduced-motion: reduce)').matches) return;
    const step = Math.max(1000, durationSeconds * 1000 / 3);
    this.tour = { image, phases: ['middle', 'bottom'], index: 0, step, remaining: step };
    this.resumePortraitTour();
  }
  resumePortraitTour() {
    const tour = this.tour;
    if (!tour || tour.index >= tour.phases.length || this.paused || document.hidden) return;
    const delay = Math.max(100, tour.remaining || tour.step);
    tour.nextAt = Date.now() + delay;
    this.tourTimer = setTimeout(() => {
      this.tourTimer = null;
      if (!this.tour || this.paused || document.hidden) return;
      this.render.setFrameView?.(tour.image, tour.phases[tour.index]);
      tour.index += 1;
      tour.remaining = tour.step;
      this.resumePortraitTour();
    }, delay);
  }
  pausePortraitTour() {
    if (!this.tourTimer || !this.tour) return;
    this.tour.remaining = Math.max(100, this.tour.nextAt - Date.now());
    clearTimeout(this.tourTimer);
    this.tourTimer = null;
  }
  async prepare(frame, immediate) {
    if (this.stopped || this.loading || this.transitionTimer || document.hidden) return;
    this.loading = true;
    this.render.pending?.(true);
    const target = this.render.images[1 - this.active];
    this.prepared = null;
    try {
      await decodeInto(target, frame, this.config.imageTimeoutSeconds * 1000);
      if (this.stopped) { target.removeAttribute('src'); return; }
      // Ambient декодируется в малом отдельном буфере до начала общего перехода.
      const ambientTarget = this.render.ambientImages?.[1 - this.active];
      if (ambientTarget) {
        ambientTarget.removeAttribute('src');
        if (this.config.ambient && frame.ambient) {
          try {
            await decodeInto(ambientTarget, { display: frame.ambient }, this.config.imageTimeoutSeconds * 1000);
          } catch (_) {
            // Неисправный декоративный фон не должен скрывать ценный метеорологический кадр.
            ambientTarget.removeAttribute('src');
          }
        }
      }
      if (this.stopped) return;
      this.prepared = frame;
      this.rebuild();
      this.failures.delete(frame.id);
      if (this.prepared && !document.hidden && (immediate || (!this.paused && Date.now() >= this.deadline))) this.commit();
    } catch (error) {
      this.lastError = error.message;
      this.failures.set(frame.id, Date.now() + 90000);
    } finally {
      this.loading = false;
      this.render.pending?.(false);
      if (!this.stopped) this.schedule(100);
    }
  }
  commit() {
    if (this.stopped || !this.prepared || this.transitionTimer || document.hidden) return;
    const frame = this.prepared;
    // Ежесуточное обновление страницы разрешено только на границе полного цикла.
    if (this.shouldReload() && this.current && this.queue.length > 1 && frame.id === this.queue[0]?.id && this.current.id === this.queue[this.queue.length - 1]?.id) {
      location.reload(); return;
    }
    const old = this.render.images[this.active];
    this.active = 1 - this.active;
    const active = this.render.images[this.active];
    const context = frameContext(this.queue, frame);
    this.current = frame;
    this.prepared = null;
    const duration = frameDuration(frame, context, this.config);
    const presentation = this.render.showFrame(frame, { ...context, now: this.now(), demo: this.config.demo,
      ambient: this.config.ambient && !this.config.performanceMode,
      activeImage: active, durationSeconds: duration });
    active.classList.add('is-active');
    old.classList.remove('is-active');
    const activeAmbient = this.render.ambientImages?.[this.active];
    const oldAmbient = this.render.ambientImages?.[1 - this.active];
    activeAmbient?.classList.add('is-active');
    oldAmbient?.classList.remove('is-active');
    this.currentDuration = duration;
    this.currentInspectable = !!presentation?.inspectable;
    this.startPortraitTour(active, duration, this.currentInspectable);
    this.deadline = Date.now() + duration * 1000;
    if (this.paused) this.pauseStartedAt = Date.now();
    const transition = this.config.performanceMode || matchMedia('(prefers-reduced-motion: reduce)').matches ? 0 : this.config.transitionMilliseconds;
    this.transitionTimer = setTimeout(() => {
      old.removeAttribute('src');
      oldAmbient?.removeAttribute('src');
      this.render.releaseImage?.(old);
      this.transitionTimer = null;
      this.tick();
    }, transition);
  }
  navigate(direction) {
    if (this.loading || this.transitionTimer) return;
    this.rebuild();
    const next = direction === 'latest' ? (this.queue.find(frame => frame.latest) || this.queue[this.queue.length - 1]) : this.availableNext(direction);
    if (next && !this.isCurrent(next)) this.prepare(next, true);
  }
  togglePause() {
    this.paused = !this.paused;
    if (this.paused) { this.pauseStartedAt = Date.now(); this.pausePortraitTour(); }
    else { this.deadline += Date.now() - (this.pauseStartedAt || Date.now()); this.resumePortraitTour(); }
    this.render.setPaused?.(this.paused);
    const button = document.getElementById('pause-button');
    if (button) { button.textContent = this.paused ? 'Продолжить' : 'Пауза'; button.setAttribute('aria-pressed', String(this.paused)); }
    this.tick();
  }
  stop() {
    this.stopped = true;
    clearTimeout(this.timer); clearTimeout(this.pollTimer); clearTimeout(this.transitionTimer);
    this.stopPortraitTour(); this.source.stop();
    document.removeEventListener('visibilitychange', this.boundVisibility);
    document.removeEventListener('keydown', this.boundKey);
  }
  key(event) {
    if (event.altKey || event.ctrlKey || event.metaKey || /^(INPUT|TEXTAREA|SELECT)$/.test(event.target.tagName)) return;
    if (event.code === 'Space') { event.preventDefault(); this.togglePause(); }
    else if (event.key === 'ArrowRight') { event.preventDefault(); this.navigate(1); }
    else if (event.key === 'ArrowLeft') { event.preventDefault(); this.navigate(-1); }
    else if (event.key === 'Home') { event.preventDefault(); this.navigate('latest'); }
    else if (event.code === 'KeyR') this.poll();
    else if (event.code === 'KeyD') this.render.toggleDiagnostics?.();
  }
  visibility() {
    clearTimeout(this.timer);
    clearTimeout(this.pollTimer);
    if (document.hidden) {
      this.render.setSuspended?.(true);
      this.pausePortraitTour();
      this.source.stop();
      if (this.transitionTimer) {
        clearTimeout(this.transitionTimer);
        this.transitionTimer = null;
        const inactive = this.render.images[1 - this.active];
        inactive.removeAttribute('src');
        this.render.ambientImages?.[1 - this.active]?.removeAttribute('src');
        this.render.releaseImage?.(inactive);
      }
    } else {
      this.render.setSuspended?.(false);
      // После возврата на вкладку валидный кадр снова показывается полный интервал.
      if (this.current) {
        const duration = this.currentDuration || this.config.displaySeconds;
        this.deadline = Date.now() + duration * 1000;
    if (this.paused) this.pauseStartedAt = Date.now();
        this.startPortraitTour(this.render.images[this.active], duration, this.currentInspectable);
        if (this.paused) this.pausePortraitTour();
      }
      this.poll();
      this.tick();
    }
  }
}
