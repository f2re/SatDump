// Повторно использует неактивный DOM-буфер: не создаёт Image, Blob или canvas.
// Размер не ограничивается браузером: сервер публикует уже проверенный файл.
export function decodeInto(image, frame, timeoutMilliseconds = 20000) {
  return new Promise((resolve, reject) => {
    let settled = false;
    const done = error => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      image.onload = null;
      image.onerror = null;
      if (error) { image.removeAttribute('src'); reject(error); } else resolve(frame);
    };
    const timer = setTimeout(() => done(new Error('Превышено время загрузки кадра')), timeoutMilliseconds);
    image.onerror = () => done(new Error('Кадр недоступен'));
    image.onload = async () => {
      try {
        if (image.decode) await image.decode();
        done();
      } catch (error) { done(error); }
    };
    image.src = frame.display;
  });
}
