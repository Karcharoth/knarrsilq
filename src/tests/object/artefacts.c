/* object/artefacts */
/* Spot checks of artefact data, record order and artefact lookup. */

#include "unit-test.h"
#include "test-utils.h"
#include "init.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "player-abilities.h"
#include "player-birth.h"
#include "player-history.h"
#include "z-rand.h"

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
	return 0;
}

int teardown_tests(void *state) {
	cleanup_angband();
	return 0;
}

static const struct artifact *art_named(const char *name, int tval)
{
	int i;

	for (i = 0; i < z_info->a_max; i++) {
		if (a_info[i].name && streq(a_info[i].name, name) &&
				a_info[i].tval == tval) {
			return &a_info[i];
		}
	}
	return NULL;
}

static bool art_has_ability(const struct artifact *art, const char *name)
{
	struct ability *a;

	for (a = art->abilities; a; a = a->next) {
		if (streq(a->name, name)) return true;
	}
	return false;
}

/* The special artefacts come first, in Sil-Q's index order */
static int test_order(void *state) {
	const char *specials[] = { "of Barahir", "of Melian", "of Tinfang Gelion",
		"'Nimphelos'", "'Elessar'", "of the Dwarves", "of Mairon",
		"of Daeron", "of Feanor", "of Maedhros", "of Andreth" };
	size_t i;

	for (i = 0; i < N_ELEMENTS(specials); i++) {
		require(a_info[i + 1].name);
		require(streq(a_info[i + 1].name, specials[i]));
	}
	/* Angrod is a helm between 'Burkfelek' and Curufin */
	for (i = 1; i < (size_t) z_info->a_max; i++) {
		if (a_info[i].name && streq(a_info[i].name, "of Angrod")) break;
	}
	require(i + 1 < (size_t) z_info->a_max);
	require(streq(a_info[i - 1].name, "'Burkfelek'"));
	require(streq(a_info[i + 1].name, "of Curufin"));
	eq(a_info[i].tval, TV_HELM);
	/* Removed artefacts are gone */
	null(art_named("of Dungalef", TV_SOFT_ARMOR));
	null(art_named("of Caranthir", TV_SOFT_ARMOR));
	null(art_named("of Gamil Zirak", TV_GLOVES));
	null(art_named("'Anglachel'", TV_SWORD));
	ok;
}

/* Grond's kind is two-handed; special template kinds have Sil-Q's levels */
static int test_kinds(void *state) {
	const struct artifact *grond = art_named("'Grond'", TV_HAFTED);
	const struct artifact *mairon = art_named("of Mairon", TV_RING);
	const struct artifact *melian = art_named("of Melian", TV_RING);
	const struct artifact *crown = art_named("of Morgoth", TV_CROWN);
	struct object_kind *k;

	notnull(grond);
	k = lookup_kind(grond->tval, grond->sval);
	require(of_has(k->flags, OF_TWO_HANDED));
	eq(k->level, 20);
	notnull(crown);
	eq(lookup_kind(crown->tval, crown->sval)->level, 20);
	notnull(mairon);
	notnull(melian);
	noteq(mairon->sval, melian->sval);
	eq(lookup_kind(mairon->tval, mairon->sval)->level, 1);
	eq(lookup_kind(melian->tval, melian->sval)->level, 1);
	ok;
}

/* Values copied from Sil-Q artefact.txt */
static int test_data(void *state) {
	const struct artifact *a;

	a = art_named("of Barahir", TV_RING);
	notnull(a);
	eq(a->pd, 1);
	eq(a->ps, 1);
	require(of_has(a->flags, OF_MEDIC));
	require(art_has_ability(a, "Song of Freedom"));
	a = art_named("of Feanor", TV_CROWN);
	notnull(a);
	eq(a->rarity, 20);
	eq(a->modifiers[OBJ_MOD_SMITHING], 0);
	require(art_has_ability(a, "Enchantment"));
	require(art_has_ability(a, "Masterpiece"));
	a = art_named("of Ogbar", TV_POLEARM);
	notnull(a);
	eq(a->level, 12);
	eq(a->weight, 50);
	eq(a->att, 2);
	eq(a->dd, 1);
	eq(a->ds, 14);
	a = art_named("of Avernien", TV_BOW);
	notnull(a);
	eq(a->dd, 4);
	eq(a->ds, 3);
	require(of_has(a->flags, OF_PROT_HALLU));
	a = art_named("'Agarlhang'", TV_SWORD);
	notnull(a);
	eq(a->modifiers[OBJ_MOD_WILL], -2);
	eq(a->modifiers[OBJ_MOD_CON], 2);
	a = art_named("of Amon Rudh", TV_MAIL);
	notnull(a);
	require(of_has(a->flags, OF_STAND_FAST));
	ok;
}

/* The two artefacts named "of Maedhros" are told apart by history text */
static int test_maedhros(void *state) {
	const struct artifact *crown = art_named("of Maedhros", TV_CROWN);
	const struct artifact *mail = art_named("of Maedhros", TV_MAIL);

	notnull(crown);
	notnull(mail);
	require(crown != mail);
	ptreq(history_lookup_artifact("of Maedhros",
		"Found the Hauberk of Maedhros"), mail);
	ptreq(history_lookup_artifact("of Maedhros",
		"Missed the Crown of Maedhros"), crown);
	ptreq(history_lookup_artifact("'Grond'", "Found 'Grond'"),
		art_named("'Grond'", TV_HAFTED));
	ok;
}

/* Looking up history artefacts leaves the random number state unchanged */
static int test_history_rng(void *state) {
	uint32_t before, after;

	state_i = 0;
	Rand_state_init(12345);
	Rand_quick = false;
	(void)history_lookup_artifact("of Maedhros",
		"Found the Hauberk of Maedhros");
	(void)history_lookup_artifact("'Grond'", "Found 'Grond'");
	after = randint0(1000000);
	state_i = 0;
	Rand_state_init(12345);
	Rand_quick = false;
	before = randint0(1000000);
	eq(before, after);
	ok;
}

const char *suite_name = "object/artefacts";
struct test tests[] = {
	{ "order", test_order },
	{ "kinds", test_kinds },
	{ "data", test_data },
	{ "maedhros", test_maedhros },
	{ "history-rng", test_history_rng },
	{ NULL, NULL }
};
