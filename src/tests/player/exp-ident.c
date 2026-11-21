/* player/exp-ident */
/* Experience awards and identification rules. */

#include "unit-test.h"
#include "test-utils.h"
#include "cave.h"
#include "game-event.h"
#include "game-world.h"
#include "generate.h"
#include "init.h"
#include "mon-lore.h"
#include "mon-util.h"
#include "monster.h"
#include "obj-gear.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-birth.h"
#include "player-calcs.h"
#include "player-util.h"

static char msgs[8][256];
static int n_msg;

static void msg_handler(game_event_type type, game_event_data *data,
		void *user)
{
	if (data->message.msg && n_msg < 8) {
		my_strcpy(msgs[n_msg], data->message.msg, sizeof(msgs[0]));
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
	for (i = 0; i < player->body.count; i++) {
		player->body.slots[i].obj = NULL;
	}
	event_add_handler(EVENT_MESSAGE, msg_handler, NULL);
	return 0;
}

int teardown_tests(void *state) {
	event_remove_handler(EVENT_MESSAGE, msg_handler, NULL);
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
	return obj;
}

/* With fixed experience, no experience is gained */
static int test_fixed_exp(void *state) {
	int32_t exp = player->exp;

	player->opts.opt[OPT_birth_fixed_exp] = true;
	player_exp_gain(player, 100);
	eq(player->exp, exp);
	player->opts.opt[OPT_birth_fixed_exp] = false;
	player_exp_gain(player, 100);
	eq(player->exp, exp + 100);
	ok;
}

/* Peaceful monsters give no kill experience, but do give encounter exp */
static int test_peaceful_exp(void *state) {
	struct monster_race *race = lookup_monster("Orc soldier");

	require(race);
	get_lore(race)->pkills = 0;
	get_lore(race)->psights = 0;
	require(adjusted_mon_exp(race, true) == race->level * 10);
	rf_on(race->flags, RF_PEACEFUL);
	eq(adjusted_mon_exp(race, true), 0);
	eq(adjusted_mon_exp(race, false), race->level * 10);
	rf_off(race->flags, RF_PEACEFUL);
	ok;
}

/* No identification experience while leaving the level */
static int test_aware_leaving(void *state) {
	struct object *obj = make_obj(TV_RING, "Strength");
	int32_t exp;

	require(obj);
	obj->kind->aware = false;
	exp = player->exp;
	player->upkeep->generate_level = true;
	object_flavor_aware(player, obj);
	player->upkeep->generate_level = false;
	eq(obj->kind->aware, true);
	eq(player->exp, exp);
	obj->kind->aware = false;
	object_flavor_aware(player, obj);
	eq(player->exp, exp + 100);
	object_delete(NULL, &obj);
	ok;
}

/* Identifying an item keeps its empty mark */
static int test_know_keeps_empty(void *state) {
	struct object *obj = make_obj(TV_RING, "Strength");

	require(obj);
	obj->notice |= OBJ_NOTICE_EMPTY;
	object_know(obj);
	eq(!!(obj->notice & OBJ_NOTICE_EMPTY), 1);
	object_delete(NULL, &obj);
	ok;
}

/* An unknown item of an aware flavour is recognised silently */
static int test_wield_aware_silent(void *state) {
	struct object *obj = make_obj(TV_RING, "Strength");

	require(obj);
	obj->modifiers[OBJ_MOD_STR] = 2;
	obj->kind->aware = true;
	n_msg = 0;
	ident_on_wield(player, obj);
	eq(object_is_known(obj), true);
	eq(n_msg, 1);
	require(prefix(msgs[0], "You recognize it as"));
	object_delete(NULL, &obj);
	ok;
}

/* An unknown flavour with a positive bonus gives the stat message first */
static int test_wield_stat_message(void *state) {
	struct object *obj = make_obj(TV_RING, "Strength");

	require(obj);
	obj->modifiers[OBJ_MOD_STR] = -2;
	obj->kind->aware = false;
	n_msg = 0;
	ident_on_wield(player, obj);
	eq(object_is_known(obj), true);
	require(n_msg >= 2);
	require(streq(msgs[0], "You feel less strong."));
	object_delete(NULL, &obj);
	ok;
}

/* An unknown flavour with a zero bonus stays unknown */
static int test_wield_zero_bonus(void *state) {
	struct object *obj = make_obj(TV_RING, "Strength");

	require(obj);
	obj->modifiers[OBJ_MOD_STR] = 0;
	obj->kind->aware = false;
	obj->att = 1;
	n_msg = 0;
	ident_on_wield(player, obj);
	eq(object_is_known(obj), false);
	eq(n_msg, 0);
	object_delete(NULL, &obj);
	ok;
}

const char *suite_name = "player/exp-ident";
struct test tests[] = {
	{ "fixed-exp", test_fixed_exp },
	{ "peaceful-exp", test_peaceful_exp },
	{ "aware-leaving", test_aware_leaving },
	{ "know-keeps-empty", test_know_keeps_empty },
	{ "wield-aware-silent", test_wield_aware_silent },
	{ "wield-stat-message", test_wield_stat_message },
	{ "wield-zero-bonus", test_wield_zero_bonus },
	{ NULL, NULL }
};
