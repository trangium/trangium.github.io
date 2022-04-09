function algSpeed(sequence, ignoreErrors=false, ignoreauf=false, wristMult=0.8, pushMult=1.3, ringMult=1.4, destabilize=0.5, addRegrip=1, double=1.65, sesliceMult=1.25, overWorkMult=2.25, moveblock=0.8, rotation=3.5) {
    function test(splitSeq, lGrip, rGrip, speed) {
        let lThumb = [-1, "home"]; // -1 to represent the time during AUF that you can set your fingers up
        let lIndex = [-1, "home"];
        let lMiddle = [-1, "home"];
        let lRing = [-1, "home"];
        let rThumb = [-1, "home"];
        let rIndex = [-1, "home"];
        let rMiddle = [-1, "home"];
        let rRing = [-1, "home"];
        let lOhCool = -1;
        let rOhCool = -1;
        let lWrist = lGrip;
        let rWrist = rGrip;
        let grip = 1;
        let udgrip = -1;
        let prevSpeed = null;
        let firstMoveSpeed = null;

        function overwork(finger, locationPrefer, penalty = overWorkMult) {
            if (finger[1] != locationPrefer) {
                if (speed - finger[0] < penalty) {
                    return penalty - speed + finger[0]
                }
            }
            return 0
        }
        for (let j = 0; j < splitSeq.length; j++) {
            let move = splitSeq[j];
            let normalMove = move.toUpperCase();
            let prevMove = (j==0?" ":splitSeq[j-1]).toUpperCase();
            if (prevSpeed !== null) {
                firstMoveSpeed = speed
                speed = prevSpeed
            }
            if (j < splitSeq.length - 1) {
                if ((move[0] == "U" && splitSeq[j + 1][0] == "D") || (move[0] == "D" && splitSeq[j + 1][0] == "U")) {
                    prevSpeed = speed;
                }
            }
            switch (normalMove) {
                case "R'":
                    if (rWrist == 2) { rWrist = 0 } else if (rWrist > -1 && !(lWrist >= 1 && rWrist <= 0)) { rWrist-- } else { return [j, speed, lWrist, rWrist-1, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])] }
                    speed += wristMult;
                    break;
                case "R":
                    if (rWrist < 2 && !(lWrist <= -1 && rWrist >= 0)) { rWrist++ } else { return [j, speed, lWrist, rWrist+1, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])] }
                    speed += wristMult;
                    break;
                case "R2":
                    if (rWrist >= 1 && lWrist < 1) { rWrist = -1 } else if (lWrist > -1) { rWrist += 2 } else { return [j, speed, lWrist, (rWrist>0)?rWrist-2:rWrist+2, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])] }
                    speed += double*wristMult;
                    break;
                case "U":
                    if (rWrist == 0 && (rThumb[0] + overWorkMult <= speed || rThumb[1] != "top") && rIndex[1] != "m") {
                        if (overwork(rIndex, "home") <= overwork(rMiddle, "home")) {
                            speed += overwork(rIndex, "home");
                            speed++;
                            rIndex = [speed, "uflick"];
                        } else {
                            speed += overwork(rMiddle, "home");
                            speed++;
                            rIndex = [speed, "uflick"];
                            rMiddle = [speed, "uflick"];                        
                        }
                    } else if (rWrist == 1 && lWrist == 0) {
                        speed += overwork(lIndex, "uflick");
                        if (prevMove == "B'") {speed += moveblock+pushMult} else if (prevMove[0] == "B'") {speed += moveblock*0.5+pushMult} else {speed += pushMult}
                        lIndex = [speed, "home"];
                    } else if (lWrist == 0 && prevMove[0] != "F" && prevMove[0] != "B") {
                        if (lIndex[1] == "uflick") {
                            speed += overwork(lIndex, "eido", 0.75*overWorkMult);
                            speed = Math.max(speed, lOhCool + 2.5);
                        } else {
                            speed += overwork(lIndex, "eido", 1.25*overWorkMult);
                        }
                        speed += 1.15*pushMult;
                        lIndex = [speed, "uflick"];
                        lOhCool = speed;
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "U'":
                    if (lWrist == 0 && (lThumb[0] + overWorkMult <= speed || lThumb[1] != "top") && lIndex[1] != "m") {
                        if (overwork(lIndex, "home") <= overwork(lMiddle, "home")) {
                            speed += overwork(lIndex, "home");
                            speed++;
                            lIndex = [speed, "uflick"];
                        } else {
                            speed += overwork(lMiddle, "home");
                            speed++;
                            lIndex = [speed, "uflick"];
                            lMiddle = [speed, "uflick"];                        
                        }
                    } else if (lWrist == 1 && rWrist == 0) {
                        speed += overwork(rIndex, "uflick");
                        if (prevMove == "B") {speed += moveblock+pushMult} else if (prevMove[0] == "B'") {speed += moveblock*0.5+pushMult} else {speed += pushMult}
                        rIndex = [speed, "home"];
                    } else if (rWrist == 0 && prevMove[0] != "F" && prevMove[0] != "B") {
                        if (rIndex[1] == "uflick") {
                            speed += overwork(rIndex, "eido", 0.75*overWorkMult);
                            speed = Math.max(speed, rOhCool + 2.5);
                        } else {
                            speed += overwork(rIndex, "eido", 1.25*overWorkMult);
                        }
                        speed += 1.15*pushMult;
                        rIndex = [speed, "uflick"];
                        rOhCool = speed;
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "U2":
                    if (rWrist == 0 && (lIndex[1] == "m" || lWrist != 0 || (Math.max(overwork(rIndex, "home"), overwork(rMiddle, "home"), overwork(rRing, "u2grip")) <= Math.max(overwork(lIndex, "home"), overwork(lMiddle, "home"), overwork(lRing, "u2grip"))))) {
                        speed += overwork(rIndex, "home");
                        speed += overwork(rMiddle, "home");
                        speed += overwork(rRing, "u2grip", moveblock*overWorkMult);
                        speed += double;
                        rIndex = [speed, "uflick"];
                        rMiddle = [speed, "uflick"];
                    } else if (lWrist == 0) {
                        speed += overwork(lIndex, "home");
                        speed += overwork(lMiddle, "home");
                        speed += overwork(lRing, "u2grip", moveblock*overWorkMult);
                        speed += double;
                        lIndex = [speed, "uflick"];
                        lMiddle = [speed, "uflick"];
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "D":
                    if (lWrist == 0 && (rWrist != 0 || (Math.max(overwork(lRing, "home"), overwork(lMiddle, "home")) <= Math.max(overwork(rRing, "dflick"), overwork(rMiddle, "home"))))) {
                        speed += overwork(lRing, "home");
                        speed += overwork(lMiddle, "home");
                        if (prevMove[0] == "B") {speed += moveblock*0.5+ringMult} else {speed += ringMult}
                        lRing = [speed, "dflick"];
                    } else if (rWrist == 0 && prevMove[0] != "B") {
                        speed += overwork(rRing, "dflick");
                        speed += overwork(rMiddle, "home");
                        speed += ringMult*pushMult;
                        rRing = [speed, "home"];
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "D'":
                    if (rWrist == 0 && (lWrist != 0 || (Math.max(overwork(rRing, "home"), overwork(rMiddle, "home")) <= Math.max(overwork(lRing, "dflick"), overwork(lMiddle, "home"))))) {
                        speed += overwork(rRing, "home");
                        speed += overwork(rMiddle, "home");
                        if (prevMove[0] == "B") {speed += moveblock*0.5+ringMult} else {speed += ringMult}
                        rRing = [speed, "dflick"]
                    } else if (lWrist == 0 && prevMove[0] != "B") {
                        speed += overwork(lRing, "dflick");
                        speed += overwork(lMiddle, "home");
                        speed += ringMult*pushMult;
                        lRing = [speed, "home"];
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "D2":
                    if (rWrist == 0 && (lWrist != 0 || (Math.max(overwork(rMiddle, "home"), overwork(rRing, "home")) <= Math.max(overwork(lMiddle, "home"), overwork(lRing, "home"))))) {
                        speed += overwork(rMiddle, "home");
                        speed += overwork(rRing, "home");
                        if (prevMove[0] == "B") {speed += moveblock*0.5+double*ringMult} else {speed += double*ringMult}
                        rRing = [speed, "dflick"]
                    } else if (lWrist == 0) {
                        speed += overwork(lMiddle, "home");
                        speed += overwork(lRing, "home");
                        if (prevMove[0] == "B") {speed += moveblock*0.5+double*ringMult} else {speed += double*ringMult}
                        lRing = [speed, "dflick"]
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "F":
                    if (rWrist == -1) {
                        speed += overwork(rIndex, "home");
                        speed += 1;
                        rIndex = [speed, "uflick"]
                    } else if (lWrist == 1 && move != "f") {
                        speed += overwork(lRing, "home");
                        if (prevMove[0] == "D") {speed += moveblock*0.5+ringMult} else {speed += 1}
                        lRing = [speed, "dflick"];
                    } else if (rWrist == 1 && prevMove[0] != "D" && move != "f") {
                        speed += overwork(rRing, "dflick");
                        speed += ringMult*pushMult;
                        rRing = [speed, "home"]
                    } else if (lWrist == -1 && rWrist == 0 && overwork(rIndex, "uflick") == 0) {
                        speed += 1;
                        rIndex = [speed, "fflick"];
                    } else if (lWrist == -1 && overwork(lIndex, "uflick") == 0 && prevMove[0] != "U") {
                        speed += pushMult;
                        lIndex = [speed, "home"]
                    } else if (lWrist == -1 && grip == -1) {
                        speed += overwork(lThumb, "top", 0.9*overWorkMult)
                        speed += overwork(lIndex, "top");
                        if (prevMove[0] == "D") {speed += 1.8} else {speed += 1};
                        lWrist++;
                        lThumb = [speed, "leftu"];
                        lIndex = [speed, "top"];
                    } else if (lWrist == 0 && grip == -1) {
                        speed += overwork(lThumb, "bottom");
                        speed += overwork(lIndex, "top");
                        if (prevMove[0] == "D") {speed += 2.05} else {speed += 1.25};
                        lThumb = [speed, "top"]
                        lIndex = [speed, "top"] 
                    } else if (rWrist == 0 && lWrist == 0 && move == "f") {
                        speed += overwork(rIndex,"uflick");
                        speed += overwork(rMiddle,"home");
                        speed += 1;
                        rIndex = [speed, "fflick"];
                    } else if (j == 0 && rWrist == 0 && lWrist == 0) {
                        speed += overwork(rThumb,"top");
                        speed += 1;
                        rThumb = [speed, "rdown"];
                        rMiddle = [speed, "uflick"];
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "F'":
                    if (lWrist == -1) {
                        speed += overwork(lIndex, "home");
                        speed += 1;
                        lIndex = [speed, "uflick"]
                    } else if (rWrist == 1 && move != "f") {
                        speed += overwork(rRing, "home");
                        if (prevMove[0] == "D") {speed += moveblock*0.5+ringMult} else {speed += 1}
                        rRing = [speed, "dflick"];
                    } else if (lWrist == 1 && prevMove[0] != "D" && move != "f") {
                        speed += overwork(lRing, "dflick");
                        speed += ringMult*pushMult;
                        lRing = [speed, "home"]
                    } else if (rWrist == -1 && lWrist == 0 && overwork(lIndex, "uflick") == 0) {
                        speed += 1;
                        lIndex = [speed, "fflick"];
                    } else if (rWrist == -1 && overwork(rIndex, "uflick") == 0 && prevMove[0] != "U") {
                        speed += pushMult;
                        rIndex = [speed, "home"]
                    } else if (rWrist == -1 && grip == 1) {
                        speed += overwork(rThumb, "top", 0.9*overWorkMult)
                        speed += overwork(rIndex, "top");
                        if (prevMove[0] == "D") {speed += 1.8} else {speed += 1};
                        rWrist++;
                        rThumb = [speed, "rightu"];
                        rIndex = [speed, "top"]
                    } else if (rWrist == 0 && grip == 1) {
                        speed += overwork(rThumb, "bottom")
                        speed += overwork(rIndex, "top");
                        if (prevMove[0] == "D") {speed += 2.05} else {speed += 1.25};
                        rThumb = [speed, "top"]
                        rIndex = [speed, "top"]     
                    } else if (lWrist == 0 && rWrist == 0 && move == "f'") {
                        speed += overwork(lIndex,"uflick");
                        speed += overwork(lMiddle,"home");
                        speed += 1;
                        lIndex = [speed, "fflick"];} 
                    else if (j == 0 && rWrist == 0 && lWrist == 0) {
                        speed += overwork(lThumb,"top");
                        speed += 1;
                        lThumb = [speed, "rdown"];
                        lMiddle = [speed, "uflick"];
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "F2":
                    if (rWrist == -1 && (lWrist != -1 || (Math.max(overwork(rIndex, "home"), overwork(rMiddle, "home"), overwork(rRing, "u2grip")) <= Math.max(overwork(lIndex, "home"), overwork(lMiddle, "home"), overwork(lRing, "u2grip"))))) {
                        speed += overwork(rIndex, "home");
                        speed += overwork(rMiddle, "home");
                        speed += overwork(rRing, "u2grip");
                        speed += double;
                        rIndex = [speed, "uflick"];
                        rMiddle = [speed, "uflick"]
                    } else if (lWrist == -1) {
                        speed += overwork(lIndex, "home");
                        speed += overwork(lMiddle, "home");
                        speed += overwork(lRing, "u2grip");
                        speed += double;
                        lIndex = [speed, "uflick"];
                        lMiddle = [speed, "uflick"];
                    } else if (rWrist == 1 && (lWrist != 1 || (Math.max(overwork(rMiddle, "home"), overwork(rRing, "home")) <= Math.max(overwork(lMiddle, "home"), overwork(lRing, "home"))))) {
                        speed += overwork(rMiddle, "home");
                        speed += overwork(rRing, "home");
                        if (prevMove[0] == "D") {speed += double*ringMult+moveblock*0.5} else {speed += double*ringMult}
                        rRing = [speed, "dflick"]
                    } else if (lWrist == 1) {
                        speed += overwork(lMiddle, "home");
                        speed += overwork(lRing, "home");
                        if (prevMove[0] == "D") {speed += double*ringMult+moveblock*0.5} else {speed += double*ringMult}
                        lRing = [speed, "dflick"]
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "L":
                    if (lWrist == 2) { lWrist = 0 } else if (lWrist > -1 && !(rWrist >= 1 && lWrist <= 0)) { lWrist-- } else { return [j, speed, lWrist-1, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])] }
                    speed += wristMult;
                    break;
                case "L'":
                    if (lWrist < 2 && !(rWrist <= -1 && lWrist >= 0)) { lWrist++ } else { return [j, speed, lWrist+1, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])] }
                    speed += wristMult;
                    break;
                case "L2":
                    if (lWrist >= 1 && rWrist < 1) { lWrist = -1 } else if (rWrist > -1) { lWrist += 2 } else { return [j, speed, (lWrist>0)?lWrist-2:lWrist+2, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])] }
                    speed += double*wristMult;
                    break;
                case "B":
                    if (rWrist == 1) {
                        speed += overwork(rIndex, "home");
                        speed++;
                        rIndex = [speed, "uflick"];
                    } else if (lWrist == -1) {
                        speed += overwork(lRing, "home");
                        speed += overwork(lMiddle, "home");
                        if (prevMove[0] == "U") {speed += moveblock*0.5+ringMult} else {speed += ringMult}
                        lRing = [speed, "dflick"];
                    } else if (lWrist == 1 && prevMove[0] != "U" && prevMove[0] != "D") {
                        if (lIndex[1] == "uflick") {
                            speed += overwork(lIndex, "eido", 0.75*overWorkMult);
                            speed = Math.max(speed, lOhCool + 2.5);
                        } else {
                            speed += overwork(lIndex, "eido", 1.25*overWorkMult);
                        }
                        speed += 1.15*pushMult;
                        lIndex = [speed, "uflick"];
                        lOhCool = speed;
                    } else if (lWrist == 0 && (rWrist == 1 || rWrist == -1)) {
                        speed += overwork(lIndex, "top", 0.9*overWorkMult);
                        if (prevMove[0] == "U") {speed += 1.45} else {speed += 1}
                        lIndex = [speed, "leftdb"];
                    } else if (rWrist == -1 && prevMove[0] != "U") {
                        speed += overwork(rRing, "dflick");
                        speed += overwork(rMiddle, "home");
                        speed += ringMult*pushMult;
                        rRing = [speed, "home"]
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "B'":
                    if (lWrist == 1) {
                        speed += overwork(lIndex, "home");
                        speed++;
                        lIndex = [speed, "uflick"];
                    } else if (rWrist == -1) {
                        speed += overwork(rRing, "home");
                        speed += overwork(rMiddle, "home");
                        if (prevMove[0] == "U") {speed += moveblock*0.5+ringMult} else {speed += ringMult}
                        rRing = [speed, "dflick"];
                    } else if (rWrist == 1 && prevMove[0] != "U" && prevMove[0] != "D") {
                        if (rIndex[1] == "uflick") {
                            speed += overwork(rIndex, "eido", 0.75*overWorkMult);
                            speed = Math.max(speed, rOhCool + 2.5);
                        } else {
                            speed += overwork(rIndex, "eido", 1.25*overWorkMult);
                        }
                        speed += 1.15*pushMult;
                        rIndex = [speed, "uflick"];
                        rOhCool = speed;
                    } else if (rWrist == 0 && (lWrist == 1 || lWrist == -1)) {
                        speed += overwork(rIndex, "top", 0.9*overWorkMult);
                        if (prevMove[0] == "U") {speed += 1.45} else {speed += 1}
                        rIndex = [speed, "rightdb"];
                    } else if (lWrist == -1 && prevMove[0] != "U") {
                        speed += overwork(lRing, "dflick");
                        speed += overwork(lMiddle, "home");
                        speed += ringMult*pushMult;
                        lRing = [speed, "home"];
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "B2":
                    if (rWrist == 1 && (lWrist != 1 || (Math.max(overwork(rIndex, "home"), overwork(rMiddle, "home"), overwork(rRing, "u2grip")) <= Math.max(overwork(lIndex, "home"), overwork(lMiddle, "home"), overwork(lRing, "u2grip"))))) {
                        speed += overwork(rIndex, "home");
                        speed += overwork(rMiddle, "home");
                        speed += overwork(rRing, "u2grip");
                        speed += double;
                        rIndex = [speed, "uflick"];
                        rMiddle = [speed, "uflick"]
                    } else if (lWrist == 1) {
                        speed += overwork(lIndex, "home");
                        speed += overwork(lMiddle, "home");
                        speed += overwork(lRing, "u2grip");
                        speed += double;
                        lIndex = [speed, "uflick"];
                        lMiddle = [speed, "uflick"];
                    } else if (lWrist == -1 && (rWrist != -1 || (Math.max(overwork(rMiddle, "home"), overwork(rRing, "home")) > Math.max(overwork(lMiddle, "home"), overwork(lRing, "home"))))) {
                        speed += overwork(lMiddle, "home");
                        speed += overwork(lRing, "home");
                        if (prevMove[0] == "U") {speed += moveblock*0.5+double*ringMult} else {speed += double*ringMult}
                        lRing = [speed, "dflick"]
                    } else if (rWrist == -1) {
                        speed += overwork(rMiddle, "home");
                        speed += overwork(rRing, "home");
                        if (prevMove[0] == "U") {speed += moveblock*0.5+double*ringMult} else {speed += double*ringMult}
                        rRing = [speed, "dflick"]
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "S":
                    if (rWrist == 0 && (lWrist != 0 || overwork(rIndex, "top", 1.25*overWorkMult) <= (moveblock*0.5+pushMult-1)*sesliceMult)) {
                        speed += overwork(rIndex, "top", 1.25*overWorkMult);
                        speed += sesliceMult;
                        rIndex = [speed, "sflick"];
                    } else if (lWrist == 0 && rWrist == -1) {
                        speed += overwork(rIndex, "home", 1.25*overWorkMult);
                        speed += overwork(rThumb, "top", 1.25*overWorkMult);
                        speed += overwork(rMiddle, "home", 1.25*overWorkMult)
                        speed += sesliceMult;
                        rThumb = [speed, "top"];
                        rMiddle = [speed, "eflick"];
                    } else if (lWrist == 0 && (rWrist == 0 || (rWrist == 1 && (prevMove == "R" || prevMove == "L")))) {
                        speed += overwork(lIndex, "uflick", 1.25*overWorkMult);
                        if (prevMove[0] == "U") {speed += moveblock*0.5+pushMult*sesliceMult} else {speed += pushMult*sesliceMult}
                        lIndex = [speed, "top"];
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])];
                    }
                    break;
                case "S'":
                    if (lWrist == 0 && (rWrist != 0 || overwork(lIndex, "top", 1.25*overWorkMult) <= (moveblock*0.5+pushMult-1)*sesliceMult)) {
                        speed += overwork(lIndex, "top", 1.25*overWorkMult);
                        speed += sesliceMult;
                        lIndex = [speed, "sflick"];
                    } else if (rWrist == 0 && lWrist == -1) {
                        speed += overwork(lIndex, "home", 1.25*overWorkMult);
                        speed += overwork(lThumb, "bottom", 1.25*overWorkMult);
                        speed += overwork(lMiddle, "home", 1.25*overWorkMult);
                        speed += sesliceMult;
                        lThumb = [speed, "top"];
                        lMiddle = [speed, "eflick"];
                    } else if (rWrist == 0 && (lWrist == 0 || (lWrist == 1 && (prevMove == "R" || prevMove == "L")))) {
                        speed += overwork(rIndex, "uflick", 1.25*overWorkMult);
                        if (prevMove[0] == "U") {speed += moveblock*0.5+pushMult*sesliceMult} else {speed += pushMult*sesliceMult}
                        rIndex = [speed, "top"];
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])];
                    }
                    break;
                case "S2":
                    if ((rWrist == -1 || rWrist == 1) && lWrist == 0) {
                        speed += overwork(rThumb, "home");
                        speed += overwork(rIndex, "home");
                        speed += overwork(rMiddle, "home");
                        speed += overwork(rRing, "u2grip");
                        speed += sesliceMult*double;
                        rMiddle = [speed, "e"];
                        rIndex = [speed, "e"];
                    } else if ((lWrist == -1 || lWrist == 1) && rWrist == 0) {
                        speed += overwork(lThumb, "home");
                        speed += overwork(lIndex, "home");
                        speed += overwork(lMiddle, "home");
                        speed += overwork(lRing, "u2grip");
                        speed += sesliceMult*double;
                        rMiddle = [speed, "e"];
                        rIndex = [speed, "e"];
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "E":
                    if ((rWrist == 1 || rWrist == -1) && lWrist == 0) {
                        speed += overwork(lIndex, "home");
                        speed += sesliceMult;
                        lIndex = [speed, "e"];
                    } else if ((lWrist == 1 || lWrist == -1) && rWrist == 0 && prevMove[0] != "B") {
                        speed += overwork(rIndex, "e");
                        speed += sesliceMult*pushMult;
                        rIndex = [speed, "home"];
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "E'":
                    if ((lWrist == 1 || lWrist == -1) && rWrist == 0) {
                        speed += overwork(rIndex, "home");
                        speed += sesliceMult;
                        rIndex = [speed, "e"];
                    } else if ((rWrist == 1 || rWrist == -1) && lWrist == 0 && prevMove[0] != "B") {
                        speed += overwork(lIndex, "e");
                        speed += sesliceMult*pushMult;
                        lIndex = [speed, "home"];
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "E2":
                    if ((lWrist == 1 || lWrist == -1) && rWrist == 0) {
                        speed += overwork(rIndex, "home");
                        speed += overwork(rMiddle, "home");
                        speed += overwork(rRing, "u2grip");
                        speed += sesliceMult*double;
                        rIndex = [speed, "e"];
                        rMiddle = [speed, "e"];
                    } else if ((rWrist == 1 || rWrist == -1) && lWrist == 0) {
                        speed += overwork(lIndex, "home");
                        speed += overwork(lMiddle, "home");
                        speed += overwork(lRing, "u2grip");
                        speed += sesliceMult*double;
                        lIndex = [speed, "e"];
                        lMiddle = [speed, "e"];
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "M'":
                    if (lWrist == 0) {
                        speed += overwork(lThumb, "home");
                        speed += overwork(lIndex, "m");
                        speed += overwork(lMiddle, "m");
                        speed += overwork(lRing, "m");
                        if (prevMove[0] == "B") {speed += 1.8} else {speed += 1}
                        lThumb = [speed, "home"];
                        lIndex = [speed, "m"];
                        lMiddle = [speed, "mflick"];
                        lRing = [speed, "m"]
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "M":
                    if (lWrist == 0 && prevMove[0] != "B") {
                        speed += overwork(lThumb, "home");
                        speed += overwork(lIndex, "m");
                        speed += overwork(lMiddle, "mflick", 1.25*overWorkMult);
                        speed += overwork(lRing, "m");
                        speed += pushMult;
                        lThumb = [speed, "home"];
                        lIndex = [speed, "m"];
                        lMiddle = [speed, "m"];
                        lRing = [speed, "m"]
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "M2":
                    if (lWrist == 0) {
                        speed += overwork(lThumb, "home");
                        speed += overwork(lIndex, "m");
                        speed += overwork(lMiddle, "m");
                        speed += overwork(lRing, "m");
                        if (prevMove[0] == "B") {speed += moveblock+double} else {speed += double}
                        lThumb = [speed, "home"];
                        lIndex = [speed, "m"];
                        lMiddle = [speed, "mflick"];
                        lRing = [speed, "m"]
                    } else {
                        return [j, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "X":
                    lWrist += 1;
                    rWrist += 1;
                    if (lWrist > 1 || rWrist > 1) {
                        return [j+1, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "X'":
                    lWrist -= 1;
                    rWrist -= 1;
                    if (lWrist < -1 || rWrist < -1) {
                        return [j+1, speed, lWrist, rWrist, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "X2":
                    if (lWrist >= 1 && rWrist >= 1) {
                        lWrist -= 2;
                        rWrist -= 2;
                    } else if (lWrist <= -1 && rWrist <= -1) {
                        lWrist += 2;
                        rWrist += 2;
                    } else if (lWrist+rWrist > 0) {
                        return [j, speed, lWrist-2, rWrist-2, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    } else {
                        return [j, speed, lWrist+2, rWrist+2, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]
                    }
                    break;
                case "Y": case "Y'": case "Z": case "Z'":
                    speed += rotation;
                    return [j+1, speed, 0, 0, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]                  
                    break;
                case "Y2": case "Z2":
                    speed += rotation*double;
                    return [j+1, speed, 0, 0, Math.max(lThumb[0], lIndex[0], lMiddle[0], lRing[0]), Math.max(rThumb[0], rIndex[0], rMiddle[0], rRing[0])]                  
                    break;
                default:
                    return "Unknown move: "+move
            }
            if (firstMoveSpeed !== null) {
                speed = Math.max(firstMoveSpeed, speed) + 0.5;
                prevSpeed = null;
                firstMoveSpeed = null;
            }
            if ((move[0] == "R" || move[0] == "l") && grip == -1) {
                grip = 1;
                speed += 0.65;
            } else if ((move[0] == "r" || move[0] == "L") && grip == 1) {
                grip = -1;
                speed += 0.65;
            }
            if ((move[0] == "d") && udgrip == -1) {
                udgrip = 1;
                speed += 2.25;
            } else if ((move[0] == "U" || move[0] == "u") && udgrip == 1) {
                udgrip = -1;
                speed += 2.25;
            }
            if (j >= 2) {
                if ((normalMove == "R" && move == splitSeq[j - 2] && splitSeq[j - 1].toUpperCase() == "U'") || (normalMove == "R'" && move == splitSeq[j - 2] && splitSeq[j - 1].toUpperCase() == "U")) {
                    speed -= 0.5;
                } else if ((normalMove == "R" && move == splitSeq[j - 2] && splitSeq[j - 1].toUpperCase() == "D'" && rWrist == 1) || (normalMove == "R'" && move == splitSeq[j - 2] && splitSeq[j - 1].toUpperCase() == "D")) {
                    speed -= 0.3;
                }
            }
            if (normalMove == "U" && (lWrist == -1 || rWrist == -1)) {
                speed += destabilize;
            }
            if (normalMove == "B" && (lWrist == 0 || rWrist == 0)) {
                speed += destabilize;
            }
            if (normalMove == "D" && (lWrist == 1 || rWrist == 1)) {
                speed += destabilize;
            }
            if (normalMove == "S" && (lWrist == 1 || rWrist == 1 || lWrist == -1 || rWrist == -1)) {
                speed += destabilize;
            }
            if (normalMove == "E" && (lWrist == 0 || rWrist == 0)) {
                speed += destabilize;
            }
        }
        return [-1, speed, lGrip, rGrip]
    }
    let splitSeq = sequence.split(" ");
    let trueSplitSeq = [];
    for (let i=0; i<splitSeq.length; i++) {
        if (ignoreErrors) {
            if (["r","r2","r'","u","u'","u2","f","f2","f'","d","d2","d'","l","l2","l'","b","b2","b'","m","m2","m'","s","s2","s'","e","e2","e'","x","x'","x2","y","y'","y2","z","z'","z2"].includes(splitSeq[i].toLowerCase())) {
                trueSplitSeq.push(splitSeq[i])
            }
        } else {
            if (splitSeq[i] != "") {
                trueSplitSeq.push(splitSeq[i])
            }
        }
    }
    splitSeq = trueSplitSeq.slice()
    if (ignoreauf) {
        if (splitSeq.length>=1) {
            if (splitSeq[0][0]=="U") {
                splitSeq.shift()
            } else if (splitSeq.length >= 2) {
                if (splitSeq[0][0].toLowerCase()=="d" && splitSeq[1][0]=="U") {
                    splitSeq[1] = splitSeq[0]
                    splitSeq.shift()
                }
            }
        }
        if (splitSeq.length>=1) {
            if (splitSeq[splitSeq.length-1][0]=="U") {
                splitSeq.pop()
            } else if (splitSeq.length >= 2) {
                if (splitSeq[splitSeq.length-1][0].toLowerCase()=="d" && splitSeq[splitSeq.length-2][0]=="U") {
                    splitSeq[splitSeq.length-2] = splitSeq[splitSeq.length-1]
                    splitSeq.pop()
                }
            }
        }
    }
    let tests = [test(splitSeq, 0, 0, 0), test(splitSeq, 0, -1, 1+addRegrip), test(splitSeq, 0, 1, 1+addRegrip), test(splitSeq, -1, 0, 1+addRegrip), test(splitSeq, 1, 0, 1+addRegrip)]
    while (true) {
        for (let i=0; i<tests.length; i++) {
            if (tests[i][0] == "U") { // I have no idea what this code does
                return tests[i]
            }
        }
        let bestTest = tests[0]
        for (let i = 1; i < tests.length; i++) {
            let compTest = tests[i];
            if (compTest[0] == -1 && (bestTest[0] != -1 || bestTest[1] > compTest[1])) { bestTest = compTest }
            else if (compTest[0] > bestTest[0] && bestTest[0] != -1) { bestTest = compTest }
            else if (compTest[0] == bestTest[0] && compTest[1] < bestTest[1] && bestTest[0] != -1) { bestTest = compTest }
        }
        if (bestTest[0] == -1) { return Math.round(bestTest[1] * 10) / 10 }
        tests = [];

        let prevMoveType = bestTest[0] >= 1 ? splitSeq[bestTest[0] - 1][0] : " ";
        let prev2Type = bestTest[0] >= 2 ? splitSeq[bestTest[0] - 2][0] : " ";
        let doubleRegrip = false;

        if ((bestTest[2] > 1 || bestTest[2] < -1) && (bestTest[3] > 1 || bestTest[3] < -1)) {
            doubleRegrip = true;
        }

        for (let leftWrist = -1; leftWrist < 2; leftWrist++) {
            for (let rightWrist = -1; rightWrist < 2; rightWrist++) {
                let leftMatch = (bestTest[2] == leftWrist);
                let rightMatch = (bestTest[3] == rightWrist);
                if (["X","x","Y","y","Z","z"].includes(prevMoveType)) { // rotation handling
                    tests.push(test(splitSeq.slice(bestTest[0]), leftWrist, rightWrist, bestTest[1]));
                }
                else {
                    let penalty = doubleRegrip?(rotation*double):2; // double regrips should be exceedingly rare, so penalty would almost always equal 2
                    let rMoveLatency;
                    if (prevMoveType == "R" || prev2Type == "R" || prevMoveType == "r" || prev2Type == "r") { rMoveLatency = 1 } else { rMoveLatency = 0 }
                    let lMoveLatency;
                    if (prevMoveType == "L" || prev2Type == "L" || prevMoveType == "l" || prev2Type == "l") { lMoveLatency = 1 } else { lMoveLatency = 0 }
                    if (leftMatch || doubleRegrip) {
                        let rHandLatency = Math.max(0, 2 - (bestTest[1] - bestTest[5])); // time between last right hand motion and now
                        penalty = Math.max(rHandLatency, rMoveLatency, lMoveLatency*2)
                        tests.push(test(splitSeq.slice(bestTest[0]), leftWrist, rightWrist, bestTest[1] + penalty + addRegrip))
                    } else if (rightMatch || doubleRegrip) {
                        let lHandLatency = Math.max(0, 2 - (bestTest[1] - bestTest[4])); // time between last right hand motion and now
                        penalty = Math.max(lHandLatency, lMoveLatency, rMoveLatency*2)
                        tests.push(test(splitSeq.slice(bestTest[0]), leftWrist, rightWrist, bestTest[1] + penalty + addRegrip))
                    }
                }
            }
        }
        splitSeq = splitSeq.slice(bestTest[0]);
    }
}