#include "lisp.h"
#include "isactr.h"

static LISPTR SGP, CHUNK_TYPE, ADD_DM, P, GOAL_FOCUS;

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

LISPTR p(LISPTR args)
{
	// each p defines one production.
	// car(args) = name of the production
	// cdr(args) = production (<LHS> ==> <RHS>)
	isactr_add_production(args);
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
}

void init_lisp_actr(void)
{
	SGP = intern(L"SGP");
	CHUNK_TYPE = intern(L"CHUNK-TYPE");
	ADD_DM = intern(L"ADD-DM");
	P = intern(L"P");
	GOAL_FOCUS = intern(L"GOAL-FOCUS");
	def_fsubr(L"DEFINE-MODEL", define_model);
	def_subr0(L"CLEAR-ALL", clear_all);
}

