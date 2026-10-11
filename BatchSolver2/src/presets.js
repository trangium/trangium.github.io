// Built-in puzzle definitions (`Name:` defines the move and all of its multiples).
// Generated once from the original Batch Solver definitions; the 3x3x3 follows the
// custom-puzzle documentation to the letter (six separate centers).

export const PRESETS = {
    "3x3x3": {
        puzzle: `U: (UF UL UB UR) (UFR UFL UBL UBR)
R: (UR BR DR FR) (UFR-1 UBR+1 DBR-1 DFR+1)
F: (UF+1 FR+1 DF+1 FL+1) (UFR+1 DFR-1 DFL+1 UFL-1)
D: (DF DR DB DL) (DFR DBR DBL DFL)
L: (UL FL DL BL) (UFL+1 DFL-1 DBL+1 UBL-1)
B: (UB+1 BL+1 DB+1 BR+1) (UBR-1 UBL+1 DBL-1 DBR+1)
u: (UF UL UB UR) (UFR UFL UBL UBR) (FR+1 FL+1 BL+1 BR+1) (F L B R)
r: (UR BR DR FR) (UFR-1 UBR+1 DBR-1 DFR+1) (UF+1 UB+1 DB+1 DF+1) (U B D F)
f: (UF+1 FR+1 DF+1 FL+1) (UFR+1 DFR-1 DFL+1 UFL-1) (UR+1 DR+1 DL+1 UL+1) (U R D L)
d: (DF DR DB DL) (DFR DBR DBL DFL) (FR+1 BR+1 BL+1 FL+1) (F R B L)
l: (UL FL DL BL) (UFL+1 DFL-1 DBL+1 UBL-1) (UF+1 DF+1 DB+1 UB+1) (U F D B)
b: (UB+1 BL+1 DB+1 BR+1) (UBR-1 UBL+1 DBL-1 DBR+1) (UR+1 UL+1 DL+1 DR+1) (U L D R)
M: (UF+1 DF+1 DB+1 UB+1) (U F D B)
S: (UR+1 DR+1 DL+1 UL+1) (U R D L)
E: (FR+1 BR+1 BL+1 FL+1) (F R B L)
x: (UR BR DR FR) (UFR-1 UBR+1 DBR-1 DFR+1) (UL BL DL FL) (UFL+1 UBL-1 DBL+1 DFL-1) (UF+1 UB+1 DB+1 DF+1) (U B D F)
y: (UF UL UB UR) (UFR UFL UBL UBR) (DF DL DB DR) (DFR DFL DBL DBR) (FR+1 FL+1 BL+1 BR+1) (F L B R)
z: (UF+1 FR+1 DF+1 FL+1) (UFR+1 DFR-1 DFL+1 UFL-1) (UB+1 BR+1 DB+1 BL+1) (UBR-1 DBR+1 DBL-1 UBL+1) (UR+1 DR+1 DL+1 UL+1) (U R D L)`,
        ignore: "",
        image: "3x3x3",
    },
    "2x2x2": {
        puzzle: `U: (UFR UFL UBL UBR)
R: (UFR-1 UBR+1 DBR-1 DFR+1)
F: (UFR+1 DFR-1 DFL+1 UFL-1)
D: (DFR DBR DBL DFL)
L: (UFL+1 DFL-1 DBL+1 UBL-1)
B: (UBR-1 UBL+1 DBL-1 DBR+1)`,
        ignore: "",
        image: "2x2x2",
    },
    "4x4x4": {
        puzzle: `U: (Ublc Ubrc Ufrc Uflc) (UFR UFL UBL UBR) (Ufr Ulf Ubl Urb) (Ful Lub Bur Ruf)
R: (Rufc Rubc Rdbc Rdfc) (UFR-1 UBR+1 DBR-1 DFR+1) (Ruf Rbu Rdb Rfd) (Urb Brd Drf Fru)
F: (Fulc Furc Fdrc Fdlc) (UFR+1 DFR-1 DFL+1 UFL-1) (Ful Fru Fdr Fld) (Ufr Rfd Dfl Lfu)
D: (Dflc Dfrc Dbrc Dblc) (DFR DBR DBL DFL) (Dfl Drf Dbr Dlb) (Fdr Rdb Bdl Ldf)
L: (Lubc Lufc Ldfc Ldbc) (UFL+1 DFL-1 DBL+1 UBL-1) (Lub Lfu Ldf Lbd) (Ulf Fld Dlb Blu)
B: (Burc Bulc Bdlc Bdrc) (UBR-1 UBL+1 DBL-1 DBR+1) (Bur Blu Bdl Brd) (Ubl Lbd Dbr Rbu)
2U: (Furc Lufc Bulc Rubc) (Fulc Lubc Burc Rufc) (Fru Lfu Blu Rbu)
2R: (Ubrc Bdrc Dfrc Furc) (Ufrc Burc Dbrc Fdrc) (Ufr Bur Dbr Fdr)
2F: (Ufrc Rdfc Dflc Lufc) (Uflc Rufc Dfrc Ldfc) (Ulf Ruf Drf Ldf)
2D: (Fdlc Rdfc Bdrc Ldbc) (Fdrc Rdbc Bdlc Ldfc) (Fld Rfd Brd Lbd)
2L: (Uflc Fdlc Dblc Bulc) (Ublc Fulc Dflc Bdlc) (Ubl Ful Dfl Bdl)
2B: (Ubrc Lubc Dblc Rdbc) (Ublc Ldbc Dbrc Rubc) (Urb Lub Dlb Rdb)
u: (Ublc Ubrc Ufrc Uflc) (Furc Lufc Bulc Rubc) (Fulc Lubc Burc Rufc) (UFR UFL UBL UBR) (Ufr Ulf Ubl Urb) (Ful Lub Bur Ruf) (Fru Lfu Blu Rbu)
r: (Rufc Rubc Rdbc Rdfc) (Ubrc Bdrc Dfrc Furc) (Ufrc Burc Dbrc Fdrc) (UFR-1 UBR+1 DBR-1 DFR+1) (Ruf Rbu Rdb Rfd) (Urb Brd Drf Fru) (Ufr Bur Dbr Fdr)
f: (Fulc Furc Fdrc Fdlc) (Ufrc Rdfc Dflc Lufc) (Uflc Rufc Dfrc Ldfc) (UFR+1 DFR-1 DFL+1 UFL-1) (Ful Fru Fdr Fld) (Ufr Rfd Dfl Lfu) (Ulf Ruf Drf Ldf)
d: (Dflc Dfrc Dbrc Dblc) (Fdlc Rdfc Bdrc Ldbc) (Fdrc Rdbc Bdlc Ldfc) (DFR DBR DBL DFL) (Dfl Drf Dbr Dlb) (Fdr Rdb Bdl Ldf)(Fld Rfd Brd Lbd)
l: (Lubc Lufc Ldfc Ldbc) (Uflc Fdlc Dblc Bulc) (Ublc Fulc Dflc Bdlc) (UFL+1 DFL-1 DBL+1 UBL-1) (Lub Lfu Ldf Lbd) (Ulf Fld Dlb Blu) (Ubl Ful Dfl Bdl)
b: (Burc Bulc Bdlc Bdrc) (Ubrc Lubc Dblc Rdbc) (Ublc Ldbc Dbrc Rubc) (UBR-1 UBL+1 DBL-1 DBR+1) (Bur Blu Bdl Brd) (Ubl Lbd Dbr Rbu) (Urb Lub Dlb Rdb)`,
        ignore: "{Ublc Ubrc Ufrc Uflc} {Rufc Rubc Rdbc Rdfc} {Fulc Furc Fdrc Fdlc} {Dflc Dfrc Dbrc Dblc} {Lubc Lufc Ldfc Ldbc} {Burc Bulc Bdlc Bdrc}",
        image: "4x4x4",
    },
    "Skewb": {
        puzzle: `l: (UFL-1 DFR-1 DBL-1) (DLF+1) (L F D)
L: (URF-1 DLF-1 ULB-1) (UFL+1) (U F L)
r: (DFR-1 UBR-1 DBL-1) (DRB+1) (R B D)
R: (URF-1 ULB-1 DRB-1) (UBR+1) (R U B)
b: (ULB-1 DLF-1 DRB-1) (DBL+1) (L D B)
B: (UBR-1 UFL-1 DBL-1) (ULB+1) (U L B)
F: (UFL-1 UBR-1 DFR-1) (URF+1) (F U R) 
f: (URF-1 DRB-1 DLF-1) (DFR+1) (F R D)
S: (URF-1) (UFL+1) (ULB+1) (UBR-1) (R U) (F B)
H: (URF+1) (UFL-1) (ULB-1) (UBR+1) (R U) (F B)
s: (UBR-1) (ULB+1) (DRB-1) (DBL+1) (U D) (R B)
h: (UBR+1) (ULB-1) (DRB+1) (DBL-1) (U D) (R B)      
x: (F U B D) (URF+1 UBR-1 DRB+1 DFR-1) (UFL-1 ULB+1 DBL-1 DLF+1)
y: (F L B R) (URF UFL ULB UBR) (DFR DLF DBL DRB)
z: (U R D L) (URF-1 DFR+1 DLF-1 UFL+1) (UBR+1 DRB-1 DBL+1 ULB-1)
vUperm: (U B D)
hUperm: (U R D)`,
        ignore: "",
        image: "Skewb",
    },
    "Pyraminx": {
        puzzle: `U: (UB UR UL) (UUU+1) (TUU+1)
R: (UR DR DF) (RRR+1) (TRR+1)
L: (UL+1 DF+1 DL) (LLL+1) (TLL+1)
B: (UB+1 DL DR+1) (BBB+1) (TBB+1)
u: (TUU+1)
r: (TRR+1)
l: (TLL+1)
b: (TBB+1)`,
        ignore: "",
        image: "Pyraminx",
    },
    "Megaminx": {
        puzzle: `U: (UF UL UBl UBr UR) (UFR UFL ULB UDB URB)
R: (UR RB RDr RDl RF) (UFR-1 URB+1 RDB-1 RDD RDF+1)
L: (UL LF LDr LDl LB) (ULB-1 UFL+1 LDF-1 LDD LDB+1)
F: (UF+1 RF+1 FDr+1 FDl LF+1) (UFL-1 UFR+1 RDF-1 FDD LDF+1)
Dfr: (RDl+1 DFrr+1 DFrb+1 DFrl FDr+1) (RDF-1 RDD+1 DFRr-1 DFRl FDD+1)
Br: ( UBr+1 DB+1 BRd+1 BRf RB+1) (URB-1 UDB+1 BRB-1 BRD RDB+1)
Bl: ( UBl+1 DB+1 BLd+1 BLf LB+1) (ULB-1 UDB+1 BLB-1 BLD LDB+1)`,
        ignore: "",
        image: "Megaminx",
    },
    "FTO": {
        puzzle: `U: (UF UN UM) (Ur Ul Ub) (lUr bUl rUb) (uRf uLn uBm) (fLu nBu mRu)
D: (FM FN+1 MN+1) (Fd Nd Md) (mDf fDn nDm) (dFr dNl dMb) (lFd bNd rMd)
F: (UF FM FN) (Fl Fr Fd) (rFl dFr lFd) (uRf mDf nLf) (fLu fRm fDn)
B: (UM+1 UN MN+1) (Ub Mb Nb) (mBn uBm nBu) (rUb lNb dMb) (bUl bNd bMr)
L: (UF+1 FN UN+1) (Ul Fl Nl) (nLf uLn fLu) (lUr lFd lNb) (rFl dNl bUl)
R: (UF+1 UM FM+1) (Ur Mr Fr) (fRm uRf mRu) (lUr bMr dFr) (rFl rUb rMd)
BL: (UN FN+1 MN+1) (Nl Nd Nb) (lNb bNd dNl) (uLn fDn mBn) (nLf nDm nBu)
BR: (UM MN+1 FM+1) (Mr Mb Md) (bMr dMb rMd) (mRu mBn mDf) (fRm uBm nDm)
u: (UF UN UM) (Ur Ul Ub) (Fr Nl Mb) (Fl Nb Mr) (lUr bUl rUb) (uRf uLn uBm) (fLu nBu mRu) (rFl lNb bMr) (fRm nLf mBn)
d: (FM FN+1 MN+1) (Fd Nd Md) (mDf fDn nDm) (dFr dNl dMb) (lFd bNd rMd) (Fl Mr Nb) (Fr Mb Nl) (rFl bMr lNb) (fRm mBn nLf)
f: (UF FM FN) (Fl Fr Fd) (rFl dFr lFd) (uRf mDf nLf) (fLu fRm fDn) (Ul Mr Nd) (Ur Md Nl) (lUr rMd dNl) (mRu nDm uLn)
b: (UM+1 UN MN+1) (Ub Mb Nb) (mBn uBm nBu) (rUb lNb dMb) (bUl bNd bMr) (Ul Nd Mr) (Ur Nl Md) (lUr dNl rMd) (mRu uLn nDm)
l: (UF+1 FN UN+1) (Ul Fl Nl) (nLf uLn fLu) (lUr lFd lNb) (rFl dNl bUl) (Ur Fd Nb) (Ub Fr Nd) (rUb dFr bNd) (uRf fDn nBu)
r: (UF+1 UM FM+1) (Ur Mr Fr) (Ul Mb Fd) (Ub Md Fl) (fRm uRf mRu) (lUr bMr dFr) (rFl rUb rMd) (fLu uBm mDf) (bUl dMb lFd)
bl: (UN FN+1 MN+1) (Nl Nd Nb) (lNb bNd dNl) (uLn fDn mBn) (nLf nDm nBu) (Ul Fd Mb) (Ub Fl Md) (bUl lFd dMb) (fLu mDf uBm)
br: (UM MN+1 FM+1) (Mr Mb Md) (bMr dMb rMd) (mRu mBn mDf) (fRm uBm nDm) (Ur Nb Fd) (Ub Nd Fr) (rUb bNd dFr) (uRf nBu fDn)
M: (Ul Fd Mb) (Ub Fl Md) (bUl lFd dMb) (fLu mDf uBm)
N: (Ur Fd Nb) (Ub Fr Nd) (rUb dFr bNd) (uRf fDn nBu)
E: (Fl Mr Nb) (Fr Mb Nl) (rFl bMr lNb) (fRm mBn nLf)
S: (Ul Mr Nd) (Ur Md Nl) (lUr rMd dNl) (mRu nDm uLn)`,
        ignore: "",
        image: "",
    },
};
