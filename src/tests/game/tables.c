/* game/tables.c */
/*
 * Check game constants, flags and table entries loaded from the data files.
 */

#include "unit-test.h"
#include "test-utils.h"
#include "cave.h"
#include "generate.h"
#include "init.h"
#include "mon-init.h"
#include "mon-move.h"
#include "mon-spell.h"
#include "mon-util.h"
#include "monster.h"
#include "obj-init.h"
#include "obj-properties.h"
#include "obj-slays.h"
#include "obj-util.h"
#include "object.h"
#include "player.h"
#include "player-timed.h"
#include "project.h"
#include "z-color.h"
#include "z-rand.h"

int setup_tests(void **state) {
	read_edit_files();
	*state = 0;
	return 0;
}

int teardown_tests(void *state) {
	return 0;
}

static int test_constants(void *state) {
	eq(z_info->flee_range, 40);
	eq(z_info->turn_range, 3);
	eq(z_info->fuel_mallorn, 100);
	eq(z_info->fixed_exp, 50000);
	eq(z_info->channeling_mult, 2);
	ok;
}

static int test_flee_range(void *state) {
	struct monster_race race = { 0 };
	struct monster mon = { 0 };
	struct player *saved = player;
	struct player p = { 0 };

	player = &p;
	p.state.speed = 2;
	mon.race = &race;
	mon.mspeed = 2;
	mon.cdis = 10;
	mon.stance = STANCE_FLEEING;
	monster_find_range(&mon);
	player = saved;
	eq(mon.min_range, 40);
	eq(mon.best_range, 40);
	ok;
}

static int check_spell(int index, int mana, int best_range, int desire,
		int use_past_range)
{
	const struct monster_spell *spell = monster_spell_by_index(index);

	if (!spell) return 0;
	return spell->mana == mana && spell->best_range == best_range &&
		spell->desire == desire && spell->use_past_range == use_past_range;
}

static int test_monster_spells(void *state) {
	require(check_spell(RSF_ARROW1, 0, 4, 100, 100));
	require(check_spell(RSF_ARROW2, 0, 4, 100, 100));
	require(check_spell(RSF_BOULDER, 0, 4, 50, 100));
	require(check_spell(RSF_BR_FIRE, 10, 2, 100, 50));
	require(check_spell(RSF_BR_COLD, 10, 2, 100, 50));
	require(check_spell(RSF_BR_POIS, 10, 2, 100, 50));
	require(check_spell(RSF_BR_DARK, 10, 2, 100, 50));
	require(check_spell(RSF_EARTHQUAKE, 10, 2, 50, 0));
	require(check_spell(RSF_SHRIEK, 10, 0, 50, 100));
	require(check_spell(RSF_SCREECH, 0, 2, 50, 100));
	require(check_spell(RSF_DARKNESS, 10, 0, 50, 100));
	require(check_spell(RSF_FORGET, 10, 0, 50, 100));
	require(check_spell(RSF_SCARE, 10, 0, 50, 100));
	require(check_spell(RSF_CONF, 10, 0, 50, 100));
	require(check_spell(RSF_HOLD, 10, 0, 50, 100));
	require(check_spell(RSF_SLOW, 10, 0, 50, 100));
	require(check_spell(RSF_SNG_BIND, 10, 0, 50, 100));
	require(check_spell(RSF_SNG_PIERCE, 10, 0, 0, 0));
	require(check_spell(RSF_SNG_OATHS, 10, 0, 50, 100));
	require(check_spell(RSF_HATCH_SPIDER, 10, 0, 50, 100));
	require(check_spell(RSF_DIM, 10, 0, 50, 100));
	require(check_spell(RSF_THROW_WEB, 10, 0, 50, 100));
	require(check_spell(RSF_RALLY, 10, 0, 50, 100));
	ok;
}

static int test_feanorian_lamp(void *state) {
	struct object_kind *kind = lookup_kind(TV_LIGHT,
		lookup_sval(TV_LIGHT, "Feanorian Lamp"));

	notnull(kind);
	eq(kind->pval, 4);
	ok;
}

static int test_object_flags(void *state) {
	int flags[] = { OF_PROT_BLEED, OF_TRAITOR, OF_DAMAGED, OF_CHEAT_DEATH,
		OF_STAND_FAST, OF_ACCURATE, OF_CUMBERSOME, OF_AVOID_TRAPS,
		OF_MEDIC };
	size_t i;

	for (i = 0; i < N_ELEMENTS(flags); i++) {
		notnull(lookup_obj_property(OBJ_PROPERTY_FLAG, flags[i]));
	}
	ok;
}

static int test_slays(void *state) {
	int man = lookup_slay("MAN_1"), elf = lookup_slay("ELF_1");

	require(man > 0);
	require(elf > 0);
	eq(slays[man].race_flag, RF_MAN);
	eq(slays[elf].race_flag, RF_ELF);
	eq(slays[man].dice, 1);
	eq(slays[elf].dice, 1);
	require(streq(slays[man].name, "men"));
	require(streq(slays[elf].name, "elves"));
	/* Existing slays keep their place */
	require(streq(slays[lookup_slay("ORC_1")].name, "orcs"));
	ok;
}

static int test_race_flags(void *state) {
	struct monster_race r1, r2;
	struct monster m1, m2;

	require(streq(describe_race_flag(RF_MAN), "man"));
	require(streq(describe_race_flag(RF_ELF), "elf"));

	/* Sharing MAN or ELF alone does not make monsters similar */
	memset(&r1, 0, sizeof(r1));
	memset(&r2, 0, sizeof(r2));
	memset(&m1, 0, sizeof(m1));
	memset(&m2, 0, sizeof(m2));
	r1.base = lookup_monster_base("person");
	r2.base = lookup_monster_base("wolf");
	notnull(r1.base);
	notnull(r2.base);
	rf_on(r1.flags, RF_MAN);
	rf_on(r2.flags, RF_MAN);
	rf_on(r1.flags, RF_ELF);
	rf_on(r2.flags, RF_ELF);
	m1.race = &r1;
	m2.race = &r2;
	require(!similar_monsters(&m1, &m2));
	rf_on(r1.flags, RF_ORC);
	rf_on(r2.flags, RF_ORC);
	require(similar_monsters(&m1, &m2));
	ok;
}

static int test_monster_parse(void *state) {
	struct parser *p = monster_parser.init();
	enum parser_error r;

	notnull(p);
	r = parser_parse(p, "name:Test thrall");
	eq(r, PARSE_ERROR_NONE);
	r = parser_parse(p, "flags:PEACEFUL | NEVER_BLOW | MAN | ELF");
	eq(r, PARSE_ERROR_NONE);
	r = parser_parse(p, "spells:HATCH_SPIDER | DIM | THROW_WEB | RALLY");
	eq(r, PARSE_ERROR_NONE);
	parser_destroy(p);
	ok;
}

static int test_kind_parse(void *state) {
	struct parser *p = object_parser.init();
	struct object_kind *k;
	enum parser_error r;

	notnull(p);
	r = parser_parse(p, "name:Test");
	eq(r, PARSE_ERROR_NONE);
	r = parser_parse(p, "flags:PROT_BLEED | TRAITOR | DAMAGED | CHEAT_DEATH");
	eq(r, PARSE_ERROR_NONE);
	r = parser_parse(p, "flags:STAND_FAST | ACCURATE | CUMBERSOME");
	eq(r, PARSE_ERROR_NONE);
	r = parser_parse(p, "flags:AVOID_TRAPS | MEDIC | MORE_SPECIAL");
	eq(r, PARSE_ERROR_NONE);
	r = parser_parse(p, "slay:MAN_1");
	eq(r, PARSE_ERROR_NONE);
	r = parser_parse(p, "slay:ELF_1");
	eq(r, PARSE_ERROR_NONE);
	k = parser_priv(p);
	notnull(k);
	require(of_has(k->flags, OF_TRAITOR));
	require(of_has(k->flags, OF_MEDIC));
	require(kf_has(k->kind_flags, KF_MORE_SPECIAL));
	parser_destroy(p);
	ok;
}

static int test_vault_parse(void *state) {
	struct parser *p = init_parse_vault();
	struct vault *v;
	enum parser_error r;

	notnull(p);
	r = parser_parse(p, "name:Test");
	eq(r, PARSE_ERROR_NONE);
	r = parser_parse(p, "flags:SURFACE | LIGHT");
	eq(r, PARSE_ERROR_NONE);
	v = parser_priv(p);
	notnull(v);
	require(roomf_has(v->flags, ROOMF_SURFACE));
	parser_destroy(p);
	ok;
}

static int test_race_parse(void *state) {
	struct parser *p = race_parser.init();
	struct player_race *pr;
	enum parser_error r;

	notnull(p);
	r = parser_parse(p, "name:Test");
	eq(r, PARSE_ERROR_NONE);
	r = parser_parse(p, "player-flags:BOW_PROFICIENCY");
	eq(r, PARSE_ERROR_NONE);
	pr = parser_priv(p);
	notnull(pr);
	require(pf_has(pr->pflags, PF_BOW_PROFICIENCY));
	parser_destroy(p);
	ok;
}

static int test_projections(void *state) {
	const struct projection *web = &projections[PROJ_WEB];
	const struct projection *lock = &projections[PROJ_LOCK_DOOR];
	struct parser *p;

	/* One record for each element and each projection, in list order */
	eq(z_info->projection_max, PROJ_MAX);
	require(streq(projections[PROJ_KILL_DOOR].desc,
		"destroys doors"));
	require(streq(projections[PROJ_KILL_TRAP].desc,
		"disables traps"));
	require(streq(projections[PROJ_DISP_ALL].desc,
		"damages monsters"));
	require(streq(projections[PROJ_ARROW].name, "arrows"));
	null(projections[PROJ_NOTHING].desc);
	require(streq(lock->type, "environs"));
	require(streq(lock->desc, "locks doors"));
	eq(lock->color, COLOUR_L_WHITE);
	require(streq(web->type, "environs"));
	eq(web->color, COLOUR_L_UMBER);

	/* The parser refuses records out of list order */
	p = projection_parser.init();
	notnull(p);
	eq(parser_parse(p, "code:ACID"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "code:FIRE"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "code:COLD"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "code:POIS"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "code:DARK"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "code:HURT"), PARSE_ERROR_PROJECTION_NAME_MISMATCH);
	parser_destroy(p);
	ok;
}

static int test_terrain(void *state) {
	int warded[] = { FEAT_WARDED, FEAT_WARDED2, FEAT_WARDED3 };
	uint8_t colors[] = { COLOUR_L_GREEN, COLOUR_L_BLUE, COLOUR_VIOLET };
	size_t i;

	for (i = 0; i < N_ELEMENTS(warded); i++) {
		struct feature *f = &f_info[warded[i]];

		require(streq(f->name, "warded door"));
		eq(f->d_char, L'+');
		eq(f->d_attr, colors[i]);
		require(tf_has(f->flags, TF_DOOR_ANY));
		require(tf_has(f->flags, TF_DOOR_CLOSED));
		require(!tf_has(f->flags, TF_LOS));
		require(!tf_has(f->flags, TF_PROJECT));
		require(!tf_has(f->flags, TF_PASSABLE));
	}
	require(streq(f_info[FEAT_SUNLIGHT].name, "patch of sunlight"));
	eq(f_info[FEAT_SUNLIGHT].d_char, L'.');
	eq(f_info[FEAT_SUNLIGHT].d_attr, COLOUR_YELLOW);
	require(tf_has(f_info[FEAT_SUNLIGHT].flags, TF_PASSABLE));
	require(tf_has(f_info[FEAT_SUNLIGHT].flags, TF_LOS));
	require(tf_has(f_info[FEAT_SUNLIGHT].flags, TF_PROJECT));
	ok;
}

static int test_timed_per(void *state) {
	const struct timed_grade *g = timed_effects[TMD_PER].grade;

	while (g && !g->up_msg) g = g->next;
	notnull(g);
	eq(g->max, 10000);
	require(streq(g->up_msg, "You feel your perceptions sharpen."));
	require(streq(timed_effects[TMD_PER].on_end,
		"Your perception returns to normal."));
	ok;
}

static int test_div_round(void *state) {
	eq(div_round(5, 2), 3);
	eq(div_round(4, 3), 1);
	eq(div_round(-5, 2), -3);
	eq(div_round(7, 0), 7);
	eq(div_round(6, 3), 2);
	eq(div_round(-4, 3), -1);
	eq(div_round(0, 5), 0);
	ok;
}

const char *suite_name = "game/tables";
struct test tests[] = {
	{ "constants", test_constants },
	{ "flee_range", test_flee_range },
	{ "monster_spells", test_monster_spells },
	{ "feanorian_lamp", test_feanorian_lamp },
	{ "object_flags", test_object_flags },
	{ "slays", test_slays },
	{ "race_flags", test_race_flags },
	{ "monster_parse", test_monster_parse },
	{ "kind_parse", test_kind_parse },
	{ "vault_parse", test_vault_parse },
	{ "race_parse", test_race_parse },
	{ "projections", test_projections },
	{ "terrain", test_terrain },
	{ "timed_per", test_timed_per },
	{ "div_round", test_div_round },
	{ NULL, NULL }
};
