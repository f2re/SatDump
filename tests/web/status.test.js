const test = require('node:test');
const assert = require('node:assert/strict');
const {execFileSync} = require('node:child_process');
const path = require('node:path');
const fs = require('node:fs');
const S = require('../../services/station/web/status.js');
const root = path.resolve(__dirname, '../..');

test('UTC does not depend on the browser or process timezone', () => {
  const script = 'process.stdout.write(require(process.argv[1]).displayTime(1790791800))';
  const values = ['UTC', 'Europe/Vienna', 'America/New_York'].map(TZ =>
    execFileSync(process.execPath, ['-e', script, path.join(root, 'services/station/web/status.js')],
      {encoding: 'utf8', env: {...process.env, TZ}}));
  assert.equal(new Set(values).size, 1);
  assert.match(values[0], / UTC$/);
});

test('invalid epochs are not formatted as observation times', () => {
  for (const value of [undefined, null, '123', Infinity, NaN, 1e20]) assert.equal(S.displayTime(value), '—');
});

test('missing observation does not silently use file mtime', () => {
  const text = S.observation({file_mtime: 1790791800, acquisition_time_status: 'missing'});
  assert.match(text, /^Время наблюдения не указано/);
  assert.match(text, /файл изменён/);
  assert.doesNotMatch(text, /^Наблюдение:/);
});

test('invalid observation and missing observation remain distinct', () => {
  assert.match(S.observation({acquisition_time_status: 'invalid', acquisition_time: 'bad'}), /некорректно/);
  assert.equal(S.observation({acquisition_time_status: 'ok', acquisition_time: '30.09.2026 · 18:10:00 UTC'}),
    'Наблюдение: 30.09.2026 · 18:10:00 UTC');
});

test('a legacy unverified report is not presented as confirmed loss', () => {
  const text = S.qualitySummary({status: 'partial', warnings: ['instrument_completeness_unverified']});
  assert.match(text, /^Полнота набора не проверена/);
  assert.doesNotMatch(text, /Неполный/);
  assert.match(S.qualitySummary(null), /не проверена/);
});

test('real partial output and unverified coverage can coexist', () => {
  const text = S.qualitySummary({status: 'partial', warnings: ['mtvza:no_data', 'instrument_completeness_unverified']});
  assert.match(text, /^Неполный набор/);
  assert.match(text, /также не проверена/);
});

test('external PNG and successful software processing do not claim physical validation', () => {
  assert.match(S.qualitySummary({status: 'external'}), /не проверялась/);
  assert.equal(S.qualitySummary({status: 'ok'}), 'В отчёте обработки нет предупреждений');
  assert.match(S.qualitySummary({status: 'future-status'}), /не распознано/);
});

test('instrument rows use actual counts and separate lines', () => {
  const text = S.qualityDetails({instruments: [
    {id: 'mtvza', status: 'no_data', images: 0, lines: 0},
    {id: 'msu_mr', status: 'ok', images: 4, lines: 100},
    null
  ]});
  assert.equal(text.split('\n').length, 2);
  assert.match(text, /МТВЗА-ГЯ · нет полных строк · строк: 0 · изображений: 0/);
  assert.match(text, /МСУ-МР/);
});

test('unavailable inputs and failed jobs are not hidden by last-result quality', () => {
  const health = {worker_alive: true, sources: {raw: 'unavailable', files: 'low_disk'},
    queue: {failed: 3}, last_result: {state: 'done', quality: 'partial'}};
  const text = S.healthSummary(health);
  assert.match(text, /Недостаточно свободного места/);
  assert.match(text, /Входной источник недоступен/);
  assert.match(text, /Неуспешных заданий: 3/);
  assert.match(S.lastResult(health.last_result), /предупреждения о полноте/);
});

test('dead worker takes precedence over a previous successful job', () => {
  assert.match(S.healthSummary({worker_alive: false, last_result: {state: 'done'}}), /^Обработчик не отвечает/);
  assert.equal(S.lastResult({}), '');
  assert.match(S.lastResult({state: 'pending'}), /повтора/);
});

test('HTML loads local scripts in dependency order and includes offline help', () => {
  const html = fs.readFileSync(path.join(root, 'services/station/web/index.html'), 'utf8');
  const scripts = [...html.matchAll(/<script\s+src="([^"]+)"\s+defer>/g)].map(x => x[1]);
  assert.deepEqual(scripts, ['status.js', 'app.js']);
  assert.match(html, /<details id="help">/);
  assert.match(html, /<noscript>/);
  for (const id of ['health', 'lastResult', 'qualityExplanation', 'processingMetadata', 'metadata'])
    assert.equal((html.match(new RegExp('id="' + id + '"', 'g')) || []).length, 1);
});

test('UI renders data as text and refreshes changed quality with the same image ids', () => {
  const js = fs.readFileSync(path.join(root, 'services/station/web/app.js'), 'utf8');
  assert.doesNotMatch(js, /innerHTML|insertAdjacentHTML|eval\(/);
  assert.match(js, /const digest = JSON\.stringify\(catalog\.items\)/);
  assert.match(js, /text\('health', StationText\.healthSummary\(health\)\)/);
  assert.match(js, /text\('lastResult'/);
});
