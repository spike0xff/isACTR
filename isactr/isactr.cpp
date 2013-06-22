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

///////////////////////////////////////////////////////////////////////
// forward function declarations
void isactr_process_stream(FILE* in, FILE* out, FILE* err);
isactr_event* isactr_push_event_at_time(double t, isactr_event_action action);
void isactr_clear_event_queue(void);

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
	GOAL = intern(L"GOAL");
	RETRIEVAL = intern(L"RETRIEVAL");
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

static void event_action_fire_production(isactr_event* evt)
{
	LISPTR p = evt->chunk;
	LISPTR pname = car(p);
	fprintf(model.out, "     %5.3f   %-22ls %s %ls\n",
		model.time, L"PROCEDURAL", "PRODUCTION-FIRED", string_text(symbol_name(pname)));
}

// true if the named slot is in the chunk and its value matches the specified value.
static bool slot_match(LISPTR slot, LISPTR value, LISPTR chunk)
{
	printf("slot_match(%ls, ", string_text(symbol_name(slot)));
	lisp_print(value, stdout);
	printf(", ");
	lisp_print(chunk, stdout);
	printf(")\n");
	while (consp(chunk)) {
		if (slot == cadr(chunk)) {
			// slot found, match the value
			return eql(value, caddr(chunk));
		}
		chunk = cddr(chunk);
	}
	return false;
} // slot_match

static bool lhs_matches(LISPTR lhs)
{
	while (consp(lhs)) {
		LISPTR test = car(lhs); lhs = cdr(lhs);
		LISPTR buffer = car(test);
		// get the contents of the specified buffer
		LISPTR contents = NIL;
		if (buffer == GOAL) {
			contents = model.goal;
		} else if (buffer == RETRIEVAL) {
			contents = model.retrieval;
		}
		// match the buffer contents against the rest of the test clause
		test = cddr(test);
		while (consp(test)) {
			LISPTR slot = car(test);
			LISPTR value = cadr(test);
			if (!slot_match(slot, value, contents)) {
				break;
			}
			test = cddr(test);
		} // while test
		if (test != NIL) {
			return false;
		}
		// one test passed
	} // while lhs
	return true;
} // lhs_matches


static bool is_ready_to_fire(LISPTR p)
{
	printf("is_ready_to_fire? ");
	lisp_print(p, stdout);
	printf("\n");
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
void isactr_add_production(LISPTR name, LISPTR lhs, LISPTR rhs)
{
	LISPTR prod = cons(name, cons(lhs, cons(rhs, NIL)));
	model.pm = cons(prod, model.pm);
	fprintf(model.out, "PRODUCTION:\n  ");
	lisp_print(prod, model.out);
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

