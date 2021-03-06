/*
*******************************************************************************
*
*   Copyright (C) 1999-2004, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  genprops.h
*   encoding:   US-ASCII
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 1999dec13
*   created by: Markus W. Scherer
*/

#ifndef __GENPROPS_H__
#define __GENPROPS_H__

#include "unicode/utypes.h"
#include "utrie.h"

/* file definitions */
#define DATA_NAME "uprops"
#define DATA_TYPE "icu"

/* special casing data */
typedef struct {
    uint32_t code;
    UBool isComplex;
    UChar lowerCase[32], upperCase[32], titleCase[32];
} SpecialCasing;

/* case folding data */
typedef struct {
    uint32_t code, simple;
    char status;
    UChar full[32];
} CaseFolding;

/* character properties */
typedef struct {
    uint32_t code, lowerCase, upperCase, titleCase, mirrorMapping;
    int32_t numericValue; /* see numericType */
    uint32_t denominator; /* 0: no value */
    uint8_t generalCategory, bidi, isMirrored, numericType;
    SpecialCasing *specialCasing;
    CaseFolding *caseFolding;
} Props;

/* global flags */
extern UBool beVerbose, haveCopyright;

/* name tables */
extern const char *const
bidiNames[];

extern const char *const
genCategoryNames[];

/* properties vectors in props2.c */
extern uint32_t *pv;

/* prototypes */
U_CFUNC void
writeUCDFilename(char *basename, const char *filename, const char *suffix);

U_CFUNC UBool
isToken(const char *token, const char *s);

U_CFUNC int32_t
getTokenIndex(const char *const tokens[], int32_t countTokens, const char *s);

extern void
setUnicodeVersion(const char *v);

extern void
initStore(void);

extern uint32_t
makeProps(Props *p);

extern void
addProps(uint32_t c, uint32_t props);

extern uint32_t
getProps(uint32_t c);

extern void
repeatProps(uint32_t first, uint32_t last, uint32_t props);

U_CFUNC uint32_t U_EXPORT2
getFoldedPropsValue(UNewTrie *trie, UChar32 start, int32_t offset);

extern void
addCaseSensitive(UChar32 first, UChar32 last);

extern void
generateData(const char *dataDir);

/* props2.c */
U_CFUNC void
initAdditionalProperties(void);

U_CFUNC void
generateAdditionalProperties(char *filename, const char *suffix, UErrorCode *pErrorCode);

U_CFUNC int32_t
writeAdditionalData(uint8_t *p, int32_t capacity, int32_t indexes[16]);

#endif

