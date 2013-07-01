#ifndef VERSION_H
#define VERSION_H

#define VERSION_MAJOR 0
#define VERSION_MINOR 0
#define VERSION_RELEASE 14
#define VERSION_BUILD 0

/*
0.0.14.0	2013.06.30	spike	addition.lisp run! Only diff. in trace is extra CONFLICT-RESOLUTION at end.
0.0.13.0	2013.06.30	spike	!output! working well, count.lisp runs 100%, event order still not exactly right.
0.0.12.0	2013.06.29	spike	parse buffer queries (?buffer> on LHS)
0.0.11.0	2013.06.27	spike	getting the basic LHS/RHS events in the queue, and in the right order.
0.0.10.0	2013.06.23	spike	parse !output!
0.0.9.0		2013.06.22	spike	minimal LHS matching working
0.0.8.0		2013.06.22	spike	partial parsing and matching of productions
0.0.7.0		2013.06.20	spike	reads and superficially parses model as S-exprs
0.0.6.0		2013.06.17	spike	simple read-eval-print loop working
0.0.5.0		2013.05.21	spike	stubs for some Lisp functions
0.0.4.0		2013.04.30	spike	starting on production matching	
0.0.3.0					spike	added a few asserts, changed event dequeue to return event ptr.
0.0.2.0					spike	first running event queue.  Need asserts!
0.0.1.0					spike	main, model structure
*/

#endif // VERSION_H
