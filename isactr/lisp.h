#ifndef LISP_H
#define LISP_H

#include <stdio.h>

typedef void *LISPTR;		// Lisp pointer

const extern LISPTR NIL;	// Lispy null, also false.
const extern LISPTR T;		// Lispy TRUE
const extern LISPTR QUOTE;	// symbol named QUOTE
const extern LISPTR FUNCTION;
const extern LISPTR LAMBDA;

void lisp_init(void);
void lisp_shutdown(void);

// Read and return the next S-expression from a file
LISPTR lisp_read(FILE* in);
LISPTR lisp_print(LISPTR x, FILE* out);

LISPTR lisp_eval(LISPTR x);

// Read-Eval-Print-Loop
void lisp_REPL(FILE* in, FILE* out, FILE* err);

void lisp_error(const wchar_t* msg);

LISPTR cons(LISPTR x, LISPTR y);
LISPTR car(LISPTR x);
LISPTR cdr(LISPTR x);
LISPTR cadr(LISPTR x);
LISPTR cddr(LISPTR x);
LISPTR caddr(LISPTR x);
LISPTR cadddr(LISPTR x);
LISPTR assoc(LISPTR item, LISPTR alist);
LISPTR defvar(LISPTR x, LISPTR y);
bool consp(LISPTR x);
bool listp(LISPTR x);
bool atomp(LISPTR x);
bool symbolp(LISPTR x);
bool stringp(LISPTR x);
bool numberp(LISPTR x);
bool eql(LISPTR x, LISPTR y);
LISPTR intern(const wchar_t* name);
const LISPTR symbol_name(LISPTR x);
LISPTR symbol_value(LISPTR x);
LISPTR symbol_function(LISPTR x);
LISPTR eval(LISPTR x);
LISPTR intern_string(const wchar_t* str);
LISPTR intern_number(const wchar_t* str);
LISPTR progn(LISPTR x);
LISPTR rplacd(LISPTR x, LISPTR y);		// returns modified x
LISPTR nconc(LISPTR x, LISPTR y);		// modifies x to end with y, returns x
#define string_text(x) ((const wchar_t*)(x))
#define number_value(x) (*(double*)(x))

typedef LISPTR (*NATIVE0ARGS)();
typedef LISPTR (*NATIVE1ARGS)(LISPTR);
typedef LISPTR (*NATIVE2ARGS)(LISPTR, LISPTR);
typedef LISPTR (*NATIVE3ARGS)(LISPTR, LISPTR, LISPTR);

LISPTR def_fsubr(const wchar_t* name, NATIVE1ARGS fn);
LISPTR def_subr0(const wchar_t* name, NATIVE0ARGS fn);
LISPTR def_subr1(const wchar_t* name, NATIVE1ARGS fn);
LISPTR def_subr2(const wchar_t* name, NATIVE2ARGS fn);
LISPTR def_subr3(const wchar_t* name, NATIVE3ARGS fn);
bool compiled_function_p(LISPTR x);
LISPTR make_fsubr(NATIVE1ARGS fn);
LISPTR make_subr0(NATIVE0ARGS fn);
LISPTR make_subr1(NATIVE1ARGS fn);
LISPTR make_subr2(NATIVE2ARGS fn);
LISPTR make_subr3(NATIVE3ARGS fn);
LISPTR call_compiled_fn(LISPTR f, LISPTR args);

#endif // LISP_H
