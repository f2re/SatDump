// Короткие безопасные помощники для обновления текста и UTC-дат без HTML-инъекций.
const $ = id => document.getElementById(id);
const text = (id, value) => { $(id).textContent = value == null ? '' : String(value); };
const utc = (value, options) => new Intl.DateTimeFormat('ru-RU', {timeZone:'UTC', ...options}).format(new Date(value));
const time = value => utc(value, {hour:'2-digit',minute:'2-digit',second:'2-digit',hour12:false});
const shortTime = value => utc(value, {hour:'2-digit',minute:'2-digit',hour12:false});
// Метаданные не переводятся эвристически и не заменяются справочником браузера.
const suppliedText = (value, fallback = '') => typeof value === 'string' && value.trim() ? value : fallback;
const localUrl = value => {
  if (typeof value !== 'string' || !value) return null;
  try { const url = new URL(value, location.href); return url.origin === location.origin && /^https?:$/.test(url.protocol) ? url.href : null; } catch (_) { return null; }
};

export function createRenderer(dictionary, config = {}) {
  const d = dictionary || {}, ui = d.ui || {};
  // Настройки и ширину окна наблюдений предоставляет серверный манифест.
  let windowHours = config.windowHours;
  let windowMilliseconds = windowHours * 3600000;
  const images = [$('frame-a'), $('frame-b')];
  const ambientImages = [$('ambient-a'), $('ambient-b')];
  let displayed = null;
  let displayedContext = {};
  let diagnosticData = {};
  let timelineSignature = '';
  // Давность показывается кратко и обновляется вместе с часами.
  function age(end, now) {
    if (!Number.isFinite(end)) return 'Время съёмки не указано';
    const minutes = Math.max(0, Math.floor((now - end) / 60000));
    if (minutes < 1) return ui.justNow;
    return (minutes >= 60 ? `${Math.floor(minutes / 60)} ${d.messages.hours} ` : '') + `${minutes % 60} ${d.messages.minutes} ${ui.ago}`;
  }
  // Русские формы для динамического заголовка окна наблюдений.
  function hourWord(value) {
    const last = value % 10, lastTwo = value % 100;
    return last === 1 && lastTwo !== 11 ? 'час' : last >= 2 && last <= 4 && (lastTwo < 12 || lastTwo > 14) ? 'часа' : 'часов';
  }
  function windowTitle() { return windowHours === 1 ? 'ПОСЛЕДНИЙ ЧАС' : `ПОСЛЕДНИЕ ${windowHours} ${hourWord(windowHours).toUpperCase()}`; }
  function windowSummary() { return windowHours === 1 ? 'за последний час' : `за последние ${windowHours} ${hourWord(windowHours)}`; }
  // Официальные обозначения сохраняются, меняется только язык написания Meteor-M.
  function satelliteName(value) {
    return String(value || 'Спутник');
  }
  // Канонизирует русские и латинские aliases и ищет семейство в локальном каталоге.
  function satelliteAsset(value) {
    const catalog = d.satelliteAssets || {};
    const normalize = item => String(item || '').trim().toLocaleLowerCase('ru-RU').replace(/ё/g, 'е').replace(/[^\p{L}\p{N}]+/gu, '-').replace(/^-|-$/g, '');
    const name = normalize(value);
    const item = (catalog.items || []).find(candidate => (candidate.matches || []).some(alias => normalize(alias) === name));
    return item || {
      display: catalog.fallback?.glyph, generic: true,
      glyph: catalog.fallback?.glyph,
      timelineGlyph: catalog.fallback?.timelineGlyph || '/sat/assets/satellites/timeline/generic-satellite.png'
    };
  }
  function legendColor(value) {
    if (typeof value === 'string' && /^#[\da-f]{6}$/i.test(value)) return value;
    if (!Array.isArray(value) || value.length < 3) return null;
    const normalized = value.slice(0, 3).every(channel => typeof channel === 'number' && channel >= 0 && channel <= 1);
    const channels = value.slice(0, 3).map(channel => {
      const number = Number(channel);
      if (!Number.isFinite(number)) return null;
      return Math.round(Math.max(0, Math.min(255, normalized ? number * 255 : number)));
    });
    return channels.includes(null) ? null : `rgb(${channels.join(',')})`;
  }
  function addLegendNote(legend, fallback) {
    const notes = Array.isArray(legend.notes) ? legend.notes.filter(note => typeof note === 'string') : [];
    const target = $('legend-note'); target.replaceChildren();
    for (const note of (notes.length ? notes : fallback ? [fallback] : [])) {
      const line = document.createElement('p'); line.textContent = note; target.append(line);
    }
    target.hidden = !target.childElementCount;
  }
  function addTickLabels(container, ticks) {
    if (!Array.isArray(ticks) || !ticks.length) return;
    const labels = document.createElement('div');
    labels.className = 'legend-labels legend-positioned';
    ticks.forEach(tick => {
      const position = Number(tick.position);
      if (!Number.isFinite(position)) return;
      const label = document.createElement('span');
      label.textContent = String(tick.label == null ? position : tick.label).replace('-', '−');
      label.style.left = `${Math.max(0, Math.min(100, position * 100))}%`;
      labels.append(label);
    });
    container.append(labels);
  }
  // Легенда отображает только понятную метеорологическую интерпретацию, а не внутренние параметры.
  function showLegend(frame, product) {
    const legend = frame.legend || {};
    const panel = $('legend'), image = $('legend-image'), stops = $('legend-stops'), entries = $('legend-entries');
    const context = document.querySelector('.context');
    panel.hidden = true; panel.removeAttribute('data-kind'); context.classList.remove('has-rich-legend');
    image.hidden = true; image.removeAttribute('src'); stops.hidden = true; stops.replaceChildren();
    entries.hidden = true; entries.replaceChildren(); $('legend-note').hidden = true;
    text('legend-subtitle', ''); text('legend-kind', '');
    if (!legend || typeof legend !== 'object') return;
    const path = localUrl(legend.display || legend.url || legend.image);
    text('legend-title', suppliedText(legend.title, product.legendTitle || 'Как читать цвета'));
    if (legend.embedded) { panel.hidden = false; text('legend-title', ui.legendEmbedded); return; }
    const subtitle = suppliedText(legend.subtitle);
    if (legend.kind === 'categorical' && Array.isArray(legend.categories) && legend.categories.length) {
      panel.hidden = false; panel.dataset.kind = 'categorical'; context.classList.add('has-rich-legend');
      text('legend-kind', 'КАТЕГОРИИ / КЛАССЫ');
      text('legend-subtitle', subtitle);
      legend.categories.forEach(category => {
        const color = legendColor(category.color);
        if (!color || !suppliedText(category.label)) return;
        const item = document.createElement('div'), swatch = document.createElement('i'), copy = document.createElement('span');
        const separator = category.label.indexOf(':');
        item.className = 'legend-category'; swatch.className = 'legend-category-swatch'; swatch.style.backgroundColor = color;
        if (separator > 0) {
          const name = document.createElement('b');
          name.textContent = category.label.slice(0, separator);
          copy.append(name, document.createTextNode(category.label.slice(separator + 1)));
        } else copy.textContent = category.label;
        item.append(swatch, copy); entries.append(item);
      });
      entries.className = 'legend-categories'; entries.hidden = !entries.childElementCount;
      addLegendNote(legend, 'Цвет — класс интерпретации, а не числовое измерение.');
      return;
    }
    if (legend.kind === 'continuous' && (path || Array.isArray(legend.color_stops) || Array.isArray(legend.colorStops) || Array.isArray(legend.stops))) {
      panel.hidden = false; panel.dataset.kind = 'continuous'; context.classList.add('has-rich-legend');
      text('legend-kind', ['КОЛИЧЕСТВЕННАЯ ШКАЛА', legend.unit].filter(Boolean).join(' · '));
      text('legend-subtitle', subtitle);
      if (path) { image.hidden = false; image.src = path; }
      else {
        const source = legend.color_stops || legend.colorStops || legend.stops;
        const colors = source.map(stop => ({ color: legendColor(stop.color), position: Number(stop.position ?? stop.value) }))
          .filter(stop => stop.color && Number.isFinite(stop.position));
        if (colors.length > 1) {
          const bar = document.createElement('div'); bar.className = 'legend-gradient';
          bar.style.backgroundImage = `linear-gradient(90deg,${colors.map(stop => `${stop.color} ${Math.max(0, Math.min(1, stop.position)) * 100}%`).join(',')})`;
          stops.append(bar);
        }
      }
      addTickLabels(stops, legend.ticks);
      stops.hidden = !stops.childElementCount;
      addLegendNote(legend, '');
      return;
    }
    if (legend.kind === 'composite' && Array.isArray(legend.components) && legend.components.length) {
      panel.hidden = false; panel.dataset.kind = 'composite'; context.classList.add('has-rich-legend');
      text('legend-kind', 'СИНТЕЗ КАНАЛОВ'); text('legend-subtitle', subtitle);
      legend.components.forEach(component => {
        const item = document.createElement('div'), key = document.createElement('b'), copy = document.createElement('span');
        item.className = 'legend-component'; key.textContent = component.component;
        copy.textContent = [component.channel, component.spectral_range, component.quantity, component.formula, component.description].filter(Boolean).join(' · ');
        item.append(key, copy); entries.append(item);
      });
      entries.className = 'legend-components'; entries.hidden = !entries.childElementCount;
      addLegendNote(legend, 'Оттенок — результат синтеза каналов, а не отдельная физическая величина.');
      return;
    }
    if (subtitle || (Array.isArray(legend.notes) && legend.notes.length)) {
      panel.hidden = false; panel.dataset.kind = 'text'; text('legend-subtitle', subtitle); addLegendNote(legend, ''); return;
    }
    if (legend.required) { panel.hidden = false; panel.dataset.kind = 'missing'; text('legend-title', ui.legendMissing); }
  }
  // Заполняет краткую сводку и помечает компоновку как обычную или вертикальную.
  function showFrame(frame, context = {}) {
    displayed = frame; displayedContext = context;
    const raw = frame.raw || frame;
    const code = String(frame.product || '').toLowerCase();
    const product = raw.product || {};
    const now = context.now || Date.now();
    const activeImage = context.activeImage;
    const width = activeImage?.naturalWidth || frame.width || images.find(image => image.naturalWidth)?.naturalWidth || 1;
    const height = activeImage?.naturalHeight || frame.height || images.find(image => image.naturalHeight)?.naturalHeight || 1;
    const aspect = width / height;
    const portrait = aspect < 1.1;
    $('chronoscope').dataset.layout = portrait ? 'portrait' : 'landscape';
    $('image-stage').classList.add('is-ready');
    text('satellite', satelliteName(frame.satellite));
    const asset = satelliteAsset(frame.satellite), artwork = $('satellite-art');
    if (asset.display) {
      artwork.src = asset.display; artwork.alt = `${asset.generic ? "Условная пиктограмма" : "Иллюстрация семейства"} ${satelliteName(frame.satellite)}`; artwork.hidden = false;
    } else {
      artwork.removeAttribute('src'); artwork.alt = ''; artwork.hidden = true;
    }
    text('instrument', (d.instruments || {})[frame.instrument] || frame.instrument);
    text('product', frame.title || product.title || ui.unknownProduct);
    text('description', suppliedText(frame.description, 'Описание не предоставлено в паспорте продукта.'));
    text('purpose', frame.purpose || ''); $('purpose').hidden = !frame.purpose;
    text('image-dimensions', `${frame.width} × ${frame.height} px · ${raw.image?.mode === 'preview' ? 'Превью сервера' : 'Оригинал сервера'}`);
    text('metadata-warning', (raw.metadataWarnings || []).join(' '));
    $('metadata-warning').hidden = !(raw.metadataWarnings || []).length;
    renderDetails(frame);
    text('product-index', context.productCount > 1 ? `${ui.product} ${context.productIndex} / ${context.productCount}` : '');
    $('latest').hidden = !context.latest;
    if (frame.timeKnown) {
      text('acquisition-date', `${utc(frame.start, {day:'numeric',month:'long'})} · UTC`);
      const crossDay = new Date(frame.start).toISOString().slice(0,10) !== new Date(frame.end).toISOString().slice(0,10);
      text('acquisition-time', `${time(frame.start)}–${time(frame.end)} UTC`);
      if (crossDay) text('acquisition-date', `${utc(frame.start, {day:'numeric',month:'short'})} — ${utc(frame.end, {day:'numeric',month:'short'})} · UTC`);
    } else {
      text('acquisition-date', 'ВРЕМЯ НАБЛЮДЕНИЯ НЕ УКАЗАНО');
      text('acquisition-time', 'Нет времени съёмки');
    }
    text('pass', (d.passes || {})[frame.pass || raw.pass || raw.pass_direction] || suppliedText(frame.pass, ''));
    for (const [id, value] of [['passport-link', raw.metadata], ['original-link', raw.original]]) {
      const link = $(id), url = localUrl(value);
      if (link) { link.hidden = !url; if (url) link.href = url; else link.removeAttribute('href'); }
    }
    text('age', age(frame.timeKnown ? frame.endMs : NaN, now));
    showLegend(frame, product);
    // Вертикальный кадр увеличивается до ширины сцены; координаты трёх остановок считаются один раз.
    let inspectable = false;
    if (activeImage) {
      inspectable = layoutImage(activeImage, frame, portrait);
    }
    images.forEach(image => { image.alt = `${frame.satellite} · ${frame.title || ui.unknownProduct} · ${frame.timeKnown ? time(frame.start) + ' UTC' : 'Время не указано'}`; });
    return { inspectable };
  }
  function renderDetails(frame) {
    const raw = frame.raw || frame, target = $('metadata-details');
    target.replaceChildren();
    const add = (label, value) => {
      if (value === undefined || value === null || value === '') return;
      const dt = document.createElement('dt'), dd = document.createElement('dd');
      dt.textContent = label; dd.textContent = typeof value === 'string' ? value : JSON.stringify(value);
      target.append(dt, dd);
    };
    add('Прибор', frame.instrument); add('Макет', raw.layout);
    add('Качество', raw.quality?.label); add('Оценка качества', raw.quality?.detail);
    add('Ориентация', raw.orientation?.description);
    if (raw.orientation?.north_up_requested) add('Север сверху', raw.orientation.north_up_verified ? 'Подтверждено' : 'Не подтверждено');
    for (const item of raw.details || []) if (item && typeof item === 'object') add(item.label, item.value);
    for (const layer of raw.layers || []) {
      if (typeof layer === 'string') add('Слой', layer);
      else if (layer && typeof layer === 'object') add(layer.component || layer.name || 'Слой', [layer.channel, layer.spectral_range, layer.quantity, layer.formula, layer.description].filter(Boolean).join(' · '));
    }
    const source = raw.originalImage;
    if (source) add('Размер исходного файла', `${source.width} × ${source.height} px`);
  }
  function layoutImage(image, frame, portrait) {
    const previousView = image.dataset.view || 'top';
    releaseImage(image);
    const stage = $('image-stage').getBoundingClientRect();
    const width = image.naturalWidth || frame.width, height = image.naturalHeight || frame.height;
    const overflow = Math.max(0, height * stage.width / width - stage.height);
    const inspectable = portrait && config.portraitTourEnabled && !matchMedia('(prefers-reduced-motion: reduce)').matches && overflow > 24;
    if (inspectable) {
      image.style.setProperty('--inspection-middle', `${-overflow / 2}px`);
      image.style.setProperty('--inspection-bottom', `${-overflow}px`);
      image.dataset.view = previousView;
      image.classList.add('is-inspectable');
    }
    // Иначе object-fit:contain показывает весь кадр. Никакого лимита 115% или 1920×1080.
    return inspectable;
  }
  function resize() {
    timelineSignature = '';
    if (displayed) {
      const image = images.find(item => item.classList.contains('is-active'));
      if (image) layoutImage(image, displayed, (image.naturalWidth || displayed.width) / (image.naturalHeight || displayed.height) < 1.1);
    }
  }
  // Шкала группирует несколько продуктов одного пролёта в одну реальную точку наблюдения.
  function timeline(queue, current, now = Date.now()) {
    if (!config.windowHours) return;
    const undated = queue.filter(frame => !frame.timeKnown).length;
    $('undated-button').hidden = !undated;
    $('undated-button').textContent = `Без времени: ${undated}`;
    queue = queue.filter(frame => frame.timeKnown);
    current = current?.timeKnown ? current : null;
    windowHours = config.windowHours; windowMilliseconds = windowHours * 3600000;
    const start = now - windowMilliseconds;
    const grouped = new Map();
    queue.forEach(frame => {
      const key = frame.eventId || frame.start;
      const previous = grouped.get(key);
      if (!previous || (frame.startMs || Date.parse(frame.start)) < (previous.startMs || Date.parse(previous.start))) grouped.set(key, frame);
    });
    const events = [...grouped.values()].sort((a, b) => (a.startMs || Date.parse(a.start)) - (b.startMs || Date.parse(b.start)));
    const signature = `${windowHours}|${Math.floor(now / 60000)}|${current?.id}|${events.map(frame => frame.id).join(',')}`;
    if (signature === timelineSignature) return;
    timelineSignature = signature;
    const ticks = document.createDocumentFragment(), marks = document.createDocumentFragment();
    // Получаем около 6–9 читаемых делений для любого разрешённого окна.
    const desiredTickHours = windowHours / 8;
    const tickHours = [1, 2, 3, 4, 6, 8, 12].find(value => value >= desiredTickHours) || 12;
    const tickMilliseconds = tickHours * 3600000;
    for (let at = Math.ceil(start / tickMilliseconds) * tickMilliseconds; at <= now; at += tickMilliseconds) {
      const tick = document.createElement('span'), label = document.createElement('span');
      tick.className = 'timeline-tick'; tick.style.left = `${(at - start) * 100 / windowMilliseconds}%`;
      label.textContent = utc(at, {hour:'2-digit',hour12:false}); tick.append(label); ticks.append(tick);
      tick.classList.toggle('is-midnight', new Date(at).getUTCHours() === 0);
    }
    // Полные подписи размещаются справа налево в двух полосах без пересечений.
    // Непоместившееся наблюдение сохраняет точку и PNG-глиф, поэтому данные не исчезают.
    const trackWidth = Math.max(320, $('timeline-events').clientWidth || document.querySelector('.timeline-track')?.clientWidth || 1200);
    const labelWidth = trackWidth < 900 ? 96 : 118, labelGap = 8, laneStart = [Infinity, Infinity], placements = new Map();
    const currentPosition = current ? Math.min(100,Math.max(0,((current.startMs || Date.parse(current.start)) - start) * 100 / windowMilliseconds)) : -100;
    const currentCenter = currentPosition * trackWidth / 100;
    const currentCardCenter = Math.max(116, Math.min(trackWidth - 116, currentCenter));
    for (let index = events.length - 1; index >= 0; index--) {
      const frame = events[index];
      if ((frame.eventId || frame.start) === (current?.eventId || current?.start)) continue;
      const position = Math.min(100,Math.max(0,((frame.startMs || Date.parse(frame.start)) - start) * 100 / windowMilliseconds));
      const center = position * trackWidth / 100;
      const left = Math.max(0, Math.min(trackWidth - labelWidth, center - labelWidth / 2));
      const right = left + labelWidth;
      // Рядом с карточкой выбранного кадра остаются только точки и глифы.
      if (current && right > currentCardCenter - 118 && left < currentCardCenter + 118) continue;
      const preferred = index % 2, lanes = [preferred, 1 - preferred];
      const lane = lanes.find(candidate => right + labelGap <= laneStart[candidate]);
      if (lane === undefined) continue;
      laneStart[lane] = left;
      placements.set(index, { lane, offset: left - center });
    }
    events.forEach((frame, index) => {
      const mark = document.createElement('button'); mark.type = 'button'; mark.className = 'timeline-event'; mark.dataset.frameId = frame.id; mark.setAttribute('aria-label', `${satelliteName(frame.satellite)} ${time(frame.start)} UTC`);
      mark.classList.toggle('is-latest', index === events.length - 1);
      mark.classList.toggle('is-current', (frame.eventId || frame.start) === (current?.eventId || current?.start));
      const position = Math.min(100,Math.max(0,((frame.startMs || Date.parse(frame.start)) - start) * 100 / windowMilliseconds));
      mark.style.left = `${position}%`;
      mark.title = `${satelliteName(frame.satellite)} · ${time(frame.start)} UTC`;
      const icon = document.createElement('img'); icon.className = 'event-glyph'; icon.src = satelliteAsset(frame.satellite).timelineGlyph; icon.alt = '';
      mark.append(icon);
      const placement = placements.get(index);
      if (placement) {
        const label = document.createElement('span'), name = document.createElement('strong'), observed = document.createElement('time');
        label.className = `event-label row-${placement.lane}`; label.style.left = `${placement.offset}px`; label.style.width = `${labelWidth}px`;
        name.textContent = satelliteName(frame.satellite); observed.textContent = shortTime(frame.start); observed.dateTime = frame.start;
        label.append(name, observed); mark.append(label);
      }
      marks.append(mark);
    });
    $('timeline-ticks').replaceChildren(ticks); $('timeline-events').replaceChildren(marks);
    text('window-start', utc(start, {day:'numeric',month:'short',hour:'2-digit',minute:'2-digit'}));
    text('window-end', `${utc(now, {hour:'2-digit',minute:'2-digit'})} UTC`);
    text('observation-count', events.length ? String(events.length) : '—');
    text('timeline-caption', current ? `${shortTime(current.start)} UTC · ${satelliteName(current.satellite)}` : 'Время съёмки');
    const latest = events[events.length - 1];
    text('last-observation', latest ? `${shortTime(latest.start)} UTC` : '—');
    text('timeline-title', windowTitle());
    text('timeline-summary-label', windowSummary());
    document.querySelector('.timeline').setAttribute('aria-label', `Временная шкала ${windowSummary()}`);
    const card = $('selection-card');
    if (current) {
      card.style.left = `clamp(116px, ${currentPosition}%, calc(100% - 116px))`;
      text('selection-time', `${time(current.start)}–${time(current.end)} UTC`);
      text('selection-satellite', satelliteName(current.satellite)); card.hidden = false;
    } else card.hidden = true;
  }
  // Часы всегда работают в UTC, независимо от часового пояса Raspberry Pi.
  function clock(now = Date.now()) {
    text('clock', `${utc(now,{day:'numeric',month:'long'})} · ${utc(now,{hour:'2-digit',minute:'2-digit'})} UTC`);
    if (displayed) text('age', age(displayed.timeKnown ? displayed.endMs : NaN, now));
  }
  function diagnostics(data) { diagnosticData = data; if (!$('diagnostics').hidden) $('diagnostics').textContent = JSON.stringify(diagnosticData, null, 2); }
  $('legend-image').addEventListener('error', () => { $('legend-image').hidden = true; text('legend-title', ui.legendMissing); });
  // Меняет только готовую позицию вертикального кадра; размеры изображения повторно не вычисляются.
  function setFrameView(image, view) {
    if (image?.classList.contains('is-inspectable') && ['top', 'middle', 'bottom'].includes(view)) image.dataset.view = view;
  }
  function releaseImage(image) {
    if (!image) return;
    image.classList.remove('is-inspectable');
    image.removeAttribute('data-view');
    image.style.removeProperty('--inspection-middle');
    image.style.removeProperty('--inspection-bottom');
  }
  return {images, ambientImages, showFrame, setFrameView, releaseImage, resize, timeline, clock, diagnostics, diagnostic:diagnostics,
    status:value => text('status', value),
    pending:value => document.querySelector('.timeline').classList.toggle('is-pending', !!value),
    setPaused:value => { $('paused').hidden = !value; $('chronoscope').classList.toggle('is-paused', !!value); },
    setSuspended:value => $('chronoscope').classList.toggle('is-suspended', !!value),
    clear:() => {
      displayed = null; $('image-stage').classList.remove('is-ready');
      text('satellite', 'Спутниковая съёмка'); text('product', 'Нет кадров в выбранном периоде');
      text('acquisition-time', '—'); text('acquisition-date', 'ВРЕМЯ НАБЛЮДЕНИЯ · UTC');
      text('age', ''); text('pass', ''); text('description', 'Ожидание новых наблюдений.');
      $('legend').hidden = true; $('satellite-art').hidden = true;
      $('latest').hidden = true; $('passport-link').hidden = true; $('original-link').hidden = true;
      $('metadata-details').replaceChildren(); text('purpose', ''); text('image-dimensions', ''); text('metadata-warning', '');
    },
    toggleDiagnostics:() => { $('diagnostics').hidden = !$('diagnostics').hidden; diagnostics(diagnosticData); }
  };
}
