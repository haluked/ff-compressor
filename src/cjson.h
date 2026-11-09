/* SPDX-License-Identifier: MIT */
/*
  cJSON - An ultra-lightweight JSON parser in C.
  Derived from Dave Gamble's cJSON (MIT License).
*/

#ifndef CJSON__H
#define CJSON__H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>

/* cJSON Types: */
#define cJSON_Invalid (0)
#define cJSON_False  (1 << 0)
#define cJSON_True   (1 << 1)
#define cJSON_NULL   (1 << 2)
#define cJSON_Number (1 << 3)
#define cJSON_String (1 << 4)
#define cJSON_Array  (1 << 5)
#define cJSON_Object (1 << 6)
#define cJSON_Raw    (1 << 7)

#define cJSON_IsReference 256
#define cJSON_StringIsConst 512

/* The cJSON structure: */
typedef struct cJSON
{
    struct cJSON *next;
    struct cJSON *prev;
    struct cJSON *child;
    int type;
    char *valuestring;
    int valueint;
    double valuedouble;
    char *string;
} cJSON;

/* Supply a block of JSON, and this returns a cJSON object you can interrogate. */
cJSON *cJSON_Parse(const char *value);
/* Delete a cJSON entity and all subentities. */
void cJSON_Delete(cJSON *item);

/* Returns the number of items in an array (or object). */
int cJSON_GetArraySize(const cJSON *array);
/* Retrieve item number "index" from array "array". Returns NULL if not found. */
cJSON *cJSON_GetArrayItem(const cJSON *array, int index);
/* Get item "string" from object. Case sensitive (uses exact strcmp). */
cJSON *cJSON_GetObjectItem(const cJSON * const object, const char * const string);
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string);
int cJSON_HasObjectItem(const cJSON *object, const char *string);

/* Checkers */
int cJSON_IsInvalid(const cJSON * const item);
int cJSON_IsFalse(const cJSON * const item);
int cJSON_IsTrue(const cJSON * const item);
int cJSON_IsBool(const cJSON * const item);
int cJSON_IsNull(const cJSON * const item);
int cJSON_IsNumber(const cJSON * const item);
int cJSON_IsString(const cJSON * const item);
int cJSON_IsArray(const cJSON * const item);
int cJSON_IsObject(const cJSON * const item);
int cJSON_IsRaw(const cJSON * const item);

char *cJSON_GetStringValue(const cJSON * const item);
double cJSON_GetNumberValue(const cJSON * const item);

#ifdef __cplusplus
}
#endif

#endif
