#include <string.h>
#include <stdio.h>

#include "modloader_order.h"

static int parse_num(const char **ps, int *out)
{
	const char *s = *ps;
	int v = 0;
	if (*s < '0' || *s > '9')
		return 0;
	while (*s >= '0' && *s <= '9') {
		if (v > 1000000)
			return 0;
		v = v * 10 + (*s - '0');
		s++;
	}
	*out = v;
	*ps = s;
	return 1;
}

static int semver_parse(const char *s, int *maj, int *min, int *pat)
{
	*maj = *min = *pat = 0;
	if (!s || !s[0])
		return 0;
	if (!parse_num(&s, maj))
		return 0;
	if (*s == '.') {
		s++;
		if (!parse_num(&s, min))
			return 0;
		if (*s == '.') {
			s++;
			if (!parse_num(&s, pat))
				return 0;
		}
	}
	return *s == 0;
}

int rq_semver_cmp(const char *a, const char *b, int *ok)
{
	int amaj, amin, apat, bmaj, bmin, bpat;
	if (!semver_parse(a, &amaj, &amin, &apat) || !semver_parse(b, &bmaj, &bmin, &bpat)) {
		if (ok)
			*ok = 0;
		return 0;
	}
	if (ok)
		*ok = 1;
	if (amaj != bmaj)
		return amaj < bmaj ? -1 : 1;
	if (amin != bmin)
		return amin < bmin ? -1 : 1;
	if (apat != bpat)
		return apat < bpat ? -1 : 1;
	return 0;
}

static int same_id(const struct rq_mod_node *m, const char *id)
{
	return id && id[0] && !strcmp(m->id, id);
}

static int find_live(const struct rq_mod_node *mods, int n, const char *id)
{
	int i;
	for (i = 0; i < n; i++) {
		if (mods[i].skip == RQ_SKIP_NONE && same_id(&mods[i], id))
			return i;
	}
	return -1;
}

static void mark(struct rq_mod_node *m, int why, const char *other)
{
	m->skip = why;
	m->why_id[0] = 0;
	if (other && other[0])
		snprintf(m->why_id, sizeof m->why_id, "%s", other);
}

static void gate(struct rq_mod_node *mods, int n, const char *loader_ver)
{
	int i, j;
	for (i = 0; i < n; i++) {
		mods[i].skip = RQ_SKIP_NONE;
		mods[i].why_id[0] = 0;
	}
	for (i = 0; i < n; i++) {
		for (j = 0; j < i; j++) {
			if (mods[j].skip == RQ_SKIP_DUP)
				continue;
			if (mods[i].id[0] && !strcmp(mods[i].id, mods[j].id)) {
				mark(&mods[i], RQ_SKIP_DUP, mods[i].id);
				break;
			}
		}
	}
	for (i = 0; i < n; i++) {
		int ok = 0, cmp;
		if (mods[i].skip != RQ_SKIP_NONE)
			continue;
		if (!mods[i].loader[0])
			continue;
		cmp = rq_semver_cmp(mods[i].loader, loader_ver ? loader_ver : "0.0.0", &ok);
		if (!ok)
			mark(&mods[i], RQ_SKIP_LOADER, mods[i].loader);
		else if (cmp > 0)
			mark(&mods[i], RQ_SKIP_VERSION, mods[i].loader);
	}
	for (j = 0; j < n + 2; j++) {
		int changed = 0;
		for (i = 0; i < n; i++) {
			int c;
			if (mods[i].skip != RQ_SKIP_NONE)
				continue;
			for (c = 0; c < mods[i].nconf; c++) {
				int o = find_live(mods, n, mods[i].conf[c]);
				if (o >= 0 && o != i) {
					mark(&mods[i], RQ_SKIP_CONFLICT, mods[i].conf[c]);
					changed = 1;
					break;
				}
			}
		}
		for (i = 0; i < n; i++) {
			int d;
			if (mods[i].skip != RQ_SKIP_NONE)
				continue;
			for (d = 0; d < mods[i].ndeps; d++) {
				if (find_live(mods, n, mods[i].deps[d]) < 0) {
					mark(&mods[i], RQ_SKIP_DEP, mods[i].deps[d]);
					changed = 1;
					break;
				}
			}
		}
		if (!changed)
			break;
	}
}

static void add_edge(int to[][RQ_ORDER_MAX], int *nto, int *indeg, int from, int dst)
{
	int k;
	if (from < 0 || dst < 0)
		return;
	/* A self edge never clears, so Kahn refuses that mod. */
	if (from == dst) {
		indeg[dst]++;
		return;
	}
	for (k = 0; k < nto[from]; k++) {
		if (to[from][k] == dst)
			return;
	}
	if (nto[from] >= RQ_ORDER_MAX)
		return;
	to[from][nto[from]++] = dst;
	indeg[dst]++;
}

int rq_order_mods(struct rq_mod_node *mods, int n, const char *loader_ver,
		int *order_out)
{
	int to[RQ_ORDER_MAX][RQ_ORDER_MAX];
	int nto[RQ_ORDER_MAX];
	int indeg[RQ_ORDER_MAX];
	int taken[RQ_ORDER_MAX];
	int i, outn = 0;

	if (n > RQ_ORDER_MAX)
		n = RQ_ORDER_MAX;
	if (n < 0)
		n = 0;
	gate(mods, n, loader_ver);
	memset(to, 0, sizeof to);
	memset(nto, 0, sizeof nto);
	memset(indeg, 0, sizeof indeg);
	memset(taken, 0, sizeof taken);

	for (i = 0; i < n; i++) {
		int k, o;
		if (mods[i].skip != RQ_SKIP_NONE)
			continue;
		for (k = 0; k < mods[i].ndeps; k++) {
			o = find_live(mods, n, mods[i].deps[k]);
			add_edge(to, nto, indeg, o, i);
		}
		for (k = 0; k < mods[i].nafter; k++) {
			o = find_live(mods, n, mods[i].after[k]);
			if (o >= 0)
				add_edge(to, nto, indeg, o, i);
		}
		for (k = 0; k < mods[i].nbefore; k++) {
			o = find_live(mods, n, mods[i].before[k]);
			if (o >= 0)
				add_edge(to, nto, indeg, i, o);
		}
	}

	for (;;) {
		int best = -1;
		for (i = 0; i < n; i++) {
			if (mods[i].skip != RQ_SKIP_NONE || taken[i] || indeg[i] != 0)
				continue;
			if (best < 0 || mods[i].priority < mods[best].priority
					|| (mods[i].priority == mods[best].priority
						&& strcmp(mods[i].id, mods[best].id) < 0))
				best = i;
		}
		if (best < 0)
			break;
		taken[best] = 1;
		order_out[outn++] = best;
		for (i = 0; i < nto[best]; i++) {
			int dst = to[best][i];
			if (indeg[dst] > 0)
				indeg[dst]--;
		}
	}
	for (i = 0; i < n; i++) {
		if (mods[i].skip == RQ_SKIP_NONE && !taken[i])
			mark(&mods[i], RQ_SKIP_CYCLE, mods[i].id);
	}
	return outn;
}

#ifdef RQ_ORDER_TEST
static void zero(struct rq_mod_node *m, const char *id, int priority)
{
	memset(m, 0, sizeof *m);
	snprintf(m->id, sizeof m->id, "%s", id);
	m->priority = priority;
}

static void add_list(char dest[][RQ_ID_LEN], int *n, const char *id)
{
	snprintf(dest[*n], RQ_ID_LEN, "%s", id);
	(*n)++;
}

static int expect_order(const char *title, struct rq_mod_node *m, int n,
		const char *want)
{
	int order[RQ_ORDER_MAX];
	char got[256];
	int i, used = 0, count;
	count = rq_order_mods(m, n, "0.2.0", order);
	got[0] = 0;
	for (i = 0; i < count; i++) {
		used += snprintf(got + used, sizeof got - used, "%s%s",
				i ? "," : "", m[order[i]].id);
	}
	if (strcmp(got, want) != 0) {
		printf("FAIL %s\n  got  %s\n  want %s\n", title, got, want);
		return 1;
	}
	printf("ok %s -> %s\n", title, got);
	return 0;
}

int main(void)
{
	int fails = 0, ok = 0;
	struct rq_mod_node m[8];

	if (rq_semver_cmp("0.2.0", "0.2.0", &ok) != 0 || !ok)
		fails++;
	if (rq_semver_cmp("0.2", "0.2.0", &ok) != 0 || !ok)
		fails++;
	if (rq_semver_cmp("0.10.0", "0.9.0", &ok) <= 0 || !ok)
		fails++;
	if (rq_semver_cmp("0.2.0", "0.2.1", &ok) >= 0 || !ok)
		fails++;
	if (rq_semver_cmp("1.0.0", "0.2.0", &ok) <= 0 || !ok)
		fails++;
	if (rq_semver_cmp("0.2.0-beta", "0.2.0", &ok) != 0 || ok)
		fails++;
	if (fails)
		printf("FAIL semver\n");
	else
		printf("ok semver\n");

	memset(m, 0, sizeof m);
	zero(&m[0], "a", 0);
	zero(&m[1], "b", 0);
	zero(&m[2], "c", 0);
	fails += expect_order("alpha", m, 3, "a,b,c");

	memset(m, 0, sizeof m);
	zero(&m[0], "a", 5);
	zero(&m[1], "b", 1);
	zero(&m[2], "c", 1);
	fails += expect_order("priority", m, 3, "b,c,a");

	memset(m, 0, sizeof m);
	zero(&m[0], "a", 0);
	zero(&m[1], "b", 10);
	add_list(m[0].deps, &m[0].ndeps, "b");
	fails += expect_order("dep beats priority", m, 2, "b,a");

	memset(m, 0, sizeof m);
	zero(&m[0], "a", 0);
	zero(&m[1], "b", 0);
	add_list(m[0].after, &m[0].nafter, "b");
	fails += expect_order("loadAfter", m, 2, "b,a");

	memset(m, 0, sizeof m);
	zero(&m[0], "a", 0);
	zero(&m[1], "b", 0);
	add_list(m[0].before, &m[0].nbefore, "b");
	fails += expect_order("loadBefore", m, 2, "a,b");

	memset(m, 0, sizeof m);
	zero(&m[0], "a", 0);
	zero(&m[1], "b", 0);
	add_list(m[0].after, &m[0].nafter, "missing");
	fails += expect_order("soft loadAfter", m, 2, "a,b");

	memset(m, 0, sizeof m);
	zero(&m[0], "a", 0);
	zero(&m[1], "b", 0);
	add_list(m[0].deps, &m[0].ndeps, "z");
	{
		int order[8], count;
		count = rq_order_mods(m, 2, "0.2.0", order);
		if (count != 1 || strcmp(m[order[0]].id, "b")
				|| m[0].skip != RQ_SKIP_DEP || strcmp(m[0].why_id, "z")) {
			printf("FAIL missing dep\n");
			fails++;
		} else {
			printf("ok missing dep\n");
		}
	}

	memset(m, 0, sizeof m);
	zero(&m[0], "a", 0);
	zero(&m[1], "b", 0);
	add_list(m[0].conf, &m[0].nconf, "b");
	{
		int order[8], count;
		count = rq_order_mods(m, 2, "0.2.0", order);
		if (count != 1 || strcmp(m[order[0]].id, "b")
				|| m[0].skip != RQ_SKIP_CONFLICT || strcmp(m[0].why_id, "b")) {
			printf("FAIL conflict\n");
			fails++;
		} else {
			printf("ok conflict\n");
		}
	}

	memset(m, 0, sizeof m);
	zero(&m[0], "a", 0);
	zero(&m[1], "b", 0);
	zero(&m[2], "c", 0);
	add_list(m[0].deps, &m[0].ndeps, "b");
	add_list(m[1].deps, &m[1].ndeps, "a");
	{
		int order[8], count;
		count = rq_order_mods(m, 3, "0.2.0", order);
		if (count != 1 || strcmp(m[order[0]].id, "c")
				|| m[0].skip != RQ_SKIP_CYCLE || m[1].skip != RQ_SKIP_CYCLE) {
			printf("FAIL cycle\n");
			fails++;
		} else {
			printf("ok cycle\n");
		}
	}

	memset(m, 0, sizeof m);
	zero(&m[0], "new", 0);
	zero(&m[1], "old", 0);
	zero(&m[2], "bad", 0);
	zero(&m[3], "same", 0);
	snprintf(m[0].loader, sizeof m[0].loader, "0.3.0");
	snprintf(m[1].loader, sizeof m[1].loader, "0.1.0");
	snprintf(m[2].loader, sizeof m[2].loader, "nope");
	snprintf(m[3].loader, sizeof m[3].loader, "0.2.0");
	{
		int order[8], count;
		count = rq_order_mods(m, 4, "0.2.0", order);
		if (count != 2 || m[0].skip != RQ_SKIP_VERSION || m[2].skip != RQ_SKIP_LOADER
				|| m[1].skip || m[3].skip) {
			printf("FAIL version gate count=%d skips %d %d %d %d\n",
					count, m[0].skip, m[1].skip, m[2].skip, m[3].skip);
			fails++;
		} else {
			printf("ok version gate\n");
		}
	}

	memset(m, 0, sizeof m);
	zero(&m[0], "a", 0);
	zero(&m[1], "a", 1);
	{
		int order[8], count;
		count = rq_order_mods(m, 2, "0.2.0", order);
		if (count != 1 || m[1].skip != RQ_SKIP_DUP) {
			printf("FAIL dup\n");
			fails++;
		} else {
			printf("ok dup\n");
		}
	}

	if (fails) {
		printf("%d failed\n", fails);
		return 1;
	}
	printf("all passed\n");
	return 0;
}
#endif
