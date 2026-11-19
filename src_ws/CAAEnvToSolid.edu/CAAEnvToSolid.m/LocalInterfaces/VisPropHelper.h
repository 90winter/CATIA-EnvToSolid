#ifndef VIS_PROP_HELPER_H
#define VIS_PROP_HELPER_H

#include <string>
#include <utility>
#include <vector>

#include "CATBaseUnknown.h"
#include "CATISpecObject.h"
#include "CATIGeometricalElement.h"

class CATBody;
class CATCell;
class CATTopData;

struct GraphicStyle
{
    bool hasColor;
    unsigned int r;
    unsigned int g;
    unsigned int b;

    bool hasOpacity;
    unsigned int opacity;

    bool hasEdgeColor;
    unsigned int er;
    unsigned int eg;
    unsigned int eb;

    bool hasEdgeWidth;
    unsigned int edgeWidth;

    bool hasEdgeLineType;
    unsigned int edgeLineType;

    bool hasLayer;
    unsigned int layer;

    GraphicStyle()
        : hasColor(false), r(0), g(0), b(0),
          hasOpacity(false), opacity(255),
          hasEdgeColor(false), er(0), eg(0), eb(0),
          hasEdgeWidth(false), edgeWidth(1),
          hasEdgeLineType(false), edgeLineType(1),
          hasLayer(false), layer(0)
    {}
};

struct FaceColorSummary
{
    int totalFaces;
    int explicitColoredFaces;
    int customOpacityFaces;
    std::vector<std::string> faceColorDetails;

    FaceColorSummary()
        : totalFaces(0), explicitColoredFaces(0), customOpacityFaces(0)
    {}
};

bool GetObjectShowStatus(CATBaseUnknown* iObj, HRESULT& oHr, int& oShowEnum);

bool IsSingleObjectVisible(CATBaseUnknown* iObj);

bool IsHierarchyVisible(const CATISpecObject_var& iObj, const CATISpecObject_var& iStopPart);

void ExtractEffectiveGraphicStyle(
    const CATISpecObject_var& iObj,
    const CATISpecObject_var& iStopPart,
    GraphicStyle& oStyle);

void ApplyBodyAndDatumGraphicStyle(
    const CATISpecObject_var& iDstBody,
    const CATISpecObject_var& iDstDatumSolid,
    const GraphicStyle& iStyle);

void BuildGeometricFaceMap(
    CATTopData* iTopData,
    CATBody* iSrcBody,
    CATBody* iDstBody,
    std::vector<std::pair<CATCell*, CATCell*> >& ioFacePairs);

void TransferFaceGraphicProperties(
    CATTopData* iTopData,
    const CATIGeometricalElement_var& iSrcGeomElem,
    CATBody* iSrcBody,
    const CATIGeometricalElement_var& iDstGeomElem,
    CATBody* iDstBody,
    const std::vector<std::pair<CATCell*, CATCell*> >& iInitialPairs,
    const GraphicStyle& iBaseStyle);

FaceColorSummary InspectFaceGraphicProperties(
    const CATIGeometricalElement_var& iGeomElem,
    CATBody* iBody);

#endif
