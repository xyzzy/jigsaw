
LS: A cell is a entry in the 20x20 grid

## WHERE DID jigsaw begin - short words, complex meshes, corners, etc.??? Why?

Jigsaw mainly starts from the top-left corner and works it way down to the
bottom-right. New words a placed in such a fashion that they fit tightly
against the already placed words. Why? I've tried many other algorithms but
it seems that they create 'islands' (an island is a large cluster of cells
that cannot be filled with letters). The presence of islands use up valuable
grid space, thus  lowering the score. I've even tried island detection
algorithms, but they cost too many CPU.

The placing of new words can create many new grids, too many to process
within the CPU time limit. To reduce calculation time, a selection must
be made of grids which are to be generated. Therefore, we can be very fussy
about which word will be placed where. It makes no sense to create a new
grid that contains a pathological bad solution. New words are placed
in the following order:

  - Adjacent letters are handled specially. When adjacent letters are
    found that do not belong to an already placed word, new grids are
    generated containing *ALL* word possibilities. These grids do not
    count against GRIDMAX (see below).

  - A word can *only* be placed if at least one letter of the word is
    already present in the grid, *AND* when newly placed letters will
    be adjacent to already existing letters, those letter pairs (or
    three-sums) must exist somewhere in the wordlist.

  For the nearest unprocessed letter located to the top-left corner:

  - If a word can be placed using the selected letter and the starting
    letter of that word fits tightly to the words already placed (max.
    one free cell distance to an existing letter), then place that word.

  - If no words could be found, then try placing *one* word using the
    selected letter in such a way that the first letter of the word
    is located nearest to an existing letter. Only one word may be
    placed as there are many combinations possible, and jigsaw must
    be fussy.

  - If still no word has been found, mark the selected letter as
    unprocessable and find a new letter and

  - Stop generating new grids when more than GRIDMAX grids have created.
    (unprocessed adjacent letter pairs are special and always processed).

## HOW DID jigsaw try to maximize the number of words and intersections used?

Jigsaw's algorithm needs to know how good a grid is. Good means it is
candidate to get a high final score. Jigsaw continues filling grids that
have high scores, and discards grids that have low scores. The scoring
formula is:

   /

This implies:

  - Many connectors good. If a word has more connectors it means that
    other words are occupying the neighbouring space, thus more words will
    fit the grid.

  - Many letters bad. It's better to have two short words than one bad.

The number of words are not taken into account, as all score comparisons
are done with grids containing the same number of words.

For comparison, I've tries some other formulas, but they didn't work
out for the better.

## Did jigsaw iterate?  randomize?  attempt to improve it's first solution?

Jigsaw uses a non-exhaustive width-first tree-search algorithm. The level
on which a node resides in the tree indicates the number of words placed.
The root contains no words, all nodes on the first level contain 1 word,
and so on.

The algorithm processes one level at a time, starting at the root. It takes
NODEMAX (1500) nodes of a single level with the highest score, adds a word
forming a new node of a next tree level. Once the level has been completed,
all nodes of the (old) level are deleted to free memory space. This implies
that there is no recallable history of how the grid was created. Duplicate
grid detection is needed to compensate the lack of history. For example:

  ---f-
  your-
  -n-o-
  blank
  -y-t-

This grid can be created by placing (in sequence) "your front blank only"
or "your only blank front". If there wasn't duplicate detection, the tree
would contain many of these (duplicate) combinations, effectively reducing
the algorithms outcome dramatically.

## How did jigsaw know it was done?

Simple, when no more words can be inserted into the grid.

## ANY OTHER COMMENTS, TRICKS, HELP FOR THE UNWASHED, WHATEVER?

This scoring algorithm has one flaw. It tends to nominate very tight grids
for the first handful of words consisting mainly of short words. When the
grid is about half full, only larger words can be placed that are
not so tight, but there are no shorter words available anymore. One solution
is to 'pollute' the levels with grids containing relatively bad scores in the
hope that they might work out better when the grid is starting to get full.

I've implemented polluting by giving NODEMAX a relatively low value of 1500.
A better method is to add a random value to the score, but I havn't had the
time to implement/test that.