/* player/melee */
/* The player's melee attack and the melee abilities. */

#include "unit-test.h"
#include "test-utils.h"
#include "cave.h"
#include "game-event.h"
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
#include "player-attack.h"
#include "player-birth.h"
#include "player-calcs.h"
#include "player-util.h"
#include "target.h"

static char last_msg[256];
static int n_impale, n_hit, n_smite, n_stop, n_charge;
static int last_dd, last_ds, last_dam;

static void msg_handler(game_event_type type, game_event_data *data,
		void *user)
{
	const char *m = data->message.msg;

	if (!m) return;
	my_strcpy(last_msg, m, sizeof(last_msg));
	if (prefix(m, "You impale")) n_impale++;
	if (prefix(m, "You hit") || prefix(m, "You miss")) n_hit++;
	if (prefix(m, "You smite")) n_smite++;
	if (prefix(m, "You charge")) n_charge++;
	if (prefix(m, "You stop before you bump into")) n_stop++;
}

static void damage_handler(game_event_type type, game_event_data *data,
		void *user)
{
	last_dd = data->combat_damage.dd;
	last_ds = data->combat_damage.ds;
	last_dam = data->combat_damage.dam;
}

static bool always_yes(const char *prompt)
{
	return true;
}

static void reset_counts(void)
{
	n_impale = n_hit = n_smite = n_stop = n_charge = 0;
	last_dd = last_ds = last_dam = -1;
}

int setup_tests(void **state) {
	int i;

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
	for (i = 0; i < player->body.count; i++) {
		player->body.slots[i].obj = NULL;
	}
	get_check_hook = always_yes;
	event_add_handler(EVENT_MESSAGE, msg_handler, NULL);
	event_add_handler(EVENT_COMBAT_DAMAGE, damage_handler, NULL);
	return 0;
}

int teardown_tests(void *state) {
	event_remove_handler(EVENT_MESSAGE, msg_handler, NULL);
	event_remove_handler(EVENT_COMBAT_DAMAGE, damage_handler, NULL);
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

static void wield(struct object *obj)
{
	player->body.slots[slot_by_name(player, "weapon")].obj = obj;
	player->upkeep->update |= (PU_BONUS);
	update_stuff(player);
}

static void give_ability(int skill, const char *name)
{
	struct ability *ability = lookup_ability(skill, name);

	add_ability(&player->abilities, ability);
	locate_ability(player->abilities, ability)->active = true;
	player->upkeep->update |= (PU_BONUS);
	update_stuff(player);
}

static void take_ability(int skill, const char *name)
{
	remove_ability(&player->abilities, lookup_ability(skill, name));
	player->upkeep->update |= (PU_BONUS);
	update_stuff(player);
}

static void clear_area(void)
{
	int y, x;

	for (y = -3; y <= 3; y++) {
		for (x = -3; x <= 3; x++) {
			struct loc grid = loc_sum(player->grid, loc(x, y));

			if (square_monster(cave, grid)) delete_monster(cave, grid);
			if (!loc_eq(grid, player->grid)) {
				square_set_feat(cave, grid, FEAT_FLOOR);
			}
		}
	}
}

/* Critical punctuation stops at 20 '!' */
static int test_punctuation(void *state) {
	char punct[21];

	attack_punctuation(punct, sizeof(punct), 5, 25);
	eq(strlen(punct), 20);
	attack_punctuation(punct, sizeof(punct), 5, 3);
	require(streq(punct, "!!!"));
	attack_punctuation(punct, sizeof(punct), 0, 3);
	require(streq(punct, "..."));
	ok;
}

/* Bare hands give the monster its full protection */
static int test_bare_protection(void *state) {
	int flag = 0;

	eq(prt_after_sharpness(player, NULL, &flag), 100);
	ok;
}

/* Riposte needs no weapon; bare-handed the threshold is -10 */
static int test_riposte_bare(void *state) {
	wield(NULL);
	give_ability(SKILL_EVASION, "Riposte");
	player->upkeep->riposte = false;
	require(player_can_riposte(player, -10));
	require(!player_can_riposte(player, -9));
	take_ability(SKILL_EVASION, "Riposte");
	ok;
}

/* A peaceful monster is not attacked and no turn is taken */
static int test_peaceful(void *state) {
	struct loc target = loc_sum(player->grid, loc(1, 0));
	struct monster *mon;

	wield(make_obj(TV_SWORD, "Longsword"));
	clear_area();
	mon = t_add_monster(cave, target, "Wolf");
	rf_on(mon->race->flags, RF_PEACEFUL);
	reset_counts();
	player->attacked = false;
	player->upkeep->energy_use = z_info->move_energy;
	py_attack(player, target, ATT_MAIN);
	eq(n_stop, 1);
	eq(n_hit, 0);
	eq(player->upkeep->energy_use, 0);
	ptreq(square_monster(cave, target), mon);
	rf_off(mon->race->flags, RF_PEACEFUL);
	clear_area();
	ok;
}

/* Melee targets a visible monster */
static int test_target(void *state) {
	struct loc target = loc_sum(player->grid, loc(1, 0));
	struct monster *mon;

	wield(make_obj(TV_SWORD, "Longsword"));
	clear_area();
	mon = t_add_monster(cave, target, "Wolf");
	mon->hp = mon->maxhp = 30000;
	player->upkeep->update |= (PU_UPDATE_VIEW | PU_MONSTERS);
	update_stuff(player);
	mflag_on(mon->mflag, MFLAG_VISIBLE);
	require(target_able(mon));
	target_set_location(loc(0, 0));
	py_attack_real(player, target, ATT_MAIN);
	ptreq(target_get_monster(), mon);
	clear_area();
	ok;
}

/* Impale attacks the monster behind the target as well */
static int test_impale(void *state) {
	struct loc target = loc_sum(player->grid, loc(1, 0));
	struct loc behind = loc_sum(player->grid, loc(2, 0));
	struct monster *m1, *m2;

	give_ability(SKILL_MELEE, "Impale");
	wield(make_obj(TV_POLEARM, "Spear"));
	clear_area();
	m1 = t_add_monster(cave, target, "Wolf");
	m2 = t_add_monster(cave, behind, "Wolf");
	m1->hp = m1->maxhp = 30000;
	m2->hp = m2->maxhp = 30000;
	reset_counts();
	player->state.skill_use[SKILL_MELEE] = 200;
	py_attack(player, target, ATT_MAIN);
	eq(n_impale, 2);

	/* Nobody behind: an ordinary attack */
	delete_monster(cave, behind);
	reset_counts();
	py_attack(player, target, ATT_MAIN);
	eq(n_impale, 0);
	eq(n_hit, 1);

	/* A one-handed sword does not impale */
	square_set_feat(cave, behind, FEAT_FLOOR);
	m2 = t_add_monster(cave, behind, "Wolf");
	m2->hp = m2->maxhp = 30000;
	wield(make_obj(TV_SWORD, "Longsword"));
	reset_counts();
	player->state.skill_use[SKILL_MELEE] = 200;
	py_attack(player, target, ATT_MAIN);
	eq(n_impale, 0);
	take_ability(SKILL_MELEE, "Impale");
	clear_area();
	ok;
}

/* Charging into an impale charges the adjacent monster only */
static int test_charge_impale(void *state) {
	struct loc target = loc_sum(player->grid, loc(1, 0));
	struct loc behind = loc_sum(player->grid, loc(2, 0));
	struct monster *m1, *m2;

	give_ability(SKILL_MELEE, "Impale");
	give_ability(SKILL_MELEE, "Charge");
	wield(make_obj(TV_POLEARM, "Spear"));
	clear_area();
	m1 = t_add_monster(cave, target, "Wolf");
	m2 = t_add_monster(cave, behind, "Wolf");
	m1->hp = m1->maxhp = 30000;
	m2->hp = m2->maxhp = 30000;
	reset_counts();
	player->state.speed = 2;
	player->previous_action[1] = DIR_E;
	player->state.skill_use[SKILL_MELEE] = 200;
	py_attack(player, target, ATT_MAIN);
	eq(n_charge, 1);
	eq(n_impale, 1);
	take_ability(SKILL_MELEE, "Charge");
	take_ability(SKILL_MELEE, "Impale");
	clear_area();
	ok;
}

/* Whirlwind works next to walls */
static int test_whirlwind_walls(void *state) {
	struct loc a = loc_sum(player->grid, loc(1, 0));
	struct loc b = loc_sum(player->grid, loc(-1, 0));
	struct monster *m1, *m2;

	give_ability(SKILL_MELEE, "Whirlwind Attack");
	wield(make_obj(TV_SWORD, "Longsword"));
	clear_area();
	square_set_feat(cave, loc_sum(player->grid, loc(0, 1)), FEAT_GRANITE);
	square_set_feat(cave, loc_sum(player->grid, loc(0, -1)), FEAT_GRANITE);
	m1 = t_add_monster(cave, a, "Wolf");
	m2 = t_add_monster(cave, b, "Wolf");
	m1->hp = m1->maxhp = 30000;
	m2->hp = m2->maxhp = 30000;
	reset_counts();
	player->state.skill_use[SKILL_MELEE] = 200;
	py_attack(player, a, ATT_MAIN);
	eq(n_hit, 2);
	take_ability(SKILL_MELEE, "Whirlwind Attack");
	clear_area();
	ok;
}

/* Smite does maximum damage and costs the next turn */
static int test_smite(void *state) {
	struct loc target = loc_sum(player->grid, loc(1, 0));
	struct monster *mon;

	give_ability(SKILL_MELEE, "Smite");
	wield(make_obj(TV_SWORD, "Greatsword"));
	require(two_handed_melee(player));
	clear_area();
	mon = t_add_monster(cave, target, "Wolf");
	mon->hp = mon->maxhp = 30000;
	reset_counts();
	player->upkeep->skip_next_turn = false;
	player->state.skill_use[SKILL_MELEE] = 200;
	py_attack_real(player, target, ATT_MAIN);
	eq(n_smite, 1);
	require(last_dd > 0 && last_ds > 0);
	eq(last_dam, last_dd * last_ds);
	require(player->upkeep->skip_next_turn);
	require(!player->upkeep->knocked_back);

	/* Not on attacks in the monsters' turns */
	player->upkeep->skip_next_turn = false;
	player->state.skill_use[SKILL_MELEE] = 200;
	py_attack_real(player, target, ATT_ZONE_OF_CONTROL);
	require(!player->upkeep->skip_next_turn);
	take_ability(SKILL_MELEE, "Smite");
	clear_area();
	ok;
}

/* The evasion halving survives a recalculation of bonuses */
static int test_halve_evasion(void *state) {
	int evn;

	player->upkeep->update |= (PU_BONUS);
	update_stuff(player);
	evn = player->state.skill_use[SKILL_EVASION];
	player->upkeep->halve_evasion = true;
	player->upkeep->update |= (PU_BONUS);
	update_stuff(player);
	eq(player->state.skill_use[SKILL_EVASION], evn / 2);
	player->upkeep->halve_evasion = false;
	player->upkeep->update |= (PU_BONUS);
	update_stuff(player);
	eq(player->state.skill_use[SKILL_EVASION], evn);
	ok;
}

const char *suite_name = "player/melee";
struct test tests[] = {
	{ "punctuation", test_punctuation },
	{ "bare protection", test_bare_protection },
	{ "riposte bare", test_riposte_bare },
	{ "peaceful", test_peaceful },
	{ "target", test_target },
	{ "impale", test_impale },
	{ "charge impale", test_charge_impale },
	{ "whirlwind walls", test_whirlwind_walls },
	{ "smite", test_smite },
	{ "halve evasion", test_halve_evasion },
	{ NULL, NULL }
};
