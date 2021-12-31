self.onmessage = function (msg) {
    let input = msg.data;
    let pzlDef = input.solve;
    if (pzlDef.includes(":")) {postMessage({value: "Colon notation for indicating adjust moves is deprecated.", type: "stop"})}
    let [fullPuzzle, subPuzzles] = setPuzzles(input.puzzle, input.ignore, input.subgroups, input.preAdjust); // subgroup.adjust is NO LONGER BEING USED!

    let solutionIndex = 1;
    let batchStates = fullPuzzle.getBatchStates(pzlDef, input.preAdjust, input.postAdjust);
    let numStates = 0;

    for (let stateStr of batchStates) {
        let state = fullPuzzle.execute(fullPuzzle.solved, fullPuzzle.moveStrToList(stateStr));
        if(!(arraysEqual(fullPuzzle.solved, state))) {numStates++}
    }

    postMessage({value: numStates, type: "num-states"})

    for (let stateStr of batchStates) {
        let state = fullPuzzle.execute(fullPuzzle.solved, fullPuzzle.moveStrToList(stateStr));
        if(!(arraysEqual(fullPuzzle.solved, state))) {
            postMessage({value: solutionIndex, type: "next-state"})
            calcState(state, subPuzzles);
            solutionIndex++;
        }
    }
    postMessage({value: null, type: "stop"});
};

function calcState(state, subPuzzles) {
    for (let subData of subPuzzles) {
        let searchDepth = parseInt(subData.search, 10);
        if (searchDepth !== searchDepth) {// that means it's NaN
            if (subData.search[0] === "=") {searchDepth = subData.puzzle.pruneDepth}
            else if (subData.search[0] === "+") {searchDepth = subData.puzzle.pruneDepth + (subData.search.split("+").length-1) } // the prune depth PLUS the number of +
            else if (subData.search[0] === "-") {searchDepth = subData.puzzle.pruneDepth - (subData.search.split("-").length-1) } // the prune depth MINUS the number of -
            else {searchDepth = Infinity}
        }
        for (let solution of subData.puzzle.solve(state, searchDepth)) {
            postMessage({value: subData.puzzle.moveListToStr(solution, true), type: "solution"});
        }
        postMessage({value: 0, type: "set-depth"})
    }
}

function removeBrackets(s) { // Removes (), {}, <>, and []
    return s.replace(/\(|\)|\[|\]|{|}|<|>/g, "");
}

function splitSubgroupStr(s) {
    return removeBrackets(s).replaceAll(","," ").split(" ").filter(x => x !== "");
}

function setPuzzles(puzzleDef, ignore, subgroups, adjust) {
    let moves = puzzleDef;
    let moveLines = moves.split('\n');

    let pieceList = [];
    let moveDataList = [];

    let cubeOri = [];
    let moveList = [];
    let clockwiseMoveStr = [];

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
            if (!/^[a-z]+$/i.test(moveName)) { // if moveName contains anything that isn't a letter
                postMessage({value: "'" + moveName + "' is not a valid move name, because move names must only contain letters.", type: "stop"})
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
        let equivSet = splitEquivalences[i].split("}")[0];
        let equivPieces = equivSet.split(" ");
        let equivNum = pieceList.indexOf(equivPieces[0]);
        for (let j=1; j<equivPieces.length; j++) {
            solvedState[pieceList.indexOf(equivPieces[j])] = equivNum;
        }
    }

    // Deal with subgroups
    let fullPuzzle = new Puzzle(cubeOri.slice(), moveList.slice(), clockwiseMoveStr.slice(), solvedState.slice());
    let subPuzzles = [];
    for (let sub of subgroups) {
        subPuzzles.push({puzzle: getSubPuzzle(pieceList, fullPuzzle, ignore, sub.subgroup, sub.prune, adjust), search: sub.search});
    }

    initCubeOri(fullPuzzle, pieceList, ignore);
    
    return [fullPuzzle, subPuzzles];
}

function initCubeOri(pzl, pieceList, ignore) {
    let lines = ignore.split("\n");
    for (let i=0; i<lines.length; i++) {
        let line = lines[i];
        if (line.includes(":")) {
            let numOri = parseInt(line.split(":")[0]);
            let orientData = line.split(":")[1].split(" ");
            for (let i=0; i<orientData.length; i++) {
                let pieceIndex = pieceList.indexOf(removeBrackets(orientData[i]));
                if (pieceIndex !== -1) {pzl.cubeOri[pieceIndex] = numOri}
            }
        }
    }
}

function getSubPuzzle(pieceList, fullPuzzle, ignore, subgroup, prune, adjustStr) {
    let generators = (subgroup.replace(" ","").length > 0) ? splitSubgroupStr(subgroup) : fullPuzzle.clockwiseMoveStr;
    let adjust = (adjustStr == "") ? [] : splitSubgroupStr(adjustStr);
    
    generators.sort((x, y) => adjust.includes(y) - adjust.includes(x)) // moves all adjust moves to the front
    let subPuzzle = fullPuzzle.setSubgroup(generators);

    initCubeOri(subPuzzle, pieceList, ignore);

    subPuzzle.setAdjustMoves(adjust.length); // deal with adjust - setAdjustMoves passes in a NUMBER
    postMessage({value: 0, type: "set-depth"})

    if (prune.toLowerCase().includes("m")) {
        subPuzzle.createPrunSized(parseFloat(prune) * 1e6 );
    } else if (prune.toLowerCase().includes("k")) {
        subPuzzle.createPrunSized(parseFloat(prune) * 1e3 );
    } else {
        subPuzzle.createPrun(parseInt(prune));
    }

    return subPuzzle;
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

// END HTML-SIDE JS

function arraysEqual(arr1,arr2) {
    for (let i=0; i<arr1.length; i++) {
        if (arr1[i] !== arr2[i]) {return false}
    }
    return true
}

function cartesian(arrays) { 
	let singleStep = (arrays, add) => {return arrays.concat(...add.map(next => arrays.map( arr => arr.concat(next) )))} 
	let prod = [[]];
    for (arr of arrays) {prod = singleStep(prod, arr)}
    return prod;
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
    
    constructor (cubeOri, clockwiseMoves, clockwiseMoveStr, solvedState=null) {
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
    nextValid(prevMove, move) {
        while (move < this.moves.length) {
            move += 1;
            if (this.validPairs[prevMove][move]) {
                return move;
            }
        }
        return -1;
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
    nextValidInitial(move) {
        let x = move+1;
        while (x < this.moves.length) {
            if (!(this.adjustMovesTable[x])) {return x};
            x++;
        }
        return -1;
    }

    // stores first valid array of moves with given length in arr
    firstValidArray(length) {
        let arr = [this.nextValidInitial(-1)];
        for (let i=1; i<length; i++) {
            arr[i] = this.nextValid(arr[i-1],-1);
        }
        return arr;
    }

    *getAllSequences(seqLength) {
        if (seqLength === 0) {
            for (let sequence of this.adjustSequences) {
                yield sequence; // need to account for adjust moves
            }
            return;
        }
        postMessage({value: 1, type: "depthUpdate"});
        let arr = this.firstValidArray(seqLength);
        while (true) {
            for (let sequence of this.adjustSequences) {
                yield sequence.concat(arr); // need to account for adjust moves
            }
            let moveInd = seqLength-1;
            while (moveInd < seqLength) {
                if (moveInd) {
                    arr[moveInd] = this.nextValid(arr[moveInd-1],arr[moveInd])
                } else {
                    arr[0] = this.nextValidInitial(arr[0]);
                    if (arr[0] == -1) {return}
                }
                if (arr[moveInd] == -1) {moveInd--} else {moveInd++}
            }
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
                    throw this;
                }
            }
        }
        return result
    }
    
    // create a prune table up to a given depth
    createPrun(maxDepth) {
        let tempTable = new Map();
        for (let depth=0; depth<=maxDepth; depth++) {
            for (let sequence of this.getAllSequences(depth)) {
                let cubeStr = this.compressArr(this.execute(this.solved, sequence));
                if (!(tempTable.has(cubeStr))) {tempTable.set(cubeStr, depth)}
            }
        }
        this.pruneTable = tempTable;
        this.pruneDepth = maxDepth;
    }

    // same as createPrun, but automatically determines a depth given a max size
    createPrunSized(maxSize) {
        let tempTable = new Map();
        let prevSize = -1;
        let depth = 0;
        while (true) {
            for (let sequence of this.getAllSequences(depth)) {
                let cubeStr = this.compressArr(this.execute(this.solved, sequence));
                if (!(tempTable.has(cubeStr))) {tempTable.set(cubeStr, depth)}
            }
            console.log(tempTable.size+" at depth "+depth)
            if (tempTable.size**2/prevSize > maxSize) {break}
            depth++;
            prevSize = tempTable.size;
        }
        this.pruneTable = tempTable;
        this.pruneDepth = depth;
    }

    // read all solutions from a given state under the prune table's depth
    *readPrun(state, partialSolve=[], exactDepth=false, maxDepth=this.pruneDepth) { // maxDepth should be the same as the prune table's maxDepth
        for (let m=0; m<this.moves.length; m++) {
            if (partialSolve.length == 0 || this.validPairs[partialSolve[partialSolve.length-1]][m]) {
                let nextState = this.execute(state, [m]);
                let nextDistance = this.pruneTable.get(this.compressArr(nextState));
                if (nextDistance == 0) { 
                    if (maxDepth == 1 || !(exactDepth)) {
                        let fullSolve = partialSolve.concat(m); // TODO: Find which adjust sequence to apply, then apply it
                        if (!this.hasEndAdjust(fullSolve)) yield * [fullSolve];
                    }
                } else if (nextDistance < maxDepth) { // false if nextDistance is undefined
                    yield * this.readPrun(nextState, partialSolve.concat(m), exactDepth, maxDepth-1);
                }
            }
        }
    }

    // generating function that returns all solutions for a state
    *solve(state, searchDepth, startDepth=0) {
        for (let depth=startDepth; depth<=searchDepth; depth++) {
            for (let sequence of this.getAllSequences(depth)) {
                let nextState = this.execute(state, sequence);
                let thisDistance = this.pruneTable.get(this.compressArr(nextState));
                if (thisDistance !== undefined) {
                    if (sequence.length === 0) {
                        yield * this.readPrun(nextState, sequence, false);
                    } else {
                        let prevState = this.execute(nextState, [this.inverse[sequence[sequence.length-1]]]);
                        let prevDistance = this.pruneTable.get(this.compressArr(prevState));
                        if (prevDistance === undefined || prevDistance <= this.pruneDepth || thisDistance >= prevDistance) {
                            yield * this.readPrun(nextState, sequence, true, Math.max(this.pruneDepth, thisDistance)); // false, address underlying problem
                        }
                    }
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
            genArray.push(this.execute(this.nullmove, this.moveStrToList(gen)));
        }
        return new Puzzle(this.cubeOri.slice(), genArray, generators, this.solved.slice());
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
        }
        return Array.from(states.values());
    }

    getAdjustFromStr(adjStr) { // helper method for getReducedSet
        return (adjStr == "") ? [[]] : cartesian(splitSubgroupStr(adjStr).map(str => this.getMoveMultiples(this.moveStr.indexOf(str))));
    }

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

    moveStrToState(str) { // There are TONS of places where I'm using the long way that can be shortened.
        return this.execute(this.solved, this.moveStrToList(str))
    }

    compareStates(state1, state2) {
        for (let i=0; i<state1.length; i++) {
            if (state1[i] > state2[i]) {return 1}
            if (state1[i] < state2[i]) {return -1}
        }
        return 0;
    }

    getBatchStates(input, preAdjust, postAdjust) {
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
        states.sort((s1, s2) => this.compareStates(this.moveStrToState(s1), this.moveStrToState(s2)));
        return this.getReducedSet(states, preAdjust, postAdjust);
    }
}