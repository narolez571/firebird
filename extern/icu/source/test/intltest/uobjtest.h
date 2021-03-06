/********************************************************************
 * COPYRIGHT: 
 * Copyright (c) 2002-2003, International Business Machines Corporation and
 * others. All Rights Reserved.
 ********************************************************************/


#ifndef _UOBJECTTEST_
#define _UOBJECTTEST_

#include "intltest.h"

/** 
 * Test uobjtest.h
 **/
class UObjectTest : public IntlTest {
    // IntlTest override
    void runIndexedTest( int32_t index, UBool exec, const char* &name, char* par );
 private:
    // tests
    void testIDs();
    void testUMemory();
    
    //helper
    
    /**
     * @param obj The UObject to be tested
     * @param className The name of the class being tested 
     * @param factory String version of obj, for exanple   "new UFoo(1,3,4)". NULL if object is abstract.
     * @param staticID The result of class :: getStaticClassID
     * @return Returns obj, suitable for deletion
     */
    UObject *testClass(UObject *obj,
		       const char *className, const char *factory, 
		       UClassID staticID);
		   
		   
};
 
#endif
//eof
