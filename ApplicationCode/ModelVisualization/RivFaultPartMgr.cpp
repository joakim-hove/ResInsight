/////////////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) Statoil ASA, Ceetron Solutions AS
// 
//  ResInsight is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
// 
//  ResInsight is distributed in the hope that it will be useful, but WITHOUT ANY
//  WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.
// 
//  See the GNU General Public License at <http://www.gnu.org/licenses/gpl.html> 
//  for more details.
//
/////////////////////////////////////////////////////////////////////////////////

#include "RivFaultPartMgr.h"

#include "cvfPart.h"
#include "cafEffectGenerator.h"
#include "cvfStructGrid.h"
#include "cvfDrawableGeo.h"
#include "cvfModelBasicList.h"
#include "RivCellEdgeEffectGenerator.h"
#include "RimReservoirView.h"
#include "RimResultSlot.h"
#include "RimCellEdgeResultSlot.h"
#include "RigCaseCellResultsData.h"
#include "RigCaseData.h"
#include "RiaApplication.h"
#include "RiaPreferences.h"

#include "RimCase.h"
#include "RimWellCollection.h"
#include "cafPdmFieldCvfMat4d.h"
#include "cafPdmFieldCvfColor.h"
#include "RimCellRangeFilterCollection.h"
#include "RimCellPropertyFilterCollection.h"
#include "Rim3dOverlayInfoConfig.h"
#include "RimReservoirCellResultsCacher.h"
#include "cvfDrawableText.h"
#include "cvfqtUtils.h"
#include "cvfPrimitiveSetIndexedUInt.h"
#include "cvfPrimitiveSetDirect.h"
#include "RivGridPartMgr.h"
#include "cvfRenderStateDepth.h"
#include "RivSourceInfo.h"





//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
RivFaultPartMgr::RivFaultPartMgr(const RigGridBase* grid, const RimFaultCollection* rimFaultCollection, const RimFault* rimFault)
    :   m_grid(grid),
        m_rimFaultCollection(rimFaultCollection),
        m_rimFault(rimFault),
        m_opacityLevel(1.0f),
        m_defaultColor(cvf::Color3::WHITE)
{
    cvf::ref< cvf::Array<size_t> > connIdxes = new cvf::Array<size_t>;
    connIdxes->assign(rimFault->faultGeometry()->connectionIndices());

    m_nativeFaultGenerator = new RivFaultGeometryGenerator(grid, rimFault->faultGeometry(), true);
    m_oppositeFaultGenerator = new  RivFaultGeometryGenerator(grid, rimFault->faultGeometry(), false);

    m_NNCGenerator = new RivNNCGeometryGenerator(grid->mainGrid()->nncData(), grid->mainGrid()->displayModelOffset(), connIdxes.p());

    m_nativeFaultFacesTextureCoords = new cvf::Vec2fArray;
    m_oppositeFaultFacesTextureCoords = new cvf::Vec2fArray;
    m_NNCTextureCoords = new cvf::Vec2fArray;
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivFaultPartMgr::setCellVisibility(cvf::UByteArray* cellVisibilities)
{
    m_nativeFaultGenerator->setCellVisibility(cellVisibilities);
    m_oppositeFaultGenerator->setCellVisibility(cellVisibilities);
    m_NNCGenerator->setCellVisibility(cellVisibilities, m_grid.p());

    generatePartGeometry();
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivFaultPartMgr::applySingleColorEffect()
{
    m_defaultColor = m_rimFault->faultColor();
    this->updatePartEffect();
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivFaultPartMgr::updateCellResultColor(size_t timeStepIndex, RimResultSlot* cellResultSlot)
{
    CVF_ASSERT(cellResultSlot);

    updateNNCColors(cellResultSlot);

    size_t scalarSetIndex = cellResultSlot->gridScalarIndex();
    const cvf::ScalarMapper* mapper = cellResultSlot->legendConfig()->scalarMapper();

    // If the result is static, only read that.
    size_t resTimeStepIdx = timeStepIndex;
    if (cellResultSlot->hasStaticResult()) resTimeStepIdx = 0;

    RifReaderInterface::PorosityModelResultType porosityModel = RigCaseCellResultsData::convertFromProjectModelPorosityModel(cellResultSlot->porosityModel());

    RigCaseData* eclipseCase = cellResultSlot->reservoirView()->eclipseCase()->reservoirData();
    cvf::ref<cvf::StructGridScalarDataAccess> dataAccessObject = eclipseCase->dataAccessObject(m_grid.p(), porosityModel, resTimeStepIdx, scalarSetIndex);

    if (dataAccessObject.isNull()) return;

    // Faults
    if (m_nativeFaultFaces.notNull())
    {
        if (cellResultSlot->resultVariable().compare(RimDefines::combinedTransmissibilityResultName(), Qt::CaseInsensitive) == 0)
        {
            const std::vector<cvf::StructGridInterface::FaceType>& quadsToFaceTypes = m_nativeFaultGenerator->quadToFace();
            const std::vector<size_t>& quadsToGridCells = m_nativeFaultGenerator->quadToGridCellIndices();
            cvf::Vec2fArray* textureCoords = m_nativeFaultFacesTextureCoords.p();

            RivTransmissibilityColorMapper::updateCombinedTransmissibilityTextureCoordinates(cellResultSlot, m_grid.p(), textureCoords, quadsToFaceTypes, quadsToGridCells);
        }
        else
        {
            m_nativeFaultGenerator->textureCoordinates(m_nativeFaultFacesTextureCoords.p(), dataAccessObject.p(), mapper);
        }

        if (m_opacityLevel < 1.0f )
        {
            const std::vector<cvf::ubyte>& isWellPipeVisible      = cellResultSlot->reservoirView()->wellCollection()->isWellPipesVisible(timeStepIndex);
            cvf::ref<cvf::UIntArray>       gridCellToWellindexMap = eclipseCase->gridCellToWellIndex(m_grid->gridIndex());
            const std::vector<size_t>&  quadsToGridCells = m_nativeFaultGenerator->quadToGridCellIndices();

            for(size_t i = 0; i < m_nativeFaultFacesTextureCoords->size(); ++i)
            {
                if ((*m_nativeFaultFacesTextureCoords)[i].y() == 1.0f) continue; // Do not touch undefined values

                size_t quadIdx = i/4;
                size_t cellIndex = quadsToGridCells[quadIdx];
                cvf::uint wellIndex = gridCellToWellindexMap->get(cellIndex);
                if (wellIndex != cvf::UNDEFINED_UINT)
                {
                    if ( !isWellPipeVisible[wellIndex]) 
                    {
                        (*m_nativeFaultFacesTextureCoords)[i].y() = 0; // Set the Y texture coordinate to the opaque line in the texture
                    }
                }
            }
        }

        cvf::DrawableGeo* dg = dynamic_cast<cvf::DrawableGeo*>(m_nativeFaultFaces->drawable());
        if (dg) dg->setTextureCoordArray(m_nativeFaultFacesTextureCoords.p());

        cvf::ref<cvf::Effect> scalarEffect = cellResultEffect(mapper, caf::PO_1);
        m_nativeFaultFaces->setEffect(scalarEffect.p());
    }


    if (m_oppositeFaultFaces.notNull())
    {
        if (cellResultSlot->resultVariable().compare(RimDefines::combinedTransmissibilityResultName(), Qt::CaseInsensitive) == 0)
        {
            const std::vector<cvf::StructGridInterface::FaceType>& quadsToFaceTypes = m_oppositeFaultGenerator->quadToFace();
            const std::vector<size_t>& quadsToGridCells = m_oppositeFaultGenerator->quadToGridCellIndices();
            cvf::Vec2fArray* textureCoords = m_oppositeFaultFacesTextureCoords.p();

            RivTransmissibilityColorMapper::updateCombinedTransmissibilityTextureCoordinates(cellResultSlot, m_grid.p(), textureCoords, quadsToFaceTypes, quadsToGridCells);
        }
        else
        {
            m_oppositeFaultGenerator->textureCoordinates(m_oppositeFaultFacesTextureCoords.p(), dataAccessObject.p(), mapper);
        }

        if (m_opacityLevel < 1.0f )
        {
            const std::vector<cvf::ubyte>& isWellPipeVisible      = cellResultSlot->reservoirView()->wellCollection()->isWellPipesVisible(timeStepIndex);
            cvf::ref<cvf::UIntArray>       gridCellToWellindexMap = eclipseCase->gridCellToWellIndex(m_grid->gridIndex());
            const std::vector<size_t>&  quadsToGridCells = m_oppositeFaultGenerator->quadToGridCellIndices();

            for(size_t i = 0; i < m_oppositeFaultFacesTextureCoords->size(); ++i)
            {
                if ((*m_oppositeFaultFacesTextureCoords)[i].y() == 1.0f) continue; // Do not touch undefined values

                size_t quadIdx = i/4;
                size_t cellIndex = quadsToGridCells[quadIdx];
                cvf::uint wellIndex = gridCellToWellindexMap->get(cellIndex);
                if (wellIndex != cvf::UNDEFINED_UINT)
                {
                    if ( !isWellPipeVisible[wellIndex]) 
                    {
                        (*m_oppositeFaultFacesTextureCoords)[i].y() = 0; // Set the Y texture coordinate to the opaque line in the texture
                    }
                }
            }
        }

        cvf::DrawableGeo* dg = dynamic_cast<cvf::DrawableGeo*>(m_oppositeFaultFaces->drawable());
        if (dg) dg->setTextureCoordArray(m_oppositeFaultFacesTextureCoords.p());

        // Use a different offset than native fault faces to avoid z-fighting
        cvf::ref<cvf::Effect> scalarEffect = cellResultEffect(mapper, caf::PO_2);

        m_oppositeFaultFaces->setEffect(scalarEffect.p());
    }

}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivFaultPartMgr::updateCellEdgeResultColor(size_t timeStepIndex, RimResultSlot* cellResultSlot, RimCellEdgeResultSlot* cellEdgeResultSlot)
{

}


//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivFaultPartMgr::generatePartGeometry()
{
    const int priFaultGeo = 1;
    const int priNncGeo = 2;
    const int priMesh = 3;

    bool useBufferObjects = true;
    // Surface geometry
    {
        cvf::ref<cvf::DrawableGeo> geo = m_nativeFaultGenerator->generateSurface();
        if (geo.notNull())
        {
            geo->computeNormals();

            if (useBufferObjects)
            {
                geo->setRenderMode(cvf::DrawableGeo::BUFFER_OBJECT);
            }

            cvf::ref<cvf::Part> part = new cvf::Part;
            part->setName("Grid " + cvf::String(static_cast<int>(m_grid->gridIndex())));
            part->setId(m_grid->gridIndex());       // !! For now, use grid index as part ID (needed for pick info)
            part->setDrawable(geo.p());

            // Set mapping from triangle face index to cell index
            cvf::ref<RivSourceInfo> si = new RivSourceInfo;
            si->m_cellIndices = m_nativeFaultGenerator->triangleToSourceGridCellMap().p();
            si->m_faceTypes = m_nativeFaultGenerator->triangleToFaceType().p();
            part->setSourceInfo(si.p());

            part->updateBoundingBox();
            part->setEnableMask(faultBit);
            part->setPriority(priFaultGeo);

            m_nativeFaultFaces = part;
        }
    }

    // Mesh geometry
    {
        cvf::ref<cvf::DrawableGeo> geoMesh = m_nativeFaultGenerator->createMeshDrawable();
        if (geoMesh.notNull())
        {
            if (useBufferObjects)
            {
                geoMesh->setRenderMode(cvf::DrawableGeo::BUFFER_OBJECT);
            }

            cvf::ref<cvf::Part> part = new cvf::Part;
            part->setName("Grid mesh" + cvf::String(static_cast<int>(m_grid->gridIndex())));
            part->setDrawable(geoMesh.p());

            part->updateBoundingBox();
            part->setEnableMask(meshFaultBit);
            part->setPriority(priMesh);

            m_nativeFaultGridLines = part;
        }
    }


    // Surface geometry
    {
        cvf::ref<cvf::DrawableGeo> geo = m_oppositeFaultGenerator->generateSurface();
        if (geo.notNull())
        {
            geo->computeNormals();

            if (useBufferObjects)
            {
                geo->setRenderMode(cvf::DrawableGeo::BUFFER_OBJECT);
            }

            cvf::ref<cvf::Part> part = new cvf::Part;
            part->setName("Grid " + cvf::String(static_cast<int>(m_grid->gridIndex())));
            part->setId(m_grid->gridIndex());       // !! For now, use grid index as part ID (needed for pick info)
            part->setDrawable(geo.p());

            // Set mapping from triangle face index to cell index
            cvf::ref<RivSourceInfo> si = new RivSourceInfo;
            si->m_cellIndices = m_oppositeFaultGenerator->triangleToSourceGridCellMap().p();
            si->m_faceTypes = m_oppositeFaultGenerator->triangleToFaceType().p();
            part->setSourceInfo(si.p());

            part->updateBoundingBox();
            part->setEnableMask(faultBit);
            part->setPriority(priFaultGeo);

            m_oppositeFaultFaces = part;
        }
    }

    // Mesh geometry
    {
        cvf::ref<cvf::DrawableGeo> geoMesh = m_oppositeFaultGenerator->createMeshDrawable();
        if (geoMesh.notNull())
        {
            if (useBufferObjects)
            {
                geoMesh->setRenderMode(cvf::DrawableGeo::BUFFER_OBJECT);
            }

            cvf::ref<cvf::Part> part = new cvf::Part;
            part->setName("Grid mesh" + cvf::String(static_cast<int>(m_grid->gridIndex())));
            part->setDrawable(geoMesh.p());

            part->updateBoundingBox();
            part->setEnableMask(meshFaultBit);
            part->setPriority(priMesh);

            m_oppositeFaultGridLines = part;
        }
    }

    {
        cvf::ref<cvf::DrawableGeo> geo = m_NNCGenerator->generateSurface();
        if (geo.notNull())
        {
            geo->computeNormals();

            if (useBufferObjects)
            {
                geo->setRenderMode(cvf::DrawableGeo::BUFFER_OBJECT);
            }

            cvf::ref<cvf::Part> part = new cvf::Part;
            part->setName("NNC in Fault. Grid " + cvf::String(static_cast<int>(m_grid->gridIndex())));
            part->setId(m_grid->gridIndex());       // !! For now, use grid index as part ID (needed for pick info)
            part->setDrawable(geo.p());

            // Set mapping from triangle face index to cell index
            cvf::ref<RivSourceInfo> si = new RivSourceInfo;
            si->m_NNCIndices = m_NNCGenerator->triangleToNNCIndex().p();
            part->setSourceInfo(si.p());

            part->updateBoundingBox();
            part->setEnableMask(faultBit);
            part->setPriority(priNncGeo);

            m_NNCFaces = part;
        }
    }
    
    createLabelWithAnchorLine(m_nativeFaultFaces.p());

    updatePartEffect();
}


//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivFaultPartMgr::updatePartEffect()
{
    // Set default effect
    caf::SurfaceEffectGenerator geometryEffgen(m_defaultColor, caf::PO_1);
    geometryEffgen.setCullBackfaces(faceCullingMode());
  
    cvf::ref<cvf::Effect> geometryOnlyEffect = geometryEffgen.generateEffect();

    if (m_nativeFaultFaces.notNull())
    {
        m_nativeFaultFaces->setEffect(geometryOnlyEffect.p());
    }

    if (m_oppositeFaultFaces.notNull())
    {
        m_oppositeFaultFaces->setEffect(geometryOnlyEffect.p());
    }

    updateNNCColors(NULL);

    // Update mesh colors as well, in case of change
    RiaPreferences* prefs = RiaApplication::instance()->preferences();

    cvf::ref<cvf::Effect> eff;
    caf::MeshEffectGenerator faultEffGen(prefs->defaultFaultGridLineColors());
    eff = faultEffGen.generateEffect();

    if (m_nativeFaultGridLines.notNull())
    {
        m_nativeFaultGridLines->setEffect(eff.p());
    }

    if (m_oppositeFaultGridLines.notNull())
    {
        m_oppositeFaultGridLines->setEffect(eff.p());
    }

    if (m_opacityLevel < 1.0f)
    {
        // Must be fixed since currently fault drawing relies on internal priorities of the parts
        CVF_FAIL_MSG("Not implemented");

        // Set priority to make sure this transparent geometry are rendered last
        if (m_nativeFaultFaces.notNull()) m_nativeFaultFaces->setPriority(100);
        if (m_oppositeFaultFaces.notNull()) m_oppositeFaultFaces->setPriority(100);
        if (m_NNCFaces.notNull())  m_NNCFaces->setPriority(100);
    }
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivFaultPartMgr::createLabelWithAnchorLine(const cvf::Part* part)
{
    m_faultLabelPart = NULL;
    m_faultLabelLinePart = NULL;

    if (!part) return;

    cvf::BoundingBox bb = part->boundingBox();

    cvf::Vec3d bbTopCenter = bb.center();
    bbTopCenter.z() = bb.max().z();

    const cvf::DrawableGeo* geo = dynamic_cast<const cvf::DrawableGeo*>(part->drawable());

    // Find closest vertex to top of bounding box.
    // Will be recomputed when filter changes, to make sure the label is always visible
    // for any filter combination
    cvf::Vec3f faultVertexToAttachLabel = findClosestVertex(cvf::Vec3f(bbTopCenter), geo->vertexArray());

    cvf::Vec3f labelPosition = faultVertexToAttachLabel;
    labelPosition.z() += bb.extent().z() / 2;

    // Fault label
    {
        cvf::Font* standardFont = RiaApplication::instance()->standardFont();

        cvf::ref<cvf::DrawableText> drawableText = new cvf::DrawableText;
        drawableText->setFont(standardFont);
        drawableText->setCheckPosVisible(false);
        drawableText->setDrawBorder(false);
        drawableText->setDrawBackground(false);
        drawableText->setVerticalAlignment(cvf::TextDrawer::CENTER);
        
        cvf::Color3f defWellLabelColor = RiaApplication::instance()->preferences()->defaultWellLabelColor();
        {
            std::vector<RimFaultCollection*> parentObjects;
            m_rimFault->parentObjectsOfType(parentObjects);

            if (parentObjects.size() > 0)
            {
                defWellLabelColor = parentObjects[0]->faultLabelColor();;
            }
        }

        drawableText->setTextColor(defWellLabelColor);

        cvf::String cvfString = cvfqt::Utils::toString(m_rimFault->name());

        cvf::Vec3f textCoord(labelPosition);
        double characteristicCellSize = bb.extent().z() / 20;
        textCoord.z() += characteristicCellSize;

        drawableText->addText(cvfString, textCoord);

        cvf::ref<cvf::Part> part = new cvf::Part;
        part->setName("RivFaultPart : text " + cvfString);
        part->setDrawable(drawableText.p());

        cvf::ref<cvf::Effect> eff = new cvf::Effect;

        part->setEffect(eff.p());
        part->setPriority(1000);

        m_faultLabelPart = part;
    }


    // Line from fault geometry to label
    {
        cvf::ref<cvf::Vec3fArray> vertices = new cvf::Vec3fArray;
        vertices->reserve(2);
        vertices->add(faultVertexToAttachLabel);
        vertices->add(labelPosition);

        cvf::ref<cvf::DrawableGeo> geo = new cvf::DrawableGeo;
        geo->setVertexArray(vertices.p());

        cvf::ref<cvf::PrimitiveSetDirect> primSet = new cvf::PrimitiveSetDirect(cvf::PT_LINES);
        primSet->setStartIndex(0);
        primSet->setIndexCount(vertices->size());
        geo->addPrimitiveSet(primSet.p());

        m_faultLabelLinePart = new cvf::Part;
        m_faultLabelLinePart->setName("Anchor line for label" + cvf::String(static_cast<int>(m_grid->gridIndex())));
        m_faultLabelLinePart->setDrawable(geo.p());

        m_faultLabelLinePart->updateBoundingBox();

        caf::MeshEffectGenerator gen(m_rimFault->faultColor());
        cvf::ref<cvf::Effect> eff = gen.generateEffect();
        
        m_faultLabelLinePart->setEffect(eff.p());
    }
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
cvf::Vec3f RivFaultPartMgr::findClosestVertex(const cvf::Vec3f& point, const cvf::Vec3fArray* vertices)
{
    CVF_ASSERT(vertices);
    
    if (!vertices) return cvf::Vec3f::UNDEFINED;

    float closestDiff(HUGE_VAL);

    size_t closestIndex = cvf::UNDEFINED_SIZE_T;

    for (size_t i = 0; i < vertices->size(); i++)
    {
        float diff = point.pointDistance(vertices->get(i));

        if (diff < closestDiff)
        {
            closestDiff = diff;
            closestIndex = i;
        }
    }

    if (closestIndex != cvf::UNDEFINED_SIZE_T)
    {
        return vertices->get(closestIndex);
    }
    else
    {
        return cvf::Vec3f::UNDEFINED;
    }
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivFaultPartMgr::appendNativeFaultFacesToModel(cvf::ModelBasicList* model)
{
    if (m_nativeFaultFaces.notNull())
    {
        model->addPart(m_nativeFaultFaces.p());
    }
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivFaultPartMgr::appendOppositeFaultFacesToModel(cvf::ModelBasicList* model)
{
    if (m_oppositeFaultFaces.notNull())
    {
        model->addPart(m_oppositeFaultFaces.p());
    }
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivFaultPartMgr::appendLabelPartsToModel(cvf::ModelBasicList* model)
{
    if (m_faultLabelPart.notNull())         model->addPart(m_faultLabelPart.p());
    if (m_faultLabelLinePart.notNull())     model->addPart(m_faultLabelLinePart.p());
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivFaultPartMgr::appendMeshLinePartsToModel(cvf::ModelBasicList* model)
{
    if (m_nativeFaultGridLines.notNull())   model->addPart(m_nativeFaultGridLines.p());
    if (m_oppositeFaultGridLines.notNull()) model->addPart(m_oppositeFaultGridLines.p());
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivFaultPartMgr::appendNNCFacesToModel(cvf::ModelBasicList* model)
{
    if (m_NNCFaces.notNull())   model->addPart(m_NNCFaces.p());
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
cvf::ref<cvf::Effect> RivFaultPartMgr::cellResultEffect(const cvf::ScalarMapper* mapper, caf::PolygonOffset polygonOffset) const
{
    CVF_ASSERT(mapper);

    caf::ScalarMapperEffectGenerator scalarEffgen(mapper, polygonOffset);
    scalarEffgen.setFaceCulling(faceCullingMode());
    scalarEffgen.setOpacityLevel(m_opacityLevel);

    cvf::ref<cvf::Effect> scalarEffect = scalarEffgen.generateEffect();

    return scalarEffect;
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
caf::FaceCulling RivFaultPartMgr::faceCullingMode() const
{
    bool isShowingGrid = m_rimFaultCollection->isGridVisualizationMode();
    if (!isShowingGrid )
    {
        if (m_rimFaultCollection->faultResult() == RimFaultCollection::FAULT_BACK_FACE_CULLING)
        {
            if (m_grid->mainGrid()->faceNormalsIsOutwards())
            {
                return caf::FC_BACK;
            }
            else
            {
                return caf::FC_FRONT;
            }
        }
        else if (m_rimFaultCollection->faultResult() == RimFaultCollection::FAULT_FRONT_FACE_CULLING)
        {
            if (m_grid->mainGrid()->faceNormalsIsOutwards())
            {
                return caf::FC_FRONT;
            }
            else
            {
                return caf::FC_BACK;
            }
        }
        else
        {
             return caf::FC_NONE;
        }
    }
    else
    {
        // Do not perform face culling in grid mode to make sure the displayed grid is watertight
        return caf::FC_NONE;
    }
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivFaultPartMgr::updateNNCColors(RimResultSlot* cellResultSlot)
{
    if (m_NNCFaces.isNull()) return;

    if (cellResultSlot && cellResultSlot->resultVariable() == RimDefines::combinedTransmissibilityResultName())
    {
        const cvf::ScalarMapper* mapper = cellResultSlot->legendConfig()->scalarMapper();

        m_NNCGenerator->textureCoordinates(m_NNCTextureCoords.p(), mapper);

        cvf::ref<cvf::Effect> nncEffect;

        if (m_rimFaultCollection->showFaultFaces || m_rimFaultCollection->showOppositeFaultFaces)
        {
            // Move NNC closer to camera to avoid z-fighting with grid surface
            caf::ScalarMapperEffectGenerator nncEffgen(mapper, caf::PO_NEG_LARGE);
            nncEffect = nncEffgen.generateEffect();
        }
        else
        {
            // If no grid is present, use same offset as grid geometry to be able to see mesh lines
            caf::ScalarMapperEffectGenerator nncEffgen(mapper, caf::PO_1);
            nncEffect = nncEffgen.generateEffect();
        }

        cvf::DrawableGeo* dg = dynamic_cast<cvf::DrawableGeo*>(m_NNCFaces->drawable());
        if (dg) dg->setTextureCoordArray(m_NNCTextureCoords.p());

        m_NNCFaces->setEffect(nncEffect.p());
    }
    else
    {
        // NNC faces a bit lighter than the fault for now
        cvf::Color3f nncColor = m_defaultColor;
        nncColor.r() +=  (1.0 - nncColor.r()) * 0.2;
        nncColor.g() +=  (1.0 - nncColor.g()) * 0.2;
        nncColor.g() +=  (1.0 - nncColor.b()) * 0.2;

        cvf::ref<cvf::Effect> nncEffect;

        if (m_rimFaultCollection->showFaultFaces || m_rimFaultCollection->showOppositeFaultFaces)
        {
            // Move NNC closer to camera to avoid z-fighting with grid surface
            caf::SurfaceEffectGenerator nncEffgen(nncColor, caf::PO_NEG_LARGE);
            nncEffect = nncEffgen.generateEffect();
        }
        else
        {
            // If no grid is present, use same offset as grid geometry to be able to see mesh lines
            caf::SurfaceEffectGenerator nncEffgen(nncColor, caf::PO_1);
            nncEffect = nncEffgen.generateEffect();
        }

        m_NNCFaces->setEffect(nncEffect.p());
    }
}
