/* player/calc-combat */
/* Derived values (skills, stats, light, voice and health) and the shared
 * hit, critical and protection rolls. */

#include "unit-test.h"
#include "test-utils.h"
#include "cave.h"
#include "combat.h"
#include "game-event.h"
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
#include "player-util.h"
#include "project.h"

static char last_msg[256];
static int n_msg;

static void msg_handler(game_event_type type, game_event_data *data,
		void *user)
{
	if (data->message.msg) {
		my_strcpy(last_msg, data->message.msg, sizeof(last_msg));
	}
	n_msg++;
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
	/* Start from bare slots; the starting kit stays in the gear list */
	for (i = 0; i < player->body.count; i++) {
		player->body.slots[i].obj = NULL;
	}
	event_add_handler(EVENT_MESSAGE, msg_handler, NULL);
	return 0;
}

int teardown_tests(void *state) {
	event_remove_handler(EVENT_MESSAGE, msg_handler, NULL);
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

/* Put an object straight into an equipment slot, returning what was there */
static struct object *put_in_slot(const char *slot_name, struct object *obj)
{
	int slot = slot_by_name(player, slot_name);
	struct object *old = player->body.slots[slot].obj;

	player->body.slots[slot].obj = obj;
	return old;
}

/* Give the player an active copy of an ability, innate or from an item */
static void give_ability(int skill, const char *name, bool from_item)
{
	struct ability **set = from_item ? &player->item_abilities :
		&player->abilities;
	struct ability *ability = lookup_ability(skill, name);

	add_ability(set, ability);
	locate_ability(*set, ability)->active = true;
}

static void take_ability(int skill, const char *name, bool from_item)
{
	struct ability **set = from_item ? &player->item_abilities :
		&player->abilities;

	remove_ability(set, lookup_ability(skill, name));
}

static void clear_area(void)
{
	int y, x;

	for (y = -2; y <= 2; y++) {
		for (x = -2; x <= 2; x++) {
			struct loc grid = loc_sum(player->grid, loc(x, y));

			if (square_monster(cave, grid)) delete_monster(cave, grid);
			if (!loc_eq(grid, player->grid)) {
				square_set_feat(cave, grid, FEAT_FLOOR);
			}
		}
	}
}

/* Bow proficiency adds Archery with a bow, per race flag; no blade bonus */
static int test_bow_bonus(void *state) {
	struct object *bow = make_obj(TV_BOW, "Longbow");
	struct object *sword = make_obj(TV_SWORD, "Longsword");
	struct player_race *old_race = player->race, *race;

	require(bow && sword);
	for (race = races; race; race = race->next) {
		if (streq(race->name, "Noldor")) break;
	}
	require(race);
	player->race = race;
	eq(bow_bonus(player, bow), 1);
	eq(bow_bonus(player, sword), 0);
	eq(bow_bonus(player, NULL), 0);
	for (race = races; race; race = race->next) {
		if (streq(race->name, "Edain")) break;
	}
	require(race);
	player->race = race;
	eq(bow_bonus(player, bow), 0);
	player->race = old_race;
	object_free(bow);
	object_free(sword);
	ok;
}

/* Polearm Mastery: +2 with a polearm (Sil-Q xtra1.c:204-221) */
static int test_polearm_bonus(void *state) {
	struct object *spear = make_obj(TV_POLEARM, "Spear");

	require(spear);
	eq(polearm_bonus(player, spear), 0);
	give_ability(SKILL_MELEE, "Polearm Mastery", false);
	eq(polearm_bonus(player, spear), 2);
	take_ability(SKILL_MELEE, "Polearm Mastery", false);
	object_free(spear);
	ok;
}

/* A LIGHT flag on a light counts only while lit; DARKNESS on it never */
static int test_light_flags(void *state) {
	struct object *torch = make_obj(TV_LIGHT, "Wooden Torch");
	struct object *old;
	bool smithing = player->upkeep->smithing;

	require(torch);
	player->upkeep->smithing = false;
	old = put_in_slot("light", torch);
	of_on(torch->flags, OF_LIGHT);
	torch->timeout = 0;
	calc_light(player);
	eq(player->upkeep->cur_light, 0);
	torch->timeout = 1000;
	calc_light(player);
	eq(player->upkeep->cur_light, 2);
	of_off(torch->flags, OF_LIGHT);
	of_on(torch->flags, OF_DARKNESS);
	calc_light(player);
	/* Darkness only makes it flicker; at full fuel it may lower to -2 */
	require(player->upkeep->cur_light <= 1);
	torch->timeout = 0;
	calc_light(player);
	eq(player->upkeep->cur_light, 0);
	put_in_slot("light", old);
	object_free(torch);
	player->upkeep->smithing = smithing;
	calc_light(player);
	ok;
}

/* A stat ability held and also granted by an item counts once per skill */
static int test_stat_ability_stacking(void *state) {
	struct player_state st;
	int base;

	calc_bonuses(player, &st, false, false);
	base = st.stat_misc_mod[STAT_DEX];
	give_ability(SKILL_ARCHERY, "Dexterity", false);
	give_ability(SKILL_ARCHERY, "Dexterity", true);
	calc_bonuses(player, &st, false, false);
	eq(st.stat_misc_mod[STAT_DEX], base + 1);
	give_ability(SKILL_EVASION, "Dexterity", true);
	calc_bonuses(player, &st, false, false);
	eq(st.stat_misc_mod[STAT_DEX], base + 2);
	take_ability(SKILL_ARCHERY, "Dexterity", false);
	take_ability(SKILL_ARCHERY, "Dexterity", true);
	take_ability(SKILL_EVASION, "Dexterity", true);
	ok;
}

/* A change of maximum health or voice clears the regeneration fraction */
static int test_frac_reset(void *state) {
	player->mhp += 3;
	player->chp_frac = 1234;
	player->upkeep->update |= PU_HP;
	update_stuff(player);
	eq(player->chp_frac, 0);
	player->msp += 3;
	player->csp_frac = 1234;
	player->upkeep->update |= PU_MANA;
	update_stuff(player);
	eq(player->csp_frac, 0);
	/* No change, no reset */
	player->chp_frac = 77;
	player->upkeep->update |= PU_HP;
	update_stuff(player);
	eq(player->chp_frac, 77);
	ok;
}

/* The armour bounds use the minimum and maximum of the protection roll */
static int test_p_max(void *state) {
	struct object *shield = make_obj(TV_SHIELD, "Round Shield");
	struct object *old;
	struct player_state st;

	require(shield);
	old = put_in_slot("arm", shield);
	calc_bonuses(player, &st, false, false);
	eq(st.p_min, protection_roll(player, PROJ_HURT, true, MINIMISE));
	eq(st.p_max, protection_roll(player, PROJ_HURT, true, MAXIMISE));
	require(st.p_max > st.p_min);
	put_in_slot("arm", old);
	object_free(shield);
	ok;
}

/* Off-hand weapon without Two Weapon Fighting: removed as by take-off */
static int test_offhand_removal(void *state) {
	struct object *main_old, *off_old;
	struct object *bastard = make_obj(TV_SWORD, "Bastard Sword");
	struct object *dagger = make_obj(TV_SWORD, "Dagger");
	struct player_state st;

	require(bastard && dagger);
	main_old = put_in_slot("weapon", bastard);
	off_old = put_in_slot("arm", dagger);
	gear_insert_end(player, dagger);
	player->upkeep->equip_cnt++;

	/* Cursed: stays put */
	of_on(dagger->flags, OF_CURSED);
	n_msg = 0;
	player->upkeep->energy_use = 0;
	calc_bonuses(player, &st, false, true);
	eq(n_msg, 2);
	require(streq(last_msg, "You cannot bear to part with it."));
	ptreq(equipped_item_by_slot_name(player, "arm"), dagger);
	eq(player->upkeep->energy_use, 0);

	/* Uncursed: removed, a turn taken, hand-and-a-half message */
	of_off(dagger->flags, OF_CURSED);
	n_msg = 0;
	player->previous_action[0] = ACTION_STAND;
	calc_bonuses(player, &st, false, true);
	require(n_msg >= 2);
	require(strstr(last_msg, "more effectively.") != NULL);
	null(equipped_item_by_slot_name(player, "arm"));
	eq(player->upkeep->energy_use, z_info->move_energy);
	eq(player->previous_action[0], ACTION_MISC);

	put_in_slot("weapon", main_old);
	put_in_slot("arm", off_old);
	gear_excise_object(player, dagger);
	object_free(dagger);
	object_free(bastard);
	player->upkeep->energy_use = 0;
	ok;
}

/* Surround bonus: +2 behind, +1 to the sides; peaceful monsters ignored */
static int test_surround(void *state) {
	struct monster *a, *b;

	clear_area();
	a = t_add_monster(cave, loc_sum(player->grid, loc(1, 0)), "Wolf");
	eq(overwhelming_att_mod(player, a), 0);
	b = t_add_monster(cave, loc_sum(player->grid, loc(-1, 0)), "Wolf");
	eq(overwhelming_att_mod(player, a), 2);
	rf_on(b->race->flags, RF_PEACEFUL);
	eq(overwhelming_att_mod(player, a), 0);
	rf_off(b->race->flags, RF_PEACEFUL);
	delete_monster(cave, b->grid);
	t_add_monster(cave, loc_sum(player->grid, loc(0, 1)), "Wolf");
	eq(overwhelming_att_mod(player, a), 1);
	clear_area();
	ok;
}

/* Critical separation: Finesse -20; no archery or player-side modifiers */
static int test_crit_bonus(void *state) {
	struct monster_race *wolf = lookup_monster("Wolf");

	require(!rf_has(wolf->flags, RF_RES_CRIT));
	eq(crit_bonus(player, 16, 30, wolf, SKILL_MELEE, false), 1);
	give_ability(SKILL_MELEE, "Finesse", false);
	eq(crit_bonus(player, 16, 30, wolf, SKILL_MELEE, false), 2);
	/* The player is not a monster: no Finesse when the player is hit */
	eq(crit_bonus(player, 16, 30, NULL, SKILL_MELEE, false), 1);
	eq(crit_bonus(player, 16, 30, &r_info[0], SKILL_MELEE, false), 1);
	take_ability(SKILL_MELEE, "Finesse", false);
	/* Arrows at the player: (100 * 10 + 4) / (70 + 30) = 10 */
	eq(crit_bonus(player, 100, 30, &r_info[0], SKILL_ARCHERY, false), 10);
	ok;
}

/* Protection: Blocking doubles shield dice unless the player moved;
 * Heavy Armour adds 1d(weight / 15 lb) */
static int test_protection(void *state) {
	struct object *shield = make_obj(TV_SHIELD, "Round Shield");
	struct object *mail = make_obj(TV_SOFT_ARMOR, "Leather Armour");
	struct object *old_arm, *old_body;
	int min0, max0;

	require(shield && mail);
	old_arm = put_in_slot("arm", shield);
	old_body = put_in_slot("body", NULL);
	player->previous_action[0] = ACTION_STAND;
	min0 = protection_roll(player, PROJ_HURT, true, MINIMISE);
	max0 = protection_roll(player, PROJ_HURT, true, MAXIMISE);
	eq(min0, shield->pd);
	eq(max0, shield->pd * shield->ps);

	give_ability(SKILL_EVASION, "Blocking", false);
	require(blocking_bonus_active(player));
	eq(protection_roll(player, PROJ_HURT, false, MINIMISE), 2 * min0);
	eq(protection_roll(player, PROJ_FIRE, false, MAXIMISE), 2 * max0);
	player->previous_action[0] = 6;
	require(!blocking_bonus_active(player));
	eq(protection_roll(player, PROJ_HURT, true, MAXIMISE), max0);
	player->previous_action[0] = ACTION_MISC;
	require(blocking_bonus_active(player));
	take_ability(SKILL_EVASION, "Blocking", false);
	player->previous_action[0] = ACTION_STAND;

	/* Heavy Armour: under 15 lb adds nothing at all */
	give_ability(SKILL_EVASION, "Heavy Armour", false);
	shield->weight = 140;
	eq(protection_roll(player, PROJ_HURT, true, MINIMISE), min0);
	eq(protection_roll(player, PROJ_HURT, true, MAXIMISE), max0);
	/* 45 lb in all: min +1, max +3 */
	mail->weight = 450 - 140;
	put_in_slot("body", mail);
	eq(protection_roll(player, PROJ_HURT, true, MINIMISE),
		min0 + mail->pd + 1);
	eq(protection_roll(player, PROJ_HURT, true, MAXIMISE),
		max0 + mail->pd * mail->ps + 3);
	take_ability(SKILL_EVASION, "Heavy Armour", false);

	put_in_slot("arm", old_arm);
	put_in_slot("body", old_body);
	object_free(shield);
	object_free(mail);
	ok;
}

/* Knock back never picks a sideways square that holds a monster */
static int test_knock_back(void *state) {
	struct loc target, behind;
	struct monster *mon, *side1, *side2;
	int i;

	for (i = 0; i < 20; i++) {
		clear_area();
		target = loc_sum(player->grid, loc(1, 0));
		behind = loc_sum(player->grid, loc(2, 0));
		mon = t_add_monster(cave, target, "Wolf");
		square_set_feat(cave, behind, FEAT_GRANITE);
		side1 = t_add_monster(cave, loc_sum(player->grid, loc(2, 1)),
			"Wolf");
		side2 = t_add_monster(cave, loc_sum(player->grid, loc(2, -1)),
			"Wolf");
		require(!knock_back(player->grid, target));
		ptreq(square_monster(cave, target), mon);
		ptreq(square_monster(cave, side1->grid), side1);
		ptreq(square_monster(cave, side2->grid), side2);
		/* One side free: the monster goes there */
		delete_monster(cave, side2->grid);
		require(knock_back(player->grid, target));
		ptreq(square_monster(cave, loc_sum(player->grid, loc(2, -1))), mon);
	}
	clear_area();
	ok;
}

const char *suite_name = "player/calc-combat";
struct test tests[] = {
	{ "bow bonus", test_bow_bonus },
	{ "polearm bonus", test_polearm_bonus },
	{ "light flags", test_light_flags },
	{ "stat ability stacking", test_stat_ability_stacking },
	{ "frac reset", test_frac_reset },
	{ "p_max", test_p_max },
	{ "off-hand removal", test_offhand_removal },
	{ "surround", test_surround },
	{ "crit bonus", test_crit_bonus },
	{ "protection", test_protection },
	{ "knock back", test_knock_back },
	{ NULL, NULL }
};
