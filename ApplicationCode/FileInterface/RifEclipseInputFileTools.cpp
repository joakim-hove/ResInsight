/////////////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2011-2012 Statoil ASA, Ceetron AS
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

#include "RifEclipseInputFileTools.h"
#include "RifReaderEclipseOutput.h"
#include "RigReservoirCellResults.h"

#include "RigReservoir.h"
#include "cafProgressInfo.h"

#include <vector>
#include <cmath>
#include <iostream>

#include <QFile>
#include <QTextStream>

#ifdef USE_ECL_LIB
#include "ecl_grid.h"
#include "well_state.h"
#include "util.h"
#endif 
#include <fstream>


//--------------------------------------------------------------------------------------------------
/// Constructor
//--------------------------------------------------------------------------------------------------
RifEclipseInputFileTools::RifEclipseInputFileTools()
{

}


//--------------------------------------------------------------------------------------------------
/// Destructor
//--------------------------------------------------------------------------------------------------
RifEclipseInputFileTools::~RifEclipseInputFileTools()
{
  
}


//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
bool RifEclipseInputFileTools::openGridFile(const QString& fileName, RigReservoir* reservoir)
{
    CVF_ASSERT(reservoir);

    FILE* gridFilePointer = util_fopen(fileName.toLatin1().data(), "r");
    if (!gridFilePointer) return false;

    // Main grid Dimentions
    // SPECGRID - This is whats normally available, but not really the input to Eclipse.
    // DIMENS - Is what Eclipse expects and uses, but is not defined in the GRID section and is not (?) available normally
    // ZCORN, COORD, ACTNUM, MAPAXES

    //ecl_kw_type  *  ecl_kw_fscanf_alloc_grdecl_dynamic__( FILE * stream , const char * kw , bool strict , ecl_type_enum ecl_type);
    //ecl_grid_type * ecl_grid_alloc_GRDECL_kw( int nx, int ny , int nz , const ecl_kw_type * zcorn_kw , const ecl_kw_type * coord_kw , const ecl_kw_type * actnum_kw , const ecl_kw_type * mapaxes_kw ); 

    ecl_kw_type* specGridKw  = NULL;
    ecl_kw_type* zCornKw     = NULL;
    ecl_kw_type* coordKw     = NULL;
    ecl_kw_type* actNumKw    = NULL;
    ecl_kw_type* mapAxesKw   = NULL;

    // Try to read all the needed keywords. Early exit if some are not found
    caf::ProgressInfo progress(7, "Read Grid from Eclipse Input file");
    bool allKwReadOk = true;

    allKwReadOk = allKwReadOk && NULL != (specGridKw = ecl_kw_fscanf_alloc_grdecl_dynamic__( gridFilePointer , "SPECGRID" , false , ECL_INT_TYPE));
    progress.setProgress(1);
    allKwReadOk = allKwReadOk && NULL != (zCornKw    = ecl_kw_fscanf_alloc_grdecl_dynamic__( gridFilePointer , "ZCORN" , false , ECL_FLOAT_TYPE));
    progress.setProgress(2);
    allKwReadOk = allKwReadOk && NULL != (coordKw    = ecl_kw_fscanf_alloc_grdecl_dynamic__( gridFilePointer , "COORD" , false , ECL_FLOAT_TYPE));
    progress.setProgress(3);
    allKwReadOk = allKwReadOk && NULL != (actNumKw   = ecl_kw_fscanf_alloc_grdecl_dynamic__( gridFilePointer , "ACTNUM" , false , ECL_INT_TYPE));
    progress.setProgress(4);
    allKwReadOk = allKwReadOk && NULL != (mapAxesKw  = ecl_kw_fscanf_alloc_grdecl_dynamic__( gridFilePointer , "MAPAXES" , false , ECL_FLOAT_TYPE));
   
    if (!allKwReadOk)
    {
        if(specGridKw) ecl_kw_free(specGridKw);
        if(zCornKw) ecl_kw_free(zCornKw);
        if(coordKw) ecl_kw_free(coordKw);
        if(actNumKw) ecl_kw_free(actNumKw);
        if(mapAxesKw) ecl_kw_free(mapAxesKw);

        return false;
    }

    progress.setProgress(5);

    int nx = ecl_kw_iget_int(specGridKw, 0); 
    int ny = ecl_kw_iget_int(specGridKw, 1); 
    int nz = ecl_kw_iget_int(specGridKw, 2);

    ecl_grid_type* inputGrid = ecl_grid_alloc_GRDECL_kw( nx, ny, nz, zCornKw, coordKw, actNumKw, mapAxesKw ); 

    progress.setProgress(6);

    RifReaderEclipseOutput::transferGeometry(inputGrid, reservoir);

    progress.setProgress(7);
    progress.setProgressDescription("Cleaning up ...");

    ecl_kw_free(specGridKw);
    ecl_kw_free(zCornKw);
    ecl_kw_free(coordKw);
    ecl_kw_free(actNumKw);
    ecl_kw_free(mapAxesKw);

    ecl_grid_free(inputGrid);

    util_fclose(gridFilePointer);
    
    return true;
}


//--------------------------------------------------------------------------------------------------
/// Read known properties from the input file
//--------------------------------------------------------------------------------------------------
std::map<QString, QString>  RifEclipseInputFileTools::readProperties(const QString &fileName, RigReservoir *reservoir)
{
    CVF_ASSERT(reservoir);

    std::set<QString> knownKeywordSet;
    {
        const std::vector<QString>& knownKeywords = RifEclipseInputFileTools::knownPropertyKeywords();
        for( size_t fkIt = 0; fkIt < knownKeywords.size(); ++fkIt) knownKeywordSet.insert(knownKeywords[fkIt]);
    }

    caf::ProgressInfo mainProgress(2, "Reading Eclipse Input properties");
    caf::ProgressInfo startProgress(knownKeywordSet.size(), "Scanning for known properties");

    std::vector<QString> fileKeywords = RifEclipseInputFileTools::findKeywordsOnFile(fileName);

    mainProgress.setProgress(1);
    caf::ProgressInfo progress(fileKeywords.size(), "Reading properties");

    FILE* gridFilePointer = util_fopen(fileName.toLatin1().data(), "r");

    if (!gridFilePointer || !fileKeywords.size() ) 
    {
        return std::map<QString, QString>();
    }

    bool isSomethingRead = false;
    std::map<QString, QString> newResults;
    for (size_t i = 0; i < fileKeywords.size(); ++i)
    {
        std::cout << fileKeywords[i].toLatin1().data() << std::endl;
        if (knownKeywordSet.count(fileKeywords[i]))
        {
            ecl_kw_type* eclKeyWordData = ecl_kw_fscanf_alloc_grdecl_dynamic__( gridFilePointer , fileKeywords[i].toLatin1().data() , false , ECL_FLOAT_TYPE);
            if (eclKeyWordData)
            {
                QString newResultName = reservoir->mainGrid()->results()->makeResultNameUnique(fileKeywords[i]);

                size_t resultIndex = reservoir->mainGrid()->results()->addEmptyScalarResult(RimDefines::INPUT_PROPERTY, newResultName); // Should really merge with inputProperty object information because we need to use PropertyName, and not keyword

                std::vector< std::vector<double> >& newPropertyData = reservoir->mainGrid()->results()->cellScalarResults(resultIndex);
                newPropertyData.push_back(std::vector<double>());
                newPropertyData[0].resize(ecl_kw_get_size(eclKeyWordData), HUGE_VAL);
                ecl_kw_get_data_as_double(eclKeyWordData, newPropertyData[0].data());

                ecl_kw_free(eclKeyWordData);
                newResults[newResultName] = fileKeywords[i];
            }
        }
        progress.setProgress(i);
    }

    util_fclose(gridFilePointer);
    return newResults;
}

//--------------------------------------------------------------------------------------------------
/// Read all the keywords from a file
//--------------------------------------------------------------------------------------------------
std::vector<QString> RifEclipseInputFileTools::findKeywordsOnFile(const QString &fileName)
{
    std::vector<QString> keywords;

    std::ifstream is(fileName.toLatin1().data());

    while (is)
    {
        std::string word;
        is >> word;
        if (word.size() && isalpha(word[0])) 
        {
            keywords.push_back(word.c_str());
        }

        is.ignore(20000, '\n');
    }

    is.close();

    /*
    FILE* gridFilePointer = util_fopen(fileName.toLatin1().data(), "r");

    if (!gridFilePointer) return keywords;

    char * keyWordString = NULL;
 
    keyWordString = ecl_kw_grdecl_alloc_next_header(gridFilePointer);

    while(keyWordString)
    {
        keywords.push_back(keyWordString);
        util_realloc(keyWordString, 0, "RifEclipseInputFileTools::findKeywordsOnFile");
        keyWordString = ecl_kw_grdecl_alloc_next_header(gridFilePointer);
    }

    util_fclose(gridFilePointer);
    */
    return keywords;
}

//--------------------------------------------------------------------------------------------------
/// Reads the property data requested into the \a reservoir, overwriting any previous 
/// propeties with the same name.
//--------------------------------------------------------------------------------------------------
bool RifEclipseInputFileTools::readProperty(const QString& fileName, RigReservoir* reservoir, const QString& eclipseKeyWord, const QString& resultName)
{
    CVF_ASSERT(reservoir);

    FILE* filePointer = util_fopen(fileName.toLatin1().data(), "r");
    if (!filePointer) return false;

    ecl_kw_type* eclKeyWordData = ecl_kw_fscanf_alloc_grdecl_dynamic__( filePointer , eclipseKeyWord.toLatin1().data() , false , ECL_FLOAT_TYPE);
    bool isOk = false;
    if (eclKeyWordData)
    {
        QString newResultName = resultName;
        size_t resultIndex = reservoir->mainGrid()->results()->findScalarResultIndex(newResultName);
        if (resultIndex == cvf::UNDEFINED_SIZE_T)
        {
            resultIndex = reservoir->mainGrid()->results()->addEmptyScalarResult(RimDefines::INPUT_PROPERTY, newResultName); 
        }

        std::vector< std::vector<double> >& newPropertyData = reservoir->mainGrid()->results()->cellScalarResults(resultIndex);
        newPropertyData.resize(1);
        newPropertyData[0].resize(ecl_kw_get_size(eclKeyWordData), HUGE_VAL);
        ecl_kw_get_data_as_double(eclKeyWordData, newPropertyData[0].data());
        isOk = true;
        ecl_kw_free(eclKeyWordData);
    }

    util_fclose(filePointer);
    return isOk;
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
const std::vector<QString>& RifEclipseInputFileTools::knownPropertyKeywords()
{
    static std::vector<QString> knownKeywords;
    static bool isInitialized = false;
    if (!isInitialized)
    {
        knownKeywords.push_back("AQUIFERA");
        knownKeywords.push_back("ACTNUM");
        knownKeywords.push_back("EQLNUM");
        knownKeywords.push_back("FIPNUM");
        knownKeywords.push_back("KRG");
        knownKeywords.push_back("KRGR");
        knownKeywords.push_back("KRO");
        knownKeywords.push_back("KRORG");
        knownKeywords.push_back("KRORW");
        knownKeywords.push_back("KRW");
        knownKeywords.push_back("KRWR");
        knownKeywords.push_back("MINPVV");
        knownKeywords.push_back("MULTPV");
        knownKeywords.push_back("MULTX");
        knownKeywords.push_back("MULTX-");
        knownKeywords.push_back("MULTY");
        knownKeywords.push_back("MULTY-");
        knownKeywords.push_back("MULTZ");
        knownKeywords.push_back("NTG");
        knownKeywords.push_back("PCG");
        knownKeywords.push_back("PCW");
        knownKeywords.push_back("PERMX");
        knownKeywords.push_back("PERMY");
        knownKeywords.push_back("PERMZ");
        knownKeywords.push_back("PORO");
        knownKeywords.push_back("PVTNUM");
        knownKeywords.push_back("SATNUM");
        knownKeywords.push_back("SGCR");
        knownKeywords.push_back("SGL");
        knownKeywords.push_back("SGLPC");
        knownKeywords.push_back("SGU");
        knownKeywords.push_back("SGWCR");
        knownKeywords.push_back("SWATINIT");
        knownKeywords.push_back("SWCR");
        knownKeywords.push_back("SWGCR");
        knownKeywords.push_back("SWL");
        knownKeywords.push_back("SWLPC");
        knownKeywords.push_back("TRANX");
        knownKeywords.push_back("TRANY");
        knownKeywords.push_back("TRANZ");

        isInitialized = true;
    }
    return knownKeywords;
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
bool RifEclipseInputFileTools::writePropertyToTextFile(const QString& fileName, RigReservoir* reservoir, size_t timeStep, const QString& resultName, const QString& eclipseKeyWord)
{
    CVF_ASSERT(reservoir);

    size_t resultIndex = reservoir->mainGrid()->results()->findScalarResultIndex(resultName);
    if (resultIndex == cvf::UNDEFINED_SIZE_T)
    {
        return false;
    }
    
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }

    std::vector< std::vector<double> >& resultData = reservoir->mainGrid()->results()->cellScalarResults(resultIndex);
    if (resultData.size() == 0)
    {
        return false;
    }

    std::vector<double>& singleTimeStepData = resultData[timeStep];
    writeDataToTextFile(&file, eclipseKeyWord, singleTimeStepData);

    return true;
}

//--------------------------------------------------------------------------------------------------
/// Create and write a result vector with values for all cells.
/// undefinedValue is used for cells with no result
//--------------------------------------------------------------------------------------------------
bool RifEclipseInputFileTools::writeBinaryResultToTextFile(const QString& fileName, RigReservoir* reservoir, size_t timeStep, const QString& resultName, const QString& eclipseKeyWord, const double undefinedValue)
{
    CVF_ASSERT(reservoir);

    size_t resultIndex = reservoir->mainGrid()->results()->findScalarResultIndex(resultName);
    if (resultIndex == cvf::UNDEFINED_SIZE_T)
    {
        return false;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }

    std::vector<double> resultData;
    size_t i, j, k;

    for (k = 0; k < reservoir->mainGrid()->cellCountK(); k++)
    {
        for (j = 0; j < reservoir->mainGrid()->cellCountJ(); j++)
        {
            for (i = 0; i < reservoir->mainGrid()->cellCountI(); i++)
            {
                double resultValue = reservoir->mainGrid()->cellScalar(timeStep, resultIndex, i, j, k);
                if (resultValue == HUGE_VAL)
                {
                    resultValue = undefinedValue;
                }

                resultData.push_back(resultValue);
            }
        }
    }

    writeDataToTextFile(&file, eclipseKeyWord, resultData);

    return true;
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RifEclipseInputFileTools::writeDataToTextFile(QFile* file, const QString& eclipseKeyWord, const std::vector<double>& resultData)
{
    QTextStream out(file);
    out << "\n";
    out << "-- Exported from ResInsight" << "\n";
    out << eclipseKeyWord << "\n" << right << qSetFieldWidth(16);

    caf::ProgressInfo pi(resultData.size(), QString("Writing data to file %1").arg(file->fileName()) );
    int progressSteps = resultData.size() / 20;

    size_t i;
    for (i = 0; i < resultData.size(); i++)
    {
        out << resultData[i];

        if ( (i + 1) % 5 == 0)
        {
            out << "\n";
        }

        if (i % progressSteps == 0)
        {
            pi.setProgress(i);
        }
    }

    out << "\n" << "/" << "\n";
}