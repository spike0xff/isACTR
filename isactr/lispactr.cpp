#include "lisp.h"
#include "isactr.h"

#include <assert.h>
#include <string.h>

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

static unsigned first_char(LISPTR x)
{
	if (symbolp(x)) {
		const wchar_t* name = string_text(symbol_name(x));
		return name[0];
	}
	return 0;
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

// true iff x is the kind of symbol that starts
// a 'clause' (condition or action) in the LHS or RHS of a production
// That is, either a buffer-spec like =goal> or +retrieval>
// or a thing like !stop! or !output!
static bool is_clause_start(LISPTR x)
{
	// It's a buffer-spec if it's a symbol whose last char is '>'
	if (symbolp(x)) {
		const wchar_t* name = string_text(symbol_name(x));
		return name[wcslen(name)-1] == '>' || name[0]=='!';
	}
	return false;
} // is_clause_start

static bool is_slot_modifier(LISPTR x)
{
	return (x == MINUS) ||
		   (x == LT) ||
		   (x == LEQ) ||
		   (x == GT) ||
		   (x == GEQ) ||
		   (x == EQUALS);
} // is_slot_modifier

// buffer-test ::= =buffer-name> isa chunk-type slot-test*
// slot-test ::= {slot-modifier} slot-name slot-value
// slot-modifier ::= [= | - | < | > | <= | >=]
static LISPTR translate_slot_test_sequence(LISPTR p, LISPTR* pvars)
{
	if (!consp(p)) {
		return p;
	}
	if (is_clause_start(car(p))) {
		return NIL;						// start of new clause, end of test sequence
	}
	// default modifier is '='
	LISPTR modifier = EQUALS;
	LISPTR slotName = car(p);
	LISPTR value = cadr(p);
	p = cddr(p);
	if (is_slot_modifier(slotName)) {
		modifier = slotName;
		slotName = value;
		value = car(p); p = cdr(p);
	}
	// replace variables of the form =name with shared dotted pairs (<var>.NIL)
	if (is_variable(value)) {
		LISPTR binding = assoc(value, *pvars);
		if (binding==NIL) {
			binding = cons(value,NIL);
			*pvars = cons(binding, *pvars);
			printf("new binding at 0x%08x: ", binding); lisp_print(binding, stdout); printf("\n");
		} else {
			printf("old binding at 0x%08x: ", binding); lisp_print(binding, stdout); printf("\n");
		}
		value = binding;
	}
	// Represent the test as a triplet (modifier slot-name value)
	LISPTR test = cons(modifier, cons(slotName, cons(value, NIL)));
	// Return the list of tests
	return cons(test, translate_slot_test_sequence(p, pvars));
}

static LISPTR copy_test(LISPTR p, LISPTR* pvars)
{
	if (!consp(p)) {
		return p;
	}
	LISPTR item = car(p);
	if (is_clause_start(item)) {
		return NIL;
	}
	if (is_variable(item)) {
		LISPTR binding = assoc(item, *pvars);
		if (binding==NIL) {
			binding = cons(item,NIL);
			printf("new binding at 0x%08x: ", binding); lisp_print(binding, stdout); printf("\n");
			*pvars = cons(binding, *pvars);
		} else {
			printf("old binding at 0x%08x: ", binding); lisp_print(binding, stdout); printf("\n");
		}
		item = binding;
	} else if (consp(item)) {
		item = copy_test(item, pvars);
	}
	return cons(item, copy_test(cdr(p), pvars));
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

static bool parse_condition(LISPTR* pp, LISPTR* pcond, LISPTR* pvars)
{
	LISPTR p = *pp;
	if (!consp(p) || !is_clause_start(car(p))) {
		lisp_error(L"invalid condition in production LHS");
		return false;
	}
	switch (first_char(car(p))) {
	case '=':		// buffer test
		// buffer-test ::= =buffer-name> isa chunk-type slot-test*
		// Note: We treat the chunk-type test as just another slot-test.
		*pcond = cons(BUFFER_TEST, cons(extract_buffer_name(car(p)), translate_slot_test_sequence(cdr(p), pvars)));
		break;
	case '?':		// buffer query
		*pcond = cons(BUFFER_QUERY, cons(extract_buffer_name(car(p)), copy_test(cdr(p), pvars)));
		break;
	case '!':
		// such as !output! or !eval! or !bind!
		*pcond = cons(car(p), copy_test(cdr(p), pvars));
		break;
	default:
		lisp_error(L"invalid buffer-spec in LHS condition");
		return false;
	} // switch
	p = cdr(p);
	while (consp(p) && !is_clause_start(car(p))) {
		p = cdr(p);
	}
	assert(p==NIL || is_clause_start(car(p)));
	*pp = p;
	return true;
}

static bool parse_action(LISPTR* pp, LISPTR* pact, LISPTR* pvars)
{
	LISPTR p = *pp;
	if (!consp(p) || !is_clause_start(car(p))) {
		lisp_error(L"unrecognized action in production RHS");
		return false;
	}
	switch (first_char(car(p))) {
	case '=':
		*pact = cons(MOD_BUFFER_CHUNK, cons(extract_buffer_name(car(p)), copy_test(cdr(p), pvars)));
		break;
	case '+':
		// request ::= +buffer-name> [direct-value | isa chunk-type request-spec*]
		*pact = cons(MODULE_REQUEST, cons(extract_buffer_name(car(p)), copy_test(cdr(p), pvars)));
		break;
	case '-':
		// buffer-clearing ::= -buffer-name>
		*pact = cons(CLEAR_BUFFER, cons(extract_buffer_name(car(p)), NIL));
		break;
	case '!':
		// for example !eval! !bind! !output! or !stop!
		*pact = cons(car(p), copy_test(cdr(p), pvars));
		break;
	default:
		lisp_error(L"unrecognized action in production RHS");
		return false;
	} // switch
	p = cdr(p);
	while (consp(p) && !is_clause_start(car(p))) {
		p = cdr(p);
	}
	assert(p==NIL || is_clause_start(car(p)));
	*pp = p;
	return true;
} // parse_action

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
		lisp_error(L"production isn't a list");
	} else {
		LISPTR name = car(args);
		if (!symbolp(name)) {
			lisp_error(L"production name is not a symbol");
		} else {
			LISPTR lhs = NIL;
			LISPTR rhs = NIL;
			LISPTR vars = NIL;		// a-list of variables
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
	def_fsubr(L"DEFINE-MODEL", define_model);
	def_subr0(L"CLEAR-ALL", clear_all);
	def_subr1(L"RUN", subr_run);
}

