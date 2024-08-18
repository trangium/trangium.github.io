import { Perm } from "./Perm.js";
import { Puzzle, exec, solve } from "./util.js";
let U1 = { str: "U", perm: new Perm([6, 7, 0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19]), weight: 1 };
let U2 = { str: "U2", perm: U1.perm.mul(U1.perm), weight: 1 };
let U3 = { str: "U'", perm: U2.perm.mul(U1.perm), weight: 1 };
let D1 = { str: "D", perm: new Perm([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 18, 19, 12, 13, 14, 15, 16, 17]), weight: 1 };
let D2 = { str: "D2", perm: D1.perm.mul(D1.perm), weight: 1 };
let D3 = { str: "D'", perm: D2.perm.mul(D1.perm), weight: 1 };
let y1 = { str: "y", perm: new Perm([6, 7, 0, 1, 2, 3, 4, 5, 11, 8, 9, 10, 14, 15, 16, 17, 18, 19, 12, 13]), weight: 10 };
let R2 = { str: "R2", perm: new Perm([0, 1, 14, 15, 16, 5, 6, 7, 8, 10, 9, 11, 12, 13, 2, 3, 4, 17, 18, 19]), weight: 1 };
let B2 = { str: "B2", perm: y1.perm.rmul(R2.perm.rmul(y1.perm.rmul(y1.perm.rmul(y1.perm)))), weight: 1 };
let L2 = { str: "L2", perm: y1.perm.rmul(B2.perm.rmul(y1.perm.rmul(y1.perm.rmul(y1.perm)))), weight: 1 };
let F2 = { str: "F2", perm: y1.perm.rmul(L2.perm.rmul(y1.perm.rmul(y1.perm.rmul(y1.perm)))), weight: 1 };
let drhtr = new Puzzle([U1, U2, U3, D1, D2, D3, R2, B2, L2, F2], [U2.perm, D2.perm, R2.perm, B2.perm, L2.perm, F2.perm]);
let gen = solve(drhtr, 5, 11, exec(drhtr, "R2 U' R2 U' R2 U R2 D' R2 U R2 U' R2 D R2"), drhtr.identity);
let solCount = 0;
for (let val of gen) {
    console.log(val);
    solCount++;
}
console.log(solCount, "of 1024 solutions");
