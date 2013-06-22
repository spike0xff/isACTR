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

///////////////////////////////////////////////////////////////////////
// types and typedefs

typedef void (*isactr_event_action)(struct _isactr_model*, struct _isactr_event*);
typedef wchar_t* chunk_name;

typedef struct _isactr_event {
	struct _isactr_event*	next;
	float					time;			// when this event happens
	float					priority;
	bool					requested;
	isactr_event_action	action;
	wchar_t*				buffer;			// name of buffer, if any
	LISPTR					chunk;			// chunk, if any
} isactr_event;


typedef struct _isactr_model {
	FILE*			in;
	FILE*			out;
	FILE*			err;
	float			time;
	isactr_event*	nextEvent;			// queued-up events
	LISPTR			types;				// list of chunk-types
	LISPTR			dm;					// list of chunks
	LISPTR			pm;					// list of productions
} isactr_model;

///////////////////////////////////////////////////////////////////////
// global variables

isactr_model model;

///////////////////////////////////////////////////////////////////////
// forward function declarations
void isactr_process_stream(FILE* in, FILE* out, FILE* err);

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
	isactr_model_init();
	init_lisp_actr();
	isactr_process_stream(in, out, stderr);
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

static void event_action_null(isactr_model* model, isactr_event* evt)
{
	fprintf(model->out, "     %5.3f   ------                 %s\n",
		model->time, "-no action specified-");
}

static void event_action_set_buffer_chunk(isactr_model* model, isactr_event* evt)
{
	const wchar_t* chunk_str = L"<bad>";
	if (symbolp(evt->chunk)) {
		chunk_str = string_text(symbol_name(evt->chunk));
	}
	fprintf(model->out, "     %5.3f   %-22ls %s %ls %ls REQUESTED %s\n",
		model->time, evt->buffer, "SET-BUFFER-CHUNK", evt->buffer, chunk_str, "NIL");
} // event_action_set_buffer_chunk

static void event_action_end(isactr_model* model, isactr_event* evt)
{
	fprintf(model->out, "     %5.3f   ------                 %s\n",
		model->time, "Stopped because no events left to process");
} // event_action_end


// Create and enqueue an event at future time t with description d.
// If successful, returns a pointer to the enqueued event.
// Otherwise returns NULL after reporting error to 'err'.
// Causes of failure:
//	insufficient memory
isactr_event* isactr_push_event_at_time(isactr_model* model, double t, char* d)
{
	assert(model != NULL);
	assert(t >= model->time);
	assert(d != NULL);

	// allocate storage for event:
	isactr_event* evt = (isactr_event*)malloc(sizeof isactr_event);
	if (evt) {
		evt->time = (float)t;
		// make a copy we can delete later:
		evt->action = event_action_null;
		// sort new event into the model's event queue
		isactr_push_event(evt);
	} else {
		fprintf(model->err, "out of memory in isactr_push_event_at_time(t=%1.3f,d='%s')\n", t, d);
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
		evt->action(model, evt);				// 'do' the event
		isactr_release_event(evt);
		return true;
	} else {
		event_action_end(model, NULL);
		return false;
	}
}

void isactr_model_init(void)
{
	memset(&model, 0, sizeof model);
	model.time = 0.0;
	model.nextEvent = NULL;
	model.types = NIL;
	model.dm = NIL;
	model.pm = NIL;

}

void isactr_model_release(void)
{
	// clear out the event queue if any:
	isactr_event* evt;
	while ((evt = isactr_dequeue_next_event(&model))) {
		isactr_release_event(evt);
	}
	model.types = NIL;
	model.dm = NIL;
	model.pm = NIL;
}


bool isactr_model_load(isactr_model* model, FILE* in, FILE* err)
{
	fputs("** Loading Model\n", model->out);
	while (true) {
		LISPTR m = lisp_read(in);
		// debugging - trace what we just read:
		fputs("lisp_read => ", model->out);
		lisp_print(m, model->out);
		fputs("\n", model->out);
		// NIL means end-of-job:
		if (m==NIL) break;
		LISPTR v = lisp_eval(m);
		fputs("lisp_eval => ", model->out);
		lisp_print(v, model->out);
		fputs("\n", model->out);
	}
	fputs("#|##  load model complete ##|#\n", model->out);
	return true;
}

void isactr_model_run(isactr_model* model)
{
	while (isactr_do_next_event(model)) {
	}
	fprintf(model->out, "0.3\n47\nNIL\n");
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


void isactr_add_production(LISPTR prod)
{
	model.pm = cons(prod, model.pm);
	fprintf(model.out, "PRODUCTION:\n  ");
	lisp_print(prod, model.out);
	fprintf(model.out, "\n");
}

void isactr_set_goal_focus(LISPTR chunk_name)
{
	isactr_event* evt = (isactr_event*)malloc(sizeof isactr_event);
	evt->time = model.time;		// i.e. 'now'
	evt->priority = PRIORITY_MAX;
	evt->requested = false;
	evt->buffer = L"GOAL";
	evt->chunk = chunk_name;
	evt->action = event_action_set_buffer_chunk;

	isactr_push_event(evt);
} // fn_goal_focus


void isactr_process_stream(FILE* in, FILE* out, FILE* err)
{
	model.in = in;
	model.out = out;
	model.err = err;

	if (isactr_model_load(&model, in, err)) {
		isactr_push_event_at_time(&model, 0.050, "production-fired start");
		isactr_push_event_at_time(&model, 0.100, "retrieved-chunk C");
		isactr_push_event_at_time(&model, 0.100, "production-fired increment");
		isactr_model_run(&model);
	}
}

