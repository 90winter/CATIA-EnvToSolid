#include "PartConverter.h"

#include <math.h>
#include <stdio.h>
#include <windows.h>
#include <set>
#include <utility>
#include <vector>

// System & ObjectModelerBase
#include "CATBaseUnknown.h"
#include "CATBoolean.h"
#include "CATUnicodeString.h"
#include "CATLISTV_CATBaseUnknown.h"
#include "CATDocument.h"
#include "CATDocumentServices.h"
#include "CATSessionServices.h"
#include "CATInit.h"
#include "CATIAlias.h"

// ObjectSpecsLegacy / ObjectSpecsModeler
#include "CATISpecObject.h"
#include "CATLISTV_CATISpecObject.h"
#include "CATIDescendants.h"
#include "LifeCycleObject.h"

// MecModInterfaces & MechanicalModeler
#include "CATIContainerOfDocument.h"
#include "CATIPrtContainer.h"
#include "CATIPrtPart.h"
#include "CATIPartRequest.h"
#include "CATIBodyRequest.h"
#include "CATIDatumFactory.h"
#include "CATIMechanicalRootFactory.h"
#include "CATIPrtProceduralView.h"
#include "CATIGSMTool.h"
#include "CATMmrLinearBodyServices.h"
#include "CATIMf3DBehavior.h"
#include "CATIMf3DBehavior2.h"
#include "CATIMechanicalProperties.h"
#include "CATIGeometricalElement.h"
#include "CATIShapeFeatureBody.h"
#include "CATIBRepAccess.h"
#include "CATIFeaturize.h"
#include "CATMfBRepDecode.h"
#include "CATMmrInterPartCopy.h"
#include "CATMmrGraphicPropertiesModes.h"

// PartInterfaces & SketcherInterfaces & GSMInterfaces
#include "CATIPrtFactory.h"
#include "CATIPad.h"
#include "CATLimitDefs.h"
#include "CATISketchFactory.h"
#include "CATISketch.h"
#include "CATI2DWFFactory.h"
#include "CATI2DPoint.h"
#include "CATI2DLine.h"
#include "CATI2DCurve.h"
#include "CATPrtEnum.h"
#include "CATIGSMFactory.h"
#include "CATIGSMExtrude.h"
#include "CATIGSMDirection.h"
#include "CATIGSMProceduralView.h"
#include "CATGSMContextDef.h"

// Mathematics & GeometricObjects & GMModelInterfaces & GMOperatorsInterfaces & TopologicalOperators
#include "CATMathAxis.h"
#include "CATMathBox.h"
#include "CATMathDirection.h"
#include "CATMathPoint.h"
#include "CATSoftwareConfiguration.h"
#include "CATGeoFactory.h"
#include "CATICGMContainer.h"
#include "CATCloneManager.h"
#include "CATCGMJournalList.h"
#include "CATTopData.h"
#include "CATBody.h"
#include "CATDomain.h"
#include "CATShell.h"
#include "CATCell.h"
#include "CATFace.h"
#include "CATSphere.h"
#include "CATSurLimits.h"
#include "CATListOfCATCells.h"
#include "CATListOfCATGeometries.h"
#include "CATICGMCloseOperator.h"
#include "CATICGMSolidCuboid.h"
#include "CATICGMTopSkin.h"
#include "CATDynMassProperties3D.h"

// Visualization
#include "CATIVisProperties.h"
#include "CATVisPropertiesValues.h"
#include "CATVisPropertyType.h"
#include "CATVisGeomType.h"
#include "CATShowAttribut.h"

#include "CAASafeCall.h"
#include "UnicodeUtil.h"
#include "VisPropHelper.h"

namespace
{

enum SourceKind
{
    KIND_SOLID_BODY = 0,
    KIND_SOLID_FEATURE = 1,
    KIND_VOLUME = 2,
    KIND_CLOSED_SURFACE = 3
};

struct SourceItem
{
    SourceKind kind;
    CATISpecObject_var ownerSet;
    CATISpecObject_var sourceFeature;
    CATIGeometricalElement_var geomElem;
    CATISpecObject_var styleOwner;
    CATBody_var topoBody;
    CATUnicodeString alias;
    bool isMainBody;
    bool hasExtraSurfacesInBody;

    SourceItem()
        : kind(KIND_SOLID_BODY), isMainBody(false), hasExtraSurfacesInBody(false)
    {}
};

static const char* KindToString(SourceKind k)
{
    switch (k)
    {
    case KIND_SOLID_BODY:     return "SolidBody";
    case KIND_SOLID_FEATURE:  return "SolidFeature";
    case KIND_VOLUME:         return "Volume";
    case KIND_CLOSED_SURFACE: return "ClosedSurface";
    default:                  return "Unknown";
    }
}

static CATUnicodeString GetObjectAliasU(const CATBaseUnknown_var& iObj)
{
    if (iObj == NULL_var) return CATUnicodeString("Unnamed");
    CATIAlias_var spAlias(iObj);
    if (spAlias != NULL_var)
    {
        CATUnicodeString u = spAlias->GetAlias();
        if (u.GetLengthInChar() > 0) return u;
    }
    return CATUnicodeString("Unnamed");
}

static std::string GetObjectAliasUtf8(const CATBaseUnknown_var& iObj)
{
    return UnicodeUtil::CATUnicodeToUtf8(GetObjectAliasU(iObj));
}

static void SetObjectAliasU(const CATBaseUnknown_var& iObj, const CATUnicodeString& iName)
{
    if (iObj == NULL_var || iName.GetLengthInChar() == 0) return;
    CATIAlias_var spAlias(iObj);
    if (spAlias != NULL_var)
    {
        spAlias->SetAlias(iName);
    }
}

static bool IsClosedSurfaceBody(CATBody* iBody)
{
    if (iBody == NULL) return false;
    int nV = 0, nE = 0, nF = 0, nVol = 0;
    iBody->GetCellNumbers(&nV, &nE, &nF, &nVol);
    if (nVol != 0 || nF <= 0)
    {
        return false;
    }

    CATLONG32 nbDom = iBody->GetNbDomains();
    if (nbDom <= 0)
    {
        return false;
    }

    int closedShells = 0;
    for (CATLONG32 i = 1; i <= nbDom; ++i)
    {
        CATDomain* pDom = iBody->GetDomain(i);
        if (pDom == NULL) return false;
        if (pDom->GetLowDimension() != 2)
        {
            return false;
        }
        CATShell* pShell = NULL;
        if (FAILED(pDom->QueryInterface(IID_CATShell, (void**)&pShell)) || pShell == NULL)
        {
            return false;
        }
        CATBoolean closed = pShell->IsClosed();
        pShell->Release();
        pShell = NULL;
        if (!closed)
        {
            return false;
        }
        closedShells++;
    }
    return closedShells > 0;
}

static bool GetPartHandles(
    CATDocument* iDoc,
    CATIPrtContainer*& oPrtCont,
    CATIPrtPart_var& oPrtPart,
    CATISpecObject_var& oPartSpec,
    CATIDatumFactory_var& oDatumFactory,
    CATIMechanicalRootFactory_var& oMechRootFactory,
    CATGeoFactory_var& oGeoFactory,
    std::string& oErr)
{
    oPrtCont = NULL;
    oPrtPart = NULL_var;
    oPartSpec = NULL_var;
    oDatumFactory = NULL_var;
    oMechRootFactory = NULL_var;
    oGeoFactory = NULL_var;

    if (iDoc == NULL)
    {
        oErr = "Null CATDocument";
        return false;
    }

    CATIContainerOfDocument_var spDocCont(iDoc);
    if (spDocCont == NULL_var)
    {
        oErr = "Document does not implement CATIContainerOfDocument";
        return false;
    }

    CATIContainer* piSpecCont = NULL;
    HRESULT hr = spDocCont->GetSpecContainer(piSpecCont);
    if (FAILED(hr) || piSpecCont == NULL)
    {
        oErr = "GetSpecContainer failed";
        return false;
    }

    hr = piSpecCont->QueryInterface(IID_CATIPrtContainer, (void**)&oPrtCont);
    if (FAILED(hr) || oPrtCont == NULL)
    {
        piSpecCont->Release();
        oErr = "Spec container does not implement CATIPrtContainer";
        return false;
    }

    oDatumFactory = piSpecCont;
    oMechRootFactory = piSpecCont;
    piSpecCont->Release();
    piSpecCont = NULL;

    oPartSpec = oPrtCont->GetPart();
    oPrtPart = oPartSpec;
    if (oPartSpec == NULL_var || oPrtPart == NULL_var)
    {
        oPrtCont->Release();
        oPrtCont = NULL;
        oErr = "Failed to get MechanicalPart from CATIPrtContainer";
        return false;
    }

    CATIContainer* piResultCont = NULL;
    if (SUCCEEDED(spDocCont->GetResultContainer(piResultCont)) && piResultCont != NULL)
    {
        oGeoFactory = piResultCont;
        piResultCont->Release();
        piResultCont = NULL;
    }
    if (oGeoFactory == NULL_var)
    {
        CATIContainer_var spGeomCont = oPrtCont->GetGeometricContainer();
        oGeoFactory = spGeomCont;
    }

    return true;
}

static bool ResolveFeatureBodyAndGeomElem(
    const CATBaseUnknown_var& iFeat,
    CATIGeometricalElement_var& oGeomElem,
    CATBody_var& oBody)
{
    oGeomElem = NULL_var;
    oBody = NULL_var;
    if (iFeat == NULL_var) return false;

    CATIShapeFeatureBody_var spShape(iFeat);
    if (spShape != NULL_var)
    {
        CATISpecObject_var spOut = spShape->GetResultOUT();
        if (spOut != NULL_var)
        {
            CATIGeometricalElement_var spOutGeom(spOut);
            if (spOutGeom != NULL_var)
            {
                CATBody_var spOutBody = spOutGeom->GetBodyResult();
                if (spOutBody != NULL_var)
                {
                    oGeomElem = spOutGeom;
                    oBody = spOutBody;
                    return true;
                }
            }
        }
    }

    CATIGeometricalElement_var spGeom(iFeat);
    if (spGeom != NULL_var)
    {
        CATBody_var spB = spGeom->GetBodyResult();
        if (spB != NULL_var)
        {
            oGeomElem = spGeom;
            oBody = spB;
            return true;
        }
    }
    return false;
}

static bool IsFeatureActive(const CATBaseUnknown_var& iFeat)
{
    if (iFeat == NULL_var) return false;
    CATIMechanicalProperties_var spProps(iFeat);
    if (spProps != NULL_var && spProps->IsInactive() == 1)
    {
        return false;
    }
    return true;
}

static void CollectConvertibleItems(
    const CATISpecObject_var& iPartSpec,
    std::vector<SourceItem>& oItems,
    int& oDiscardedCount,
    int& oSetsInspected)
{
    oItems.clear();
    oDiscardedCount = 0;
    oSetsInspected = 0;

    CATIPartRequest_var spPartReq(iPartSpec);
    if (spPartReq == NULL_var) return;

    CATBaseUnknown_var spMainBody;
    spPartReq->GetMainBody("MfDefault3DView", spMainBody);

    std::set<CATBaseUnknown*> visited;

    // Pass 1: Root Solid Bodies (Main PartBody first)
    CATLISTV(CATBaseUnknown_var) solidBodies;
    if (SUCCEEDED(spPartReq->GetSolidBodies("MfDefault3DView", solidBodies)))
    {
        std::vector<CATBaseUnknown_var> orderedBodies;
        if (spMainBody != NULL_var)
        {
            for (int i = 1; i <= solidBodies.Size(); ++i)
            {
                if (solidBodies[i] == spMainBody)
                {
                    orderedBodies.push_back(solidBodies[i]);
                    break;
                }
            }
        }
        for (int i = 1; i <= solidBodies.Size(); ++i)
        {
            if (solidBodies[i] != NULL_var && solidBodies[i] != spMainBody)
            {
                orderedBodies.push_back(solidBodies[i]);
            }
        }

        for (size_t bIdx = 0; bIdx < orderedBodies.size(); ++bIdx)
        {
            CATBaseUnknown_var spBodyUnk = orderedBodies[bIdx];
            CATISpecObject_var spBodySpec(spBodyUnk);
            if (spBodySpec == NULL_var) continue;
            oSetsInspected++;

            if (!IsHierarchyVisible(spBodySpec, iPartSpec))
            {
                oDiscardedCount++;
                UnicodeUtil::LogInfo("Skip hidden Body: " + GetObjectAliasUtf8(spBodySpec));
                continue;
            }

            CATIBodyRequest_var spBodyReq(spBodyUnk);
            if (spBodyReq == NULL_var) continue;

            CATLISTV(CATBaseUnknown_var) results;
            if (FAILED(spBodyReq->GetResults("MfDefault3DView", results))) continue;

            CATISpecObject_var spSolidResFeat;
            CATIGeometricalElement_var spSolidGeom;
            CATBody_var spSolidTopo;
            int surfacicCount = 0;
            std::vector<CATISpecObject_var> extraSurfacicFeats;

            for (int r = 1; r <= results.Size(); ++r)
            {
                CATISpecObject_var spFeat(results[r]);
                if (spFeat == NULL_var) continue;

                CATIMf3DBehavior_var spBeh(spFeat);
                bool isSolid = (spBeh != NULL_var && spBeh->IsASolid() == S_OK);
                bool isShape = (spBeh != NULL_var && spBeh->IsAShape() == S_OK);

                CATIGeometricalElement_var spGeom;
                CATBody_var spTopo;
                if (!ResolveFeatureBodyAndGeomElem(spFeat, spGeom, spTopo))
                {
                    if (!isSolid) oDiscardedCount++;
                    continue;
                }

                int nV = 0, nE = 0, nF = 0, nVol = 0;
                spTopo->GetCellNumbers(&nV, &nE, &nF, &nVol);

                if (isSolid && nVol > 0 && spSolidResFeat == NULL_var)
                {
                    spSolidResFeat = spFeat;
                    spSolidGeom = spGeom;
                    spSolidTopo = spTopo;
                }
                else if (isShape)
                {
                    surfacicCount++;
                    extraSurfacicFeats.push_back(spFeat);
                }
                else if (nVol > 0 && spSolidResFeat == NULL_var)
                {
                    spSolidResFeat = spFeat;
                    spSolidGeom = spGeom;
                    spSolidTopo = spTopo;
                }
            }

            if (spSolidResFeat != NULL_var && spSolidTopo != NULL_var)
            {
                SourceItem item;
                item.kind = KIND_SOLID_BODY;
                item.ownerSet = spBodySpec;
                item.sourceFeature = spSolidResFeat;
                item.geomElem = spSolidGeom;
                item.styleOwner = spBodySpec;
                item.topoBody = spSolidTopo;
                item.alias = GetObjectAliasU(spBodySpec);
                item.isMainBody = (spBodyUnk == spMainBody);
                item.hasExtraSurfacesInBody = (surfacicCount > 0);
                oItems.push_back(item);
                visited.insert((CATBaseUnknown*)spBodySpec);
                visited.insert((CATBaseUnknown*)spSolidResFeat);
            }

            // Also process any Volumes / Closed Surfaces placed directly in a HybridBody
            for (size_t e = 0; e < extraSurfacicFeats.size(); ++e)
            {
                CATISpecObject_var spFeat = extraSurfacicFeats[e];
                if (visited.count((CATBaseUnknown*)spFeat) > 0) continue;
                visited.insert((CATBaseUnknown*)spFeat);

                if (!IsFeatureActive(spFeat) || !IsHierarchyVisible(spFeat, iPartSpec))
                {
                    oDiscardedCount++;
                    continue;
                }

                CATIGeometricalElement_var spGeom;
                CATBody_var spTopo;
                if (!ResolveFeatureBodyAndGeomElem(spFeat, spGeom, spTopo))
                {
                    oDiscardedCount++;
                    continue;
                }

                int nV = 0, nE = 0, nF = 0, nVol = 0;
                spTopo->GetCellNumbers(&nV, &nE, &nF, &nVol);

                CATIMf3DBehavior2_var spBeh2(spFeat);
                bool isVol = ((spBeh2 != NULL_var && spBeh2->IsAVolume() == S_OK) || nVol > 0);
                bool isClosedSurf = (!isVol && IsClosedSurfaceBody((CATBody*)spTopo));

                if (isVol || isClosedSurf)
                {
                    SourceItem item;
                    item.kind = isVol ? KIND_VOLUME : KIND_CLOSED_SURFACE;
                    item.ownerSet = spBodySpec;
                    item.sourceFeature = spFeat;
                    item.geomElem = spGeom;
                    item.styleOwner = spFeat;
                    item.topoBody = spTopo;
                    item.alias = GetObjectAliasU(spFeat);
                    oItems.push_back(item);
                }
                else
                {
                    oDiscardedCount++;
                }
            }
        }
    }

    // Pass 2: Surfacic Feature Sets (Geometrical Sets & Ordered Geometrical Sets)
    CATLISTV(CATBaseUnknown_var) surfBodies;
    if (SUCCEEDED(spPartReq->GetSurfBodies("MfDefault3DView", surfBodies)))
    {
        for (int s = 1; s <= surfBodies.Size(); ++s)
        {
            CATBaseUnknown_var spSetUnk = surfBodies[s];
            CATISpecObject_var spSetSpec(spSetUnk);
            if (spSetSpec == NULL_var) continue;
            oSetsInspected++;

            if (!IsHierarchyVisible(spSetSpec, iPartSpec))
            {
                oDiscardedCount++;
                UnicodeUtil::LogInfo("Skip hidden Geometrical Set: " + GetObjectAliasUtf8(spSetSpec));
                continue;
            }

            CATIBodyRequest_var spSetReq(spSetUnk);
            if (spSetReq == NULL_var) continue;

            CATLISTV(CATBaseUnknown_var) results;
            if (FAILED(spSetReq->GetResults("MfDefault3DView", results))) continue;

            for (int r = 1; r <= results.Size(); ++r)
            {
                CATISpecObject_var spFeat(results[r]);
                if (spFeat == NULL_var) continue;
                if (visited.count((CATBaseUnknown*)spFeat) > 0) continue;
                visited.insert((CATBaseUnknown*)spFeat);

                if (!IsFeatureActive(spFeat))
                {
                    oDiscardedCount++;
                    UnicodeUtil::LogInfo("Skip inactive feature: " + GetObjectAliasUtf8(spFeat));
                    continue;
                }
                if (!IsHierarchyVisible(spFeat, iPartSpec))
                {
                    oDiscardedCount++;
                    UnicodeUtil::LogInfo("Skip hidden feature/sub-set child: " + GetObjectAliasUtf8(spFeat));
                    continue;
                }

                CATIGeometricalElement_var spGeom;
                CATBody_var spTopo;
                if (!ResolveFeatureBodyAndGeomElem(spFeat, spGeom, spTopo))
                {
                    oDiscardedCount++;
                    continue;
                }

                int nV = 0, nE = 0, nF = 0, nVol = 0;
                spTopo->GetCellNumbers(&nV, &nE, &nF, &nVol);

                CATIMf3DBehavior_var spBeh(spFeat);
                CATIMf3DBehavior2_var spBeh2(spFeat);
                bool isSolid = (spBeh != NULL_var && spBeh->IsASolid() == S_OK && nVol > 0);
                bool isVol = (!isSolid) && ((spBeh2 != NULL_var && spBeh2->IsAVolume() == S_OK) || nVol > 0);
                bool isClosedSurf = (!isSolid && !isVol && IsClosedSurfaceBody((CATBody*)spTopo));

                if (isSolid || isVol || isClosedSurf)
                {
                    SourceItem item;
                    item.kind = isSolid ? KIND_SOLID_FEATURE : (isVol ? KIND_VOLUME : KIND_CLOSED_SURFACE);
                    item.ownerSet = spSetSpec;
                    item.sourceFeature = spFeat;
                    item.geomElem = spGeom;
                    item.styleOwner = spFeat;
                    item.topoBody = spTopo;
                    item.alias = GetObjectAliasU(spFeat);
                    oItems.push_back(item);
                }
                else
                {
                    oDiscardedCount++;
                }
            }
        }
    }
}

static bool IsValidDatumSolidFeature(const CATISpecObject_var& iFeat)
{
    if (iFeat == NULL_var) return false;
    CATIMf3DBehavior_var spBeh(iFeat);
    CATIMf3DBehavior2_var spBeh2(iFeat);
    if (spBeh == NULL_var) return false;
    if (spBeh->IsASolid() != S_OK) return false;
    if (spBeh->IsADatum() != S_OK) return false;
    if (spBeh2 != NULL_var && spBeh2->IsAVolume() == S_OK) return false;
    return true;
}

static void RemoveSpecObjectFromParent(const CATISpecObject_var& iParent, const CATISpecObject_var& iChild)
{
    if (iChild == NULL_var) return;
    if (iParent != NULL_var)
    {
        CATIDescendants_var spDesc(iParent);
        if (spDesc != NULL_var)
        {
            spDesc->RemoveChild(iChild);
        }
    }
    LifeCycleObject_var spLife(iChild);
    if (spLife != NULL_var)
    {
        spLife->remove(0);
    }
}

static bool EnsureDatumSolidInBody(
    const CATISpecObject_var& iDatum,
    const CATISpecObject_var& iDstBody,
    CATISpecObject_var& oVerifiedResult,
    std::string& oErr)
{
    oVerifiedResult = NULL_var;
    if (iDatum == NULL_var || iDstBody == NULL_var)
    {
        oErr = "Null datum or destination body in EnsureDatumSolidInBody";
        return false;
    }

    CATISpecObject* pRawFather = iDatum->GetFather();
    CATISpecObject_var spFather = pRawFather;
    if (pRawFather != NULL)
    {
        pRawFather->Release();
        pRawFather = NULL;
    }

    char insertErrBuf[512];
    insertErrBuf[0] = '\0';
    if (spFather != iDstBody)
    {
        std::string fatherInfo = "NULL";
        if (spFather != NULL_var)
        {
            fatherInfo = GetObjectAliasUtf8(spFather) + " (type=" +
                UnicodeUtil::CATUnicodeToUtf8(spFather->GetType()) + ")";
        }
        UnicodeUtil::LogInfo("InstanciateDatum initial father for '" +
            GetObjectAliasUtf8(iDatum) + "' (type=" +
            UnicodeUtil::CATUnicodeToUtf8(iDatum->GetType()) + "): " +
            fatherInfo + "; relocating into target Body '" +
            GetObjectAliasUtf8(iDstBody) + "'");

        if (spFather != NULL_var)
        {
            CATIDescendants_var spOldDesc(spFather);
            if (spOldDesc != NULL_var)
            {
                spOldDesc->RemoveChild(iDatum);
            }
        }

        CATIPrtProceduralView_var spProcView(iDatum);
        if (spProcView != NULL_var)
        {
            SafePrtInsertInProceduralView(
                (CATIPrtProceduralView*)spProcView,
                (CATISpecObject*)iDstBody,
                insertErrBuf,
                sizeof(insertErrBuf));
        }

        pRawFather = iDatum->GetFather();
        spFather = pRawFather;
        if (pRawFather != NULL)
        {
            pRawFather->Release();
            pRawFather = NULL;
        }

        if (spFather != iDstBody)
        {
            CATIDescendants_var spBodyDesc(iDstBody);
            if (spBodyDesc != NULL_var)
            {
                spBodyDesc->Append(iDatum);
            }
        }
    }

    char updErrBuf[512];
    updErrBuf[0] = '\0';
    SafeSpecUpdate((CATISpecObject*)iDatum, updErrBuf, sizeof(updErrBuf));

    CATIGSMTool_var spGsmTool(iDstBody);
    if (spGsmTool != NULL_var)
    {
        int bodySetType = -1;
        if (SUCCEEDED(spGsmTool->GetType(bodySetType)) && bodySetType == 1)
        {
            CATMmrLinearBodyServices::Insert(CATBaseUnknown_var(iDatum));
        }
    }

    char bodyUpdErrBuf[512];
    bodyUpdErrBuf[0] = '\0';
    SafeSpecUpdate((CATISpecObject*)iDstBody, bodyUpdErrBuf, sizeof(bodyUpdErrBuf));

    CATIBodyRequest_var spBodyReq(iDstBody);
    CATLISTV(CATBaseUnknown_var) resList;
    if (spBodyReq != NULL_var && SUCCEEDED(spBodyReq->GetResults("MfDefault3DView", resList)))
    {
        for (int k = 1; k <= resList.Size(); ++k)
        {
            CATISpecObject_var spRes(resList[k]);
            if (!IsValidDatumSolidFeature(spRes)) continue;

            CATIGeometricalElement_var spG;
            CATBody_var spB;
            if (!ResolveFeatureBodyAndGeomElem(spRes, spG, spB) || spB == NULL_var) continue;

            int nV = 0, nE = 0, nF = 0, nVol = 0;
            ((CATBody*)spB)->GetCellNumbers(&nV, &nE, &nF, &nVol);
            if (nVol > 0)
            {
                oVerifiedResult = spRes;
                return true;
            }
        }
    }

    pRawFather = iDatum->GetFather();
    CATISpecObject_var spFinalFather = pRawFather;
    if (pRawFather != NULL)
    {
        pRawFather->Release();
        pRawFather = NULL;
    }

    char ebuf[1024];
    _snprintf_s(ebuf, sizeof(ebuf), _TRUNCATE,
        "EnsureDatumSolidInBody failed: datumType=%s finalFather=%s results=%d insertErr='%s' updErr='%s' bodyUpdErr='%s'",
        UnicodeUtil::CATUnicodeToUtf8(iDatum->GetType()).c_str(),
        (spFinalFather != NULL_var) ? GetObjectAliasUtf8(spFinalFather).c_str() : "NULL",
        resList.Size(),
        insertErrBuf,
        updErrBuf,
        bodyUpdErrBuf);
    oErr = ebuf;
    return false;
}

static bool Clone3DBodyToDatumSolid(
    const SourceItem& iItem,
    const CATISpecObject_var& iSrcPartSpec,
    const CATISpecObject_var& iDstBody,
    const CATIPrtPart_var& iDstPrtPart,
    const CATIDatumFactory_var& iDstDatumFactory,
    const CATGeoFactory_var& iDstGeoFactory,
    std::string& oErr)
{
    if (iItem.topoBody == NULL_var || iDstBody == NULL_var ||
        iDstPrtPart == NULL_var || iDstDatumFactory == NULL_var || iDstGeoFactory == NULL_var)
    {
        oErr = "Invalid handle in Clone3DBodyToDatumSolid";
        return false;
    }

    iDstPrtPart->SetCurrentFeature(iDstBody);

    CATSoftwareConfiguration* pConfig = ::CATCreateSoftwareConfiguration();
    if (pConfig == NULL)
    {
        oErr = "CATCreateSoftwareConfiguration failed";
        return false;
    }

    CATCloneManager cloneMgr((CATGeoFactory*)iDstGeoFactory, CatCGMFullDuplicate, pConfig);
    cloneMgr.Add((CATICGMObject*)(CATBody*)iItem.topoBody);

    int cloneRc = 1;
    char errBuf[512];
    if (!SafeCloneManagerRun(&cloneMgr, cloneRc, errBuf, sizeof(errBuf)) || cloneRc != 0)
    {
        pConfig->Release();
        oErr = (errBuf[0] != '\0') ? std::string(errBuf) : "CATCloneManager::Run failed";
        return false;
    }

    CATICGMObject* pClonedObj = cloneMgr.ReadImage((CATICGMObject*)(CATBody*)iItem.topoBody);
    CATBody* pClonedBody = NULL;
    if (pClonedObj != NULL)
    {
        pClonedObj->QueryInterface(IID_CATBody, (void**)&pClonedBody);
    }
    if (pClonedBody == NULL)
    {
        pConfig->Release();
        oErr = "CATCloneManager::ReadImage did not return CATBody";
        return false;
    }

    // Build initial face mapping from CATCloneManager
    std::vector<std::pair<CATCell*, CATCell*> > facePairs;
    CATLISTP(CATCell) srcFaces;
    ((CATBody*)iItem.topoBody)->GetAllCells(srcFaces, 2);
    for (int f = 1; f <= srcFaces.Size(); ++f)
    {
        CATCell* pSrcCell = srcFaces[f];
        if (pSrcCell == NULL) continue;
        CATICGMObject* pDstCellObj = cloneMgr.ReadImage((CATICGMObject*)pSrcCell);
        CATCell* pDstCell = NULL;
        if (pDstCellObj != NULL &&
            SUCCEEDED(pDstCellObj->QueryInterface(IID_CATCell, (void**)&pDstCell)) &&
            pDstCell != NULL)
        {
            facePairs.push_back(std::make_pair(pSrcCell, pDstCell));
            pDstCell->Release();
        }
    }

    CATISpecObject* pRawDatum = NULL;
    HRESULT hrDatum = E_FAIL;
    if (!SafeInstanciateDatum((CATIDatumFactory*)iDstDatumFactory, pClonedBody, pRawDatum, hrDatum, errBuf, sizeof(errBuf)) ||
        FAILED(hrDatum) || pRawDatum == NULL)
    {
        ((CATICGMContainer*)(CATGeoFactory*)iDstGeoFactory)->Remove((CATICGMObject*)pClonedBody, CATICGMContainer::RemoveDependancies);
        pClonedBody->Release();
        pConfig->Release();
        oErr = (errBuf[0] != '\0') ? std::string(errBuf) : "InstanciateDatum failed";
        return false;
    }

    CATISpecObject_var spDatum = pRawDatum;
    pRawDatum->Release();
    pRawDatum = NULL;

    CATISpecObject_var spVerifiedDatum;
    if (!EnsureDatumSolidInBody(spDatum, iDstBody, spVerifiedDatum, oErr))
    {
        CATISpecObject* pActFather = spDatum->GetFather();
        CATISpecObject_var spActFather = pActFather;
        if (pActFather != NULL)
        {
            pActFather->Release();
            pActFather = NULL;
        }
        RemoveSpecObjectFromParent((spActFather != NULL_var) ? spActFather : iDstBody, spDatum);
        pClonedBody->Release();
        pConfig->Release();
        return false;
    }

    // Preserve display style & face colors
    GraphicStyle style;
    ExtractEffectiveGraphicStyle(iItem.styleOwner, iSrcPartSpec, style);
    ApplyBodyAndDatumGraphicStyle(iDstBody, spVerifiedDatum, style);

    CATIGeometricalElement_var spDstGeom;
    CATBody_var spDstTopo;
    if (!ResolveFeatureBodyAndGeomElem(spVerifiedDatum, spDstGeom, spDstTopo))
    {
        spDstGeom = spVerifiedDatum;
        spDstTopo = pClonedBody;
    }
    if ((CATBody*)spDstTopo != pClonedBody)
    {
        facePairs.clear();
    }

    CATTopData topData(pConfig, NULL);
    TransferFaceGraphicProperties(
        &topData,
        iItem.geomElem,
        (CATBody*)iItem.topoBody,
        spDstGeom,
        (CATBody*)spDstTopo,
        facePairs,
        style);

    pClonedBody->Release();
    pConfig->Release();
    return true;
}

static bool ConvertClosedSurfaceDirect(
    const SourceItem& iItem,
    const CATISpecObject_var& iSrcPartSpec,
    const CATISpecObject_var& iDstBody,
    const CATIPrtPart_var& iDstPrtPart,
    const CATIDatumFactory_var& iDstDatumFactory,
    const CATGeoFactory_var& iDstGeoFactory,
    std::string& oErr)
{
    if (iItem.topoBody == NULL_var || iDstBody == NULL_var ||
        iDstPrtPart == NULL_var || iDstDatumFactory == NULL_var || iDstGeoFactory == NULL_var)
    {
        oErr = "Invalid handle in ConvertClosedSurfaceDirect";
        return false;
    }

    iDstPrtPart->SetCurrentFeature(iDstBody);

    CATSoftwareConfiguration* pConfig = ::CATCreateSoftwareConfiguration();
    if (pConfig == NULL)
    {
        oErr = "CATCreateSoftwareConfiguration failed";
        return false;
    }

    CATCloneManager cloneMgr((CATGeoFactory*)iDstGeoFactory, CatCGMFullDuplicate, pConfig);
    cloneMgr.Add((CATICGMObject*)(CATBody*)iItem.topoBody);

    int cloneRc = 1;
    char errBuf[512];
    if (!SafeCloneManagerRun(&cloneMgr, cloneRc, errBuf, sizeof(errBuf)) || cloneRc != 0)
    {
        pConfig->Release();
        oErr = (errBuf[0] != '\0') ? std::string(errBuf) : "Clone skin failed";
        return false;
    }

    CATICGMObject* pClonedObj = cloneMgr.ReadImage((CATICGMObject*)(CATBody*)iItem.topoBody);
    CATBody* pClonedSkin = NULL;
    if (pClonedObj != NULL)
    {
        pClonedObj->QueryInterface(IID_CATBody, (void**)&pClonedSkin);
    }
    if (pClonedSkin == NULL)
    {
        pConfig->Release();
        oErr = "Cloned skin body is NULL";
        return false;
    }

    // Track srcFace -> clonedSkinFace
    std::vector<std::pair<CATCell*, CATCell*> > srcToClonedFaces;
    CATLISTP(CATCell) srcFaces;
    ((CATBody*)iItem.topoBody)->GetAllCells(srcFaces, 2);
    for (int f = 1; f <= srcFaces.Size(); ++f)
    {
        CATCell* pSrcCell = srcFaces[f];
        if (pSrcCell == NULL) continue;
        CATICGMObject* pCloneFaceObj = cloneMgr.ReadImage((CATICGMObject*)pSrcCell);
        CATCell* pCloneCell = NULL;
        if (pCloneFaceObj != NULL &&
            SUCCEEDED(pCloneFaceObj->QueryInterface(IID_CATCell, (void**)&pCloneCell)) &&
            pCloneCell != NULL)
        {
            srcToClonedFaces.push_back(std::make_pair(pSrcCell, pCloneCell));
            pCloneCell->Release();
        }
    }

    CATCGMJournalList* pJournal = new CATCGMJournalList(pConfig, NULL);
    CATTopData topData(pConfig, pJournal);
    CATICGMCloseOperator* pCloseOp = ::CATCGMCreateCloseOperator((CATGeoFactory*)iDstGeoFactory, &topData, pClonedSkin);
    if (pCloseOp == NULL)
    {
        delete pJournal;
        ((CATICGMContainer*)(CATGeoFactory*)iDstGeoFactory)->Remove((CATICGMObject*)pClonedSkin, CATICGMContainer::RemoveDependancies);
        pClonedSkin->Release();
        pConfig->Release();
        oErr = "CATCGMCreateCloseOperator returned NULL";
        return false;
    }

    int closeRc = 1;
    bool runOk = SafeCloseOperatorRun(pCloseOp, closeRc, errBuf, sizeof(errBuf));
    CATBody* pClosedBody = (runOk && closeRc == 0) ? pCloseOp->GetResult() : NULL;
    int nV = 0, nE = 0, nF = 0, nVol = 0;
    if (pClosedBody != NULL)
    {
        pClosedBody->GetCellNumbers(&nV, &nE, &nF, &nVol);
    }

    if (!runOk || closeRc != 0 || pClosedBody == NULL || nVol <= 0)
    {
        pCloseOp->Release();
        delete pJournal;
        ((CATICGMContainer*)(CATGeoFactory*)iDstGeoFactory)->Remove((CATICGMObject*)pClonedSkin, CATICGMContainer::RemoveDependancies);
        pClonedSkin->Release();
        pConfig->Release();
        oErr = (errBuf[0] != '\0') ? std::string(errBuf) : "CATICGMCloseOperator did not produce a volume";
        return false;
    }

    // Map srcFace -> closedBodyFace via journal, then fallback geometric matching will complete any gaps
    std::vector<std::pair<CATCell*, CATCell*> > facePairs;
    CATLISTP(CATCell) closedFaces;
    pClosedBody->GetAllCells(closedFaces, 2);
    std::set<CATCell*> closedFaceSet;
    for (int cf = 1; cf <= closedFaces.Size(); ++cf)
    {
        if (closedFaces[cf] != NULL) closedFaceSet.insert(closedFaces[cf]);
    }

    for (size_t k = 0; k < srcToClonedFaces.size(); ++k)
    {
        CATCell* pSrcFace = srcToClonedFaces[k].first;
        CATCell* pSkinFace = srcToClonedFaces[k].second;
        if (pSrcFace == NULL || pSkinFace == NULL) continue;

        if (closedFaceSet.count(pSkinFace) > 0)
        {
            facePairs.push_back(std::make_pair(pSrcFace, pSkinFace));
            continue;
        }

        CATGeometry* pGeomSkinFace = NULL;
        if (SUCCEEDED(pSkinFace->QueryInterface(IID_CATGeometry, (void**)&pGeomSkinFace)) && pGeomSkinFace != NULL)
        {
            CATLISTP(CATGeometry) lastObjs;
            pJournal->FindLasts(pGeomSkinFace, lastObjs, ThroughCreateAndModify);
            for (int lo = 1; lo <= lastObjs.Size(); ++lo)
            {
                CATCell* pOutCell = NULL;
                if (lastObjs[lo] != NULL &&
                    SUCCEEDED(lastObjs[lo]->QueryInterface(IID_CATCell, (void**)&pOutCell)) &&
                    pOutCell != NULL)
                {
                    if (closedFaceSet.count(pOutCell) > 0)
                    {
                        facePairs.push_back(std::make_pair(pSrcFace, pOutCell));
                        pOutCell->Release();
                        break;
                    }
                    pOutCell->Release();
                }
            }
            pGeomSkinFace->Release();
        }
    }

    pCloseOp->Release();
    pCloseOp = NULL;
    delete pJournal;
    pJournal = NULL;

    ((CATICGMContainer*)(CATGeoFactory*)iDstGeoFactory)->Remove((CATICGMObject*)pClonedSkin, CATICGMContainer::RemoveDependancies);
    pClonedSkin->Release();
    pClonedSkin = NULL;

    CATISpecObject* pRawDatum = NULL;
    HRESULT hrDatum = E_FAIL;
    if (!SafeInstanciateDatum((CATIDatumFactory*)iDstDatumFactory, pClosedBody, pRawDatum, hrDatum, errBuf, sizeof(errBuf)) ||
        FAILED(hrDatum) || pRawDatum == NULL)
    {
        ((CATICGMContainer*)(CATGeoFactory*)iDstGeoFactory)->Remove((CATICGMObject*)pClosedBody, CATICGMContainer::RemoveDependancies);
        pConfig->Release();
        oErr = (errBuf[0] != '\0') ? std::string(errBuf) : "InstanciateDatum failed on closed body";
        return false;
    }

    CATISpecObject_var spDatum = pRawDatum;
    pRawDatum->Release();
    pRawDatum = NULL;

    CATISpecObject_var spVerifiedDatum;
    if (!EnsureDatumSolidInBody(spDatum, iDstBody, spVerifiedDatum, oErr))
    {
        CATISpecObject* pActFather = spDatum->GetFather();
        CATISpecObject_var spActFather = pActFather;
        if (pActFather != NULL)
        {
            pActFather->Release();
            pActFather = NULL;
        }
        RemoveSpecObjectFromParent((spActFather != NULL_var) ? spActFather : iDstBody, spDatum);
        pConfig->Release();
        return false;
    }

    GraphicStyle style;
    ExtractEffectiveGraphicStyle(iItem.styleOwner, iSrcPartSpec, style);
    ApplyBodyAndDatumGraphicStyle(iDstBody, spVerifiedDatum, style);

    CATIGeometricalElement_var spDstGeom;
    CATBody_var spDstTopo;
    if (!ResolveFeatureBodyAndGeomElem(spVerifiedDatum, spDstGeom, spDstTopo))
    {
        spDstGeom = spVerifiedDatum;
        spDstTopo = pClosedBody;
    }
    if ((CATBody*)spDstTopo != pClosedBody)
    {
        facePairs.clear();
    }

    CATTopData matchTopData(pConfig, NULL);
    TransferFaceGraphicProperties(
        &matchTopData,
        iItem.geomElem,
        (CATBody*)iItem.topoBody,
        spDstGeom,
        (CATBody*)spDstTopo,
        facePairs,
        style);

    pConfig->Release();
    return true;
}

static bool TryInterPartCopySolidBody(
    const SourceItem& iItem,
    const CATISpecObject_var& iSrcPartSpec,
    const CATISpecObject_var& iDstPartSpec,
    CATISpecObject_var& oCreatedBody,
    std::string& oErr);

static bool ConvertViaScratchCloseSurface(
    const SourceItem& iItem,
    const CATISpecObject_var& iSrcPartSpec,
    const CATISpecObject_var& iDstBody,
    const CATIPrtPart_var& iDstPrtPart,
    const CATIDatumFactory_var& iDstDatumFactory,
    const CATGeoFactory_var& iDstGeoFactory,
    const CATISpecObject_var& iDstPartSpec,
    bool iReusedMain,
    bool& ioMainBodyUsed,
    std::string& oErr)
{
    CATDocument* pScratchDoc = NULL;
    HRESULT hr = CATDocumentServices::New("CATPart", pScratchDoc);
    if (FAILED(hr) || pScratchDoc == NULL)
    {
        oErr = "Scratch CATPart New() failed";
        return false;
    }

    CATInit_var spInit(pScratchDoc);
    if (spInit != NULL_var) spInit->Init(TRUE);

    bool ok = false;
    {
        CATIPrtContainer* pScratchPrtCont = NULL;
        CATIPrtPart_var spScratchPart;
        CATISpecObject_var spScratchPartSpec;
        CATIDatumFactory_var spScratchDatumFactory;
        CATIMechanicalRootFactory_var spScratchMechRoot;
        CATGeoFactory_var spScratchGeo;

        if (GetPartHandles(
                pScratchDoc,
                pScratchPrtCont,
                spScratchPart,
                spScratchPartSpec,
                spScratchDatumFactory,
                spScratchMechRoot,
                spScratchGeo,
                oErr))
        {
            CATMmrInterPartCopy copier(iItem.sourceFeature, spScratchPartSpec);
            copier.SetLinkMode(FALSE);
            copier.SetColorMode(CATMmrGraphicPropertiesModes::ALL_GP);

            CATUnicodeString copyErr;
            HRESULT hrCopy = E_FAIL;
            char errBuf[512];
            if (SafeInterPartCopyRun(&copier, &copyErr, hrCopy, errBuf, sizeof(errBuf)) && SUCCEEDED(hrCopy))
            {
                CATISpecObject_var spCopiedSurf;
                if (SUCCEEDED(copier.GetResult(spCopiedSurf)) && spCopiedSurf != NULL_var)
                {
                    CATISpecObject_var spScratchBody = spScratchMechRoot->CreatePRTTool("ScratchBody", spScratchPartSpec);
                    if (spScratchBody != NULL_var)
                    {
                        spScratchPart->SetCurrentFeature(spScratchBody);
                        CATIPrtFactory_var spPrtFactory(pScratchPrtCont);
                        if (spPrtFactory != NULL_var)
                        {
                            CATISpecObject_var spCloseFeat = spPrtFactory->CreateCloseSurface(spCopiedSurf);
                            if (spCloseFeat != NULL_var &&
                                SafeSpecUpdate((CATISpecObject*)spCloseFeat, errBuf, sizeof(errBuf)) &&
                                SafeSpecUpdate((CATISpecObject*)spScratchBody, errBuf, sizeof(errBuf)))
                            {
                                CATIGeometricalElement_var spClosedGeom;
                                CATBody_var spClosedTopo;
                                if (ResolveFeatureBodyAndGeomElem(spCloseFeat, spClosedGeom, spClosedTopo))
                                {
                                    SourceItem tempItem = iItem;
                                    tempItem.topoBody = spClosedTopo;
                                    // Preserve original source geomElem & styleOwner so face colors come from real source
                                    ok = Clone3DBodyToDatumSolid(
                                        tempItem,
                                        iSrcPartSpec,
                                        iDstBody,
                                        iDstPrtPart,
                                        iDstDatumFactory,
                                        iDstGeoFactory,
                                        oErr);
                                    if (ok)
                                    {
                                        // Re-run face transfer directly against true source geomElem/topoBody
                                        CATIGeometricalElement_var spDstGeom;
                                        CATBody_var spDstTopo;
                                        CATIBodyRequest_var spDstReq(iDstBody);
                                        CATLISTV(CATBaseUnknown_var) dstRes;
                                        if (spDstReq != NULL_var &&
                                            SUCCEEDED(spDstReq->GetResults("MfDefault3DView", dstRes)) &&
                                            dstRes.Size() > 0 &&
                                            ResolveFeatureBodyAndGeomElem(dstRes[1], spDstGeom, spDstTopo))
                                        {
                                            GraphicStyle style;
                                            ExtractEffectiveGraphicStyle(iItem.styleOwner, iSrcPartSpec, style);
                                            CATSoftwareConfiguration* pTmpCfg = ::CATCreateSoftwareConfiguration();
                                            if (pTmpCfg != NULL)
                                            {
                                                CATTopData td(pTmpCfg, NULL);
                                                std::vector<std::pair<CATCell*, CATCell*> > emptyPairs;
                                                TransferFaceGraphicProperties(
                                                    &td,
                                                    iItem.geomElem,
                                                    (CATBody*)iItem.topoBody,
                                                    spDstGeom,
                                                    (CATBody*)spDstTopo,
                                                    emptyPairs,
                                                    style);
                                                pTmpCfg->Release();
                                            }
                                        }
                                    }
                                    else if (iDstPartSpec != NULL_var)
                                    {
                                        SourceItem copyItem = iItem;
                                        copyItem.ownerSet = spScratchBody;
                                        CATISpecObject_var spCopiedBody;
                                        if (TryInterPartCopySolidBody(
                                                copyItem,
                                                iSrcPartSpec,
                                                iDstPartSpec,
                                                spCopiedBody,
                                                oErr))
                                        {
                                            ok = true;
                                            if (!iReusedMain)
                                            {
                                                RemoveSpecObjectFromParent(iDstPartSpec, iDstBody);
                                            }
                                            else
                                            {
                                                ioMainBodyUsed = false;
                                            }
                                        }
                                    }
                                }
                            }
                            else
                            {
                                oErr = (errBuf[0] != '\0') ? std::string(errBuf) : "CreateCloseSurface update failed";
                            }
                        }
                    }
                }
            }
            else
            {
                oErr = (errBuf[0] != '\0') ? std::string(errBuf) : UnicodeUtil::CATUnicodeToUtf8(copyErr);
                if (oErr.empty()) oErr = "Scratch InterPartCopy failed";
            }

            if (pScratchPrtCont != NULL)
            {
                pScratchPrtCont->Release();
                pScratchPrtCont = NULL;
            }
        }
    }

    CATDocumentServices::Remove(*pScratchDoc);
    pScratchDoc = NULL;
    return ok;
}

static bool TryInterPartCopySolidBody(
    const SourceItem& iItem,
    const CATISpecObject_var& iSrcPartSpec,
    const CATISpecObject_var& iDstPartSpec,
    CATISpecObject_var& oCreatedBody,
    std::string& oErr)
{
    oCreatedBody = NULL_var;
    CATMmrInterPartCopy copier(iItem.ownerSet, iDstPartSpec);
    copier.SetLinkMode(FALSE);
    copier.SetTypeOfCreatedSolidSet(FALSE);
    copier.SetColorMode(CATMmrGraphicPropertiesModes::ALL_GP);

    CATUnicodeString copyErr;
    HRESULT hrCopy = E_FAIL;
    char errBuf[512];
    if (!SafeInterPartCopyRun(&copier, &copyErr, hrCopy, errBuf, sizeof(errBuf)) || FAILED(hrCopy))
    {
        oErr = (errBuf[0] != '\0') ? std::string(errBuf) : UnicodeUtil::CATUnicodeToUtf8(copyErr);
        if (oErr.empty()) oErr = "CATMmrInterPartCopy failed";
        return false;
    }

    CATISpecObject_var spRes;
    if (FAILED(copier.GetResult(spRes)) || spRes == NULL_var)
    {
        oErr = "CATMmrInterPartCopy::GetResult returned NULL";
        return false;
    }

    SetObjectAliasU(spRes, iItem.alias);

    CATIBodyRequest_var spBodyReq(spRes);
    CATLISTV(CATBaseUnknown_var) resList;
    if (spBodyReq == NULL_var ||
        FAILED(spBodyReq->GetResults("MfDefault3DView", resList)) ||
        resList.Size() == 0)
    {
        RemoveSpecObjectFromParent(iDstPartSpec, spRes);
        oErr = "Copied Body has no result";
        return false;
    }

    CATISpecObject_var spSolidFeat(resList[1]);
    if (!IsValidDatumSolidFeature(spSolidFeat))
    {
        RemoveSpecObjectFromParent(iDstPartSpec, spRes);
        oErr = "Copied Body result is not a Datum Solid";
        return false;
    }

    // Also preserve Body-level opacity / layer / edge / show properties
    GraphicStyle style;
    ExtractEffectiveGraphicStyle(iItem.styleOwner, iSrcPartSpec, style);
    // Avoid overwriting ALL_GP colors if already explicitly set by InterPartCopy
    CATIVisProperties* pSolidVis = NULL;
    if (SUCCEEDED(spSolidFeat->QueryInterface(IID_CATIVisProperties, (void**)&pSolidVis)) && pSolidVis != NULL)
    {
        CATVisPropertiesValues cv;
        if (pSolidVis->GetPropertiesAtt(cv, CATVPColor, CATVPMesh) == S_OK)
        {
            style.hasColor = false;
        }
        pSolidVis->Release();
        pSolidVis = NULL;
    }
    ApplyBodyAndDatumGraphicStyle(spRes, spSolidFeat, style);

    oCreatedBody = spRes;
    return true;
}

static bool ContainsNonAsciiW(const std::wstring& iText)
{
    for (size_t i = 0; i < iText.size(); ++i)
    {
        if (iText[i] > 127)
        {
            return true;
        }
    }
    return false;
}

static bool SavePartDocumentSafe(
    CATDocument* iDoc,
    const std::wstring& iDstPath,
    std::string& oErr)
{
    oErr.clear();
    if (iDoc == NULL)
    {
        oErr = "Null CATDocument in SavePartDocumentSafe";
        return false;
    }

    std::wstring normDst = UnicodeUtil::NormalizeFullPathW(iDstPath);
    std::wstring outParent = UnicodeUtil::GetParentDirW(normDst);
    if (!UnicodeUtil::EnsureDirectoryRecursiveW(outParent))
    {
        oErr = "Failed to create output directory: " + UnicodeUtil::WStringToUtf8(outParent);
        return false;
    }

    CATUnicodeString dstPathU = UnicodeUtil::WStringToCATUnicode(normDst);
    HRESULT hrDirect = CATDocumentServices::SaveAs(*iDoc, dstPathU);
    if (SUCCEEDED(hrDirect))
    {
        return true;
    }

    if (ContainsNonAsciiW(normDst))
    {
        wchar_t tempDir[MAX_PATH];
        DWORD tLen = ::GetTempPathW(MAX_PATH, tempDir);
        if (tLen > 0 && tLen < MAX_PATH)
        {
            wchar_t tempFile[MAX_PATH];
            _snwprintf_s(
                tempFile,
                MAX_PATH,
                _TRUNCATE,
                L"%scaa_env2solid_save_%lu_%llu.CATPart",
                tempDir,
                (unsigned long)::GetCurrentProcessId(),
                (unsigned long long)::GetTickCount64());

            std::wstring tempW(tempFile);
            CATUnicodeString tempU = UnicodeUtil::WStringToCATUnicode(tempW);
            HRESULT hrTemp = CATDocumentServices::SaveAs(*iDoc, tempU);
            if (SUCCEEDED(hrTemp))
            {
                if (::MoveFileExW(
                        tempW.c_str(),
                        normDst.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
                {
                    UnicodeUtil::LogInfo("Saved output via ASCII temp staging: " +
                        UnicodeUtil::WStringToUtf8(normDst));
                    return true;
                }
                DWORD gle = ::GetLastError();
                ::DeleteFileW(tempW.c_str());
                char mbuf[256];
                _snprintf_s(mbuf, sizeof(mbuf), _TRUNCATE,
                    "MoveFileExW failed after temp SaveAs (directHr=0x%08X, WinError=%lu)",
                    (unsigned int)hrDirect, (unsigned long)gle);
                oErr = mbuf;
                return false;
            }

            char ebuf[256];
            _snprintf_s(ebuf, sizeof(ebuf), _TRUNCATE,
                "SaveAs failed (directHr=0x%08X, tempAsciiHr=0x%08X)",
                (unsigned int)hrDirect, (unsigned int)hrTemp);
            oErr = ebuf;
            return false;
        }
    }

    char sbuf[128];
    _snprintf_s(sbuf, sizeof(sbuf), _TRUNCATE,
        "CATDocumentServices::SaveAs failed (HRESULT=0x%08X)", (unsigned int)hrDirect);
    oErr = sbuf;
    return false;
}

static std::string GetSpecTypeUtf8(const CATISpecObject_var& iObj)
{
    if (iObj == NULL_var) return "NULL";
    return UnicodeUtil::CATUnicodeToUtf8(iObj->GetType());
}

static void PrintFeatureInspectLine(
    const char* iPrefix,
    const CATISpecObject_var& iFeat,
    const CATISpecObject_var& iPartSpec,
    CATSoftwareConfiguration* iConfig)
{
    std::string name = GetObjectAliasUtf8(iFeat);
    std::string featType = GetSpecTypeUtf8(iFeat);
    HRESULT hrShow = E_FAIL;
    int showAttr = -1;
    GetObjectShowStatus((CATBaseUnknown*)iFeat, hrShow, showAttr);
    bool selfVis = IsSingleObjectVisible((CATBaseUnknown*)iFeat);
    bool hierVis = IsHierarchyVisible(iFeat, iPartSpec);
    bool active = IsFeatureActive(iFeat);

    CATIMf3DBehavior_var spBeh(iFeat);
    CATIMf3DBehavior2_var spBeh2(iFeat);
    int isSolid = (spBeh != NULL_var && spBeh->IsASolid() == S_OK) ? 1 : 0;
    int isShape = (spBeh != NULL_var && spBeh->IsAShape() == S_OK) ? 1 : 0;
    int isDatum = (spBeh != NULL_var && spBeh->IsADatum() == S_OK) ? 1 : 0;
    int isVol   = (spBeh2 != NULL_var && spBeh2->IsAVolume() == S_OK) ? 1 : 0;

    CATIGeometricalElement_var spGeom;
    CATBody_var spTopo;
    ResolveFeatureBodyAndGeomElem(iFeat, spGeom, spTopo);

    int nV = 0, nE = 0, nF = 0, nVol = 0;
    bool closedSurf = false;
    double volVal = 0.0, areaVal = 0.0;
    std::string bboxStr = "empty";

    if (spTopo != NULL_var)
    {
        spTopo->GetCellNumbers(&nV, &nE, &nF, &nVol);
        closedSurf = (nVol == 0 && IsClosedSurfaceBody((CATBody*)spTopo));

        CATMathBox box = spTopo->GetBoundingBox();
        if (!box.IsEmpty())
        {
            double x0, x1, y0, y1, z0, z1;
            box.GetLimits(x0, x1, y0, y1, z0, z1);
            char bbuf[192];
            _snprintf_s(bbuf, sizeof(bbuf), _TRUNCATE,
                "[%.3f..%.3f, %.3f..%.3f, %.3f..%.3f]", x0, x1, y0, y1, z0, z1);
            bboxStr = bbuf;
        }

        if (iConfig != NULL)
        {
            CATTopData td(iConfig, NULL);
            CATDynMassProperties3D* pMass = ::CATDynCreateMassProperties3D(&td, (CATBody*)spTopo);
            if (pMass != NULL)
            {
                volVal = pMass->GetVolume();
                areaVal = pMass->GetWetArea();
                delete pMass;
                pMass = NULL;
            }
        }
    }

    GraphicStyle st;
    ExtractEffectiveGraphicStyle(iFeat, iPartSpec, st);
    FaceColorSummary fcs = InspectFaceGraphicProperties(spGeom, (CATBody*)spTopo);

    char line[1280];
    _snprintf_s(line, sizeof(line), _TRUNCATE,
        "%s'%s' type=%s | vis(self=%d,hier=%d,showRaw(hr=0x%08X,attr=%d)) act=%d | Solid=%d Vol=%d Shape=%d Datum=%d ClosedSurf=%d | "
        "cells(V=%d,E=%d,F=%d,Vol=%d) | volume=%.4f area=%.4f bbox=%s | "
        "style(rgb=%s%u,%u,%u op=%s%u) | faces=%d explicitFaceColors=%d explicitFaceOp=%d",
        iPrefix, name.c_str(), featType.c_str(),
        selfVis ? 1 : 0, hierVis ? 1 : 0, (unsigned int)hrShow, showAttr, active ? 1 : 0,
        isSolid, isVol, isShape, isDatum, closedSurf ? 1 : 0,
        nV, nE, nF, nVol,
        volVal, areaVal, bboxStr.c_str(),
        st.hasColor ? "" : "auto:", st.r, st.g, st.b,
        st.hasOpacity ? "" : "auto:", st.opacity,
        fcs.totalFaces, fcs.explicitColoredFaces, fcs.customOpacityFaces);
    UnicodeUtil::LogInfo(line);

    if (!fcs.faceColorDetails.empty())
    {
        std::string details = std::string(iPrefix) + "  faceOverrides: ";
        for (size_t i = 0; i < fcs.faceColorDetails.size(); ++i)
        {
            if (i > 0) details += ", ";
            details += fcs.faceColorDetails[i];
        }
        UnicodeUtil::LogInfo(details);
    }
}

static void PrintDirectDescendantsInspect(const CATISpecObject_var& iSet)
{
    CATIDescendants_var spDesc(iSet);
    if (spDesc == NULL_var) return;
    int n = spDesc->GetNumberOfChildren();
    for (int c = 1; c <= n; ++c)
    {
        CATISpecObject_var spChild = spDesc->GetChildAtPosition(c);
        if (spChild == NULL_var) continue;
        HRESULT hrShow = E_FAIL;
        int showAttr = -1;
        GetObjectShowStatus((CATBaseUnknown*)spChild, hrShow, showAttr);
        char cbuf[512];
        _snprintf_s(cbuf, sizeof(cbuf), _TRUNCATE,
            "  * child[%d]: '%s' type=%s act=%d showRaw(hr=0x%08X,attr=%d)",
            c,
            GetObjectAliasUtf8(spChild).c_str(),
            GetSpecTypeUtf8(spChild).c_str(),
            IsFeatureActive(spChild) ? 1 : 0,
            (unsigned int)hrShow,
            showAttr);
        UnicodeUtil::LogInfo(cbuf);
    }
}

static bool BuildCubeSketch(
    const CATIContainer_var& iSpecContainer,
    const CATISpecObject_var& iPlane,
    double iOriginX,
    double iOriginY,
    double iSize,
    CATISpecObject_var& oSketchSpec)
{
    CATISketchFactory_var spSketchFact(iSpecContainer);
    if (spSketchFact == NULL_var || iPlane == NULL_var) return false;

    CATISketch_var spSketch = spSketchFact->CreateSketch(iPlane);
    if (spSketch == NULL_var) return false;

    spSketch->OpenEdition();
    CATI2DWFFactory_var sp2DFact(spSketch);
    if (sp2DFact == NULL_var)
    {
        spSketch->CloseEdition();
        return false;
    }

    double p1[2] = { iOriginX, iOriginY };
    double p2[2] = { iOriginX + iSize, iOriginY };
    double p3[2] = { iOriginX + iSize, iOriginY + iSize };
    double p4[2] = { iOriginX, iOriginY + iSize };

    CATI2DPoint_var pt1 = sp2DFact->CreatePoint(p1);
    CATI2DPoint_var pt2 = sp2DFact->CreatePoint(p2);
    CATI2DPoint_var pt3 = sp2DFact->CreatePoint(p3);
    CATI2DPoint_var pt4 = sp2DFact->CreatePoint(p4);

    CATI2DLine_var l1 = sp2DFact->CreateLine(p1, p2);
    CATI2DLine_var l2 = sp2DFact->CreateLine(p2, p3);
    CATI2DLine_var l3 = sp2DFact->CreateLine(p3, p4);
    CATI2DLine_var l4 = sp2DFact->CreateLine(p4, p1);

    CATI2DCurve_var c1(l1), c2(l2), c3(l3), c4(l4);
    if (c1 != NULL_var) { c1->SetStartPoint(pt1); c1->SetEndPoint(pt2); }
    if (c2 != NULL_var) { c2->SetStartPoint(pt2); c2->SetEndPoint(pt3); }
    if (c3 != NULL_var) { c3->SetStartPoint(pt3); c3->SetEndPoint(pt4); }
    if (c4 != NULL_var) { c4->SetStartPoint(pt4); c4->SetEndPoint(pt1); }

    spSketch->CloseEdition();
    oSketchSpec = spSketch;
    return (oSketchSpec != NULL_var);
}

static void SetObjColorAndShow(
    const CATBaseUnknown_var& iObj,
    bool iSetColor, unsigned int iR, unsigned int iG, unsigned int iB,
    bool iSetOp, unsigned int iOp,
    CATShowAttribut iShow)
{
    if (iObj == NULL_var) return;
    CATIVisProperties* pVis = NULL;
    if (FAILED(iObj->QueryInterface(IID_CATIVisProperties, (void**)&pVis)) || pVis == NULL)
    {
        return;
    }

    CATVisPropertiesValues sv;
    sv.SetShowAttr(iShow);
    pVis->SetPropertiesAtt(sv, CATVPShow, CATVPGlobalType);

    if (iSetColor)
    {
        CATVisPropertiesValues cv;
        cv.SetColor(iR, iG, iB, 0);
        CATVisGeomType tm = CATVPMesh;
        CATVisGeomType ta = CATVPAsm;
        if (SUCCEEDED(pVis->IsGeomTypeDefined(tm))) pVis->SetPropertiesAtt(cv, CATVPColor, CATVPMesh);
        if (SUCCEEDED(pVis->IsGeomTypeDefined(ta))) pVis->SetPropertiesAtt(cv, CATVPColor, CATVPAsm);
    }
    if (iSetOp)
    {
        CATVisPropertiesValues ov;
        ov.SetOpacity(iOp, 0);
        CATVisGeomType tm = CATVPMesh;
        CATVisGeomType ta = CATVPAsm;
        if (SUCCEEDED(pVis->IsGeomTypeDefined(tm))) pVis->SetPropertiesAtt(ov, CATVPOpacity, CATVPMesh);
        if (SUCCEEDED(pVis->IsGeomTypeDefined(ta))) pVis->SetPropertiesAtt(ov, CATVPOpacity, CATVPAsm);
    }

    pVis->Release();
    pVis = NULL;
}

static void SetFaceColorAndOpacity(
    const CATIGeometricalElement_var& iGeomElem,
    const CATBody_var& iBody,
    int iFaceIndex1Based,
    unsigned int iR, unsigned int iG, unsigned int iB,
    bool iSetOp, unsigned int iOp)
{
    if (iGeomElem == NULL_var || iBody == NULL_var) return;
    CATLISTP(CATCell) faces;
    ((CATBody*)iBody)->GetAllCells(faces, 2);
    if (iFaceIndex1Based < 1 || iFaceIndex1Based > faces.Size()) return;

    CATCell* pCell = faces[iFaceIndex1Based];
    CATIBRepAccess_var spBRep = ::CATBRepDecode(CATCell_var(pCell), iGeomElem);
    if (spBRep == NULL_var) return;

    CATIVisProperties* pVis = NULL;
    if (SUCCEEDED(spBRep->QueryInterface(IID_CATIVisProperties, (void**)&pVis)) && pVis != NULL)
    {
        CATVisPropertiesValues cv;
        cv.SetColor(iR, iG, iB, 0);
        pVis->SetPropertiesAtt(cv, CATVPColor, CATVPMesh);
        if (iSetOp)
        {
            CATVisPropertiesValues ov;
            ov.SetOpacity(iOp, 0);
            pVis->SetPropertiesAtt(ov, CATVPOpacity, CATVPMesh);
        }
        pVis->Release();
        pVis = NULL;
    }
}

} // anonymous namespace

bool InspectPartFile(const std::wstring& iPath, std::string& oErr)
{
    oErr.clear();
    if (!UnicodeUtil::FileExistsW(iPath))
    {
        oErr = "File does not exist: " + UnicodeUtil::WStringToUtf8(iPath);
        return false;
    }

    CATDocument* pDoc = NULL;
    CATUnicodeString pathU = UnicodeUtil::WStringToCATUnicode(iPath);
    HRESULT hr = CATDocumentServices::OpenDocument(pathU, pDoc, TRUE);
    if (FAILED(hr) || pDoc == NULL)
    {
        char buf[128];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "OpenDocument failed (HRESULT=0x%08X)", (unsigned int)hr);
        oErr = buf;
        return false;
    }

    ::CATLockDocument(*pDoc);
    bool ok = false;

    {
        CATIPrtContainer* pPrtCont = NULL;
        CATIPrtPart_var spPrtPart;
        CATISpecObject_var spPartSpec;
        CATIDatumFactory_var spDatumFactory;
        CATIMechanicalRootFactory_var spMechRoot;
        CATGeoFactory_var spGeoFactory;

        if (GetPartHandles(pDoc, pPrtCont, spPrtPart, spPartSpec, spDatumFactory, spMechRoot, spGeoFactory, oErr))
        {
            UnicodeUtil::LogInfo("=== INSPECT: " + UnicodeUtil::WStringToUtf8(iPath) + " ===");
            CATSoftwareConfiguration* pConfig = ::CATCreateSoftwareConfiguration();

            CATIPartRequest_var spPartReq(spPartSpec);
            CATBaseUnknown_var spMainBody;
            if (spPartReq != NULL_var)
            {
                UnicodeUtil::LogInfo("[Part] '" + GetObjectAliasUtf8(spPartSpec) + "' top-level children:");
                PrintDirectDescendantsInspect(spPartSpec);
                spPartReq->GetMainBody("MfDefault3DView", spMainBody);

                CATLISTV(CATBaseUnknown_var) solidBodies;
                if (SUCCEEDED(spPartReq->GetSolidBodies("MfDefault3DView", solidBodies)))
                {
                    for (int i = 1; i <= solidBodies.Size(); ++i)
                    {
                        CATISpecObject_var spBody(solidBodies[i]);
                        if (spBody == NULL_var) continue;
                        HRESULT hrShow = E_FAIL;
                        int showAttr = -1;
                        GetObjectShowStatus((CATBaseUnknown*)spBody, hrShow, showAttr);
                        GraphicStyle bst;
                        ExtractEffectiveGraphicStyle(spBody, spPartSpec, bst);
                        char hdr[512];
                        _snprintf_s(hdr, sizeof(hdr), _TRUNCATE,
                            "[Body%s] '%s' type=%s vis(self=%d,hier=%d,showRaw(hr=0x%08X,attr=%d)) style(rgb=%s%u,%u,%u op=%s%u)",
                            (solidBodies[i] == spMainBody) ? ":Main" : "",
                            GetObjectAliasUtf8(spBody).c_str(),
                            GetSpecTypeUtf8(spBody).c_str(),
                            IsSingleObjectVisible((CATBaseUnknown*)spBody) ? 1 : 0,
                            IsHierarchyVisible(spBody, spPartSpec) ? 1 : 0,
                            (unsigned int)hrShow, showAttr,
                            bst.hasColor ? "" : "auto:", bst.r, bst.g, bst.b,
                            bst.hasOpacity ? "" : "auto:", bst.opacity);
                        UnicodeUtil::LogInfo(hdr);
                        PrintDirectDescendantsInspect(spBody);

                        CATIBodyRequest_var spBReq(spBody);
                        CATLISTV(CATBaseUnknown_var) resList;
                        if (spBReq != NULL_var && SUCCEEDED(spBReq->GetResults("MfDefault3DView", resList)))
                        {
                            for (int r = 1; r <= resList.Size(); ++r)
                            {
                                CATISpecObject_var spFeat(resList[r]);
                                if (spFeat != NULL_var)
                                {
                                    PrintFeatureInspectLine("  - ", spFeat, spPartSpec, pConfig);
                                }
                            }
                        }
                    }
                }

                CATLISTV(CATBaseUnknown_var) surfBodies;
                if (SUCCEEDED(spPartReq->GetSurfBodies("MfDefault3DView", surfBodies)))
                {
                    for (int i = 1; i <= surfBodies.Size(); ++i)
                    {
                        CATISpecObject_var spSet(surfBodies[i]);
                        if (spSet == NULL_var) continue;
                        HRESULT hrShow = E_FAIL;
                        int showAttr = -1;
                        GetObjectShowStatus((CATBaseUnknown*)spSet, hrShow, showAttr);
                        char hdr[512];
                        _snprintf_s(hdr, sizeof(hdr), _TRUNCATE,
                            "[GeomSet] '%s' type=%s vis(self=%d,hier=%d,showRaw(hr=0x%08X,attr=%d))",
                            GetObjectAliasUtf8(spSet).c_str(),
                            GetSpecTypeUtf8(spSet).c_str(),
                            IsSingleObjectVisible((CATBaseUnknown*)spSet) ? 1 : 0,
                            IsHierarchyVisible(spSet, spPartSpec) ? 1 : 0,
                            (unsigned int)hrShow, showAttr);
                        UnicodeUtil::LogInfo(hdr);
                        PrintDirectDescendantsInspect(spSet);

                        CATIBodyRequest_var spSReq(spSet);
                        CATLISTV(CATBaseUnknown_var) resList;
                        if (spSReq != NULL_var && SUCCEEDED(spSReq->GetResults("MfDefault3DView", resList)))
                        {
                            for (int r = 1; r <= resList.Size(); ++r)
                            {
                                CATISpecObject_var spFeat(resList[r]);
                                if (spFeat != NULL_var)
                                {
                                    PrintFeatureInspectLine("  - ", spFeat, spPartSpec, pConfig);
                                }
                            }
                        }
                    }
                }
            }

            if (pConfig != NULL) pConfig->Release();
            if (pPrtCont != NULL)
            {
                pPrtCont->Release();
                pPrtCont = NULL;
            }
            ok = true;
        }
    }

    ::CATUnLockDocument(*pDoc);
    pDoc = NULL;
    return ok;
}

int ConvertSinglePart(
    const std::wstring& iSrcPath,
    const std::wstring& iDstPath,
    ConversionSummary& oSummary)
{
    oSummary = ConversionSummary();
    oSummary.inputPath = UnicodeUtil::NormalizeFullPathW(iSrcPath);
    oSummary.outputPath = UnicodeUtil::NormalizeFullPathW(iDstPath);

    if (!UnicodeUtil::FileExistsW(oSummary.inputPath))
    {
        oSummary.errorMessage = "Source file does not exist: " + UnicodeUtil::WStringToUtf8(oSummary.inputPath);
        oSummary.exitCode = 2;
        return oSummary.exitCode;
    }

    if (_wcsicmp(oSummary.inputPath.c_str(), oSummary.outputPath.c_str()) == 0)
    {
        oSummary.errorMessage = "Refusing to overwrite source input file";
        oSummary.exitCode = 3;
        return oSummary.exitCode;
    }

    CATDocument* pSrcDoc = NULL;
    CATUnicodeString srcPathU = UnicodeUtil::WStringToCATUnicode(oSummary.inputPath);
    HRESULT hr = CATDocumentServices::OpenDocument(srcPathU, pSrcDoc, TRUE);
    if (FAILED(hr) || pSrcDoc == NULL)
    {
        char buf[128];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "OpenDocument failed (HRESULT=0x%08X)", (unsigned int)hr);
        oSummary.errorMessage = buf;
        oSummary.exitCode = 4;
        return oSummary.exitCode;
    }
    ::CATLockDocument(*pSrcDoc);

    CATDocument* pDestDoc = NULL;
    bool createdDestDoc = false;

    {
        CATIPrtContainer* pSrcPrtCont = NULL;
        CATIPrtPart_var spSrcPrtPart;
        CATISpecObject_var spSrcPartSpec;
        CATIDatumFactory_var spSrcDatumFactory;
        CATIMechanicalRootFactory_var spSrcMechRoot;
        CATGeoFactory_var spSrcGeoFactory;

        if (!GetPartHandles(
                pSrcDoc,
                pSrcPrtCont,
                spSrcPrtPart,
                spSrcPartSpec,
                spSrcDatumFactory,
                spSrcMechRoot,
                spSrcGeoFactory,
                oSummary.errorMessage))
        {
            oSummary.exitCode = 5;
        }
        else
        {
            std::vector<SourceItem> items;
            CollectConvertibleItems(
                spSrcPartSpec,
                items,
                oSummary.skippedDiscardedCount,
                oSummary.totalBodiesAndSetsInspected);
            oSummary.discoveredConvertible = (int)items.size();

            if (items.empty())
            {
                oSummary.errorMessage = "No visible active Solid Body, Volume, or Closed Surface found to convert";
                oSummary.exitCode = 10;
            }
            else
            {
                hr = CATDocumentServices::New("CATPart", pDestDoc);
                if (FAILED(hr) || pDestDoc == NULL)
                {
                    oSummary.errorMessage = "Failed to create destination CATPart";
                    oSummary.exitCode = 6;
                }
                else
                {
                    createdDestDoc = true;
                    CATInit_var spDestInit(pDestDoc);
                    if (spDestInit != NULL_var) spDestInit->Init(TRUE);

                    CATIPrtContainer* pDstPrtCont = NULL;
                    CATIPrtPart_var spDstPrtPart;
                    CATISpecObject_var spDstPartSpec;
                    CATIDatumFactory_var spDstDatumFactory;
                    CATIMechanicalRootFactory_var spDstMechRoot;
                    CATGeoFactory_var spDstGeoFactory;

                    if (!GetPartHandles(
                            pDestDoc,
                            pDstPrtCont,
                            spDstPrtPart,
                            spDstPartSpec,
                            spDstDatumFactory,
                            spDstMechRoot,
                            spDstGeoFactory,
                            oSummary.errorMessage))
                    {
                        oSummary.exitCode = 7;
                    }
                    else
                    {
                        CATIPartRequest_var spDstPartReq(spDstPartSpec);
                        CATBaseUnknown_var spDstMainBodyUnk;
                        if (spDstPartReq != NULL_var)
                        {
                            spDstPartReq->GetMainBody("MfDefault3DView", spDstMainBodyUnk);
                        }
                        CATISpecObject_var spDstMainBody(spDstMainBodyUnk);
                        bool mainBodyUsed = false;

                        for (size_t idx = 0; idx < items.size(); ++idx)
                        {
                            const SourceItem& item = items[idx];
                            ConvertedItemReport rep;
                            rep.sourceName = UnicodeUtil::CATUnicodeToUtf8(item.alias);
                            rep.sourceKind = KindToString(item.kind);
                            rep.destName = rep.sourceName;

                            bool itemOk = false;
                            std::string itemErr;

                            if (item.kind == KIND_SOLID_BODY)
                            {
                                if (mainBodyUsed && !item.hasExtraSurfacesInBody)
                                {
                                    CATISpecObject_var spCreatedBody;
                                    if (TryInterPartCopySolidBody(item, spSrcPartSpec, spDstPartSpec, spCreatedBody, itemErr))
                                    {
                                        rep.method = "CATMmrInterPartCopy(AsResult,ALL_GP)";
                                        itemOk = true;
                                    }
                                    else
                                    {
                                        UnicodeUtil::LogWarn("InterPartCopy fallback for " + rep.sourceName + ": " + itemErr);
                                        CATISpecObject_var spNewBody = spDstMechRoot->CreatePRTTool(item.alias, spDstPartSpec);
                                        if (spNewBody != NULL_var &&
                                            Clone3DBodyToDatumSolid(
                                                item, spSrcPartSpec, spNewBody, spDstPrtPart,
                                                spDstDatumFactory, spDstGeoFactory, itemErr))
                                        {
                                            rep.method = "Clone3DBodyToDatumSolid";
                                            itemOk = true;
                                        }
                                        else if (spNewBody != NULL_var)
                                        {
                                            RemoveSpecObjectFromParent(spDstPartSpec, spNewBody);
                                        }
                                    }
                                }
                                else
                                {
                                    CATISpecObject_var spTargetBody;
                                    bool reusedMain = false;
                                    if (!mainBodyUsed && spDstMainBody != NULL_var)
                                    {
                                        mainBodyUsed = true;
                                        reusedMain = true;
                                        spTargetBody = spDstMainBody;
                                        SetObjectAliasU(spTargetBody, item.alias);
                                    }
                                    else
                                    {
                                        spTargetBody = spDstMechRoot->CreatePRTTool(item.alias, spDstPartSpec);
                                    }

                                    if (spTargetBody != NULL_var &&
                                        Clone3DBodyToDatumSolid(
                                            item, spSrcPartSpec, spTargetBody, spDstPrtPart,
                                            spDstDatumFactory, spDstGeoFactory, itemErr))
                                    {
                                        rep.method = "Clone3DBodyToDatumSolid";
                                        itemOk = true;
                                    }
                                    else
                                    {
                                        if (reusedMain)
                                        {
                                            mainBodyUsed = false;
                                        }
                                        else if (spTargetBody != NULL_var)
                                        {
                                            RemoveSpecObjectFromParent(spDstPartSpec, spTargetBody);
                                        }

                                        if (!item.hasExtraSurfacesInBody)
                                        {
                                            UnicodeUtil::LogWarn("Direct Datum Solid failed for SolidBody '" + rep.sourceName +
                                                "' (" + itemErr + "), trying CATMmrInterPartCopy fallback...");
                                            CATISpecObject_var spCreatedBody;
                                            if (TryInterPartCopySolidBody(item, spSrcPartSpec, spDstPartSpec, spCreatedBody, itemErr))
                                            {
                                                rep.method = "CATMmrInterPartCopy(AsResult,ALL_GP)";
                                                itemOk = true;
                                            }
                                        }
                                        else if (spTargetBody == NULL_var)
                                        {
                                            itemErr = "CreatePRTTool returned NULL";
                                        }
                                    }
                                }
                            }
                            else if (item.kind == KIND_SOLID_FEATURE || item.kind == KIND_VOLUME)
                            {
                                CATISpecObject_var spTargetBody;
                                bool reusedMain = false;
                                if (!mainBodyUsed && spDstMainBody != NULL_var)
                                {
                                    mainBodyUsed = true;
                                    reusedMain = true;
                                    spTargetBody = spDstMainBody;
                                    SetObjectAliasU(spTargetBody, item.alias);
                                }
                                else
                                {
                                    spTargetBody = spDstMechRoot->CreatePRTTool(item.alias, spDstPartSpec);
                                }

                                if (spTargetBody != NULL_var)
                                {
                                    if (Clone3DBodyToDatumSolid(
                                            item, spSrcPartSpec, spTargetBody, spDstPrtPart,
                                            spDstDatumFactory, spDstGeoFactory, itemErr))
                                    {
                                        rep.method = "Clone3DBodyToDatumSolid";
                                        itemOk = true;
                                    }
                                    else
                                    {
                                        UnicodeUtil::LogWarn("Direct Datum Solid failed for " + rep.sourceName +
                                            " (" + itemErr + "), trying CloseSurface fallback...");
                                        if (ConvertViaScratchCloseSurface(
                                                item, spSrcPartSpec, spTargetBody, spDstPrtPart,
                                                spDstDatumFactory, spDstGeoFactory,
                                                spDstPartSpec, reusedMain, mainBodyUsed, itemErr))
                                        {
                                            rep.method = "ScratchCloseSurface+CloneDatum";
                                            itemOk = true;
                                        }
                                        else if (!reusedMain)
                                        {
                                            RemoveSpecObjectFromParent(spDstPartSpec, spTargetBody);
                                        }
                                        else
                                        {
                                            mainBodyUsed = false;
                                        }
                                    }
                                }
                                else
                                {
                                    itemErr = "CreatePRTTool returned NULL";
                                }
                            }
                            else if (item.kind == KIND_CLOSED_SURFACE)
                            {
                                CATISpecObject_var spTargetBody;
                                bool reusedMain = false;
                                if (!mainBodyUsed && spDstMainBody != NULL_var)
                                {
                                    mainBodyUsed = true;
                                    reusedMain = true;
                                    spTargetBody = spDstMainBody;
                                    SetObjectAliasU(spTargetBody, item.alias);
                                }
                                else
                                {
                                    spTargetBody = spDstMechRoot->CreatePRTTool(item.alias, spDstPartSpec);
                                }

                                if (spTargetBody != NULL_var)
                                {
                                    if (ConvertClosedSurfaceDirect(
                                            item, spSrcPartSpec, spTargetBody, spDstPrtPart,
                                            spDstDatumFactory, spDstGeoFactory, itemErr))
                                    {
                                        rep.method = "CATCGMCreateCloseOperator+Datum";
                                        itemOk = true;
                                    }
                                    else
                                    {
                                        UnicodeUtil::LogWarn("CATCGMCreateCloseOperator failed for " + rep.sourceName +
                                            " (" + itemErr + "), trying CreateCloseSurface fallback...");
                                        if (ConvertViaScratchCloseSurface(
                                                item, spSrcPartSpec, spTargetBody, spDstPrtPart,
                                                spDstDatumFactory, spDstGeoFactory,
                                                spDstPartSpec, reusedMain, mainBodyUsed, itemErr))
                                        {
                                            rep.method = "ScratchCloseSurface+CloneDatum";
                                            itemOk = true;
                                        }
                                        else if (!reusedMain)
                                        {
                                            RemoveSpecObjectFromParent(spDstPartSpec, spTargetBody);
                                        }
                                        else
                                        {
                                            mainBodyUsed = false;
                                        }
                                    }
                                }
                                else
                                {
                                    itemErr = "CreatePRTTool returned NULL";
                                }
                            }

                            rep.ok = itemOk;
                            rep.note = itemOk ? "OK" : itemErr;
                            oSummary.items.push_back(rep);

                            if (itemOk)
                            {
                                oSummary.convertedCount++;
                                UnicodeUtil::LogInfo("Converted [" + rep.sourceKind + "] '" +
                                    rep.sourceName + "' -> '" + rep.destName + "' via " + rep.method);
                            }
                            else
                            {
                                oSummary.failedCount++;
                                UnicodeUtil::LogError("Failed [" + rep.sourceKind + "] '" +
                                    rep.sourceName + "': " + rep.note);
                            }
                        }

                        if (oSummary.convertedCount == 0)
                        {
                            oSummary.errorMessage = "All discovered convertible items failed conversion";
                            oSummary.exitCode = 8;
                        }
                        else
                        {
                            if (spDstPartReq != NULL_var)
                            {
                                CATLISTV(CATBaseUnknown_var) dstSurfBodies;
                                if (SUCCEEDED(spDstPartReq->GetSurfBodies("MfDefault3DView", dstSurfBodies)))
                                {
                                    for (int gs = 1; gs <= dstSurfBodies.Size(); ++gs)
                                    {
                                        CATISpecObject_var spGs(dstSurfBodies[gs]);
                                        if (spGs == NULL_var) continue;
                                        CATIDescendants_var spGsDesc(spGs);
                                        if (spGsDesc != NULL_var && spGsDesc->GetNumberOfChildren() == 0)
                                        {
                                            UnicodeUtil::LogInfo("Removing empty Geometrical Set: " + GetObjectAliasUtf8(spGs));
                                            RemoveSpecObjectFromParent(spDstPartSpec, spGs);
                                        }
                                    }
                                }
                            }

                            char updErr[512];
                            if (!SafeSpecUpdate((CATISpecObject*)spDstPartSpec, updErr, sizeof(updErr)))
                            {
                                oSummary.errorMessage = std::string("Final part update failed: ") +
                                    ((updErr[0] != '\0') ? updErr : "Unknown Update() error");
                                oSummary.exitCode = 13;
                                UnicodeUtil::LogError(oSummary.errorMessage);
                            }
                            else
                            {
                                std::string saveErr;
                                if (!SavePartDocumentSafe(pDestDoc, oSummary.outputPath, saveErr))
                                {
                                    oSummary.errorMessage = saveErr;
                                    oSummary.exitCode = 9;
                                }
                                else
                                {
                                    oSummary.exitCode = (oSummary.failedCount > 0) ? 11 : 0;
                                    UnicodeUtil::LogInfo("Saved output: " + UnicodeUtil::WStringToUtf8(oSummary.outputPath));
                                }
                            }
                        }

                        if (pDstPrtCont != NULL)
                        {
                            pDstPrtCont->Release();
                            pDstPrtCont = NULL;
                        }
                    }
                }
            }

            if (pSrcPrtCont != NULL)
            {
                pSrcPrtCont->Release();
                pSrcPrtCont = NULL;
            }
        }
    }

    if (createdDestDoc && pDestDoc != NULL)
    {
        CATDocumentServices::Remove(*pDestDoc);
        pDestDoc = NULL;
    }

    ::CATUnLockDocument(*pSrcDoc);
    pSrcDoc = NULL;

    return oSummary.exitCode;
}

bool CreateTestPartFile(const std::wstring& iDstPath, std::string& oErr)
{
    oErr.clear();
    std::wstring normOut = UnicodeUtil::NormalizeFullPathW(iDstPath);
    if (!UnicodeUtil::EnsureDirectoryRecursiveW(UnicodeUtil::GetParentDirW(normOut)))
    {
        oErr = "Failed to create output directory for test part";
        return false;
    }

    CATDocument* pDoc = NULL;
    HRESULT hr = CATDocumentServices::New("CATPart", pDoc);
    if (FAILED(hr) || pDoc == NULL)
    {
        oErr = "CATDocumentServices::New(CATPart) failed";
        return false;
    }

    CATInit_var spInit(pDoc);
    if (spInit != NULL_var) spInit->Init(TRUE);

    bool ok = false;
    {
        CATIPrtContainer* pPrtCont = NULL;
        CATIPrtPart_var spPrtPart;
        CATISpecObject_var spPartSpec;
        CATIDatumFactory_var spDatumFactory;
        CATIMechanicalRootFactory_var spMechRoot;
        CATGeoFactory_var spGeoFactory;

        if (GetPartHandles(pDoc, pPrtCont, spPrtPart, spPartSpec, spDatumFactory, spMechRoot, spGeoFactory, oErr))
        {
            CATIContainer_var spSpecCont(pPrtCont);
            CATIPrtFactory_var spPrtFactory(pPrtCont);
            CATIGSMFactory_var spGsmFactory(pPrtCont);

            CATLISTV(CATISpecObject_var) refPlanes = spPrtPart->GetReferencePlanes();
            CATISpecObject_var spXYPlane = (refPlanes.Size() >= 1) ? refPlanes[1] : NULL_var;

            CATIPartRequest_var spPartReq(spPartSpec);
            CATBaseUnknown_var spMainBodyUnk;
            if (spPartReq != NULL_var) spPartReq->GetMainBody("MfDefault3DView", spMainBodyUnk);
            CATISpecObject_var spMainBody(spMainBodyUnk);

            char errBuf[512];

            // 1. Visible parametric PartBody (Pad + EdgeFillet) with Blue color, opacity 200, 2 face overrides
            if (spMainBody != NULL_var && spXYPlane != NULL_var && spPrtFactory != NULL_var)
            {
                spPrtPart->SetCurrentFeature(spMainBody);
                CATISpecObject_var spSketch1;
                if (BuildCubeSketch(spSpecCont, spXYPlane, 0.0, 0.0, 20.0, spSketch1))
                {
                    CATISpecObject_var spPadSpec = spPrtFactory->CreatePad(spSketch1);
                    CATIPad_var spPad(spPadSpec);
                    if (spPad != NULL_var)
                    {
                        CATMathDirection dirZ(0.0, 0.0, 1.0);
                        spPad->ModifyDirection(dirZ);
                        spPad->ModifyEndType(catOffsetLimit);
                        spPad->ModifyEndOffset(20.0);
                        spPad->ModifyStartType(catOffsetLimit);
                        spPad->ModifyStartOffset(0.0);
                        SafeSpecUpdate((CATISpecObject*)spPadSpec, errBuf, sizeof(errBuf));

                        // Add EdgeFillet on first edge of Pad
                        CATIGeometricalElement_var spPadGeom;
                        CATBody_var spPadTopo;
                        if (ResolveFeatureBodyAndGeomElem(spPadSpec, spPadGeom, spPadTopo))
                        {
                            CATLISTP(CATCell) edges;
                            ((CATBody*)spPadTopo)->GetAllCells(edges, 1);
                            if (edges.Size() >= 1)
                            {
                                CATIBRepAccess_var spEdgeBRep = ::CATBRepDecode(CATCell_var(edges[1]), spPadGeom);
                                CATIFeaturize_var spEdgeFeaturize(spEdgeBRep);
                                if (spEdgeFeaturize != NULL_var)
                                {
                                    CATISpecObject_var spRedge = spEdgeFeaturize->FeaturizeR();
                                    if (spRedge != NULL_var)
                                    {
                                        CATLISTV(CATISpecObject_var) edgeList;
                                        edgeList.Append(spRedge);
                                        CATISpecObject_var spFillet = spPrtFactory->CreateFillet(&edgeList, MINIMAL, 2.0);
                                        if (spFillet != NULL_var)
                                        {
                                            SafeSpecUpdate((CATISpecObject*)spFillet, errBuf, sizeof(errBuf));
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                SafeSpecUpdate((CATISpecObject*)spMainBody, errBuf, sizeof(errBuf));
                SetObjColorAndShow(spMainBody, true, 0, 80, 220, true, 200, CATShowAttr);

                    CATIBodyRequest_var spMainReq(spMainBody);
                    CATLISTV(CATBaseUnknown_var) mainRes;
                    if (spMainReq != NULL_var && SUCCEEDED(spMainReq->GetResults("MfDefault3DView", mainRes)) && mainRes.Size() > 0)
                    {
                        CATIGeometricalElement_var spSolidGeom;
                        CATBody_var spSolidTopo;
                        if (ResolveFeatureBodyAndGeomElem(mainRes[1], spSolidGeom, spSolidTopo))
                        {
                            SetFaceColorAndOpacity(spSolidGeom, spSolidTopo, 1, 255, 0, 0, false, 255);
                            SetFaceColorAndOpacity(spSolidGeom, spSolidTopo, 2, 0, 255, 0, false, 255);
                            UnicodeUtil::LogInfo("[TestPart] Created visible PartBody (Pad+Fillet, Blue, op=200, 2 face colors)");
                        }
                    }
            }

            // 2. Hidden (NoShow) parametric HiddenBody
            CATISpecObject_var spHiddenBody = spMechRoot->CreatePRTTool("HiddenBody", spPartSpec);
            if (spHiddenBody != NULL_var && spXYPlane != NULL_var && spPrtFactory != NULL_var)
            {
                spPrtPart->SetCurrentFeature(spHiddenBody);
                CATISpecObject_var spSketchH;
                if (BuildCubeSketch(spSpecCont, spXYPlane, 0.0, 30.0, 15.0, spSketchH))
                {
                    CATISpecObject_var spPadH = spPrtFactory->CreatePad(spSketchH);
                    CATIPad_var spPad(spPadH);
                    if (spPad != NULL_var)
                    {
                        spPad->ModifyDirection(CATMathDirection(0.0, 0.0, 1.0));
                        spPad->ModifyEndType(catOffsetLimit);
                        spPad->ModifyEndOffset(15.0);
                        spPad->ModifyStartType(catOffsetLimit);
                        spPad->ModifyStartOffset(0.0);
                        SafeSpecUpdate((CATISpecObject*)spPadH, errBuf, sizeof(errBuf));
                    }
                }
                SafeSpecUpdate((CATISpecObject*)spHiddenBody, errBuf, sizeof(errBuf));
                SetObjColorAndShow(spHiddenBody, true, 128, 128, 128, false, 255, CATNoShowAttr);
                UnicodeUtil::LogInfo("[TestPart] Created HiddenBody (NoShow)");
            }

            // 3. Visible Geometrical Set GS_Visible
            CATISpecObject_var spGsVis;
            spMechRoot->CreateGeometricalSet("GS_Visible", spPartSpec, spGsVis);
            if (spGsVis != NULL_var)
            {
                SetObjColorAndShow(spGsVis, false, 0, 0, 0, false, 255, CATShowAttr);
                CATSoftwareConfiguration* pCfg = ::CATCreateSoftwareConfiguration();

                // 3a. Visible Volume (via GSM Extrude Volume context if licensed, else Datum 3D in GS)
                bool createdGsmVolume = false;
                if (spGsmFactory != NULL_var && spXYPlane != NULL_var)
                {
                    CATISpecObject_var spSketchV;
                    if (BuildCubeSketch(spSpecCont, spXYPlane, 30.0, 0.0, 15.0, spSketchV))
                    {
                        CATIGSMDirection_var spDir = spGsmFactory->CreateDirection(spXYPlane);
                        CATIGSMExtrude_var spExt = spGsmFactory->CreateExtrude(spSketchV, spDir, NULL_var, NULL_var, TRUE);
                        if (spExt != NULL_var)
                        {
                            spExt->SetFirstLimitValue(15.0);
                            spExt->SetSecondLimitValue(0.0);
                            spExt->SetContext(CATGSMVolumeCtxt);

                            CATIGSMProceduralView_var spProc(spExt);
                            if (spProc != NULL_var)
                            {
                                spProc->InsertInProceduralView(spGsVis);
                            }
                            CATISpecObject_var spExtSpec(spExt);
                            SetObjectAliasU(spExtSpec, "Volume_Visible");
                            SafeSpecUpdate((CATISpecObject*)spExtSpec, errBuf, sizeof(errBuf));

                            CATIMf3DBehavior2_var spBeh2(spExtSpec);
                            if (spBeh2 != NULL_var && spBeh2->IsAVolume() == S_OK)
                            {
                                createdGsmVolume = true;
                                SetObjColorAndShow(spExtSpec, true, 240, 200, 0, false, 255, CATShowAttr);
                                CATIGeometricalElement_var spVg;
                                CATBody_var spVb;
                                if (ResolveFeatureBodyAndGeomElem(spExtSpec, spVg, spVb))
                                {
                                    SetFaceColorAndOpacity(spVg, spVb, 1, 180, 0, 220, false, 255);
                                }
                                UnicodeUtil::LogInfo("[TestPart] Created Volume_Visible (GSM Volume Extrude, Yellow, 1 face color)");
                            }
                            else
                            {
                                RemoveSpecObjectFromParent(spGsVis, spExtSpec);
                            }
                        }
                    }
                }
                if (!createdGsmVolume && pCfg != NULL)
                {
                    // Fallback: instantiate 3D cuboid inside GS_Visible
                    spPrtPart->SetCurrentFeature(spGsVis);
                    CATTopData td(pCfg, NULL);
                    CATICGMSolidCuboid* pCub = ::CATCGMTopCreateSolidCuboid(
                        (CATGeoFactory*)spGeoFactory, &td,
                        CATMathPoint(30, 0, 0), CATMathPoint(45, 0, 0),
                        CATMathPoint(30, 15, 0), CATMathPoint(30, 0, 15));
                    int rc = 1;
                    if (pCub != NULL && SafeCGMOperatorRun(pCub, rc, errBuf, sizeof(errBuf)) && rc == 0)
                    {
                        CATBody* pCubBody = pCub->GetResult();
                        CATISpecObject* pRawVol = NULL;
                        HRESULT hrD = E_FAIL;
                        if (pCubBody != NULL &&
                            SafeInstanciateDatum((CATIDatumFactory*)spDatumFactory, pCubBody, pRawVol, hrD, errBuf, sizeof(errBuf)) &&
                            SUCCEEDED(hrD) && pRawVol != NULL)
                        {
                            CATISpecObject_var spVol = pRawVol;
                            pRawVol->Release();
                            SetObjectAliasU(spVol, "Volume_Visible");
                            SafeSpecUpdate((CATISpecObject*)spVol, errBuf, sizeof(errBuf));
                            SetObjColorAndShow(spVol, true, 240, 200, 0, false, 255, CATShowAttr);
                            CATIGeometricalElement_var spVg;
                            CATBody_var spVb;
                            if (ResolveFeatureBodyAndGeomElem(spVol, spVg, spVb))
                            {
                                SetFaceColorAndOpacity(spVg, spVb, 1, 180, 0, 220, false, 255);
                            }
                            UnicodeUtil::LogInfo("[TestPart] Created Volume_Visible (Datum cuboid in GS, Yellow, 1 face color)");
                        }
                    }
                    if (pCub != NULL) pCub->Release();
                }

                // 3b. Hidden Volume (NoShow)
                if (pCfg != NULL)
                {
                    spPrtPart->SetCurrentFeature(spGsVis);
                    CATTopData td(pCfg, NULL);
                    CATICGMSolidCuboid* pCubH = ::CATCGMTopCreateSolidCuboid(
                        (CATGeoFactory*)spGeoFactory, &td,
                        CATMathPoint(30, 30, 0), CATMathPoint(45, 30, 0),
                        CATMathPoint(30, 45, 0), CATMathPoint(30, 30, 15));
                    int rc = 1;
                    if (pCubH != NULL && SafeCGMOperatorRun(pCubH, rc, errBuf, sizeof(errBuf)) && rc == 0)
                    {
                        CATBody* pBodyH = pCubH->GetResult();
                        CATISpecObject* pRawH = NULL;
                        HRESULT hrD = E_FAIL;
                        if (pBodyH != NULL &&
                            SafeInstanciateDatum((CATIDatumFactory*)spDatumFactory, pBodyH, pRawH, hrD, errBuf, sizeof(errBuf)) &&
                            SUCCEEDED(hrD) && pRawH != NULL)
                        {
                            CATISpecObject_var spVH = pRawH;
                            pRawH->Release();
                            SetObjectAliasU(spVH, "Volume_Hidden");
                            SafeSpecUpdate((CATISpecObject*)spVH, errBuf, sizeof(errBuf));
                            SetObjColorAndShow(spVH, true, 150, 150, 0, false, 255, CATNoShowAttr);
                            UnicodeUtil::LogInfo("[TestPart] Created Volume_Hidden (NoShow)");
                        }
                    }
                    if (pCubH != NULL) pCubH->Release();
                }

                // 3c. Visible Closed Surface (full sphere skin)
                if (pCfg != NULL)
                {
                    spPrtPart->SetCurrentFeature(spGsVis);
                    CATSphere* pSph = ((CATGeoFactory*)spGeoFactory)->CreateSphere(
                        CATMathAxis(CATMathPoint(70.0, 10.0, 10.0)), 10.0);
                    if (pSph != NULL)
                    {
                        CATSurLimits lim;
                        pSph->GetLimits(lim);
                        CATTopData td(pCfg, NULL);
                        CATICGMTopSkin* pSkinOp = ::CATCGMCreateTopSkin((CATGeoFactory*)spGeoFactory, &td, pSph, &lim);
                        int rc = 1;
                        if (pSkinOp != NULL)
                        {
                            pSkinOp->ForceSkinClosureInU();
                            pSkinOp->ForceSkinClosureInV();
                            if (SafeCGMOperatorRun(pSkinOp, rc, errBuf, sizeof(errBuf)) && rc == 0)
                            {
                                CATBody* pSkinBody = pSkinOp->GetResult();
                                CATISpecObject* pRawCs = NULL;
                                HRESULT hrD = E_FAIL;
                                if (pSkinBody != NULL &&
                                    SafeInstanciateDatum((CATIDatumFactory*)spDatumFactory, pSkinBody, pRawCs, hrD, errBuf, sizeof(errBuf)) &&
                                    SUCCEEDED(hrD) && pRawCs != NULL)
                                {
                                    CATISpecObject_var spCs = pRawCs;
                                    pRawCs->Release();
                                    SetObjectAliasU(spCs, "ClosedSurface_Visible");
                                    SafeSpecUpdate((CATISpecObject*)spCs, errBuf, sizeof(errBuf));
                                    SetObjColorAndShow(spCs, true, 0, 200, 220, false, 255, CATShowAttr);
                                    CATIGeometricalElement_var spCg;
                                    CATBody_var spCb;
                                    if (ResolveFeatureBodyAndGeomElem(spCs, spCg, spCb))
                                    {
                                        SetFaceColorAndOpacity(spCg, spCb, 1, 255, 128, 0, false, 255);
                                    }
                                    UnicodeUtil::LogInfo("[TestPart] Created ClosedSurface_Visible (Cyan, 1 face color)");
                                }
                            }
                            pSkinOp->Release();
                        }
                    }
                }

                // 3d. Visible Open Surface (partial sphere skin - should be discarded)
                if (pCfg != NULL)
                {
                    spPrtPart->SetCurrentFeature(spGsVis);
                    CATSphere* pOpenSph = ((CATGeoFactory*)spGeoFactory)->CreateSphere(
                        CATMathAxis(CATMathPoint(100.0, 10.0, 10.0)), 10.0, 0.0, 3.14159, -0.8, 0.8);
                    if (pOpenSph != NULL)
                    {
                        CATSurLimits lim;
                        pOpenSph->GetLimits(lim);
                        CATTopData td(pCfg, NULL);
                        CATICGMTopSkin* pSkinOp = ::CATCGMCreateTopSkin((CATGeoFactory*)spGeoFactory, &td, pOpenSph, &lim);
                        int rc = 1;
                        if (pSkinOp != NULL && SafeCGMOperatorRun(pSkinOp, rc, errBuf, sizeof(errBuf)) && rc == 0)
                        {
                            CATBody* pOpenBody = pSkinOp->GetResult();
                            CATISpecObject* pRawOs = NULL;
                            HRESULT hrD = E_FAIL;
                            if (pOpenBody != NULL &&
                                SafeInstanciateDatum((CATIDatumFactory*)spDatumFactory, pOpenBody, pRawOs, hrD, errBuf, sizeof(errBuf)) &&
                                SUCCEEDED(hrD) && pRawOs != NULL)
                            {
                                CATISpecObject_var spOs = pRawOs;
                                pRawOs->Release();
                                SetObjectAliasU(spOs, "OpenSurface_Visible");
                                SafeSpecUpdate((CATISpecObject*)spOs, errBuf, sizeof(errBuf));
                                SetObjColorAndShow(spOs, true, 200, 100, 100, false, 255, CATShowAttr);
                                UnicodeUtil::LogInfo("[TestPart] Created OpenSurface_Visible (open shell)");
                            }
                        }
                        if (pSkinOp != NULL) pSkinOp->Release();
                    }
                }

                // 4. Hidden Geometrical Set GS_Hidden (NoShow) containing a Show Volume
                CATISpecObject_var spGsHid;
                spMechRoot->CreateGeometricalSet("GS_Hidden", spPartSpec, spGsHid);
                if (spGsHid != NULL_var && pCfg != NULL)
                {
                    spPrtPart->SetCurrentFeature(spGsHid);
                    CATTopData td(pCfg, NULL);
                    CATICGMSolidCuboid* pCubG = ::CATCGMTopCreateSolidCuboid(
                        (CATGeoFactory*)spGeoFactory, &td,
                        CATMathPoint(130, 0, 0), CATMathPoint(145, 0, 0),
                        CATMathPoint(130, 15, 0), CATMathPoint(130, 0, 15));
                    int rc = 1;
                    if (pCubG != NULL && SafeCGMOperatorRun(pCubG, rc, errBuf, sizeof(errBuf)) && rc == 0)
                    {
                        CATBody* pBodyG = pCubG->GetResult();
                        CATISpecObject* pRawG = NULL;
                        HRESULT hrD = E_FAIL;
                        if (pBodyG != NULL &&
                            SafeInstanciateDatum((CATIDatumFactory*)spDatumFactory, pBodyG, pRawG, hrD, errBuf, sizeof(errBuf)) &&
                            SUCCEEDED(hrD) && pRawG != NULL)
                        {
                            CATISpecObject_var spVG = pRawG;
                            pRawG->Release();
                            SetObjectAliasU(spVG, "Volume_In_Hidden_GS");
                            SafeSpecUpdate((CATISpecObject*)spVG, errBuf, sizeof(errBuf));
                            SetObjColorAndShow(spVG, true, 50, 50, 200, false, 255, CATShowAttr);
                            UnicodeUtil::LogInfo("[TestPart] Created Volume_In_Hidden_GS inside GS_Hidden (NoShow)");
                        }
                    }
                    if (pCubG != NULL) pCubG->Release();
                    SetObjColorAndShow(spGsHid, false, 0, 0, 0, false, 255, CATNoShowAttr);
                }

                if (pCfg != NULL) pCfg->Release();
            }

            SafeSpecUpdate((CATISpecObject*)spPartSpec, errBuf, sizeof(errBuf));
            ok = SavePartDocumentSafe(pDoc, normOut, oErr);

            if (pPrtCont != NULL)
            {
                pPrtCont->Release();
                pPrtCont = NULL;
            }
        }
    }

    CATDocumentServices::Remove(*pDoc);
    pDoc = NULL;
    return ok;
}
