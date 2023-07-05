self.onmessage = function (msg) {
    if (msg.data.puzzle) {main(msg.data)}
};

/**
 * @param {{ puzzle: String; ignore: String; solve: String; preAdjust: String; postAdjust: String; subgroups: { subgroup: String; prune: String; search: String; }[]; sorting: { type: String; pieces: String; }[]; esq: string; rankesq: string; showPost: boolean; }} input 
 * Posts messages:
 * - "stop" - indicates deprecated notation or end of search
 * - "moveWeights" - returns parsed rank ESQ
 * - "next-state" - returns setup info (not solution) for next state
 */
function main(input) {
    let scramble = input.solve;
    if (scramble.includes(":")) {
        postMessage({ value: "Colon notation for indicating adjust moves is deprecated.", type: "stop" });
    }
    let [fullPuzzle, batchStates, subPuzzles] = setPuzzles(scramble, input.puzzle, input.ignore, input.subgroups, input.preAdjust, input.postAdjust, input.sorting, input.esq);
    postMessage({value: parseESQ(input.rankesq), type: "moveWeights"}); 
    let [modifiers, startNum] = parseModifiers(scramble);
    let caseNum = 1;
    let solutionIndex = 1;
    for (let stateStr of batchStates) {
        let state = fullPuzzle.execute(fullPuzzle.solved, fullPuzzle.moveStrToList(stateStr));
        if (!(arraysEqual(fullPuzzle.solved, state))) {
            if (caseNum >= startNum || modifiers.has(caseNum)) {
                postMessage({ value: { index: solutionIndex, setup: stateStr, num: caseNum }, type: "next-state" });
                calcState(state, subPuzzles, input.showPost);
                solutionIndex++;
            }
            caseNum++;
        }
    }
    postMessage({ value: null, type: "stop" });
}

/**
 * @param {String} scramble - Raw user input (Scramble field)
 * @param {String} puzzleDef - Raw user input (Puzzle field)
 * @param {String} ignore - Raw user input (Unique Orientations & Equivalences field)
 * @param {{ subgroup: String; prune: String; search: String; }[]} subgroups - Each entry represents one row of user input (Subgroup, Prune, Search)
 * @param {String} adjust - Raw user input (Pre-Adjust)
 * @param {String} postAdjust - Raw user input (Post-Adjust)
 * @param {{ type: String; pieces: String; }[]} sorting - Type: one of {"priority", "ori-of", "ori-at", "perm-of", "perm-at"}. Pieces: One row of raw user input (Sorting field)
 * @param {String} esq - Raw user input (Rank ESQ field if dropdown set to "Match", else Generation ESQ field)
 * @return {[Puzzle, Set<String>, {puzzle: any; search: any;}]} [fullPuzzle, batchStates, subPuzzles] TODO: REPLACE THE any WITH REAL TYPES
 */
function setPuzzles(scramble, puzzleDef, ignore, subgroups, adjust, postAdjust, sorting, esq) {
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
    checkMoveGroup(fullPuzzle, adjust, "pre-adjust");
    checkMoveGroup(fullPuzzle, postAdjust, "post-adjust");
    for (let sub of subgroups) {checkMoveGroup(fullPuzzle, sub.subgroup, "a subgroup")}
    let adjustList = (adjust == "") ? [] : splitSubgroupStr(adjust);
    for (let i=0; i<adjustList.length; i++) {
        for (let j=0; j<i; j++) {
            if (!fullPuzzle.commutes(fullPuzzle.moveStr.indexOf(adjustList[i]), fullPuzzle.moveStr.indexOf(adjustList[j]))) {
                postMessage({value: '"' + adjust + '" is an invalid pre-adjust because all moves in pre-adjust must commute.', type: "stop"})
            }
        }
    }
    initCubeOri(fullPuzzleDupe, pieceList, ignore);

    // calculate batch states
    let batchStates = fullPuzzleDupe.getBatchStates(scramble, adjust, postAdjust, pieceList, sorting);
    let numStates = 0;
    let solutionIndex = 1;
    let [modifiers, startNum] = parseModifiers(scramble)
    for (let stateStr of batchStates) {
        let state = fullPuzzleDupe.execute(fullPuzzleDupe.solved, fullPuzzleDupe.moveStrToList(stateStr));
        if(!(arraysEqual(fullPuzzleDupe.solved, state))) {
            if (solutionIndex >= startNum || modifiers.has(solutionIndex)) {
                numStates++;
            }
            solutionIndex++;
        }
    }
    postMessage({value: numStates, type: "num-states"})

    // deal with subpuzzles
    let subPuzzles = [];
    for (let sub of subgroups) {
        subPuzzles.push({puzzle: getSubPuzzle(pieceList, fullPuzzle, ignore, sub.subgroup, sub.prune, adjustList), search: sub.search});
    }

    initCubeOri(fullPuzzle, pieceList, ignore);
    return [fullPuzzle, batchStates, subPuzzles];
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
function getSubPuzzle(pieceList, fullPuzzle, ignore, subgroup, prune, adjust) {
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

    for (let move of adjust) {
        if (!generators.includes(move)) {
            postMessage({value: '"' + subgroup + '" is not a valid subgroup because it does not contain the adjust move "' + move + '".', type: "stop"})
        }
    }

    generators.sort((x, y) => adjust.includes(y) - adjust.includes(x)) // moves all adjust moves to the front
    let subPuzzle = fullPuzzle.setSubgroup(generators);

    initCubeOri(subPuzzle, pieceList, ignore);

    subPuzzle.setAdjustMoves(adjust.length); // deal with adjust: setAdjustMoves passes in a NUMBER
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

function parseModifiers(input) {
    // returns [modification, startNum]
    function errParse(x) {
        let pInt = parseInt(x, 10);
        if (pInt !== pInt || pInt <= 0) {
            postMessage({value: '"' + x + '" is not a positive number. (Error in Scramble)', type: "stop"});
        }
        return pInt;
    }

    input = input.replaceAll("\n","");
    let indexPound = input.indexOf("#");
    if (indexPound === -1) {
        return [new Set(), 1];
    }
    let modificationStr = input.slice(indexPound+1);
    let modificationList = modificationStr.split(",").filter(x => x !== "");
    let modifications = new Set();
    let startNum = Infinity;
    for (let mod of modificationList) {
        if (mod.includes("+")) {
            startNum = Math.min(startNum, errParse(mod));
        } else if (mod.includes("-")) {
            let int1 = errParse(mod.split("-")[0]);
            let int2 = errParse(mod.split("-")[1]);
            if (int2 <= int1) {
                postMessage({value: 'Invalid range: "' + mod + '" (Error in Scramble)', type: "stop"});
            }
            for (var i = int1; i <= int2; i++) {
                modifications.add(i);
            }
        } else {
            modifications.add(errParse(mod));
        }
    }
    return [modifications, startNum];
}

function parseBatch(input) {
    let closingChar = "";
    let dataInside = "";
    let finalData = [];
    const bracketMap = new Map([["[","]"],["<",">"]]);
    for (let i=0; i<input.length; i++) {
        let char = input[i]; 
        if (closingChar !== "") {
            if (char === closingChar) {
                finalData.push([closingChar, dataInside]);
                closingChar = ""; 
                dataInside = "";
            } else {
                dataInside += char;
                if (i === input.length - 1) {
                    postMessage({value: 'Missing "' + closingChar + '" in Scramble', type: "stop"});
                }
            }
        } else {
            if (!(bracketMap.has(char))) {
                dataInside += char;
            }
            if (bracketMap.has(char) || i === input.length - 1) {
                finalData.push(["", dataInside]);
                closingChar = bracketMap.get(char);
                dataInside = "";
            }
        }
    }
    return finalData;
}

function calcState(state, subPuzzles, showPostAdj) {
    for (let subData of subPuzzles) {
        let searchDepth = parseInt(subData.search, 10);
        if (searchDepth !== searchDepth) {// that means it's NaN
            if (subData.search[0] === "=") {searchDepth = subData.puzzle.pruneDepth}
            else if (subData.search[0] === "+") {searchDepth = subData.puzzle.pruneDepth + (subData.search.split("+").length-1) } // the prune depth PLUS the number of +
            else if (subData.search[0] === "-") {searchDepth = subData.puzzle.pruneDepth - (subData.search.split("-").length-1) } // the prune depth MINUS the number of -
            else {postMessage({value: '"' + subData.search + '" is not a valid search depth.', type: "stop"})}
        }
        for (let solution of subData.puzzle.solve(state, searchDepth, showPostAdj)) {
            postMessage({value: solution, type: "solution"});
        }
        postMessage({value: 0, type: "set-depth"})
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

// END HTML-SIDE JS

/**
 * @param {any[]} arr1 
 * @param {any[]} arr2 
 */
function arraysEqual(arr1,arr2) {
    for (let i=0; i<arr1.length; i++) {
        if (arr1[i] !== arr2[i]) {return false}
    }
    return true
}

/**
 * @param {any[][]} arrays 
 * @return {any[][]} All possible arrays that can be constructed by
 * optionally selecting an element from arrays[0], then optionally
 * selecting an element from arrays[1], etc. Always contains the empty array.
 */
function cartesian(arrays) { 
	let singleStep = (arrays, add) => {return arrays.concat(...add.map(next => arrays.map( arr => arr.concat(next) )))} 
	let prod = [[]];
    for (arr of arrays) {prod = singleStep(prod, arr)}
    return prod;
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

        // initialize adjust moves (none upon initialization)
        this.adjustSequences = [[]];
        this.adjustMovesTable = [];
        for (let i=0; i<this.moves.length; i++) {
            this.adjustMovesTable[i] = false;
        }
        this.adjustCount = 0;

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
            string += String.fromCharCode(list[i]);
        }
        return string;
    }

    // like nextValid, but with the first move of a sequence.
    nextValidInitial(move, moveNextTable) {
        let x = moveNextTable[move];
        while (true) {
            if (x === -1 || !(this.adjustMovesTable[x])) {return x};
            x = moveNextTable[x];
        }
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
                arr[0] = this.nextValidInitial(arr[0], moveNextTable);
                if (arr[0] === -1) {arr.pop()}
            } 
            if (arr.length === 0) {return}
        } while (arr[arr.length-1] === -1)
    }
    
    // generates all sequences with an effective length (with weights, not including adjustments)
    // in the range (seqLength-1, seqLength]. Yields [cost excluding adjustments, sequence]
    *getPruneSequences(seqLength) {
        if (seqLength === 0) {
            for (let sequence of this.adjustSequences) {
                yield [0, sequence]; // need to account for adjust moves
            }
            return;
        }
        postMessage({value: 1, type: "depthUpdate"});
        let arr = [this.nextValidInitial(this.moves.length, this.inverseNexts)];
        while (arr.length) {
            let effectiveLength = this.getCost(arr, this.inverseWeights) - 1e-9;
            if (effectiveLength <= seqLength) {
                if (effectiveLength > seqLength-1) {
                    for (let sequence of this.adjustSequences) {
                        yield [effectiveLength + 1e-9, sequence.concat(arr)]; // need to account for adjust moves
                    }
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
            for (let sequence of this.adjustSequences) {
                yield [0, sequence]; // need to account for adjust moves
            }
            return;
        }
        postMessage({value: 1, type: "depthUpdate"});
        let arr = [this.nextValidInitial(this.moves.length, this.moveNexts)];
        while (arr.length) {
            let effectiveLength = this.getCost(arr, this.moveWeights) + 1e-9;
            if (effectiveLength - this.moveWeights[arr[arr.length-1]] < seqLength) {
                if (effectiveLength - this.moveWeights[arr[arr.length-1]] >= seqLength-1) {
                    for (let sequence of this.adjustSequences) {
                        yield [effectiveLength - 1e-9, sequence.concat(arr)];
                    }
                }
                // add an element to arr
                arr.push(this.nextValid(arr[arr.length-1], this.moves.length, this.moveNexts));            
            } 
            if (effectiveLength >= seqLength) {this.popAndAdvance(arr, this.moveNexts)}
        }
    }

    // convert a sequence of moves, internally represented by a string of numbers, into human-readable text
    moveListToStr(list, parens=false) {
        let result = "";
        let adjusting = false;
        for (let i=0; i<list.length; i++) {
            
            // open parenthesis
            if (parens && i==0 && this.adjustMovesTable[list[i]]) {
                result += "(";
                adjusting = true;
            }
 
            // move
            result += this.moveStr[list[i]];

            // close parenthesis
            if (parens && adjusting && (i==list.length-1 || !this.adjustMovesTable[list[i+1]])) {
                result += ")";
                adjusting = false;
            }

            // space
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
    *readPrun(state, partialSolve=[], showPostAdj, maxDepth=this.pruneDepth) { // maxDepth should be the same as the prune table's maxDepth
        for (let m=0; m<this.moves.length; m++) {
            if (partialSolve.length == 0 || this.validPairs[partialSolve[partialSolve.length-1]][m]) {
                let nextState = this.execute(state, [m]);
                let nextDistance = this.pruneTable.get(this.compressArr(nextState));
                if (nextDistance === 0) { 
                    let fullSolve = partialSolve.concat(m);
                    if (!this.hasEndAdjust(fullSolve)) {
                        if (showPostAdj) {yield * [this.moveListToStr(fullSolve, true) + " " + this.moveListToStr(this.getEndAdjust(nextState), true)]}
                        else {yield * [this.moveListToStr(fullSolve, true)]}
                    }
                } else if (nextDistance <= maxDepth-this.moveWeights[m]) { // false if nextDistance is undefined
                    yield * this.readPrun(nextState, partialSolve.concat(m), showPostAdj, maxDepth-this.moveWeights[m]);
                }
            }
        }
    }

    // generating function that returns all solutions for a state
    *solve(state, searchDepth, showPostAdj, startDepth=0) {
        for (let depth=startDepth; depth<=searchDepth; depth++) {
            for (let [cost, sequence] of this.getSearchSequences(depth)) {
                let nextState = this.execute(state, sequence);
                let thisDistance = this.pruneTable.get(this.compressArr(nextState));
                if (thisDistance !== undefined) {
                    yield * this.readPrun(nextState, sequence, showPostAdj, Math.min(this.pruneDepth, this.pruneDepth+searchDepth-cost));
                }
            }   
        }  
    }

    // EXPERIMENTAL METHODS
    invert(list) {
        return list.map(x => this.inverse[x]).reverse();
    }

    commutes(index1, index2) {
        let prod1 = [];
        let prod2 = [];
        this.mult(this.moves[index1], this.moves[index2], prod1);
        this.mult(this.moves[index2], this.moves[index1], prod2);
        return arraysEqual(prod1, prod2);
    }

    // returns true iff sequence has any adjust moves that are either at the end or can be moved to the end by commuting
    hasEndAdjust(list) {
        for (let i=list.length-1; i<list.length && i>=0; i--) {
            if (this.adjustMovesTable[list[i]]) {
                let allAfterCommute = true;
                for (let j=i+1; j<list.length; j++) {
                    if (!(this.commutes(list[i], list[j]))) {allAfterCommute = false}
                }
                if (allAfterCommute) {return true}
            } else {
                let noAdjustCommute = true;
                for (let j=0; j<this.adjustMovesTable.length; j++) {
                    if (this.adjustMovesTable[j] && this.commutes(list[i], j)) {noAdjustCommute = false}
                }
                if (noAdjustCommute) {return false}
            }
        }
        return false;
    }

    // Takes in a state that's only adjust moves away from being solved. Returns the move list representing the adjustments.
    getEndAdjust(nextState) {
        for (let seq of this.adjustSequences) {
            if (arraysEqual(this.execute(nextState, seq), this.solved)) {
                return seq;
            }
        }
        throw nextState;
    }

    // ADJUST & BATCH FUNCTIONS
    getMoveMultiples(moveNum) {
        let moveReps = [];
        let currentRep = moveNum;
        while (true) {
            moveReps.push(currentRep);
            let move = this.execute(this.moves[currentRep], [moveNum]);
            currentRep = -1;
            for (let i=0; i<this.moves.length; i++) {
                if (arraysEqual(this.moves[i], move)) {
                    currentRep = i;
                    break;
                }
            }
            if (currentRep === -1) {break}
        }
        return moveReps;
    }

    setAdjustMoves(num) {
        let moveList = this.clockwiseMoveStr.slice(0, num).map(str => this.moveStr.indexOf(str));
        this.adjustCount = moveList.length;
        let adjustMoves = [];
        for (let i=0; i<this.moves.length; i++) {this.adjustMovesTable[i] = false}
        for (let moveNum of moveList) {
            let moveReps = this.getMoveMultiples(moveNum);
            for (let j of moveReps) {
                this.adjustMovesTable[j] = true;
            }
            adjustMoves.push(moveReps);
        }
        this.adjustSequences = cartesian(adjustMoves);
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
    
    compressStr(str) {
        return this.compressArr(this.execute(this.solved, this.moveStrToList(str)));
    }

    seriesMult(stateLists) {
        let states = new Map(stateLists[0].map(x => [this.compressStr(x), x]));
        for (let i=1; i<stateLists.length; i++) {
            let newStates = new Map();
            let algs = stateLists[i];
            for (let state of states.values()) {
                for (let alg of algs) {
                    let newState = state + " " + alg;
                    newStates.set(this.compressStr(newState), newState);
                }
            }
            states = new Map(newStates);
        }
        return Array.from(states.values());
    }

    bfs(startStates, generators) {
        let states = new Map(startStates.map(x => [this.compressStr(x), x]));
        let newStates = new Map(startStates.map(x => [this.compressStr(x), x]));
        let nextStates = new Map();
        while (true) {
            for (let state of newStates.values()) {
                for (let gen of generators) {
                    let prod = state + " " + gen;
                    if (!(states.has(this.compressStr(prod)))) {
                        nextStates.set(this.compressStr(prod), prod);
                        states.set(this.compressStr(prod), prod);
                    }
                }
            }
            if (nextStates.size === 0) {
                break;
            }
            newStates = nextStates;
            nextStates = new Map();
            postMessage({value: states.size + " (not reduced)", type: "num-states"})
        }
        return Array.from(states.values());
    }

    getAdjustFromStr(adjStr) { // helper method for getReducedSet
        return (adjStr == "") ? [[]] : cartesian(splitSubgroupStr(adjStr).map(str => this.getMoveMultiples(this.moveStr.indexOf(str))));
    }

    /**
     * @param {String[]} states - List of setups (derived from Scramble field)
     * @param {String} preAdjust - Raw user input (Pre-Adjust)
     * @param {String} postAdjust - Raw user input (Post-Adjust)
     * @return {Set<String>} - List of setups with states differing by only a pre- or post- adjust removed
     */
    getReducedSet(states, preAdjust, postAdjust) {
        let preAdjustSequences = this.getAdjustFromStr(preAdjust);
        let postAdjustSequences = this.getAdjustFromStr(postAdjust);
        let reducedStates = new Set();
        let duplicateStates = new Set();
        for (let state of states) {
            let cubeStr = this.compressStr(state)
            if (!(duplicateStates.has(cubeStr))) {
                reducedStates.add(state);
                for (let preAdjustment of preAdjustSequences) {
                    for (let postAdjustment of postAdjustSequences) {
                        duplicateStates.add(this.compressStr(this.moveListToStr(postAdjustment) + " " + state + " " + this.moveListToStr(preAdjustment)));
                    }
                }
            }
        }
        return reducedStates;
    }

    compareStates(state1, state2) {
        for (let i=0; i<state1.length; i++) {
            if (state1[i] > state2[i]) {return 1}
            if (state1[i] < state2[i]) {return -1}
        }
        return 0;
    }

    /**
     * @param {String} input - Raw user input (Scramble field)
     * @param {String} preAdjust - Raw user input (Pre-Adjust)
     * @param {String} postAdjust - Raw user input (Post-Adjust)
     * @param {String[]} pieceList - Ordered array of named pieces defined in Puzzle
     * @param {{ type: String; pieces: String; }[]} sorting - Type: one of {"priority", "ori-of", "ori-at", "perm-of", "perm-at"}. Pieces: One row of raw user input (Sorting field)
     * @return {Set<String>} Sorted list of setups to all distinct states
     */
    getBatchStates(input, preAdjust, postAdjust, pieceList, sorting) {
        input = (input.includes("#") ? input.slice(0, input.indexOf("#")) : input).replaceAll("\n", "");
        let parsedInput = parseBatch(input);
        let states = [""];
        for (let i=0; i<parsedInput.length; i++) {
            let type = parsedInput[i][0];
            let data = parsedInput[i][1];
            if (type === "") {
                states = states.map(state => state + " " + data);
            } else if (type === "]") {
                let algs = data.split(",");
                states = this.seriesMult([states, algs]);
            } else if (type === ">") {
                states = this.bfs(states, data.split(","));
            }
        }
        let pieceMap = new Map(pieceList.map((pc, ind) => [pc, ind]));
        let sortLookupTable = new Map(states.map(x => [x, getStatePriority(x, pieceMap, sorting, this)]));
        states.sort((s1, s2) => this.compareStates(sortLookupTable.get(s1), sortLookupTable.get(s2)));
        return this.getReducedSet(states, preAdjust, postAdjust);
    }
}

function maskedIndex(stateArr, num, bitMask, piece) {
    let ind = 0;
    for (let n of stateArr) {
        if ((n & bitMask) === num) {
            return ind;
        }
        ind++;
    }
    postMessage({value: 'Invalid piece: "' + piece + '"', type: "stop"})
}

function getStatePriority(str, pieceMap, sortCriteria, fullPuzzle) { // TODO: error handling
    let stateArr = fullPuzzle.execute(fullPuzzle.nullmove, fullPuzzle.moveStrToList(str));
    let pcCount = pieceMap.size;
    let minIndex = 0;
    let pcPriority = [];
    let statePriority = [];
    for (let i=0; i<pcCount; i++) {
        pcPriority.push(i);
    }
    for (let criteria of sortCriteria) {
        if (criteria.pieces.trim().length > 0) {
            if (criteria.type === "priority") {
                minIndex -= pcCount;
                for (let pieceFull of criteria.pieces.split(" ")) {
                    let piece = pieceFull.trim();
                    if (piece.length > 0) {
                        if (!pieceMap.has(piece)) {
                            postMessage({value: 'Invalid piece: "' + piece + '" (in Case Sorting)', type: "stop"});
                        }
                        pcPriority[pieceMap.get(piece)] = minIndex;
                        minIndex++;
                    }
                }
                minIndex -= pcCount;
            } else {
                let ori = [];
                for (let pieceFull of criteria.pieces.split(" ")) {
                    let piece = pieceFull.trim();
                    if (piece.length > 0) {
                        let pieceInd = pieceMap.get(piece);
                        if (pieceInd === undefined) {postMessage({value: 'Invalid piece: "' + piece + '" (in Case Sorting)', type: "stop"})}

                        if (criteria.type === "ori-at") {ori.push( stateArr[pieceInd] & fullPuzzle.oriMask )}
                        else if (criteria.type === "ori-of") {ori.push( stateArr[maskedIndex(stateArr, pieceInd, fullPuzzle.posMask, piece)] & fullPuzzle.oriMask )}
                        else if (criteria.type === "perm-at") {ori.push( pcPriority[stateArr[pieceInd] & fullPuzzle.posMask] )}
                        else if (criteria.type === "perm-of") {ori.push( pcPriority[maskedIndex(stateArr, pieceInd, fullPuzzle.posMask, piece)] )}
                    }
                }
                if (criteria.type === "ori-at" || criteria.type === "ori-of") {
                    let sortedOri = ori.slice().sort((a, b) => a-b);
                    statePriority = statePriority.concat(sortedOri);
                }
                statePriority = statePriority.concat(ori);
            }
        }
    }
    return statePriority;
}