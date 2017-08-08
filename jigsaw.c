/*
   jigsaw, to create crossword puzzle grids
   Copyright 1996 https://github.com/xyzzy

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/times.h>
#include <signal.h>

// Configuring parameters

int opt_debug;						// 0=Off 1=On 2=Verbose
int opt_symmetrical;					// 0=No 1=Yes
int opt_dump;						// 0=No 1=after every round 2=after every addNode
int opt_timemax = (10*60-15);				// 10 minute limit
int opt_nodemax = 15000;				// 500=Fast 1500=Normal

#define GRIDMAX		22				// Size of grid incl. border
#define LINKMAX		4096				// # links
#define WORDMAX		256				// # words
#define WORDLENMAX	32				// # length of word
#define ADJMAX		128				// # unaccounted adjacent chars
#define SCOREMAX	1000				// Spead for hashing

#define BASE		('a'-1)				// Word conversion
#define STAR		(28)				// Word delimiters
#define FREE		(31)				// Unoccupied grid cell
#define TODOH		1				// Hint: Hor. word can be here
#define TODOV		2				// Hint: Ver. word can be here
#define BORDER		4				// Cell is grid border

#define ISSTAR(C)	((C)==STAR)			// Test the above
#define ISFREE(C)	((C)==FREE)
#define ISCHAR(C)	((C)< STAR)
#define ISBORDER(C)	((C)&BORDER)

#define BITSET(M, B) ((M).s[(B)>>3] |=  (1<<((B)&7)))	// BitSet manipulation
#define BITCLR(M, B) ((M).s[(B)>>3] &= ~(1<<((B)&7)))
#define INSET(M, B)  ((M).s[(B)>>3] &   (1<<((B)&7)))

typedef struct {					// Bitset containing 256 bits
	uint8_t s[32];
} SET;

struct node {
	struct node	*next;				//
	int		seqnr;				// For diagnostics
	SET		words;				// Summary of placed words
	int		numword, numchar, numconn;	// Statistics
	uint32_t	hash;				// Duplicate detection
	int		firstlevel, lastlevel;		// Grids hotspot
	float		score;				// Will it survive?
	int8_t		symdir;				// Force symmetry
	int16_t		symxy;				//
	int16_t		symlen;				//
	int		numadj;				// # unprocessed char pairs
	int8_t		adjdir[ADJMAX];			// Pair's direction
	int16_t		adjxy[ADJMAX];			// Pair's location
	int16_t		adjl[ADJMAX];			// Pair's wordlist
	uint8_t		grid[GRIDMAX * GRIDMAX];	// *THE* grid
	uint8_t		attr[GRIDMAX * GRIDMAX];	// Grid hints
};

struct link {
	int16_t next;
	int16_t w;					// What's the word
	int16_t ofs;					// Were's in the word
};


// The external word list
uint8_t wordbase[WORDMAX][WORDLENMAX];			// Converted wordlist
int wlen[WORDMAX];					// Length of words
int numword;						// How many

// Where are 1,2,3 long character combinations
int16_t links1[32];					// 1-char wordlist
int16_t links2[32][32];					// 2-char wordlist
int16_t links3[32][32][32];				// 3-char wordlist
struct link linkdat[LINKMAX];				// Body above wordlist
int numlinkdat;						// How many

// Hotspot pre-calculations
int16_t xy2level[GRIDMAX * GRIDMAX];			// distance 0,0 to x,y
int16_t level2xy[GRIDMAX * 2];				// inverse

// Node administration
struct node *freenode;					// Don't malloc() too much
int numnode, realnumnode;				// Statistics
struct node solution;					// What are we doing?
struct node *scores[SCOREMAX];				// Speed up hashing

// Diagnostics
int seqnr;
int hashtst, hashhit;
int nummalloc;
int numscan;

// What's left
int ticks;

/*
 * Timer logic
 */
void sigAlarm(int s) {
	ticks++;
	alarm(1);
}


/*
 * Elapsed time for diagnostics
 */
char *elapsedstr(void) {
	static char line[40];

	sprintf(line, "%02d:%02d", ticks / 60, ticks % 60);
	return line;
}

/*
 * Display the grid. Show disgnostics info in verbose mode
 */

void dump_grid(struct node *d) {
	int x, y, cnt, attr;

	if (opt_debug == 2) {
		// Double check the number of words
		cnt = 0;
		for (x = GRIDMAX * GRIDMAX - 1; x >= 0; x--)
			if (ISCHAR(d->grid[x])) {
				if (!ISCHAR(d->grid[x - 1]) && ISCHAR(d->grid[x + 1])) cnt++;
				if (!ISCHAR(d->grid[x - GRIDMAX]) && ISCHAR(d->grid[x + GRIDMAX])) cnt++;
			}
		printf("%s seqnr:%d level:%d/%d score:%f numword:%d/%d numchar:%d numconn:%d\n",
		       elapsedstr(), d->seqnr, d->firstlevel, d->lastlevel, d->score, d->numword,
		       cnt, d->numchar, d->numconn);
	}

	if (opt_debug == 2) {
		// Show grid as I would like to see it
		for (y = 0; y < GRIDMAX; y++) {
			for (x = 0; x < GRIDMAX; x++) {
				attr = d->attr[x + y * GRIDMAX] & (TODOH | TODOV);
				if (attr == (TODOH | TODOV))
					printf("B");
				else if (attr == TODOH)
					printf("H");
				else if (attr == TODOV)
					printf("V");
				else
					printf(".");
			}
			printf(" ");
			for (x = 0; x < GRIDMAX; x++)
				if (ISSTAR(d->grid[x + y * GRIDMAX]))
					printf("*");
				else if (ISFREE(d->grid[x + y * GRIDMAX]))
					printf(".");
				else if (ISCHAR(d->grid[x + y * GRIDMAX]))
					printf("%c", d->grid[x + y * GRIDMAX] + BASE);
				else
					printf("0x%02x", d->grid[x + y * GRIDMAX]);
			printf("\n");
		}
	} else {
		// Show grid as Fred would like to see it
		for (y = 1; y < GRIDMAX - 1; y++) {
			for (x = 1; x < GRIDMAX - 1; x++)
				if (ISCHAR(d->grid[x + y * GRIDMAX]))
					printf("%c", d->grid[x + y * GRIDMAX] + BASE);
				else
					printf("-");
			printf("\n");
		}
	}

	// For diagnostics
	fflush(stdout);
}

/*
 * Get a node, reusing free'ed nodes.
 */

struct node *mallocnode(void) {
	struct node *d;

	d = freenode;
	if (d == NULL) {
		d = (struct node *) malloc(sizeof(struct node));
		nummalloc++;
	} else
		freenode = d->next;

	if (d == NULL) {
		fprintf(stderr, "Out of memory after %d nodes (%d bytes)\n", nummalloc, (int)(nummalloc * sizeof(struct node)));
		dump_grid(&solution);
		exit(0);
	}

	return d;
}

void add_node(struct node *d) {
	int i;
	struct node **prev, *next;

	// Evaluate grids score
	d->score = (float) d->numconn / d->numchar;

	// Insert grid into sorted list, eliminating duplicates
	i = (int)(d->score * (SCOREMAX - 1));
	if (i < 0) i = 0;
	if (i >= SCOREMAX) i = SCOREMAX - 1;
	prev = &scores[i];
	next = scores[i];
	for (;;) {
		if (next == NULL) break;
		if (d->score > next->score) break;
		if (d->score == next->score && d->hash >= next->hash) break;
		prev = &next->next;
		next = next->next;
	}

	// Test if entry is duplicate
	while (next && d->score == next->score && d->hash == next->hash) {
		hashtst++;
		if (memcmp(d->grid, next->grid, sizeof(d->grid)) == 0) {
			d->next = freenode;
			freenode = d;
			return;
		}
		hashhit++;
		prev = &next->next;
		next = next->next;
	}

	// Diagnostics
	if (opt_dump > 1)
		dump_grid(d);

	// Enter grid into list
	d->seqnr = seqnr++;
	d->next = next;
	(*prev) = d;
	if (d->numadj == 0)
		numnode++;
	realnumnode++;
}


/*
 * The next two routines test if a given word can be placed in the grid.
 * If a new character will be adjecent to an existing character, check
 * if the newly formed character pair exist in the wordlist (it doesn't
 * matter where).
 */

int test_hword(struct node *d, int xybase, int word) {
	uint8_t *p, *grid;
	int l;

	// Some basic tests
	if (xybase < 0 || xybase + wlen[word] >= GRIDMAX * GRIDMAX + 1) return 0;

	if (opt_symmetrical) {
		// How about star's
		if (ISCHAR(d->grid[GRIDMAX * GRIDMAX - 1 - xybase]))
			return 0;
		if (ISCHAR(d->grid[GRIDMAX * GRIDMAX - 1 - (xybase + wlen[word] - 1)]))
			return 0;
	}

	// Will new characters create conflicts
	for (grid = d->grid + xybase, p = wordbase[word]; *p; grid++, p++) {
		if (*grid == *p)
			continue; // Char already there
		if (!ISFREE(*grid))
			return 0; // Char placement conflict
		if (ISSTAR(*p))
			continue; // Skip stars
		if (!ISCHAR(grid[-GRIDMAX]) && !ISCHAR(grid[+GRIDMAX]))
			continue; // No adjecent chars

		if (ISFREE(grid[-GRIDMAX]))
			l = links2[*p][grid[+GRIDMAX]];
		else if (ISFREE(grid[+GRIDMAX]))
			l = links2[grid[-GRIDMAX]][*p];
		else
			l = links3[grid[-GRIDMAX]][*p][grid[+GRIDMAX]];
		if (l == 0)
			return 0;
	}

	// Word can be placed
	return 1;
}

int test_vword(struct node *d, int xybase, int word) {
	uint8_t *p, *grid;
	int l;

	// Some basic tests
	if (xybase < 0 || xybase + wlen[word] * GRIDMAX >= GRIDMAX * GRIDMAX + GRIDMAX) return 0;

	if (opt_symmetrical) {
		// How about star's
		if (ISCHAR(d->grid[GRIDMAX * GRIDMAX - 1 - xybase]))
			return 0;
		if (ISCHAR(d->grid[GRIDMAX * GRIDMAX - 1 - (xybase + wlen[word] * GRIDMAX - GRIDMAX)]))
			return 0;
	}

	// Will new characters create conflicts
	for (grid = d->grid + xybase, p = wordbase[word]; *p; grid += GRIDMAX, p++) {
		if (*grid == *p)
			continue; // Char already there
		if (!ISFREE(*grid))
			return 0; // Char placement conflict
		if (ISSTAR(*p))
			continue; // Skip stars
		if (!ISCHAR(grid[-1]) && !ISCHAR(grid[+1]))
			continue; // No adjecent characters

		if (ISFREE(grid[-1]))
			l = links2[*p][grid[+1]];
		else if (ISFREE(grid[+1]))
			l = links2[grid[-1]][*p];
		else
			l = links3[grid[-1]][*p][grid[+1]];
		if (l == 0)
			return 0;
	}

	// Word can be placed
	return 1;
}


/*
 * The next two routines will place a given word in the grid. These routines
 * also performs several sanity checks to make sure the new grid is worth
 * it to continue with. If a newly placed character is adjecent to an
 * existing character, then that pair must be part of a word that can
 * be physically placed. If multiple character pairs exist, then no check
 * is done to determine if those words (of which the pairs are part) can
 * be adjecent. That is done later as these grids are not counted against
 * NODEMAX.
 */

int place_hword(struct node *data, int xybase, int word) {
	struct node *d = data;
	uint8_t *p, *grid, *attr;
	int i, l, xy;
	int newnumadj;
	struct link *ld;

	// Can word be placed
	if (INSET(d->words, word)) return 0;
	if (xybase < 0 || xybase + wlen[word] >= GRIDMAX * GRIDMAX + 1) return 0;

	if (opt_symmetrical) {
		// How about star's
		if (ISCHAR(d->grid[GRIDMAX * GRIDMAX - 1 - xybase]))
			return 0;
		if (ISCHAR(d->grid[GRIDMAX * GRIDMAX - 1 - (xybase + wlen[word] - 1)]))
			return 0;
	}

	// check character environment
	newnumadj = d->numadj;
	for (xy = xybase, grid = d->grid + xy, p = wordbase[word]; *p; grid++, xy++, p++) {
		if (*grid == *p)
			continue; // Char already there
		if (!ISFREE(*grid))
			return 0; // Char placement conflict
		if (ISSTAR(*p))
			continue; // Skip stars
		if (!ISCHAR(grid[-GRIDMAX]) && !ISCHAR(grid[+GRIDMAX]))
			continue; // No adjecent chars

		if (ISFREE(grid[-GRIDMAX])) {
			d->adjxy[newnumadj] = xy;
			d->adjl[newnumadj] = links2[*p][grid[+GRIDMAX]];
		} else if (ISFREE(grid[+GRIDMAX])) {
			d->adjxy[newnumadj] = xy - GRIDMAX;
			d->adjl[newnumadj] = links2[grid[-GRIDMAX]][*p];
		} else {
			d->adjxy[newnumadj] = xy - GRIDMAX;
			d->adjl[newnumadj] = links3[grid[-GRIDMAX]][*p][grid[+GRIDMAX]];
		}
		if (d->adjl[newnumadj] == 0 || newnumadj == ADJMAX - 1)
			return 0;
		d->adjdir[newnumadj++] = 'V';
	}

	// Test if new adj's really exist
	for (i = d->numadj; i < newnumadj; i++) {
		for (l = d->adjl[i]; l; l = ld->next) {
			ld = &linkdat[l];
			if (test_vword(d, d->adjxy[i] + ld->ofs * GRIDMAX, ld->w))
				break;
		}
		if (l == 0) return 0;
		d->adjl[i] = l;
	}

	// Get a new grid
	d = mallocnode();
	memcpy(d, data, sizeof(struct node));

	// Place word
	BITSET(d->words, word);
	d->numword++;
	d->numadj = newnumadj;
	for (xy = xybase, grid = d->grid + xy, attr = d->attr + xy, p = wordbase[word];
	     *p;
	     grid++, attr++, xy++, p++) {
		if (!ISSTAR(*p)) {
			*attr &= ~TODOH;

			// Remove character pair hints that are part of the new word
			for (i = 0; i < d->numadj; i++)
				if (d->adjdir[i] == 'H' && d->adjxy[i] == xy) {
					d->adjdir[i] = d->adjdir[--d->numadj];
					d->adjxy[i] = d->adjxy[d->numadj];
					d->adjl[i] = d->adjl[d->numadj];
					break; // There can be only one
				}

			// Place character
			if (ISFREE(*grid)) {
				d->hash += (123456 + xy) * (123456 - *p); // Neat, isn't it?
				*attr |= TODOV;
				d->numchar++;
			} else {
				d->numconn++;
			}
		}
		*grid = *p;
	}

	// Update hotspot
	if (xy2level[xy - 1] > d->lastlevel)
		d->lastlevel = xy2level[xy - 1];

	if (opt_symmetrical) {
		// Don't forget the symmetry
		if (d->symdir == 0) {
			d->symdir = 'H';
			d->symxy = GRIDMAX * GRIDMAX - 1 - (xybase + wlen[word] - 1);
			d->symlen = wlen[word];
		} else
			d->symdir = 0;
	}

	// Save grid
	add_node(d);
	return 1;
}

int place_vword(struct node *data, int xybase, int word) {
	struct node *d = data;
	uint8_t *p, *grid, *attr;
	int i, xy, l;
	int newnumadj;
	struct link *ld;

	// Some basic tests
	if (INSET(d->words, word)) return 0;
	if (xybase < 0 || xybase + wlen[word] * GRIDMAX >= GRIDMAX * GRIDMAX + GRIDMAX) return 0;

	if (opt_symmetrical) {
		// How about star's
		if (ISCHAR(d->grid[GRIDMAX * GRIDMAX - 1 - xybase]))
			return 0;
		if (ISCHAR(d->grid[GRIDMAX * GRIDMAX - 1 - (xybase + wlen[word] * GRIDMAX - GRIDMAX)]))
			return 0;
	}

	// Check character environment
	newnumadj = d->numadj;
	for (xy = xybase, grid = d->grid + xy, p = wordbase[word]; *p; grid += GRIDMAX, xy += GRIDMAX, p++) {
		if (*grid == *p)
			continue; // Char already there
		if (!ISFREE(*grid))
			return 0; // Char placement conflict
		if (ISSTAR(*p))
			continue; // Skip stars
		if (!ISCHAR(grid[-1]) && !ISCHAR(grid[+1]))
			continue; // No adjecent chars

		if (ISFREE(grid[-1])) {
			d->adjxy[newnumadj] = xy;
			d->adjl[newnumadj] = links2[*p][grid[+1]];
		} else if (ISFREE(grid[+1])) {
			d->adjxy[newnumadj] = xy - 1;
			d->adjl[newnumadj] = links2[grid[-1]][*p];
		} else {
			d->adjxy[newnumadj] = xy - 1;
			d->adjl[newnumadj] = links3[grid[-1]][*p][grid[+1]];
		}
		if (d->adjl[newnumadj] == 0 || newnumadj == ADJMAX - 1)
			return 0;
		d->adjdir[newnumadj++] = 'H';
	}

	// Test if new adj's really exist
	for (i = d->numadj; i < newnumadj; i++) {
		for (l = d->adjl[i]; l; l = ld->next) {
			ld = &linkdat[l];
			if (test_hword(d, d->adjxy[i] + ld->ofs, ld->w))
				break;
		}
		if (l == 0) return 0;
		d->adjl[i] = l;
	}

	// Get a new grid
	d = mallocnode();
	memcpy(d, data, sizeof(struct node));

	// Place word
	BITSET(d->words, word);
	d->numword++;
	d->numadj = newnumadj;
	for (xy = xybase, grid = d->grid + xy, attr = d->attr + xy, p = wordbase[word];
	     *p;
	     grid += GRIDMAX, attr += GRIDMAX, xy += GRIDMAX, p++) {
		if (!ISSTAR(*p)) {
			*attr &= ~TODOV;

			// Remove character pair hints that are part of the new word
			for (i = 0; i < d->numadj; i++)
				if (d->adjdir[i] == 'V' && d->adjxy[i] == xy) {
					d->adjdir[i] = d->adjdir[--d->numadj];
					d->adjxy[i] = d->adjxy[d->numadj];
					d->adjl[i] = d->adjl[d->numadj];
					break; // There can be only one
				}

			// Place character
			if (ISFREE(*grid)) {
				*attr |= TODOH;
				d->hash += (123456 + xy) * (123456 - *p); // Neat, isn't it?
				d->numchar++;
			} else {
				d->numconn++;
			}
		}
		*grid = *p;
	}

	// Update hotspot
	if (xy2level[xy - GRIDMAX] > d->lastlevel)
		d->lastlevel = xy2level[xy - GRIDMAX];

	if (opt_symmetrical) {
		// Don't forget the symmetry
		if (d->symdir == 0) {
			d->symdir = 'V';
			d->symxy = GRIDMAX * GRIDMAX - 1 - (xybase + wlen[word] * GRIDMAX - GRIDMAX);
			d->symlen = wlen[word];
		} else
			d->symdir = 0;
	}

	// Save grid
	add_node(d);
	return 1;
}


/*
 * Scan a grid and place a word. To supress an exponential growth of
 * generated grids, we can be very fussy when chosing which word to
 * place. I have chosen to fill the grid from top-left to bottom-right
 * making sure the newly placed words fit tightly to the already placed
 * words. If this is not possible, then I choose just one word such that
 * the first letter is nearest to the start of the hotspot regeon.
 * This sounds easy but it took me quite some time to figure it out.
 */

void scan_grid(struct node *d) {
	uint8_t *grid, *attr;
	int xy, l, cnt, tstxy, level, w;
	struct link *ld;
	int hasplace, hasfree;

	if (opt_symmetrical) {
		// Don't forget the symmetry
		if (d->symdir == 'H') {
			for (w = numword - 1; w >= 0; w--)
				if (!INSET(d->words, w) && wlen[w] == d->symlen)
					place_hword(d, d->symxy, w);
			return;
		}
		if (d->symdir == 'V') {
			for (w = numword - 1; w >= 0; w--)
				if (!INSET(d->words, w) && wlen[w] == d->symlen)
					place_vword(d, d->symxy, w);
			return;
		}
	}

	// locate unaccounted adjecent cells
	if (d->numadj > 0) {
		xy = d->adjxy[--d->numadj];
		if (d->adjdir[d->numadj] == 'H') {
			for (l = d->adjl[d->numadj]; l; l = ld->next) {
				ld = &linkdat[l];
				place_hword(d, xy + ld->ofs, ld->w);
			}
		} else {
			for (l = d->adjl[d->numadj]; l; l = ld->next) {
				ld = &linkdat[l];
				place_vword(d, xy + ld->ofs * GRIDMAX, ld->w);
			}
		}
		return;
	}

	// Nominate grid for final result
	if (d->numword > solution.numword)
		memcpy(&solution, d, sizeof(struct node));

	// Sweep grid from top-left to bottom-right corner
	for (level = d->firstlevel; level <= d->lastlevel && level <= GRIDMAX * 2 - 4; level++) {

		if (!opt_symmetrical) {
			// Locate 'tight' words
			hasplace = 0;
			for (xy = level2xy[level], grid = d->grid + xy, attr = d->attr + xy;
			     !ISBORDER(*attr);
			     xy += GRIDMAX - 1, grid += GRIDMAX - 1, attr += GRIDMAX - 1) {
				if (*attr & TODOH) {
					for (l = links1[*grid]; l; l = ld->next) {
						ld = &linkdat[l];
						tstxy = xy + ld->ofs;
						if (xy2level[tstxy] == d->firstlevel)
							hasplace += place_hword(d, tstxy, ld->w);
					}
				}
				if (*attr & TODOV) {
					for (l = links1[*grid]; l; l = ld->next) {
						ld = &linkdat[l];
						tstxy = xy + ld->ofs * GRIDMAX;
						if (xy2level[tstxy] == d->firstlevel)
							hasplace += place_vword(d, tstxy, ld->w);
					}
				}
				if (hasplace)
					return;
			}
		}

		// Locate 'adjecent' word
		hasplace = 0;
		for (xy = level2xy[level], grid = d->grid + xy, attr = d->attr + xy;
		     !ISBORDER(*attr);
		     xy += GRIDMAX - 1, grid += GRIDMAX - 1, attr += GRIDMAX - 1) {
			if (*attr & TODOH) {
				for (l = links1[*grid]; l; l = ld->next) {
					ld = &linkdat[l];
					tstxy = xy + ld->ofs;
					if (ISSTAR(d->grid[tstxy]))
						hasplace += place_hword(d, tstxy, ld->w);
					if (opt_symmetrical) {
						if (ISSTAR(d->grid[GRIDMAX * GRIDMAX - 1 - (tstxy + wlen[ld->w] - 1)]))
							hasplace += place_hword(d, tstxy, ld->w);
					}
				}
			}
			if (*attr & TODOV) {
				for (l = links1[*grid]; l; l = ld->next) {
					ld = &linkdat[l];
					tstxy = xy + ld->ofs * GRIDMAX;
					if (ISSTAR(d->grid[tstxy]))
						hasplace += place_vword(d, tstxy, ld->w);
					if (opt_symmetrical) {
						if (ISSTAR(d->grid[GRIDMAX * GRIDMAX - 1 - (tstxy + wlen[ld->w] * GRIDMAX - GRIDMAX)]))
							hasplace += place_vword(d, tstxy, ld->w);
					}
				}
			}
			if (hasplace)
				return;
		}

		// Locate word fragments (just one word please)
		hasplace = 0;
		hasfree = 0;
		for (xy = level2xy[level], grid = d->grid + xy, attr = d->attr + xy;
		     !ISBORDER(*attr);
		     xy += GRIDMAX - 1, grid += GRIDMAX - 1, attr += GRIDMAX - 1) {
			if (ISFREE(*grid))
				hasfree = 1;
			if (*attr & TODOH) {
				cnt = 0;
				for (l = links1[*grid]; l; l = ld->next) {
					ld = &linkdat[l];
					tstxy = xy + ld->ofs;
					if (!ISSTAR(d->grid[tstxy]))
						cnt += place_hword(d, tstxy, ld->w);
					if (!opt_symmetrical) {
						if (cnt != 0)
							break;
					}
				}
				if (cnt == 0) {
					// Speed things up (Not 100% correct, but it's fast)
					*attr &= ~TODOH;
					grid[-1] = STAR;
					grid[+1] = STAR;
					if (opt_symmetrical) {
						if (ISCHAR(d->grid[GRIDMAX * GRIDMAX - 1 - (xy - 1)])) return; // Arghh
						d->grid[GRIDMAX * GRIDMAX - 1 - (xy - 1)] = STAR;
						if (ISCHAR(d->grid[GRIDMAX * GRIDMAX - 1 - (xy + 1)])) return; // Arghh
						d->grid[GRIDMAX * GRIDMAX - 1 - (xy + 1)] = STAR;
					}
				}
				hasplace += cnt;
			}
			if (*attr & TODOV) {
				cnt = 0;
				for (l = links1[*grid]; l; l = ld->next) {
					ld = &linkdat[l];
					tstxy = xy + ld->ofs * GRIDMAX;
					if (!ISSTAR(d->grid[tstxy]))
						cnt += place_vword(d, tstxy, ld->w);
					if (!opt_symmetrical) {
						if (cnt != 0)
							break;
					}
				}
				if (cnt == 0) {
					// Speed things up (Not 100% correct, but it's fast)
					*attr &= ~TODOV;
					grid[-GRIDMAX] = STAR;
					grid[+GRIDMAX] = STAR;
					if (opt_symmetrical) {
						if (ISCHAR(d->grid[GRIDMAX * GRIDMAX - 1 - (xy - GRIDMAX)])) return; // Arghh
						d->grid[GRIDMAX * GRIDMAX - 1 - (xy - GRIDMAX)] = STAR;
						if (ISCHAR(d->grid[GRIDMAX * GRIDMAX - 1 - (xy + GRIDMAX)])) return; // Arghh
						d->grid[GRIDMAX * GRIDMAX - 1 - (xy + GRIDMAX)] = STAR;
					}
				}
				hasplace += cnt;
			}
			if (hasplace)
				return;
		}

		// Update hotspot
		if (!hasfree)
			d->firstlevel = level + 1;
	}
}

void kick_ass(void) {
	struct node *d, *todonode;
	int i;

	for (;;) {
		// setup up some debugging statistics
		realnumnode = numnode = numscan = 0;
		hashtst = hashhit = 0;

		// gather all nodes into a single list with highest score first
		d = todonode = NULL;
		for (i = SCOREMAX - 1; i >= 0; i--)
			if (scores[i]) {
				if (todonode == NULL)
					d = todonode = scores[i];
				else
					d->next = scores[i];
				while (d->next) d = d->next;
				scores[i] = NULL;
			}

		// Ok babe, lets go!!!
		while (todonode) {
			d = todonode;
			todonode = d->next;
			if (d->numadj > 0 || numnode < opt_nodemax) {
				if (d->numadj == 0) numscan++;
				scan_grid(d);
			}
			d->next = freenode;
			freenode = d;
		}

		// Test for timeouts
		if (opt_timemax && ticks >= opt_timemax) {
			// PRINT OUT YOUR SOLUTION BEFORE YOU GO!
			/*
			 * HELP, the algorithm must compleet well under 10 minutes.
			 */
			dump_grid(&solution);
			exit(0);
		}

		if (opt_debug) {
			fprintf(stderr, "%s word:%2d score:%f level:%2d/%2d node:%4d/%4d/%4d hash:%3d/%3d\n",
				elapsedstr(), solution.numword, solution.score,
				solution.firstlevel, solution.lastlevel, numscan, numnode,
				realnumnode, hashtst, hashhit);
			if (opt_dump) dump_grid(&solution);
		}

		// Ass kicked?
		if (realnumnode == 0)
			break;
	}
}


void load_words(char *fname) {
	char line[80], c;
	FILE *f;
	int i, w, done;
	uint8_t *p;

	// Open file and load the words and check if they are valid
	if (fname) {
		f = fopen(fname, "r");
		if (!f) {
			fprintf(stderr, "Cannot open %s\n", fname);
			exit(1);
		}
	} else {
		f = stdin;
	}
	for (numword = 0;;) {
		// Read line
		fgets(line, sizeof(line), f);
		if (feof(f))
			break;
		// Copy the word
		i = 0;
		wordbase[numword][i++] = STAR;
		for (; isalpha(c = line[i - 1]); i++)
			wordbase[numword][i] = (uint8_t) (isupper(c) ? tolower(c) - BASE : c - BASE);
		wordbase[numword][i++] = STAR;
		wordbase[numword][i] = 0;
		wlen[numword] = i;
		if (i > WORDLENMAX - 2) {
			fprintf(stderr, "Word too long\n");
			exit(0);
		}
		if (i > 2) {
			if (numword == WORDMAX - 1) {
				fprintf(stderr, "Too many words\n");
				exit(0);
			} else
				numword++;
		}
	}
	fclose(f);

	if (opt_debug)
		fprintf(stderr, "%s Loaded %d words\n", elapsedstr(), numword);

	/*
	 * Calculate links. These are indexes on the wordlist which are used
	 * to quickly locate 1,2,3 long letter sequences within the words.
	 */
	numlinkdat = 1;
	memset(links1, 0, sizeof(links1));
	memset(links2, 0, sizeof(links2));
	memset(links3, 0, sizeof(links3));
	for (i = 0, done = 0; !done && i < WORDLENMAX; i++) {
		done = 1;
		for (w = numword - 1; w >= 0; w--) {
			p = wordbase[w];
			if (i >= 0 && i <= wlen[w] - 3) {
				// With delimiters
				linkdat[numlinkdat].w = w;
				linkdat[numlinkdat].ofs = -i;
				linkdat[numlinkdat].next = links3[p[i + 0]][p[i + 1]][p[i + 2]];
				links3[p[i + 0]][p[i + 1]][p[i + 2]] = numlinkdat++;
				done = 0;
			}
			if (i >= 0 && i <= wlen[w] - 2) {
				// With delimiters
				linkdat[numlinkdat].w = w;
				linkdat[numlinkdat].ofs = -i;
				linkdat[numlinkdat].next = links2[p[i + 0]][p[i + 1]];
				links2[p[i + 0]][p[i + 1]] = numlinkdat++;
				done = 0;
			}
			if (i > 0 && i <= wlen[w] - 2) {
				// Without delimiters
				linkdat[numlinkdat].w = w;
				linkdat[numlinkdat].ofs = -i;
				linkdat[numlinkdat].next = links1[p[i]];
				links1[p[i]] = numlinkdat++;
				done = 0;
			}
		}
	}

	if (opt_debug)
		fprintf(stderr, "%s Found %d links\n", elapsedstr(), numlinkdat);
}

void usage(char ** argv)
{
        fprintf(stderr,"usage: %s [<wordlist>]\n", argv[0]);
	fprintf(stderr,"\t-h\thelp\n");
	fprintf(stderr,"\t-s\tsymmetrical\n");
	fprintf(stderr,"\t-t N\tTIMEMAX (default %d)\n", opt_timemax);
	fprintf(stderr,"\t-n N\tNODEMAX (default %d)\n", opt_nodemax);
        exit(1);
}

int main(int argc, char **argv) {
	struct node *d;
	int x, y, i, w;
	int opt;

	if (opt_debug)
		fprintf(stderr, "sizeof(node)=%d\n", (int)sizeof(struct node));

	while ((opt = getopt(argc, argv, "hst:n:dD")) != -1) {
		switch (opt) {
		case 'h':
			usage(argv);
			break;
		case 's':
			opt_symmetrical = 1;
			break;
		case 't':
			opt_timemax = atoi(optarg);
			break;
		case 'n':
			opt_nodemax = atoi(optarg);
			break;
		case 'd':
			opt_debug++;
			break;
		case 'D':
			opt_dump++;
			break;
		default: /* '?' */
			usage(argv);
		}
	}

	// start the timer
	signal(SIGALRM, sigAlarm);
	alarm(1);

	// Load the word list
	load_words(argc<optind ? NULL : argv[optind++]);

	// init grid administration
	freenode = NULL;

	// Do some hotspot pre-calculations
	for (i = GRIDMAX * GRIDMAX - 1; i >= 0; i--)
		xy2level[i] = (i % GRIDMAX) + (i / GRIDMAX);
	for (i = 0; i < GRIDMAX * 2; i++) {
		if (i < GRIDMAX) {
			level2xy[i] = i + GRIDMAX - 1;
		} else {
			level2xy[i] = GRIDMAX * GRIDMAX - (GRIDMAX * 2 - 3 - i) * GRIDMAX - 2;
		}
	}

	// create an initial grid
	d = (struct node *) calloc(1, sizeof(struct node));
	for (y = GRIDMAX - 1; y >= 0; y--)
		for (x = GRIDMAX - 1; x >= 0; x--) {
			d->grid[x + y * GRIDMAX] = STAR;
			d->attr[x + y * GRIDMAX] = BORDER;
		}
	for (y = GRIDMAX - 2; y > 0; y--)
		for (x = GRIDMAX - 2; x > 0; x--) {
			d->grid[x + y * GRIDMAX] = FREE;
			d->attr[x + y * GRIDMAX] = 0;
		}

	// Place all the words for starters
	if (opt_symmetrical) {
		// work from the middle out
		for (w = numword - 1; w >= 0; w--)
			if (wlen[w] >= 5) {
				d->hash = 0;
				place_hword(d, (GRIDMAX / 2 + 2 - wlen[w]) + (GRIDMAX / 2) * GRIDMAX, w);
			}
	} else {
		// work from top-left to bottom-right
		for (w = numword - 1; w >= 0; w--) {
			d->firstlevel = 2;
			place_hword(d, 0 + 1 * GRIDMAX, w);
		}
	}

	d->next = freenode;
	freenode = d;

	// Here we go
	kick_ass();
	dump_grid(&solution);

	exit(0);
}
