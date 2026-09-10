import { ManifestSource } from './manifest.js';
import { SatellitePlayer } from './player.js';
import { createRenderer } from './render.js';

const base = new URL('../', import.meta.url);
async function readJSON(path) {
  const response = await fetch(new URL(path, base), { cache: 'no-cache', redirect: 'error', credentials: 'same-origin' });
  if (!response.ok) throw new Error('Не удалось прочитать локальные ресурсы');
  return response.json();
}
try {
  const [dictionary, satelliteAssets] = await Promise.all([
    readJSON('ru.json'), readJSON('assets/satellites/manifest.json')
  ]);
  // До первой публикации есть лишь интервал повторного подключения, не рецепты и не фильтры снимков.
  const config = { messages: dictionary.messages || {}, demo: false, pollSeconds: 5 };
  dictionary.satelliteAssets = satelliteAssets;
  function applyConfig(settings) {
    if (!settings) throw new Error('Бэкенд не передал настройки показа');
    Object.assign(config, settings);
    const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
    document.documentElement.style.setProperty('--transition-ms', `${config.performanceMode || reduced ? 0 : config.transitionMilliseconds}ms`);
    document.documentElement.style.setProperty('--portrait-tour-ms', `${config.performanceMode || reduced ? 0 : config.portraitTourTransitionMilliseconds}ms`);
    document.documentElement.style.setProperty('--ambient-opacity', config.ambient ? String(config.ambientOpacityPercent / 100) : '0');
    document.documentElement.classList.toggle('performance-mode', !!config.performanceMode);
  }
  const renderer = createRenderer(dictionary, config);
  const source = new ManifestSource(new URL('/api/v1/satellite/manifest', base).href, applyConfig);
  const player = new SatellitePlayer(renderer, source, config);
  document.getElementById('previous').onclick = () => player.navigate(-1);
  document.getElementById('next').onclick = () => player.navigate(1);
  document.getElementById('latest-button').onclick = () => player.navigate('latest');
  document.getElementById('refresh').onclick = () => player.poll();
  document.getElementById('pause-button').onclick = () => player.togglePause();
  document.getElementById('timeline-events').addEventListener('click', event => {
    const target = event.target.closest('[data-frame-id]');
    const frame = player.queue.find(item => item.id === target?.dataset.frameId);
    if (frame && !player.loading && !player.transitionTimer && !player.isCurrent(frame)) player.prepare(frame, true);
  });
  document.getElementById('undated-button').onclick = () => {
    const frame = player.queue.find(item => !item.timeKnown);
    if (frame && !player.loading && !player.transitionTimer) player.prepare(frame, true);
  };
  let resizing = false;
  const resize = () => {
    if (resizing) return;
    resizing = true;
    requestAnimationFrame(() => { resizing = false; renderer.resize?.(); renderer.timeline(player.queue, player.current, player.now()); });
  };
  window.addEventListener('resize', resize);
  player.start();
  window.addEventListener('pagehide', () => { player.stop(); window.removeEventListener('resize', resize); }, { once: true });
} catch (error) {
  const status = document.getElementById('status');
  if (status) { status.textContent = 'Не удалось запустить показ · проверьте локальные ресурсы'; status.hidden = false; }
  console.error(error);
}
