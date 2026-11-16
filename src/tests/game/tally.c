/* game/tally.c */
/*
 * Tally the outcomes of a game routine over many calls, for statistical
 * comparison with the same routine in another build of the game.
 *
 * Run as part of the test suite, each routine is called a few thousand times
 * with fixed arguments and its results are checked against their bounds.
 *
 * With KNARR_TALLY set in the environment, the one routine it names is run
 * and its tally printed instead:
 *
 *   KNARR_TALLY="[-s seed] <routine> <iterations> [args...]"
 *   KNARR_TALLY="-l"                     (list routines)
 *
 * Output, one value per line in ascending order:
 *
 *   # routine <name> <args...>
 *   # iterations <n>
 *   # seed <seed>
 *   <value> <count>
 *
 * To add a routine, write a function returning one integer outcome per call
 * and add a row to routines[].
 */

#include "unit-test.h"
#include "test-utils.h"
#include "combat.h"
#include "init.h"
#include "player.h"
#include "player-birth.h"
#include "source.h"
#include "z-rand.h"
#include "z-virt.h"

/*
 * hit <att> <evn> <cursed> <side>
 * Result of hit_roll().  side 0: the player attacks; side 1: something
 * attacks the player.  cursed sets player->cursed.
 */
static int r_hit(const int *a)
{
	player->cursed = a[2] ? true : false;
	if (a[3] == 0)
		return hit_roll(a[0], a[1], source_player(), source_none(), false);
	return hit_roll(a[0], a[1], source_none(), source_player(), false);
}

/*
 * damroll <dice> <sides>
 * Result of damroll().
 */
static int r_damroll(const int *a)
{
	return damroll(a[0], a[1]);
}

struct tally_routine {
	const char *name;
	int nargs;
	int (*fn)(const int *args);
	const char *usage;
	/* Arguments and inclusive bounds for the self-check in the test suite */
	int check_args[16];
	int lo, hi;
};

static const struct tally_routine routines[] = {
	{ "hit", 4, r_hit,
	  "<att> <evn> <cursed 0|1> <side 0=player attacks|1=player defends>",
	  { 5, 3, 0, 0 }, 5 + 1 - (3 + 20), 5 + 20 - (3 + 1) },
	{ "damroll", 2, r_damroll, "<dice> <sides>",
	  { 2, 4 }, 2, 8 },
	{ NULL, 0, NULL, NULL, { 0 }, 0, 0 }
};

#define TALLY_MIN (-10000)
#define TALLY_MAX 10000

int setup_tests(void **state) {
	set_file_paths();
	init_angband();
	if (!player_make_simple(NULL, NULL, NULL, "Tester")) {
		cleanup_angband();
		return 1;
	}
	return 0;
}

int teardown_tests(void *state) {
	cleanup_angband();
	return 0;
}

/* Print a tally as requested by KNARR_TALLY; returns false on bad input */
static bool run_tally(const char *spec)
{
	char *buf = string_make(spec), *tok, *save = NULL;
	char *words[24];
	int nw = 0, i, w = 0, args[16];
	uint32_t seed = (uint32_t) time(NULL);
	const struct tally_routine *r = NULL;
	long iters, n;
	long *counts;

	for (tok = strtok_r(buf, " \t", &save); tok && nw < 24;
		 tok = strtok_r(NULL, " \t", &save))
		words[nw++] = tok;

	if (nw >= 1 && streq(words[0], "-l")) {
		for (i = 0; routines[i].name; i++)
			printf("%s %s\n", routines[i].name, routines[i].usage);
		string_free(buf);
		return true;
	}
	if (nw >= 2 && streq(words[0], "-s")) {
		seed = (uint32_t) strtoul(words[1], NULL, 10);
		w = 2;
	}
	if (w + 2 <= nw) {
		for (i = 0; routines[i].name; i++)
			if (streq(routines[i].name, words[w])) r = &routines[i];
	}
	if (!r || nw - w - 2 != r->nargs) {
		fprintf(stderr, "usage: KNARR_TALLY=\"[-s seed] <routine> "
				"<iterations> [args...]\"\n");
		for (i = 0; routines[i].name; i++)
			fprintf(stderr, "  %s %s\n", routines[i].name,
					routines[i].usage);
		string_free(buf);
		return false;
	}
	iters = atol(words[w + 1]);
	for (i = 0; i < r->nargs; i++)
		args[i] = atoi(words[w + 2 + i]);

	Rand_quick = false;
	Rand_state_init(seed);

	counts = mem_zalloc((TALLY_MAX - TALLY_MIN + 1) * sizeof(*counts));
	for (n = 0; n < iters; n++) {
		int v = r->fn(args);
		if (v < TALLY_MIN || v > TALLY_MAX) {
			fprintf(stderr, "tally: value %d out of range\n", v);
			mem_free(counts);
			string_free(buf);
			return false;
		}
		counts[v - TALLY_MIN]++;
	}

	printf("# routine %s", r->name);
	for (i = 0; i < r->nargs; i++)
		printf(" %d", args[i]);
	printf("\n# iterations %ld\n", iters);
	printf("# seed %lu\n", (unsigned long) seed);
	for (i = 0; i <= TALLY_MAX - TALLY_MIN; i++)
		if (counts[i])
			printf("%d %ld\n", i + TALLY_MIN, counts[i]);
	fflush(stdout);

	mem_free(counts);
	string_free(buf);
	return true;
}

static int test_tally(void *state) {
	const char *spec = getenv("KNARR_TALLY");
	int i;

	if (spec) {
		require(run_tally(spec));
		ok;
	}

	/* Self-check: every routine stays within its bounds, and reaches both */
	Rand_quick = false;
	Rand_state_init(1);
	for (i = 0; routines[i].name; i++) {
		const struct tally_routine *r = &routines[i];
		bool seen_lo = false, seen_hi = false;
		int n;

		for (n = 0; n < 200000; n++) {
			int v = r->fn(r->check_args);
			require(v >= r->lo && v <= r->hi);
			if (v == r->lo) seen_lo = true;
			if (v == r->hi) seen_hi = true;
		}
		require(seen_lo && seen_hi);
	}
	ok;
}

const char *suite_name = "game/tally";
struct test tests[] = {
	{ "tally", test_tally },
	{ NULL, NULL }
};
