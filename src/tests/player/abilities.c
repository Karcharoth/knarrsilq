/* player/abilities */
/* The ability trees and the rules for gaining and holding abilities. */

#include "unit-test.h"
#include "test-utils.h"
#include "cave.h"
#include "combat.h"
#include "game-input.h"
#include "game-world.h"
#include "generate.h"
#include "init.h"
#include "mon-make.h"
#include "mon-util.h"
#include "obj-gear.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-abilities.h"
#include "player-birth.h"
#include "player-calcs.h"
#include "player-history.h"

static bool always_yes(const char *prompt)
{
	return true;
}

int setup_tests(void **state) {
	set_file_paths();
	init_angband();
#ifdef UNIX
	create_needed_dirs();
#endif
	if (!player_make_simple(NULL, NULL, "Tester")) {
		cleanup_angband();
		return 1;
	}
	prepare_next_level(player);
	on_new_level();
	get_check_hook = always_yes;
	return 0;
}

int teardown_tests(void *state) {
	wipe_mon_list(cave, player);
	cleanup_angband();
	return 0;
}

static struct object *make_obj(int tval, const char *name)
{
	struct object_kind *kind = lookup_kind(tval, lookup_sval(tval, name));
	struct object *obj;

	if (!kind) return NULL;
	obj = object_new();
	object_prep(obj, kind, 0, MINIMISE);
	obj->number = 1;
	object_know(obj);
	return obj;
}

static void give_ability(int skill, const char *name)
{
	struct ability *ability = lookup_ability(skill, name);

	add_ability(&player->abilities, ability);
	locate_ability(player->abilities, ability)->active = true;
}

static void take_ability(int skill, const char *name)
{
	remove_ability(&player->abilities, lookup_ability(skill, name));
}

/* The Melee tree holds Sil-Q's abilities in Sil-Q's order (ability.txt) */
static int test_melee_tree(void *state) {
	const char *names[] = { "Power", "Finesse", "Knock Back",
		"Polearm Mastery", "Charge", "Follow-Through", "Impale", "Subtlety",
		"Whirlwind Attack", "Zone of Control", "Smite",
		"Two Weapon Fighting", "Rapid Attack", "Strength" };
	struct ability *a;
	size_t i = 0;

	for (a = abilities; a; a = a->next) {
		if (a->skill != SKILL_MELEE) continue;
		require(i < N_ELEMENTS(names));
		require(streq(a->name, names[i]));
		i++;
	}
	eq(i, N_ELEMENTS(names));
	eq(lookup_ability(SKILL_MELEE, "Knock Back")->level, 3);
	eq(lookup_ability(SKILL_PERCEPTION, "Quick Study")->level, 1);
	eq(lookup_ability(SKILL_SONG, "Grace")->level, 12);
	null(lookup_ability(SKILL_MELEE, "Throwing Mastery"));
	null(lookup_ability(SKILL_PERCEPTION, "Lore-Master"));
	ok;
}

/* Prerequisites are kept in file order: Follow-Through is Power or Finesse */
static int test_prereq_order(void *state) {
	struct ability *a = lookup_ability(SKILL_MELEE, "Follow-Through");
	struct ability *pre = a->prerequisites;

	notnull(pre);
	require(streq(pre->name, "Power"));
	notnull(pre->next);
	require(streq(pre->next->name, "Finesse"));
	null(pre->next->next);
	ok;
}

/* Quick Study waives prerequisite abilities while it is active */
static int test_quick_study(void *state) {
	struct ability *a = lookup_ability(SKILL_MELEE, "Follow-Through");

	require(!player_has_prereq_abilities(player, a));
	give_ability(SKILL_PERCEPTION, "Quick Study");
	require(player_has_prereq_abilities(player, a));
	locate_ability(player->abilities,
		lookup_ability(SKILL_PERCEPTION, "Quick Study"))->active = false;
	require(!player_has_prereq_abilities(player, a));
	take_ability(SKILL_PERCEPTION, "Quick Study");
	ok;
}

/* Any polearm may carry Polearm Mastery; other weapons may not */
static int test_polearm_applicable(void *state) {
	struct ability *a = lookup_ability(SKILL_MELEE, "Polearm Mastery");
	struct object *spear = make_obj(TV_POLEARM, "Spear");
	struct object *axe = make_obj(TV_POLEARM, "Battle Axe");
	struct object *gloves = make_obj(TV_GLOVES, "Set of Gloves");

	notnull(spear);
	notnull(axe);
	notnull(gloves);
	require(applicable_ability(a, spear));
	require(!applicable_ability(a, axe));
	require(applicable_ability(a, gloves));
	object_free(spear);
	object_free(axe);
	object_free(gloves);
	ok;
}

/* Taking off one of two items granting an ability keeps the ability */
static int test_two_items_same_ability(void *state) {
	struct ability *a = lookup_ability(SKILL_PERCEPTION, "Keen Senses");
	struct object *ring1 = make_obj(TV_RING, "Accuracy");
	struct object *ring2 = make_obj(TV_RING, "Accuracy");

	notnull(ring1);
	notnull(ring2);
	add_ability(&ring1->abilities, a);
	add_ability(&ring2->abilities, a);
	gear_insert_end(player, ring1);
	gear_insert_end(player, ring2);
	inven_wield(ring1, wield_slot(ring1));
	inven_wield(ring2, wield_slot(ring2));
	require(object_is_equipped(player->body, ring1));
	require(object_is_equipped(player->body, ring2));
	require(player_active_ability(player, "Keen Senses"));
	inven_takeoff(ring1);
	require(player_active_ability(player, "Keen Senses"));
	inven_takeoff(ring2);
	require(!player_active_ability(player, "Keen Senses"));
	ok;
}

/* Gaining an ability is recorded in the history */
static int test_gain_note(void *state) {
	struct ability *a = lookup_ability(SKILL_MELEE, "Power");
	struct history_info *h;
	size_t n;

	player->new_exp = 10000;
	require(player_gain_ability(player, a));
	n = history_get_list(player, &h);
	require(n > 0);
	require(streq(h[n - 1].event, "(Power)"));
	take_ability(SKILL_MELEE, "Power");
	ok;
}

/* Outwit negates the critical dice of an attacking monster that it beats */
static int test_outwit(void *state) {
	struct monster_race *race = lookup_monster("Wolf");
	struct monster *mon;
	int old = player->state.skill_use[SKILL_PERCEPTION];

	notnull(race);
	mon = mem_zalloc(sizeof(*mon));
	mon->race = race;
	player->state.skill_use[SKILL_PERCEPTION] = 100;
	eq(crit_bonus(player, 16, 30, NULL, SKILL_MELEE, false, mon), 1);
	give_ability(SKILL_PERCEPTION, "Outwit");
	eq(crit_bonus(player, 16, 30, NULL, SKILL_MELEE, false, mon), 0);
	eq(crit_bonus(player, 16, 30, NULL, SKILL_MELEE, false, NULL), 1);
	take_ability(SKILL_PERCEPTION, "Outwit");
	player->state.skill_use[SKILL_PERCEPTION] = old;
	mem_free(mon);
	ok;
}

const char *suite_name = "player/abilities";
struct test tests[] = {
	{ "melee tree", test_melee_tree },
	{ "prerequisite order", test_prereq_order },
	{ "quick study", test_quick_study },
	{ "polearm applicable", test_polearm_applicable },
	{ "two items same ability", test_two_items_same_ability },
	{ "gain note", test_gain_note },
	{ "outwit", test_outwit },
	{ NULL, NULL }
};
