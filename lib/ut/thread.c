/* -*- C -*- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/thread.h"
#include "lib/assert.h"

enum {
	NR = 255
};

int t0place = 0;

void t0(int x)
{
	t0place = x;
}

struct c2_thread t[NR];
int r[NR] = { 0, };

void t2(int n)
{
	int result;

	if (n > 0) {
		result = C2_THREAD_INIT(&t[n - 1], int, &t2, n - 1);
		C2_ASSERT(result == 0);
	}
	r[n] = n;
}

void test_thread(void)
{
	int i;
	int result;
	char t1place[100];

	result = C2_THREAD_INIT(&t[0], int, &t0, 42);
	C2_ASSERT(result == 0);
	c2_thread_join(&t[0]);
	c2_thread_fini(&t[0]);
	C2_ASSERT(t0place == 42);

	result = C2_THREAD_INIT(&t[0], const char *, 
				LAMBDA(void, (const char *s) { 
						strcpy(t1place, s); } ), 
				(const char *)"forty-two");
	C2_ASSERT(result == 0);
	c2_thread_join(&t[0]);
	c2_thread_fini(&t[0]);
	C2_ASSERT(!strcmp(t1place, "forty-two"));

	t2(NR - 1);
	for (i = NR - 2; i >= 0; --i) {
		/* this loop is safe, because t[n] fills t[n - 1] before
		   exiting. */
		c2_thread_join(&t[i]);
		c2_thread_fini(&t[i]);
		C2_ASSERT(r[i] == i);
	}
}


/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
