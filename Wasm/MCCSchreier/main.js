let n = 2;
let initVec = [0, Infinity];
const moves = new Map(); // name -> Float32Array, flat row-major, length n*n

// ── Worker communication ──────────────────────────────────────────────────────

worker.addEventListener('message', ({ data }) => {
    if (data.type !== 'result' && data.type !== 'error') return;
    const el = document.getElementById('result-display');
    const status = document.getElementById('status');
    status.textContent = '';
    el.className = '';
    if (data.type === 'result') {
        const v = data.value;
        if (!isFinite(v)) {
            el.textContent = '∞';
            el.className = 'inf';
        } else {
            el.textContent = parseFloat(v.toPrecision(6));
        }
    } else {
        el.textContent = data.message;
        el.className = 'err';
    }
});

// ── Helpers ───────────────────────────────────────────────────────────────────

function parseCell(s) {
    s = s.trim();
    if (!s || /^inf/i.test(s) || s === '∞') return Infinity;
    const v = parseFloat(s);
    return isNaN(v) ? Infinity : v;
}

function cellStr(v) { return isFinite(v) ? String(v) : ''; }

function makeInput(val, onChange) {
    const inp = document.createElement('input');
    inp.type = 'text';
    inp.className = 'cell';
    inp.placeholder = '∞';
    inp.value = cellStr(val);
    inp.addEventListener('change', () => {
        const v = parseCell(inp.value);
        inp.value = cellStr(v);
        onChange(v);
    });
    return inp;
}

// ── Render ────────────────────────────────────────────────────────────────────

function renderVector() {
    const container = document.getElementById('vector-inputs');
    container.innerHTML = '';
    initVec.forEach((v, i) => {
        container.appendChild(makeInput(v, val => { initVec[i] = val; }));
    });
}

function renderMoves() {
    const container = document.getElementById('moves-list');
    container.innerHTML = '';
    for (const [name, flat] of moves) container.appendChild(makeMoveCard(name, flat));
}

function makeMoveCard(moveName, flat) {
    const card = document.createElement('div');
    card.className = 'move-card';
    card.dataset.name = moveName;

    const header = document.createElement('div');
    header.className = 'move-header';

    const nameInp = document.createElement('input');
    nameInp.className = 'move-name';
    nameInp.value = moveName;
    nameInp.title = 'Move name';
    nameInp.addEventListener('change', () => {
        const oldName = card.dataset.name;
        const newName = nameInp.value.trim();
        if (!newName || newName === oldName) { nameInp.value = oldName; return; }
        if (moves.has(newName)) { nameInp.value = oldName; return; }
        moves.set(newName, moves.get(oldName));
        moves.delete(oldName);
        card.dataset.name = newName;
    });

    const delBtn = document.createElement('button');
    delBtn.textContent = '×';
    delBtn.className = 'del';
    delBtn.title = 'Delete move';
    delBtn.addEventListener('click', () => { moves.delete(card.dataset.name); card.remove(); });

    header.appendChild(nameInp);
    header.appendChild(delBtn);
    card.appendChild(header);

    const wrap = document.createElement('div');
    wrap.className = 'matrix-wrap';
    const grid = document.createElement('div');
    grid.className = 'matrix-grid';
    grid.style.gridTemplateColumns = `repeat(${n}, 54px)`;

    for (let i = 0; i < n; i++) {
        for (let j = 0; j < n; j++) {
            const idx = i * n + j;
            grid.appendChild(makeInput(flat[idx], val => { flat[idx] = val; }));
        }
    }
    wrap.appendChild(grid);
    card.appendChild(wrap);
    return card;
}

// ── Dimension change ──────────────────────────────────────────────────────────

function setDimension(newN) {
    newN = Math.max(1, newN | 0);
    if (newN === n) return;
    const oldN = n;
    n = newN;

    initVec = Array.from({ length: n }, (_, i) => i < initVec.length ? initVec[i] : Infinity);

    for (const [name, oldFlat] of moves) {
        const newFlat = new Float32Array(n * n).fill(Infinity);
        for (let i = 0; i < Math.min(oldN, n); i++)
            for (let j = 0; j < Math.min(oldN, n); j++)
                newFlat[i * n + j] = oldFlat[i * oldN + j];
        moves.set(name, newFlat);
    }

    renderVector();
    renderMoves();
}

// ── Event listeners ───────────────────────────────────────────────────────────

document.getElementById('n-input').addEventListener('change', e => {
    setDimension(parseInt(e.target.value) || 1);
});

document.getElementById('add-move-btn').addEventListener('click', () => {
    let name = 'M';
    let i = 1;
    while (moves.has(name + i)) i++;
    name = name + i;
    const flat = new Float32Array(n * n).fill(Infinity);
    moves.set(name, flat);
    document.getElementById('moves-list').appendChild(makeMoveCard(name, flat));
});

function doEvaluate() {
    const sequence = document.getElementById('algo-input').value.trim().split(/\s+/).filter(Boolean);
    if (!sequence.length) return;

    const movesObj = {};
    for (const [name, flat] of moves) movesObj[name] = Array.from(flat);

    document.getElementById('result-display').textContent = '…';
    document.getElementById('result-display').className = '';
    document.getElementById('status').textContent = 'computing…';

    worker.postMessage({
        type: 'evaluate',
        initialVector: initVec.slice(),
        moves: movesObj,
        sequence,
    });
}

document.getElementById('eval-btn').addEventListener('click', doEvaluate);
document.getElementById('algo-input').addEventListener('keydown', e => {
    if (e.key === 'Enter') doEvaluate();
});

// ── Init ──────────────────────────────────────────────────────────────────────

renderVector();
