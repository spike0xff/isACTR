#ifndef ISACTR_H
#define ISACTR_H

#include <math.h>		// for log

const float PRIORITY_MAX = (float)(-log(0.0));
const float PRIORITY_MIN = (float)log(0.0);

void isactr_model_init(void);
void isactr_model_release(void);
bool isactr_model_load(FILE* in, FILE* err);

void isactr_define_chunk_type(LISPTR ct);

// Add a chunk to DM.
// chunk format is (<name> ISA <chunktype> { <slotname> <value> })
void isactr_add_dm(LISPTR chunk);

// Add a production to PM.
// production format is (<name> <LHS> ==> <RHS>)
void isactr_add_production(LISPTR prod);

// note: takes a Symbol
void isactr_set_goal_focus(LISPTR chunk_name);

#endif // ISACTR_H
