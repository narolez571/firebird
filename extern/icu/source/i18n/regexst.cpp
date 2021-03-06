//
//  regexst.h
//
//  Copyright (C) 2004, International Business Machines Corporation and others.
//  All Rights Reserved.
//
//  This file contains class RegexStaticSets
//
//  This class is internal to the regular expression implementation.
//  For the public Regular Expression API, see the file "unicode/regex.h"
//
//  RegexStaticSets groups together the common UnicodeSets that are needed
//   for compiling or executing RegularExpressions.  This grouping simplifies
//   the thread safe lazy creation and sharing of these sets across
//   all instances of regular expressions.
//
#include "unicode/utypes.h"

#if !UCONFIG_NO_REGULAR_EXPRESSIONS

#include "unicode/unistr.h"
#include "unicode/uniset.h"
#include "unicode/uchar.h"
#include "unicode/regex.h"
#include "uprops.h"
#include "cmemory.h"
#include "cstring.h"
#include "uassert.h"
#include "ucln_in.h"
#include "umutex.h"

#include "regexcst.h"   // Contains state table for the regex pattern parser.
                        //   generated by a Perl script.
#include "regexst.h"



U_NAMESPACE_BEGIN


//----------------------------------------------------------------------------------------
//
// Unicode Set pattern strings for all of the required constant sets.
//               Initialized with hex values for portability to EBCDIC based machines.
//                Really ugly, but there's no good way to avoid it.
//
//----------------------------------------------------------------------------------------

// "Rule Char" Characters are those with no special meaning, and therefore do not
//    need to be escaped to appear as literals in a regexp.  Expressed
//    as the inverse of those needing escaping --  [^\*\?\+\[\(\)\{\}\^\$\|\\\.]
static const UChar gRuleSet_rule_char_pattern[]       = {
 //   [    ^      \     *     \     ?     \     +     \     [     \     (     /     )
    0x5b, 0x5e, 0x5c, 0x2a, 0x5c, 0x3f, 0x5c, 0x2b, 0x5c, 0x5b, 0x5c, 0x28, 0x5c, 0x29,
 //   \     {    \     }     \     ^     \     $     \     |     \     \     \     .     ]
    0x5c, 0x7b,0x5c, 0x7d, 0x5c, 0x5e, 0x5c, 0x24, 0x5c, 0x7c, 0x5c, 0x5c, 0x5c, 0x2e, 0x5d, 0};


static const UChar gRuleSet_digit_char_pattern[] = {
//    [    0      -    9     ]
    0x5b, 0x30, 0x2d, 0x39, 0x5d, 0};
//static const UnicodeSet *gRuleDigits = NULL;



//
//   Here are the backslash escape characters that ICU's unescape() function
//    will handle.
//
static const UChar gUnescapeCharPattern[] = {
//    [     a     c     e     f     n     r     t     u     U     x    ]
    0x5b, 0x61, 0x63, 0x65, 0x66, 0x6e, 0x72, 0x74, 0x75, 0x55, 0x78, 0x5d, 0};


//
//  White space characters that may appear within a pattern in free-form mode
//
static const UChar gRuleWhiteSpacePattern[] = {
    /* "[[:Cf:][:WSpace:]]" */
    91, 91, 58, 67, 102, 58, 93, 91, 58, 87,
        83, 112, 97, 99, 101, 58, 93, 93, 0 };



//
//  Unicode Set Definitions for Regular Expression  \w
//
static const UChar gIsWordPattern[] = {
//    [     \     p     {     L     l     }     \     p     {     L     u     }
    0x5b, 0x5c, 0x70, 0x7b, 0x4c, 0x6c, 0x7d, 0x5c, 0x70, 0x7b, 0x4c, 0x75, 0x7d,
//          \     p     {     L     t     }     \     p     {     L     o     }
          0x5c, 0x70, 0x7b, 0x4c, 0x74, 0x7d, 0x5c, 0x70, 0x7b, 0x4c, 0x6f, 0x7d,
//          \     p     {     N     d     }     _     ]
          0x5c, 0x70, 0x7b, 0x4e, 0x64, 0x7d, 0x5f, 0x5d, 0};


//
//  Unicode Set Definitions for Regular Expression  \s
//
    static const UChar gIsSpacePattern[] = {
//    [     \     t     \     n     \     f     \     r     \     p     {     Z     }     ]
    0x5b, 0x5c, 0x74, 0x5c, 0x6e, 0x5c, 0x66, 0x5c, 0x72, 0x5c, 0x70, 0x7b, 0x5a, 0x7d, 0x5d,  0};


//
//  UnicodeSets used in implementation of Grapheme Cluster detection, \X
//
    static const UChar gGC_ControlPattern[] = {
//    [     [     :     Z     l     :     ]     [     :     Z     p     :     ]    
    0x5b, 0x5b, 0x3a, 0x5A, 0x6c, 0x3a, 0x5d, 0x5b, 0x3a, 0x5A, 0x70, 0x3a, 0x5d, 
//    [     :     C     c     :     ]     [     :     C     f     :     ]     -
    0x5b, 0x3a, 0x43, 0x63, 0x3a, 0x5d, 0x5b, 0x3a, 0x43, 0x66, 0x3a, 0x5d, 0x2d,
//    [     :     G     r     a     p     h     e     m     e     _
    0x5b, 0x3a, 0x47, 0x72, 0x61, 0x70, 0x68, 0x65, 0x6d, 0x65, 0x5f,
//	  E     x     t     e     n     d     :     ]     ]
	0x45, 0x78, 0x74, 0x65, 0x6e, 0x64, 0x3a, 0x5d, 0x5d, 0};

    static const UChar gGC_ExtendPattern[] = {
//    [     \     p     {     G     r     a     p     h     e     m     e     _
    0x5b, 0x5c, 0x70, 0x7b, 0x47, 0x72, 0x61, 0x70, 0x68, 0x65, 0x6d, 0x65, 0x5f,
//    E     x     t     e     n     d     }     ]
    0x45, 0x78, 0x74, 0x65, 0x6e, 0x64, 0x7d, 0x5d, 0};

    static const UChar gGC_LPattern[] = {
//    [     \     p     {     H     a     n     g     u     l     _     S     y     l    
    0x5b, 0x5c, 0x70, 0x7b, 0x48, 0x61, 0x6e, 0x67, 0x75, 0x6c, 0x5f, 0x53, 0x79, 0x6c,
//    l     a     b     l     e     _     T     y     p     e     =     L     }     ]
    0x6c, 0x61, 0x62, 0x6c, 0x65, 0x5f, 0x54, 0x79, 0x70, 0x65, 0x3d, 0x4c, 0x7d,  0x5d, 0}; 

    static const UChar gGC_VPattern[] = {
//    [     \     p     {     H     a     n     g     u     l     _     S     y     l    
    0x5b, 0x5c, 0x70, 0x7b, 0x48, 0x61, 0x6e, 0x67, 0x75, 0x6c, 0x5f, 0x53, 0x79, 0x6c,
//    l     a     b     l     e     _     T     y     p     e     =     V     }     ]
    0x6c, 0x61, 0x62, 0x6c, 0x65, 0x5f, 0x54, 0x79, 0x70, 0x65, 0x3d, 0x56, 0x7d,  0x5d, 0}; 

    static const UChar gGC_TPattern[] = {
//    [     \     p     {     H     a     n     g     u     l     _     S     y     l    
    0x5b, 0x5c, 0x70, 0x7b, 0x48, 0x61, 0x6e, 0x67, 0x75, 0x6c, 0x5f, 0x53, 0x79, 0x6c,
//    l     a     b     l     e     _     T     y     p     e     =     T     }    ]
    0x6c, 0x61, 0x62, 0x6c, 0x65, 0x5f, 0x54, 0x79, 0x70, 0x65, 0x3d, 0x54, 0x7d, 0x5d, 0}; 

    static const UChar gGC_LVPattern[] = {
//    [     \     p     {     H     a     n     g     u     l     _     S     y     l    
    0x5b, 0x5c, 0x70, 0x7b, 0x48, 0x61, 0x6e, 0x67, 0x75, 0x6c, 0x5f, 0x53, 0x79, 0x6c,
//    l     a     b     l     e     _     T     y     p     e     =     L     V     }     ]
    0x6c, 0x61, 0x62, 0x6c, 0x65, 0x5f, 0x54, 0x79, 0x70, 0x65, 0x3d, 0x4c, 0x56, 0x7d, 0x5d, 0}; 

    static const UChar gGC_LVTPattern[] = {
//    [     \     p     {     H     a     n     g     u     l     _     S     y     l    
    0x5b, 0x5c, 0x70, 0x7b, 0x48, 0x61, 0x6e, 0x67, 0x75, 0x6c, 0x5f, 0x53, 0x79, 0x6c,
//    l     a     b     l     e     _     T     y     p     e     =     L     V     T     }     ]
    0x6c, 0x61, 0x62, 0x6c, 0x65, 0x5f, 0x54, 0x79, 0x70, 0x65, 0x3d, 0x4c, 0x56, 0x54, 0x7d, 0x5d, 0}; 

RegexStaticSets *RegexStaticSets::gStaticSets = NULL;

RegexStaticSets::RegexStaticSets(UErrorCode *status) {
    // First zero out everything  
    int i;
    for (i=0; i<URX_LAST_SET; i++) {
        fPropSets[i] = NULL;
    }
    for (i=0; i<10; i++) {
        fRuleSets[i] = NULL;
    }
    fUnescapeCharSet = NULL;
    fRuleDigits      = NULL;
    fEmptyString     = NULL;

    // Then init the sets to their correct values.
    fPropSets[URX_ISWORD_SET]  = new UnicodeSet(gIsWordPattern,     *status);
    fPropSets[URX_ISSPACE_SET] = new UnicodeSet(gIsSpacePattern,    *status);    
    fPropSets[URX_GC_EXTEND]   = new UnicodeSet(gGC_ExtendPattern,  *status);
    fPropSets[URX_GC_CONTROL]  = new UnicodeSet(gGC_ControlPattern, *status);
    fPropSets[URX_GC_L]        = new UnicodeSet(gGC_LPattern,       *status);
    fPropSets[URX_GC_V]        = new UnicodeSet(gGC_VPattern,       *status);
    fPropSets[URX_GC_T]        = new UnicodeSet(gGC_TPattern,       *status);
    fPropSets[URX_GC_LV]       = new UnicodeSet(gGC_LVPattern,      *status);
    fPropSets[URX_GC_LVT]      = new UnicodeSet(gGC_LVTPattern,     *status);
    if (U_FAILURE(*status)) {
        // Bail out if we were unable to create the above sets.
        // The rest of the initialization needs them, so we cannot proceed.
        return;
    }
    
    
    //
    // The following sets  are dynamically constructed, because their
    //   intialization strings would be unreasonable.
    //
    
    
    //
    //  "Normal" is the set of characters that don't need special handling
    //            when finding grapheme cluster boundaries.
    //
    fPropSets[URX_GC_NORMAL] = new UnicodeSet;
    fPropSets[URX_GC_NORMAL]->complement();
    fPropSets[URX_GC_NORMAL]->remove(0xac00, 0xd7a4);
    fPropSets[URX_GC_NORMAL]->removeAll(*fPropSets[URX_GC_CONTROL]);
    fPropSets[URX_GC_NORMAL]->removeAll(*fPropSets[URX_GC_L]);
    fPropSets[URX_GC_NORMAL]->removeAll(*fPropSets[URX_GC_V]);
    fPropSets[URX_GC_NORMAL]->removeAll(*fPropSets[URX_GC_T]);
    
    // Initialize the 8-bit fast bit sets from the parallel full
    //   UnicodeSets.
    for (i=0; i<URX_LAST_SET; i++) {
        fPropSets8[i].init(fPropSets[i]);
    }

    // Sets used while parsing rules, but not referenced from the parse state table
    fRuleSets[kRuleSet_rule_char-128]   = new UnicodeSet(gRuleSet_rule_char_pattern,  *status);
    fRuleSets[kRuleSet_white_space-128] = new UnicodeSet(gRuleWhiteSpacePattern,      *status);
    fRuleSets[kRuleSet_digit_char-128]  = new UnicodeSet(gRuleSet_digit_char_pattern, *status);
    fRuleDigits                         = new UnicodeSet(gRuleSet_digit_char_pattern, *status);
    fUnescapeCharSet                    = new UnicodeSet(gUnescapeCharPattern,        *status);

    // Empty UnicodeString, for use by matchers with NULL input.
    fEmptyString = new UnicodeString;
};


RegexStaticSets::~RegexStaticSets() {
    int i;

    for (i=0; i<URX_LAST_SET; i++) {
        delete fPropSets[i];
        fPropSets[i] = NULL;
    }
    for (i=0; i<10; i++) {
        delete fRuleSets[i];
        fRuleSets[i] = NULL;
    }
    delete fUnescapeCharSet;
    fUnescapeCharSet = NULL;
    delete fRuleDigits;
    fRuleDigits = NULL;
    delete fEmptyString;
    fEmptyString = NULL;
};


void RegexStaticSets::initGlobals(UErrorCode *status) {
    umtx_lock(NULL);
    RegexStaticSets *p = gStaticSets;
    umtx_unlock(NULL);
    if (p == NULL) {
        p = new RegexStaticSets(status);
        if (U_FAILURE(*status)) {
            delete p;
            return;
        }
        umtx_lock(NULL);
        if (gStaticSets == NULL) {
            gStaticSets = p;
            p = NULL;
        }
        umtx_unlock(NULL);
        if (p) {
            delete p;
        }
        ucln_i18n_registerCleanup();
    }
}
    

U_NAMESPACE_END
#endif  // !UCONFIG_NO_REGULAR_EXPRESSIONS
