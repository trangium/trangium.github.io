export class Gelem {
    public n: number; // length
    public p: number[]; // compact repr of the permutation itself

    constructor(a: number[]) {
        this.n = a.length;
        this.p = a;
    }
}