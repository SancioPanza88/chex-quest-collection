#include <stdlib.h>
#include <string.h>

#include "doomfeatures.h"
#ifdef FEATURE_VITA_DEH
#include "deh_str.h"

#define MAX_REPLACEMENTS 384

typedef struct
{
    char *original;
    char *replacement;
} replacement_t;

static replacement_t replacements[MAX_REPLACEMENTS];
static int replacement_count;

char *DEH_String(char *text)
{
    int i;
    for (i = replacement_count - 1; i >= 0; --i)
    {
        if (strcmp(text, replacements[i].original) == 0)
        {
            return replacements[i].replacement;
        }
    }
    return text;
}

void DEH_AddStringReplacement(char *from_text, char *to_text)
{
    char *original_copy;
    char *replacement_copy;
    int i;

    for (i = replacement_count - 1; i >= 0; --i)
    {
        if (strcmp(from_text, replacements[i].original) == 0)
        {
            replacement_copy = malloc(strlen(to_text) + 1);
            if (replacement_copy == NULL) return;
            strcpy(replacement_copy, to_text);
            free(replacements[i].replacement);
            replacements[i].replacement = replacement_copy;
            return;
        }
    }
    if (replacement_count == MAX_REPLACEMENTS) return;

    original_copy = malloc(strlen(from_text) + 1);
    replacement_copy = malloc(strlen(to_text) + 1);
    if (original_copy == NULL || replacement_copy == NULL)
    {
        free(original_copy);
        free(replacement_copy);
        return;
    }
    strcpy(original_copy, from_text);
    strcpy(replacement_copy, to_text);
    replacements[replacement_count].original = original_copy;
    replacements[replacement_count].replacement = replacement_copy;
    ++replacement_count;
}
#endif
