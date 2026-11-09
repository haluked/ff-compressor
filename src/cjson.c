/* SPDX-License-Identifier: MIT */
/*
  cJSON - An ultra-lightweight JSON parser in C.
  Derived from Dave Gamble's cJSON (MIT License).
*/

#include "cjson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

static void *cJSON_malloc(size_t size) { return malloc(size); }
static void cJSON_free(void *ptr) { free(ptr); }

static cJSON *cJSON_New_Item(void)
{
    cJSON* node = (cJSON*)cJSON_malloc(sizeof(cJSON));
    if (node) memset(node, 0, sizeof(cJSON));
    return node;
}

void cJSON_Delete(cJSON *c)
{
    cJSON *next;
    while (c)
    {
        next = c->next;
        if (!(c->type & cJSON_IsReference) && c->child) cJSON_Delete(c->child);
        if (!(c->type & cJSON_IsReference) && c->valuestring) cJSON_free(c->valuestring);
        if (!(c->type & cJSON_StringIsConst) && c->string) cJSON_free(c->string);
        cJSON_free(c);
        c = next;
    }
}

static const char *skip(const char *in)
{
    while (in && *in && (unsigned char)*in <= 32) in++;
    return in;
}

static const char *parse_value(cJSON *item, const char *value, int depth);

/* Prevents stack overflow on excessively nested (malformed/attack) JSON. */
#define CJSON_MAX_DEPTH 64

static const char *parse_number(cJSON *item, const char *num)
{
    double n = 0;
    char *endptr = NULL;
    n = strtod(num, &endptr);
    if (endptr == num) return NULL;
    item->valuedouble = n;
    item->valueint = (int)n;
    item->type = cJSON_Number;
    return endptr;
}

static unsigned int parse_hex4(const char *p, int *ok)
{
    unsigned int v = 0;
    *ok = 1;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v += (unsigned int)(c - '0');
        else if (c >= 'a' && c <= 'f') v += (unsigned int)(c - 'a') + 10;
        else if (c >= 'A' && c <= 'F') v += (unsigned int)(c - 'A') + 10;
        else { *ok = 0; return 0; }
    }
    return v;
}

static const char *parse_string(cJSON *item, const char *str)
{
    const char *ptr = str + 1;
    char *ptr2;
    char *out;
    int len = 0;
    if (*str != '\"') return NULL;

    while (*ptr && *ptr != '\"' && ++len)
    {
        if (*ptr == '\\')
        {
            ptr++;
            if (!*ptr) return NULL;   /* truncated input: escape sequence left incomplete */
        }
        ptr++;
    }

    out = (char*)cJSON_malloc(len + 1);
    if (!out) return NULL;

    ptr = str + 1;
    ptr2 = out;
    while (*ptr && *ptr != '\"')
    {
        if (*ptr != '\\') *ptr2++ = *ptr++;
        else
        {
            ptr++;
            if (!*ptr) { cJSON_free(out); return NULL; }  /* kesik girdi */
            switch (*ptr)
            {
                case 'b': *ptr2++ = '\b'; break;
                case 'f': *ptr2++ = '\f'; break;
                case 'n': *ptr2++ = '\n'; break;
                case 'r': *ptr2++ = '\r'; break;
                case 't': *ptr2++ = '\t'; break;
                case '\"': case '\\': case '/': *ptr2++ = *ptr; break;
                case 'u':
                {
                    /* \uXXXX escape. Characters such as emoji arrive as a UTF-16 surrogate
                       pair (\uD83D\uDE00); these must be converted into a SINGLE
                       correct UTF-8 code point instead of two separate 3-byte
                       sequences. */
                    int hi_ok = 0, lo_ok = 0;
                    unsigned int uc = parse_hex4(ptr + 1, &hi_ok);
                    if (!hi_ok) { *ptr2++ = '?'; break; }
                    ptr += 4;

                    if (uc >= 0xD800 && uc <= 0xDBFF &&
                        ptr[1] == '\\' && ptr[2] == 'u')
                    {
                        unsigned int lo = parse_hex4(ptr + 3, &lo_ok);
                        if (lo_ok && lo >= 0xDC00 && lo <= 0xDFFF)
                        {
                            unsigned int cp = 0x10000 + ((uc - 0xD800) << 10) + (lo - 0xDC00);
                            *ptr2++ = (char)(0xF0 | (cp >> 18));
                            *ptr2++ = (char)(0x80 | ((cp >> 12) & 0x3F));
                            *ptr2++ = (char)(0x80 | ((cp >> 6) & 0x3F));
                            *ptr2++ = (char)(0x80 | (cp & 0x3F));
                            ptr += 6;  /* we also consumed the low surrogate escape */
                            break;
                        }
                    }
                    if (uc >= 0xD800 && uc <= 0xDFFF) {
                        /* unmatched surrogate: use a deterministic '?' instead of invalid UTF-8 */
                        *ptr2++ = '?';
                        break;
                    }
                    if (uc < 0x80) *ptr2++ = (char)uc;
                    else if (uc < 0x800) {
                        *ptr2++ = (char)(0xC0 | (uc >> 6));
                        *ptr2++ = (char)(0x80 | (uc & 0x3F));
                    } else {
                        *ptr2++ = (char)(0xE0 | (uc >> 12));
                        *ptr2++ = (char)(0x80 | ((uc >> 6) & 0x3F));
                        *ptr2++ = (char)(0x80 | (uc & 0x3F));
                    }
                    break;
                }
                default: *ptr2++ = *ptr; break;
            }
            ptr++;
        }
    }
    *ptr2 = 0;
    if (*ptr == '\"') ptr++;
    item->valuestring = out;
    item->type = cJSON_String;
    return ptr;
}

static const char *parse_array(cJSON *item, const char *value, int depth)
{
    cJSON *child;
    if (*value != '[') return NULL;

    item->type = cJSON_Array;
    value = skip(value + 1);
    if (*value == ']') return value + 1;

    item->child = child = cJSON_New_Item();
    if (!item->child) return NULL;
    value = skip(parse_value(child, skip(value), depth + 1));
    if (!value) return NULL;

    while (*value == ',')
    {
        cJSON *new_item = cJSON_New_Item();
        if (!new_item) return NULL;
        child->next = new_item;
        new_item->prev = child;
        child = new_item;
        value = skip(parse_value(child, skip(value + 1), depth + 1));
        if (!value) return NULL;
    }

    if (*value == ']') return value + 1;
    return NULL;
}

static const char *parse_object(cJSON *item, const char *value, int depth)
{
    cJSON *child;
    if (*value != '{') return NULL;

    item->type = cJSON_Object;
    value = skip(value + 1);
    if (*value == '}') return value + 1;

    item->child = child = cJSON_New_Item();
    if (!item->child) return NULL;
    value = skip(parse_string(child, skip(value)));
    if (!value) return NULL;
    child->string = child->valuestring;
    child->valuestring = NULL;
    if (*value != ':') return NULL;
    value = skip(parse_value(child, skip(value + 1), depth + 1));
    if (!value) return NULL;

    while (*value == ',')
    {
        cJSON *new_item = cJSON_New_Item();
        if (!new_item) return NULL;
        child->next = new_item;
        new_item->prev = child;
        child = new_item;
        value = skip(parse_string(child, skip(value + 1)));
        if (!value) return NULL;
        child->string = child->valuestring;
        child->valuestring = NULL;
        if (*value != ':') return NULL;
        value = skip(parse_value(child, skip(value + 1), depth + 1));
        if (!value) return NULL;
    }

    if (*value == '}') return value + 1;
    return NULL;
}

static const char *parse_value(cJSON *item, const char *value, int depth)
{
    if (!value) return NULL;
    if (depth > CJSON_MAX_DEPTH) return NULL;
    if (!strncmp(value, "null", 4)) { item->type = cJSON_NULL; return value + 4; }
    if (!strncmp(value, "false", 5)) { item->type = cJSON_False; return value + 5; }
    if (!strncmp(value, "true", 4)) { item->type = cJSON_True; item->valueint = 1; return value + 4; }
    if (*value == '\"') return parse_string(item, value);
    if (*value == '-' || (*value >= '0' && *value <= '9')) return parse_number(item, value);
    if (*value == '[') return parse_array(item, value, depth);
    if (*value == '{') return parse_object(item, value, depth);
    return NULL;
}

cJSON *cJSON_Parse(const char *value)
{
    cJSON *c = cJSON_New_Item();
    if (!c) return NULL;
    if (!parse_value(c, skip(value), 0))
    {
        cJSON_Delete(c);
        return NULL;
    }
    return c;
}

int cJSON_GetArraySize(const cJSON *array)
{
    cJSON *c = array ? array->child : NULL;
    int i = 0;
    while (c) { i++; c = c->next; }
    return i;
}

cJSON *cJSON_GetArrayItem(const cJSON *array, int index)
{
    cJSON *c = array ? array->child : NULL;
    while (c && index > 0) { index--; c = c->next; }
    return c;
}

cJSON *cJSON_GetObjectItem(const cJSON * const object, const char * const string)
{
    return cJSON_GetObjectItemCaseSensitive(object, string);
}

cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string)
{
    cJSON *c = object ? object->child : NULL;
    while (c)
    {
        if (c->string && strcmp(c->string, string) == 0) return c;
        c = c->next;
    }
    return NULL;
}

int cJSON_HasObjectItem(const cJSON *object, const char *string)
{
    return cJSON_GetObjectItem(object, string) != NULL;
}

int cJSON_IsInvalid(const cJSON * const item) { return item ? (item->type & 0xFF) == cJSON_Invalid : 1; }
int cJSON_IsFalse(const cJSON * const item) { return item ? (item->type & 0xFF) == cJSON_False : 0; }
int cJSON_IsTrue(const cJSON * const item) { return item ? (item->type & 0xFF) == cJSON_True : 0; }
int cJSON_IsBool(const cJSON * const item) { return item ? (item->type & (cJSON_True | cJSON_False)) != 0 : 0; }
int cJSON_IsNull(const cJSON * const item) { return item ? (item->type & 0xFF) == cJSON_NULL : 0; }
int cJSON_IsNumber(const cJSON * const item) { return item ? (item->type & 0xFF) == cJSON_Number : 0; }
int cJSON_IsString(const cJSON * const item) { return item ? (item->type & 0xFF) == cJSON_String : 0; }
int cJSON_IsArray(const cJSON * const item) { return item ? (item->type & 0xFF) == cJSON_Array : 0; }
int cJSON_IsObject(const cJSON * const item) { return item ? (item->type & 0xFF) == cJSON_Object : 0; }
int cJSON_IsRaw(const cJSON * const item) { return item ? (item->type & 0xFF) == cJSON_Raw : 0; }

char *cJSON_GetStringValue(const cJSON * const item)
{
    if (!cJSON_IsString(item)) return NULL;
    return item->valuestring;
}

double cJSON_GetNumberValue(const cJSON * const item)
{
    if (!cJSON_IsNumber(item)) return 0.0;
    return item->valuedouble;
}
