#ifndef RQ_MODLOADER_ORDER_H
#define RQ_MODLOADER_ORDER_H

/* Manifest order. Hard edges, then priority, then id. No loadorder.txt. */

#define RQ_ORDER_MAX 64
#define RQ_ID_LEN 64
#define RQ_EDGE_MAX 16

enum rq_skip {
	RQ_SKIP_NONE = 0,
	RQ_SKIP_VERSION = 1,
	RQ_SKIP_CONFLICT = 2,
	RQ_SKIP_DEP = 3,
	RQ_SKIP_CYCLE = 4,
	RQ_SKIP_LOADER = 5,
	RQ_SKIP_DUP = 6
};

struct rq_mod_node {
	char id[RQ_ID_LEN];
	int priority;
	int ndeps, nafter, nbefore, nconf;
	char deps[RQ_EDGE_MAX][RQ_ID_LEN];
	char after[RQ_EDGE_MAX][RQ_ID_LEN];
	char before[RQ_EDGE_MAX][RQ_ID_LEN];
	char conf[RQ_EDGE_MAX][RQ_ID_LEN];
	char loader[32];
	int skip;
	char why_id[RQ_ID_LEN];
};

/* loader_ver is this build, e.g. "0.2.0".
 * order_out receives indexes to load, hard edges first.
 * Among ready mods, lower priority loads first (higher priority later),
 * then alphabetical id. Returns how many load.
 * skip is set on refused mods. why_id names the other id when there is one.
 */
int rq_order_mods(struct rq_mod_node *mods, int n, const char *loader_ver,
		int *order_out);

/* 1 if both parse. Result <0 when a is older than b. */
int rq_semver_cmp(const char *a, const char *b, int *ok);

#endif
