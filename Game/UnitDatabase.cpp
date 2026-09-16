#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "UnitDatabase.h"

#include "CMappedResource.h"
#include "SoundManager.h"
/////////////////////////////////////////////////////////////////////////////////////
SUnitDatabase theUnitDatabase;
/////////////////////////////////////////////////////////////////////////////////////
BOOL SUnitDatabase::Load(LPCSTR pszPath, LPCSTR pszFName)
{
    CString szPathName = CString(pszPath) + pszFName;
    CMappedResource res;
    if (res.OpenResource(szPathName) == FALSE)
        return ReportError("Can't open database file \"%s\"\n", szPathName);
    //
    DWORD num;
    LPBYTE pData = res.GetFile("units.udb", &num);
    if (!pData)
        return ReportError("Can't open units database\n");
    //
    CMemorySave file;

    file.StartSaving();
    file.Write(pData, num);
    file.Seek(0);
    file.StartLoading();

    hlts.Load(file, 1);
    races.Load(file, 2);
    prototypes.Load(file, 3);
    npcs.Load(file, 4);
    //
    res.CloseFile(pData);
    res.CloseResource();
    //
    int i, j;
    // resolve races indices for prototypes
    for (i = 0; i < prototypes.GetNumEntries(); ++i)
        const_cast<SUDBUnitPrototype *>(prototypes[i])->dwRaceIndex = races.GetEntryIndex(
            prototypes[i]->szRace);
    // resolve hit locations
    for (i = 0; i < races.GetNumEntries(); ++i)
    {
        for (j = 0; j < RPG_NUM_HIT_LOCATIONS; ++j)
            const_cast<SUDBRaceModel *>(races[i])->hls[j].dwHLTypeIndex = hlts.GetEntryIndex(
                races[i]->hls[j].szHLType);
    }
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void SUnitDatabase::SetupWithSounds(CSoundManager *pSoundManager)
{
    std::vector<std::string> szStrings;
    CStringArray szFiles;
    int i;
    // setup sounds in races
    pSoundManager->GetFXFileList(szFiles, "*.wav");
    // to lower case
    szStrings.reserve(szFiles.GetSize());
    for (i = 0; i < szFiles.GetSize(); ++i)
    {
        szFiles[i].MakeLower();
        szStrings.push_back((LPCSTR)szFiles[i]);
    }
    //
    for (i = 0; i < races.GetNumEntries(); ++i)
    {
        SUDBRaceModel *pRace = const_cast<SUDBRaceModel *>(races[i]);
        // steps
        pRace->dwNumSteps.resize(pRace->szSteps.size());
        for (int j = 0; j < pRace->szSteps.size(); ++j)
        {
            pRace->dwNumSteps[j] = 0;
            pRace->szSteps[j].MakeLower();
            std::string szStep = pRace->szSteps[j].GetString();
            for (int k = 0; k < szStrings.size(); ++k)
            {
                if (szStrings[k].compare(0, szStep.size(), szStep) == 0)
                    ++pRace->dwNumSteps[j];
            }
        }
        /*
        // attacks
        szFiles.RemoveAll();
        pSoundManager->GetFileList( szFiles, pRace->szSFXPath + "\\attack\\*.wav" );
        pRace->dwNumSFXAttack = szFiles.GetSize();
        // deathes
        szFiles.RemoveAll();
        pSoundManager->GetFileList( szFiles, pRace->szSFXPath + "\\death\\*.wav" );
        pRace->dwNumSFXDeath = szFiles.GetSize();
        // hits
        szFiles.RemoveAll();
        pSoundManager->GetFileList( szFiles, pRace->szSFXPath + "\\hit\\*.wav" );
        pRace->dwNumSFXHit = szFiles.GetSize();
        // idles
        szFiles.RemoveAll();
        pSoundManager->GetFileList( szFiles, pRace->szSFXPath + "\\idle\\*.wav" );
        pRace->dwNumSFXIdle = szFiles.GetSize();
        */
    }
    // setup sounds in prototypes
    /*
    for ( i=0; i<prototypes.GetNumEntries(); ++i )
    {
            SUDBUnitPrototype* pPrototype = const_cast<SUDBUnitPrototype*>( prototypes[i] );
            if ( pPrototype->szSFXPath.GetLength() == 0 )
                    continue;
            // attacks
            szFiles.RemoveAll();
            pSoundManager->GetFileList( szFiles, pPrototype->szSFXPath + "\\attack\\*.wav" );
            pPrototype->dwNumSFXAttack = szFiles.GetSize();
            // deathes
            szFiles.RemoveAll();
            pSoundManager->GetFileList( szFiles, pPrototype->szSFXPath + "\\death\\*.wav" );
            pPrototype->dwNumSFXDeath = szFiles.GetSize();
            // hits
            szFiles.RemoveAll();
            pSoundManager->GetFileList( szFiles, pPrototype->szSFXPath + "\\hit\\*.wav" );
            pPrototype->dwNumSFXHit = szFiles.GetSize();
            // idles
            szFiles.RemoveAll();
            pSoundManager->GetFileList( szFiles, pPrototype->szSFXPath + "\\idle\\*.wav" );
            pPrototype->dwNumSFXIdle = szFiles.GetSize();
    }
    */
}
/////////////////////////////////////////////////////////////////////////////////////
