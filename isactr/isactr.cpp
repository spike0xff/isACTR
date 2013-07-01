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

typedef enum {
	BUFFER_FREE,
	BUFFER_BUSY,
	BUFFER_ERROR
} BufferState;

typedef struct _isactr_model {
	bool			running;
	FILE*			in;
	FILE*			out;
	FILE*			err;
	double			time;
	double			timeLimit;			// max time to run
	isactr_event	eventQueue;			// queued-up events
										// the head of the queue is a dummy event
	LISPTR			types;				// list of chunk-types
	LISPTR			dm;					// list of chunks
	LISPTR			pm;					// list of productions
	// state
	LISPTR			goal;				// contents of GOAL buffer
	LISPTR			retrieval;			// contents of RETRIEVAL buffer
	BufferState		retrievalState;
} isactr_model;

///////////////////////////////////////////////////////////////////////
// global variables

isactr_model model;

// lots of known atoms
LISPTR GOAL, RETRIEVAL;
LISPTR SGP, CHUNK_TYPE, ADD_DM, P, GOAL_FOCUS, RIGHT_ARROW;
LISPTR EQUALS, MINUS, NOT, LT, LEQ, GT, GEQ;
LISPTR BUFFER_TEST, BUFFER_QUERY;
LISPTR MOD_BUFFER_CHUNK;
LISPTR MODULE_REQUEST;
LISPTR CLEAR_BUFFER;
LISPTR BANG_OUTPUT;
LISPTR BANG_EVAL, BANG_SAFE_EVAL;
LISPTR BANG_BIND, BANG_SAFE_BIND, BANG_MV_BIND;

bool inner_trace = false;			// trace internal interpreter activity

///////////////////////////////////////////////////////////////////////
// forward function declarations
void isactr_process_stream(FILE* in, FILE* out, FILE* err);
isactr_event* isactr_schedule_event(double t, double priority, isactr_event_action action);
void isactr_clear_event_queue(void);
void isactr_delete_event_by_action(isactr_event_action action);
void isactr_release_event(isactr_event* evt);
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
	EQUALS = intern(L"=");
	MINUS = intern(L"-");
	NOT = intern(L"NOT");
	LT = intern(L"<");
	LEQ = intern(L"<=");
	GT = intern(L"<");
	GEQ = intern(L"<=");
	BUFFER_TEST = intern(L"BUFFER-TEST");
	BUFFER_QUERY = intern(L"BUFFER-QUERY");
	MOD_BUFFER_CHUNK = intern(L"MOD-BUFFER-CHUNK");
	MODULE_REQUEST = intern(L"MODULE-REQUEST");
	CLEAR_BUFFER = intern(L"CLEAR-BUFFER");
	BANG_OUTPUT = intern(L"!OUTPUT!");
	BANG_EVAL = intern(L"!EVAL!");
	BANG_SAFE_EVAL = intern(L"!SAFE-EVAL!");
	BANG_BIND = intern(L"!BIND!");
	BANG_SAFE_BIND = intern(L"!SAFE-BIND!");
	BANG_MV_BIND = intern(L"!MV-BIND!");

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

void isactr_model_warning(const char* msg)
{
	fprintf(model.err, "#|Warning: %s |#", msg);
}

bool is_variable(LISPTR x)
{
	return symbolp(x) &&
		  '=' == string_text(symbol_name(x))[0];
} // is_variable

static bool precedes(isactr_event* evt1, isactr_event* evt2)
{
	if (evt1->time != evt2->time) {
		return evt1->time < evt2->time;
	}
	// note: simultaneous events of equal priority
	// are queue FIFO.
	return evt1->priority >= evt2->priority;
}

void isactr_push_event(isactr_event* evt)
{
	assert(evt != NULL);
	assert(evt->action != NULL);
	assert(evt->time >= model.time);

	isactr_event* p = &model.eventQueue;
	while (p->next && precedes(p->next, evt)) {
		p = p->next;
	}
	// insert e into the list at this point.
	// The next event in list follows this one:
	evt->next = p->next;
	// the previous event in list is followed by this one:
	p->next = evt;
} // isactr_push_event

void isactr_delete_event(isactr_event* evt)
{
	assert(evt != NULL);
	// point to dummy head event
	isactr_event* p = &model.eventQueue;
	// scan for evt or end of queue
	while (p->next != evt && (p = p->next)) {}
	if (p) {
		assert(p->next == evt);
		// patch out of queue
		p->next = evt->next;
		// release the deleted event
		isactr_release_event(evt);
	}
} // isactr_delete_event

void isactr_delete_event_by_action(isactr_event_action action)
{
	assert(action != NULL);
	isactr_event* p = &model.eventQueue;
	while (p->next) {
		// is the next event the one we're looking for?
		if (p->next->action == action) {
			// yes: patch it out of the queue
			isactr_event* evt = p->next;
			p->next = evt->next;
			// free the deleted event
			isactr_release_event(evt);
			break;
		}
		p = p->next;
	}
} // isactr_delete_event_by_action

static void event_action_null(isactr_event* evt)
{
	fprintf(model.out, "     %5.3f   ------                 %s\n",
		model.time, "-no action specified-");
}

static void event_action_buffer_read_action(isactr_event* evt)
{
	LISPTR buffer = evt->buffer;
	fprintf(model.out, "     %5.3f   %-22ls %s %ls\n",
		model.time, L"PROCEDURAL", "BUFFER-READ-ACTION", string_text(symbol_name(buffer)));
}

static void event_action_retrieval_failure(isactr_event* evt)
{
	fprintf(model.out, "     %5.3f   %-22ls %s\n",
		model.time, L"DECLARATIVE", "RETRIEVAL-FAILURE");
	model.retrievalState = BUFFER_ERROR;
}

// evt->buffer is buffer, evt->chunk = full chunk, car=name
static void event_action_set_buffer_chunk(isactr_event* evt)
{
	LISPTR chunk = evt->chunk;
	if (!consp(chunk)) {
		lisp_error(L"set_buffer_chunk: bad chunk");
		return;
	}
	LISPTR chunkName = car(evt->chunk);
	chunk = cdr(chunk);					// don't include chunk-name in buffer

	// put the chunk in the designated buffer
	LISPTR buffer = evt->buffer;
	const wchar_t* area = L"<buffer?>";
	if (buffer == GOAL) {
		model.goal = chunk;
		area = L"GOAL";
	} else if (buffer == RETRIEVAL) {
		model.retrieval = chunk;
		area = L"DECLARATIVE";
	}

	fprintf(model.out, "     %5.3f   %-22ls %s %ls %ls %s\n",
		model.time, area, "SET-BUFFER-CHUNK",
		string_text(symbol_name(evt->buffer)), string_text(symbol_name(chunkName)), 
		(evt->requested ? "" : "REQUESTED NIL")
		);

	isactr_schedule_event(model.time, PRIORITY_MIN, event_action_conflict_resolution);

	if (inner_trace) {
		printf("--goal:      "); lisp_print(model.goal, stdout); printf("\n");
		printf("--retrieval: "); lisp_print(model.retrieval, stdout); printf("\n");
	}
} // event_action_set_buffer_chunk

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

static void event_action_mod_buffer(isactr_event* evt)
{
	LISPTR buffer = evt->buffer;
	LISPTR action = evt->chunk;
	fprintf(model.out, "     %5.3f   %-22ls %s %ls\n",
		model.time, L"PROCEDURAL", "MOD-BUFFER-CHUNK", string_text(symbol_name(buffer)));
	LISPTR* pbuffer = NULL;
	if (buffer == GOAL) {
		pbuffer = &model.goal;
	} else if (buffer == RETRIEVAL) {
		pbuffer = &model.retrieval;
	} else {
		fprintf(model.err, "unknown buffer (%ls) in RHS action", string_text(symbol_name(buffer)));
	}
	while (consp(action)) {
		LISPTR slotName = car(action);
		LISPTR value = cadr(action);
		if (consp(value)) {
			value = cdr(value);
		}
		*pbuffer = modify_chunk(*pbuffer, slotName, value);
		action = cddr(action);
	}
	if (inner_trace) {
		fprintf(model.out, "--goal:      "); lisp_print(model.goal, stdout); fprintf(model.out, "\n");
		fprintf(model.out, "--retrieval: "); lisp_print(model.retrieval, stdout); fprintf(model.out, "\n");
	}
} // event_action_mod_buffer

static void event_action_retrieved(isactr_event* evt)
{
	LISPTR chunk = evt->chunk;				// includes car=name
	LISPTR chunkName = car(evt->chunk);

	fprintf(model.out, "     %5.3f   %-22ls %s %ls\n",
		model.time, L"DECLARATIVE", "RETRIEVED-CHUNK", string_text(symbol_name(chunkName)));
	model.retrievalState = BUFFER_FREE;
	isactr_event* evt2 = isactr_schedule_event(model.time, PRIORITY_MAX, event_action_set_buffer_chunk);
	evt2->buffer = RETRIEVAL;
	evt2->chunk = evt->chunk;
	evt2->requested = true;
}

static void event_action_start_retrieval(isactr_event* evt)
{
	// buffer is understood to be RETRIEVAL
	// 'chunk' is the pattern for the chunk to be retrieved
	LISPTR pattern = evt->chunk;
	fprintf(model.out, "     %5.3f   %-22ls %s\n",
		model.time, L"DECLARATIVE", "START-RETRIEVAL");
	LISPTR chunk = isactr_retrieve_chunk(pattern);
	// Note, includes name = car(chunk)
	if (chunk == NIL) {
		// retrieval failed
		evt = isactr_schedule_event(model.time+0.050, PRIORITY_0, event_action_retrieval_failure);
	} else {
		evt = isactr_schedule_event(model.time+0.050, PRIORITY_0, event_action_retrieved);
		evt->chunk = chunk;
	}
}

static void event_action_production_fired(isactr_event* evt)
{
	LISPTR p = evt->chunk;		// the production that fired
	LISPTR pname = car(p);
	fprintf(model.out, "     %5.3f   %-22ls %s %ls\n",
		model.time, L"PROCEDURAL", "PRODUCTION-FIRED", string_text(symbol_name(pname)));
	isactr_fire_production(p);
	evt = isactr_schedule_event(model.time, PRIORITY_MIN, event_action_conflict_resolution);
}

static bool action_buffer_modification(LISPTR action)
{
	isactr_event* evt = isactr_schedule_event(model.time, PRIORITY_100, event_action_mod_buffer);
	evt->buffer = car(action);
	evt->chunk = cdr(action);
	return true;
}

static void event_action_clear_buffer(isactr_event* evt)
{
	LISPTR buffer = evt->buffer;
	fprintf(model.out, "     %5.3f   %-22ls %s %ls\n",
		model.time, L"PROCEDURAL", "CLEAR-BUFFER", string_text(symbol_name(buffer)));
	if (buffer == GOAL) {
		model.goal = NIL;
	} else if (buffer == RETRIEVAL) {
		model.retrieval = NIL;
	}
}

static bool action_clear_buffer(LISPTR action)
{
	isactr_event* evt = isactr_schedule_event(model.time, PRIORITY_10, event_action_clear_buffer);
	evt->buffer = car(action);
	return true;
}

static void event_action_module_request(isactr_event* evt)
{
	LISPTR buffer = evt->buffer;
	fprintf(model.out, "     %5.3f   %-22ls %s %ls\n",
		model.time, L"PROCEDURAL", "MODULE-REQUEST", string_text(symbol_name(buffer)));

	if (buffer==RETRIEVAL) {
		if (model.retrievalState == BUFFER_BUSY) {
			isactr_model_warning("A retrieval event has been aborted by a new request");
			isactr_delete_event_by_action(event_action_start_retrieval);
		}
		model.retrievalState = BUFFER_FREE;
		isactr_event* evt2 = isactr_schedule_event(model.time, -2000, event_action_start_retrieval);
		evt2->chunk = evt->chunk;
		model.retrievalState = BUFFER_BUSY;
	}
}

static bool action_module_request(LISPTR action)
{
	LISPTR buffer = car(action);
	isactr_event* evt = isactr_schedule_event(model.time, PRIORITY_50, event_action_module_request);
	evt->buffer = buffer;
	evt->chunk = cdr(action);

	isactr_schedule_event(model.time, PRIORITY_10, event_action_clear_buffer)->buffer = buffer;
	return true;
}

static LISPTR eval_form_with_vars(LISPTR form)
{
	if (!consp(form)) {
		return form;
	}
	if (is_variable(car(form))) {
		return cdr(form);
	}
	LISPTR x = eval_form_with_vars(car(form));
	LISPTR y = eval_form_with_vars(cdr(form));
	if (x != car(form) || y != cdr(form)) {
		return cons(x,y);
	} else {
		return form;
	}
}

static bool action_output(LISPTR action)
{
	LISPTR form = eval_form_with_vars(car(action));
	while (consp(form)) {
		lisp_print(car(form), model.out);
		fprintf(model.out, " ");
		form = cdr(form);
	}
	if (form != NIL) {
		lisp_print(form, model.out);
	}
	fprintf(model.out, "\n");
	return true;
}

static bool action_eval(LISPTR action)
{
	fprintf(model.out, "** !eval! not implemented\n");
	return false;
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
		// takes place immediately (during production-fired event)
		return action_output(action);
	} else if (op == BANG_EVAL) {
		return action_eval(action);
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
// Note that (for equality only) value can be a (var.val) pair, which is matched if
// val != NIL, or bound if if val==NIL.
static bool slot_match(LISPTR chunk, LISPTR modifier, LISPTR slotName, LISPTR value)
{
	bool bResult = false;
	if (inner_trace) {
		fprintf(model.out, "slot_match %ls %ls, ", string_text(symbol_name(modifier)), string_text(symbol_name(slotName)));
		lisp_print(value, stdout); fprintf(model.out, ", "); lisp_print(chunk, stdout);
	}
	while (consp(chunk)) {
		if (car(chunk) == slotName) {
			// slot found, match the value
			LISPTR slotVal = cadr(chunk);
			bool bMatch;
			if (modifier == EQUALS) {
				if (!consp(value)) {
					// atomic value, must be eql to slot value
					bMatch = eql(value, slotVal);
				} else if (cdr(value)!=NIL) {
					// variable (var.val) compare to 
					bMatch = eql(cdr(value), slotVal);
				} else if (slotVal != NIL) {
					// unbound variable, bind to value from slot
					rplacd(value, slotVal);
					bMatch = true;
				} else {
					bMatch = false;
				}
			} else if (modifier == MINUS) {
				if (consp(value)) {
					value = cdr(value);
				}
				bMatch = !eql(value, slotVal);
			} else {
				// inequality: = [< | > | <= | >=] - Only applies to numbers.
				if (consp(value)) {
					value = cdr(value);
				}
				if (numberp(value) && numberp(slotVal)) {
					double dValue = number_value(value);
					double dSlotVal = number_value(slotVal);
					if (modifier == LT) {
						bMatch = (dValue < dSlotVal);
					} else if (modifier == LEQ) {
						bMatch = (dValue <= dSlotVal);
					} else if (modifier == GT) {
						bMatch = (dValue > dSlotVal);
					} else if (modifier == GEQ) {
						bMatch = (dValue >= dSlotVal);
					} else {
						lisp_error(L"invalid slot modifier");
						bMatch = false;
					}
				}
			}
			bResult = bMatch;
			break;
		}
		chunk = cddr(chunk);
	}
	if (chunk==NIL && value==NIL) {
		// Treat slot not found same as (slot NIL).
		bResult = true;
	}
	if (inner_trace) {
		fprintf(model.out, " => %s\n", bResult ? "true" : "false");
	}
	return bResult;
} // slot_match

// Test a buffer for match to condition
static bool buffer_test(LISPTR buffer, LISPTR cond)
{
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
		// get the test - a triplet of (modifier slot-name value)
		LISPTR test = car(cond);
		LISPTR modifier = car(test);
		LISPTR slotName = cadr(test);
		LISPTR value = caddr(test);
		if (!slot_match(contents, modifier, slotName, value)) {
			break;
		}
		cond = cdr(cond);
	} // while cond
	return cond == NIL;
} // buffer_test

static bool buffer_query(LISPTR buffer, LISPTR cond)
{
	fprintf(model.err, "buffer queries not implemented\n");
	return false;
} // buffer_query

static bool test_condition(LISPTR cond)
{
	LISPTR op = car(cond);				// operation, like BUFFER-TEST
	cond = cdr(cond);
	if (op == BUFFER_TEST) {
		return buffer_test(car(cond), cdr(cond));
	} else if (op == BUFFER_QUERY) {
		return buffer_query(car(cond), cdr(cond));
	} else if (op == BANG_EVAL || op == BANG_SAFE_EVAL
		    || op == BANG_BIND || op == BANG_SAFE_BIND || op == BANG_MV_BIND) {
		fprintf(model.err, "%ls is not currently supported in LHS condition\n", string_text(symbol_name(op)));
		return false;
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

// reset all the values in an a-list to NIL
static void reset_variables(LISPTR vars)
{
	while (consp(vars)) {
		rplacd(car(vars), NIL);
		vars = cdr(vars);
	}
} // reset_variables

// Return true if production p is ready to fire
// production format is: (name LHS RHS vars)
static bool is_ready_to_fire(LISPTR p)
{
	if (inner_trace) {
		fprintf(model.out, "is_ready_to_fire? "); lisp_print(car(p), stdout); printf("\n");
	}
	// set the production's vars to value NIL
	LISPTR vars = cadddr(p);
	reset_variables(vars);
	// match the left-hand-side against current model state
	if (lhs_matches(cadr(p))) {
		if (inner_trace) {
			fprintf(model.out, " ... ready to fire!\n");
		}
		return true;
	}
	if (inner_trace) {
		fprintf(model.out, " ... not ready.\n");
	}
	return false;
} // is_ready_to_fire


static void event_action_production_selected(isactr_event* evt)
{
	LISPTR pname = car(evt->chunk);
	fprintf(model.out, "     %5.3f   %-22ls %s %ls\n",
		model.time, L"PROCEDURAL", "PRODUCTION-SELECTED", string_text(symbol_name(pname)));

	// queue up events for reading, querying or searching buffers in the LHS
	LISPTR lhs = cadr(evt->chunk);
	while (consp(lhs)) {
		LISPTR condition = car(lhs);
		LISPTR op = car(condition);
		if (op == BUFFER_TEST) {
			isactr_event* evt = isactr_schedule_event(model.time, PRIORITY_0, event_action_buffer_read_action);
			evt->buffer = cadr(condition);
		}
		lhs = cdr(lhs);
	} // while

	// followed (later) by the firing event
	isactr_event* evt2 = isactr_schedule_event(model.time+0.05, PRIORITY_0, event_action_production_fired);
	evt2->chunk = evt->chunk;
}

static void schedule_firing(LISPTR p)
{
	isactr_event* evt = isactr_schedule_event(model.time, PRIORITY_MAX, event_action_production_selected);
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
}

// Create and enqueue an event at future time t with action act.
// If successful, returns a pointer to the enqueued event.
// Otherwise returns NULL after reporting error to 'err'.
// Causes of failure:
//	insufficient memory
isactr_event* isactr_schedule_event(double t, double priority, isactr_event_action act)
{
	assert(t >= model.time);
	assert(act != NULL);

	// allocate storage for event:
	isactr_event* evt = (isactr_event*)malloc(sizeof isactr_event);
	if (evt) {
		evt->time = (float)t;
		evt->action = act;
		evt->priority = priority;
		evt->buffer = NIL;
		evt->chunk = NIL;
		evt->requested = false;
		// sort new event into the model's event queue
		isactr_push_event(evt);
	} else {
		fprintf(model.err, "out of memory in isactr_schedule_event(t=%1.3f)\n", t);
	}
	// return the newly created event for possible further customization by caller:
	return evt;
} // isactr_schedule_event

void isactr_release_event(isactr_event* evt)
{
	assert(evt != NULL);
	assert(evt->action);

	free(evt);
}

isactr_event* isactr_dequeue_next_event(isactr_model* model)
{
	isactr_event* evt = model->eventQueue.next;
	if (evt) {
		assert(evt->action);
		model->eventQueue.next = evt->next;
	}
	return evt;
}

bool isactr_do_next_event(void)
{
	isactr_event* evt;
	if (!(evt = isactr_dequeue_next_event(&model))) {
		fprintf(model.out, "     %5.3f   ------                 %s\n",
			model.time, "Stopped because no events left to process");
		return false;							// event queue empty
	}
	if (evt->time > model.timeLimit) {
		fprintf(model.out, "     %5.3f   ------                 %s\n",
			model.time, "Stopped because time limit reached");
		return false;
	}
	model.time = evt->time;				// 'now' is the time of this event
	evt->action(evt);						// 'do' the event
	isactr_release_event(evt);
	return true;
}

void isactr_model_init(void)
{
	memset(&model, 0, sizeof model);
	model.running = false;
	model.time = 0.0;
	model.timeLimit = INFINITY;
	model.eventQueue.next = NULL;
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

	while (isactr_do_next_event()) {}
	isactr_clear_event_queue();
	fprintf(model.out, "%0.1f\n47\n", model.time);
}


void isactr_define_chunk_type(LISPTR ct)
{
	model.types = cons(ct, model.types);
	if (inner_trace) {
		fprintf(model.out, "CHUNK-TYPE: ");
		lisp_print(ct, model.out);
		fprintf(model.out, "\n");
	}
}


void isactr_add_dm(LISPTR chunk)
{
	model.dm = cons(chunk, model.dm);
	if (inner_trace) {
		fprintf(model.out, "ADD-DM: ");
		lisp_print(chunk, model.out);
		fprintf(model.out, "\n");
	}
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

static bool chunk_matches_key(LISPTR chunk, LISPTR key)
{
	chunk = cdr(chunk);		// skip over chunk-name
	while (consp(key) && consp(chunk)) {
		LISPTR slot = car(key);
		LISPTR value = cadr(key);
		if (consp(value)) {
			value = cdr(value);
		}
		// search the chunk for matching slot
		LISPTR c = chunk;
		while (consp(c)) {
			if (car(c) == slot) {
				if (!eql(cadr(c), value)) {
					return false;			// value mismatch, fail
				}
				break;						// value match, continue with key
			}
			c = cddr(c);
		}
		if (c == NIL) {
			// key-slot not found in chunk
			return false;
		}
		// we just found the leading (slot value) of key, in chunk
		if (slot == car(chunk)) {
			// if we just matched the leading slot of the chunk
			// only search the tail from now on:
			chunk = cddr(chunk);
		}
		// continue with rest of key
		key = cddr(key);
	}
	// true if found everything in key, false otherwise:
	return (key == NIL);
}

// note, returned list includes name in CAR
LISPTR isactr_retrieve_chunk(LISPTR key)
{
	LISPTR dm = model.dm;
	LISPTR chunk = NIL;
	while (consp(dm)) {
		if (chunk_matches_key(car(dm), key)) {
			chunk = car(dm);
			break;
		}
		dm = cdr(dm);
	}
	return chunk;
} // isactr_retrieve_chunk

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
	if (inner_trace) {
		fprintf(model.out, "PRODUCTION: %ls\n  LHS: ", string_text(symbol_name(name)));
		lisp_print(lhs, model.out);
		fprintf(model.out, "\n  RHS: "); lisp_print(rhs, model.out);
		fprintf(model.out, "\n  VARS: "); lisp_print(vars, model.out);
		fprintf(model.out, "\n");
	}
}

void isactr_set_goal_focus(LISPTR chunk_name)
{
	isactr_event* evt = isactr_schedule_event(model.time, PRIORITY_MAX, event_action_set_buffer_chunk);
	evt->buffer = intern(L"GOAL");
	evt->chunk = isactr_get_chunk(chunk_name);
	evt->requested = false;
} // fn_goal_focus

