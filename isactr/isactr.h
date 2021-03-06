#ifndef ISACTR_H
#define ISACTR_H

#include <math.h>		// for log

const float PRIORITY_MAX = (float)(-log(0.0));
const float PRIORITY_MIN = (float)log(0.0);
#define PRIORITY_0		  0
#define PRIORITY_10		 10
#define PRIORITY_50		 50
#define PRIORITY_90		 90
#define PRIORITY_100	100

extern LISPTR GOAL, RETRIEVAL;
extern LISPTR SGP, CHUNK_TYPE, ADD_DM, P, GOAL_FOCUS, RIGHT_ARROW;
extern LISPTR EQUALS, MINUS, NOT, LT, LEQ, GT, GEQ;
extern LISPTR BUFFER_TEST;
extern LISPTR BUFFER_QUERY;
extern LISPTR MOD_BUFFER_CHUNK;
extern LISPTR MODULE_REQUEST;
extern LISPTR CLEAR_BUFFER;
extern LISPTR BANG_OUTPUT;
extern LISPTR BANG_EVAL, BANG_SAFE_EVAL;
extern LISPTR BANG_BIND, BANG_SAFE_BIND, BANG_MV_BIND;

void isactr_model_init(void);
void isactr_model_release(void);
bool isactr_model_load(FILE* in, FILE* out, FILE* err);
void isactr_model_run(double dDur);

void isactr_model_warning(const char* msg);

void isactr_define_chunk_type(LISPTR ct);

// Add a chunk to DM.
// chunk format is (<name> ISA <chunktype> { <slotname> <value> })
void isactr_add_dm(LISPTR chunk);

// find and return the chunk in DM with the given name
LISPTR isactr_get_chunk(LISPTR chunk_name);

// retrieve chunk matching pattern from DM
LISPTR isactr_retrieve_chunk(LISPTR pattern);

// Add a production to PM, lhs ==> rhs.
// lhs and rhs are lists of clauses of the form
// (<buffer> <operation> <arg> <arg> ...)
void isactr_add_production(LISPTR name, LISPTR lhs, LISPTR rhs, LISPTR vars);

// note: takes a Symbol
void isactr_set_goal_focus(LISPTR chunk_name);

// true if x is a variable by ACT-R convention i.e. a symbol whose name starts with '='
bool is_variable(LISPTR x);

#endif // ISACTR_H
