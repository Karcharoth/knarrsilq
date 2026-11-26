/* cave/vault */
/* Tests for vault selection and vault symbols */

#include "unit-test.h"
#include "test-utils.h"
#include "angband.h"
#include "cave.h"
#include "generate.h"
#include "init.h"
#include "mon-make.h"
#include "mon-spell.h"
#include "monster.h"
#include "player-birth.h"
#include "source.h"
#include "mon-util.h"
#include "trap.h"

int setup_tests(void **state) {
	set_file_paths();
	if (!init_angband()) {
		return 1;
	}
	if (!player_make_simple(NULL, NULL, "Tester")) {
		cleanup_angband();
		return 1;
	}
	return 0;
}

int teardown_tests(void *state) {
	cleanup_angband();
	return 0;
}

static struct chunk *create_rock_cave(int height, int width, int depth) {
	struct chunk *c = cave_new(height, width);
	struct loc grid;

	for (grid.y = 0; grid.y < height; ++grid.y) {
		for (grid.x = 0; grid.x < width; ++grid.x) {
			square_set_feat(c, grid, FEAT_GRANITE);
		}
	}
	c->depth = depth;
	return c;
}

static struct vault *find_vault(const char *name) {
	struct vault *v;

	for (v = vaults; v; v = v->next) {
		if (streq(v->name, name)) return v;
	}
	return NULL;
}

/*
 * Compare the share of SURFACE rooms among chosen interesting rooms with
 * the share given by weights 1/rarity, quartered for other rooms at depths
 * below 6, and 1/(rarity + 2^depth) for SURFACE rooms deeper down.
 */
static bool surface_share_ok(int depth) {
	const int n = 200000;
	double total = 0.0, surface = 0.0, expected, observed;
	struct vault *v;
	int i, count = 0;

	for (v = vaults; v; v = v->next) {
		double w;

		if (!streq(v->typ, "Interesting room") || v->depth > depth
				|| !v->base_rarity) continue;
		w = 1.0 / v->base_rarity;
		if (roomf_has(v->flags, ROOMF_SURFACE)) {
			if (depth >= 6) w = 1.0 / (v->base_rarity + (1UL << depth));
			surface += w;
		} else if (depth < 6) {
			w /= 4;
		}
		total += w;
	}
	expected = surface / total;

	for (i = 0; i < n; i++) {
		v = random_vault(depth, "Interesting room", false);
		if (v && roomf_has(v->flags, ROOMF_SURFACE)) count++;
	}
	observed = (double) count / n;
	/* Within five standard deviations */
	return (observed - expected) * (observed - expected)
		< 25 * expected * (1 - expected) / n;
}

static int test_surface_shallow(void *state) {
	require(surface_share_ok(2));
	ok;
}

static int test_surface_deep(void *state) {
	require(surface_share_ok(8));
	ok;
}

/* Sunlight symbol, and thrall symbols with no thrall races */
static int test_sunlight_and_thralls(void *state) {
	struct chunk *c = create_rock_cave(30, 30, 2);
	struct loc centre = loc(15, 15);
	struct vault *v = find_vault("Small sunlit shaft");
	struct loc grid;
	int sun = 0;

	require(v);
	cave = c;
	require(build_vault(c, centre, v, false));
	for (grid.y = 0; grid.y < c->height; grid.y++) {
		for (grid.x = 0; grid.x < c->width; grid.x++) {
			if (square(c, grid)->feat == FEAT_SUNLIGHT) sun++;
		}
	}
	eq(sun, 1);
	require(square(c, centre)->feat == FEAT_SUNLIGHT);
	cave = NULL;
	cave_free(c);

	/* With no thrall races in the data, the squares stay empty floor */
	c = create_rock_cave(30, 30, 2);
	cave = c;
	v = find_vault("Thrall pit 1");
	require(v);
	require(!lookup_monster("Dejected human thrall"));
	require(build_vault(c, centre, v, false));
	for (grid.y = 0; grid.y < c->height; grid.y++) {
		for (grid.x = 0; grid.x < c->width; grid.x++) {
			require(!square(c, grid)->mon);
		}
	}
	cave = NULL;
	cave_free(c);
	ok;
}

/* Webs fall only on bare floor, and webbed vaults get no extra traps */
static int test_webs(void *state) {
	struct vault *v = find_vault("Spider cave");
	struct loc centre = loc(15, 15);
	int i, webs = 0;

	require(v);
	for (i = 0; i < 50; i++) {
		struct chunk *c = create_rock_cave(30, 30, 8);
		struct loc grid;

		cave = c;
		require(build_vault(c, centre, v, false));
		for (grid.y = 0; grid.y < c->height; grid.y++) {
			for (grid.x = 0; grid.x < c->width; grid.x++) {
				struct trap *trap = square_trap(c, grid);

				for (; trap; trap = trap->next) {
					require(trap->kind == lookup_trap("web"));
					require(square(c, grid)->feat == FEAT_FLOOR);
					require(!square_object(c, grid));
					require(!square(c, grid)->mon);
					webs++;
				}
			}
		}
		cave = NULL;
		cave_free(c);
	}
	require(webs > 0);
	ok;
}

/* The archer symbol chooses monsters with arrows */
static int test_archers(void *state) {
	struct loc grid = loc(5, 5);
	int i;

	for (i = 0; i < 50; i++) {
		struct chunk *c = create_rock_cave(11, 11, 10);
		struct monster *mon;

		cave = c;
		square_set_feat(c, grid, FEAT_FLOOR);
		place_monster_by_flag(c, grid, RSF_ARROW1, RSF_ARROW2, true, 11,
							  true);
		mon = square_monster(c, grid);
		require(mon);
		require(rsf_has(mon->race->spell_flags, RSF_ARROW1) ||
				rsf_has(mon->race->spell_flags, RSF_ARROW2));
		cave = NULL;
		cave_free(c);
	}
	ok;
}

const char *suite_name = "cave/vault";
struct test tests[] = {
	{ "surface_shallow", test_surface_shallow },
	{ "surface_deep", test_surface_deep },
	{ "sunlight_and_thralls", test_sunlight_and_thralls },
	{ "webs", test_webs },
	{ "archers", test_archers },
	{ NULL, NULL }
};
