/* object/kinds */
/* Spot checks of object kind data, kind abilities, brands and flavours. */

#include "unit-test.h"
#include "test-utils.h"
#include "game-event.h"
#include "init.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-slays.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "player-abilities.h"
#include "player-birth.h"
#include "player-util.h"

static bool ability_msg;
static int n_msg;

static void msg_handler(game_event_type type, game_event_data *data,
		void *user)
{
	if (data->message.msg &&
			streq(data->message.msg, "You have gained the ability 'Dodging'.")) {
		ability_msg = true;
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

static struct object_kind *kind_of(int tval, const char *name)
{
	int sval = lookup_sval(tval, name);

	return (sval < 0) ? NULL : lookup_kind(tval, sval);
}

/* A kind left only as an artefact's base is a never-allocated dummy */
static bool artefact_dummy(int tval, const char *name)
{
	int sval = lookup_sval(tval, name);
	struct object_kind *k;

	if (sval < 0) return true;
	k = lookup_kind(tval, sval);
	return k && kf_has(k->kind_flags, KF_INSTA_ART) &&
		(!k->alloc || !k->alloc[0].chance);
}

/* Kinds Sil-Q does not have are gone */
static int test_removed(void *state) {
	eq(artefact_dummy(TV_SOFT_ARMOR, "Filthy Rag"), true);
	eq(artefact_dummy(TV_SWORD, "Broken Sword"), true);
	eq(artefact_dummy(TV_SWORD, "Deathblade"), true);
	eq(artefact_dummy(TV_HAFTED, "Sceptre"), true);
	eq(artefact_dummy(TV_CROWN, "Jewelled Crown"), true);
	eq(lookup_sval(TV_POLEARM, "Throwing Axe"), -1);
	eq(lookup_sval(TV_AMULET, "Adornment"), -1);
	eq(lookup_sval(TV_RING, "Perception"), -1);
	eq(lookup_sval(TV_RING, "Sustenance"), -1);
	eq(lookup_sval(TV_RING, "Damage"), -1);
	eq(lookup_sval(TV_RING, "Hunger"), -1);
	eq(lookup_sval(TV_STAFF, "Earthquakes"), -1);
	eq(lookup_sval(TV_STAFF, "Entrapment"), -1);
	eq(lookup_sval(TV_POTION, "Slow Poison"), -1);
	ok;
}

/* Sil-Q object.txt N:76 Hand Axe and N:22 Robe */
static int test_weapon_armour_data(void *state) {
	struct object_kind *k = kind_of(TV_POLEARM, "Hand Axe");

	require(k);
	eq(k->level, 2);
	eq(k->weight, 10);
	eq(k->att, -1);
	eq(k->dd, 4);
	eq(k->ds, 2);
	eq(of_has(k->flags, OF_THROWING), false);
	eq(of_has(k->flags, OF_AXE), true);
	k = kind_of(TV_SOFT_ARMOR, "Robe");
	require(k);
	eq(k->level, 1);
	eq(k->evn, 1);
	k = kind_of(TV_CROWN, "Crown");
	require(k);
	eq(kf_has(k->kind_flags, KF_INSTA_ART), true);
	eq(k->alloc[0].chance, 0);
	k = kind_of(TV_LIGHT, "Mallorn Torch");
	require(k);
	eq(k->pval, 3);
	eq(of_has(k->flags, OF_BURNS_OUT), true);
	k = kind_of(TV_HELM, "Rusty Helm");
	require(k);
	eq(of_has(k->flags, OF_DAMAGED), true);
	eq(k->alloc[0].chance, 0);
	ok;
}

/* Sil-Q object.txt B: lines on the Vigilant Eye, Secrets and Cowardice */
static int test_kind_abilities(void *state) {
	struct object_kind *k = kind_of(TV_RING, "Cowardice");

	require(k && k->abilities);
	require(streq(k->abilities->name, "Dodging"));
	eq(k->abilities->skill, SKILL_EVASION);
	require(k->abilities->desc);
	k = kind_of(TV_RING, "Secrets");
	require(k && k->abilities);
	require(streq(k->abilities->name, "Alchemy"));
	k = kind_of(TV_AMULET, "the Vigilant Eye");
	require(k && k->abilities);
	require(streq(k->abilities->name, "Keen Senses"));
	eq(k->abilities->skill, SKILL_PERCEPTION);
	k = kind_of(TV_RING, "Ered Luin");
	require(k);
	null(k->abilities);
	ok;
}

/* The Laiquendi always give +1 stealth and +1 archery (object2.c) */
static int test_laiquendi(void *state) {
	struct object_kind *k = kind_of(TV_RING, "the Laiquendi");
	struct object *obj;

	require(k);
	obj = object_new();
	object_prep(obj, k, 10, RANDOMISE);
	eq(obj->modifiers[OBJ_MOD_STEALTH], 1);
	eq(obj->modifiers[OBJ_MOD_ARCHERY], 1);
	object_delete(NULL, &obj);
	ok;
}

/* An unknown ring that grants an ability is noticed on wielding */
static int test_wield_ability_message(void *state) {
	struct object_kind *k = kind_of(TV_RING, "Cowardice");
	struct object *obj;

	require(k);
	obj = object_new();
	object_prep(obj, k, 0, MINIMISE);
	obj->number = 1;
	require(obj->abilities);
	k->aware = false;
	n_msg = 0;
	ability_msg = false;
	ident_on_wield(player, obj);
	require(n_msg >= 1);
	eq(ability_msg, true);
	eq(object_flavor_is_aware(obj), true);
	object_delete(NULL, &obj);
	ok;
}

/* Brand names as Sil-Q's obj-info.c describe_brand */
static int test_brand_names(void *state) {
	int i;
	int found = 0;

	for (i = 1; i < z_info->brand_max; i++) {
		if (streq(brands[i].code, "FIRE_1")) {
			require(streq(brands[i].name, "flame"));
			found++;
		} else if (streq(brands[i].code, "COLD_1")) {
			require(streq(brands[i].name, "frost"));
			found++;
		} else if (streq(brands[i].code, "POIS_1")) {
			require(streq(brands[i].name, "venom"));
			found++;
		}
	}
	eq(found, 3);
	ok;
}

/* Sil-Q flavor.txt: Limpid is Esgalduin's, ten easter egg flavours */
static int test_flavors(void *state) {
	struct flavor *f;
	int easter = 0;
	bool limpid = false, azure = false;

	for (f = flavors; f; f = f->next) {
		if (f->tval == TV_EASTER) {
			require(f->fidx >= 141 && f->fidx <= 150);
			easter++;
		}
		if (f->fidx == 196) {
			require(streq(f->text, "Limpid"));
			eq(f->sval, lookup_sval(TV_POTION, "Esgalduin"));
			limpid = true;
		}
		if (f->fidx == 221) {
			require(streq(f->text, "Azure"));
			azure = true;
		}
	}
	eq(easter, 10);
	eq(limpid, true);
	eq(azure, true);
	ok;
}

const char *suite_name = "object/kinds";
struct test tests[] = {
	{ "removed", test_removed },
	{ "weapon-armour-data", test_weapon_armour_data },
	{ "kind-abilities", test_kind_abilities },
	{ "laiquendi", test_laiquendi },
	{ "wield-ability-message", test_wield_ability_message },
	{ "brand-names", test_brand_names },
	{ "flavors", test_flavors },
	{ NULL, NULL }
};
