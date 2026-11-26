/* object/egos */
/* Spot checks of special item data and of applying a special to an object. */

#include "unit-test.h"
#include "test-utils.h"
#include "init.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "player-birth.h"

int setup_tests(void **state) {
	set_file_paths();
	init_angband();
#ifdef UNIX
	create_needed_dirs();
#endif
	return 0;
}

int teardown_tests(void *state) {
	cleanup_angband();
	return 0;
}

static struct ego_item *ego_for(const char *name, int tval, const char *kind)
{
	int sval = lookup_sval(tval, kind);

	return (sval < 0) ? NULL : lookup_ego_item(name, tval, sval);
}

static int count_egos(const char *name)
{
	int i, n = 0;

	for (i = 0; i < z_info->e_max; i++) {
		if (e_info[i].name && streq(e_info[i].name, name)) n++;
	}
	return n;
}

/* Sil-Q special.txt has 73 records; the removed NarSil ones are gone */
static int test_records(void *state) {
	int i, n = 0;

	for (i = 0; i < z_info->e_max; i++) {
		if (e_info[i].name) n++;
	}
	eq(n, 73);
	eq(count_egos("of Permanence"), 0);
	eq(count_egos("of Majesty"), 0);
	eq(count_egos("of Shadows"), 0);
	eq(count_egos("of Power"), 0);
	eq(count_egos("of Fire"), 0);
	eq(count_egos("of Command"), 0);
	eq(count_egos("of Quickness"), 0);
	eq(count_egos("of Free Action"), 0);
	eq(count_egos("of the Forge"), 0);
	eq(count_egos("of Strength"), 0);
	eq(count_egos("of Stealth"), 2);
	eq(count_egos("(Poisoned)"), 2);
	eq(count_egos("of Piercing"), 2);
	ok;
}

/* Same-named specials are told apart by kind (Sil-Q special.txt) */
static int test_same_names(void *state) {
	struct ego_item *e;

	/* S:373 polearms, S:643 arrows */
	e = ego_for("of Piercing", TV_POLEARM, "Glaive");
	require(e);
	eq(e->rarity, 8);
	eq(e->level, 4);
	eq(e->cost, 10000);
	e = ego_for("of Piercing", TV_ARROW, "Arrow");
	require(e);
	eq(e->rarity, 10);
	eq(e->level, 10);
	eq(e->cost, 300);
	/* S:420 melee, S:636 arrows */
	e = ego_for("(Poisoned)", TV_POLEARM, "Hand Axe");
	require(e);
	eq(e->cost, 2000);
	e = ego_for("(Poisoned)", TV_ARROW, "Arrow");
	require(e);
	eq(e->rarity, 6);
	/* S:104 soft armour, S:544 cloaks */
	e = ego_for("of Stealth", TV_CLOAK, "Cloak");
	require(e);
	eq(e->rarity, 2);
	e = ego_for("of Stealth", TV_SOFT_ARMOR, "Robe");
	require(e);
	eq(e->rarity, 1);
	ok;
}

/* Kind lists from Sil-Q T: lines */
static int test_kinds(void *state) {
	/* S:486-532: no special applies to crowns */
	null(ego_for("of Defiance", TV_CROWN, "Crown"));
	null(ego_for("of Grace", TV_CROWN, "Crown"));
	require(ego_for("of Grace", TV_HELM, "Mithril Helm"));
	null(ego_for("of Terror", TV_HELM, "Mithril Helm"));
	require(ego_for("of Terror", TV_HELM, "Rusty Helm"));
	require(ego_for("of True Sight", TV_LIGHT, "Mallorn Torch"));
	/* S:64 Protection: greaves, not cloaks */
	null(ego_for("of Protection", TV_CLOAK, "Cloak"));
	require(ego_for("of Protection", TV_BOOTS, "Pair of Shabby Boots"));
	/* S:208 Gondolin: no bows or hafted weapons */
	null(ego_for("of Gondolin", TV_BOW, "Longbow"));
	null(ego_for("of Gondolin", TV_HAFTED, "War Hammer"));
	require(ego_for("of Gondolin", TV_ARROW, "Arrow"));
	/* S:175 with Many Runes */
	require(ego_for("with Many Runes", TV_SHIELD, "Mithril Shield"));
	ok;
}

static struct object *make_with_ego(int tval, const char *kind,
		const char *ego)
{
	struct object *obj = object_new();
	struct object_kind *k = lookup_kind(tval, lookup_sval(tval, kind));

	object_prep(obj, k, 0, AVERAGE);
	obj->ego = ego_for(ego, tval, kind);
	ego_apply_magic(obj, false);
	return obj;
}

/*
 * S:476 of the Longbeards has max pval 0: the Mattock's pval 2 (object.txt
 * N:98) is read by both Strength and Tunnelling (object2.c:2534-2543).
 */
static int test_longbeards(void *state) {
	struct object *obj = make_with_ego(TV_DIGGING, "Mattock",
		"of the Longbeards");

	require(obj->ego);
	eq(obj->modifiers[OBJ_MOD_STR], 2);
	eq(obj->modifiers[OBJ_MOD_TUNNEL], 2);
	object_delete(NULL, &obj);
	ok;
}

/* S:741 of the Ironfists: pval p gives +p Con and -p Dex */
static int test_ironfists(void *state) {
	struct object *obj = make_with_ego(TV_GLOVES, "Set of Gauntlets",
		"of the Ironfists");

	require(obj->ego);
	eq(obj->modifiers[OBJ_MOD_CON], 1);
	eq(obj->modifiers[OBJ_MOD_DEX], -1);
	object_delete(NULL, &obj);
	ok;
}

const char *suite_name = "object/egos";
struct test tests[] = {
	{ "records", test_records },
	{ "same-names", test_same_names },
	{ "kinds", test_kinds },
	{ "longbeards", test_longbeards },
	{ "ironfists", test_ironfists },
	{ NULL, NULL }
};
