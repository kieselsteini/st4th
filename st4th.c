/*============================================================================*/
/*==[ st4th - a minimalistic Forth interpreter/compiler ]=====================*/
/*==[ written by Sebastian Steinhauer <s.steinhauer@yahoo.de> ]===============*/
/*============================================================================*/
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>


/*============================================================================*/
/*==[ configuration ]=========================================================*/
/*============================================================================*/
#define MEMORY_SIZE			(1024 * 64)
#define DATA_STACK_SIZE		16
#define RETURN_STACK_SIZE	64
#define WORD_NAME_SIZE		32
#define PARSE_LINE_SIZE		128

typedef intptr_t			CELL;
typedef uint8_t				BYTE;
typedef char				CHAR;

#define	FL_IMMEDIATE		1
#define FL_HIDDEN			2

typedef struct WORD {
	CHAR					name[WORD_NAME_SIZE];
	void					(*func)();
	CELL					value;
	BYTE					flags;
	struct WORD				*prev;
} WORD;


/*============================================================================*/
/*==[ global state ]==========================================================*/
/*============================================================================*/
BYTE	*m0, *mp;			/* memory */
CELL	*s0, *sp;			/* data stack */
CELL	*r0, *rp;			/* return stack */
WORD	*w0, *wp;			/* dictionary / word pointer */
WORD	**ip;				/* instruction pointer */
CHAR	*cp;				/* character input */
CELL	mode;				/* interpreter / compiler mode */
CELL	showstack;			/* debugging mode...show stack between each word */


/*============================================================================*/
/*==[ helper functions ]======================================================*/
/*============================================================================*/
#define push(x)		(*sp++ = (x))
#define pop() 		(*--sp)
#define pushr(x)	(*rp++ = (x))
#define popr()		(*--rp)
#define pushf(x)	((x) ? push(~0) : push(0))

static BYTE *allot(const CELL length) {
	BYTE *ptr = mp;
	mp += length;
	return ptr;
}

static CHAR *refill(FILE *fp) {
	static CHAR buffer[PARSE_LINE_SIZE];
	return fgets(buffer, sizeof(buffer), fp);
}

static CHAR *parse() {
	static CHAR buffer[WORD_NAME_SIZE];
	unsigned int i;
	if (!cp) return NULL;
	while (*cp && isspace(*cp)) ++cp;
	if (!*cp) return NULL;
	for (i = 0; i < (sizeof(buffer) - 1) && *cp && !isspace(*cp); ++i, ++cp)
		buffer[i] = toupper(*cp);
	buffer[i] = 0;
	return buffer;
}

static CHAR *parseraw(CHAR delim) {
	static CHAR buffer[PARSE_LINE_SIZE];
	unsigned int i;
	if (!cp) return NULL;
	for (i = 0; i < (sizeof(buffer) - 1) && *cp && *cp != delim; ++i, ++cp)
		buffer[i] = *cp;
	buffer[i] = 0;
	if (*cp == delim) ++cp;
	return buffer;
}

static WORD *findword(const CHAR *name) {
	WORD *w = w0;
	for (; w; w = w->prev)
		if (!(w->flags & FL_HIDDEN) && !strcmp(name, w->name))
			return w;
	return NULL;
}

static void makeword(const char *name) {
	if (!name) { puts("no name for word!"); exit(-1); }
	wp = (WORD*)allot(sizeof(WORD));
	memset(wp, 0, sizeof(WORD));
	strncpy(wp->name, name, WORD_NAME_SIZE - 1);
	wp->prev = w0;
	w0 = wp;
}

static void comma(const CELL value) {
	CELL *ptr = (CELL*)allot(sizeof(CELL));
	*ptr = value;
}

static void compile(const char *name) {
	wp = findword(name);
	if (!wp) { printf("COMPILE: %s?\n", name); exit(-1); }
	comma((CELL)wp);
}

static void compilestring(const char *str) {
	CELL *laddr, *baddr;
	if (!str) { puts("no string to compile"); exit(-1); }
	compile("DOLITERAL"); laddr = (CELL*)mp; comma(0);
	compile("BRANCH"); baddr = (CELL*)mp; comma(0);
	*laddr = (CELL)mp;
	strcpy((CHAR*)allot(strlen(str) + 1), str);
	*baddr = (CELL)mp;
}

static void dumpstack(int depth) {
	int i;
	CELL *p;
	for (i = 0, p = sp - 1; p >= s0 && i < depth; --p, ++i) printf("[%02d] %ld\n", i, *p);
}

static void evaluate(CHAR *input) {
	CHAR *old_cp, *token;
	CELL value;

	old_cp = cp; cp = input; mode = 0;
	while (1) {
		token = parse();
		if (!token) {
			puts("ok");
			goto finish;
		}
		wp = findword(token);
		if (showstack) printf("-> %s\n", token);
		if (wp) {
			if (mode == 0 || wp->flags & FL_IMMEDIATE) wp->func();
			else comma((CELL)wp);
		} else {
			if (sscanf(token, "%ld", &value) == 1) {
				if (mode == 0) push(value);
				else { compile("DOLITERAL"); comma(value); }
			} else {
				printf("%s?\n", token);
				goto finish;
			}
		}
		if (showstack) dumpstack(4);
	}
finish:
	cp = old_cp;
	if (sp < s0) puts("stack underflow");
	else if (sp > s0 + DATA_STACK_SIZE) puts("stack overflow");
}

static void makedictionary(WORD *w) {
	w0 = wp = NULL;
	for (; w->func; ++w) {
		w->prev = w0;
		w0 = w;
	}
}


/*============================================================================*/
/*==[ basic forth words ]=====================================================*/
/*============================================================================*/
static void fDOCOLON() {
	static int execute = 0;
	if (execute) {
		pushr((CELL)ip);
		ip = (WORD**)wp->value;
	} else {
		/* inner interpreter loop */
		execute = 1;
		pushr(0);
		ip = (WORD**)wp->value;
		while (ip) {
			wp = *ip++;
			wp->func();
		}
		execute = 0;
	}
}

static void fDOCONSTANT() { push(wp->value); }
static void fDOVARIABLE() { push((CELL)&wp->value); }
static void fDOLITERAL() { push((CELL)*ip++); }
static void fEXIT() { ip = (WORD**)popr(); }
static void fBRANCH() { ip = (WORD**)*ip; }

static void fCOLON() { makeword(parse()); wp->func = fDOCOLON; wp->flags = FL_HIDDEN; wp->value = (CELL)mp; mode = 1; }
static void fSEMICOLON() { compile("EXIT"); w0->flags &= ~FL_HIDDEN; mode = 0; }
static void fCONSTANT() { makeword(parse()); wp->func = fDOCONSTANT; wp->value = pop(); }
static void fVARIABLE() { makeword(parse()); wp->func = fDOVARIABLE; }
static void fCREATE() { makeword(parse()); wp->func = fDOCONSTANT; wp->value = (CELL)mp; }
static void fNONAME() { wp = (WORD*)allot(sizeof(WORD)); wp->func = fDOCOLON; wp->value = (CELL)mp; mode = 1; push((CELL)wp); }
static void fIMMEDIATE() { w0->flags |= FL_IMMEDIATE; }
static void fRECURSE() { w0->flags &= ~FL_HIDDEN; }

static void fDROP() { (void)pop(); }
static void fDUP() { CELL x = pop(); push(x); push(x); }
static void f_DUP() { CELL x = pop(); if (x) { push(x); push(x); } else push(0); }
static void fSWAP() { CELL b = pop(), a = pop(); push(b); push(a); }
static void fOVER() { CELL b = pop(), a = pop(); push(a); push(b); push(a); }
static void fROT() { CELL c = pop(), b = pop(), a = pop(); push(b); push(c); push(a); }
static void fDEPTH() { CELL x = sp - s0; push(x); }
static void fCLEAR() { sp = s0; }

static void f2R() { pushr(pop()); }
static void fR2() { push(popr()); }
static void f_R() { CELL x = popr(); pushr(x); push(x); }

static void fADD() { CELL b = pop(), a = pop(); push(a + b); }
static void fSUB() { CELL b = pop(), a = pop(); push(a - b); }
static void fMUL() { CELL b = pop(), a = pop(); push(a * b); }
static void fDIV() { CELL b = pop(), a = pop(); push(a / b); }
static void fMOD() { CELL b = pop(), a = pop(); push(a % b); }
static void fNEGATE() { CELL x = pop(); push(-x); }
static void fABS() { CELL x = pop(); push(abs(x)); }
static void fMAX() { CELL b = pop(), a = pop(); push(a > b ? a : b); }
static void fMIN() { CELL b = pop(), a = pop(); push(a < b ? a : b); }

static void fAND() { CELL b = pop(), a = pop(); push(a & b); }
static void fOR () { CELL b = pop(), a = pop(); push(a | b); }
static void fXOR() { CELL b = pop(), a = pop(); push(a ^ b); }
static void fLSHIFT() { CELL b = pop(), a = pop(); push(a << b); }
static void fRSHIFT() { CELL b = pop(), a = pop(); push(a >> b); }
static void fINVERT() { CELL x = pop(); push(~x); }

static void fEQ() { CELL b = pop(), a = pop(); pushf(a == b); }
static void fNE() { CELL b = pop(), a = pop(); pushf(a != b); }
static void fLT() { CELL b = pop(), a = pop(); pushf(a <  b); }
static void fLE() { CELL b = pop(), a = pop(); pushf(a <= b); }
static void fGT() { CELL b = pop(), a = pop(); pushf(a >  b); }
static void fGE() { CELL b = pop(), a = pop(); pushf(a >= b); }
static void fE0() { CELL a = pop(); pushf(a == 0); }

static void fPEEK() { CELL *p = (CELL*)pop(); push(*p); }
static void fPOKE() { CELL *p = (CELL*)pop(), x = pop(); *p = x; }
static void fCPEEK() { BYTE *p = (BYTE*)pop(); push(*p); }
static void fCPOKE() { BYTE *p = (BYTE*)pop(), x = (BYTE)pop(); *p = x; }
static void fAPOKE() { CELL *p = (CELL*)pop(), x = pop(); *p += x; }
static void fHERE() { push((CELL)mp); }
static void fALLOT() { (void)allot(pop()); }
static void fCOMMA() { comma(pop()); }
static void fCHARS() { CELL x = pop(); push(x * sizeof(CHAR)); }
static void fCELLS() { CELL x = pop(); push(x * sizeof(CELL)); }
static void fCHAR_() { CELL x = pop(); push(x + sizeof(CHAR)); }
static void fCELL_() { CELL x = pop(); push(x + sizeof(CELL)); }

static void fFILL() { CHAR c = (CHAR)pop(); CELL u = pop(); CHAR *addr = (CHAR*)pop(); memset(addr, c, u); }
static void fERASE() { CELL u = pop(); CHAR *addr = (CHAR*)pop(); memset(addr, 0, u); }
static void fCOUNT() { CHAR *s = (CHAR*)pop(); push((CELL)s); push(strlen(s)); }
static void fTYPE() { printf("%s", (CHAR*)pop()); }
static void fCSTRING() { CHAR *s = parseraw('"'); compilestring(s); }
static void fPSTRING() { CHAR *s = parseraw('"'); if (mode) { compilestring(s); compile("TYPE"); } else printf("%s", s); }
static void fCOMMENT() { (void)parseraw(')'); }
static void fLCOMMENT() { (void)parseraw('\n'); }
static void fCCOMMENT() { CHAR *s = parseraw(')'); if (mode) printf("%s", s); }

static void fWORD() { push((CELL)parse()); }
static void fPARSE() { CELL delim = pop(); push((CELL)parseraw(delim)); }
static void fFIND() { CHAR *s = (CHAR*)pop(); push((CELL)findword(s)); }
static void fEVALUATE() { evaluate((CHAR*)pop()); }
static void fEXECUTE() { wp = (WORD*)pop(); wp->func(); }
static void fLBRACKET() { mode = 0; }
static void fRBRACKET() { mode = 1; }
static void fCHAR() { CHAR *c = parse(); push(*c); }
static void f_CHAR_() { CHAR *c = parse(); compile("DOLITERAL"); comma(*c); }

static void fDOT() { printf("%ld ", pop()); }
static void fEMIT() { putchar(pop()); }
static void fSPACE() { putchar(' '); }
static void fSPACES() { CELL n = pop(), i; for (i = 0; i < n; ++i) putchar(' '); }
static void fCR() { puts(""); }
static void fKEY() { push(getchar()); }

static void fWORDS() {
	int i;
	for (wp = w0, i = 0; wp; wp = wp->prev)
		if (!(wp->flags & FL_HIDDEN)) { printf("%s ", wp->name); ++i; }
	printf("(%d total)\n", i);
}
static void f_S() { dumpstack(DATA_STACK_SIZE); }
static void fSHOWSTACK() { showstack = !showstack; }


/*============================================================================*/
/*==[ dictionary definition ]=================================================*/
/*============================================================================*/
static WORD dictionary[] = {
	{ "DOLITERAL", fDOLITERAL, 0, 0, NULL },
	{ "EXIT", fEXIT, 0, 0, NULL },
	{ "BRANCH", fBRANCH, 0, 0, NULL },
	{ ":", fCOLON, 0, 0, NULL },
	{ ";", fSEMICOLON, 0, FL_IMMEDIATE, NULL },
	{ "CONSTANT", fCONSTANT, 0, 0, NULL },
	{ "VARIABLE", fVARIABLE, 0, 0, NULL },
	{ "CREATE", fCREATE, 0, 0, NULL },
	{ ":NONAME", fNONAME, 0, 0, NULL },
	{ "IMMEDIATE", fIMMEDIATE, 0, 0, NULL },
	{ "RECURSE", fRECURSE, 0, 0, NULL },
	{ "DROP", fDROP, 0, 0, NULL },
	{ "DUP", fDUP, 0, 0, NULL },
	{ "?DUP", f_DUP, 0, 0, NULL },
	{ "SWAP", fSWAP, 0, 0, NULL },
	{ "OVER", fOVER, 0, 0, NULL },
	{ "ROT", fROT, 0, 0, NULL },
	{ "DEPTH", fDEPTH, 0, 0, NULL },
	{ "CLEAR", fCLEAR, 0, 0, NULL },
	{ ">R", f2R, 0, 0, NULL },
	{ "R>", fR2, 0, 0, NULL },
	{ "@R", f_R, 0, 0, NULL },
	{ "+", fADD, 0, 0, NULL },
	{ "-", fSUB, 0, 0, NULL },
	{ "*", fMUL, 0, 0, NULL },
	{ "/", fDIV, 0, 0, NULL },
	{ "MOD", fMOD, 0, 0, NULL },
	{ "NEGATE", fNEGATE, 0, 0, NULL },
	{ "ABS", fABS, 0, 0, NULL },
	{ "MAX", fMAX, 0, 0, NULL },
	{ "MIN", fMIN, 0, 0, NULL },
	{ "AND", fAND, 0, 0, NULL },
	{ "OR", fOR, 0, 0, NULL },
	{ "XOR", fXOR, 0, 0, NULL },
	{ "<<", fLSHIFT, 0, 0, NULL },
	{ ">>", fRSHIFT, 0, 0, NULL },
	{ "INVERT", fINVERT, 0, 0, NULL },
	{ "=", fEQ, 0, 0, NULL },
	{ "<>", fNE, 0, 0, NULL },
	{ "<", fLT, 0, 0, NULL },
	{ "<=", fLE, 0, 0, NULL },
	{ ">", fGT, 0, 0, NULL },
	{ ">=", fGE, 0, 0, NULL },
	{ "0=", fE0, 0, 0, NULL },
	{ "@", fPEEK, 0, 0, NULL },
	{ "!", fPOKE, 0, 0, NULL },
	{ "C@", fCPEEK, 0, 0, NULL },
	{ "C!", fCPOKE, 0, 0, NULL },
	{ "+!", fAPOKE, 0, 0, NULL },
	{ "HERE", fHERE, 0, 0, NULL },
	{ "ALLOT", fALLOT, 0, 0, NULL },
	{ ",", fCOMMA, 0, 0, NULL },
	{ "CHARS", fCHARS, 0, 0, NULL },
	{ "CELLS", fCELLS, 0, 0, NULL },
	{ "CHAR+", fCHAR_, 0, 0, NULL },
	{ "CELL+", fCELL_, 0, 0, NULL },
	{ "FILL", fFILL, 0, 0, NULL },
	{ "ERASE", fERASE, 0, 0, NULL },
	{ "COUNT", fCOUNT, 0, 0, NULL },
	{ "TYPE", fTYPE, 0, 0, NULL },
	{ ".\"", fPSTRING, 0, FL_IMMEDIATE, NULL },
	{ "C\"", fCSTRING, 0, FL_IMMEDIATE, NULL },
	{ "(", fCOMMENT, 0, FL_IMMEDIATE, NULL },
	{ "\\", fLCOMMENT, 0, FL_IMMEDIATE, NULL },
	{ ".(", fCCOMMENT, 0, FL_IMMEDIATE, NULL },
	{ "WORD", fWORD, 0, 0, NULL },
	{ "PARSE", fPARSE, 0, 0, NULL },
	{ "FIND", fFIND, 0, 0, NULL },
	{ "EVALUATE", fEVALUATE, 0, 0, NULL },
	{ "EXECUTE", fEXECUTE, 0, 0, NULL },
	{ "[", fLBRACKET, 0, FL_IMMEDIATE, NULL },
	{ "]", fRBRACKET, 0, FL_IMMEDIATE, NULL },
	{ "CHAR", fCHAR, 0, 0, NULL },
	{ "[CHAR]", f_CHAR_, 0, FL_IMMEDIATE, NULL },
	{ ".", fDOT, 0, 0, NULL },
	{ "EMIT", fEMIT, 0, 0, NULL },
	{ "SPACE", fSPACE, 0, 0, NULL },
	{ "SPACES", fSPACES, 0, 0, NULL },
	{ "CR", fCR, 0, 0, NULL },
	{ "KEY", fKEY, 0, 0, NULL },
	{ "WORDS", fWORDS, 0, 0, NULL },
	{ ".S", f_S, 0, 0, NULL },
	{ "SHOWSTACK", fSHOWSTACK, 0, 0, NULL },
	/* some constants */
	{ "FALSE", fDOCONSTANT, 0, 0, NULL },
	{ "TRUE", fDOCONSTANT, ~0, 0, NULL },
	{ "BL", fDOCONSTANT, ' ', 0, NULL },
	{ "MODE", fDOCONSTANT, (CELL)&mode, 0, NULL },
	{ "0", fDOCONSTANT, 0, 0, NULL },
	{ "1", fDOCONSTANT, 1, 0, NULL },
	{ "-1", fDOCONSTANT, -1, 0, NULL },
	{ "", NULL, 0, 0, NULL }
};

/*============================================================================*/
/*==[ main ]==================================================================*/
/*============================================================================*/
int main(int argc, char **argv) {
	(void)argc; (void)argv;
	m0 = mp = (BYTE*)malloc(MEMORY_SIZE);
	s0 = sp = (CELL*)allot(sizeof(CELL) * DATA_STACK_SIZE);
	r0 = rp = (CELL*)allot(sizeof(CELL) * RETURN_STACK_SIZE);
	showstack = 0;
	makedictionary(dictionary);
	puts("welcome to st4th");
	while (1) {
		CHAR *line = refill(stdin);
		if (!line) break;
		evaluate(line);
	}
	free(m0);
	return 0;
}

/*============================================================================*/
/*==[ license ]===============================================================*/
/*============================================================================*/
/*
	This is free and unencumbered software released into the public domain.

	Anyone is free to copy, modify, publish, use, compile, sell, or
	distribute this software, either in source code form or as a compiled
	binary, for any purpose, commercial or non-commercial, and by any
	means.

	In jurisdictions that recognize copyright laws, the author or authors
	of this software dedicate any and all copyright interest in the
	software to the public domain. We make this dedication for the benefit
	of the public at large and to the detriment of our heirs and
	successors. We intend this dedication to be an overt act of
	relinquishment in perpetuity of all present and future rights to this
	software under copyright law.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
	IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
	OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
	ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
	OTHER DEALINGS IN THE SOFTWARE.

	For more information, please refer to <http://unlicense.org/>
*/

