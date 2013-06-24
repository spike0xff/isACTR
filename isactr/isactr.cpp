// isactr.cpp : Defines the entry point for the console application.
//

#include "stdlib.h"
#include "stdio.h"
#include <math.h>
#include <limits.h>
#include <string.h>
#include <assert.h>
#include "version.h"
#include "lisp.h"		// "Lisp" functions
#include "isactr.h"		// isACTR API
#include "lispactr.h"	// ACT-R-in-Lisp stuff

/* Design Notes
2013.06.21
Don't bother with thread-safe.  If we need to multithread within a model, we'll add the necessary internal interlocks.
Models run one to a process. That means we don't have to provide inter-model security within a process, we rely on
the inter-process protection of the underlying operating system.  We still have to worry about inter-model security, but
not the additional issues of in-process inter-thread security.
One user, one process, one address space, one cognitive instance.
One big advantage: All the major systems can be singletons if they want to be, which can significantly reduce code
size and speed up low-level operations, because 'the model' or 'the state' doesn't have to be passed as a pointer
to every function, even car and consp.
*/

///////////////////////////////////////////////////////////////////////
// constants

#define INFINITY (-log(0.0))

///////////////////////////////////////////////////////////////////////
// types and typedefs

typedef void (*isactr_event_action)(struct _isactr_event*);
typedef wchar_t* chunk_name;

typedef struct _isactr_event {
	struct _isactr_event*	next;
	double					time;			// when this event happens
	double					priority;
	bool					requested;
	isactr_event_action		action;
	LISPTR					buffer;			// buffer name (SYMBOL)
	LISPTR					chunk;			// chunk, if any
} isactr_event;


typedef struct _isactr_model {
	FILE*			in;
	FILE*			out;
	FILE*			err;
	double			time;
	double			timeLimit;			// max time to run
	isactr_event*	nextEvent;			// queued-up events
	LISPTR			types;				// list of chunk-types
	LISPTR			dm;					// list of chunks
	LISPTR			pm;					// list of productions
	// state
	LISPTR			goal;				// contents of GOAL buffer
	LISPTR			retrieval;			// contents of RETRIEVAL buffer
} isactr_model;

///////////////////////////////////////////////////////////////////////
// global variables

isactr_model model;
LISPTR GOAL, RETRIEVAL;
LISPTR SGP, CHUNK_TYPE, ADD_DM, P, GOAL_FOCUS, RIGHT_ARROW;
LISPTR BUFFER_TEST;
LISPTR MOD_BUFFER_CHUNK;
LISPTR MODULE_REQUEST;
LISPTR CLEAR_BUFFER;
LISPTR BANG_OUTPUT;

///////////////////////////////////////////////////////////////////////
// forward function declarations
void isactr_process_stream(FILE* in, FILE* out, FILE* err);
isactr_event* isactr_push_event_at_time(double t, isactr_event_action action);
void isactr_clear_event_queue(void);
static void event_action_conflict_resolution(isactr_event* evt);
void isactr_fire_production(LISPTR p);

///////////////////////////////////////////////////////////////////////
// functions

int main(int argc, char* argv[])
{
	int i;
	FILE* in = stdin;
	FILE* out = stdout;
	fprintf(out, "Industrial Strength ACT-R  %d.%d.%d.%d\n", VERSION_MAJOR, VERSION_MINOR, VERSION_RELEASE, VERSION_BUILD);
	// arg 0 is the full path to this executable.
	for (i = 1; i < argc; i++) {
		printf("argv[%d] = '%s'\n", i, argv[i]);
		if (argv[i][0] != '-') {
			// not an option, assume it's an input file
			if (in != stdin) {
				fclose(in);
			}
			in = fopen(argv[i], "r");
			if (!in) {
				return errno;
			}
		}
	}
	lisp_init();

	// create our standard symbols
	GOAL = intern(L"GOAL");
	RETRIEVAL = intern(L"RETRIEVAL");
	SGP = intern(L"SGP");
	CHUNK_TYPE = intern(L"CHUNK-TYPE");
	ADD_DM = intern(L"ADD-DM");
	P = intern(L"P");
	GOAL_FOCUS = intern(L"GOAL-FOCUS");
	RIGHT_ARROW = intern(L"==>");
	BUFFER_TEST = intern(L"BUFFER-TEST");
	MOD_BUFFER_CHUNK = intern(L"MOD-BUFFER-CHUNK");
	MODULE_REQUEST = intern(L"MODULE-REQUEST");
	CLEAR_BUFFER = intern(L"CLEAR-BUFFER");
	BANG_OUTPUT = intern(L"!OUTPUT!");

	isactr_model_init();
	init_lisp_actr();
	model.in = in;
	model.out = out;
	model.err = stderr;
	if (isactr_model_load(in, out, stderr)) {
		lisp_REPL(stdin, stdout, stderr);
	}
	lisp_shutdown();
	isactr_model_release();
	fgetwc(stdin);
	return 0;
}

static bool precedes(isactr_event* evt1, isactr_event* evt2)
{
	if (evt1->time != evt2->time) {
		return evt1->time < evt2->time;
	}
	return evt1->priority > evt2->priority;
}

void isactr_push_event(isactr_event* evt)
{
	assert(evt != NULL);
	assert(evt->action != NULL);
	assert(evt->time >= model.time);

	isactr_event** p = &model.nextEvent;
	while (*p && precedes(*p, evt)) {
		p = &(*p)->next;
	}
	// insert e into the list at this point.
	// The next event in list follows this one:
	evt->next = *p;
	// the previous event in list is followed by this one:
	*p = evt;
} // isactr_push_event

static void event_action_end(isactr_event* evt)
{
	isactr_clear_event_queue();
	fprintf(model.out, "     %5.3f   ------                 %s\n",
		model.time, "Stopped because no events left to process");
} // event_action_end

static void event_action_timeout(isactr_event* evt)
{
	isactr_clear_event_queue();
	fprintf(model.out, "     %5.3f   ------                 %s\n",
		model.time, "Stopped because time limit reached");
} // event_action_timeout

static void event_action_null(isactr_event* evt)
{
	fprintf(model.out, "     %5.3f   ------                 %s\n",
		model.time, "-no action specified-");
}

static void event_action_retrieved(isactr_event* evt)
{
	LISPTR chunkName = intern(L"D");		// TODO: unfake!

	fprintf(model.out, "     %5.3f   %-22ls %s %ls\n",
		model.time, L"DECLARATIVE", "RETRIEVED-CHUNK", string_text(symbol_name(chunkName)));
}

static void event_action_start_retrieval(isactr_event* evt)
{
	// buffer is understood to be RETRIEVAL
	// 'chunk' is the pattern for the chunk to be retrieved
	LISPTR pattern = evt->chunk;
	fprintf(model.out, "     %5.3f   %-22ls %s\n",
		model.time, L"DECLARATIVE", "START-RETRIEVAL");
	evt = isactr_push_event_at_time(model.time+0.050, event_action_retrieved);
}

static void event_action_fire_production(isactr_event* evt)
{
	LISPTR p = evt->chunk;		// the production that fired
	LISPTR pname = car(p);
	fprintf(model.out, "     %5.3f   %-22ls %s %ls\n",
		model.time, L"PROCEDURAL", "PRODUCTION-FIRED", string_text(symbol_name(pname)));
	isactr_fire_production(p);
	evt = isactr_push_event_at_time(model.time, event_action_conflict_resolution);
}

static LISPTR modify_chunk(LISPTR chunk, LISPTR slotName, LISPTR value)
{
	if (consp(chunk)) {
		if (car(chunk)==slotName) {
			return cons(slotName, cons(value, cddr(chunk)));
		} else {
			return cons(car(chunk), cons(cadr(chunk), modify_chunk(cddr(chunk), slotName, value)));
		}
	}
	return cons(slotName, cons(value, NIL));
} // modify_chunk

static bool action_buffer_modification(LISPTR action)
{
	LISPTR buffer = car(action); action = cdr(action);
	LISPTR* pbuffer = NULL;
	if (buffer == GOAL) {
		pbuffer = &model.goal;
	} else if (buffer == RETRIEVAL) {
		pbuffer = &model.retrieval;
	} else {
		fprintf(model.err, "unknown buffer (%ls) in RHS action", string_text(symbol_name(buffer)));
		return false;
	}
	fprintf(model.out, "     %5.3f   %-22ls %s %ls\n",
		model.time, L"PROCEDURAL", "BUFFER-READ-ACTION", string_text(symbol_name(buffer)));
	while (consp(action)) {
		LISPTR slotName = car(action);
		LISPTR value = cadr(action);
		if (consp(value)) {
			value = cdr(value);
		}
		*pbuffer = modify_chunk(*pbuffer, slotName, value);
		action = cddr(action);
	}
	fprintf(model.out, "     %5.3f   %-22ls %s %ls\n",
		model.time, L"PROCEDURAL", "MOD-BUFFER-CHUNK", string_text(symbol_name(buffer)));
	printf("--goal:      "); lisp_print(model.goal, stdout); printf("\n");
	printf("--retrieval: "); lisp_print(model.retrieval, stdout); printf("\n");
	return true;
}

static bool action_clear_buffer(LISPTR action)
{
	LISPTR buffer = car(action);
	if (buffer == GOAL) {
		model.goal = NIL;
	} else if (buffer == RETRIEVAL) {
		model.retrieval = NIL;
	} else {
		fprintf(model.err, "unknown buffer (%ls) in RHS action", string_text(symbol_name(buffer)));
		return false;
	}
	fprintf(model.out, "     %5.3f   %-22ls %s %ls\n",
		model.time, L"PROCEDURAL", "CLEAR-BUFFER", string_text(symbol_name(buffer)));
	return true;
}

static bool action_module_request(LISPTR action)
{
	LISPTR buffer = car(action);
	fprintf(model.out, "     %5.3f   %-22ls %s %ls\n",
		model.time, L"PROCEDURAL", "MODULE-REQUEST", string_text(symbol_name(buffer)));
	isactr_event* evt = isactr_push_event_at_time(model.time, event_action_start_retrieval);
	evt->chunk = cdr(action);
	action_clear_buffer(action);
	return true;
}

static bool action_output(LISPTR action)
{
	LISPTR value = car(action);
	if (consp(value)) {
		value = cdr(value);
	}
	lisp_print(value, model.out);
	fprintf(model.out, "\n");
	return true;
}

bool apply_action(LISPTR action)
{
	LISPTR op = car(action);
	action = cdr(action);
	if (op == MOD_BUFFER_CHUNK) {
		// =buffer> { slot value }*
		// modify contents of a buffer
		return action_buffer_modification(action);
	} else if (op == MODULE_REQUEST) {
		return action_module_request(action);
	} else if (op == CLEAR_BUFFER) {
		return action_clear_buffer(action);
	} else if (op == BANG_OUTPUT) {
		return action_output(action);
	}
	fprintf(model.err, "invalid RHS action type: %ls\n", string_text(symbol_name(op)));
	return false;
}

// fire production p.
// assume LHS matched, variables are bound
void isactr_fire_production(LISPTR p)
{
	LISPTR rhs = caddr(p);
	while (consp(rhs)) {
		LISPTR action = car(rhs);
		apply_action(action);
		rhs = cdr(rhs);
	}
}

// true if the named slot is in the chunk and its value matches the specified value.
static bool slot_match(LISPTR slot, LISPTR value, LISPTR chunk)
{
	bool bMatch = false;
	printf("slot_match %ls, ", string_text(symbol_name(slot)));
	lisp_print(value, stdout); printf(", "); lisp_print(chunk, stdout);
	while (consp(chunk)) {
		if (slot == car(chunk)) {
			// slot found, match the value
			if (!consp(value)) {
				bMatch = eql(value, cadr(chunk));
			} else if (cdr(value)!=NIL) {
				bMatch = eql(cdr(value), cadr(chunk));
			} else {
				rplacd(value, cadr(chunk));
				bMatch = true;
			}
			break;
		}
		chunk = cddr(chunk);
	}
	if (chunk==NIL && value==NIL) {
		// Treat slot not found same as (slot NIL).
		bMatch = true;
	}
	printf(" => %s\n", bMatch ? "true" : "false");
	return bMatch;
} // slot_match

static bool buffer_test(LISPTR cond)
{
	LISPTR buffer = car(cond);			// buffer name
	cond = cdr(cond);
	// get the contents of the specified buffer
	LISPTR contents = NIL;
	if (buffer == GOAL) {
		contents = model.goal;
	} else if (buffer == RETRIEVAL) {
		contents = model.retrieval;
	} else {
		fprintf(model.err, "unknown buffer (%ls) in LHS clause", string_text(symbol_name(buffer)));
		return false;
	}
	// match the buffer contents against the rest of the cond clause
	while (consp(cond)) {
		LISPTR slot = car(cond);
		LISPTR value = cadr(cond);
		if (!slot_match(slot, value, contents)) {
			break;
		}
		cond = cddr(cond);
	} // while cond
	return cond == NIL;
} // buffer_test

static bool test_condition(LISPTR cond)
{
	LISPTR op = car(cond);				// operation, like BUFFER-TEST
	cond = cdr(cond);
	if (op == BUFFER_TEST) {
		return buffer_test(cond);
	}
	fprintf(model.err, "invalid condition test: %ls\n", string_text(symbol_name(op)));
	return false;
} // test_condition

static bool lhs_matches(LISPTR lhs)
{
	while (consp(lhs)) {
		LISPTR cond = car(lhs); lhs = cdr(lhs);
		if (!test_condition(cond)) {
			return false;
		}
	} // while lhs
	return true;
} // lhs_matches


static bool is_ready_to_fire(LISPTR p)
{
	printf("is_ready_to_fire? "); lisp_print(car(p), stdout); printf("\n");
	// set all production vars to value NIL
	LISPTR vars = cadr(cddr(p));
	while (consp(vars)) {
		rplacd(car(vars), NIL);
		vars = cdr(vars);
	}
	// match the left-hand-side against current model state
	if (lhs_matches(cadr(p))) {
		printf(" ... ready to fire!\n");
		return true;
	}
	printf(" ... not ready.\n");
	return false;
} // is_ready_to_fire


static void schedule_firing(LISPTR p)
{
	LISPTR pname = car(p);
	const wchar_t* pname_str = L"<bad>";
	if (symbolp(pname)) {
		pname_str = string_text(symbol_name(pname));
	}
	fprintf(model.out, "     %5.3f   %-22ls %s %ls\n",
		model.time, L"PROCEDURAL", "PRODUCTION-SELECTED", pname_str);
	isactr_event* evt = isactr_push_event_at_time(model.time+0.05, event_action_fire_production);
	evt->chunk = p;
}

static void event_action_conflict_resolution(isactr_event* evt)
{
	fprintf(model.out, "     %5.3f   %-22ls %s\n",
		model.time, L"PROCEDURAL", "CONFLICT-RESOLUTION");
	// look for a production that is ready to fire.
	LISPTR plist = model.pm;
	while (consp(plist)) {
		LISPTR p = car(plist);
		if (is_ready_to_fire(p)) {
			schedule_firing(p);
			return;
		}
		plist = cdr(plist);
	}
	// if no production ready to fire, schedule the 'stop' event at the front of the queue
	isactr_push_event_at_time(model.time, event_action_end)->priority = MAX_PRIORITY;
}

static void event_action_set_buffer_chunk(isactr_event* evt)
{
	const wchar_t* chunk_str = L"<bad>";
	if (symbolp(evt->chunk)) {
		chunk_str = string_text(symbol_name(evt->chunk));
	}
	LISPTR chunk = isactr_get_chunk(evt->chunk);
	if (chunk==NIL) {
		lisp_error(L"set_buffer_chunk: named chunk not found");
	} else {
		chunk = cdr(chunk);					// don't include chunk-name in buffer
		const char* area = "<buffer?>";
		if (evt->buffer == GOAL) {
			model.goal = chunk;
			area = "GOAL";
		} else if (evt->buffer == RETRIEVAL) {
			model.retrieval = chunk;
			area = "DECLARATIVE";
		}

		fprintf(model.out, "     %5.3f   %-22s %s %ls %ls REQUESTED %s\n",
			model.time, area, "SET-BUFFER-CHUNK", string_text(symbol_name(evt->buffer)), chunk_str, "NIL");
	}
	printf("--goal:      "); lisp_print(model.goal, stdout); printf("\n");
	printf("--retrieval: "); lisp_print(model.retrieval, stdout); printf("\n");
} // event_action_set_buffer_chunk


// Create and enqueue an event at future time t with action act.
// If successful, returns a pointer to the enqueued event.
// Otherwise returns NULL after reporting error to 'err'.
// Causes of failure:
//	insufficient memory
isactr_event* isactr_push_event_at_time(double t, isactr_event_action act)
{
	assert(t >= model.time);
	assert(act != NULL);

	// allocate storage for event:
	isactr_event* evt = (isactr_event*)malloc(sizeof isactr_event);
	if (evt) {
		evt->time = (float)t;
		evt->action = act;
		// sort new event into the model's event queue
		isactr_push_event(evt);
	} else {
		fprintf(model.err, "out of memory in isactr_push_event_at_time(t=%1.3f)\n", t);
	}
	// return the newly created event for possible further customization by caller:
	return evt;
} // isactr_push_event_at_time

void isactr_release_event(isactr_event* evt)
{
	assert(evt != NULL);
	assert(evt->action);

	free(evt);
}

isactr_event* isactr_dequeue_next_event(isactr_model* model)
{
	isactr_event* evt = NULL;
	if (model->nextEvent) {
		evt = model->nextEvent;
		assert(evt->action);
		model->nextEvent = evt->next;
	}
	return evt;
}

bool isactr_do_next_event(isactr_model* model)
{
	isactr_event* evt;
	if ((evt = isactr_dequeue_next_event(model))) {
		model->time = evt->time;				// 'now' is the time of this event
		evt->action(evt);						// 'do' the event
		isactr_release_event(evt);
		return true;
	} else {
		return false;
	}
}

void isactr_model_init(void)
{
	memset(&model, 0, sizeof model);
	model.time = 0.0;
	model.timeLimit = INFINITY;
	model.nextEvent = NULL;
	model.types = NIL;
	model.dm = NIL;
	model.pm = NIL;
	model.goal = NIL;
	model.retrieval = NIL;
}


void isactr_clear_event_queue(void)
{
	isactr_event* evt;
	while ((evt = isactr_dequeue_next_event(&model))) {
		isactr_release_event(evt);
	}
}

void isactr_model_release(void)
{
	// clear out the event queue if any:
	isactr_clear_event_queue();
	model.types = NIL;
	model.dm = NIL;
	model.pm = NIL;
}


bool isactr_model_load(FILE* in, FILE* out, FILE* err)
{
	fputs("** Loading Model\n", out);
	lisp_REPL(in, out, err);
	fputs("#|##  load model complete ##|#\n", out);
	return true;
}

void isactr_model_run(double dDur)
{
	model.timeLimit = dDur;
	isactr_event* evt;

	evt = isactr_push_event_at_time(0, event_action_conflict_resolution);

	evt = isactr_push_event_at_time(model.timeLimit, event_action_timeout);
	// do this last at that time:
	evt->priority = MIN_PRIORITY;

	while (isactr_do_next_event(&model)) {
	}
	fprintf(model.out, "%0.1f\n47\n", model.time);
}


void isactr_define_chunk_type(LISPTR ct)
{
	model.types = cons(ct, model.types);
	fprintf(model.out, "CHUNK-TYPE: ");
	lisp_print(ct, model.out);
	fprintf(model.out, "\n");
}


void isactr_add_dm(LISPTR chunk)
{
	model.dm = cons(chunk, model.dm);
	fprintf(model.out, "ADD-DM: ");
	lisp_print(chunk, model.out);
	fprintf(model.out, "\n");
}

LISPTR isactr_get_chunk(LISPTR chunk_name)
{
	LISPTR dm = model.dm;
	while (consp(dm)) {
		LISPTR chunk = car(dm); dm = cdr(dm);
		if (car(chunk)==chunk_name) {
			return chunk;
		}
	}
	return NIL;
}


// Add a production lhs ==> rhs with specified name, to production memory.
// where <lhs> and <rhs> are lists of 'clauses' of the form:
// (<buffer> <action> <arg> <arg>...)
// Adds a triplet of this form to the internal PM:
// (<name> <lhs> <rhs>)
void isactr_add_production(LISPTR name, LISPTR lhs, LISPTR rhs, LISPTR vars)
{
	LISPTR prod = cons(name, cons(lhs, cons(rhs, cons(vars, NIL))));
	// append production to production memory, so productions are tested in order.
	model.pm = nconc(model.pm, cons(prod, NIL));
	fprintf(model.out, "PRODUCTION: %ls\n  LHS: ", string_text(symbol_name(name)));
	lisp_print(lhs, model.out);
	fprintf(model.out, "\n  RHS: "); lisp_print(rhs, model.out);
	fprintf(model.out, "\n  VARS: "); lisp_print(vars, model.out);
	fprintf(model.out, "\n");
}

void isactr_set_goal_focus(LISPTR chunk_name)
{
	isactr_event* evt = (isactr_event*)malloc(sizeof isactr_event);
	evt->time = model.time;				// i.e. 'now'
	evt->priority = MAX_PRIORITY;		// do this first at that time
	evt->requested = false;
	evt->buffer = intern(L"GOAL");
	evt->chunk = chunk_name;
	evt->action = event_action_set_buffer_chunk;

	isactr_push_event(evt);
} // fn_goal_focus

