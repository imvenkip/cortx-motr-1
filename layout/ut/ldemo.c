/* -*- C -*- */

#include <string.h> /* memset */
#include <stdio.h>  /* printf */
#include <stdlib.h> /* atoi */

#include "lib/cdefs.h"
#include "lib/memory.h"
#include "lib/assert.h"

/**
   @addtogroup layout
   @{
*/

uint64_t m_enc(uint64_t row, uint64_t column, uint64_t width)
{
	C2_ASSERT(column < width);
	return row * width + column;
}

void m_dec(uint64_t pos, uint64_t width, uint64_t *row, uint64_t *column)
{
	*row    = pos / width;
	*column = pos % width;
}

struct pdec_layout {
	uint64_t pl_seed;
	uint32_t pl_N;
	uint32_t pl_K;
	uint32_t pl_P;

	uint32_t pl_C;
	uint32_t pl_L;
};

struct src_addr {
	uint64_t sa_group;
	uint64_t sa_unit;
};

struct tgt_addr {
	uint32_t ta_obj;
	uint64_t ta_unit;
};

uint32_t permute(uint64_t k, uint32_t n, uint32_t val)
{
	unsigned long f;
	unsigned      i;
	unsigned      j;
	unsigned      t;
	unsigned      x;
	uint32_t      s[n];

	C2_ASSERT(val < n);
	s[0] = 0; 
	s[1] = 1;
	for (f = 1, i = 2; i < n; ++i) {
		C2_ASSERT(f*i > f);
		f *= i;
		s[i] = i;
	}

	for (i = 0; i < n - 1; ++i) {
		t = (k/f) % (n - i);
		x = s[i + t];
		for (j = i + t; j > i; --j)
			s[j] = s[j - 1];
		s[i] = x;
		f /= n - i - 1;
	}
	C2_ASSERT(s[val] < n);
	return s[val];
}


/*  2^63 + 2^61 - 2^57 + 2^54 - 2^51 - 2^18 + 1 */
#define GOLDEN_RATIO_PRIME 0x9e37fffffffc0001UL

static uint64_t hash_64(uint64_t val)
{
	uint64_t hash = val;

	/*  Sigh, gcc can't optimise this alone like it does for 32 bits. */
	uint64_t n = hash;
	n <<= 18;
	hash -= n;
	n <<= 33;
	hash -= n;
	n <<= 3;
	hash += n;
	n <<= 3;
	hash -= n;
	n <<= 4;
	hash += n;
	n <<= 2;
	hash += n;

	return hash;
}

uint64_t hash(uint64_t v0, uint64_t v1)
{
	return hash_64(v0) ^ hash_64(v1);
}

void pdec_layout_map(const struct pdec_layout *play, const struct src_addr *src,
		     struct tgt_addr *tgt)
{
	uint32_t N;
	uint32_t K;
	uint32_t P;
	uint32_t W;

	uint32_t C;
	uint32_t L;

	uint64_t omega;
	uint64_t j;

	uint64_t b;
	uint64_t d;

	N = play->pl_N;
	K = play->pl_K;
	P = play->pl_P;
	C = play->pl_C;
	L = play->pl_L;

	W = N + 2*K;
	C2_ASSERT(C * W == L * P);

	m_dec(src->sa_group, C, &omega, &j);
	m_dec(m_enc(j, src->sa_unit, W), P, &b, &d);
	tgt->ta_obj  = permute(hash(play->pl_seed, omega), P, d);
	tgt->ta_unit = m_enc(omega, b, L);
}

void g(const struct pdec_layout *play, uint64_t unit, struct src_addr *src)
{
	m_dec(unit, play->pl_N, &src->sa_group, &src->sa_unit);
}

void pdec_layout(const struct pdec_layout *play, uint64_t unit, 
		 struct tgt_addr *tgt)
{
	struct src_addr src;

	g(play, unit, &src);
	pdec_layout_map(play, &src, tgt);
}

enum {
	DATA,
	PARITY,
	SPARE,
	NR
};

int classify(const struct pdec_layout *play, int unit)
{
	if (unit < play->pl_N)
		return DATA;
	else if (unit < play->pl_N + play->pl_K)
		return PARITY;
	else
		return SPARE;
}

int main(int argc, char **argv)
{
	const int P = 20;
	const struct pdec_layout play = {
		.pl_seed = 42,
		.pl_N = 4,
		.pl_K = 2,
		.pl_P = P, /* lcm(4 + 2*2, 20) == 40 */

		.pl_C = 40/(4 + 2*2),
		.pl_L = 40/20
	};
	uint64_t group;
	uint32_t unit;
	uint32_t obj;
	struct src_addr src;
	struct tgt_addr tgt;
	int R;
	int C;
	int i;
	struct src_addr (*map)[P];
	uint32_t incidence[P][P];
	uint32_t usage[P][NR + 1];
	uint32_t where[play.pl_N + 2*play.pl_K];
	const char *brace[NR] = { "[]", "<>", "{}" };
	const char *head[NR + 1] = { "D", "P", "S", "total" };

	R = atoi(argv[1]);
	C = atoi(argv[2]);
	C2_ALLOC_ARR(map, R);
	C2_ASSERT(map != NULL);

	memset(usage, 0, sizeof usage);
	memset(incidence, 0, sizeof incidence);

	for (group = 0; group < C ; ++group) {
		src.sa_group = group;
		for (unit = 0; unit < play.pl_N + 2*play.pl_K; ++unit) {
			src.sa_unit = unit;
			pdec_layout_map(&play, &src, &tgt);
			if (tgt.ta_unit < R)
				map[tgt.ta_unit][tgt.ta_obj] = src;
			where[unit] = tgt.ta_obj;
			usage[tgt.ta_obj][NR]++;
			usage[tgt.ta_obj][classify(&play, unit)]++;
		}
		for (unit = 0; unit < play.pl_N + 2*play.pl_K; ++unit) {
			for (i = 0; i < play.pl_N + 2*play.pl_K; ++i)
				incidence[where[unit]][where[i]]++;
		}
	}
	printf("map: \n");
	for (unit = 0; unit < R; ++unit) {
		printf("%5i : ", (int)unit);
		for (obj = 0; obj < P; ++obj) {
			int d;

			d = classify(&play, map[unit][obj].sa_unit);
			printf("%c%2i, %1i%c ", 
			       brace[d][0],
			       (int)map[unit][obj].sa_group, 
			       (int)map[unit][obj].sa_unit,
			       brace[d][1]);
		}
		printf("\n");
	}
	printf("usage : \n");
	for (i = 0; i < NR + 1; ++i) {
		printf("%5s : ", head[i]);
		for (obj = 0; obj < P; ++obj)
			printf("%7i ", usage[obj][i]);
		printf("\n");
	}
	printf("\nincidence:\n");
	for (obj = 0; obj < P; ++obj) {
		for (i = 0; i < P; ++i)
			printf("%5i ", incidence[obj][i]);
		printf("\n");
	}
	return 0;
}

/** @} end of layout group */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
