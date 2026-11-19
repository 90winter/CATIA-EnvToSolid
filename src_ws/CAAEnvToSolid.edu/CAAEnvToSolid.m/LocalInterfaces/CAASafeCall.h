#ifndef CAA_SAFE_CALL_H
#define CAA_SAFE_CALL_H

#include <stddef.h>
#include "CATDataType.h"

class CATISpecObject;
class CATMmrInterPartCopy;
class CATCloneManager;
class CATICGMCloseOperator;
class CATICGMOperator;
class CATIDatumFactory;
class CATIPrtProceduralView;
class CATBody;
class CATUnicodeString;

bool SafeSpecUpdate(CATISpecObject* iObj, char* oErrBuf, size_t iBufLen);

bool SafePrtInsertInProceduralView(
    CATIPrtProceduralView* iProcView,
    CATISpecObject* iDestination,
    char* oErrBuf,
    size_t iBufLen);

bool SafeInterPartCopyRun(
    CATMmrInterPartCopy* iCopy,
    CATUnicodeString* oMsg,
    HRESULT& oHr,
    char* oErrBuf,
    size_t iBufLen);

bool SafeCloneManagerRun(
    CATCloneManager* iCloneMgr,
    int& oRc,
    char* oErrBuf,
    size_t iBufLen);

bool SafeCloseOperatorRun(
    CATICGMCloseOperator* iCloseOp,
    int& oRc,
    char* oErrBuf,
    size_t iBufLen);

bool SafeCGMOperatorRun(
    CATICGMOperator* iOp,
    int& oRc,
    char* oErrBuf,
    size_t iBufLen);

bool SafeInstanciateDatum(
    CATIDatumFactory* iFactory,
    CATBody* iBody,
    CATISpecObject*& oDatum,
    HRESULT& oHr,
    char* oErrBuf,
    size_t iBufLen);

#endif
