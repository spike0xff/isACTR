#include "lisp.h"

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
	return CHUNK_TYPE;
}

LISPTR add_dm(LISPTR args)
{
	return ADD_DM;
}

LISPTR p(LISPTR args)
{
	return P;
}

LISPTR goal_focus(LISPTR args)
{
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
	//defvar(T, T);
	def_fsubr(L"DEFINE-MODEL", define_model);
	def_subr0(L"CLEAR-ALL", clear_all);
	//def_subr1(L"CAR", car);
	//def_subr1(L"CDR", cdr);
	//def_subr2(L"CONS", cons);
	//def_subr1(L"ATOMP", subr_atomp);
	//def_subr1(L"NUMBERP", subr_numberp);
	//def_subr2(L"EQ", subr_eq);
	//def_subr2(L"ASSOC", assoc);
}

