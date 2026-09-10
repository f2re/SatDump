import test from 'node:test';
import assert from 'node:assert/strict';
import { normalizeManifest, localURL, ManifestSource } from './manifest.js';
import { makeQueue, nextFrame } from './queue.js';
import { decodeInto } from './preload.js';
import { SatellitePlayer, frameDuration } from './player.js';

const now = Date.parse('2026-09-05T14:00:00Z');
const base = 'http://localhost/sat/data/manifest.json';
const config = { windowHours: 24, productPriority: ['cloudtopir', 'mcir', 'msa'] };
const raw = (id, changes = {}) => ({ id, satellite: 'METOP-C', instrument: 'AVHRR/3', eventId: 'event-a',
  start: '2026-09-05T12:00:00Z', end: '2026-09-05T12:10:00Z',
  product: { code: 'CloudTopIR', title: 'Температура верхней границы облаков', family: 'thermal-ir' },
  image: { width: 720, height: 1080 }, urls: { imagery: `frames/${id}/imagery.png`, legend: `frames/${id}/legend.png` }, ...changes });

test('SatDump nested schema preserves product and supplied legend while resolving local paths', () => {
  const [frame] = normalizeManifest({ frames: [raw('a')] }, base).frames;
  assert.equal(frame.product, 'CloudTopIR');
  assert.equal(frame.title, 'Температура верхней границы облаков');
  assert.equal(frame.display, 'http://localhost/sat/data/frames/a/imagery.png');
  assert.match(frame.legend.display, /frames\/a\/legend.png$/);
});

test('Rejects unsafe URLs but retains large and undated server images', () => {
  for (const url of ['https://cdn.example/x.png', '//other.example/x.png', 'data:image/png;base64,AA', 'http://name:password@localhost/x']) assert.equal(localURL(url, base), null);
  const frames = normalizeManifest({ frames: [raw('remote', { display: 'https://other.example/x.png' }), raw('big', { image: { width: 2000, height: 2000 } }), raw('undated', { start: null }), raw('ok')] }, base).frames;
  assert.deepEqual(frames.map(frame => frame.id), ['big', 'undated', 'ok']);
});

test('Preserves server order and never re-filters publication against the browser clock', () => {
  const frames = normalizeManifest({ frames: [raw('mcir', { product: 'MCIR', start: '2026-09-05T11:59:00Z' }), raw('ir'), raw('later', { eventId: 'event-b', start: '2026-09-05T13:00:00Z', end: '2026-09-05T13:01:00Z' }), raw('old', { start: '2026-09-04T13:00:00Z', end: '2026-09-04T13:10:00Z' }), raw('future', { start: '2026-09-06T13:00:00Z', end: '2026-09-06T13:10:00Z' })] }, base).frames;
  const queue = makeQueue(frames, config, now);
  assert.deepEqual(queue.map(frame => frame.id), frames.map(frame => frame.id));
  assert.equal(nextFrame(queue, queue[queue.length - 1]).id, queue[0].id);
  assert.equal(nextFrame(queue, { id: 'removed', startMs: now - 7200000 }).id, 'later');
});

test('Conditional request uses server ETag; 304 leaves last valid manifest untouched', async () => {
  const previousFetch = globalThis.fetch;
  let count = 0;
  globalThis.fetch = async (_url, options) => {
    count += 1;
    if (count === 1) return new Response(JSON.stringify({ frames: [raw('a')] }), { headers: { ETag: '"version-1"' } });
    assert.equal(options.headers['If-None-Match'], '"version-1"');
    return new Response(null, { status: 304 });
  };
  try {
    const source = new ManifestSource(base);
    assert.equal((await source.refresh()).frames.length, 1);
    assert.equal(await source.refresh(), null);
  } finally { globalThis.fetch = previousFetch; }
});

test('Inactive buffer becomes ready only after decode; failed image releases its source', async () => {
  let finishDecode;
  const image = { naturalWidth: 100, naturalHeight: 100, removeAttribute() { this.src = ''; }, decode() { return new Promise(resolve => { finishDecode = resolve; }); } };
  let ready = false;
  const promise = decodeInto(image, { display: '/image.png' }, 1000).then(() => { ready = true; });
  image.onload();
  await Promise.resolve();
  assert.equal(ready, false);
  finishDecode();
  await promise;
  assert.equal(ready, true);
  const failure = decodeInto(image, { display: '/missing.png' }, 1000);
  image.onerror();
  await assert.rejects(failure, /недоступен/);
  assert.equal(image.src, '');
});

test('Manifest removal during decode cannot replace a valid current image', async () => {
  const previousDocument = globalThis.document;
  globalThis.document = { hidden: false };
  const makeImage = () => ({ naturalWidth: 100, naturalHeight: 100, removeAttribute() { this.src = ''; }, async decode() {} });
  const renderer = { images: [makeImage(), makeImage()], pending() {} };
  const player = new SatellitePlayer(renderer, {}, { ...config, messages: {}, imageTimeoutSeconds: 1 });
  const [candidate] = normalizeManifest({ frames: [raw('candidate', { start: new Date(Date.now() - 3600000).toISOString(), end: new Date(Date.now() - 3500000).toISOString() })] }, base).frames;
  player.current = { id: 'last-good' };
  player.frames = [candidate];
  player.deadline = Infinity;
  try {
    const preparation = player.prepare(candidate, false);
    player.frames = [];
    renderer.images[1].onload();
    await preparation;
    assert.equal(player.current.id, 'last-good');
    assert.equal(player.prepared, null);
    assert.equal(renderer.images[1].src, '');
  } finally { clearTimeout(player.timer); globalThis.document = previousDocument; }
});

test('Single-frame content revision remains eligible for a normal transition', () => {
  const player = new SatellitePlayer({}, {}, { ...config, messages: {} });
  player.current = { id: 'stable', display: '/old.png', version: 'a' };
  assert.equal(player.isCurrent({ ...player.current }), true);
  assert.equal(player.isCurrent({ ...player.current, display: '/new.png', version: 'b' }), false);
});

test('Задержки соблюдают минимум и приоритет свежего кадра', () => {
  const timing = { minimumDisplaySeconds: 25, displaySeconds: 30, complexDisplaySeconds: 40, latestSeconds: 45, complexProducts: ['mcir'] };
  assert.equal(frameDuration({ product: 'msa', legend: {} }, { latest: false }, timing), 30);
  assert.equal(frameDuration({ product: 'mcir', legend: {} }, { latest: false }, timing), 40);
  assert.equal(frameDuration({ product: 'msa', legend: { kind: 'composite' } }, { latest: false }, timing), 40);
  assert.equal(frameDuration({ product: 'mcir', legend: {} }, { latest: true }, timing), 45);
  assert.equal(frameDuration({ product: 'msa', legend: {} }, { latest: false }, { ...timing, displaySeconds: 5 }), 5); // Test injection proves no second client policy; real server rejects 5.
});


test('Decode accepts any backend-approved dimensions including >4K without a second cap', async () => {
  for (const [naturalWidth,naturalHeight] of [[4096,1200],[900,5000],[32,32]]) {
    const image={naturalWidth,naturalHeight,removeAttribute(){},async decode(){}};
    const result=decodeInto(image,{display:'/approved.png'},1000);image.onload();await result;
  }
});

test('Rejected configuration cannot poison ETag retries; server Date also works on 304', async () => {
  const previous=globalThis.fetch;
  globalThis.fetch=async()=>new Response(JSON.stringify({frames:[raw('a')]}),{headers:{ETag:'"new"',Date:'Thu, 10 Sep 2026 12:00:00 GMT'}});
  try {
    const source=new ManifestSource(base,()=>{throw new Error('config unavailable');});
    await assert.rejects(source.refresh(),/config unavailable/);assert.equal(source.etag,null);
    assert.ok(Number.isFinite(source.clockOffset));
  } finally {globalThis.fetch=previous;}
});

test('Supplied backend metadata is preserved even in English or outside old product dictionaries', () => {
  const source=raw('a',{product:{code:'arbitrary',title:'Backend name',description:'Not a retrieved cloud-top field',purpose:'Provided use'}});
  const frame=normalizeManifest({frames:[source]},base).frames[0];
  assert.equal(frame.title,source.product.title);assert.equal(frame.description,source.product.description);assert.equal(frame.purpose,source.product.purpose);
});
