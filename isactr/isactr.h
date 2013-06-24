#ifndef ISACTR_H
#define ISACTR_H

#include <math.h>		// for log

const float MAX_PRIORITY = (float)(-log(0.0));
const float MIN_PRIORITY = (float)log(0.0);

extern LISPTR GOAL, RETRIEVAL;
extern LISPTR SGP, CHUNK_TYPE, ADD_DM, P, GOAL_FOCUS, RIGHT_ARROW;
extern LISPTR BUFFER_TEST;
extern LISPTR MOD_BUFFER_CHUNK;
extern LISPTR MODULE_REQUEST;
extern LISPTR CLEAR_BUFFER;
extern LISPTR BANG_OUTPUT;

void isactr_model_init(void);
void isactr_model_release(void);
bool isactr_model_load(FILE* in, FILE* out, FILE* err);
void isactr_model_run(double dDur);

void isactr_define_chunk_type(LISPTR ct);

// Add a chunk to DM.
// chunk format is (<name> ISA <chunktype> { <slotname> <value> })
void isactr_add_dm(LISPTR chunk);

// find and return the chunk in DM with the given name
LISPTR isactr_get_chunk(LISPTR chunk_name);

// Add a production to PM, lhs ==> rhs.
// lhs and rhs are lists of clauses of the form
// (<buffer> <operation> <arg> <arg> ...)
void isactr_add_production(LISPTR name, LISPTR lhs, LISPTR rhs, LISPTR vars);

// note: takes a Symbol
void isactr_set_goal_focus(LISPTR chunk_name);

#endif // ISACTR_H
