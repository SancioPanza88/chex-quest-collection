#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "d_englsh.h"
#include "d_items.h"
#include "d_think.h"
#include "deh_main.h"
#include "deh_str.h"
#include "doomtype.h"
#include "info.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_cheat.h"
#include "m_misc.h"
#include "sounds.h"
#include "st_stuff.h"
#include "w_wad.h"
#include "am_map.h"

#define DEH_LOG_PATH "ux0:/data/chexquest3/debug.log"
#define DEH_MAX_TEXT 1024
#define DEH_MAX_RENAMES 32
#define DEH_MAX_REPLACEMENTS 384

static const char *pos;
static const char *end;
static actionf_t original_actions[NUMSTATES];
static int actions_saved;
static char renamed_sprites[DEH_MAX_RENAMES][5];
static int renamed_sprite_count;
static char *replacement_sources[DEH_MAX_REPLACEMENTS];
static char *replacement_values[DEH_MAX_REPLACEMENTS];
static int replacement_count;
static int text_count;
static int cheat_count;
static int failed;

static void log_message(const char *format, ...)
{
    FILE *file = fopen(DEH_LOG_PATH, "a");
    if (file != NULL)
    {
        va_list args;
        va_start(args, format);
        vfprintf(file, format, args);
        va_end(args);
        fputc('\n', file);
        fclose(file);
    }
}

static int read_line(char *line, size_t size)
{
    size_t n = 0;
    if (pos >= end) return 0;
    while (pos < end)
    {
        char c = *pos++;
        if (c == '\n') break;
        if (c != '\r' && n + 1 < size) line[n++] = c;
    }
    line[n] = '\0';
    return 1;
}

static int read_bytes(char *buffer, size_t size)
{
    if (size > (size_t)(end - pos))
    {
        log_message("DEH: truncated text record");
        failed = 1;
        return 0;
    }
    memcpy(buffer, pos, size);
    pos += size;
    return 1;
}

static void consume_eol(void)
{
    if (pos < end && *pos == '\r') ++pos;
    if (pos < end && *pos == '\n') ++pos;
}

static char *trim(char *s)
{
    char *tail;
    while (*s == ' ' || *s == '\t') ++s;
    tail = s + strlen(s);
    while (tail > s && (tail[-1] == ' ' || tail[-1] == '\t')) *--tail = '\0';
    return s;
}

static int blank(const char *s)
{
    while (*s == ' ' || *s == '\t') ++s;
    return *s == '\0' || *s == '#';
}

static int begins(const char *s, const char *prefix)
{
    return strncasecmp(s, prefix, strlen(prefix)) == 0;
}

static int value_of(const char *s)
{
    const char *eq = strchr(s, '=');
    return eq == NULL ? 0 : atoi(eq + 1);
}

static void save_actions(void)
{
    int i;
    if (actions_saved) return;
    for (i = 0; i < NUMSTATES; ++i) original_actions[i] = states[i].action;
    actions_saved = 1;
}

static void parse_thing(int number)
{
    char line[256];
    mobjinfo_t *thing;
    if (number < 1 || number > NUMMOBJTYPES)
    {
        log_message("DEH: invalid Thing %d", number);
        failed = 1;
        return;
    }
    thing = &mobjinfo[number - 1];
    while (read_line(line, sizeof(line)))
    {
        char *field = trim(line);
        int v;
        if (blank(field)) return;
        v = value_of(field);
        if (begins(field, "ID #")) thing->doomednum = v;
        else if (begins(field, "Initial frame")) thing->spawnstate = v;
        else if (begins(field, "Hit points")) thing->spawnhealth = v;
        else if (begins(field, "First moving frame")) thing->seestate = v;
        else if (begins(field, "Alert sound")) thing->seesound = v;
        else if (begins(field, "Reaction time")) thing->reactiontime = v;
        else if (begins(field, "Attack sound")) thing->attacksound = v;
        else if (begins(field, "Injury frame")) thing->painstate = v;
        else if (begins(field, "Pain chance")) thing->painchance = v;
        else if (begins(field, "Pain sound")) thing->painsound = v;
        else if (begins(field, "Close attack frame")) thing->meleestate = v;
        else if (begins(field, "Far attack frame")) thing->missilestate = v;
        else if (begins(field, "Death frame")) thing->deathstate = v;
        else if (begins(field, "Exploding frame")) thing->xdeathstate = v;
        else if (begins(field, "Death sound")) thing->deathsound = v;
        else if (begins(field, "Speed")) thing->speed = v;
        else if (begins(field, "Width")) thing->radius = v;
        else if (begins(field, "Height")) thing->height = v;
        else if (begins(field, "Mass")) thing->mass = v;
        else if (begins(field, "Missile damage")) thing->damage = v;
        else if (begins(field, "Action sound")) thing->activesound = v;
        else if (begins(field, "Bits")) thing->flags = v;
        else if (begins(field, "Respawn frame")) thing->raisestate = v;
    }
}

static void parse_frame(int number)
{
    char line[256];
    state_t *state;
    if (number < 0 || number >= NUMSTATES)
    {
        log_message("DEH: invalid Frame %d", number);
        failed = 1;
        return;
    }
    state = &states[number];
    while (read_line(line, sizeof(line)))
    {
        char *field = trim(line);
        int v;
        if (blank(field)) return;
        v = value_of(field);
        if (begins(field, "Sprite number")) state->sprite = (spritenum_t)v;
        else if (begins(field, "Sprite subnumber")) state->frame = v;
        else if (begins(field, "Duration")) state->tics = v;
        else if (begins(field, "Next frame")) state->nextstate = (statenum_t)v;
        else if (begins(field, "Unknown 1")) state->misc1 = v;
        else if (begins(field, "Unknown 2")) state->misc2 = v;
    }
}

static void parse_pointer(const char *header)
{
    char line[256];
    const char *frame = strchr(header, '(');
    int target;
    if (frame == NULL || sscanf(frame, "(Frame %d)", &target) != 1 ||
        target < 0 || target >= NUMSTATES)
    {
        log_message("DEH: invalid Pointer header %s", header);
        failed = 1;
        return;
    }
    while (read_line(line, sizeof(line)))
    {
        char *field = trim(line);
        int code;
        if (blank(field)) return;
        if (!begins(field, "Codep Frame")) continue;
        code = value_of(field);
        if (code < 0 || code >= NUMSTATES)
        {
            log_message("DEH: invalid Codep Frame %d", code);
            failed = 1;
        }
        else states[target].action = original_actions[code];
    }
}

static void parse_weapon(int number)
{
    char line[256];
    weaponinfo_t *weapon;
    if (number < 0 || number >= NUMWEAPONS)
    {
        log_message("DEH: invalid Weapon %d", number);
        failed = 1;
        return;
    }
    weapon = &weaponinfo[number];
    while (read_line(line, sizeof(line)))
    {
        char *field = trim(line);
        int v;
        if (blank(field)) return;
        v = value_of(field);
        if (begins(field, "Ammo type")) weapon->ammo = (ammotype_t)v;
        else if (begins(field, "Deselect frame")) weapon->upstate = v;
        else if (begins(field, "Select frame")) weapon->downstate = v;
        else if (begins(field, "Bobbing frame")) weapon->readystate = v;
        else if (begins(field, "Shooting frame")) weapon->atkstate = v;
        else if (begins(field, "Firing frame")) weapon->flashstate = v;
    }
}

static void parse_text(int old_length, int new_length)
{
    char old_text[DEH_MAX_TEXT + 1];
    char new_text[DEH_MAX_TEXT + 1];
    char *stored;
    int i;

    if (old_length < 0 || new_length < 0 || old_length > DEH_MAX_TEXT || new_length > DEH_MAX_TEXT)
    {
        log_message("DEH: unsupported Text lengths %d -> %d", old_length, new_length);
        failed = 1;
        return;
    }
    if (!read_bytes(old_text, (size_t)old_length)) return;
    old_text[old_length] = '\0';
    consume_eol();
    if (!read_bytes(new_text, (size_t)new_length)) return;
    new_text[new_length] = '\0';

    if (old_length == 4 && new_length == 4)
    {
        for (i = 0; i < NUMSPRITES; ++i)
        {
            if (sprnames[i] != NULL && strncmp(sprnames[i], old_text, 4) == 0)
            {
                if (renamed_sprite_count >= DEH_MAX_RENAMES)
                {
                    failed = 1;
                    return;
                }
                memcpy(renamed_sprites[renamed_sprite_count], new_text, 4);
                renamed_sprites[renamed_sprite_count][4] = '\0';
                sprnames[i] = renamed_sprites[renamed_sprite_count++];
                break;
            }
        }
    }

    for (i = 0; i < replacement_count; ++i)
    {
        if (strcmp(replacement_sources[i], old_text) == 0)
        {
            stored = malloc((size_t)new_length + 1);
            if (stored == NULL)
            {
                failed = 1;
                return;
            }
            memcpy(stored, new_text, (size_t)new_length + 1);
            free(replacement_values[i]);
            replacement_values[i] = stored;
            DEH_AddStringReplacement(replacement_sources[i], stored);
            ++text_count;
            return;
        }
    }
    if (replacement_count >= DEH_MAX_REPLACEMENTS)
    {
        failed = 1;
        log_message("DEH: too many Text replacements");
        return;
    }
    stored = malloc((size_t)old_length + 1);
    if (stored == NULL)
    {
        failed = 1;
        return;
    }
    memcpy(stored, old_text, (size_t)old_length + 1);
    replacement_sources[replacement_count] = stored;
    replacement_values[replacement_count] = malloc((size_t)new_length + 1);
    if (replacement_values[replacement_count] == NULL)
    {
        failed = 1;
        return;
    }
    memcpy(replacement_values[replacement_count], new_text, (size_t)new_length + 1);
    DEH_AddStringReplacement(stored, replacement_values[replacement_count]);
    ++replacement_count;
    ++text_count;
}

static void skip_block(void)
{
    char line[256];
    while (read_line(line, sizeof(line))) if (blank(line)) return;
}

static void set_cheat(cheatseq_t *cheat, const char *sequence)
{
    size_t n = strlen(sequence);
    if (n >= sizeof(cheat->sequence))
    {
        log_message("DEH: cheat sequence too long");
        failed = 1;
        return;
    }
    memcpy(cheat->sequence, sequence, n + 1);
    cheat->sequence_len = n;
    if (cheat == &cheat_mus || cheat == &cheat_clev) cheat->parameter_chars = 2;
    cheat->chars_read = 0;
    cheat->param_chars_read = 0;
    memset(cheat->parameter_buf, 0, sizeof(cheat->parameter_buf));
}

static void parse_cheat(void)
{
    char line[256];
    while (read_line(line, sizeof(line)))
    {
        char *field = trim(line);
        char *eq;
        char *name;
        char *value;
        cheatseq_t *cheat = NULL;
        int i;
        static const char *names[] = {
            "Change music", "Chainsaw", "God mode", "Ammo & Keys", "Ammo",
            "No Clipping 1", "No Clipping 2", "Invincibility", "Berserk",
            "Invisibility", "Radiation Suit", "Auto-map", "Lite-Amp Goggles",
            "BEHOLD menu", "Level Warp", "Player Position", "Map cheat"
        };
        cheatseq_t *cheats[] = {
            &cheat_mus, &cheat_choppers, &cheat_god, &cheat_ammo, &cheat_ammonokey,
            &cheat_noclip, &cheat_commercial_noclip, &cheat_powerup[0], &cheat_powerup[1],
            &cheat_powerup[2], &cheat_powerup[3], &cheat_powerup[4], &cheat_powerup[5],
            &cheat_powerup[6], &cheat_clev, &cheat_mypos, &cheat_amap
        };
        if (blank(field)) return;
        eq = strchr(field, '=');
        if (eq == NULL)
        {
            failed = 1;
            log_message("DEH: malformed Cheat field %s", field);
            continue;
        }
        *eq = '\0';
        name = trim(field);
        value = trim(eq + 1);
        for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); ++i)
        {
            if (strcasecmp(name, names[i]) == 0)
            {
                cheat = cheats[i];
                break;
            }
        }
        if (cheat == NULL)
        {
            failed = 1;
            log_message("DEH: unsupported Cheat field %s", name);
            continue;
        }
        set_cheat(cheat, value);
        ++cheat_count;
    }
}

static void parse_data(const char *data, size_t length)
{
    char line[256];
    pos = data;
    end = data + length;
    save_actions();
    while (read_line(line, sizeof(line)))
    {
        char *header = trim(line);
        if (blank(header)) continue;
        if (begins(header, "Patch File for DeHackEd") || begins(header, "Doom version") || begins(header, "Patch format")) continue;
        if (begins(header, "Thing ")) parse_thing(atoi(header + 6));
        else if (begins(header, "Frame ")) parse_frame(atoi(header + 6));
        else if (begins(header, "Pointer ")) parse_pointer(header);
        else if (begins(header, "Weapon ")) parse_weapon(atoi(header + 7));
        else if (begins(header, "Text "))
        {
            int old_length, new_length;
            if (sscanf(header + 5, "%d %d", &old_length, &new_length) != 2)
            {
                failed = 1;
                log_message("DEH: malformed Text header %s", header);
            }
            else parse_text(old_length, new_length);
        }
        else if (begins(header, "Cheat ")) parse_cheat();
        else if (begins(header, "Sound "))
        {
            int sound = atoi(header + 6);
            if (sound < 0 || sound >= NUMSFX)
            {
                failed = 1;
                log_message("DEH: invalid Sound %d", sound);
                continue;
            }
            while (read_line(line, sizeof(line)))
            {
                char *field = trim(line);
                if (blank(field)) break;
                if (begins(field, "Value")) S_sfx[sound].priority = value_of(field);
            }
        }
        else if (begins(header, "Ammo ") || begins(header, "Misc ")) skip_block();
        else if (begins(header, "[STRINGS]")) skip_block();
        else if (begins(header, "[CODEPTR]")) skip_block();
        else if (strchr(header, '=') != NULL) continue;
        else
        {
            log_message("DEH: ignoring unsupported record %s", header);
        }
    }
}

void DEH_LoadFromFile(const char *path)
{
    FILE *file;
    long length;
    char *data;

    file = fopen(path, "rb");
    if (file == NULL)
    {
        log_message("DEH: missing required patch %s", path);
        I_Error("Required DeHackEd patch is missing: %s", path);
        return;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
        length > 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        log_message("DEH: invalid patch file %s", path);
        I_Error("Invalid DeHackEd patch file: %s", path);
        return;
    }
    data = malloc((size_t)length + 1);
    if (data == NULL || fread(data, 1, (size_t)length, file) != (size_t)length)
    {
        free(data);
        fclose(file);
        I_Error("Could not read DeHackEd patch: %s", path);
        return;
    }
    fclose(file);
    data[length] = '\0';
    failed = 0;
    replacement_count = 0;
    renamed_sprite_count = 0;
    text_count = 0;
    cheat_count = 0;
    parse_data(data, (size_t)length);
    free(data);
    if (failed)
    {
        log_message("DEH: rejected patch %s", path);
        I_Error("DeHackEd patch contains unsupported or invalid records");
        return;
    }
    DEH_AddStringReplacement(E1TEXT, "");
    DEH_AddStringReplacement(E2TEXT, "");
    DEH_AddStringReplacement(E3TEXT, "");
    log_message("DEH: loaded %ld bytes, %d text and %d cheat records from %s",
                length, text_count, cheat_count, path);
}
