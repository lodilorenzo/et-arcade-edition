// One trailer-style typed line; full text reserves its final size and stays accessible.
const line = document.querySelector('.hero [data-type]');
if (!matchMedia('(prefers-reduced-motion: reduce)').matches && 'IntersectionObserver' in window) {
  const text = line.textContent;
  const sizer = document.createElement('span');
  sizer.className = 'type-sizer';
  sizer.textContent = text;
  const ink = document.createElement('span');
  ink.className = 'type-ink';
  ink.setAttribute('aria-hidden', 'true');
  line.replaceChildren(sizer, ink);
  line.classList.add('typing');
  const seen = new IntersectionObserver(entries => {
    if (!entries.some(entry => entry.isIntersecting)) return;
    seen.disconnect();
    let i = 0;
    const tick = setInterval(() => {
      ink.textContent = text.slice(0, ++i);
      if (i >= text.length) {
        clearInterval(tick);
        line.textContent = text;
        line.classList.remove('typing');
      }
    }, 38);
  });
  seen.observe(line);
}

const menu = document.querySelector('.mobile-nav');
menu.addEventListener('click', event => {
  if (event.target.closest('a')) menu.open = false;
});
document.addEventListener('keydown', event => {
  if (event.key === 'Escape' && menu.open) {
    menu.open = false;
    menu.querySelector('summary').focus();
  }
});

// Phones held upright get the portrait cut; nothing is downloaded before play (preload=none).
const video = document.querySelector('#trailer video');
if (matchMedia('(orientation: portrait) and (max-width: 720px)').matches) {
  video.poster = video.dataset.portraitPoster;
  video.querySelector('source').src = video.dataset.portraitSrc;
  video.classList.add('portrait');
  video.load();
}

// If autoplay is blocked, the native player controls remain available.
document.querySelector('[data-play]').addEventListener('click', () => {
  video.play().catch(() => {});
});
