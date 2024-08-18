const zeroCache = [];
const iotaCache = [];
export function zeros(n) {
    if (!zeroCache[n]) {
        const c = Array(n);
        for (let i = 0; i < n; i++) {
            c[i] = 0;
        }
        zeroCache[n] = c;
    }
    return zeroCache[n];
}
export function iota(n) {
    if (!iotaCache[n]) {
        const c = Array(n);
        for (let i = 0; i < n; i++) {
            c[i] = i;
        }
        iotaCache[n] = c;
    }
    return iotaCache[n];
}
export function identity(n) {
    return new Perm(iota(n));
}
export function random(n) {
    // random
    const c = Array(n);
    for (let i = 0; i < n; i++) {
        c[i] = i;
    }
    for (let i = 0; i < n; i++) {
        const j = i + Math.floor((n - i) * Math.random());
        const t = c[i];
        c[i] = c[j];
        c[j] = t;
    }
    return new Perm(c);
}
export function factorial(a) {
    let r = BigInt(1);
    while (a > 1) {
        r *= BigInt(a);
        a--;
    }
    return r;
}
function gcd(a, b) {
    if (a > b) {
        const t = a;
        a = b;
        b = t;
    }
    while (a > 0) {
        const m = b % a;
        b = a;
        a = m;
    }
    return b;
}
export function lcm(a, b) {
    return (a / gcd(a, b)) * b;
}
export class Perm {
    constructor(a) {
        this.n = a.length;
        this.p = a;
    }
    toString() {
        return String.fromCodePoint(...this.p);
    }
    at(index) {
        return this.p[index];
    }
    mul(p2) {
        // multiply
        const c = Array(this.n);
        for (let i = 0; i < this.n; i++) {
            c[i] = p2.at(this.at(i));
        }
        return new Perm(c);
    }
    rmul(p2) {
        // multiply the other way
        const c = Array(this.n);
        for (let i = 0; i < this.n; i++) {
            c[i] = this.at(p2.at(i));
        }
        return new Perm(c);
    }
    inv() {
        const c = Array(this.n);
        for (let i = 0; i < this.n; i++) {
            c[this.at(i)] = i;
        }
        return new Perm(c);
    }
    compareTo(p2) {
        // comparison
        for (let i = 0; i < this.n; i++) {
            if (this.p[i] !== p2.p[i]) {
                return this.p[i] - p2.p[i];
            }
        }
        return 0;
    }
    commutes(p2) {
        let p12 = this.mul(p2);
        let p21 = this.rmul(p2);
        return (p12.compareTo(p21) == 0);
    }
    toGap() {
        const cyc = new Array();
        const seen = new Array(this.n);
        for (let i = 0; i < this.p.length; i++) {
            if (seen[i] || this.at(i) === i) {
                continue;
            }
            const incyc = new Array();
            for (let j = this.at(i); !seen[j]; j = this.at(j)) {
                incyc.push(1 + j);
                seen[j] = true;
            }
            cyc.push(`(${incyc.reverse().join(",")})`);
        }
        return cyc.join("");
    }
    toMathematica() {
        const cyc = new Array();
        const seen = new Array(this.n);
        for (let i = 0; i < this.p.length; i++) {
            if (seen[i] || this.at(i) === i) {
                continue;
            }
            const incyc = new Array();
            for (let j = this.at(i); !seen[j]; j = this.at(j)) {
                incyc.push(1 + j);
                seen[j] = true;
            }
            cyc.push(`{${incyc.reverse().join(",")}}`);
        }
        return `Cycles[{${cyc.join(",")}}]`;
    }
    order() {
        let r = 1;
        const seen = new Array(this.n);
        for (let i = 0; i < this.p.length; i++) {
            if (seen[i] || this.at(i) === i) {
                continue;
            }
            let cs = 0;
            for (let j = i; !seen[j]; j = this.at(j)) {
                cs++;
                seen[j] = true;
            }
            r = lcm(r, cs);
        }
        return r;
    }
}
