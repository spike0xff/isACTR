#include "lisp.h"
#include <stdlib.h>
#include <malloc.h>
#include <string.h>

#define MAX_CELLS 32768
#define MAX_SYMBOLS 32768
#define MAX_STRING_POOL 1000000
#define MAX_NUMBERS 32768
#define MAX_SUBRS 2000

// strings are stored as pointers to their (wchar_t*) text

typedef struct {
	LISPTR	car, cdr;
} CELL;

typedef struct {
	LISPTR	name;				// string name
	LISPTR	valueCell;			// value cell
	LISPTR	fnCell;				// function cell
} SYMBOL;

typedef struct {
	int			nargs;			// number of parameters. -1=FSUBR
	union {
		NATIVE1ARGS	fsubr;
		NATIVE0ARGS	subr0;
		NATIVE1ARGS	subr1;
		NATIVE2ARGS	subr2;
		NATIVE3ARGS	subr3;
	};
} SUBR;

LISPTR subr_atomp(LISPTR x)
{
	return atomp(x) ? T : NIL;
}

LISPTR subr_numberp(LISPTR x)
{
	return numberp(x) ? T : NIL;
}

LISPTR subr_eq(LISPTR x, LISPTR y)
{
	return (x==y) ? T : NIL;
}


static CELL cellBlock[MAX_CELLS];
static int cellCount;
static SYMBOL symPool[MAX_SYMBOLS];
static int symCount;
static wchar_t stringPool[MAX_STRING_POOL];
static int stringCount;
static double numberPool[MAX_NUMBERS];
static int numberCount;
static SUBR subrPool[MAX_SUBRS];
static int subrCount;

const LISPTR NIL = &symPool[0];
const LISPTR T = &symPool[1];
const LISPTR QUOTE = &symPool[2];
const LISPTR FUNCTION = &symPool[3];
const LISPTR LAMBDA = &symPool[4];

void lisp_init(void)
{
	cellCount = 0;
	symCount = 0;
	stringCount = 0;
	numberCount = 0;
	subrCount = 0;
	intern(L"NIL");
	intern(L"T");
	intern(L"QUOTE");
	intern(L"FUNCTION");
	intern(L"LAMBDA");
	defvar(T, T);
	def_fsubr(L"QUOTE", car);
	def_subr1(L"CAR", car);
	def_subr1(L"CDR", cdr);
	def_subr2(L"CONS", cons);
	def_subr1(L"ATOMP", subr_atomp);
	def_subr1(L"NUMBERP", subr_numberp);
	def_subr2(L"EQ", subr_eq);
	def_subr2(L"ASSOC", assoc);
}

void lisp_shutdown(void)
{
}

void lisp_error(const wchar_t* msg)
{
	fwprintf(stdout, L"**LISP ERROR: %s\n", msg);
}

LISPTR cons(LISPTR x, LISPTR y)
{
	CELL* c = &cellBlock[cellCount++];
	c->car = x;
	c->cdr = y;
	return (LISPTR)c;
}

bool consp(LISPTR x)
{
	return x >= &cellBlock[0] &&
		   x < &cellBlock[MAX_CELLS] &&
		   !(((char*)x-(char*)cellBlock) % sizeof(cellBlock[0]));
}

bool listp(LISPTR x)
{
	return x==NIL || consp(x);
}

bool atomp(LISPTR x)
{
	return !consp(x);
}

bool symbolp(LISPTR x)
{
	return x >= &symPool[0] &&
		   x < &symPool[MAX_SYMBOLS];
}

bool stringp(LISPTR x)
{
	return x >= &stringPool[0] &&
		   x < &stringPool[MAX_STRING_POOL];
}

bool numberp(LISPTR x)
{
	return x >= &numberPool[0] &&
		   x < &numberPool[MAX_NUMBERS];
}

LISPTR car(LISPTR x)
{
	if (consp(x)) {
		return ((CELL*)x)->car;
	}
	if (x != NIL) {
		lisp_error(L"bad arg to car");
	}
	return NIL;
}

LISPTR cdr(LISPTR x)
{
	if (consp(x)) {
		return ((CELL*)x)->cdr;
	}
	if (x != NIL) {
		lisp_error(L"bad arg to cdr");
	}
	return NIL;
}

LISPTR cadr(LISPTR x)
{
	if (consp(x)) {
		x = ((CELL*)x)->cdr;
		if (consp(x)) {
			return ((CELL*)x)->car;
		}
	}
	if (x != NIL) {
		lisp_error(L"bad arg to cadr");
	}
	return NIL;
}

LISPTR cddr(LISPTR x)
{
	if (consp(x)) {
		x = ((CELL*)x)->cdr;
		if (consp(x)) {
			return ((CELL*)x)->cdr;
		}
	}
	if (x != NIL) {
		lisp_error(L"bad arg to cddr");
	}
	return NIL;
}

LISPTR defvar(LISPTR x, LISPTR y)
{
	if (symbolp(x)) {
		((SYMBOL*)x)->valueCell = y;
	}
	return x;
}

LISPTR rplacd(LISPTR x, LISPTR y)
{
	((CELL*)x)->cdr = y;
	return x;
}

LISPTR intern_string(const wchar_t* s)
{
	wcscpy_s(stringPool+stringCount, MAX_STRING_POOL-1-stringCount, s);
	LISPTR x = (LISPTR)(stringPool+stringCount);
	stringCount += wcslen(s)+1;
	return x;
}

LISPTR intern_number(const wchar_t* s)
{
	LISPTR x = (LISPTR)&numberPool[numberCount++];
	wchar_t* ep;
	*((double*)x) = wcstod(s, &ep);
	return x;
}

LISPTR intern(const wchar_t* s)
{
	int i;
	for (i = 0; i < symCount; i++) {
		if (0==wcscmp(string_text(symPool[i].name), s)) {
			break;
		}
	}
	if (i == symCount) {
		// Create a new symbol with name s
		symCount++;
		symPool[i].name = intern_string(s);
		symPool[i].fnCell = NIL;
		symPool[i].valueCell = NIL;
	}
	return (LISPTR)&symPool[i];
}

const LISPTR symbol_name(LISPTR x)
{
	if (!symbolp(x)) {
		return NIL;
	}
	return ((SYMBOL*)x)->name;
}

LISPTR symbol_value(LISPTR x)
{
	if (!symbolp(x)) {
		return NIL;		// should be *UNBOUND* or something?
	}
	return ((SYMBOL*)x)->valueCell;
}

LISPTR symbol_function(LISPTR x)
{
	if (!symbolp(x)) {
		return NIL;		// should be *UNBOUND* or something?
	}
	return ((SYMBOL*)x)->fnCell;
}

// Compiled/native functions (SUBRs and FSUBRs)
//
LISPTR def_fsubr(const wchar_t* name, NATIVE1ARGS fn)
{
	// make symbol
	LISPTR x = intern(name);
	((SYMBOL*)x)->fnCell = make_fsubr(fn);
	return x;
} // def_fsubr

LISPTR def_subr0(const wchar_t* name, NATIVE0ARGS fn)
{
	// make symbol
	LISPTR x = intern(name);
	((SYMBOL*)x)->fnCell = make_subr0(fn);
	return x;
} // def_subr0

LISPTR def_subr1(const wchar_t* name, NATIVE1ARGS fn)
{
	// make symbol
	LISPTR x = intern(name);
	((SYMBOL*)x)->fnCell = make_subr1(fn);
	return x;
} // def_subr1

LISPTR def_subr2(const wchar_t* name, NATIVE2ARGS fn)
{
	// make symbol
	LISPTR x = intern(name);
	((SYMBOL*)x)->fnCell = make_subr2(fn);
	return x;
} // def_subr2

bool compiled_function_p(LISPTR x)
{
	return x >= &subrPool[0] &&
		   x < &subrPool[MAX_SUBRS];
}

LISPTR make_fsubr(NATIVE1ARGS fn)
{
	SUBR* x = &subrPool[subrCount++];
	x->nargs = -1;
	x->subr1 = fn;
	return (LISPTR)x;
}

LISPTR make_subr0(NATIVE0ARGS fn)
{
	SUBR* x = &subrPool[subrCount++];
	x->nargs = 0;
	x->subr0 = fn;
	return (LISPTR)x;
}

LISPTR make_subr1(NATIVE1ARGS fn)
{
	SUBR* x = &subrPool[subrCount++];
	x->nargs = 1;
	x->subr1 = fn;
	return (LISPTR)x;
}

LISPTR make_subr2(NATIVE2ARGS fn)
{
	SUBR* x = &subrPool[subrCount++];
	x->nargs = 2;
	x->subr2 = fn;
	return (LISPTR)x;
}

LISPTR call_compiled_fn(LISPTR f, LISPTR args)
{
	LISPTR v = NIL;
	if (!compiled_function_p(f)) {
		lisp_error(L"call_compiled_fn called with non-SUBR");
	} else {
		SUBR* cfp = (SUBR*)f;
		switch (cfp->nargs) {
		case -1:
			v = cfp->fsubr(args);
			break;
		case 0:
			v = cfp->subr0();
			break;
		case 1:
			v = cfp->subr1(eval(car(args)));
			break;
		case 2:
			v = cfp->subr2(eval(car(args)), eval(cadr(args)));
			break;
		case 3:
			v = cfp->subr3(eval(car(args)), eval(cadr(args)), eval(cadr(cddr(args))));
			break;
		default:
			lisp_error(L"compiled fn wants too many args");
			break;
		} // switch
	}
	return v;
}