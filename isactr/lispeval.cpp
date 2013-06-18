#include "lisp.h"

static LISPTR lexvars = NIL;

LISPTR bind_args(LISPTR formals, LISPTR acts, LISPTR prev)
{
	if (!consp(formals)) {
		return prev;
	}
	return cons(cons(car(formals), consp(acts) ? eval(car(acts)) : NIL), bind_args(cdr(formals), consp(acts) ? cdr(acts) : NIL, prev));
}

LISPTR apply(LISPTR f, LISPTR args)
{
	if (symbolp(f)) {
		// get the function binding of f
		f = symbol_function(f);
		if (consp(f)) {
			// function defined as S-expr
			if (car(f) == LAMBDA) {
				LISPTR oldBindings = lexvars;
				// bind formal arguments to evaluated actual arguments:
				lexvars = bind_args(cadr(f), args, lexvars);
				f = progn(cddr(f));
				lexvars = oldBindings;
			}
		} else if (compiled_function_p(f)) {
			// call compiled function with args
			f = call_compiled_fn(f, args);
		}
	}
	return f;
}

bool eql(LISPTR x, LISPTR y)
{
	if (x == y) {
		return true;
	}
	if (numberp(x)) {
		return numberp(y) && number_value(x)==number_value(y);
	}
	return false;
}

LISPTR assoc(LISPTR item, LISPTR alist)
{
	while (consp(alist)) {
		LISPTR binding = car(alist);
		if (consp(binding) && eql(item, car(binding))) {
			return binding;
		}
		alist = cdr(alist);
	}
	return NIL;
}

// evaluate form x with lexical bindings a
LISPTR eval(LISPTR x)
{
	if (consp(x)) {
		// evaluate a form
		LISPTR f = car(x);
		LISPTR args = cdr(x);
		x = apply(f, args);
	} else if (stringp(x) || numberp(x)) {
		return x;
	} else if (symbolp(x)) {
		LISPTR binding = assoc(x, lexvars);
		if (binding != NIL) {
			x = cdr(binding);
		} else {
			x = symbol_value(x);
		}
	}
	return x;
}

LISPTR lisp_eval(LISPTR x)
{
	LISPTR oldBindings = lexvars;
	lexvars = NIL;
	// evaluate with no lexical bindings:
	x = eval(x);
	lexvars = oldBindings;
	return x;
}

LISPTR progn(LISPTR x)
{
	LISPTR v = NIL;
	while (consp(x)) {
		v = eval(car(x));
		x = cdr(x);
	}
	return v;
} // progn
