'use strict';
(function () {
  const $ = id => document.getElementById(id);
  let all = [], items = [], selected = '', index = 0, token = 0, limit = 60;
  let playing = false, timer = null, lastDigest = '';
  const displayTime = epoch => new Date(epoch * 1000).toLocaleString('ru-RU', {timeZone: 'UTC'}) + ' UTC';
  const safeAsset = url => /^items\/[0-9a-f]{64}\/[0-9]{3}(?:-preview|-thumb)?\.(png|jpg|json)$/.test(url || '') ? url : '';
  const text = (id, value) => { $(id).textContent = value || ''; };
  const clear = element => { while (element.firstChild) element.removeChild(element.firstChild); };
  function schedule() {
    clearTimeout(timer);
    if (playing && items.length > 1 && !document.hidden) timer = setTimeout(() => show((index + 1) % items.length), 10000);
  }
  async function show(i) {
    if (!items.length) return;
    index = (i + items.length) % items.length;
    const item = items[index], own = ++token;
    selected = item.id;
    $('loading').hidden = false;
    clearTimeout(timer);
    const image = new Image();
    image.onload = async function () {
      if (own !== token) return;
      $('hero').src = image.src;
      $('hero').alt = item.satellite + ': ' + item.title;
      $('hero').hidden = false; $('empty').hidden = true; $('caption').hidden = false; $('loading').hidden = true;
      text('title', item.title); text('instrument', [item.satellite, item.instrument].filter(Boolean).join(' / '));
      text('time', item.acquisition_time ? 'Наблюдение: ' + item.acquisition_time : 'Время наблюдения не указано · файл изменён ' + displayTime(item.file_mtime));
      text('counter', (index + 1) + ' / ' + items.length);
      $('original').href = safeAsset(item.original); $('passport').href = safeAsset(item.metadata);
      text('metadata', 'Чтение паспорта…');
      schedule();
      const next = items[(index + 1) % items.length];
      if (next && next.id !== item.id) { const preload = new Image(); preload.src = safeAsset(next.preview); }
      try {
        const response = await fetch(safeAsset(item.metadata), {cache: 'force-cache'});
        if (!response.ok) throw new Error('Паспорт недоступен');
        const passport = await response.json();
        if (own === token) text('metadata', (item.native_presentation ? '' : 'Готовое изображение: спектральные каналы и физический смысл цветов не подтверждены.\n\n') + JSON.stringify(passport, null, 2));
      } catch (error) { if (own === token) text('metadata', error.message); }
    };
    image.onerror = function () { if (own === token) { $('loading').hidden = false; text('loading', 'Не удалось загрузить снимок. Предыдущий кадр сохранён.'); schedule(); } };
    text('loading', 'Загрузка снимка…');
    image.src = safeAsset(item.preview);
  }
  function renderGrid() {
    clear($('grid'));
    items.slice(0, limit).forEach((item, i) => {
      const card = document.createElement('button'); card.className = 'card'; card.type = 'button';
      const img = document.createElement('img'); img.src = safeAsset(item.thumbnail); img.alt = ''; img.loading = 'lazy';
      const title = document.createElement('strong'); title.textContent = item.title;
      const desc = document.createElement('small'); desc.textContent = item.satellite + ' · ' + (item.acquisition_time || 'Время наблюдения не указано');
      card.append(img, title, desc); card.onclick = () => { show(i); $('hero').scrollIntoView({block: 'center'}); };
      $('grid').appendChild(card);
    });
    $('more').hidden = limit >= items.length;
    text('count', items.length + ' продуктов');
  }
  function filter() {
    const query = $('search').value.toLowerCase(), date = $('date').value.toLowerCase();
    items = all.filter(x => (!$('source').value || x.source === $('source').value) && (!$('satellite').value || x.satellite === $('satellite').value) && (x.title + ' ' + x.instrument).toLowerCase().includes(query) && (x.acquisition_time || '').toLowerCase().includes(date));
    renderGrid();
    if (!items.length) {
      ++token; clearTimeout(timer); $('hero').hidden = true; $('caption').hidden = true; $('loading').hidden = true; $('empty').hidden = false;
      text('emptyTitle', all.length ? 'Ничего не найдено' : 'Архив пока пуст');
      text('emptyText', all.length ? 'Измените фильтры источника, спутника или времени.' : 'Завершённые снимки появятся здесь автоматически.');
      text('metadata', 'Нет продуктов, соответствующих фильтру.');
      return;
    }
    const retained = items.findIndex(x => x.id === selected);
    show(retained >= 0 ? retained : 0);
  }
  function options(id, field, label) {
    const saved = $(id).value;
    clear($(id)); $(id).add(new Option(label, ''));
    [...new Set(all.map(x => x[field]).filter(Boolean))].sort().forEach(x => $(id).add(new Option(x, x)));
    if ([...$(id).options].some(x => x.value === saved)) $(id).value = saved;
  }
  async function refresh() {
    try {
      const response = await fetch('catalog.json', {cache: 'no-store'});
      if (!response.ok) throw new Error('Каталог ещё не сформирован');
      const catalog = await response.json();
      if (catalog.schema !== 'satdump.gallery/1') throw new Error('Неизвестная версия каталога');
      text('siteTitle', catalog.title); text('updated', 'Каталог: ' + displayTime(catalog.updated_at));
      const digest = catalog.items.map(x => x.id).join(',');
      if (digest !== lastDigest || !all.length) {
        lastDigest = digest; all = catalog.items; options('source', 'source', 'Все источники'); options('satellite', 'satellite', 'Все спутники'); filter();
      }
    } catch (error) { text('updated', 'Связь с каталогом потеряна · последний снимок сохранён'); }
    try {
      const response = await fetch('health.json', {cache: 'no-store'});
      const health = await response.json();
      text('health', !health.worker_alive ? 'Воркер не отвечает · архив доступен' : Object.keys(health.sources || {}).length ? 'Источник недоступен или недостаточно места' : 'Воркер работает · ошибок: ' + ((health.queue || {}).failed || 0));
    } catch (error) { text('health', 'Нет связи с сервером · показ сохранён'); }
    setTimeout(refresh, 15000);
  }
  $('play').onclick = () => { playing = !playing; $('play').setAttribute('aria-pressed', String(playing)); $('play').textContent = playing ? 'Ⅱ Пауза' : '▶ Презентация'; document.body.classList.add('presenting'); $('exitPresentation').hidden = false; schedule(); };
  $('exitPresentation').onclick = () => { playing = false; clearTimeout(timer); document.body.classList.remove('presenting'); $('exitPresentation').hidden = true; $('play').setAttribute('aria-pressed', 'false'); text('play', '▶ Презентация'); };
  $('fullscreen').onclick = () => {
    if (!document.documentElement.requestFullscreen) { text('health', 'Используйте F11 для полноэкранного режима'); return; }
    const result = document.fullscreenElement ? document.exitFullscreen() : document.documentElement.requestFullscreen();
    if (result && result.catch) result.catch(() => text('health', 'Полноэкранный режим запрещён браузером'));
  };
  $('prev').onclick = () => show(index - 1); $('next').onclick = () => show(index + 1);
  $('more').onclick = () => { limit += 60; renderGrid(); };
  ['source', 'satellite', 'search', 'date'].forEach(id => $(id).addEventListener('input', () => { limit = 60; filter(); }));
  document.addEventListener('visibilitychange', schedule);
  document.addEventListener('keydown', event => {
    if (['INPUT','SELECT','TEXTAREA'].includes(document.activeElement.tagName)) return;
    if (event.key === 'ArrowRight') show(index + 1);
    if (event.key === 'ArrowLeft') show(index - 1);
    if (event.key === ' ') { event.preventDefault(); $('play').click(); }
  });
  refresh();
}());
