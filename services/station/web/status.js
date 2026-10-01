/* Shared display rules. No network calls, credentials or inferred observation times. */
(function (root, factory) {
  'use strict';
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.StationText = api;
}(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  const count = value => Number.isInteger(value) && value >= 0 ? value : null;
  const names = {mtvza: 'МТВЗА-ГЯ', msu_mr: 'МСУ-МР'};
  const instrument = value => typeof value === 'string' ? (names[value] || value.slice(0, 64)) : 'Прибор';
  function displayTime(epoch) {
    if (!Number.isFinite(epoch)) return '—';
    const date = new Date(epoch * 1000);
    return Number.isFinite(date.getTime()) ? date.toLocaleString('ru-RU', {
      timeZone: 'UTC', hourCycle: 'h23'
    }) + ' UTC' : '—';
  }
  function observation(item) {
    const file = ' · файл изменён ' + displayTime(item.file_mtime);
    if (item.acquisition_time_status === 'invalid') return 'Время наблюдения некорректно' + file;
    if (item.acquisition_time_status === 'missing' || !item.acquisition_time)
      return 'Время наблюдения не указано' + file;
    return 'Наблюдение: ' + item.acquisition_time;
  }
  function qualitySummary(quality) {
    if (!quality || typeof quality !== 'object') return 'Полнота набора не проверена';
    if (quality.status === 'external') return 'Готовое изображение · полнота приборов не проверялась';
    if (quality.status === 'ok') return 'В отчёте обработки нет предупреждений';
    const warnings = Array.isArray(quality.warnings) ? quality.warnings : [];
    if (quality.status === 'partial') {
      const unverified = warnings.includes('instrument_completeness_unverified');
      const onlyUnverified = unverified && warnings.every(x => x === 'instrument_completeness_unverified');
      if (onlyUnverified) return 'Полнота набора не проверена · движок не предоставил отчёты';
      return 'Неполный набор продукции' + (unverified ? ' · полнота приборов также не проверена' : '');
    }
    return 'Состояние полноты не распознано';
  }
  function qualityDetails(quality) {
    if (!quality || !Array.isArray(quality.instruments)) return '';
    const labels = {ok: 'обработка выполнена', partial: 'частичные данные',
      no_data: 'нет полных строк', no_products: 'продукция не сформирована'};
    return quality.instruments.filter(x => x && typeof x === 'object').map(row => {
      const parts = [instrument(row.id), labels[row.status] || 'состояние неизвестно'];
      if (count(row.lines) !== null) parts.push('строк: ' + row.lines);
      if (count(row.images) !== null) parts.push('изображений: ' + row.images);
      return parts.join(' · ');
    }).join('\n');
  }
  function healthSummary(health) {
    if (!health || typeof health !== 'object') return 'Состояние обработчика недоступно';
    if (health.worker_alive !== true) return 'Обработчик не отвечает · опубликованный архив доступен';
    const sources = health.sources && typeof health.sources === 'object' ? Object.values(health.sources) : [];
    const messages = [];
    if (sources.includes('low_disk')) messages.push('Недостаточно свободного места');
    if (sources.some(x => x !== 'low_disk')) messages.push('Входной источник недоступен');
    const failed = health.queue ? count(health.queue.failed) : null;
    if (failed) messages.push('Неуспешных заданий: ' + failed);
    if (!messages.length) messages.push('Обработчик работает');
    return messages.join(' · ');
  }
  function lastResult(result) {
    if (!result || !result.state) return '';
    const states = {done: 'опубликовано', pending: 'ожидает обработки или повтора',
      running: 'обрабатывается', failed: 'ошибка', superseded: 'заменено новым входом'};
    let message = 'Последнее задание: ' + (states[result.state] || 'неизвестное состояние');
    if (result.quality === 'partial') message += ' · есть предупреждения о полноте';
    return message;
  }
  return {displayTime, observation, qualitySummary, qualityDetails, healthSummary, lastResult};
}));
