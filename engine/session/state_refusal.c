#include "state_refusal.h"

#include <stdio.h>
#include <string.h>

int oracles_state_transitions_refusal(int taken_with_transitions, int enhanced, int transitions,
                                      char *message, size_t message_capacity, char *detail, size_t detail_capacity)
{
    if (message_capacity) message[0] = 0;
    if (detail_capacity) detail[0] = 0;
    transitions = enhanced && transitions;
    if ((taken_with_transitions != 0) == (transitions != 0)) return 0;
    if (taken_with_transitions && !enhanced) {
        /* In Faithful the transitions are greyed: the profile is what to change. */
        snprintf(message, message_capacity, "Refused: the savestate was taken in Enhanced, with continuous transitions");
        snprintf(detail, detail_capacity, "the savestate was taken in Enhanced with continuous transitions, and this session is Faithful "
                 "(--continuous-transitions, or the Enhanced profile in the launcher's Display): it loads only there");
        return 1;
    }
    const char *taken = taken_with_transitions ? "on" : "off", *now = transitions ? "on" : "off";
    snprintf(message, message_capacity, "Refused: the savestate was taken with continuous transitions %s", taken);
    snprintf(detail, detail_capacity, "the savestate was taken with continuous transitions %s, and this session has them %s "
             "(--continuous-transitions, or Continuous transitions in the launcher's Display): it loads only with them %s", taken, now, taken);
    return 1;
}

/* A core's name for the player, by its version's family ("sameboy-...", "mgba-..."), and the name --core takes. */
static const char *core_label(const char *version) { return !strncmp(version, "mgba-", 5) ? "Fast (mGBA)" : "Accurate (SameBoy)"; }
static const char *core_option(const char *version) { return !strncmp(version, "mgba-", 5) ? "mgba" : "sameboy"; }

int oracles_state_core_refusal(const char *taken_on, const char *now, char *message, size_t message_capacity, char *detail, size_t detail_capacity)
{
    if (message_capacity) message[0] = 0;
    if (detail_capacity) detail[0] = 0;
    if (!strcmp(core_option(taken_on), core_option(now))) return 0;
    snprintf(message, message_capacity, "Refused: taken on %s", core_label(taken_on));
    snprintf(detail, detail_capacity, "the savestate was taken on %s (%s) and this game runs on %s (%s): choose %s in Display's Core, or --core %s",
             core_label(taken_on), taken_on, core_label(now), now, core_label(taken_on), core_option(taken_on));
    return 1;
}

/* The digest of the compact text's layout a set of mods and gameplay options names (its word text-compact@, apart by
 * commas or spaces), "" without the compact text. */
static void compact_layout(const char *mods, char out[41])
{
    static const char word[] = "text-compact@";
    out[0] = 0;
    for (const char *at = mods; (at = strstr(at, word)) != NULL; at += sizeof word - 1u) {
        if (at != mods && at[-1] != ',' && at[-1] != ' ') continue;
        const char *digest = at + sizeof word - 1u;
        size_t n = 0;
        while (n < 40u && digest[n] && digest[n] != ',' && digest[n] != ' ') { out[n] = digest[n]; n++; }
        out[n] = 0;
        return;
    }
}

int oracles_state_text_refusal(const char *taken_mods, const char *mods, char *message, size_t message_capacity, char *detail, size_t detail_capacity)
{
    if (message_capacity) message[0] = 0;
    if (detail_capacity) detail[0] = 0;
    char taken[41], now[41];
    compact_layout(taken_mods, taken);
    compact_layout(mods, now);
    if (!strcmp(taken, now)) return 0;
    if (taken[0] && now[0]) {
        snprintf(message, message_capacity, "Refused: taken with another version of the compact text");
        snprintf(detail, detail_capacity, "the savestate was taken with the texts laid out by another version of the compact text (text-compact@%s, "
                 "now text-compact@%s): it loads only with that version", taken, now);
        return 1;
    }
    const char *text = taken[0] ? "Compact" : "Classic", *option = taken[0] ? "compact" : "classic";
    snprintf(message, message_capacity, "Refused: taken with the %s text: choose %s in Display's Text", text, text);
    snprintf(detail, detail_capacity, "the savestate was taken with the %s text and this session plays the %s text: it loads only with the %s text "
             "(%s in the launcher's Display's Text, text=%s in its settings, or --text %s)", option, taken[0] ? "classic" : "compact", option, text, option, option);
    return 1;
}
