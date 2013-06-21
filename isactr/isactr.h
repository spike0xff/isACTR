#ifndef ISACTR_H
#define ISACTR_H

typedef void (*isactr_event_action)(struct _isactr_model*, struct _isactr_event*);
typedef char* chunk_name;

typedef struct _isactr_event {
	struct _isactr_event*	next;
	float					time;			// when this event happens
	float					priority;
	bool					requested;
	isactr_event_action	action;
} isactr_event;

const float PRIORITY_MAX = (float)(-log(0.0));
const float PRIORITY_MIN = (float)log(0.0);

typedef struct _isactr_model {
	FILE*			in;
	FILE*			out;
	FILE*			err;
	float			time;
	isactr_event*	nextEvent;				// queued-up events
} isactr_model;

void isactr_model_init(isactr_model* model);
void isactr_model_release(isactr_model* model);
bool isactr_model_load(isactr_model* model, FILE* in, FILE* err);
void isactr_set_goal_focus(isactr_model* model, chunk_name cn);
void isactr_push_event(isactr_model* model, isactr_event* evt);

#endif // ISACTR_H
