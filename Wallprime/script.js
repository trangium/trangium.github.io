const canvas = document.getElementById('canvas');
const ctx = canvas.getContext('2d');

const minnum = 100;
const maxnum = 3500;
const maxtime = 240;
const timepenalty = 1;
const scorepenalty = 5;
const primes = [2, 3, 5, 7, 11, 13, 17, 19];

const FLAG_DIVIDE = -2;
const FLAG_RESET = -1;
const prin = primes.concat([FLAG_DIVIDE, FLAG_RESET]);
const rows = primes.length <= 10 ? 2 : primes.length <= 27 ? 3 : 4;
const szrc = Math.round(250 / (rows + 0.5));
const szrg = (250 - (szrc * rows)) / (rows - 1) + szrc;
const rects = [...Array(primes.length)].map((_, i) => [
  210 + szrg * Math.floor(i / rows),
  750 - (100 + szrg * (rows - 1 - i % rows) + szrc), 
  szrc,
  szrc,
]).concat([
  [60, 400, 100, 100], 
  [60, 550, 100, 100] 
]);
const ptxsz = Math.floor(180 / (rows + 1.5));
const rtmin = 35;
const rtmax = 70;
let rtouch = Array(rects.length).fill(1);
let z, corranim, score, newanim, scdta, time = 0, origz;
let buffer = [];
const green = [0, 0.9, 0.1];
const llgray = [0.9, 0.9, 0.9];

function randz() {
  let acc = 1;
  let accbuf = [];
  let prev = origz;
  let cand = [];
  while (acc < maxnum) {
    accbuf.push(primes[Math.floor(Math.random() * primes.length)]);
    acc *= accbuf[accbuf.length - 1];
    if (minnum <= acc && acc <= maxnum && acc !== prev) {
      cand.push(acc);
    }
    if (acc >= maxnum && cand.length === 0) {
      acc = 1;
    }
  }
  return cand[Math.floor(Math.random() * cand.length)];
}

function factor_input(prime) {
  let prod = buffer.reduce((a, b) => a * b, 1);
  if (time === 0) {
    if (prime === FLAG_RESET) {
      score = 0;
      time = maxtime * 60;
      corranim = 0;
      newanim = 0;
      scdta = 0;
      buffer = [];
      z = origz = randz();
    } else if (prime === FLAG_DIVIDE) {
      if (z % prod === 0) {
        z /= prod;
      }
      buffer = [];
    } else {
      buffer.push(prime);
    }
    return;
  }
  if (prime === FLAG_DIVIDE) {
    if (buffer.length === 0) return;
    let bonus = z === origz;
    if (z % prod === 0) {
      z /= prod;
      corranim = 1;
      scdta = Math.floor(Math.log2(prod) * (z === 1 ? (bonus ? 10 : 5) : 1));
      score += scdta;
    } else {
      score -= scorepenalty;
      scdta = -scorepenalty;
      time -= 60 * timepenalty;
    }
    buffer = [];
    if (z === 1) {
      z = origz = randz();
      corranim = 1;
      newanim = 1;
    }
  } else if (prime === FLAG_RESET) {
    if (buffer.length > 0) buffer.pop();
  } else {
    buffer.push(prime);
  }
}

factor_input(FLAG_RESET);

canvas.addEventListener('mousedown', (e) => {
  let rect = canvas.getBoundingClientRect();
  let x = e.clientX - rect.left;
  let y = e.clientY - rect.top;
  for (let i = 0; i < rects.length; i++) {
    let r = rects[i];
    if (r[0] < x && x < r[0] + r[2] && r[1] < y && y < r[1] + r[3]) {
      if (time) rtouch[i] = 0;
      factor_input(prin[i]);
    }
  }
});

document.addEventListener('keydown', function(event) {
    if (event.key === 'Enter') {
        factor_input(FLAG_DIVIDE);
    } else if (event.key === 'Backspace') {
        if (buffer[buffer.length - 1] > 10) {
            buffer[buffer.length - 1] = 1;
        } else {
            if (buffer.length > 0) buffer.pop();
        }
    } else if (buffer[buffer.length - 1] === 1) {
        if (event.key === "1") {
            buffer[buffer.length - 1] = 11;
        } else if (event.key === "3") {
            buffer[buffer.length - 1] = 13;
        } else if (event.key === "7") {
            buffer[buffer.length - 1] = 17;
        } else if (event.key === "9") {
            buffer[buffer.length - 1] = 19;
        } else {
            buffer.pop();
        }
    } else {
        if (event.key === "2") {
            buffer.push(2);
        } else if (event.key === "3") {
            buffer.push(3);
        } else if (event.key === "5") {
            buffer.push(5);
        } else if (event.key === "7") {
            buffer.push(7);
        } else if (event.key === "1") {
            buffer.push(1);
        }
    }
  });

function draw() {
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  for (let i = 0; i < rects.length; i++) {
    let r = rects[i];
    rtouch[i] = 1 - ((1 - rtouch[i]) * 0.85);
    let prime = prin[i];
    let cellfill = rtmin * (1 - rtouch[i]) + rtmax * rtouch[i] * (prime === FLAG_DIVIDE ? 1.7 : prime === FLAG_RESET ? 0.8 : 1);
    ctx.fillStyle = `rgba(${cellfill}, ${cellfill}, ${cellfill}, 1)`;
    ctx.strokeStyle = 'rgba(0, 255, 0, 1)';
    ctx.lineWidth = 3;
    ctx.strokeRect(r[0], r[1], r[2], r[3]);
    ctx.fillRect(r[0], r[1], r[2], r[3]);
    let centerx = r[0] + r[2] / 2;
    let centery = r[1] + r[3] / 2;
    ctx.fillStyle = `rgba(${llgray[0] * 255}, ${llgray[1] * 255}, ${llgray[2] * 255}, ${time ? 1 : 0.68})`;
    ctx.font = `${ptxsz}px Trebuchet MS`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    if (prime > 0) {
      ctx.fillText(primes[i], centerx, centery);
    } else if (prime === FLAG_DIVIDE) {
      ctx.fillStyle = `rgba(51, 51, 51, ${time ? 1 : 0.68})`;
      ctx.fillText('÷', centerx, centery);
    } else if (prime === FLAG_RESET) {
      ctx.fillStyle = `rgba(20, 20, 20, ${time ? 1 : 0.68})`;
      ctx.fillText('↺', centerx, centery);
    }
  }
  ctx.fillStyle = `rgba(255, 255, 255, ${time ? 1 : 0.6})`;
  ctx.font = '35px Trebuchet MS';
  ctx.textAlign = 'left';
  ctx.fillText(`Score: ${score}`, 540, 130); 
  if (buffer.length > 0) {
    ctx.fillText(buffer.join(' x '), 100, 310); 
  }
  ctx.fillText('Time:', 540, 180); 
  ctx.textAlign = 'center';
  ctx.fillStyle = 'rgba(102, 102, 153, 1)';
  ctx.fillRect(647, 163, 125, 30); 
  ctx.fillStyle = 'rgba(255, 255, 255, 1)';
  ctx.fillRect(647, 163, (time / (maxtime * 60)) * 125, 30); 
  ctx.fillStyle = `rgba(${255 - corranim * 255}, 255, ${255 - corranim * 255}, ${time ? 1 : 0.4})`;
  ctx.font = '150px Trebuchet MS';
  ctx.fillText(z, 250, 160 - 260 * newanim);
  ctx.fillStyle = `rgba(${128 - corranim * 64}, 128, ${128 - corranim * 64}, 1)`;
  ctx.fillText('1', 250 - 290 * (1 - newanim), 160);
  ctx.fillStyle = `rgba(0, ${corranim ** 0.22 * 255}, 0, 1)`;
  ctx.font = '35px Trebuchet MS';
  ctx.fillText(`${scdta >= 0 ? '+' : ''}${scdta}`, 660, 90); 
  if (newanim === 0) corranim *= 0.93;
  newanim = Math.max(0, newanim - 0.06);
  time = Math.max(0, time - 1);
  requestAnimationFrame(draw);
}

draw();