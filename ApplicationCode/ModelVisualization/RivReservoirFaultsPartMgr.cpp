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

#include "RivReservoirFaultsPartMgr.h"

#include "cvfPart.h"
#include "cvfModelBasicList.h"
#include "cvfColor3.h"
#include "cvfTransform.h"

#include "cafPdmFieldCvfColor.h"

#include "RimFaultCollection.h"
#include "RigMainGrid.h"


//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
RivReservoirFaultsPartMgr::RivReservoirFaultsPartMgr(const RigMainGrid* grid,  const RimFaultCollection* faultCollection)
:   m_faultCollection(faultCollection)
{
    CVF_ASSERT(grid);

    if (faultCollection)
    {
        for (size_t i = 0; i < faultCollection->faults.size(); i++)
        {
            m_faultParts.push_back(new RivFaultPartMgr(grid, faultCollection, faultCollection->faults[i]));
        }
    }

    m_forceVisibility = false;
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
RivReservoirFaultsPartMgr::~RivReservoirFaultsPartMgr()
{
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivReservoirFaultsPartMgr::setTransform(cvf::Transform* scaleTransform)
{
    m_scaleTransform = scaleTransform;
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivReservoirFaultsPartMgr::setCellVisibility(cvf::UByteArray* cellVisibilities)
{
    CVF_ASSERT(cellVisibilities);

    for (size_t i = 0; i < m_faultParts.size(); i++)
    {
        m_faultParts.at(i)->setCellVisibility(cellVisibilities);
    }
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivReservoirFaultsPartMgr::appendPartsToModel(cvf::ModelBasicList* model)
{
    CVF_ASSERT(model != NULL);

    if (!m_faultCollection) return;

    bool isShowingGrid = m_faultCollection->isGridVisualizationMode();
    if (!m_faultCollection->showFaultCollection() && !isShowingGrid) return;
    
    // Check match between model fault count and fault parts
    CVF_ASSERT(m_faultCollection->faults.size() == m_faultParts.size());

    cvf::ModelBasicList parts;

    for (size_t i = 0; i < m_faultCollection->faults.size(); i++)
    {
        const RimFault* rimFault = m_faultCollection->faults[i];

        cvf::ref<RivFaultPartMgr> rivFaultPart = m_faultParts[i];
        CVF_ASSERT(rivFaultPart.notNull());

        // Parts that is overridden by the grid settings
        bool forceDisplayOfFault = isShowingGrid;
        if (m_forceVisibility)
        {
            forceDisplayOfFault = true;
        }

        if (rimFault->showFault() || forceDisplayOfFault)
        {
            if (m_faultCollection->showFaultFaces() || forceDisplayOfFault)
            {
                rivFaultPart->appendNativeFaultFacesToModel(&parts);
            }

            if (m_faultCollection->showOppositeFaultFaces() || forceDisplayOfFault)
            {
                rivFaultPart->appendOppositeFaultFacesToModel(&parts);
            }

            if (m_faultCollection->showFaultFaces() || m_faultCollection->showOppositeFaultFaces() || m_faultCollection->showNNCs() || forceDisplayOfFault)
            {
                rivFaultPart->appendMeshLinePartsToModel(&parts);
            }
        }

        // Parts that is not overridden by the grid settings

        if (rimFault->showFault() && m_faultCollection->showFaultCollection())
        {
            if (m_faultCollection->showNNCs())
            {
                rivFaultPart->appendNNCFacesToModel(&parts);
            }
        }
    }

    for (size_t i = 0; i < parts.partCount(); i++)
    {
        cvf::Part* part = parts.part(i);
        part->setTransform(m_scaleTransform.p());
        
        model->addPart(part);
    }
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivReservoirFaultsPartMgr::applySingleColorEffect()
{
    for (size_t i = 0; i < m_faultParts.size(); i++)
    {
        m_faultParts[i]->applySingleColorEffect();
    }
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivReservoirFaultsPartMgr::updateColors(size_t timeStepIndex, RimResultSlot* cellResultSlot)
{
    if (m_faultCollection->showResultsOnFaults())
    {
        for (size_t i = 0; i < m_faultParts.size(); i++)
        {
            m_faultParts[i]->updateCellResultColor(timeStepIndex, cellResultSlot);
        }
    }
    else
    {
        applySingleColorEffect();
    }
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivReservoirFaultsPartMgr::updateCellEdgeResultColor(size_t timeStepIndex, RimResultSlot* cellResultSlot, RimCellEdgeResultSlot* cellEdgeResultSlot)
{
    for (size_t i = 0; i < m_faultParts.size(); i++)
    {
        m_faultParts[i]->updateCellEdgeResultColor(timeStepIndex, cellResultSlot, cellEdgeResultSlot);
    }
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivReservoirFaultsPartMgr::appendLabelPartsToModel(cvf::ModelBasicList* model)
{
    CVF_ASSERT(model != NULL);

    if (!m_faultCollection) return;

    if (!m_faultCollection->showFaultCollection()) return;
    
    if (!m_faultCollection->showFaultLabel() ) return;

    // Check match between model fault count and fault parts
    CVF_ASSERT(m_faultCollection->faults.size() == m_faultParts.size());

    cvf::ModelBasicList parts;

    for (size_t i = 0; i < m_faultCollection->faults.size(); i++)
    {
        const RimFault* rimFault = m_faultCollection->faults[i];

        cvf::ref<RivFaultPartMgr> rivFaultPart = m_faultParts[i];
        CVF_ASSERT(rivFaultPart.notNull());

        if (rimFault->showFault())
        {
            rivFaultPart->appendLabelPartsToModel(&parts);
        }
    }

    for (size_t i = 0; i < parts.partCount(); i++)
    {
        cvf::Part* part = parts.part(i);
        part->setTransform(m_scaleTransform.p());

        model->addPart(part);
    }
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RivReservoirFaultsPartMgr::setFaultForceVisibility(bool forceVisibility)
{
    m_forceVisibility = forceVisibility;
}
