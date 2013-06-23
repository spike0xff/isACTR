#include "lisp.h"
#include "isactr.h"

#include <assert.h>
#include <string.h>

static LISPTR SGP, CHUNK_TYPE, ADD_DM, P, GOAL_FOCUS, RIGHT_ARROW;
static LISPTR BUFFER_TEST;

LISPTR clear_all(void)
{
	return NIL;
}

static LISPTR model_name;

LISPTR sgp(LISPTR args)
{
	return SGP;
}

LISPTR chunk_type(LISPTR args)
{
	isactr_define_chunk_type(args);
	return CHUNK_TYPE;
}

LISPTR add_dm(LISPTR args)
{
	while (consp(args)) {
		LISPTR chunk = car(args);
		args = cdr(args);
		isactr_add_dm(chunk);
	}
	return ADD_DM;
}

static bool is_bufspec(LISPTR x)
{
	// It's a buffer-spec if it's a symbol whose last char is '>'
	if (symbolp(x)) {
		const wchar_t* name = string_text(symbol_name(x));
		return name[wcslen(name)-1] == '>';
	}
	return false;
}

static bool is_variable(LISPTR x)
{
	return symbolp(x) &&
		  '=' == string_text(symbol_name(x))[0];
} // is_variable

static LISPTR copy_test(LISPTR p, LISPTR* pvars)
{
	if (!consp(p)) {
		return p;
	}
	LISPTR item = car(p);
	if (!is_bufspec(item)) {
		if (is_variable(item)) {
			LISPTR binding = assoc(item, *pvars);
			if (binding==NIL) {
				binding = cons(item,NIL);
				*pvars = cons(binding, *pvars);
			}
			item = binding;
		}
		return cons(item, copy_test(cdr(p), pvars));
	}
	return NIL;
} // copy_test

static LISPTR extract_buffer_name(LISPTR sym)
{
	if (!symbolp(sym)) {
		lisp_error(L"buffer-spec in production is not a symbol");
		return NIL;
	}
	wchar_t bufname[128];
	wcscpy_s(bufname, string_text(symbol_name(sym)));
	bufname[wcslen(bufname)-1] = 0;
	return intern(bufname+1);
} // extract_buffer_name

static bool parse_clause(LISPTR* pp, LISPTR* ptest, LISPTR* pvars)
{
	LISPTR p = *pp;
	if (consp(p)) {
		if (is_bufspec(car(p))) {
			*ptest = cons(BUFFER_TEST, cons(extract_buffer_name(car(p)), copy_test(cdr(p), pvars)));
			p = cdr(p);
			while (consp(p) && !is_bufspec(car(p))) {
				p = cdr(p);
			}
			assert(p==NIL || is_bufspec(car(p)));
			*pp = p;
			return true;
		}
	}
	return false;
} // parse_clause

static bool parse_condition(LISPTR* pp, LISPTR* pcond, LISPTR* pvars)
{
	return parse_clause(pp, pcond, pvars);
}

static bool parse_action(LISPTR* pp, LISPTR* pact, LISPTR* pvars)
{
	return parse_clause(pp, pact, pvars);
}

static bool parse_production(LISPTR p, LISPTR* plhs, LISPTR* prhs, LISPTR* pvars)
{
	while (consp(p)) {
		// it's a list, it starts with either a right-arrow (==>)
		// or a buffer-spec, like =goal>
		if (car(p)==RIGHT_ARROW) {
			if (!prhs) {
				lisp_error(L"2nd ==> in production??");
				return false;
			}
			// recurse to parse right-hand side
			// (tail recursion, could be flattened)
			return parse_production(cdr(p), prhs, NULL, pvars);
		}
		LISPTR clause = NIL;
		if (prhs) {
			// parse LHS
			if (!parse_condition(&p, &clause, pvars)) {
				return false;
			}
		} else {
			// working on RHS
			if (!parse_action(&p, &clause, pvars)) {
				return false;
			}
		}
		// parsed a clause, append to the LHS or RHS
		*plhs = nconc(*plhs, cons(clause, NIL));
	} // while parsing production
	// end of production, check for syntax errors
	if (p != NIL) {
		lisp_error(L"junk at end of production");
		return false;
	}
	if (prhs!=NULL) {
		lisp_error(L"didn't find ==> in production");
		return false;
	}
	// OK!
	return true;
}

LISPTR p(LISPTR args)
{
	// each p defines one production.
	// car(args) = name of the production
	// cdr(args) = production (<LHS> ==> <RHS>)
	if (!consp(args)) {
		lisp_error(L"production not list");
	} else {
		LISPTR name = car(args);
		if (!symbolp(name)) {
			lisp_error(L"production name is not a symbol");
		} else {
			LISPTR lhs = NIL;
			LISPTR rhs = NIL;
			LISPTR vars = NIL;		// alist of variables
			if (parse_production(cdr(args), &lhs, &rhs, &vars)) {
				isactr_add_production(name, lhs, rhs, vars);
			}
		}
	}
	return P;
}

LISPTR goal_focus(LISPTR args)
{
	if (!consp(args) || !symbolp(car(args))) {
		lisp_error(L"argument-1 to GOAL-FOCUS is not a symbol");
	} else {
		isactr_set_goal_focus(car(args));
	}
	return GOAL_FOCUS;
}

LISPTR define_model(LISPTR m)
{
	if (consp(m)) {
		model_name = car(m); m = cdr(m);
		while (consp(m)) {
			LISPTR f = car(m); m = cdr(m);
			if (consp(f)) {
				LISPTR verb = car(f);
				LISPTR args = cdr(f);
				if (verb==SGP) {
					sgp(args);
				} else if (verb==CHUNK_TYPE) {
					chunk_type(args);
				} else if (verb==ADD_DM) {
					add_dm(args);
				} else if (verb==P) {
					p(args);
				} else if (verb==GOAL_FOCUS) {
					goal_focus(args);
				} else {
					lisp_error(L"unrecognized verb in model");
					break;
				}
			}
		} // while
	}
	return model_name;
} // define_model

LISPTR subr_run(LISPTR duration)
{
	double dduration = number_value(duration);
	isactr_model_run(dduration);
	return NIL;
} // subr_run

void init_lisp_actr(void)
{
	SGP = intern(L"SGP");
	CHUNK_TYPE = intern(L"CHUNK-TYPE");
	ADD_DM = intern(L"ADD-DM");
	P = intern(L"P");
	GOAL_FOCUS = intern(L"GOAL-FOCUS");
	RIGHT_ARROW = intern(L"==>");
	BUFFER_TEST = intern(L"BUFFER-TEST");
	def_fsubr(L"DEFINE-MODEL", define_model);
	def_subr0(L"CLEAR-ALL", clear_all);
	def_subr1(L"RUN", subr_run);
}

