/* player/race-house */
/*
 * Check race data, the starting kit, and the age, height, weight, history
 * and name rolls made at birth.
 */

#include "unit-test.h"
#include "test-utils.h"
#include "init.h"
#include "obj-gear.h"
#include "obj-knowledge.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "player.h"
#include "player-birth.h"
#include "z-rand.h"

int setup_tests(void **state) {
	set_file_paths();
	init_angband();
#ifdef UNIX
	/* Necessary for creating the randart file. */
	create_needed_dirs();
#endif
	*state = 0;
	return 0;
}

int teardown_tests(void *state) {
	cleanup_angband();
	return 0;
}

static struct player_race *find_race(const char *name)
{
	struct player_race *r;

	for (r = races; r; r = r->next) {
		if (streq(r->name, name)) return r;
	}
	return NULL;
}

static struct player_house *find_house(const char *name)
{
	struct player_house *h;

	for (h = houses; h; h = h->next) {
		if (streq(h->name, name)) return h;
	}
	return NULL;
}

static int test_race_data(void *state) {
	struct player_race *r = find_race("Noldor");

	/* Sil-Q lib/edit/race.txt:53-56 (Noldor), 76-79 (Sindar),
	 * 98-101 (Naugrim), 119-121 (Edain); SNG_AFFINITY is song +1 */
	require(r);
	eq(r->base_hgt, 78);
	eq(r->mod_hgt, 3);
	eq(r->base_wgt, 150);
	eq(r->mod_wgt, 8);
	eq(r->skill_adj[SKILL_ARCHERY], 0);
	eq(r->skill_adj[SKILL_SONG], 1);
	require(pf_has(r->pflags, PF_BOW_PROFICIENCY));
	require(!pf_has(r->pflags, PF_BLADE_PROFICIENCY));

	r = find_race("Sindar");
	require(r);
	eq(r->base_hgt, 76);
	eq(r->mod_hgt, 3);
	eq(r->base_wgt, 148);
	eq(r->mod_wgt, 8);
	eq(r->skill_adj[SKILL_ARCHERY], 0);
	eq(r->skill_adj[SKILL_SONG], 1);
	require(pf_has(r->pflags, PF_BOW_PROFICIENCY));
	require(!pf_has(r->pflags, PF_BLADE_PROFICIENCY));

	r = find_race("Naugrim");
	require(r);
	eq(r->base_hgt, 54);
	eq(r->mod_hgt, 2);
	eq(r->base_wgt, 155);
	eq(r->mod_wgt, 10);

	r = find_race("Edain");
	require(r);
	eq(r->base_hgt, 70);
	eq(r->mod_hgt, 4);
	eq(r->base_wgt, 150);
	eq(r->mod_wgt, 12);
	ok;
}

static int test_outfit(void *state) {
	struct object *weapon, *light, *obj;
	int torches = 0;

	/* Sil-Q race.txt:122-124 (E: lines), birth.c:576-650 player_outfit */
	require(player_make_simple("Edain", "House of Beor", NULL, "Tester"));
	weapon = equipped_item_by_slot_name(player, "weapon");
	require(weapon);
	require(streq(weapon->kind->name, "& Curved Sword~"));
	eq(weapon->number, 1);
	require(object_is_known(weapon));
	light = equipped_item_by_slot_name(player, "light");
	require(light);
	require(tval_is_light(light));
	eq(light->number, 1);
	for (obj = player->gear; obj; obj = obj->next) {
		if (obj->kind == light->kind) torches += obj->number;
	}
	eq(torches, 3);
	ok;
}

static int test_ahw(void *state) {
	struct player_race *r = find_race("Edain");
	int i, min_age = 1000, max_age = 0;
	long wt_sum = 0;

	require(r);
	player->race = r;
	Rand_state_init(1);
	for (i = 0; i < 2000; i++) {
		get_ahw(player);
		if (player->age < min_age) min_age = player->age;
		if (player->age > max_age) max_age = player->age;
		eq(player->wt, player->wt_birth);
		wt_sum += player->wt;
	}
	/* Sil-Q birth.c:248 rand_range(b_age, m_age); race.txt:118 I:21:15:45 */
	eq(min_age, 15);
	eq(max_age, 45);

	/* Sil-Q birth.c:252-255; mean weight is 150 + 70 / 5 = 164; standard error is about 0.3 */
	require(wt_sum >= 2000 * 163 && wt_sum <= 2000 * 165);
	ok;
}

/*
 * Count Noldor histories with golden hair; named counts those naming the
 * house, other those naming any other house.
 */
static int count_golden(const char *house, int *named, int *other)
{
	int i, golden = 0;

	player->race = find_race("Noldor");
	player->house = find_house(house);
	*named = 0;
	*other = 0;
	for (i = 0; i < 2000; i++) {
		char *hist = get_history(player->race->history, player);
		char buf[40];

		strnfmt(buf, sizeof(buf), "from the house of %s.",
			player->house->short_name);
		if (strstr(hist, buf)) {
			(*named)++;
		} else if (strstr(hist, "from the house of")) {
			(*other)++;
		}
		if (strstr(hist, "golden hair")) golden++;
		string_free(hist);
	}
	return golden;
}

static int test_history_house(void *state) {
	int named, other, golden;

	Rand_state_init(1);

	/* Sil-Q birth.c:96-127 get_history_aux; history.txt:60-70, 215-226 */
	/* Chart 3 is reached on a roll of 1-95 in chart 1 */
	golden = count_golden("House of Finarfin", &named, &other);
	require(named >= 1850 && named <= 1950);
	eq(other, 0);

	/* Finarfin: golden on 1-95 */
	require(golden >= 1850 && golden <= 1950);

	/* Other Noldor: golden on 89-95 */
	golden = count_golden("House of Feanor", &named, &other);
	require(named >= 1850 && named <= 1950);
	eq(other, 0);
	require(golden >= 90 && golden <= 190);
	golden = count_golden("House of Fingolfin", &named, &other);
	require(named >= 1850 && named <= 1950);
	eq(other, 0);
	require(golden >= 90 && golden <= 190);
	ok;
}

static int test_random_name(void *state) {
	int i, min_len = 100, max_len = 0;

	Rand_state_init(1);
	for (i = 0; i < 1000; i++) {
		char buf[32];
		int len = (int) player_random_name(buf, sizeof(buf));

		eq(len, (int) strlen(buf));
		if (len < min_len) min_len = len;
		if (len > max_len) max_len = len;
	}
	/* Sil-Q randart.c:18-19 MIN_NAME_LEN 5, MAX_NAME_LEN 9 */
	eq(min_len, 5);
	eq(max_len, 9);
	ok;
}

const char *suite_name = "player/race-house";
struct test tests[] = {
	{ "race-data", test_race_data },
	{ "outfit", test_outfit },
	{ "ahw", test_ahw },
	{ "history-house", test_history_house },
	{ "random-name", test_random_name },
	{ NULL, NULL }
};
