#include "CAASafeCall.h"

#include <stdio.h>
#include <string.h>

#include "CATErrorMacros.h"
#include "CATError.h"
#include "CATUnicodeString.h"
#include "CATISpecObject.h"
#include "CATMmrInterPartCopy.h"
#include "CATCloneManager.h"
#include "CATICGMCloseOperator.h"
#include "CATICGMOperator.h"
#include "CATIDatumFactory.h"
#include "CATIPrtProceduralView.h"
#include "CATBody.h"

static void CopyErrToBuf(CATError* pErr, char* oErrBuf, size_t iBufLen)
{
    if (oErrBuf == NULL || iBufLen == 0)
    {
        return;
    }
    oErrBuf[0] = '\0';
    if (pErr == NULL)
    {
        _snprintf_s(oErrBuf, iBufLen, _TRUNCATE, "Unknown CATError");
        return;
    }

    CATUnicodeString nlsMsg = pErr->GetNLSMessage();
    size_t utf8Cap = (size_t)nlsMsg.GetLengthInChar() * 4 + 8;
    if (utf8Cap > 4096) utf8Cap = 4096;
    char tmp[4096];
    tmp[0] = '\0';
    size_t outLen = 0;
    if (nlsMsg.GetLengthInChar() > 0)
    {
        nlsMsg.ConvertToUTF8(tmp, &outLen);
        if (outLen < sizeof(tmp))
        {
            tmp[outLen] = '\0';
        }
        else
        {
            tmp[sizeof(tmp) - 1] = '\0';
        }
    }

    if (tmp[0] != '\0')
    {
        _snprintf_s(oErrBuf, iBufLen, _TRUNCATE, "%s", tmp);
    }
    else
    {
        const char* msgId = pErr->GetMsgId();
        const char* cat = pErr->GetMsgCatalog();
        _snprintf_s(
            oErrBuf,
            iBufLen,
            _TRUNCATE,
            "CATError[%s:%s]",
            cat ? cat : "",
            msgId ? msgId : "unknown");
    }
}

bool SafeSpecUpdate(CATISpecObject* iObj, char* oErrBuf, size_t iBufLen)
{
    if (oErrBuf && iBufLen > 0) oErrBuf[0] = '\0';
    if (iObj == NULL)
    {
        if (oErrBuf && iBufLen > 0) _snprintf_s(oErrBuf, iBufLen, _TRUNCATE, "Null CATISpecObject");
        return false;
    }

    bool ok = false;
    CATTry
    {
        iObj->Update();
        ok = true;
    }
    CATCatch(CATError, pErr)
    {
        CopyErrToBuf(pErr, oErrBuf, iBufLen);
        ::Flush(pErr);
        pErr = NULL;
        ok = false;
    }
    CATCatchOthers
    {
        if (oErrBuf && iBufLen > 0) _snprintf_s(oErrBuf, iBufLen, _TRUNCATE, "Unknown CAA exception in Update()");
        ok = false;
    }
    CATEndTry;
    return ok;
}

bool SafePrtInsertInProceduralView(
    CATIPrtProceduralView* iProcView,
    CATISpecObject* iDestination,
    char* oErrBuf,
    size_t iBufLen)
{
    if (oErrBuf && iBufLen > 0) oErrBuf[0] = '\0';
    if (iProcView == NULL)
    {
        if (oErrBuf && iBufLen > 0) _snprintf_s(oErrBuf, iBufLen, _TRUNCATE, "Null CATIPrtProceduralView");
        return false;
    }

    bool ok = false;
    CATTry
    {
        iProcView->InsertInProceduralView(CATISpecObject_var(iDestination));
        ok = true;
    }
    CATCatch(CATError, pErr)
    {
        CopyErrToBuf(pErr, oErrBuf, iBufLen);
        ::Flush(pErr);
        pErr = NULL;
        ok = false;
    }
    CATCatchOthers
    {
        if (oErrBuf && iBufLen > 0)
        {
            _snprintf_s(oErrBuf, iBufLen, _TRUNCATE, "Unknown CAA exception in InsertInProceduralView()");
        }
        ok = false;
    }
    CATEndTry;
    return ok;
}

bool SafeInterPartCopyRun(
    CATMmrInterPartCopy* iCopy,
    CATUnicodeString* oMsg,
    HRESULT& oHr,
    char* oErrBuf,
    size_t iBufLen)
{
    if (oErrBuf && iBufLen > 0) oErrBuf[0] = '\0';
    oHr = E_FAIL;
    if (iCopy == NULL)
    {
        if (oErrBuf && iBufLen > 0) _snprintf_s(oErrBuf, iBufLen, _TRUNCATE, "Null CATMmrInterPartCopy");
        return false;
    }

    bool ok = false;
    CATTry
    {
        oHr = iCopy->Run(oMsg);
        ok = true;
    }
    CATCatch(CATError, pErr)
    {
        CopyErrToBuf(pErr, oErrBuf, iBufLen);
        ::Flush(pErr);
        pErr = NULL;
        oHr = E_FAIL;
        ok = false;
    }
    CATCatchOthers
    {
        if (oErrBuf && iBufLen > 0) _snprintf_s(oErrBuf, iBufLen, _TRUNCATE, "Unknown CAA exception in CATMmrInterPartCopy::Run");
        oHr = E_FAIL;
        ok = false;
    }
    CATEndTry;
    return ok;
}

bool SafeCloneManagerRun(
    CATCloneManager* iCloneMgr,
    int& oRc,
    char* oErrBuf,
    size_t iBufLen)
{
    if (oErrBuf && iBufLen > 0) oErrBuf[0] = '\0';
    oRc = 1;
    if (iCloneMgr == NULL)
    {
        if (oErrBuf && iBufLen > 0) _snprintf_s(oErrBuf, iBufLen, _TRUNCATE, "Null CATCloneManager");
        return false;
    }

    bool ok = false;
    CATTry
    {
        oRc = iCloneMgr->Run();
        ok = true;
    }
    CATCatch(CATError, pErr)
    {
        CopyErrToBuf(pErr, oErrBuf, iBufLen);
        ::Flush(pErr);
        pErr = NULL;
        oRc = 1;
        ok = false;
    }
    CATCatchOthers
    {
        if (oErrBuf && iBufLen > 0) _snprintf_s(oErrBuf, iBufLen, _TRUNCATE, "Unknown CAA exception in CATCloneManager::Run");
        oRc = 1;
        ok = false;
    }
    CATEndTry;
    return ok;
}

bool SafeCloseOperatorRun(
    CATICGMCloseOperator* iCloseOp,
    int& oRc,
    char* oErrBuf,
    size_t iBufLen)
{
    if (oErrBuf && iBufLen > 0) oErrBuf[0] = '\0';
    oRc = 1;
    if (iCloseOp == NULL)
    {
        if (oErrBuf && iBufLen > 0) _snprintf_s(oErrBuf, iBufLen, _TRUNCATE, "Null CATICGMCloseOperator");
        return false;
    }

    bool ok = false;
    CATTry
    {
        oRc = iCloseOp->Run();
        ok = true;
    }
    CATCatch(CATError, pErr)
    {
        CopyErrToBuf(pErr, oErrBuf, iBufLen);
        ::Flush(pErr);
        pErr = NULL;
        oRc = 1;
        ok = false;
    }
    CATCatchOthers
    {
        if (oErrBuf && iBufLen > 0) _snprintf_s(oErrBuf, iBufLen, _TRUNCATE, "Unknown CAA exception in CATICGMCloseOperator::Run");
        oRc = 1;
        ok = false;
    }
    CATEndTry;
    return ok;
}

bool SafeCGMOperatorRun(
    CATICGMOperator* iOp,
    int& oRc,
    char* oErrBuf,
    size_t iBufLen)
{
    if (oErrBuf && iBufLen > 0) oErrBuf[0] = '\0';
    oRc = 1;
    if (iOp == NULL)
    {
        if (oErrBuf && iBufLen > 0) _snprintf_s(oErrBuf, iBufLen, _TRUNCATE, "Null CATICGMOperator");
        return false;
    }

    bool ok = false;
    CATTry
    {
        oRc = iOp->Run();
        ok = true;
    }
    CATCatch(CATError, pErr)
    {
        CopyErrToBuf(pErr, oErrBuf, iBufLen);
        ::Flush(pErr);
        pErr = NULL;
        oRc = 1;
        ok = false;
    }
    CATCatchOthers
    {
        if (oErrBuf && iBufLen > 0) _snprintf_s(oErrBuf, iBufLen, _TRUNCATE, "Unknown CAA exception in CATICGMOperator::Run");
        oRc = 1;
        ok = false;
    }
    CATEndTry;
    return ok;
}

bool SafeInstanciateDatum(
    CATIDatumFactory* iFactory,
    CATBody* iBody,
    CATISpecObject*& oDatum,
    HRESULT& oHr,
    char* oErrBuf,
    size_t iBufLen)
{
    if (oErrBuf && iBufLen > 0) oErrBuf[0] = '\0';
    oDatum = NULL;
    oHr = E_FAIL;
    if (iFactory == NULL || iBody == NULL)
    {
        if (oErrBuf && iBufLen > 0) _snprintf_s(oErrBuf, iBufLen, _TRUNCATE, "Null factory or body in InstanciateDatum");
        return false;
    }

    bool ok = false;
    CATTry
    {
        oHr = iFactory->InstanciateDatum(iBody, oDatum);
        ok = true;
    }
    CATCatch(CATError, pErr)
    {
        CopyErrToBuf(pErr, oErrBuf, iBufLen);
        ::Flush(pErr);
        pErr = NULL;
        oHr = E_FAIL;
        ok = false;
    }
    CATCatchOthers
    {
        if (oErrBuf && iBufLen > 0) _snprintf_s(oErrBuf, iBufLen, _TRUNCATE, "Unknown CAA exception in InstanciateDatum");
        oHr = E_FAIL;
        ok = false;
    }
    CATEndTry;
    return ok;
}
