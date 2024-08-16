import { schreierSims } from "./SchreierSims.js";
import { Perm } from "./Perm.js";
import { Puzzle } from "./Puzzle.js";
// test:
let pzl = new Puzzle([new Perm([1, 0, 3, 2, 4, 5]), new Perm([2, 3, 4, 5, 0, 1])], [[0, 1], [2, 3], [4, 5]]);
console.log(schreierSims);
console.log(Perm);
console.log(Puzzle);
