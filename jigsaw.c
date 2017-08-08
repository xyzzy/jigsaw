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

/*
**  *** For your pleasure only ***
**
**  The code can produce symmetrical grids by setting the macro
**  SYMMETRICAL to 1. Sadly enough not as many words will fit
**  into a symmetrical grid than into a non-symmetrical grid,
**  so NODEMAX must be increased (say with 1000) to get better results.
*/

/* Configuring parameters */

#define TIMEMAX		(10*60-15)	/* 10 minute limit		*/
#define DEBUG		0		/* 0=Off 1=On 2=Verbose		*/
#define NODEMAX		15000		/* 500=Fast 1500=Normal		*/
#define SYMMETRICAL	0		/* 0=No 1=Yes			*/
#define MALLOC_BROKEN	1		/* 0=No 1=Yes (See below)	*/

/*
** It seems that malloc() under Linux is broken. I could only allocate
** a couple of thosand nodes before my program crashed. A solution was to
** allocate a BIG chunk of memory and split it up manually. This way about
** 10500 nodes could be allocated.
*/

/* Start of program */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/times.h>

#if defined(__alpha__)
#ifndef __GNUC__
#error I want GCC !!!
#endif
typedef          int int8   __attribute__ ((mode (QI)));
typedef unsigned int uint8  __attribute__ ((mode (QI)));
typedef          int int16  __attribute__ ((mode (HI)));
typedef unsigned int uint16 __attribute__ ((mode (HI)));
typedef          int int32  __attribute__ ((mode (SI)));
typedef unsigned int uint32 __attribute__ ((mode (SI)));
#else
/* It seems that your Sparc-GCC doesn't support the above :-(( */
typedef char int8;
typedef unsigned char uint8;
typedef short int16;
typedef unsigned short uint16;
typedef long int32;
typedef unsigned long uint32;
#endif

#define GRIDMAX		22		/* Size of grid incl. border	*/
#define LINKMAX		4096		/* # links			*/
#define WORDMAX		256		/* # words			*/
#define WORDLENMAX	32		/* # length of word		*/
#define ADJMAX		128		/* # unaccounted adjecent chars	*/
#define SCOREMAX	1000		/* Spead for hashing		*/

#define BASE		('a'-1)		/* Word conversion		*/
#define STAR		(28)		/* Word delimiters		*/
#define FREE		(31)		/* Unoccupied grid cell		*/
#define TODOH		1		/* Hint: Hor. word can be here	*/
#define TODOV		2		/* Hint: Ver. word can be here	*/
#define BORDER		4		/* Cell is grid border		*/

#define ISSTAR(C)	((C)==STAR)	/* Test the above		*/
#define ISFREE(C)	((C)==FREE)
#define ISCHAR(C)	((C)< STAR)
#define ISBORDER(C)	((C)&BORDER)

#define BITSET(M, B) ((M).s[(B)>>3] |=  (1<<((B)&7))) /* BitSet		*/
#define BITCLR(M, B) ((M).s[(B)>>3] &= ~(1<<((B)&7))) /*   manipulation	*/
#define INSET(M, B)  ((M).s[(B)>>3] &   (1<<((B)&7)))

typedef struct {			/* Bitset containing 256 bits	*/
	uint8 s[32];
} SET;

typedef struct node {
	struct node *next;		/*				*/
	int seqnr;			/* For diagnostics		*/
	SET words;			/* Summary of placed words	*/
	int numword, numchar, numconn;	/* Statistics			*/
	uint32 hash;			/* Duplicate detection		*/
	int firstlevel, lastlevel;	/* Grids hotspot		*/
	float score;			/* Will it survive?		*/
	int8 symdir;			/* Force symmetry		*/
	int16 symxy;			/* 				*/
	int16 symlen;			/* 				*/
	int numadj;			/* # unprocessed char pairs	*/
	int8 adjdir[ADJMAX];		/* Pair's direction		*/
	int16 adjxy[ADJMAX];		/* Pair's location		*/
	int16 adjl[ADJMAX];		/* Pair's wordlist		*/
	uint8 grid[GRIDMAX * GRIDMAX];	/* *THE* grid			*/
	uint8 attr[GRIDMAX * GRIDMAX];	/* Grid hints			*/
} node;

struct link {
	int16 next;
	int16 w;			/* What's the word	*/
	int16 ofs;			/* Were's in the word	*/
};


/* The external word list */
uint8 wordbase[WORDMAX][WORDLENMAX];	/* Converted wordlist	*/
int wlen[WORDMAX];			/* Length of words	*/
int numword;				/* How many		*/
char *wordfname = "wordlist";		/* Where are they found	*/

/* Where are 1,2,3 long character combinations */
int16 links1[32];			/* 1-char wordlist	*/
int16 links2[32][32];			/* 2-char wordlist	*/
int16 links3[32][32][32];		/* 3-char wordlist	*/
struct link linkdat[LINKMAX];		/* Body above wordlist	*/
int numlinkdat;				/* How many		*/

/* Hotspot pre-calculations */
int16 xy2level[GRIDMAX * GRIDMAX];	/* distance 0,0 to x,y	*/
int16 level2xy[GRIDMAX * 2];		/* inverse		*/

/* Node administration */
node *freenode;				/* Don't malloc() too much */
int numnode, realnumnode;		/* Statistics		*/
node solution;				/* What are we doing?	*/
node *scores[SCOREMAX];			/* Speed up hashing	*/

/* Diagnostics */
int seqnr;
int hashtst, hashhit;
int nummalloc;
int numscan;
int flg_dump;

/* What's left */
int clktck;				/* Timing portability	*/

/* Forward declartions */
char *elapsedstr(void);


/*
** Display the grid. Show disgnostics info in verbose mode
*/

void dump_grid(node *d) {
	int x, y, cnt, attr;

#if DEBUG == 2
	/* Double check the number of words */
	cnt = 0;
	for (x=GRIDMAX*GRIDMAX-1; x>=0; x--)
	  if (ISCHAR(d->grid[x])) {
	    if (!ISCHAR(d->grid[x-1]) && ISCHAR(d->grid[x+1])) cnt++;
	    if (!ISCHAR(d->grid[x-GRIDMAX]) && ISCHAR(d->grid[x+GRIDMAX])) cnt++;
	  }
	printf("%s seqnr:%d level:%d/%d score:%f numword:%d/%d numchar:%d numconn:%d\n",
	       elapsedstr(), d->seqnr, d->firstlevel, d->lastlevel, d->score, d->numword,
	       cnt, d->numchar, d->numconn);
#endif

#if DEBUG == 2
	/* Show grid as I would like to see it */
	for (y=0; y<GRIDMAX; y++) {
	  for (x=0; x<GRIDMAX; x++) {
	    attr = d->attr[x+y*GRIDMAX]&(TODOH|TODOV);
	    if (attr==(TODOH|TODOV))
	      printf("B");
	    else if (attr==TODOH)
	      printf("H");
	    else if (attr==TODOV)
	      printf("V");
	    else
	      printf(".");
	  }
	  printf (" ");
	  for (x=0; x<GRIDMAX; x++)
	    if (ISSTAR(d->grid[x+y*GRIDMAX]))
	      printf("*");
	    else if (ISFREE(d->grid[x+y*GRIDMAX]))
	      printf(".");
	    else if (ISCHAR(d->grid[x+y*GRIDMAX]))
	      printf ("%c", d->grid[x+y*GRIDMAX]+BASE);
	    else
	      printf("0x%02x", d->grid[x+y*GRIDMAX]);
#else
	/* Show grid as Fred would like to see it */
	for (y = 1; y < GRIDMAX - 1; y++) {
		for (x = 1; x < GRIDMAX - 1; x++)
			if (ISCHAR(d->grid[x + y * GRIDMAX]))
				printf("%c", d->grid[x + y * GRIDMAX] + BASE);
			else
				printf("-");
#endif
		printf("\n");
	}

	/* For diagnostics */
	fflush(stdout);
}

int elapsedtime(void) {
	int seconds;
	static struct tms TMS;

	times(&TMS);

	/* add the elapsed system and user times                        */
	/* calculation comes out to the nearest second - rounded down   */
	seconds = (TMS.tms_stime + TMS.tms_utime) / clktck;
	if (seconds < 1) seconds = 1;
	if (seconds >= TIMEMAX) {
		/* PRINT OUT YOUR SOLUTION BEFORE YOU GO! */
		/*
		** HELP, the algorithm must compleet well under 10 minutes.
		*/
		dump_grid(&solution);
		exit(0);
	}
	return seconds;
}

/*
** Elapsed time for diagnostics
*/
char *elapsedstr(void) {
	static char line[40];
	int seconds;

	seconds = elapsedtime();
	sprintf(line, "%02d:%02d", seconds / 60, seconds % 60);
	return line;
}


/*
** Get a node, reusing free'ed nodes.
*/

node *mallocnode(void) {
	node *d;

	d = freenode;
	if (d == NULL) {
		d = (node *) malloc(sizeof(node));
		nummalloc++;
	} else
		freenode = d->next;

	if (d == NULL) {
		fprintf(stderr, "Out of memory after %d nodes (%d bytes)\n", nummalloc, nummalloc * sizeof(node));
		dump_grid(&solution);
		exit(0);
	}

	return d;
}

void add_node(node *d) {
	int i;
	node **prev, *next;

	/* Evaluate grids score */
	d->score = (float) d->numconn / d->numchar;

	/* Insert grid into sorted list, eliminating duplicates */
	i = d->score * (SCOREMAX - 1);
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

	/* Test if entry is duplicate */
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

	/* Diagnostics */
	if (flg_dump > 1)
		dump_grid(d);

	/* Enter grid into list */
	d->seqnr = seqnr++;
	d->next = next;
	(*prev) = d;
	if (d->numadj == 0)
		numnode++;
	realnumnode++;
}


/*
** The next two routines test if a given word can be placed in the grid.
** If a new character will be adjecent to an existing character, check
** if the newly formed character pair exist in the wordlist (it doesn't
** matter where).
*/

int test_hword(node *d, int xybase, int word) {
	uint8 *p, *grid;
	int l;

	/* Some basic tests */
	if (xybase < 0 || xybase + wlen[word] >= GRIDMAX * GRIDMAX + 1) return 0;

#if SYMMETRICAL
	/* How about star's */
	if (ISCHAR(d->grid[GRIDMAX*GRIDMAX-1-xybase]))
	  return 0;
	if (ISCHAR(d->grid[GRIDMAX*GRIDMAX-1-(xybase+wlen[word]-1)]))
	  return 0;
#endif

	/* Will new characters create conflicts */
	for (grid = d->grid + xybase, p = wordbase[word]; *p; grid++, p++) {
		if (*grid == *p)
			continue; /* Char already there */
		if (!ISFREE(*grid))
			return 0; /* Char placement conflict */
		if (ISSTAR(*p))
			continue; /* Skip stars */
		if (!ISCHAR(grid[-GRIDMAX]) && !ISCHAR(grid[+GRIDMAX]))
			continue; /* No adjecent chars */

		if (ISFREE(grid[-GRIDMAX]))
			l = links2[*p][grid[+GRIDMAX]];
		else if (ISFREE(grid[+GRIDMAX]))
			l = links2[grid[-GRIDMAX]][*p];
		else
			l = links3[grid[-GRIDMAX]][*p][grid[+GRIDMAX]];
		if (l == 0)
			return 0;
	}

	/* Word can be placed */
	return 1;
}

int test_vword(node *d, int xybase, int word) {
	uint8 *p, *grid;
	int l;

	/* Some basic tests */
	if (xybase < 0 || xybase + wlen[word] * GRIDMAX >= GRIDMAX * GRIDMAX + GRIDMAX) return 0;

#if SYMMETRICAL
	/* How about star's */
	if (ISCHAR(d->grid[GRIDMAX*GRIDMAX-1-xybase]))
	  return 0;
	if (ISCHAR(d->grid[GRIDMAX*GRIDMAX-1-(xybase+wlen[word]*GRIDMAX-GRIDMAX)]))
	  return 0;
#endif

	/* Will new characters create conflicts */
	for (grid = d->grid + xybase, p = wordbase[word]; *p; grid += GRIDMAX, p++) {
		if (*grid == *p)
			continue; /* Char already there */
		if (!ISFREE(*grid))
			return 0; /* Char placement conflict */
		if (ISSTAR(*p))
			continue; /* Skip stars */
		if (!ISCHAR(grid[-1]) && !ISCHAR(grid[+1]))
			continue; /* No adjecent characters */

		if (ISFREE(grid[-1]))
			l = links2[*p][grid[+1]];
		else if (ISFREE(grid[+1]))
			l = links2[grid[-1]][*p];
		else
			l = links3[grid[-1]][*p][grid[+1]];
		if (l == 0)
			return 0;
	}

	/* Word can be placed */
	return 1;
}


/*
** The next two routines will place a given word in the grid. These routines
** also performs several sanity checks to make sure the new grid is worth
** it to continue with. If a newly placed character is adjecent to an
** existing character, then that pair must be part of a word that can
** be physically placed. If multiple character pairs exist, then no check
** is done to determine if those words (of which the pairs are part) can
** be adjecent. That is done later as these grids are not counted against
** NODEMAX.
*/

int place_hword(node *data, int xybase, int word) {
	node *d = data;
	uint8 *p, *grid, *attr;
	int i, l, xy;
	int newnumadj;
	struct link *ld;

	/* Can word be placed */
	if (INSET(d->words, word)) return 0;
	if (xybase < 0 || xybase + wlen[word] >= GRIDMAX * GRIDMAX + 1) return 0;

#if SYMMETRICAL
	/* How about star's */
	if (ISCHAR(d->grid[GRIDMAX*GRIDMAX-1-xybase]))
	  return 0;
	if (ISCHAR(d->grid[GRIDMAX*GRIDMAX-1-(xybase+wlen[word]-1)]))
	  return 0;
#endif

	/* check character environment */
	newnumadj = d->numadj;
	for (xy = xybase, grid = d->grid + xy, p = wordbase[word]; *p; grid++, xy++, p++) {
		if (*grid == *p)
			continue; /* Char already there */
		if (!ISFREE(*grid))
			return 0; /* Char placement conflict */
		if (ISSTAR(*p))
			continue; /* Skip stars */
		if (!ISCHAR(grid[-GRIDMAX]) && !ISCHAR(grid[+GRIDMAX]))
			continue; /* No adjecent chars */

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

	/* Test if new adj's really exist */
	for (i = d->numadj; i < newnumadj; i++) {
		for (l = d->adjl[i]; l; l = ld->next) {
			ld = &linkdat[l];
			if (test_vword(d, d->adjxy[i] + ld->ofs * GRIDMAX, ld->w))
				break;
		}
		if (l == 0) return 0;
		d->adjl[i] = l;
	}

	/* Get a new grid */
	d = mallocnode();
	memcpy(d, data, sizeof(node));

	/* Place word */
	BITSET(d->words, word);
	d->numword++;
	d->numadj = newnumadj;
	for (xy = xybase, grid = d->grid + xy, attr = d->attr + xy, p = wordbase[word];
	     *p;
	     grid++, attr++, xy++, p++) {
		if (!ISSTAR(*p)) {
			*attr &= ~TODOH;

			/* Remove character pair hints that are part of the new word */
			for (i = 0; i < d->numadj; i++)
				if (d->adjdir[i] == 'H' && d->adjxy[i] == xy) {
					d->adjdir[i] = d->adjdir[--d->numadj];
					d->adjxy[i] = d->adjxy[d->numadj];
					d->adjl[i] = d->adjl[d->numadj];
					break; /* There can be only one */
				}

			/* Place character */
			if (ISFREE(*grid)) {
				d->hash += (123456 + xy) * (123456 - *p); /* Neat, isn't it? */
				*attr |= TODOV;
				d->numchar++;
			} else {
				d->numconn++;
			}
		}
		*grid = *p;
	}

	/* Update hotspot */
	if (xy2level[xy - 1] > d->lastlevel)
		d->lastlevel = xy2level[xy - 1];

#if SYMMETRICAL
	/* Don't forget the symmetry */
	if (d->symdir == 0) {
	  d->symdir = 'H';
	  d->symxy  = GRIDMAX*GRIDMAX-1-(xybase+wlen[word]-1);
	  d->symlen = wlen[word];
	} else
	  d->symdir = 0;
#endif

	/* Save grid */
	add_node(d);
	return 1;
}

int place_vword(node *data, int xybase, int word) {
	node *d = data;
	uint8 *p, *grid, *attr;
	int i, xy, l;
	int newnumadj;
	struct link *ld;

	/* Some basic tests */
	if (INSET(d->words, word)) return 0;
	if (xybase < 0 || xybase + wlen[word] * GRIDMAX >= GRIDMAX * GRIDMAX + GRIDMAX) return 0;

#if SYMMETRICAL
	/* How about star's */
	if (ISCHAR(d->grid[GRIDMAX*GRIDMAX-1-xybase]))
	  return 0;
	if (ISCHAR(d->grid[GRIDMAX*GRIDMAX-1-(xybase+wlen[word]*GRIDMAX-GRIDMAX)]))
	  return 0;
#endif

	/* Check character environment */
	newnumadj = d->numadj;
	for (xy = xybase, grid = d->grid + xy, p = wordbase[word]; *p; grid += GRIDMAX, xy += GRIDMAX, p++) {
		if (*grid == *p)
			continue; /* Char already there */
		if (!ISFREE(*grid))
			return 0; /* Char placement conflict */
		if (ISSTAR(*p))
			continue; /* Skip stars */
		if (!ISCHAR(grid[-1]) && !ISCHAR(grid[+1]))
			continue; /* No adjecent chars */

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

	/* Test if new adj's really exist */
	for (i = d->numadj; i < newnumadj; i++) {
		for (l = d->adjl[i]; l; l = ld->next) {
			ld = &linkdat[l];
			if (test_hword(d, d->adjxy[i] + ld->ofs, ld->w))
				break;
		}
		if (l == 0) return 0;
		d->adjl[i] = l;
	}

	/* Get a new grid */
	d = mallocnode();
	memcpy(d, data, sizeof(node));

	/* Place word */
	BITSET(d->words, word);
	d->numword++;
	d->numadj = newnumadj;
	for (xy = xybase, grid = d->grid + xy, attr = d->attr + xy, p = wordbase[word];
	     *p;
	     grid += GRIDMAX, attr += GRIDMAX, xy += GRIDMAX, p++) {
		if (!ISSTAR(*p)) {
			*attr &= ~TODOV;

			/* Remove character pair hints that are part of the new word */
			for (i = 0; i < d->numadj; i++)
				if (d->adjdir[i] == 'V' && d->adjxy[i] == xy) {
					d->adjdir[i] = d->adjdir[--d->numadj];
					d->adjxy[i] = d->adjxy[d->numadj];
					d->adjl[i] = d->adjl[d->numadj];
					break; /* There can be only one */
				}

			/* Place character */
			if (ISFREE(*grid)) {
				*attr |= TODOH;
				d->hash += (123456 + xy) * (123456 - *p); /* Neat, isn't it? */
				d->numchar++;
			} else {
				d->numconn++;
			}
		}
		*grid = *p;
	}

	/* Update hotspot */
	if (xy2level[xy - GRIDMAX] > d->lastlevel)
		d->lastlevel = xy2level[xy - GRIDMAX];

#if SYMMETRICAL
	/* Don't forget the symmetry */
	if (d->symdir == 0) {
	  d->symdir = 'V';
	  d->symxy  = GRIDMAX*GRIDMAX-1-(xybase+wlen[word]*GRIDMAX-GRIDMAX);
	  d->symlen = wlen[word];
	} else
	  d->symdir = 0;
#endif

	/* Save grid */
	add_node(d);
	return 1;
}


/*
** Scan a grid and place a word. To supress an exponential growth of
** generated grids, we can be very fussy when chosing which word to
** place. I have chosen to fill the grid from top-left to bottom-right
** making sure the newly placed words fit tightly to the already placed
** words. If this is not possible, then I choose just one word such that
** the first letter is nearest to the start of the hotspot regeon.
** This sounds easy but it took me quite some time to figure it out.
*/

void scan_grid(node *d) {
	uint8 *grid, *attr;
	int xy, l, cnt, tstxy, level, w;
	struct link *ld;
	int hasplace, hasfree;

#if SYMMETRICAL
	/* Don't forget the symmetry */
	if (d->symdir == 'H') {
	  for (w=numword-1; w>=0; w--)
	    if (!INSET(d->words, w) && wlen[w] == d->symlen)
	      place_hword (d, d->symxy, w);
	  return;
	}
	if (d->symdir == 'V') {
	  for (w=numword-1; w>=0; w--)
	    if (!INSET(d->words, w) && wlen[w] == d->symlen)
	      place_vword (d, d->symxy, w);
	  return;
	}
#endif

	/* locate unaccounted adjecent cells */
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

	/* Nominate grid for final result */
	if (d->numword > solution.numword)
		memcpy(&solution, d, sizeof(node));

	/* Sweep grid from top-left to bottom-right corner */
	for (level = d->firstlevel; level <= d->lastlevel && level <= GRIDMAX * 2 - 4; level++) {

#if !SYMMETRICAL
		/* Locate 'tight' words */
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
#endif

		/* Locate 'adjecent' word */
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
#if SYMMETRICAL
					if (ISSTAR(d->grid[GRIDMAX*GRIDMAX-1-(tstxy+wlen[ld->w]-1)]))
					  hasplace += place_hword (d, tstxy, ld->w);
#endif
				}
			}
			if (*attr & TODOV) {
				for (l = links1[*grid]; l; l = ld->next) {
					ld = &linkdat[l];
					tstxy = xy + ld->ofs * GRIDMAX;
					if (ISSTAR(d->grid[tstxy]))
						hasplace += place_vword(d, tstxy, ld->w);
#if SYMMETRICAL
					if (ISSTAR(d->grid[GRIDMAX*GRIDMAX-1-(tstxy+wlen[ld->w]*GRIDMAX-GRIDMAX)]))
					  hasplace += place_vword (d, tstxy, ld->w);
#endif
				}
			}
			if (hasplace)
				return;
		}

		/* Locate word fragments (just one word please) */
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
#if !SYMMETRICAL
					if (cnt != 0)
						break;
#endif
				}
				if (cnt == 0) {
					/* Speed things up (Not 100% correct, but it's fast) */
					*attr &= ~TODOH;
					grid[-1] = STAR;
					grid[+1] = STAR;
#if SYMMETRICAL
					if (ISCHAR(d->grid[GRIDMAX*GRIDMAX-1-(xy-1)])) return; /* Arghh */
					d->grid[GRIDMAX*GRIDMAX-1-(xy-1)] = STAR;
					if (ISCHAR(d->grid[GRIDMAX*GRIDMAX-1-(xy+1)])) return; /* Arghh */
					d->grid[GRIDMAX*GRIDMAX-1-(xy+1)] = STAR;
#endif
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
#if !SYMMETRICAL
					if (cnt != 0)
						break;
#endif
				}
				if (cnt == 0) {
					/* Speed things up (Not 100% correct, but it's fast) */
					*attr &= ~TODOV;
					grid[-GRIDMAX] = STAR;
					grid[+GRIDMAX] = STAR;
#if SYMMETRICAL
					if (ISCHAR(d->grid[GRIDMAX*GRIDMAX-1-(xy-GRIDMAX)])) return; /* Arghh */
					d->grid[GRIDMAX*GRIDMAX-1-(xy-GRIDMAX)] = STAR;
					if (ISCHAR(d->grid[GRIDMAX*GRIDMAX-1-(xy+GRIDMAX)])) return; /* Arghh */
					d->grid[GRIDMAX*GRIDMAX-1-(xy+GRIDMAX)] = STAR;
#endif
				}
				hasplace += cnt;
			}
			if (hasplace)
				return;
		}

		/* Update hotspot */
		if (!hasfree)
			d->firstlevel = level + 1;
	}
}

void kick_ass(void) {
	node *d, *todonode;
	int i;

	for (;;) {
		/* setup up some debugging statistics */
		realnumnode = numnode = numscan = 0;
		hashtst = hashhit = 0;

		/* gather all nodes into a single list with highest score first */
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

		/* Ok babe, lets go!!! */
		while (todonode) {
			d = todonode;
			todonode = d->next;
			if (d->numadj > 0 || numnode < NODEMAX) {
				if (d->numadj == 0) numscan++;
				scan_grid(d);
			}
			d->next = freenode;
			freenode = d;
		}

		/* Test for timeouts */
		elapsedtime();

#if DEBUG
		fprintf(stderr, "%s word:%2d score:%f level:%2d/%2d node:%4d/%4d/%4d hash:%3d/%3d\n",
			elapsedstr(), solution.numword, solution.score,
			solution.firstlevel, solution.lastlevel, numscan, numnode,
			realnumnode, hashtst, hashhit);
		if (flg_dump) dump_grid (&solution);
#endif

		/* Ass kicked? */
		if (realnumnode == 0)
			break;
	}
}


void load_words(void) {
	char line[80], c;
	FILE *f;
	int i, w, done;
	uint8 *p;

	/* Open file and load the words and check if they are valid */
	f = fopen(wordfname, "r");
	if (!f) {
		fprintf(stderr, "Cannot open %s\n", wordfname);
		exit(1);
	}
	for (numword = 0;;) {
		/* Read line */
		fgets(line, sizeof(line), f);
		if (feof(f))
			break;
		/* Copy the word */
		i = 0;
		wordbase[numword][i++] = STAR;
		for (; isalpha(c = line[i - 1]); i++)
			wordbase[numword][i] = isupper(c) ? tolower(c) - BASE : c - BASE;
		wordbase[numword][i++] = STAR;
		wordbase[numword][i] = 0;
		wlen[numword] = i;
		if (i > WORDLENMAX - 2) {
			fprintf(stderr, "Word too long\n");
			exit(0);
		}
		if (i > 2)
			if (numword == WORDMAX - 1) {
				fprintf(stderr, "Too many words\n");
				exit(0);
			} else
				numword++;
	}
	fclose(f);

#if DEBUG
	fprintf(stderr, "%s Loaded %d words\n", elapsedstr(), numword);
#endif

	/*
	** Calculate links. These are indexes on the wordlist which are used
	** to quickly locate 1,2,3 long letter sequences within the words.
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
				/* With delimiters */
				linkdat[numlinkdat].w = w;
				linkdat[numlinkdat].ofs = -i;
				linkdat[numlinkdat].next = links3[p[i + 0]][p[i + 1]][p[i + 2]];
				links3[p[i + 0]][p[i + 1]][p[i + 2]] = numlinkdat++;
				done = 0;
			}
			if (i >= 0 && i <= wlen[w] - 2) {
				/* With delimiters */
				linkdat[numlinkdat].w = w;
				linkdat[numlinkdat].ofs = -i;
				linkdat[numlinkdat].next = links2[p[i + 0]][p[i + 1]];
				links2[p[i + 0]][p[i + 1]] = numlinkdat++;
				done = 0;
			}
			if (i > 0 && i <= wlen[w] - 2) {
				/* Without delimiters */
				linkdat[numlinkdat].w = w;
				linkdat[numlinkdat].ofs = -i;
				linkdat[numlinkdat].next = links1[p[i]];
				links1[p[i]] = numlinkdat++;
				done = 0;
			}
		}
	}

#if DEBUG
	fprintf(stderr, "%s Found %d links\n", elapsedstr(), numlinkdat);
#endif
}

int main(int argc, char **argv) {
	node *d;
	int x, y, i, w;

#if DEBUG
	fprintf(stderr, "sizeof(node)=%d\n", sizeof(node));
#endif

	/* simple arguments */
	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-')
			wordfname = argv[i];
		else {
			switch (argv[i][1]) {
			case 'd':
				flg_dump++; /* Diagnostics */
				break;
			}
		}
	}

	/* In case we run out of memory, sigh */
	fflush(stderr);
	fflush(stdout);

	/* Load the database */
	clktck = sysconf(_SC_CLK_TCK);
	load_words();

	/* init grid administration */
	freenode = NULL;
#if MALLOC_BROKEN
	nummalloc = 6000;
	d = malloc(nummalloc * sizeof(node));
	if (d == NULL)
		nummalloc = 0;
	else
		for (i = 0; i < nummalloc; i++) {
			d[i].next = freenode;
			freenode = d + i;
		}
#endif

	/* Do some hotspot pre-calculations */
	for (i = GRIDMAX * GRIDMAX - 1; i >= 0; i--)
		xy2level[i] = (i % GRIDMAX) + (i / GRIDMAX);
	for (i = 0; i < GRIDMAX * 2; i++) {
		if (i < GRIDMAX) {
			level2xy[i] = i + GRIDMAX - 1;
		} else {
			level2xy[i] = GRIDMAX * GRIDMAX - (GRIDMAX * 2 - 3 - i) * GRIDMAX - 2;
		}
	}

	/* create an initial grid */
	d = (node *) calloc(1, sizeof(node));
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

	/* Place all the words for starters */
#if SYMMETRICAL
	/* work from the middle out */
	for (w=numword-1; w>=0; w--)
	  if (wlen[w]>=5) {
	    d->hash = 0;
	    place_hword (d, (GRIDMAX/2+2-wlen[w])+(GRIDMAX/2)*GRIDMAX, w);
	  }
#else
	/* work from top-left to bottom-right */
	for (w = numword - 1; w >= 0; w--) {
		d->firstlevel = 2;
		place_hword(d, 0 + 1 * GRIDMAX, w);
	}
#endif
	d->next = freenode;
	freenode = d;

	/* Here we go */
	kick_ass();
	dump_grid(&solution);

	exit(0);
}
