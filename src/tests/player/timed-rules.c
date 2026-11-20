/* player/timed-rules */
/* Rules attached to timed effects: hunger, stun, temporary boosts,
 * entrancement and confusion. */

#include "unit-test.h"
#include "unit-test-data.h"
#include "test-utils.h"
#include "cave.h"
#include "game-event.h"
#include "game-world.h"
#include "generate.h"
#include "init.h"
#include "mon-make.h"
#include "mon-util.h"
#include "player.h"
#include "player-birth.h"
#include "player-calcs.h"
#include "player-timed.h"
#include "player-util.h"
#include "cmd-core.h"
#include "effects.h"
#include "obj-gear.h"
#include "source.h"
#include "z-rand.h"
#include "z-virt.h"

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
	event_add_handler(EVENT_MESSAGE, msg_handler, NULL);
	return 0;
}

int teardown_tests(void *state) {
	event_remove_handler(EVENT_MESSAGE, msg_handler, NULL);
	wipe_mon_list(cave, player);
	cleanup_angband();
	return 0;
}

static void reset_msgs(void)
{
	last_msg[0] = '\0';
	n_msg = 0;
}

static void clear_all_timed(void)
{
	int i;

	for (i = 0; i < TMD_MAX; i++) {
		player->timed[i] = 0;
	}
	player->timed[TMD_FOOD] = PY_FOOD_FULL - 1;
	player->chp = player->mhp;
	player->csp = player->msp;
}

static void recalc(void)
{
	player->upkeep->update |= PU_BONUS;
	handle_stuff(player);
}

/* Sil-Q's food constants and bands: defines.h:391-395, xtra2.c:1414-1590 */
static int test_food_bands(void *state) {
	eq(PY_FOOD_STARVE, 1);
	eq(PY_FOOD_WEAK, 1000);
	eq(PY_FOOD_ALERT, 2000);
	eq(PY_FOOD_FULL, 5000);
	eq(PY_FOOD_MAX, 8000);

	clear_all_timed();
	require(player_timed_grade_eq(player, TMD_FOOD, "Fed"));
	player->timed[TMD_FOOD] = 0;
	require(player_timed_grade_eq(player, TMD_FOOD, "Starving"));
	player->timed[TMD_FOOD] = 999;
	require(player_timed_grade_eq(player, TMD_FOOD, "Weak"));
	player->timed[TMD_FOOD] = 1000;
	require(player_timed_grade_eq(player, TMD_FOOD, "Hungry"));
	player->timed[TMD_FOOD] = 5000;
	require(player_timed_grade_eq(player, TMD_FOOD, "Full"));
	player->timed[TMD_FOOD] = 8000;
	require(player_timed_grade_eq(player, TMD_FOOD, "Full"));

	/* Food can reach zero, with the starving message */
	player->timed[TMD_FOOD] = 1;
	reset_msgs();
	player_dec_timed(player, TMD_FOOD, 5, false, false);
	eq(player->timed[TMD_FOOD], 0);
	require(streq(last_msg, "You are beginning to starve!"));

	/* Replete messages */
	player->timed[TMD_FOOD] = 7000;
	reset_msgs();
	player_inc_timed(player, TMD_FOOD, 2000, false, false, false);
	require(streq(last_msg, "You are as full as you can be."));
	reset_msgs();
	player_set_timed(player, TMD_FOOD, 7999, false, false);
	require(streq(last_msg, "You feel comfortably full."));
	ok;
}

/* Starvation hurts (dungeon.c:2228-2235) and digestion is 50 times faster
 * when replete (dungeon.c:2220-2222) */
static int test_digest(void *state) {
	clear_all_timed();
	recalc();
	player->timed[TMD_FOOD] = 0;
	player->chp = player->mhp;
	player_digest(player);
	eq(player->chp, player->mhp - 1);

	clear_all_timed();
	recalc();
	if (player->state.flags[OF_HUNGER] == 0) {
		player->timed[TMD_FOOD] = 9000;
		player_digest(player);
		eq(player->timed[TMD_FOOD], 9000 - 50);
		player->timed[TMD_FOOD] = 7000;
		player_digest(player);
		eq(player->timed[TMD_FOOD], 6999);
	}
	ok;
}

/* STR -1 below 1000 food, including starving: xtra1.c:2649-2653 */
static int test_weak_str(void *state) {
	int base;

	clear_all_timed();
	recalc();
	base = player->state.stat_use[STAT_STR];
	player->timed[TMD_FOOD] = 999;
	recalc();
	eq(player->state.stat_use[STAT_STR], base - 1);
	player->timed[TMD_FOOD] = 0;
	recalc();
	eq(player->state.stat_use[STAT_STR], base - 1);
	player->timed[TMD_FOOD] = 1000;
	recalc();
	eq(player->state.stat_use[STAT_STR], base);
	ok;
}

/* Stun -4 to skills at 50 or more, -2 below: xtra1.c:2571-2585 */
static int test_stun_skills(void *state) {
	int base;

	clear_all_timed();
	recalc();
	base = player->state.skill_use[SKILL_MELEE];
	player->timed[TMD_STUN] = 49;
	recalc();
	eq(player->state.skill_use[SKILL_MELEE], base - 2);
	player->timed[TMD_STUN] = 50;
	recalc();
	eq(player->state.skill_use[SKILL_MELEE], base - 4);
	player->timed[TMD_STUN] = 103;
	recalc();
	eq(player->state.skill_use[SKILL_MELEE], base - 4);
	clear_all_timed();
	recalc();
	ok;
}

/* Waking from a knock-out, and noticing band falls: xtra2.c:1287-1326 */
static int test_wake(void *state) {
	clear_all_timed();
	player->timed[TMD_STUN] = 101;
	player->timed[TMD_BLIND] = 2;
	reset_msgs();
	player_dec_timed(player, TMD_STUN, 1, false, false);
	eq(player->timed[TMD_STUN], 100);
	eq(player->timed[TMD_BLIND], 1);
	require(streq(last_msg, "You wake up."));

	player->timed[TMD_STUN] = 104;
	player->timed[TMD_BLIND] = 1;
	reset_msgs();
	player_clear_timed(player, TMD_STUN, false, false);
	eq(player->timed[TMD_BLIND], 0);
	eq(n_msg, 2);
	require(streq(last_msg, "You are no longer stunned."));

	/* Heavy stun to stun is silent but noticed */
	player->timed[TMD_STUN] = 51;
	reset_msgs();
	require(player_dec_timed(player, TMD_STUN, 1, false, false));
	eq(n_msg, 0);
	clear_all_timed();
	recalc();
	ok;
}

/* Temporary perception: xtra1.c:2827-2830 */
static int test_tmp_per(void *state) {
	int base;

	clear_all_timed();
	recalc();
	base = player->state.skill_use[SKILL_PERCEPTION];
	player->timed[TMD_PER] = 5;
	recalc();
	eq(player->state.skill_use[SKILL_PERCEPTION], base + 10);
	clear_all_timed();
	recalc();
	ok;
}

/* End of a temporary stat boost restores 3 drained points: xtra2.c:661-666,
 * spells2.c:107, 178-191 */
static int test_tmp_stat_restore(void *state) {
	clear_all_timed();
	player->stat_drain[STAT_STR] = -5;
	player->timed[TMD_STR] = 1;
	reset_msgs();
	player_dec_timed(player, TMD_STR, 1, false, false);
	eq(player->stat_drain[STAT_STR], -2);
	require(streq(last_msg, "You feel less weak."));
	player->stat_drain[STAT_STR] = 0;
	recalc();
	ok;
}

/* Damage breaks a trance with its message: spells1.c:661, xtra2.c:376-387 */
static int test_trance_break(void *state) {
	clear_all_timed();
	player->upkeep->was_entranced = false;
	player->timed[TMD_ENTRANCED] = 10;
	reset_msgs();
	take_hit(player, 1, "test");
	eq(player->timed[TMD_ENTRANCED], 0);
	require(player->upkeep->was_entranced);
	require(streq(last_msg, "The trance is broken!"));
	player->chp = player->mhp;

	/* A new trance replaces the duration */
	player->timed[TMD_ENTRANCED] = 30;
	player_set_timed(player, TMD_ENTRANCED, 7, true, false);
	eq(player->timed[TMD_ENTRANCED], 7);
	clear_all_timed();
	recalc();
	ok;
}

/* Confused movement deviates binomially (xtra2.c:5107-5153); aim is uniform
 * except up and down (xtra2.c:5009-5021); running refused (cmd2.c:3293-3300) */
static int test_confuse(void *state) {
	int counts[10] = { 0 };
	int i, dir;
	const int n = 6400;

	clear_all_timed();
	player->timed[TMD_CONFUSED] = 100;
	Rand_init();
	for (i = 0; i < n; i++) {
		dir = 6;
		player_confuse_dir(player, &dir, false);
		counts[dir]++;
	}
	/* Out of 64: E 20, NE/SE 15, N/S 6, NW/SW 1, W 0 */
	eq(counts[4], 0);
	require(counts[6] > 1700 && counts[6] < 2300);
	require(counts[9] > 1250 && counts[9] < 1750);
	require(counts[3] > 1250 && counts[3] < 1750);
	require(counts[8] > 450 && counts[8] < 750);
	require(counts[2] > 450 && counts[2] < 750);
	require(counts[7] > 40 && counts[7] < 170);
	require(counts[1] > 40 && counts[1] < 170);

	/* Running is refused */
	dir = 6;
	reset_msgs();
	require(player_confuse_dir(player, &dir, true));
	require(streq(last_msg, "You are too confused!"));

	/* Aiming is uniform, up and down are kept */
	for (i = 0; i < 10; i++) counts[i] = 0;
	for (i = 0; i < 8000; i++) {
		dir = 6;
		player_confuse_aim(player, &dir);
		counts[dir]++;
	}
	for (i = 1; i < 10; i++) {
		if (i == 5) continue;
		require(counts[i] > 850 && counts[i] < 1150);
	}
	dir = DIR_UP;
	require(!player_confuse_aim(player, &dir));
	eq(dir, DIR_UP);
	clear_all_timed();
	ok;
}

static struct object *some_equipment(void)
{
	int i;

	for (i = 0; i < player->body.count; i++) {
		struct object *obj = slot_object(player, i);
		if (obj) return obj;
	}
	return NULL;
}

/* A new trance roll replaces the duration, as for the needle trap and food:
 * xtra2.c:387, cmd2.c:771-776, use-obj.c:133-135 */
static int test_trance_set(void *state) {
	bool ident = false;

	clear_all_timed();
	player->timed[TMD_ENTRANCED] = 3;
	effect_simple(EF_TIMED_SET, source_none(), "25", TMD_ENTRANCED, 0, 0,
		&ident);
	eq(player->timed[TMD_ENTRANCED], 25);
	effect_simple(EF_TIMED_SET, source_none(), "4", TMD_ENTRANCED, 0, 0,
		&ident);
	eq(player->timed[TMD_ENTRANCED], 4);
	clear_all_timed();
	recalc();
	ok;
}

/* Regeneration costs food only while wounded (xtra1.c:2702-2707); each
 * sustenance item lowers the rate by one (xtra1.c:2377-2378) */
static int test_hunger_rate(void *state) {
	struct object *obj = some_equipment();
	int base;

	notnull(obj);
	clear_all_timed();
	recalc();
	base = player->state.flags[OF_HUNGER];
	of_on(obj->flags, OF_REGEN);
	recalc();
	eq(player->state.flags[OF_HUNGER], base);
	player->chp = player->mhp - 1;
	recalc();
	eq(player->state.flags[OF_HUNGER], base + 1);
	player->chp = player->mhp;
	player->csp = player->msp - 1;
	recalc();
	eq(player->state.flags[OF_HUNGER], base + 1);
	of_off(obj->flags, OF_REGEN);
	player->csp = player->msp;
	of_on(obj->flags, OF_SLOW_DIGEST);
	recalc();
	eq(player->state.flags[OF_HUNGER], base - 1);
	of_off(obj->flags, OF_SLOW_DIGEST);
	recalc();
	ok;
}

/* See invisible equipment cures blindness: xtra1.c:2381-2385 */
static int test_see_invis_cures(void *state) {
	struct object *obj = some_equipment();

	notnull(obj);
	clear_all_timed();
	player->timed[TMD_BLIND] = 20;
	recalc();
	eq(player->timed[TMD_BLIND], 20);
	of_on(obj->flags, OF_SEE_INVIS);
	reset_msgs();
	recalc();
	eq(player->timed[TMD_BLIND], 0);
	require(streq(last_msg, "You can see again."));
	of_off(obj->flags, OF_SEE_INVIS);
	recalc();
	ok;
}

/* Rage redraws the map when it starts: xtra2.c:592-597 */
static int test_rage_map(void *state) {
	require(timed_effects[TMD_RAGE].flag_redraw & PR_MAP);
	ok;
}

/* Rest as needed ignores hallucination: dungeon.c:1580-1590 */
static int test_rest_image(void *state) {
	clear_all_timed();
	player->timed[TMD_IMAGE] = 50;
	player_resting_set_count(player, REST_COMPLETE);
	require(player_is_resting(player));
	player_resting_complete_special(player);
	require(!player_is_resting(player));
	clear_all_timed();
	ok;
}

static struct loc east_of_player(void)
{
	return loc_sum(player->grid, ddgrid[6]);
}

/* Confused walking into a known wall is refused at no cost and with no
 * confusion roll: cmd2.c:3178-3223, 3241-3276 */
static int test_confused_wall(void *state) {
	struct loc grid = east_of_player();
	int i;

	clear_all_timed();
	player->timed[TMD_CONFUSED] = 100;
	square_set_feat(cave, grid, FEAT_GRANITE);
	sqinfo_on(square(cave, grid)->info, SQUARE_MARK);
	for (i = 0; i < 20; i++) {
		reset_msgs();
		player->upkeep->energy_use = 0;
		cmdq_push(CMD_WALK);
		cmd_set_arg_direction(cmdq_peek(), "direction", 6);
		cmdq_pop(CTX_GAME);
		eq(player->upkeep->energy_use, 0);
		require(streq(last_msg, "There is a wall in the way!"));
	}
	square_set_feat(cave, grid, FEAT_FLOOR);
	clear_all_timed();
	ok;
}

/* A melee attack refused for fear takes no time: cmd1.c:3982-3988, 4064-4075 */
static int test_afraid_bump(void *state) {
	struct loc grid = east_of_player();
	struct monster *mon;

	clear_all_timed();
	square_set_feat(cave, grid, FEAT_FLOOR);
	mon = t_add_monster(cave, grid, "Orc");
	mflag_on(mon->mflag, MFLAG_VISIBLE);
	player->timed[TMD_AFRAID] = 100;
	player->attacked = false;
	player->upkeep->energy_use = 0;
	reset_msgs();
	cmdq_push(CMD_WALK);
	cmd_set_arg_direction(cmdq_peek(), "direction", 6);
	cmdq_pop(CTX_GAME);
	eq(player->upkeep->energy_use, 0);
	eq(player->previous_action[0], ACTION_NOTHING);
	require(strstr(last_msg, "You are too afraid to attack") != NULL);
	delete_monster(cave, grid);
	clear_all_timed();
	ok;
}

const char *suite_name = "player/timed-rules";
struct test tests[] = {
	{ "food_bands", test_food_bands },
	{ "digest", test_digest },
	{ "weak_str", test_weak_str },
	{ "stun_skills", test_stun_skills },
	{ "wake", test_wake },
	{ "tmp_per", test_tmp_per },
	{ "tmp_stat_restore", test_tmp_stat_restore },
	{ "trance_break", test_trance_break },
	{ "confuse", test_confuse },
	{ "trance_set", test_trance_set },
	{ "hunger_rate", test_hunger_rate },
	{ "see_invis_cures", test_see_invis_cures },
	{ "rage_map", test_rage_map },
	{ "rest_image", test_rest_image },
	{ "confused_wall", test_confused_wall },
	{ "afraid_bump", test_afraid_bump },
	{ NULL, NULL }
};
