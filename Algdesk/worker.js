let subPuzzle;
let fullPuzzle;
let input;

self.onmessage = function (msg) {
    let inputs = msg.data;
    for (let i=0; i<inputs.length; i++) {
        input = inputs[i];
        calc();
    }
    postMessage({value: null, type: "stop"});
};

function setPuzzle() {
    // Deal with moves input

    let moves = input.puzzle;
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
                move[cycle[i+1][0]] = cycle[i][0]+oriMult*mod(cycle[i][1],cubeOri[cycle[i][0]]); // there could be bugs here
            }
            if (cycle.length==1 || (cycle[0][0] != cycle[cycle.length-1][0])) {
                move[cycle[0][0]] = cycle[cycle.length-1][0]+oriMult*mod(cycle[cycle.length-1][1],cubeOri[cycle[cycle.length-1][0]]);
            }
        }
        moveList.push(move);
    }

    // Deal with equivalences
    let splitEquivalences = input.ignore.split('{');
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
    subPuzzle = new Puzzle(cubeOri.slice(), moveList.slice(), clockwiseMoveStr.slice(), solvedState.slice());
    fullPuzzle = new Puzzle(cubeOri.slice(), moveList.slice(), clockwiseMoveStr.slice(), solvedState.slice());
    return pieceList;
}

function calc() {
    function removeBrackets(s) { // Removes (), {}, <>, and []
        return s.replace(/\(|\)|\[|\]|{|}|<|>/g, "");
    }
    let pieceList = setPuzzle(); // loses efficiency if puzzle has not changed, so update this

    let generators = removeBrackets(input.subgroup).replace(",","").split(" ");
    if (input.subgroup.replace(" ","").length > 0) {
        subPuzzle = fullPuzzle.setSubgroup(generators);
    }

    let lines = input.ignore.split("\n");
    for (let i=0; i<lines.length; i++) {
        let line = lines[i];
        if (line.includes(":")) {
            let numOri = parseInt(line.split(":")[0]);
            let orientData = line.split(":")[1].split(" ");
            for (let i=0; i<orientData.length; i++) {
                let pieceIndex = pieceList.indexOf(removeBrackets(orientData[i]));
                if (pieceIndex !== -1) {subPuzzle.cubeOri[pieceIndex] = numOri}
            }
        }
    }

    subPuzzle.createPrun(parseInt(input.prune));

    let solve = subPuzzle.solve(fullPuzzle.execute(subPuzzle.solved, fullPuzzle.moveStrToList(removeBrackets(input.solve))), isNaN(parseInt(input.search))?Infinity:parseInt(input.search));
    function nextSolution() {
        let solution = solve.next();
        while (solution.done == false) {
            postMessage({value: subPuzzle.moveListToStr(solution.value), type: "solution"});
            solution = solve.next();
        }
    }
    nextSolution();
}

// END HTML-SIDE JS

function arraysEqual(arr1,arr2) {
    for (let i=0; i<arr1.length; i++) {
        if (arr1[i] !== arr2[i]) {return false}
    }
    return true
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
    }

    // returns next valid move given previous move. For example, U U2 is invalid, but U R is valid.
    nextValid(prevMove, move) {
        while (move < this.pcCount) {
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

    // advances an array of moves to the next valid array. returns the length of the array.
    advance(arr) {
        let moveInd = arr.length-1;
        while (moveInd < arr.length) {
            if (moveInd) {
                arr[moveInd] = this.nextValid(arr[moveInd-1],arr[moveInd])
            } else {
                arr[0] = arr[0]+1;
                if (arr[0] == this.moves.length) {
                    arr[0] = 0;
                    let newLength = arr.length+1
                    for (let i=1; i<newLength; i++) {
                        arr[i] = this.nextValid(arr[i-1],-1)
                    }
                    postMessage({value:1, type: "depthUpdate"});
                    return newLength
                }
            }
            if (arr[moveInd] == -1) {moveInd--} else {moveInd++}
        }
        return arr.length
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
                    throw this;
                }
            }
        }
        return result
    }
    
    // create a prune table
    createPrun(maxDepth) {
        let tempTable = new Map();
        let attempt = [0];
        let depth = 1;
        while (depth <= maxDepth) {
            let cubeStr = this.compressArr(this.execute(this.solved, attempt));
            if (!(tempTable.has(cubeStr))) {tempTable.set(cubeStr, depth)}
            depth = this.advance(attempt);
        }
        this.pruneTable = tempTable;
        this.pruneDepth = maxDepth;
    }

    // read all solutions from a given state under the prune table's depth
    *readPrun(state, exactDepth=false, partialSolve=[], maxDepth=this.pruneDepth) { // maxDepth should be the same as the prune table's maxDepth
        for (let m=0; m<this.moves.length; m++) {
            if (partialSolve.length == 0 || this.validPairs[partialSolve[partialSolve.length-1]][m]) {
                let nextState = this.execute(state, [m]);
                let nextDistance = this.pruneTable.get(this.compressArr(nextState));
                if (arraysEqual(nextState, this.solved)) {
                    if (maxDepth == 1 || !(exactDepth)) {
                        let fullSolve = partialSolve.concat(m);
                        yield * [fullSolve];
                    }
                } else if (nextDistance < maxDepth) { // false if nextDistance is undefined
                    yield * this.readPrun(nextState, exactDepth, partialSolve.concat(m), maxDepth-1);
                }
            }
        }
    }

    *solve(state, searchDepth) {
        yield * this.readPrun(state);
        let attempt = [0];
        let depth = 1;
        while (depth <= searchDepth) {
            let nextState = this.execute(state, attempt);
            if (this.pruneTable.get(this.compressArr(nextState)) <= this.pruneDepth) {
                yield * this.readPrun(nextState, true, attempt);
            }
            depth = this.advance(attempt);
        }                
    }

    setSubgroup(generators) {
        let genArray = [];
        for (let i=0; i<generators.length; i++) {
            let gen = generators[i];
            genArray.push(this.execute(this.nullmove, this.moveStrToList(gen)));
        }
        return new Puzzle(this.cubeOri.slice(), genArray, generators, this.solved.slice());
    }
}