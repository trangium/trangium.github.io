import torch
import torch.nn as nn
import torch.optim as optim

class MatrixSequenceModel(nn.Module):
    def __init__(self, n, k):
        """
        n = number of symbols
        k = matrix/vector dimension
        """
        super().__init__()

        # A_1 through A_n
        self.A = nn.Parameter(torch.randn(n, k, k) * 2 + 5)

        # starting vector v
        self.v = nn.Parameter(torch.randn(k) * 1.5 + 1)

    def tropical_product(self, A, x):
        return (A + x).min(dim=1).values

    def forward(self, x):
        """
        x: list of ints in {0, ..., n-1}

        Computes:
            B_i = relu(A_i)
            y = log(sum(B_{i_1} ... B_{i_l} v) + 1e-250)
        """

        # Elementwise exp
        B = torch.exp(self.A)

        # Start from v
        state = self.v

        # Multiply in sequence
        for idx in reversed(x):
            state = self.tropical_product(B[idx], state)

        # Sum coordinates
        total = torch.min(state)

        return total


# ---------------------------------------------------
# Example training data
# ---------------------------------------------------


raw_data = """R' U2 R' U' D R' U' R D' R U R U R2	0.86
R' U2 D' R U' R' D R' U R U R' U2 R U' R	1.08
R' U' R' D' R U' R' r U2 r' D R2	0.85
F R2 D R' U' R D' R2 U' R U2 R' U' F'	0.85
F R U R' U' R U R' U' F' R U R' U' R' F R F'	1.03
R' F' r U R U' r' F	0.47
R' U2 R F U' R' U R U F' R' U R	0.96
R' U' R U R' U R L' U R' U' R L	0.98
F U R U2 R' U R U R' F'	0.7
R U R' U' R' F' R U2 R U2 R' F	0.8
F U R' U' R F' R' U' R U R' U R	0.96
R' U R U R' U' R' D' R U2 R' D R U R	0.92
R U R D R' U R r' U2 r D' R2	0.84
R' D' R U R' D R2 U' R' U R U R' U' R U R'	0.89
R U R' U R U R' U2 L R U' R' U L'	0.91
F R U R' U' R' F' U2 R U R U' R2 U2 R	1.06
r U R' U' r' F R F'	0.56
R' U' R U' R2 F' R U R U' R' F U R U' R' U2 R	1.04
R2 U R' U' R' U R' U2 D R' U2 R D'	1.07
R U R' U' R U' R' L U' R U R' L'	0.99
R U2 R' U2 R' F R U R U' R' F'	0.86
F' U' L' U2 L U' L' U' L F	0.69
R U' R' U' R U R D R' U2 R D' R' U' R'	0.9
R L' U R' U' L U R U R' U' R U' R'	0.91
L' U R' U' L R U2 R' U' R	0.77
F' U' F R U R' U' R l U' R' U l'	0.89
R U' R' U2 L R U' R' U L'	0.59
f R f' R' U' R U R2 F R F' R	0.79
F R U' R' U' R U2 R' U' F' R' U' R U' R' U2 R	1.09
R' U2 R U R' U R F U R U2 R' U R U R' F'	1.09
r U' r U2 R' F R U2 r2 F	0.91
R' U' R2 U R' F' R U R' U' R' F R2 U' R' U'	0.98
R' U' R U' R' U R F U' R' U2 R U F'	0.98
R' U R U' R' U' R U R' U' R U R D R' U R D' R'	0.98
F U' R' U2 R U F' R' U' R U R' U R	0.94
R D R' U' R D' R' U' R' U R U' R' U R U R' U' R	0.98
R' D' R U R' D R2 U R' U2 R U' R' U' R U' R'	0.98
R' U2 R' D' R U2 R' D R' U R' U R U2 R'	1.03
R' U' R U' F U' R' U R U F' R' U R	0.98
R U2 R D R' U2 R D' R U' R U' R' U2 R	0.94
l' U2 R' D2 R U2 R' D2 R2 	0.83
l U2 R D2 R' U2 R D2 R2	0.77
F R U R' U' R U' R' U' R U R' F'	0.78
R U R' U2 R U' R' U2 R U' R2 F' R U R U' R'	0.98
R' U' R' D' R U R' D R U' R U' R' U2 R	0.79
R U R' U R' D' R U' R' D R U R U2 R'	0.9
r U R' U' r' F R F' R' U2 R U R' U R	0.92
R U2 R' U' R U' R2 F' r U R U' r' F	0.85
R' U' R U R' U' R2 D R' U R D' R' U2 R' U R	0.98
R U' R' U R U R' U' R U R' U R' D' R U R' D R	0.93
R U' R2 D' r U2 r' D R2 U' R' U' R U' R'	0.99
R U R' U2 R' D' R U R' D R2 U' R' U R U' R'	0.98
F U R' D' R U R' D R U' R' D' R U' R' D R F'	1.08
R' U R U' R' U R U R' U2 R U' R' U' R' D' R	1.08
R U' R2 D' r U2 r' D R2 U R'	0.75
R' U R2 D r' U2 r D' R2 U' R	0.84
R' U' R U2 R D R' U' R D' R2 U R U' R' U R	1.05
R' D' R U' R' D R U' R U' R' U R U' R' U' R U R'	0.96
R U R' U' R U R2 D' R U' R' D R U2 R U' R'	0.92
R U R' U R U R2 D' r U2 r' D R2 U R'	0.94
R U' R' U2 R U R' U2 R U R' U R U' R'	0.8
R' U R U2 R' U' R U2 R' U' R U' R' U R	0.96
R U R' U R U' R' U R' U' R2 U' R2 U2 R	0.83
R U2 R' U' R U' R' U R U R' U R U2 R'	0.86
R' U' R U' R' U R U' R U R2 U R2 U2 R'	0.96
R' U2 R U R' U R U' R' U' R U' R' U2 R	0.8
R' U' R2 U R2 U R2 U2 R' U R' U R	0.93
R U R2 U' R2 U' R2 U2 R U' R U' R'	0.96
R U2 R' U' R U' R2 U2 R U R' U R	0.91
R' U2 R U R' U R2 U2 R' U' R U' R'	0.9
R U' R' U2 R U R' U R' U' R U R U R' U' R' U R	1.05
R U R' U R U2 R' U2 R' U' R U' R' U2 R	0.94
R U' R' U' R U2 R' U' R' D' R U2 R' D R	0.95
R U2 R D R' U2 R D' R' U2 R' U' R U' R'	0.99
R2 D r' U2 r D' R' U2 R'	0.83
R2 D R' U R D' R2 U R U2 R'	0.64
r U R' U R' D' R U R' D R U r' F R F'	0.97
R2 D R' U2 R D' R' U2 R'	0.63
R' D' r U2 r' D R U2 R U' R' U' R U' R'	0.95
R' U' R U R U R' U' R' U F R U R U' R' F'	0.98
R U R' U R U' R' U F' R U2 R' U2 R' F R	0.97
F' R2 U R' U R' U' R U' R2 U' D R' U R D' U F	0.99
R U R' U R U' R' U R U' R' U' r' F R F' M'	1.07
R U' R' U R U R' U2 R' D' R U R' D R2 U R'	0.92
R2 D' r U2 r' D R U2 R	0.68
R2 D' R U' R' D R2 U' R' U2 R	0.6
R' U R U R' U2 R U R D R' U2 R D' R'	0.94
R' U2 R' D' R U2 R' D R U2 R U R' U R	0.89
R2 D' R U2 R' D R U2 R	0.63
R' F' r U2 R' D R U' R' D' R2 U' r' F	0.97
R' U R U R' U2 R U' D' R U' R' U2 R U' R' D	0.96
F R U R' U' R2 D R' U' R D' R2 U' R U R' F'	0.97
R2 D' R U2 R' U' D R' U' R2 U R U R2	1.06
R2 F' R U2 R U2 R' F U' R U R' U' R	0.91
R' U R U' R' U' R U2 R D R' U' R D' R2 U' R	0.97
F U R U' R D R' U' R D' R2 U R U R' F'	0.96
R' F R U' R' U' R U R' F' R U R' U' R' F R F' R	0.94
z U' D' R2 D R2 U R' D' R U' R U R' D	1.06
F2 R U' R' U' R U R' F' R U R' U' R' F R F2	1.09
x' R2 U2 R' U2 R' F R F' r U' r' F l'	1.02
r U R' U' r' F R U R' U' R F' R' U R	0.98
R' U' R F R' U R U' R' F' r U R U' r'	0.99
R' U R U R' F' R U R' U' R' F R2 U' R' U2 R	0.97
R U' R' U R U' L U L' U x' U2 R U2 R2	0.98
F U R U2 R' U R U R2 F' r U R U' r'	0.95
F' U' r' F2 r U' r' F' r2 U R' U' r' F R	0.99
r U R' U' r' F R2 U' R' U' R U2 R' U' F'	0.97
R2 F R U R U' R' F' R U' R2 D' R U R' D R2	0.85
R U R' U R U R' U2 R U' R2 D' R U' R' D R	0.89
R U2 R' U' R U' R D' R U2 R' D R U2 R	0.94
R' U' R F U' R' U' R U F' U R' U R	0.97
R' U2 R U R' U R' D R' U2 R D' R' U2 R'	1.07
x' R2 D2 R' U2 R D2 R' U2 R'	0.78
x R2 D2 R U2 R' D2 R U2 R 	0.64
F R U' R' U R U R' U R U' R' F'	0.72
F' R U R' U' R' F R2 U R' U2 R U R' U2 R U' R'	0.91
R' U' R U' R' U R' U L U' R U L' R	0.88
R U R' U R U' R U' L' U R' U' L R'	0.91
R' U2 R U R' U R' D' R U' R' D R U R	0.86
R U2 R' U' R U' R D R' U R D' R' U' R'	0.85
R U' R' U' R U R D R' U R D' R2	0.67
F R' U R' U' D R' U R D' R2 U' R' F'	1.05
R U R' U L' U R U' R' L U' R U' R'	0.97
R2 D' R U R' D R U R U' R' U' R	0.77
F U R U2 R' U R U R' U R U2 R' U R U R' F'	1.03
S R' F R S' R' F' U' F' U F R	0.85
F R U R' U' f' R U R' S R U' R'	0.97
R' U2 R F U' R' U R U R' U R U' F'	0.97
R2 D R' U' R D' R' U' R' U R U R'	0.62
F R U R2 D R' U' R D' U R U' R F'	0.92
R' U R U R' U' R' D' R U' R' D R2	0.66
R' U' R U' F U' R' U R U R' U R U' F'	0.98
R' U' R U R' U R U2 R' U R U2 R' U' R	0.89
R U R' U' R U' R' U2 R U' R' U2 R U R'	0.87
R U2 R' U' R U' R' U' R U R' U R U2 R'	0.92
R' U2 R2 U R2 U R U' R U R' U' R U' R'	0.93
R' U2 R U R' U R U R' U' R U' R' U2 R	0.79
R U2 R2 U' R2 U' R' U R' U' R U R' U R	0.83
R U R' U R' U2 R2 U R2 U R2 U' R'	0.76
R' U' R U' R U R' U R U2 R' U2 R' U2 R	0.91
R' U' R U' R' U2 R2 U R' U R U2 R'	0.85
R U R' U R U2 R2 U' R U' R' U2 R	0.72
x R2 D2 R U R' D2 R2 D' R' U' R D	0.9
R U2 R' U' R U' R' L' U2 L U L' U L	0.82
R' U' R U' R' U2 R' D' R U2 R' D R U2 R	0.94
R D R' U2 R D' R' U' R' U2 R U' R' U' R	0.97
R' U2 R U R2 D' R U R' D R2	0.72
R' U2 R' D' r U2 r' D R2	0.65
F' r U R2 D R U R' D' R U2 r' F R	0.97
R' U2 R' D' R U2 R' D R2	0.56
R U R' U2 R U R' U D R' U2 R U' R' U' R D'	0.99
R U2 R U R U' R2 D R' U R D' R U R'	0.98
R2 U R' U' D R2 U' R U' R' U R' U R2 D' U R'	0.96
R' U R U' R' U F' R U2 R' U2 R' F R2	0.88
R' U R2 D R' U R D' R' U2 R' U R U R' U' R	0.98
F R U' R' U' R2 D R' U R D' R' U R' U' F'	0.91
R2 D r' U2 r R' U' R D' R' U' R'	0.79
R U' R' U R U' R' U' R U R2 D' R U' R' D R	0.92
L U' R U R' L' U2 R U' R' U' R U' R'	1.06
R' U2 R2 U R' U' R' U2 F R U R U' R' F'	1.08
R' F R U R' U' F' R' F R F' R' F R F' U R	0.97
F R' F' r U R U' r'	0.49
R U R' U R U R' U' R U R D R' U2 R D' R' U' R'	0.96
L R U' R' U R L' U R' U R U' R'	0.87
R U R D R' U2 R D' R' U' R' U R U R'	0.85
R U R' U R U' R' U' L' U R U' M'	0.9
F R U R' U' R' F' R U2 R U2 R'	0.8
R' F' R U R' U' R' F R U' R U R' U R	0.7
R2 D' r U2 r' R U R' D R U R	0.8
L' U R' U' L R U2 R' U R U R' U R	0.99
R' U R U' R' U R U R' U' R2 D R' U R D' R'	0.89
R' U R' U2 R U' R' U' R D' R U R' D U2 R	0.98
F' r U R' U' r' F R	0.52
F R' F' R U R U' R' F U R U' R' U R U' R' F'	0.99
R' L' U R U' R' L U' R U' R' U R	0.88
R' U' R F U' R' U' R U F' R' U2 R	1.09
F' R U2 R' U2 R' F R U R U' R'	0.79
R' U' R U' R' U R F R' U R U' F'	0.82
R' U' R' D' R U2 R' D R U R U' R' U' R	0.87
F R U' R' U' R U2 R' U' F'	0.56
R U R' U R U2 R D R' U2 R D' R' U2 R'	0.99
R U2 R' U' R2 D R' U' R D' R2	0.67
L' U R U' L U R2 U2 R U R' U R	0.8
R U2 R D r' U2 r D' R2	0.72
R U2 R D R' U2 R D' R2	0.53
F R' F' r U' R' D' R U' R' D R U' R U' r'	0.98
F R U R' U' R' F' U' R U R U' R' U' R' U R	0.98
R' U' R U2 R' U' R D' U' R U2 R' U R U R' D	0.93
R U' R2 D' R U' R' D R U2 R U' R' U' R U R'	0.94
R' F' R U2 R U2 R' F U' R U R' U' R U' R'	0.99
F2 U' F R2 u R' U R U' R u' R2 F	0.95
R' F' R U R' U' R' F D' R U' R' D R2 U R' U R	0.97
r U2 r2 F R F' r2 R' U2 r'	0.93
R U' R' U R U' R' U' R U R' U2 R' D' R U R' D R	0.94
R' U R U' R' U R U R' U' R U2 R D R' U' R D' R'	0.96
r U2 R r2 F R' F' r2 U2 r'	0.97
R' F' R U2 R' D R U' R' U' D' R' F R U R	0.97
R' U' R' F' R D U R U R' D' R U2 R' F R	0.95
F R U R' U' R' F R2 U' R' U' R U R' F2	0.95
R U' R' U R U R' U R U' R2 D' R U R' D R	0.87
R' U' R U R' F' R U R' U' R' F R2	0.63
F R U R2 F R F' R U' R' F'	0.83
L' U2 R U' L U R' L' U2 L	0.75
R U R' U F' R U2 R' U' R' U' R' F R U R	0.96
R2 U R' U R' U' R U' R' U' R U R U' R2	0.88
R U2 R' U' R U' R' U R' U2 R U R' U R	0.85
R U R' U R U2 R' U R' U' R U' R' U2 R	0.81
R2 U' R U R U' R' U' R U' R' U R' U R2	0.9
R' U2 R U R' U R U' R U2 R' U' R U' R'	0.93
R2 U' R U' R U R' U R U R' U' R' U R2	0.95
R' U' R U' R' U2 R U' R U R' U R U2 R'	0.86
R U2 R' U' R U' R2 U2 R U R' U' R U R' U R	0.89
R U2 R' U' R U' R' U2 R U R' U R U2 R'	0.9
R U R' U R U2 R' U2 R U2 R' U' R U' R'	0.97
R U R' U R U' R' U R U' R' U R U2 R'	0.81
R U2 R' U2 R' U' R U R U' R' U2 R' U2 R	0.69
R U R' U R U2 R2 F' r U R U' r' F	0.93
r' F' r U' r' F2 r2 U R' U' r' F R F'	0.99
R' U' R' D' R U R' D R' U R' U R U2 R'	0.99
R U R D R' U' R D' R U' R U' R' U2 R	0.85
F R' F' R U2 R U2 R' U' r U R' U R U2 r'	0.98
F R U R' U' R' F' R U2 R' U' R2 U' R2 U2 R	1.03
R2 F R U R U' R' F' R U' R' U' R U R' U R	0.93
F U R U' R' U R U2 R' U' R U R' F'	0.93
R U R' U R U' R' U' R' F' R U2 R U2 R' F	0.95
F U' R U' R' U R U R' U2 R U2 R' U F'	0.95
R' U R U2 y R U' R' U2 R U' R' U y' R' U' R	1.05
R' U' F' R U R' U' R' F R2 U2 R' U2 R	0.76
R2 D' R U2 R' D R2 U R2 D' R U R' D R2	1.03
R' U2 R' F' R U R U' R' F U R U' R' U2 R	0.92
R' U' R U' R2 D' R U R' D R2 U' R' U2 R	0.88
R U R' U R2 D R' U' R D' R2 U R U2 R'	0.92
R' U' R U R2 F' R U R U' R' F U' R U R' U R	0.99
R' U' F' R U2 R' U' R U' R' F U R U R' U2 R	1.05
F U R U2 R' U R U R' F' R U2 R' U' R U' R'	1.07
R U2 R' U' R U R' U' R' F R F' R U' R' F' U F	1.08
L' U R U' L U' R' U' R U' R'	0.75
r' U r U r' U' r U l' R' U R U' R	0.87
r U' r' U' r U r' U' R2 B' R' B R' U	0.81
R U' L' U R' U L U L' U L	0.79
R' U' R' D' R U' R' D R2 U R' U' R U R' U R	0.85
R U' R' U' R U' R' U R U R' U R' F' R U R U' R' F	1.09
L' R U R' U' L U2 R U' R' U R U2 R'	0.91
R' U2 R U R' U' R U R2 F R U R U' R' F' R	0.99
R U' L' U R' U' L U' R U' L' U R' U' L	0.97
F U R U' R' U R U' R2 F' R U R U' R'	0.92
R U R' U' R U R2 D' R U R' D R U R U' R' U	1.07
R U R' U R U' R2 F R F' R U' R' F' U F	1.07
R' U' R U' B2 R' U2 R U2 l U2 l'	0.99
L' U R U' L U R2 U' R U' R' U2 R	0.87
R U2 R' U' R U' R2 U L U' R U L'	0.94
R' F2 R U2 R U2 R' F2 U' R U' R'	0.97
R' F R U R' U' R' F' R2 U' R' U R U' R' U2 R	0.95
R U R D R' U R D' R2 U' R U R' U' R U' R'	0.93
F' R U R' U' R' F R U' R U' R' U' R U R' U R U R'	1.08
R' U' R U' R' U R U' R2 D' R U R' D R U R	0.91
R U R' U' R' F R2 U R' U' R U R' U' F'	0.86
L' U R U' L U R' U L' U R U' L U R'	0.89
R U2 R' U' R U R' U' R' D' R U' R' D R2 U' R' U R U' R'	1.08
r' F' r U r U2 r' F2 U' R U R' U' R U' R'	0.99
R B2 R' U2 R' U2 R B2 U R' U R	1.08
R' U2 R U R' U R2 U' L' U R' U' L	0.95
R U' L' U R' U' L2 U L' U L U2 L'	0.96
R U R' U F2 R U2 R' U2 R' F2 R	0.98
R' U2 R U' R D R' U' R D' R2 U R U' R' U R	0.95
R U R' F' R U R' U R U2 R' F U R U2 R'	0.96
F R U R' U' F' r U2 R' U' R U R' U' R U' r'	1.06
R U R' U' R U R2 D' R U' R' D R U' R U2 R'	0.98
F R U' R' U2 R' U2 R2 U R2 U R U' F'	0.97
R' U' R' D' R U R' D R U2 R' D' R U2 R' D R2	0.96
R2 D R' U' R D' R' U' R' U R U' R' U' R U' R'	0.98
R2 D' R U R' D R U R U' R' U R U R' U R	0.93
R U2 R' U R' D' R U R' D R2 U' R' U R U' R'	0.98
r U R' U R U' R' U R U2 r' F U R U' R' F'	1.09
R' U' R U R' U' R2 D R' U R D' R' U R' U2 R	0.94
R U2 R' U' F' R U2 R' U' R U' R' F R U' R'	0.99
R U2 R2 U' R2 U' R2 U2 R	0.73
R' U2 R U R' U R2 U R' U R U2 R'	0.89
R U2 R' U2 R U' R' U2 R U' R' U2 R U R'	0.95
R' U2 R U2 R' U R U2 R' U R U2 R' U' R	0.97
R U' R' U2 R U R' U2 R U R' U2 R U2 R'	0.94
R U' R' U R2 U2 R' U' R U' R2 U R U' R'	0.98
R U2 R' U' R U' R2 U' R U' R' U2 R	0.73
R' U2 R2 U R2 U R2 U2 R'	0.63
R' U2 R2 U R' U R U2 R' U' R' U R	0.81
R U2 R2 U' R U' R' U2 R U R U' R'	0.9
R U2 R' U' R U' R' U' R U2 R' U' R U' R'	0.9
R U R2 U' R2 U' R2 U2 R2 U' R' U R U2 R'	1.08
F' r U R' U' r' F R2 U2 R' U' R U' R'	0.94
F R' F' r U R U' r2 F2 r U L' U L	0.95
R U2 R' U' R U' R D' R U' R' D R U R	0.96
R' U2 R U R' U R' D R' U R D' R' U' R'	0.98
R' U2 R2 U R2 U R U2 R' F R U R U' R' F'	0.98
R U2 R' U' R U R' U' F' R U R' U' R' F R2 U' R'	0.97
R' U' R U' R' U R U R' F R U R' U' R' F' R2	0.96
F R U' R' U R U2 R' U' R U R' U' F'	0.97
R' F R U R' U' F' R U' R' U R' F R F' U R	1.05
R' U2 R U2 R2 F' R U R U' R' F U R	0.86
F' R U2 R' U2 R' F R U R U R' U' R U' R'	0.95
F U' R U2 R' U2 R U' R' U' R U R' U F'	0.98
R' U2 R U R' U' F' R U R' U' R' F R U2 R	0.94
R U2 R' U' R2 D R' U R D' R2 U' R U' R'	0.91
R2 D' R U' R' D R2 U' R2 D' R U2 R' D R2	0.93
R' U2 R U R2 D' R U' R' D R2 U R' U R	0.91
L' U R U' L U2 R' U R' U' R2 U' R2 U2 R	1.08
R' U' R U' R' U F' R U R' U' R' F R2 U' R' U R	0.95
R' U' R f U R U2 R' U2 R' U2 R2 U R' f'	1.01
R U R' U R U2 R' F R U' R' U' R U2 R' U' F'	0.98
R' U' R U' R' U' L U' R U L'	0.87
R U R' U R U r' F R' F' r	0.79
R' F R' F' R2 U' r' U r U' r' U' r	0.84
R U R2 F R F' r U' r' U r U r'	0.84
R' U' R y U' R U' R' U R l U' R' U l'	0.91
R U' R2 U' F2 U' R2 U R2 U F2 R2 U R'	0.99
F R U R' U' R U R' U' R U R' U' F'	0.79
x' U' R U' R' U R' F2 R U' R U R' U	0.99
R' U2 R U R' U R U R' U' R U R' F' R U R' U' R' F R2	1.09
R U' R' U R U R' U' L U L' U' R U R' U2 L U L'	1.15
F' U' L' U L f F R U R' U' R U R' U' f'	1.07
f R f' U R' U R U' R2 F R F' R	0.96
R U R' U R U' R' U R U2 R'	0.53
R' U' R U' R' U R U' R' U2 R	0.53
R' U2 R U R' U' R U R' U R	0.58
R U2 R' U' R U R' U' R U' R'	0.58
R' U2 R U R' U R U R U R' U R U2 R'	0.99
R U2 R' U' R U' R' U' R' U' R U' R' U2 R	0.91
R U R' U R U2 R' U' R' U2 R U R' U R	0.91
R U R' U R U' R' U R U' R' U R' U' R2 U' R' U R' U R	0.96
L' U2 L U2 R U' L' U R' L	0.78
R U R' U R U' R2 F' R U R U' R' F R U' R'	0.87
R' U R U2 R' U R2 D R' U R D' R'	0.84
S' U2 L' U2 L U2 L F' L' f	1.07
R' F R U R' U' R' F' D' R U R' D R2	0.99
F' R U R' U R U2 R' F U R U' R' U2 R U' R'	0.98
R' U' R U R2 U' R' U' R U D' R U R' D R'	1.01
R U R' U R2 D r' U2 r D' R2	0.84
R U R' U' R U R2 D' R U R' D R U R U2 R'	1.04
R U R' U R2 D R' U2 R D' R2	0.73
R U R2 F' R U2 R U2 R' F R U' R'	0.78
R U2 R D R' U2 R D' R' U R' U R U2 R'	0.94
R' U2 R' B2 D' r U2 r' D B2 R2	0.96
F U R U' R' U R U' l U' R2 D' R U R'	0.98
R' U' R U' R2 F' R U R U' R' F U2 R	0.96
R U2 R' U' R U R' U' R U R D R' U2 R D' R2	0.99
R' U2 R U R' U' R U R' F' R U R' U' R' F R2 U' R' U R	1.09
R' D R2 D' R2 U R2 D R2 D' R2 U' R'	0.84
R2 D' r U2 r' R U R' D R U' R U R' U R	0.98
R U' R' U' R U R D R' U2 R D' R2 U R U2 R'	0.99
R' U2 R' D' R U R' D R U' R U R' U R	0.8
R U R' U R U' R D R' U R D' R' U2 R'	0.86
R U R' U R U' R D R' U' R D' R2	0.67
R2 D' R U' R' D R U' R U R' U R	0.73
R2 D R' U2 R D' R' U' R' U R U2 R'	0.83
R' U2 F' R U R' U' R' F R U2 R	0.83
R' U2 R U R' U' R' D' R U2 R' D R2	0.81
R U2 R' U2 R' F R2 U R' U' R U R' U' F'	0.98
R U' L' U R' U' L	0.47
D' R U R' U R U' R' U' D R2 U' R U' R' U R' U R2	1.04
R' U2 R U R' U' R' D' R U' R' D R U R U' R' U' R	1.05
R2 U R U' R2 U' R U R' D R' U R D'	0.97
R' U' R' U R2 U2 D' R U R' U' D R'	0.84
R U R' U R U' R' U' R' F R2 U' R' U' R U R' F'	0.99
R2 D r' U2 r D' R' U' R' U R U2 R'	0.97
R' U' D R' U R U2 D' R2 U R' U' R'	0.99
L' R U R' U' L U2 R U2 R'	0.82
R' D' R U R' D R2 U R' U2 R U R'	0.84
R' U2 R U R2 D' R U' R' D R U2 R	0.8
f R' F' R U2 R U2 R' U2 S'	0.8
M F R' F' r U R2 D R' U' R D' R2	0.87
R' U2 R2 U R2 U D' R U R' D R U2 R U' R'	0.98
R2 D' r U2 r' D R2 U R' U R	0.82
F U R U' R' S R' F' R U R U' R' S'	0.95
F R U R' U' R' F' R U2 R U' R' U R U2 R'	0.98
R' U2 R U R' U R' D' R U2 R' D R U2 R	0.97
R2 F R U R U' R' F' R U' R' U R	0.71
R2 D' R U2 R' D R2 U R' U R	0.67
R f R' U R U' R' U R U' R f' U' R'	0.96
F U R' F R F' R U' R' U R U' R' F'	0.89
R U' R2 U2 D' R U R' U D R2 U R'	0.97
F' R U R' D R U R' U' D' R U' R' F	0.77
R' U2 R2 U R' F' R U R' U' R' F R2 U' R2 U R	0.96
f R' F' R f' R' D R U R' D' R U2 F	1.03
R' U2 R U R' U' R F U' R' U' R U F'	0.96
L' U2 R U' R' U2 L U R U' R' U R U2 R'	0.96
R' D' R U' R' D R U2 R U R' U2 R U R'	0.97
F U' R' U R U F' R U R2 U R2 U2 R'	1.05
R' U R U2 R' U R U2 R D R' U' R D' R'	0.91
R U R' F' R U R' U R U2 R' F R U' R'	0.95
R U R2 U R U' R U' R2 U R U R' U' R	0.94
R U R' U R U' R' U R' U' R2 U' R' U R' U R	0.82
R U2 R' U2 R' U2 R U R' U R2 U2 R'	0.86
R' U2 R U R' U R	0.27
R' U' R U' R U R' U R U2 R' U' R' U R	0.83
R U R' U R' U' R2 U' R' U R' U' R U R' U R	0.82
R U R' U R U2 R'	0.27
R' U2 R2 U R' U R U2 R' U2 R' U2 R	0.78
R U R' U' R' U2 R U R U' R' U R' U R	0.78
R' U' R U R U R' U' R' U R U R U' R'	0.79
R' U2 R2 U R2 U R U' R U' R'	0.68
R U R' U R U2 R' U R U R' U R' U' R2 U' R' U R' U R	1.03
R U R' U R' D R2 U' R' U R2 D' R' U2 R'	1.07
R' U2 F' R U R' U' R' F R2 U R' U R	0.86
R' U' R U' R D' R U2 R' D R U2 R U2 R	1.12
R' D' R U2 R' D R2 U' R' U2 R U R' U R U R'	1.01
R2 U R' U' R D R' U2 R D' R' U' R U' R2	0.86
r' F R' F' r U' R U2 R' U' R U2 R' U2 R	1.11
R' U' R U' R' U R' D' R U' R' r U2 r' D R2	1.02
R' U R U R' U' R' D' R U2 R' D R2 U' R' U2 R	0.91
R' U' R U' R' U R' D' R U' R' D R U2 R	0.96
R U2 R D R' U' R D' R' U R' U' R U' R'	0.79
R2 D R' U R D' R' U R' U' R U' R'	0.75
R' U' R U' R' U R' D' R U R' D R2	0.65
R U' R' U2 R U' R2 D' R U' R' D R	0.67
R U2 R' U' F' R U R' U' R' F R2 U' R'	0.79
R U2 R' U2 L' U R U' M'	0.75
R' U2 R' D' R U R' D R2 U' R' U2 R	0.67
R U R' U2 R' D' R U' R' D U' R2 U' R2 U2 R	1.02
R2 D R' U R D' R2 U' r' F R F' M'	0.89
S R U R' U' R' F R S' R U R' U' F'	0.98
R' U' R U' R2 D' r U2 r' D R2	0.76
R' U' R U' R2 D' R U2 R' D R2	0.71
R U2 R' U' R U R' U2 R' F R U R U' R' F'	0.97
R' U2 R' D' R U2 R' D R U' R U' R' U2 R	0.89
R' U' R U R' F R U R' U' R' F' R2	0.75
R U2 R' U' R U R D R' U2 R D' R2	0.85
R' U2 R' F' R U R U' R' F U2 R	0.82
R2 D' R U2 R' D R U R U' R' U2 R	0.8
F U R U' R' U R U' R2 F' R U2 R U2 R'	0.92
R U' R' F D U R U' R' U R U' R' D' R' F' R	0.95
L' U R U' L U R'	0.45
R' U R U R' U' R' D' R U R' D R U R U' R' U2 R	0.98
R2 U' R' U R2 U R' U' R D' R U' R' D	0.93
R U2 R' U' R U R D r' U2 r D' R2	0.92
R U R U' R2 D U2 R' U' R U D' R	0.74
R D' U R U' R' U2 D R2 U' R U R	0.83
R2 D' r U2 r' D R U R U' R' U2 R	0.88
R U R' F' R U R' U' R' F R2 U R' U' R U' R'	0.9
f' L F L' U2 L' U2 L U2 S	0.92
x M U' L U R' U2 L' U2 L	0.85
R D R' U' R D' R2 U' R U2 R' U' R	0.8
R2 D R' U2 R D' R U' R2 U' R' U R' U R	1.08
R2 D' R U' R' D F R U R U' R' F' R	0.88
R2 D r' U2 r D' R2 U' R U' R'	0.8
R D' R U' R' D U' R' U R U R2 U' R' U R	0.99
R U2 R' U' R U' R D R' U2 R D' R' U2 R'	0.95
R U2 R' U' R' D' R U' R' D R2 U' R' U R U' R'	0.94
R2 D R' U2 R D' R2 U' R U' R'	0.73
R U R' F' R U2 R' U2 R' F R2 U' R'	0.84
R U' R' F' R U R' U' R' F R2 U' R' U2 R U' R'	0.82
R U2 R' D R' U' R D' R2 U R' U' R' U' R'	1.07
R U R' F' R U2 R' U' R U' R' F R U' R'	0.76
R D R' U R D' R' U2 R' U' R U2 R' U' R	0.96
F R' F' R U R U' R2 F R U R' U' F' U R	0.95
R U2 R' U' F2 R U2 R' U2 R' F2 R2 U' R'	0.98
F U' R' U R U F' R' U R U' R' U2 R	0.97
R U2 R' U' R U R' L' U R U' L U2 R'	0.98
R U' R D R2 U2 R U R' U R2 D' R' U R'	0.95
F R U R' U' R U R' F R' F' R U' F'	0.93
F' R U R' U D R U' R' D' R U' R' F	0.78
R' U R2 U2 D R' U' R D' U' R2 U' R	0.87
R U R' U R' U' R U R' U' R2 U' R2 U R U' R' U R	0.96
R' U' R2 U' R' U R' U R2 U' R' U' R U R'	0.83
R U R' U R' U' R U' R' U2 R U R U' R'	0.82
R U R' U' R U R2 U' R2 U' R' U R U' R' U R' U R	0.83
R' U' R2 U R2 U R2 U2 R2 U2 R	0.83
R U2 R' U' R U' R'	0.33
R U2 R2 U2 R2 U R2 U R2 U' R'	0.77
R' U' R U' R' U2 R	0.27
R U R' U' R' U' R U R U' R' U' R' U R	0.82
R' U' R U R U2 R' U' R' U R U' R U' R'	0.91
R U2 R2 U' R2 U' R' U R' U R	0.58
R' U' R U' R' U2 R U R' U R' U' R' U' R' U R U R2	0.98"""
raw_train_data = [a.split("\t") for a in raw_data.split("\n")]
raw_train_data = [(a, float(b)*20) for (a, b) in raw_train_data]
symbols = set()
for i, j in raw_train_data:
    for k in i.strip().split(" "):
        symbols.add(k)
symbols = list(enumerate(symbols))
symbol_dict = {j:i for (i, j) in symbols}

def preprocess(alg, symbol_dict):
    a = []
    for i in alg.strip().split():
        a.append(symbol_dict[i])
    return a

train_data = [(preprocess(i, symbol_dict), j) for (i, j) in raw_train_data]
print(train_data)

# Number of symbols (0 through 4 appear)
n = len(symbol_dict)

# Hyperparameter, number of states
k = 8

model = MatrixSequenceModel(n=n, k=k)

optimizer = optim.Adam(model.parameters(), lr=4e-3, betas=(0.8, 0.99))
loss_fn = nn.MSELoss()

# ---------------------------------------------------
# Test predictions
# ---------------------------------------------------

def print_preds():
    print("\nPredictions:")
    for x, y in raw_train_data:
        pred = model(preprocess(x, symbol_dict)).item()
        print(f"{x} -> predicted {pred:.4f} | target {y}")


# ---------------------------------------------------
# Training loop
# ---------------------------------------------------

num_epochs = 300

for epoch in range(num_epochs):
    total_loss = 0.0

    for x, y in train_data:
        optimizer.zero_grad()
        pred = model(x)
        target = torch.tensor(y, dtype=torch.float32)
        loss = loss_fn(pred, target)
        loss.backward()
        optimizer.step()
        total_loss += loss.item()

    if epoch % 2 == 1:
        print(f"Epoch {epoch:4d} | Loss = {total_loss:.6f}")

    if epoch % 100 == 99:
        print_preds()

while True:
    alg = input()
    print(model(preprocess(alg, symbol_dict)).item())