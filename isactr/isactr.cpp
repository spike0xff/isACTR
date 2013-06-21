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

void isactr_process_stream(FILE* in, FILE* out, FILE* err);

int main(int argc, char* argv[])
{
	int i;
	printf("Hello, humans!\n");
	FILE* in = stdin;
	FILE* out = stdout;
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
	init_lisp_actr();
	isactr_process_stream(in, out, stderr);
	lisp_shutdown();
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

void isactr_push_event(isactr_model* model, isactr_event* evt)
{
	assert(model != NULL);
	assert(evt != NULL);
	assert(evt->action != NULL);
	assert(evt->time >= model->time);

	isactr_event** p = &model->nextEvent;
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
	fprintf(model->out, "     %5.3f   ------                 %s %s %s REQUESTED %s\n",
		model->time, "SET-BUFFER-CHUNK", "GOAL", "<chunk-name>", "NIL");
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
		isactr_push_event(model, evt);
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

void isactr_model_init(isactr_model* model)
{
	model->time = 0.0;
	model->nextEvent = NULL;
}

void isactr_model_release(isactr_model* model)
{
	// clear out the event queue if any:
	isactr_event* evt;
	while ((evt = isactr_dequeue_next_event(model))) {
		isactr_release_event(evt);
	}
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
		fputs("lisp_eval => ", model->out);
		lisp_print(lisp_eval(m), model->out);
		fputs("\n", model->out);
	}
	fputs("** End Model\n---------------------\n", model->out);
	return true;
}

void isactr_model_run(isactr_model* model)
{
	fprintf(model->out, "Industrial Strength ACT-R  %d.%d.%d.%d\n", VERSION_MAJOR, VERSION_MINOR, VERSION_RELEASE, VERSION_BUILD);
	while (isactr_do_next_event(model)) {
	}
	fprintf(model->out, "0.3\n47\nNIL\n");
}


void isactr_set_goal_focus(isactr_model* model, chunk_name cn)
{
	isactr_event* evt = (isactr_event*)malloc(sizeof isactr_event);
	evt->time = model->time;		// i.e. 'now'
	evt->priority = PRIORITY_MAX;
	evt->requested = false;
	evt->action = event_action_set_buffer_chunk;

	isactr_push_event(model, evt);
} // fn_goal_focus


void isactr_process_stream(FILE* in, FILE* out, FILE* err)
{
	isactr_model model;
	isactr_model_init(&model);
	model.in = in;
	model.out = out;
	model.err = err;

	isactr_push_event_at_time(&model, 0.050, "production-fired start");
	isactr_push_event_at_time(&model, 0.100, "retrieved-chunk C");
	isactr_push_event_at_time(&model, 0.100, "production-fired increment");
	if (isactr_model_load(&model, in, err)) {
		isactr_model_run(&model);
	}
	isactr_model_release(&model);
}
