# Batch Solver
General twisty puzzle algset generator. Discussion can be found on the [Batch Solver Speedsolving thread]([url](https://www.speedsolving.com/threads/the-batch-solver-generate-large-algorithm-sets-automatically.86934/)).

## Motivation
Before the batch solver, generating algorithms involved inputting each and every case into Cube Explorer, running the search for that one case, testing out the generated algorithms to determine the best one for that case, then repeating the process for all cases in the set. For algorithm sets with 50, 100, or even 500+ cases, this quickly becomes extremely tedious and a huge time commitment. The batch solver automates most of this process. Given a suitable input, it will automatically generate algorithms for every case using the specified move groups and depths, and it will also sort the algorithms, making finding the fastest ones less time-consuming. It also automatically generates images for each case (for supported puzzles).

## Capabilities
The batch solver can generate any algorithm set where the ending state is defined by just ignoring the orientation or permutation of certain pieces (separation of pieces is also supported). There are pretty much no restrictions on the starting state.
To illustrate an example output, here is the batch solver's generated algs for OCLL:
![image](https://github.com/trangium/trangium.github.io/assets/29454205/5208a0a4-33f6-4804-a2c4-0977fccd596b)

# Input Fields

## Puzzle
This field determines which puzzle is to be solved, therefore determining what the move notation is. 3x3x3, 2x2x2, Pyraminx, Megaminx, and Skewb are natively supported puzzles: just selecting that puzzle on the dropdown is sufficient. The batch solver also supports custom puzzle definitions for any puzzle that is (1) noncommutative, (2) always has the same set of available moves, and (3) each move has a consistent action, cycling the same set of piece locations in the same way. See the [Batch Solver Custom Puzzle Documentation]([url](https://docs.google.com/document/d/1i-hCD491Jh15AY4I28BwILC8DsWux2VskRx_pgxZils/edit?usp=sharing)) for more information about how Puzzles are defined. Below is a table of which puzzles the batch solver supports.

| Puzzle               | Support                      | Comments                       |
|----------------------|------------------------------|--------------------------------|
| 3x3x3                | Yes                          |Supported moves: U, R, F, D, L, B (outer turns); u, r, f, d, l, b (wide turns); M, E, S (slice turns). The cube is only considered solved if it is in its starting orientation.
| 2x2x2                | Yes                          |Supported moves: U, R, F, D, L, B. The cube is only considered solved if it is in its starting orientation.
| NxNxN (N≥4)          | Custom                       |Not supported natively, but it is possible to create a custom puzzle definition for it.
| Skewb                | Yes                          |Uses fixed corner notation. Supported moves: L, R, B, U
| Pyraminx             | Yes                          |Supported moves: U, R, L, B (2-layer turns); u, r, l, b (tips)
| Megaminx             | Yes                          |Supported moves: U, R, L, F, Dfr, Br, Bl. This definition was created by Speedsolving forums user OreKehStrah.
| Square-1             | No                           |Unsupported because the puzzle does not always have the same set of available moves, because it bandages.
| Clock                | No                           |Unsupported because the puzzle is commutative.
| FTO                  | Custom                       |Not supported natively, but it is possible to create a custom puzzle definition for it.


## Unique Orientations And Equivalences

This field determines which state the batch solver is reducing to. Placing a set of pieces in curly braces `{}` indicates that those pieces should be treated as equivalent; that is, their permutation does not matter. Placing `1:` before a line indicates that the orientation of all pieces on that line does not matter (regardless of whether those pieces are in curly braces). 
Advanced: It is also possible to replace the `1` with any number to set the number of unique orientations of all pieces on that line to that number, as long as the set number of unique orientations is a factor of the original number of orientations for all pieces on that line. For example, suppose supercube center `RRRR` has 4 orientations, and you want to reduce it to either solved or a 180-degree rotation away. That can be done with the line `2: RRRR`.

## Subgroup

This parameter tells the solver which subgroup of moves defined in Puzzle are to be searched. The moves can be separated by commas and/or spaces and can be in any order, so `R U F`, `U,R,F`, and `F, R, U` are all valid ways of searching for solutions using only R, U, and F moves. 

If nothing is entered in, that subgroup will contain all defined moves. 
It is possible to enter any single turn as a move in Subgroup, so `R2 U` is a valid subgroup.

## Prune

The batch solver computes all states that are up to a certain number of moves from a solved state using only moves in the subgroup, storing the solutions to each state. This is called a prune table. There are two ways of entering an input to Prune.

If a number n is entered, it stores all states that are up to n moves from a solved state using only moves in the subgroup. For example, if the subgroup is `R U F` and the Prune is `8`, this will result in on the order of 6^8 states stored in memory. (Why 6? Well, after the first move, there are 6 non-redundant options for each subsequent move. If the first move was R', then the non-redundant options are U, U2, U', F, F2, F'. So, in general, the number of states stored is on the order of the number of non-redundant options for each move to the power of the Prune value.)

If a number and a letter is entered, such as `300k` or `5m`, the batch solver will try to determine the maximum number n such that the number of states up to n moves away is less than or equal to the input. For example, if the subgroup is `R U` and the Prune is `1m`, the batch solver determines that there are fewer than 1 million states reachable from a solved state with eleven R and U moves, but more than 1 million states reachable from a solved state with twelve R and U moves, so it prunes up to depth 11. It is as if `11` had been entered into Prune.

## Search

After creating the prune table, the batch solver applies all possible sequences of moves on the scrambled cube (and initial pre-adjustments of the scrambled cube) that use up to a certain number of moves in the subgroup. Then, if a state is reached that matches a state in the prune table, a solution has been found. So, the maximum length of solution that the batch solver can find is equal to the prune value plus the search value. There are multiple ways of entering an input into Search.

If a number n is entered, it applies all possible sequences of up to n moves in the subgroup. For example, if the subgroup is `R U F`, the Prune is `8`, and the search is `10`, solutions up to 18 moves long that use only R, U, and F moves can be found, where the length of the solution does not include pre- or post-adjustment moves.
You can also indicate that the search depth be the same as the prune depth with `=`, a certain number more than the prune depth with that number of pluses, or a certain number less than the prune depth with that number of minuses (e.g. `+` would mean the search depth is 1 more than the prune depth, and `---` would mean that the search depth is 3 less than the prune depth). This is primarily useful when you input a state count such as `300k` in Prune.

## Pre-Adjust
This determines which faces should count as adjustable. For most algorithm sets, this is the U face. It is possible to have multiple adjustable moves as long as they commute. For example, PBL could have adjustable moves of `U D`. All adjustable moves must occur in each Subgroup. For example, if the Pre-Adjust is `U`, all subgroups must contain `U`.

## Post-Adjust
This field is for the “symmetry” of the ending position. For most algorithm sets, this is the same as Pre-Adjust. However, if the set of pieces that become solved/permuted/oriented are not invariant under adjustments, the post-adjust becomes the adjustment with which it is invariant. For example, solving two opposite corners on the U layer would have post adjust `U2`, and solving a 1x2x2 block on the U layer would have an empty post adjust ` `.

## Scramble
This field tells the batch solver which states to solve. This is the most powerful part of the program. The batch solver reads the Scramble field left to right:

 - Any moves outside of square or angle brackets are just executed.
 - Square brackets indicate multiple paths. For example, an input of `[R U R' U R U2 R', R U2 R' U' R U' R']` would search for both sune and antisune algorithms.
 - Angle brackets indicate generators. For example, an input of `<R U R' F' R U R' U' R' F R2 U' R', R2 U R U R' U' R' U' R' U R', U>` would search full PLL.

Adding `#` after the main part of the scramble will specify only to generate certain cases, based on their position. The supported syntaxes for each comma-separated entry are [number], indicating a single case; [number]-[number], indicating a range; and [number]+, indicating the range from that number to the end.

### Examples

- If the Unique Orientations & Equivalences are `{UF UR UB UL} {UFR UBR UBL UFL}` and the scramble is `<F R U R' U' F', R U R' U R U2 R', U> R U R' U'`, then the batch solver is first allowed to execute any combination of F R U R' U' F', Sune, and U (which generates any OLL), then must execute R U R' U' on that state, so this generates VLS. 

- The scramble `<R' U L' U2 R U' R' U2 R L, R2 U R U R' U' R' U' R' U R', U> [F R' F' r U R U' r', r U R' U' r' F R F', R U2 R D R' U2 R D' R2]` generates full TUL ZBLL. The first part of the scramble generates any PLL, then for each of those states, the three algorithms in square brackets are executed on them, giving any T, U, or L ZBLL.

- If you generated the first 50 cases of an algset, and now want to generate the rest, simply add `#51+` to the end of Scramble, and it will gen starting from case number 51.
  
- If you generated a set, but cases 26 and 33 failed, then `#26,33` at the end of Scramble will gen only these two cases (assuming the rest of Scramble stays the same).

- If case 1 has a really short alg, and you want to skip the case and have the batch solver move on, you can stop the search, add `2+` to the end of the scramble, then start the search.

- If you forgot to add in a subgroup that led to cases 2-21 and 25-40 having really bad algs, simply add `#2-21,25-40` to the end of Scramble and restart the search.

## Case Sorting

There are five options for the left dropdown, explained below.

- **Set Priority**: Sets the priority of the pieces indicated in the field on the right. Any pieces indicated will have higher priority than any pieces not indicated. The first piece will have highest priority and so on. By default, the priority corresponds to the order that the pieces are defined in Puzzle. Priorities are used for the Permutation options.
- **Orientation Of**: Same as Orientation At, but instead by the pieces that belong in the indicated locations. If you use this while also specifying those pieces as equivalent, you might not get the results you expected, since multiple pieces could belong in the indicated locations. (It depends on the generators used in Scramble.)
- **Orientation At**: Sorts by the orientation of the pieces at the locations indicated on the right, first by number of oriented pieces, then number of pieces with orientation 1, then number of pieces with orientation 2 (if applicable) ... , then orientation of each individual piece in order of the input. This is more commonly used than Orientation Of. For example: Orientation of UFR UFL UBL UBR sorts by CO.
- **Permutation Of**: Same as Permutation At, but instead by the pieces that belong in the indicated locations. If you use this while also specifying those pieces as equivalent, you might not get the results you expect ed, since multiple pieces could belong in the indicated locations. (It depends on the generators used in Scramble.)
- **Permutation At**: Sorts by the permutation of the pieces at the locations indicated on the right. More specifically, sorts by the priority of the piece at the first indicated location, then 2nd, then 3rd, etc. 

If there are multiple sorting criteria, the top one will have highest priority, and subsequent ones will act as tiebreakers.

## Options
- **Sort algs by**: There are four options. 
  - **MCC** aims to approximate the speed of an algorithm on a 3x3x3 (lower = faster). However, MCC returns NaN if a puzzle is used with moves outside of the 3x3x3 move set.
  - **STM** is the number of moves in the algorithm.
  - **SQTM** is the number of single turns in the algorithm, so moves with a "2" at the end count as two moves, and likewise for "3", etc.
  - **ESQ** is based on the number of wrist/flick turns, and the number of quarter/half turns. By default, a wrist quarter turn counts as one move, a flick quarter turn or wrist half turn counts as two, and a flick half turn counts as three. "Wrist" is defined as an R or L move, and "quarter turn" is defined as a single turn (a move without a number at the end). "Half turn" is defined as a move with a "2" at the end. Moves with a "3" or more at the end are counted linearly more, so the jump between a single turn and a double turn is the same as the jump between a double turn and a triple turn.
- **Secondary metric**: If any option other than "None" is selected, the batch solver will show the value of the secondary metric at the end of each solution. If two algorithms are tied in the primary metric, the secondary metric breaks the tie for sorting purposes.

## Image Options
A preview of the images can be shown by selecting "Show preview". The slider that shows up under the preview only affects the preview; it does not affect the Y rotation of the images that show up in the search.

Below is a table of how images work for each supported puzzle.

<table>
    <thead>
        <tr>
            <th>Puzzle</th>
            <th>Notation</th>
            <th>Mask</th>
        </tr>
    </thead>
    <tbody>
        <tr>
            <td>3x3x3</td>
            <td>Consistent with built-in puzzle definition.</td>
            <td rowspan=5>Colors: <br /><br />r (red)<br />o (orange)<br />y (yellow)<br />g (green)<br />b (blue)<br />w (white)<br />s (silver)<br />l (light gray)<br />d (dark gray)<br />n (black)<br />m (magenta)<br />p (pink)<br />
            </td>
        </tr>
        <tr>
            <td>2x2x2</td>
            <td>Consistent with built-in puzzle definition, rotations also supported.</td>
        </tr>
        <tr>
            <td>4x4x4</td>
            <td>Outer layer moves: U D L R F B <br />Wide moves: u d l r f b <br /> Inner slice moves are currently not supported.</td>
        </tr>
        <tr>
            <td>5x5x5</td>
            <td>Outer layer moves: U D L R F B <br />Wide moves: u d l r f b <br />Slice moves: M E S <br /> <br />Slice moves only turn one central layer. Other inner slices are currently not supported.</td>
        </tr>
        <tr>
            <td>Skewb</td>
            <td>Inconsistent with built-in puzzle definition. <br/> <br/> Supports these moves, with the corner the move rotates about in parentheses: <br/> U (UFR), R (DBR), L (DFL), B (DBL), f (DFR), r (UBR), l (UFL), b (UBL).</td>
        </tr>
        <tr>
          <td> Pyraminx </td>
          <td> Consistent with built-in puzzle definition. </td>
          <td> r (red) <br /> y (yellow) <br /> g (green) <br /> B (light blue) <br /> <br />Any other character will show up as black.
        </tr>
        <tr>
          <td> Megaminx </td>
          <td> Consistent with built-in puzzle definition. <br /> <br /> Outer layer moves: U R F L Bl Br Dfr Dfl Dbl Db Dbr D <br /> Wide moves: u r f l bl br d
          <td> r (red) <br />o (orange)<br />y (yellow)<br />g (green)<br />b (blue)<br />w (white)<br />a (gray)<br />m (magenta)<br />p (pink)<br />Y (light yellow)<br />G (light green)<br />B (light blue)<br /> <br /> Any other character will show up as black. </td>
        </tr>
    </tbody>
</table>

 ## ESQ Options

This field makes it possible to define custom metrics where every move is weighted a fixed amount. The syntax for both the Rank ESQ field and the Generation ESQ field are the same:
Each line must contain a list of moves and/or move families (separated by commas and/or spaces), then a colon, then the weight assigned to that move or move family. A move family is a move where the move name and/or move amount have been replaced by underscores, and it represents all possible moves that match it. For example, on the 3x3x3, `U_` matches U, U', and U2; `_2` matches all half turns, `_` matches all clockwise quarter turns, and `__` matches all moves. If a move is matched by more than one line, the more specific one takes precedence (e.g. for R2, the order of precedence is `R2`, `R_`, `_2`, `__`.) If a move is not matched by any line, it is given a default weight of 1. A full example that demonstrates all major aspects of this syntax is the default definition for ESQ, given below:
```
R_ L_ r_ l_: 1
R2 L2 r2 l2: 2
__: 2
_2: 3
```
A definition given in the Rank ESQ field affects the statistics and the ranking of algs (if ESQ is chosen as a metric to sort algs by.) 

A definition given in the Generation ESQ field affects how algs are generated, and how the Prune and Search fields work. Selecting "Default" in the dropdown (or equivalently, a custom definition with nothing in it) simply generates algs up to a certain STM. However, if a non-default definition is given, the batch solver will now find all algorithms up to a given ESQ, instead of a given STM. Likewise, if "Match" is selected, the generation ESQ will find all algorithms up to a given ESQ, where the ESQ weights are given by the definition in the Rank ESQ field.

For example, to generate algorithms by SQTM, use this definition in Generation ESQ:
`_2: 2`

## Statistics
- **Case:** Shows the number of completed cases of the current search and the number of total cases. If there are failed cases, it will show how many cases failed. Clicking on it will copy the case numbers for the failed cases to your clipboard in exactly the “#” format (see **Scramble**), allowing for easy regeneration of these cases.
- **Rate:** Shows the average rate of case generation across all cases so far in the current search.
- **MCC / STM / SQTM / ESQ:** Shows the average of that metric across all _top_ algorithms in the current search, where "top" means it is the first displayed algorithm in its box.

## Output
This is where solutions to each case are shown.
Hovering over an image in Output will give its case number.
To save the output, there are multiple options:

- Option 1. Save as webpage: Simply press Ctrl+S and save.

- Option 2. Save as Excel file: Simply press "Export Output".

- Option 3. Save as Google sheet:
  - Press "Export Output" to save as an Excel file.
  - Create a new Google Sheet.
  - Import the Excel file.
    - Click "File" > "Import".
    - Click "Upload" > "Select a file from your device".
    - Select the excel file you just created.
    - Click "Import Data".


