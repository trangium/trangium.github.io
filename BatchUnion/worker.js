// note: NOT the same as BatchSolver/worker.js
// this is for testing purposes only

self.onmessage = function (msg) {
    main();
};

function main() {
    let ht = 2;
    let rc = new Puzzle([2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3],
        [[3, 0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 15, 12, 13, 14, 16, 17, 18, 19],
        [0, 5, 2, 3, 1, 9, 6, 7, 8, 4, 10, 11, 12, 78, 49, 15, 16, 82, 45, 19],
        [0, 1, 38, 3, 4, 34, 40, 7, 37, 9, 10, 11, 12, 13, 79, 48, 81, 46, 18, 19],
        [0, 1, 2, 3, 4, 5, 6, 7, 11, 8, 9, 10, 12, 13, 14, 15, 19, 16, 17, 18],
        [0, 1, 2, 7, 4, 5, 3, 11, 8, 9, 10, 6, 51, 13, 14, 76, 47, 17, 18, 80],
        [36, 1, 2, 3, 42, 5, 6, 32, 8, 9, 39, 11, 77, 50, 14, 15, 16, 17, 83, 44]],
        ["U", "R", "F", "D", "L", "B"], null, new Map([['U2', ht], ['F2', ht], ['L2', ht], ['D2', ht], ['R2', ht], ['B2', ht]]));
        // normal rubik's cube

    rc.createPrun(5);

    let scr = setup(rc, "R2 L2 U2 D");

    for (let solution of rc.solve(scr, 5)) {
        postMessage({value: solution, type: "solution"});
    }

    let a = setPuzzles(`U: (UF UL UB UR) (UFR UFL UBL UBR)
R: (UR BR DR FR) (UFR-1 UBR+1 DBR-1 DFR+1)
F: (UF+1 FR+1 DF+1 FL+1) (UFR+1 DFR-1 DFL+1 UFL-1)
D: (DF DR DB DL) (DFR DBR DBL DFL)
L: (UL FL DL BL) (UFL+1 DFL-1 DBL+1 UBL-1)
B: (UB+1 BL+1 DB+1 BR+1) (UBR-1 UBL+1 DBL-1 DBR+1)
r: (UR BR DR FR) (UFR-1 UBR+1 DBR-1 DFR+1) (UF+1 UB+1 DB+1 DF+1) (FB UD+1 FB+1)`, `1: {UF UL UB UR} {UFR UFL UBL UBR}`, [
            {subgroup: `R U F r`, prune: `10k`, search: `=`},
            {subgroup: `L D B`, prune: `10k`, search: `=`},
        ], ``)

    postMessage({value: a, type: "solution"})

}

function setup(puzzle, alg) {
    return puzzle.execute(puzzle.solved, puzzle.moveStrToList(alg));
}

function arraysEqual(arr1,arr2) {
    for (let i=0; i<arr1.length; i++) {
        if (arr1[i] !== arr2[i]) {return false}
    }
    return true
}

function lastAlpha(move) {
    let needle = move.length-1;
    while (needle >= 0) {
        if (/[a-zA-Z]/.test(move[needle])) {
            return needle;
        }
        needle--;
    }
    return -1;
}

/**
 * @param {*} moveWeights 
 * @returns An array serving as a lookup table where the indices are move IDs, and the entry
 * at each index is the move to be searched next after that move, or -1 if that move ID is
 * the most expensive, with the cheapest move appended at the end.
 * 
 * The next move is the move with the next highest weight, or with the same weight and to the right.
 * 
 * getMoveNexts(moveWeights).length === moveWeights.length + 1.
 * 
 * @example
 * this.moveNexts = [2, 4, 3, 5, 7, 6, 8, 10, 9, 11, 13, 12, 14, 16, 15, 17, -1, 1, 0]; // for 3x3x3 SQTM
 * this.moveNexts = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, -1, 0]; // for 3x3x3 STM
 */
function getMoveNexts(moveWeights) {
    let moveOrder = moveWeights.map((elem, ind) => ([ind, elem])).sort((iep1, iep2) => (iep1[1]-iep2[1])).map(c => c[0]);
    let moveNexts = [];
    for (let i=0; i<moveWeights.length; i++) {
        let moveRanking = moveOrder.indexOf(i)+1;
        moveNexts.push(moveRanking === moveWeights.length ? -1 : moveOrder[moveRanking])
    }
    return moveNexts.concat(moveOrder[0]);
}

class Puzzle {
    // executes one move on specified start cube and outputs in result cube.
    mult (start, move, result) {
        for (let p = 0; p < this.pcCount; p++) {
            let temp = (start[move[p] & this.posMask] + (move[p] & this.oriMask));
            let tempOri = this.cubeOri[temp & this.posMask];
            result[p] = temp % (tempOri << this.posBits);
        }
    }
    
    constructor (cubeOri, clockwiseMoves, clockwiseMoveStr, solvedState=null, moveWeightsMap=new Map()) {
        // initialize cube constants
        this.cubeOri = cubeOri;
        this.pcCount = cubeOri.length; // 20
        this.posBits = Math.ceil(Math.log2(this.pcCount)) // 5; number of bits needed to store piece (without orientation)
        this.posMask = (1 << this.posBits) - 1; // 31
        this.oriMask = ((1 << Math.ceil(Math.log2(Math.max(...cubeOri)))) - 1) << this.posBits; // 3 * 32 = 96
        this.totalBits = Math.ceil(Math.log2(this.oriMask)); // 7
        this.clockwiseMoves = clockwiseMoves.slice();
        this.clockwiseMoveStr = clockwiseMoveStr.slice();
        this.moveStr = [];

        this.nullmove = [];
        for (let i=0; i<this.pcCount; i++) {this.nullmove[i] = i}
        if (solvedState === null) {
            this.solved = this.nullmove;
        } else {this.solved = solvedState}

        // initialize moves in all directions (ex. clockwise, counter, and double) and inverse mapping
        this.moves = []; // clockwise, counter, and double moves
        this.inverse = []; // mapping of inverse moves
        for (let i=0; i<clockwiseMoves.length; i++) { // initialize the moves array and inverse array
            this.moves.push(clockwiseMoves[i]); // put the clockwise version of the move in 
            while (true) {
                this.moves.push([]);
                this.mult(this.moves[this.moves.length-2],clockwiseMoves[i],this.moves[this.moves.length-1]); // put the repeated moves in (U2, U')
                if (arraysEqual(this.moves[this.moves.length-1],this.nullmove)) { // break if repeated move was the solved state
                    this.moves.pop();
                    break;
                }
            }
            let order = this.moves.length-this.inverse.length+1;
            let currentMove = clockwiseMoveStr.shift();
            for (let j=1; j<order; j++) { // Loops once for each added move. Adds the string representation of moves to moveStr.
                if (j <= order/2) {
                    this.moveStr.push(currentMove+(j!=1?j:""));
                } else {
                    this.moveStr.push(currentMove+((order-j)!=1?(order-j):"")+"'");
                }
            }
            let invCounter = this.moves.length;
            while (this.inverse.length < this.moves.length) { // initializes the inverses array and moveTypes array for each new element in the moves array
                invCounter--;
                this.inverse.push(invCounter);
            }
        }

        // initialize valid pairs grid
        this.validPairs = [];
        for (let move1=0; move1<this.moves.length; move1++) { // initialize the valid pairs array
            this.validPairs[move1] = [];
            for (let move2=0; move2<this.moves.length; move2++) {
                let prod1 = [];
                let prod2 = [];
                this.mult(this.moves[move1], this.moves[move2], prod1);
                this.mult(this.moves[move2], this.moves[move1], prod2);
                // Compares if A B = B A. Two moves of the same type are never valid (like B B2), and two commuting moves of different types are only valid in one way (exactly one of U D2 and D2 U are valid)
                if (arraysEqual(prod1, prod2)) { 
                    this.validPairs[move1][move2] = (move1 < move2);
                    if (arraysEqual(prod1, this.nullmove)) {
                        this.validPairs[move1][move2] = false;
                    }
                    for (let m=0; m<this.moves.length; m++) {
                        if (arraysEqual(prod1, this.moves[m])) {
                            this.validPairs[move1][move2] = false;
                        }
                    }
                } else { 
                    this.validPairs[move1][move2] = true;
                } 
            }
        }

        // initialize prunetable (empty upon initialization)
        this.pruneTable = new Map();
        this.pruneDepth = 0;

        // initialize move weights 
        this.moveWeightsMap = moveWeightsMap;
        this.moveWeights = [];
        for (let i=0; i<this.moves.length; i++) {
            let moveName = this.moveStr[i];
            let moveType = moveName.slice(0, lastAlpha(moveName)+1)+"_";
            let moveAmount = "_"+moveName.slice(lastAlpha(moveName)+1);
            if (moveWeightsMap.has(moveName)) {this.moveWeights.push(moveWeightsMap.get(moveName))}
            else if (moveWeightsMap.has(moveType)) {this.moveWeights.push(moveWeightsMap.get(moveType))}
            else if (moveWeightsMap.has(moveAmount)) {this.moveWeights.push(moveWeightsMap.get(moveAmount))}
            else if (moveWeightsMap.has("__")) {this.moveWeights.push(moveWeightsMap.get("__"))}
            else {this.moveWeights.push(1)}
        }

        this.inverseWeights = [];
        for (let i=0; i<this.moves.length; i++) {
            this.inverseWeights.push(this.moveWeights[this.inverse[i]]);
        }

        // the next move is the move with the next highest cost, or with the same cost and to the right.
        // moveNexts.length === moves.length+1. The last entry corresponds to the cheapest move.
        // the entry containing -1 corresponds to the most expensive move.
        // this.moveNexts = [2, 4, 3, 5, 7, 6, 8, 10, 9, 11, 13, 12, 14, 16, 15, 17, -1, 1, 0]; // for SQTM
        // this.moveNexts = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, -1, 0]; // for STM
        this.moveNexts = getMoveNexts(this.moveWeights);
        this.inverseNexts = getMoveNexts(this.inverseWeights);
    }

    // executes sequence on specified starting state
    execute(start, sequence) {
        let tempCube = [];
        for (let j=0; j<sequence.length; j++) {
            this.mult(start, this.moves[sequence[j]],tempCube)
            start = tempCube.slice();
        }
        return start
    }

    // returns next valid move given previous move. For example, U U2 is invalid, but U R is valid.
    // if move == this.moves.length, returns first valid move after prevMove.
    nextValid(prevMove, move, moveNextTable) {
        while (true) {
            move = moveNextTable[move];
            if (move === -1 || this.validPairs[prevMove][move]) {
                return move;
            }
        }
    }

    // Convert a puzzle array like [2, 1, 6, 0, 5, 4, 3, 7] into a compact string. The totalBits is the number of bits to allocate per number. Takes place of strConv.
    compressArr(list) {
        let string = "";
        for (let i=0; i<list.length; i++) {
            string += String.fromCharCode(list[i]+44);
        }
        return string;
    }

    getCost(sequence, weightTable) {
        let cost = 0;
        for (let move of sequence) {
            cost += weightTable[move];
        }
        return cost;
    }

    popAndAdvance(arr, moveNextTable) {
        do {
            arr.pop(); // remove last element
            // increment next last element
            if (arr.length > 1) {
                arr[arr.length-1] = this.nextValid(arr[arr.length-2], arr[arr.length-1], moveNextTable);
            } else if (arr.length) {
                arr[0] = moveNextTable[arr[0]];
                if (arr[0] === -1) {arr.pop()}
            } 
            if (arr.length === 0) {return}
        } while (arr[arr.length-1] === -1)
    }
    
    // generates all sequences with an effective length (with weights, not including adjustments)
    // in the range (seqLength-1, seqLength]. Yields [cost excluding adjustments, sequence]
    *getPruneSequences(seqLength) {
        if (seqLength === 0) {
            yield [0, []];
        }
        postMessage({value: 1, type: "depthUpdate"});
        let arr = [this.inverseNexts[this.moves.length]];
        while (arr.length) {
            let effectiveLength = this.getCost(arr, this.inverseWeights) - 1e-9;
            if (effectiveLength <= seqLength) {
                if (effectiveLength > seqLength-1) {
                    yield [effectiveLength + 1e-9, arr]; // need to account for adjust moves
                }
                // add an element to arr
                arr.push(this.nextValid(arr[arr.length-1], this.moves.length, this.inverseNexts));
            }
            if (effectiveLength + this.inverseWeights[arr[arr.length-1]] > seqLength) {this.popAndAdvance(arr, this.inverseNexts)}
        }
    }

    // generates all sequences with an effective length (with weights, not including adjustments)
    // in the range [seqLength-1, seqLength), NOT including the final move. Yields [cost excluding adjustments, sequence].
    *getSearchSequences(seqLength) {
        if (seqLength === 0) {
            yield [0, []];
        }
        postMessage({value: 1, type: "depthUpdate"});
        let arr = [this.inverseNexts[this.moves.length]];
        while (arr.length) {
            let effectiveLength = this.getCost(arr, this.moveWeights) + 1e-9;
            if (effectiveLength - this.moveWeights[arr[arr.length-1]] < seqLength) {
                if (effectiveLength - this.moveWeights[arr[arr.length-1]] >= seqLength-1) {
                    yield [effectiveLength - 1e-9, arr];
                }
                // add an element to arr
                arr.push(this.nextValid(arr[arr.length-1], this.moves.length, this.moveNexts));            
            } 
            if (effectiveLength >= seqLength) {this.popAndAdvance(arr, this.moveNexts)}
        }
    }

    // convert a sequence of moves, internally represented by a string of numbers, into human-readable text
    moveListToStr(list) {
        let result = "";
        for (let i=0; i<list.length; i++) {
            result += this.moveStr[list[i]];
            if (i != list.length-1) {
                result += " ";
            }
        }
        return result
    }


    // convert a human-readable algorithm into a string of numbers
    moveStrToList(alg) {
        let result = [];
        let algSplit = alg.split(" ");
        for (let i=0; i<algSplit.length; i++) {
            if (algSplit[i] != "") {
                let moveNum = this.moveStr.indexOf(algSplit[i]);
                if (moveNum != -1) {
                    result.push(moveNum);
                } else {
                    postMessage({value: 'Unexpected token in Scramble: "' + algSplit[i] + '"', type: "stop"});
                }
            }
        }
        return result
    }
    
    // create a prune table up to a given depth
    createPrun(maxDepth) {
        let tempTable = new Map();
        for (let depth=0; depth<=maxDepth; depth++) {
            for (let [cost, sequence] of this.getPruneSequences(depth)) {
                let cubeStr = this.compressArr(this.execute(this.solved, sequence));
                if (!(tempTable.has(cubeStr)) || tempTable.get(cubeStr)>cost) {tempTable.set(cubeStr, cost)}
            }
        }
        this.pruneTable = tempTable;
        this.pruneDepth = maxDepth;
    }

    // used only for createPrunSized
    stopPruning(maxSize, highestCost, prevSizes) {
        let sizeRatio = 0;
        for (let i=prevSizes.length-highestCost; i<prevSizes.length; i++) {
            if (i>0) {
                sizeRatio = Math.max(sizeRatio, prevSizes[i]/(prevSizes[i-1]+1))
            }
        }
        if (sizeRatio * prevSizes[prevSizes.length-1] > maxSize) {return true}
        let hcPrevious = prevSizes.length-1-highestCost>=0 ? prevSizes[prevSizes.length-1-highestCost] : 0
        let hc2Previous = prevSizes.length-1-2*highestCost>=0 ? prevSizes[prevSizes.length-1-2*highestCost] : 0
        if (2*hcPrevious > prevSizes[prevSizes.length-1] + hc2Previous) {return true}
        return false;
    }

    // same as createPrun, but automatically determines a depth given a max size
    createPrunSized(maxSize) {
        let tempTable = new Map();
        let highestCost = Math.ceil(this.moveWeights[this.moveNexts.indexOf(-1)]);
        let prevSizes = [];
        let depth = 0;
        while (true) {
            for (let [cost, sequence] of this.getPruneSequences(depth)) {
                let cubeStr = this.compressArr(this.execute(this.solved, sequence));
                if (!(tempTable.has(cubeStr)) || tempTable.get(cubeStr)>cost) {tempTable.set(cubeStr, cost)}
            }
            prevSizes.push(tempTable.size)
            if (this.stopPruning(maxSize, highestCost, prevSizes)) {break}
            depth++;
        }
        this.pruneTable = tempTable;
        this.pruneDepth = depth;
    }

    // read all solutions from a given state under the prune table's depth
    // not done yet
    *readPrun(state, partialSolve=[], maxDepth=this.pruneDepth) { // maxDepth should be the same as the prune table's maxDepth
        for (let m=0; m<this.moves.length; m++) {
            if (partialSolve.length == 0 || this.validPairs[partialSolve[partialSolve.length-1]][m]) {
                let nextState = this.execute(state, [m]);
                let nextDistance = this.pruneTable.get(this.compressArr(nextState));
                if (nextDistance === 0) { 
                    yield * [this.moveListToStr(partialSolve.concat(m), true)];
                } else if (nextDistance <= maxDepth-this.moveWeights[m]) { // false if nextDistance is undefined
                    yield * this.readPrun(nextState, partialSolve.concat(m), maxDepth-this.moveWeights[m]);
                }
            }
        }
    }

    // generating function that returns all solutions for a state
    *solve(state, searchDepth, startDepth=0) {
        for (let depth=startDepth; depth<=searchDepth; depth++) {
            for (let [cost, sequence] of this.getSearchSequences(depth)) {
                let nextState = this.execute(state, sequence);
                let thisDistance = this.pruneTable.get(this.compressArr(nextState));
                if (thisDistance !== undefined) {
                    yield * this.readPrun(nextState, sequence, Math.min(this.pruneDepth, this.pruneDepth+searchDepth-cost));
                }
            }   
        }  
    }

    // ADJUST & BATCH FUNCTIONS
    commutes(index1, index2) {
        let prod1 = [];
        let prod2 = [];
        this.mult(this.moves[index1], this.moves[index2], prod1);
        this.mult(this.moves[index2], this.moves[index1], prod2);
        return arraysEqual(prod1, prod2);
    }

    setSubgroup(generators) {
        let genArray = [];
        for (let i=0; i<generators.length; i++) {
            let gen = generators[i];
            if (!this.moveStr.includes(gen)) {
                postMessage({value: '"' + gen + '" is not a valid move in Subgroup.', type: "stop"})
            }
            genArray.push(this.execute(this.nullmove, this.moveStrToList(gen)));
        }
        return new Puzzle(this.cubeOri.slice(), genArray, generators, this.solved.slice(), this.moveWeightsMap);
    }
}

// COMPUTATION

/**
 * @param {String} puzzleDef - Raw user input (Puzzle field)
 * @param {String} ignore - Raw user input (Unique Orientations & Equivalences field)
 * @param {{ subgroup: String; prune: String; search: String; }[]} subgroups - Each entry represents one row of user input (Subgroup, Prune, Search)
 * @param {String} esq - Raw user input (Rank ESQ field if dropdown set to "Match", else Generation ESQ field)
 * @return {[Puzzle, {puzzle: Puzzle; search: string;}[]]} [fullPuzzle, subPuzzles]
 */
function setPuzzles(puzzleDef, ignore, subgroups, esq) {
    let moves = puzzleDef;
    let moveLines = moves.split('\n');

    let pieceList = [];
    let moveDataList = [];

    let cubeOri = [];
    let moveList = [];
    let clockwiseMoveStr = [];

    /**
     * @param {string} data One line of raw Puzzle input, after the colon
     * @return {number[][][]} List of cycles, where each cycle is a list of two-element lists containing the piece index and twist
     */
    function parseMove(data) {
        let cycleList = [];
        let openParenSplit = data.split("(");
        for (let i=1; i<openParenSplit.length; i++) {
            let cycle = [];
            let cycleStr = openParenSplit[i].split(")")[0];
            let pieces = cycleStr.split(" ");
            for (let j=0; j<pieces.length; j++) {
                let pieceData = pieces[j];
                if (pieceData != "") {
                    let suffixLoc = pieceData.indexOf('+')==-1?pieceData.indexOf('-'):pieceData.indexOf('+');
                    let piece = suffixLoc==-1 ? pieceData : pieceData.slice(0, suffixLoc);
                    let twist = suffixLoc==-1 ? 0 : parseInt(pieceData.slice(suffixLoc));
                    let pieceIndex = pieceList.indexOf(piece);
                    if (pieceIndex == -1) {
                        pieceList.push(piece);
                        pieceIndex = pieceList.length-1;
                        cubeOri.push(Math.max(1,piece.replace(/[^A-Z]/g, "").length)); // Number of orientations is number of capital letters in piece name, but 1 minimum
                    }
                    cycle.push([pieceIndex, twist]);
                }
            }
            cycleList.push(cycle);
        }
        return cycleList;
    }

    function mod(a, n) { // works with negative a unlike % operator
        return a - (n * Math.floor(a/n));
    }
    
    for (let ln=0; ln<moveLines.length; ln++) {
        let line = moveLines[ln].split("//")[0];
        if (line.includes(":")) {
            let cycleStr = moveLines[ln].split(":");
            let moveName = cycleStr[0];
            if (!/^[A-Za-z0-9]*$/.test(moveName)) { // if moveName contains anything that isn't a letter or number
                postMessage({value: "'" + moveName + "' is not a valid move name, because move names must only contain alphanumeric characters.", type: "stop"})
            } else if (moveName[moveName.length-1] >= '0' && moveName[moveName.length-1] <= '9') { // if moveName ends in a number
                postMessage({value: "'" + moveName + "' is not a valid move name, because move names cannot end in a number.", type: "stop"})
            }
            clockwiseMoveStr.push(moveName);
            let cycleList = parseMove(cycleStr[1]);
            moveDataList.push(cycleList)
        }
    }

    let oriMult = 2**Math.ceil(Math.log2(pieceList.length));

    for (let m=0; m<moveDataList.length; m++) {
        let cycleList = moveDataList[m];
        let move = [];
        for (let i=0; i<pieceList.length; i++) {move.push(i)}
        for (let c=0; c<cycleList.length; c++) {
            let cycle = cycleList[c];
            for (let i=0; i<cycle.length-1; i++) {
                move[cycle[i+1][0]] = cycle[i][0]+oriMult*mod(cycle[i][1],cubeOri[cycle[i][0]]);
            }
            if (cycle.length==1 || (cycle[0][0] != cycle[cycle.length-1][0])) {
                move[cycle[0][0]] = cycle[cycle.length-1][0]+oriMult*mod(cycle[cycle.length-1][1],cubeOri[cycle[cycle.length-1][0]]);
            }
        }
        moveList.push(move);
    }

    // Deal with equivalences
    let splitEquivalences = ignore.split('{');
    let solvedState = [];
    for (let i=0; i<pieceList.length; i++) {solvedState[i] = i}
    for (let i=1; i<splitEquivalences.length; i++) {
        if (!splitEquivalences[i].includes("}")) {
            postMessage({value: 'Missing "}" in Unique Orientations and Equivalences', type: "stop"})
        }
        let equivSet = splitEquivalences[i].split("}")[0];
        let equivPieces = equivSet.split(" ").filter(x => x !== "");
        let equivNum = pieceList.indexOf(equivPieces[0]);
        if (equivNum == -1) {
            postMessage({value: '"' + equivPieces[0] + '" is not a piece. (error in Unique Orientations & Equivalences)', type: "stop"})
        }
        for (let j=1; j<equivPieces.length; j++) {
            let equivWithNum = pieceList.indexOf(equivPieces[j]);
            if (equivWithNum == -1) {
                postMessage({value: '"' + equivPieces[j] + '" is not a piece. (error in Unique Orientations & Equivalences)', type: "stop"})
            }
            if (cubeOri[equivNum] != cubeOri[equivWithNum]) {
                postMessage({value: '"' + equivPieces[j] + '" and "' + equivPieces[0] + '" cannot be in the same equivalence set because they are different types of pieces.', type: "stop"})
            }
            solvedState[equivWithNum] = equivNum;
        }
    }

    // parse ESQ input
    let moveWeights = parseESQ(esq);

    // Validate full puzzle
    let fullPuzzle = new Puzzle(cubeOri.slice(), moveList.slice(), clockwiseMoveStr.slice(), solvedState.slice(), moveWeights);
    let fullPuzzleDupe = new Puzzle(cubeOri.slice(), moveList.slice(), clockwiseMoveStr.slice(), solvedState.slice(), moveWeights);
    for (let sub of subgroups) {checkMoveGroup(fullPuzzle, sub.subgroup, "a subgroup")}
    initCubeOri(fullPuzzleDupe, pieceList, ignore);

    // deal with subpuzzles
    let subPuzzles = [];
    for (let sub of subgroups) {
        subPuzzles.push({puzzle: getSubPuzzle(pieceList, fullPuzzle, ignore, sub.subgroup, sub.prune), search: sub.search});
    }

    initCubeOri(fullPuzzle, pieceList, ignore);
    return [fullPuzzle, subPuzzles];
}

function parseESQ(esq) {
    let moveWeights = new Map();
    for (let line of esq.split("\n")) {
        let splitLine = line.split(":");
        if (splitLine.length === 2) {
            let moveNames = splitSubgroupStr(splitLine[0]);
            let data = splitLine[1].trim();
            for (let moveName of moveNames) {
                moveWeights.set(moveName, parseFloat(data, 10));
            }
        }
    }
    return moveWeights;
}

function initCubeOri(pzl, pieceList, ignore) {
    let lines = ignore.split("\n");
    for (let i=0; i<lines.length; i++) {
        let line = lines[i];
        if (line.includes(":")) {
            let numOri = parseInt(line.split(":")[0], 10);
            if (numOri !== numOri) { // checking if numOri is NaN
                postMessage({value: '"' + line.split(":")[0] + ':" is not a valid Unique Orientations header.', type: "stop"})
            }
            if (numOri <= 0) {
                postMessage({value: '"' + line.split(":")[0] + ':" is invalid because the number of orientations must be positive.', type: "stop"})
            }
            let orientData = line.split(":")[1].split(" ");
            for (let i=0; i<orientData.length; i++) {
                let pieceIndex = pieceList.indexOf(removeBrackets(orientData[i]));
                if (pieceIndex !== -1) {
                    if (pzl.cubeOri[pieceIndex] % numOri != 0) {
                        postMessage({value: "Cannot set number of orientations of piece " + removeBrackets(orientData[i]) + 
                        " to " + numOri + " because " + numOri + " is not divisible by " + pzl.cubeOri[pieceIndex] + ".", type: "stop"})
                    }
                    pzl.cubeOri[pieceIndex] = numOri;
                } else if (removeBrackets(orientData[i]) !== "") {
                    postMessage({value: '"' + removeBrackets(orientData[i]) + '" is not a piece. (error in Unique Orientations & Equivalences)', type: "stop"})
                }
            }
        }
    }
}

/**
 * @param {String[]} pieceList - Ordered array of named pieces defined in Puzzle
 * @param {Puzzle} fullPuzzle - Puzzle generated from all fields except Subgroup
 * @param {String} ignore - Raw user input (Unique Orientations & Equivalences field)
 * @param {String} subgroup - Raw user input
 * @param {String} prune - Raw user input
 * @param {String[]} adjust - List of all moves defined in the Pre-Adjust field
 * @return {Puzzle} Puzzle with given subgroup and prune table
 */
function getSubPuzzle(pieceList, fullPuzzle, ignore, subgroup, prune) {
    let generators = (subgroup.replace(" ","").length > 0) ? splitSubgroupStr(subgroup) : fullPuzzle.clockwiseMoveStr;
    
    let hasNonAdjust = false;
    for (let move of generators) {
        for (let move2 of generators) {
            if (!fullPuzzle.commutes(fullPuzzle.moveStr.indexOf(move), fullPuzzle.moveStr.indexOf(move2))) {
                hasNonAdjust = true;
            }
        }
    }
    if (!hasNonAdjust) {postMessage({value: '"' + subgroup + '" is not a valid subgroup because it is commutative.', type: "stop"})}

    let subPuzzle = fullPuzzle.setSubgroup(generators);
    initCubeOri(subPuzzle, pieceList, ignore);
    postMessage({value: 0, type: "set-depth"})

    function errParse(x, parseFunc) {
        let pFloat = parseFunc(x, 10);
        if (pFloat !== pFloat) {
            postMessage({value: '"' + x + '" is not a valid prune depth.', type: "stop"});
        }
        return pFloat;
    }

    if (prune.toLowerCase().includes("m")) {
        subPuzzle.createPrunSized(errParse(prune, parseFloat) * 1e6 );
    } else if (prune.toLowerCase().includes("k")) {
        subPuzzle.createPrunSized(errParse(prune, parseFloat) * 1e3 );
    } else {
        subPuzzle.createPrun(errParse(prune, parseInt));
    }

    return subPuzzle;
}

function checkMoveGroup(puzzle, movegroup, errorStr) {
    let moves = splitSubgroupStr(movegroup);
    for (let move of moves) {
        if (!puzzle.moveStr.includes(move)) {
            postMessage({value: '"' + move + '" is not a valid move in ' + errorStr, type: "stop"});
        }
    }
}

function removeBrackets(s) { // Removes (), {}, <>, and []
    return s.replace(/\(|\)|\[|\]|{|}|<|>/g, "");
}

/**
 * @param {String} s - Denotes a subgroup string such as "<R U>" or "M E S" 
 * @return {String[]} Array of move strings contained in the input string
 */
function splitSubgroupStr(s) {
    return removeBrackets(s).replaceAll(","," ").split(" ").filter(x => x !== "");
}