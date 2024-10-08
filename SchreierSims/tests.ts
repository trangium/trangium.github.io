import {Perm} from "./Perm.js"
import {Move, Puzzle, exec, solve} from "./util.js";
import { schreierSims } from "./SchreierSims.js";

let U1: Move = {str: "U", perm: new Perm([6, 7, 0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19]), weight: 1}
let U2: Move = {str: "U2", perm: U1.perm.mul(U1.perm), weight: 1}
let U3: Move = {str: "U'", perm: U2.perm.mul(U1.perm), weight: 1}
let D1: Move = {str: "D", perm: new Perm([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 18, 19, 12, 13, 14, 15, 16, 17]), weight: 1}
let D2: Move = {str: "D2", perm: D1.perm.mul(D1.perm), weight: 1}
let D3: Move = {str: "D'", perm: D2.perm.mul(D1.perm), weight: 1}
let y1: Move = {str: "y", perm: new Perm([6, 7, 0, 1, 2, 3, 4, 5, 11, 8, 9, 10, 14, 15, 16, 17, 18, 19, 12, 13]), weight: 10}
let R2: Move = {str: "R2", perm: new Perm([0, 1, 14, 15, 16, 5, 6, 7, 8, 10, 9, 11, 12, 13, 2, 3, 4, 17, 18, 19]), weight: 1}
let B2: Move = {str: "B2", perm: y1.perm.rmul(R2.perm.rmul(y1.perm.rmul(y1.perm.rmul(y1.perm)))), weight: 1}
let L2: Move = {str: "L2", perm: y1.perm.rmul(B2.perm.rmul(y1.perm.rmul(y1.perm.rmul(y1.perm)))), weight: 1}
let F2: Move = {str: "F2", perm: y1.perm.rmul(L2.perm.rmul(y1.perm.rmul(y1.perm.rmul(y1.perm)))), weight: 1}

let start = performance.now();
let drhtr = new Puzzle([U1, U2, U3, D1, D2, D3, R2, B2, L2, F2]);
let sgs = schreierSims([U2.perm, D2.perm, R2.perm, B2.perm, L2.perm, F2.perm]);

let s1 = performance.now()-start;


let gen = solve(drhtr, sgs, 5, 11, exec(drhtr, "R2 U' R2 U' R2 U R2 D' R2 U R2 U' R2 D R2"), 
    drhtr.identity);

let solCount = 0;
for (let val of gen) {
    if (solCount === 1023) console.log(val);
    solCount++;
}

let s2 = performance.now()-s1;
console.log(solCount, "of 1024 solutions");
console.log("initialization (ms):", s1);
console.log("computation (ms):", s2);

console.log(sgs);
