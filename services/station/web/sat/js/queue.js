// Глубина очереди берётся из единого windowHours; продукты одного пролёта остаются рядом.
export function makeQueue(frames) {
  // Период, приоритеты, состав и последовательность уже рассчитаны бэкендом.
  // Часы клиентского компьютера не могут скрыть принятый сервером снимок.
  return frames.slice();
}

export function nextFrame(queue, current, direction = 1) {
  if (!queue.length) return null;
  const index = queue.findIndex(frame => frame.id === current?.id);
  if (index >= 0) return queue[(index + direction + queue.length) % queue.length];
  if (!current) return direction > 0 ? queue[0] : queue[queue.length - 1];
  return direction > 0 ? queue.find(frame => frame.startMs > current.startMs) || queue[0] : [...queue].reverse().find(frame => frame.startMs < current.startMs) || queue[queue.length - 1];
}

// Контекст нужен для номера продукта внутри пролёта и отметки самого свежего кадра.
export function frameContext(queue, frame) {
  const products = queue.filter(item => item.eventId === frame.eventId);
  const latest = queue.reduce((value, item) => Math.max(value, Number.isFinite(item.endMs) ? item.endMs : 0), 0);
  return { productIndex: Math.max(0, products.findIndex(item => item.id === frame.id)) + 1, productCount: Math.max(1, products.length), latest: typeof frame.latest === 'boolean' ? frame.latest : !!latest && frame.endMs === latest };
}
