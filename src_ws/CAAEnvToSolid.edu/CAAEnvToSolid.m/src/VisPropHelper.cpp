#include "VisPropHelper.h"
#include "UnicodeUtil.h"

#include <math.h>
#include <set>
#include <vector>

#include "CATBody.h"
#include "CATCell.h"
#include "CATFace.h"
#include "CATTopData.h"
#include "CATMathBox.h"
#include "CATMathPoint.h"
#include "CATDynMassProperties3D.h"
#include "CATListOfCATCells.h"
#include "CATLISTV_CATBaseUnknown.h"
#include "CATListOfInt.h"

#include "CATIVisProperties.h"
#include "CATIVisPropertiesAbstract.h"
#include "CATVisPropertiesValues.h"
#include "CATVisPropertyType.h"
#include "CATVisGeomType.h"
#include "CATShowAttribut.h"

#include "CATIBRepAccess.h"
#include "CATMfBRepDecode.h"
#include "CATMmrApplicativeAttributes.h"

static bool TryGetColorOnVis(
    CATIVisProperties* iVis,
    CATVisGeomType iGeomType,
    bool iRequireExplicit,
    unsigned int& oR,
    unsigned int& oG,
    unsigned int& oB)
{
    if (iVis == NULL) return false;
    CATVisGeomType gType = iGeomType;
    if (FAILED(iVis->IsGeomTypeDefined(gType)))
    {
        return false;
    }
    CATVisPropertiesValues val;
    HRESULT hr = iVis->GetPropertiesAtt(val, CATVPColor, iGeomType);
    if (FAILED(hr)) return false;
    if (iRequireExplicit && hr != S_OK) return false;
    return SUCCEEDED(val.GetColor(oR, oG, oB));
}

static bool TryGetOpacityOnVis(
    CATIVisProperties* iVis,
    CATVisGeomType iGeomType,
    bool iRequireExplicit,
    unsigned int& oOpacity)
{
    if (iVis == NULL) return false;
    CATVisGeomType gType = iGeomType;
    if (FAILED(iVis->IsGeomTypeDefined(gType)))
    {
        return false;
    }
    CATVisPropertiesValues val;
    HRESULT hr = iVis->GetPropertiesAtt(val, CATVPOpacity, iGeomType);
    if (FAILED(hr)) return false;
    if (iRequireExplicit && hr != S_OK) return false;
    return SUCCEEDED(val.GetOpacity(oOpacity));
}

bool GetObjectShowStatus(CATBaseUnknown* iObj, HRESULT& oHr, int& oShowEnum)
{
    oHr = E_FAIL;
    oShowEnum = -1;
    if (iObj == NULL)
    {
        return false;
    }
    CATIVisProperties* pVis = NULL;
    oHr = iObj->QueryInterface(IID_CATIVisProperties, (void**)&pVis);
    if (FAILED(oHr) || pVis == NULL)
    {
        return false;
    }

    CATVisPropertiesValues val;
    oHr = pVis->GetPropertiesAtt(val, CATVPShow, CATVPGlobalType);
    pVis->Release();
    pVis = NULL;

    if (SUCCEEDED(oHr))
    {
        CATShowAttribut showAttr = CATShowAttr;
        if (SUCCEEDED(val.GetShowAttr(showAttr)))
        {
            oShowEnum = (int)showAttr;
            return true;
        }
    }
    return false;
}

bool IsSingleObjectVisible(CATBaseUnknown* iObj)
{
    if (iObj == NULL)
    {
        return false;
    }
    HRESULT hr = E_FAIL;
    int showEnum = -1;
    if (GetObjectShowStatus(iObj, hr, showEnum))
    {
        return (showEnum != (int)CATNoShowAttr && showEnum != (int)CATInvisibleAttr);
    }
    return true;
}

bool IsHierarchyVisible(const CATISpecObject_var& iObj, const CATISpecObject_var& iStopPart)
{
    if (iObj == NULL_var)
    {
        return false;
    }

    CATISpecObject_var cur = iObj;
    for (int depth = 0; depth < 64 && cur != NULL_var; ++depth)
    {
        if (!IsSingleObjectVisible((CATBaseUnknown*)cur))
        {
            return false;
        }
        if (iStopPart != NULL_var && cur == iStopPart)
        {
            break;
        }
        CATISpecObject* pFather = cur->GetFather();
        if (pFather == NULL)
        {
            break;
        }
        CATISpecObject_var nextFather = pFather;
        pFather->Release();
        pFather = NULL;

        if (nextFather == cur)
        {
            break;
        }
        cur = nextFather;
    }
    return true;
}

void ExtractEffectiveGraphicStyle(
    const CATISpecObject_var& iObj,
    const CATISpecObject_var& iStopPart,
    GraphicStyle& oStyle)
{
    oStyle = GraphicStyle();
    if (iObj == NULL_var)
    {
        return;
    }

    // First walk up the feature tree for explicit (S_OK) properties.
    CATISpecObject_var cur = iObj;
    for (int depth = 0; depth < 64 && cur != NULL_var; ++depth)
    {
        CATIVisProperties* pVis = NULL;
        if (SUCCEEDED(cur->QueryInterface(IID_CATIVisProperties, (void**)&pVis)) && pVis != NULL)
        {
            if (!oStyle.hasColor)
            {
                unsigned int r = 0, g = 0, b = 0;
                if (TryGetColorOnVis(pVis, CATVPMesh, true, r, g, b) ||
                    TryGetColorOnVis(pVis, CATVPAsm, true, r, g, b))
                {
                    oStyle.hasColor = true;
                    oStyle.r = r;
                    oStyle.g = g;
                    oStyle.b = b;
                }
            }

            if (!oStyle.hasOpacity)
            {
                unsigned int op = 255;
                if (TryGetOpacityOnVis(pVis, CATVPMesh, true, op) ||
                    TryGetOpacityOnVis(pVis, CATVPAsm, true, op))
                {
                    oStyle.hasOpacity = true;
                    oStyle.opacity = op;
                }
            }

            if (depth == 0)
            {
                CATVisGeomType edgeType = CATVPEdge;
                if (SUCCEEDED(pVis->IsGeomTypeDefined(edgeType)))
                {
                    CATVisPropertiesValues ev;
                    if (pVis->GetPropertiesAtt(ev, CATVPColor, CATVPEdge) == S_OK &&
                        SUCCEEDED(ev.GetColor(oStyle.er, oStyle.eg, oStyle.eb)))
                    {
                        oStyle.hasEdgeColor = true;
                    }
                    CATVisPropertiesValues ew;
                    if (pVis->GetPropertiesAtt(ew, CATVPWidth, CATVPEdge) == S_OK &&
                        SUCCEEDED(ew.GetWidth(oStyle.edgeWidth)))
                    {
                        oStyle.hasEdgeWidth = true;
                    }
                    CATVisPropertiesValues elt;
                    if (pVis->GetPropertiesAtt(elt, CATVPLineType, CATVPEdge) == S_OK &&
                        SUCCEEDED(elt.GetLineType(oStyle.edgeLineType)))
                    {
                        oStyle.hasEdgeLineType = true;
                    }
                }

                CATVisPropertiesValues lv;
                if (pVis->GetPropertiesAtt(lv, CATVPLayer, CATVPGlobalType) == S_OK &&
                    SUCCEEDED(lv.GetLayer(oStyle.layer)))
                {
                    oStyle.hasLayer = true;
                }
            }

            pVis->Release();
            pVis = NULL;
        }

        if (iStopPart != NULL_var && cur == iStopPart)
        {
            break;
        }
        CATISpecObject* pFather = cur->GetFather();
        if (pFather == NULL)
        {
            break;
        }
        CATISpecObject_var nextFather = pFather;
        pFather->Release();
        pFather = NULL;

        if (nextFather == cur)
        {
            break;
        }
        cur = nextFather;
    }

    // If no explicit color was set on the object or ancestors, check if the source object itself
    // has a non-standard-solid automatic color (for example Volumes/Surfaces often look light blue/yellow).
    // Wait: we only read S_AUTOMATIC on iObj if needed; wait - if we keep S_AUTOMATIC as fallback only
    // when it differs? Actually in CATIA, copying a Volume/Surface whose color is S_AUTOMATIC with
    // CATMmrInterPartCopy(ALL_GP) or preserving 3D appearance keeps its visible color if S_OK was set,
    // and leaves CATIA default if S_AUTOMATIC. Wait: if a Volume had S_OK color it is already captured above!
}

void ApplyBodyAndDatumGraphicStyle(
    const CATISpecObject_var& iDstBody,
    const CATISpecObject_var& iDstDatumSolid,
    const GraphicStyle& iStyle)
{
    // Set Show attribute explicitly on both Body and Datum Solid
    CATISpecObject_var targets[2] = { iDstBody, iDstDatumSolid };
    for (int i = 0; i < 2; ++i)
    {
        if (targets[i] == NULL_var) continue;
        CATIVisProperties* pVis = NULL;
        if (SUCCEEDED(targets[i]->QueryInterface(IID_CATIVisProperties, (void**)&pVis)) && pVis != NULL)
        {
            CATVisPropertiesValues showVal;
            showVal.SetShowAttr(CATShowAttr);
            pVis->SetPropertiesAtt(showVal, CATVPShow, CATVPGlobalType);

            if (iStyle.hasLayer)
            {
                CATVisPropertiesValues layerVal;
                layerVal.SetLayer(iStyle.layer);
                pVis->SetPropertiesAtt(layerVal, CATVPLayer, CATVPGlobalType);
            }
            pVis->Release();
            pVis = NULL;
        }
    }

    if (iDstBody != NULL_var)
    {
        CATIVisProperties* pBodyVis = NULL;
        if (SUCCEEDED(iDstBody->QueryInterface(IID_CATIVisProperties, (void**)&pBodyVis)) && pBodyVis != NULL)
        {
            CATVisGeomType asmType = CATVPAsm;
            if (SUCCEEDED(pBodyVis->IsGeomTypeDefined(asmType)))
            {
                if (iStyle.hasColor)
                {
                    CATVisPropertiesValues colVal;
                    colVal.SetColor(iStyle.r, iStyle.g, iStyle.b, 0);
                    pBodyVis->SetPropertiesAtt(colVal, CATVPColor, CATVPAsm);
                }
                if (iStyle.hasOpacity)
                {
                    CATVisPropertiesValues opVal;
                    opVal.SetOpacity(iStyle.opacity, 0);
                    pBodyVis->SetPropertiesAtt(opVal, CATVPOpacity, CATVPAsm);
                }
            }
            pBodyVis->Release();
            pBodyVis = NULL;
        }
    }

    if (iDstDatumSolid != NULL_var)
    {
        CATIVisProperties* pSolidVis = NULL;
        if (SUCCEEDED(iDstDatumSolid->QueryInterface(IID_CATIVisProperties, (void**)&pSolidVis)) && pSolidVis != NULL)
        {
            CATVisGeomType meshType = CATVPMesh;
            if (SUCCEEDED(pSolidVis->IsGeomTypeDefined(meshType)))
            {
                if (iStyle.hasColor)
                {
                    CATVisPropertiesValues colVal;
                    colVal.SetColor(iStyle.r, iStyle.g, iStyle.b, 0);
                    pSolidVis->SetPropertiesAtt(colVal, CATVPColor, CATVPMesh);
                }
                if (iStyle.hasOpacity)
                {
                    CATVisPropertiesValues opVal;
                    opVal.SetOpacity(iStyle.opacity, 0);
                    pSolidVis->SetPropertiesAtt(opVal, CATVPOpacity, CATVPMesh);
                }
            }

            CATVisGeomType edgeType = CATVPEdge;
            if (SUCCEEDED(pSolidVis->IsGeomTypeDefined(edgeType)))
            {
                if (iStyle.hasEdgeColor)
                {
                    CATVisPropertiesValues ecVal;
                    ecVal.SetColor(iStyle.er, iStyle.eg, iStyle.eb, 0);
                    pSolidVis->SetPropertiesAtt(ecVal, CATVPColor, CATVPEdge);
                }
                if (iStyle.hasEdgeWidth)
                {
                    CATVisPropertiesValues ewVal;
                    ewVal.SetWidth(iStyle.edgeWidth, 0);
                    pSolidVis->SetPropertiesAtt(ewVal, CATVPWidth, CATVPEdge);
                }
                if (iStyle.hasEdgeLineType)
                {
                    CATVisPropertiesValues elVal;
                    elVal.SetLineType(iStyle.edgeLineType, 0);
                    pSolidVis->SetPropertiesAtt(elVal, CATVPLineType, CATVPEdge);
                }
            }

            pSolidVis->Release();
            pSolidVis = NULL;
        }
    }
}

struct FaceGeomSig
{
    CATCell* cell;
    double xmin, xmax, ymin, ymax, zmin, zmax;
    double cx, cy, cz;
    double area;
    bool valid;

    FaceGeomSig()
        : cell(NULL), xmin(0), xmax(0), ymin(0), ymax(0), zmin(0), zmax(0),
          cx(0), cy(0), cz(0), area(0), valid(false)
    {}
};

static FaceGeomSig ComputeFaceSig(CATTopData* iTopData, CATFace* iFace)
{
    FaceGeomSig sig;
    sig.cell = iFace;
    if (iFace == NULL) return sig;

    const CATMathBox* pBox = iFace->Get3DBoundingBox();
    if (pBox != NULL && !pBox->IsEmpty())
    {
        pBox->GetLimits(sig.xmin, sig.xmax, sig.ymin, sig.ymax, sig.zmin, sig.zmax);
        CATMathPoint boxCenter;
        pBox->GetBoxCenter(boxCenter);
        sig.cx = boxCenter.GetX();
        sig.cy = boxCenter.GetY();
        sig.cz = boxCenter.GetZ();
        sig.valid = true;
    }

    if (iTopData != NULL)
    {
        CATDynMassProperties3D* pMass = ::CATDynCreateMassProperties3D(iTopData, iFace);
        if (pMass != NULL)
        {
            sig.area = pMass->GetWetArea();
            CATMathPoint cog = pMass->GetCenterOfGravity();
            sig.cx = cog.GetX();
            sig.cy = cog.GetY();
            sig.cz = cog.GetZ();
            sig.valid = true;
            delete pMass;
            pMass = NULL;
        }
    }
    return sig;
}

void BuildGeometricFaceMap(
    CATTopData* iTopData,
    CATBody* iSrcBody,
    CATBody* iDstBody,
    std::vector<std::pair<CATCell*, CATCell*> >& ioFacePairs)
{
    if (iSrcBody == NULL || iDstBody == NULL)
    {
        return;
    }

    CATLISTP(CATCell) srcFaces;
    CATLISTP(CATCell) dstFaces;
    iSrcBody->GetAllCells(srcFaces, 2);
    iDstBody->GetAllCells(dstFaces, 2);

    std::set<CATCell*> mappedSrc;
    std::set<CATCell*> mappedDst;
    for (size_t i = 0; i < ioFacePairs.size(); ++i)
    {
        if (ioFacePairs[i].first != NULL) mappedSrc.insert(ioFacePairs[i].first);
        if (ioFacePairs[i].second != NULL) mappedDst.insert(ioFacePairs[i].second);
    }

    std::vector<FaceGeomSig> unmappedSrc;
    for (int i = 1; i <= srcFaces.Size(); ++i)
    {
        CATCell* pCell = srcFaces[i];
        if (pCell == NULL || mappedSrc.count(pCell) > 0) continue;
        CATFace* pFace = NULL;
        if (SUCCEEDED(pCell->QueryInterface(IID_CATFace, (void**)&pFace)) && pFace != NULL)
        {
            FaceGeomSig s = ComputeFaceSig(iTopData, pFace);
            if (s.valid) unmappedSrc.push_back(s);
            pFace->Release();
        }
    }

    std::vector<FaceGeomSig> unmappedDst;
    for (int i = 1; i <= dstFaces.Size(); ++i)
    {
        CATCell* pCell = dstFaces[i];
        if (pCell == NULL || mappedDst.count(pCell) > 0) continue;
        CATFace* pFace = NULL;
        if (SUCCEEDED(pCell->QueryInterface(IID_CATFace, (void**)&pFace)) && pFace != NULL)
        {
            FaceGeomSig s = ComputeFaceSig(iTopData, pFace);
            if (s.valid) unmappedDst.push_back(s);
            pFace->Release();
        }
    }

    if (unmappedSrc.empty() || unmappedDst.empty())
    {
        return;
    }

    std::vector<bool> srcUsed(unmappedSrc.size(), false);
    for (size_t d = 0; d < unmappedDst.size(); ++d)
    {
        const FaceGeomSig& ds = unmappedDst[d];
        int bestIdx = -1;
        double bestScore = 1e30;

        for (size_t s = 0; s < unmappedSrc.size(); ++s)
        {
            if (srcUsed[s]) continue;
            const FaceGeomSig& ss = unmappedSrc[s];

            double dx = ds.cx - ss.cx;
            double dy = ds.cy - ss.cy;
            double dz = ds.cz - ss.cz;
            double cogDist = sqrt(dx * dx + dy * dy + dz * dz);

            double b1 = fabs(ds.xmin - ss.xmin);
            double b2 = fabs(ds.xmax - ss.xmax);
            double b3 = fabs(ds.ymin - ss.ymin);
            double b4 = fabs(ds.ymax - ss.ymax);
            double b5 = fabs(ds.zmin - ss.zmin);
            double b6 = fabs(ds.zmax - ss.zmax);
            double maxBoxDiff = b1;
            if (b2 > maxBoxDiff) maxBoxDiff = b2;
            if (b3 > maxBoxDiff) maxBoxDiff = b3;
            if (b4 > maxBoxDiff) maxBoxDiff = b4;
            if (b5 > maxBoxDiff) maxBoxDiff = b5;
            if (b6 > maxBoxDiff) maxBoxDiff = b6;

            double sx = ds.xmax - ds.xmin;
            double sy = ds.ymax - ds.ymin;
            double sz = ds.zmax - ds.zmin;
            double diag = sqrt(sx * sx + sy * sy + sz * sz);
            double tol = diag * 0.005;
            if (tol < 0.1) tol = 0.1;
            if (tol > 2.0) tol = 2.0;

            if (cogDist > tol || maxBoxDiff > tol)
            {
                continue;
            }

            double maxArea = (ds.area > ss.area) ? ds.area : ss.area;
            double areaRel = (maxArea > 1e-6) ? (fabs(ds.area - ss.area) / maxArea) : 0.0;
            if (areaRel > 0.05)
            {
                continue;
            }

            double score = cogDist + maxBoxDiff + areaRel * 10.0;
            if (score < bestScore)
            {
                bestScore = score;
                bestIdx = (int)s;
            }
        }

        if (bestIdx >= 0)
        {
            srcUsed[bestIdx] = true;
            ioFacePairs.push_back(std::make_pair(unmappedSrc[bestIdx].cell, ds.cell));
        }
    }
}

void TransferFaceGraphicProperties(
    CATTopData* iTopData,
    const CATIGeometricalElement_var& iSrcGeomElem,
    CATBody* iSrcBody,
    const CATIGeometricalElement_var& iDstGeomElem,
    CATBody* iDstBody,
    const std::vector<std::pair<CATCell*, CATCell*> >& iInitialPairs,
    const GraphicStyle& iBaseStyle)
{
    if (iSrcGeomElem == NULL_var || iDstGeomElem == NULL_var || iSrcBody == NULL || iDstBody == NULL)
    {
        return;
    }

    // Fast-path: inspect source faces first; skip geometric matching if no face has overrides.
    CATLISTP(CATCell) allSrcCells;
    iSrcBody->GetAllCells(allSrcCells, 2);
    if (allSrcCells.Size() == 0)
    {
        return;
    }

    std::vector<CATCell*> allSrcCellVec;
    CATLISTV(CATBaseUnknown_var) allSrcBReps;
    for (int i = 1; i <= allSrcCells.Size(); ++i)
    {
        CATCell* pCell = allSrcCells[i];
        if (pCell == NULL) continue;
        CATIBRepAccess_var spBRep = ::CATBRepDecode(CATCell_var(pCell), iSrcGeomElem);
        if (spBRep != NULL_var)
        {
            allSrcCellVec.push_back(pCell);
            allSrcBReps.Append(spBRep);
        }
    }

    int srcCount = allSrcBReps.Size();
    if (srcCount == 0)
    {
        return;
    }

    CATListOfInt srcR, srcG, srcB, srcOp;
    HRESULT hrBatch = CATMmrApplicativeAttributes::GetColorsAndOpacityOnFaces(
        allSrcBReps, srcR, srcG, srcB, srcOp);
    bool hasBatch = (hrBatch != E_FAIL && srcR.Size() == srcCount && srcG.Size() == srcCount &&
                     srcB.Size() == srcCount && srcOp.Size() == srcCount);

    struct SrcFaceOverride
    {
        bool copyColor;
        unsigned int r, g, b;
        bool copyOpacity;
        unsigned int opacity;
        SrcFaceOverride() : copyColor(false), r(0), g(0), b(0), copyOpacity(false), opacity(255) {}
    };

    std::set<CATCell*> overriddenSrcCells;
    std::vector<std::pair<CATCell*, SrcFaceOverride> > srcOverrides;

    for (int i = 1; i <= srcCount; ++i)
    {
        CATBaseUnknown_var spSrcUnk = allSrcBReps[i];
        CATCell* pSrcCell = allSrcCellVec[(size_t)(i - 1)];
        if (spSrcUnk == NULL_var || pSrcCell == NULL) continue;

        SrcFaceOverride ovr;
        CATIVisProperties* pSrcVis = NULL;
        HRESULT hrColorVis = E_FAIL;
        HRESULT hrOpVis = E_FAIL;
        unsigned int opVal = 255;

        if (SUCCEEDED(spSrcUnk->QueryInterface(IID_CATIVisProperties, (void**)&pSrcVis)) && pSrcVis != NULL)
        {
            CATVisPropertiesValues cv;
            hrColorVis = pSrcVis->GetPropertiesAtt(cv, CATVPColor, CATVPMesh);
            if (hrColorVis == S_OK && SUCCEEDED(cv.GetColor(ovr.r, ovr.g, ovr.b)))
            {
                ovr.copyColor = true;
            }

            CATVisPropertiesValues ov;
            hrOpVis = pSrcVis->GetPropertiesAtt(ov, CATVPOpacity, CATVPMesh);
            if (hrOpVis == S_OK)
            {
                ov.GetOpacity(opVal);
            }

            pSrcVis->Release();
            pSrcVis = NULL;
        }

        if (!ovr.copyColor && hasBatch)
        {
            int br = srcR[i];
            int bg = srcG[i];
            int bb = srcB[i];
            if (br >= 0 && bg >= 0 && bb >= 0 && iBaseStyle.hasColor)
            {
                if ((unsigned int)br != iBaseStyle.r ||
                    (unsigned int)bg != iBaseStyle.g ||
                    (unsigned int)bb != iBaseStyle.b)
                {
                    ovr.copyColor = true;
                    ovr.r = (unsigned int)br;
                    ovr.g = (unsigned int)bg;
                    ovr.b = (unsigned int)bb;
                }
            }
        }

        if (hrOpVis == S_OK)
        {
            ovr.copyOpacity = true;
            ovr.opacity = opVal;
        }
        else if (hasBatch && srcOp[i] >= 0 && iBaseStyle.hasOpacity && (unsigned int)srcOp[i] != iBaseStyle.opacity)
        {
            ovr.copyOpacity = true;
            ovr.opacity = (unsigned int)srcOp[i];
        }

        if (ovr.copyColor || ovr.copyOpacity)
        {
            overriddenSrcCells.insert(pSrcCell);
            srcOverrides.push_back(std::make_pair(pSrcCell, ovr));
        }
    }

    if (overriddenSrcCells.empty())
    {
        return;
    }

    std::vector<std::pair<CATCell*, CATCell*> > facePairs = iInitialPairs;
    std::set<CATCell*> initialMappedSrc;
    for (size_t i = 0; i < facePairs.size(); ++i)
    {
        if (facePairs[i].first != NULL && facePairs[i].second != NULL)
        {
            initialMappedSrc.insert(facePairs[i].first);
        }
    }

    bool needGeomFallback = false;
    for (std::set<CATCell*>::const_iterator it = overriddenSrcCells.begin(); it != overriddenSrcCells.end(); ++it)
    {
        if (initialMappedSrc.count(*it) == 0)
        {
            needGeomFallback = true;
            break;
        }
    }

    if (needGeomFallback)
    {
        BuildGeometricFaceMap(iTopData, iSrcBody, iDstBody, facePairs);
    }

    if (facePairs.empty())
    {
        UnicodeUtil::LogWarn("Face graphic transfer skipped: no mapped faces for overridden source faces");
        return;
    }

    // Build lookup from srcCell -> dstCell
    std::set<CATCell*> seenDst;
    std::set<CATCell*> mappedSrc;
    CATLISTV(CATBaseUnknown_var) dstColorTargets;
    CATListOfInt outR, outG, outB;

    for (size_t i = 0; i < facePairs.size(); ++i)
    {
        CATCell* pSrcCell = facePairs[i].first;
        CATCell* pDstCell = facePairs[i].second;
        if (pSrcCell == NULL || pDstCell == NULL) continue;
        if (overriddenSrcCells.count(pSrcCell) == 0) continue;
        if (seenDst.count(pDstCell) > 0) continue;

        const SrcFaceOverride* pOvr = NULL;
        for (size_t j = 0; j < srcOverrides.size(); ++j)
        {
            if (srcOverrides[j].first == pSrcCell)
            {
                pOvr = &srcOverrides[j].second;
                break;
            }
        }
        if (pOvr == NULL) continue;

        CATIBRepAccess_var spDstBRep = ::CATBRepDecode(CATCell_var(pDstCell), iDstGeomElem);
        if (spDstBRep == NULL_var) continue;

        seenDst.insert(pDstCell);
        mappedSrc.insert(pSrcCell);

        if (pOvr->copyColor)
        {
            dstColorTargets.Append(spDstBRep);
            outR.Append((int)pOvr->r);
            outG.Append((int)pOvr->g);
            outB.Append((int)pOvr->b);
        }

        if (pOvr->copyOpacity)
        {
            CATIVisProperties* pDstVis = NULL;
            if (SUCCEEDED(spDstBRep->QueryInterface(IID_CATIVisProperties, (void**)&pDstVis)) && pDstVis != NULL)
            {
                CATVisPropertiesValues setOp;
                setOp.SetOpacity(pOvr->opacity, 0);
                pDstVis->SetPropertiesAtt(setOp, CATVPOpacity, CATVPMesh);
                pDstVis->Release();
                pDstVis = NULL;
            }
        }
    }

    if (mappedSrc.size() < overriddenSrcCells.size())
    {
        char wbuf[128];
        _snprintf_s(wbuf, sizeof(wbuf), _TRUNCATE,
            "Face graphic transfer: %u of %u overridden source faces could not be mapped",
            (unsigned)(overriddenSrcCells.size() - mappedSrc.size()),
            (unsigned)overriddenSrcCells.size());
        UnicodeUtil::LogWarn(wbuf);
    }

    if (dstColorTargets.Size() > 0)
    {
        CATListOfInt failedIdx;
        HRESULT hrSet = CATMmrApplicativeAttributes::SetColorsOnFaces(
            dstColorTargets, outR, outG, outB, failedIdx);

        if (hrSet == E_FAIL)
        {
            for (int k = 1; k <= dstColorTargets.Size(); ++k)
            {
                CATBaseUnknown_var spDstUnk = dstColorTargets[k];
                CATIVisProperties* pDstVis = NULL;
                if (spDstUnk != NULL_var &&
                    SUCCEEDED(spDstUnk->QueryInterface(IID_CATIVisProperties, (void**)&pDstVis)) &&
                    pDstVis != NULL)
                {
                    CATVisPropertiesValues colVal;
                    colVal.SetColor((unsigned int)outR[k], (unsigned int)outG[k], (unsigned int)outB[k], 0);
                    pDstVis->SetPropertiesAtt(colVal, CATVPColor, CATVPMesh);
                    pDstVis->Release();
                    pDstVis = NULL;
                }
            }
        }
        else if (hrSet == S_FALSE && failedIdx.Size() > 0)
        {
            for (int f = 1; f <= failedIdx.Size(); ++f)
            {
                int idx = failedIdx[f];
                if (idx >= 1 && idx <= dstColorTargets.Size())
                {
                    CATBaseUnknown_var spDstUnk = dstColorTargets[idx];
                    CATIVisProperties* pDstVis = NULL;
                    if (spDstUnk != NULL_var &&
                        SUCCEEDED(spDstUnk->QueryInterface(IID_CATIVisProperties, (void**)&pDstVis)) &&
                        pDstVis != NULL)
                    {
                        CATVisPropertiesValues colVal;
                        colVal.SetColor((unsigned int)outR[idx], (unsigned int)outG[idx], (unsigned int)outB[idx], 0);
                        pDstVis->SetPropertiesAtt(colVal, CATVPColor, CATVPMesh);
                        pDstVis->Release();
                        pDstVis = NULL;
                    }
                }
            }
        }
    }
}

FaceColorSummary InspectFaceGraphicProperties(
    const CATIGeometricalElement_var& iGeomElem,
    CATBody* iBody)
{
    FaceColorSummary summary;
    if (iGeomElem == NULL_var || iBody == NULL)
    {
        return summary;
    }

    CATLISTP(CATCell) faces;
    iBody->GetAllCells(faces, 2);
    summary.totalFaces = faces.Size();

    for (int i = 1; i <= faces.Size(); ++i)
    {
        CATCell* pCell = faces[i];
        if (pCell == NULL) continue;
        CATIBRepAccess_var spBRep = ::CATBRepDecode(CATCell_var(pCell), iGeomElem);
        if (spBRep == NULL_var) continue;

        CATIVisProperties* pVis = NULL;
        if (SUCCEEDED(spBRep->QueryInterface(IID_CATIVisProperties, (void**)&pVis)) && pVis != NULL)
        {
            unsigned int fr = 0, fg = 0, fb = 0, fop = 255;
            bool hasFc = false;
            bool hasFo = false;
            CATVisPropertiesValues cv;
            if (pVis->GetPropertiesAtt(cv, CATVPColor, CATVPMesh) == S_OK &&
                SUCCEEDED(cv.GetColor(fr, fg, fb)))
            {
                summary.explicitColoredFaces++;
                hasFc = true;
            }
            CATVisPropertiesValues ov;
            if (pVis->GetPropertiesAtt(ov, CATVPOpacity, CATVPMesh) == S_OK &&
                SUCCEEDED(ov.GetOpacity(fop)))
            {
                summary.customOpacityFaces++;
                hasFo = true;
            }
            if (hasFc || hasFo)
            {
                char buf[128];
                _snprintf_s(buf, sizeof(buf), _TRUNCATE, "face#%d(rgb=%s,op=%s)",
                    i,
                    hasFc ? "" : "auto",
                    hasFo ? "" : "auto");
                std::string entry = buf;
                if (hasFc)
                {
                    char cbuf[64];
                    _snprintf_s(cbuf, sizeof(cbuf), _TRUNCATE, "%u,%u,%u", fr, fg, fb);
                    size_t p = entry.find("rgb=");
                    if (p != std::string::npos) entry.insert(p + 4, cbuf);
                }
                if (hasFo)
                {
                    char obuf[32];
                    _snprintf_s(obuf, sizeof(obuf), _TRUNCATE, "%u", fop);
                    size_t p = entry.find("op=");
                    if (p != std::string::npos) entry.insert(p + 3, obuf);
                }
                summary.faceColorDetails.push_back(entry);
            }
            pVis->Release();
            pVis = NULL;
        }
    }
    return summary;
}
