// Source-derived variant parameter cases; source unreachable comparisons intentionally retained.
bool CParticle::ConfigureExtendedSpell(ETypeIDs typeId,char*pszTexName)
{
 int i;switch(typeId){
 case PTID_VAMP:
  bUseAddEffect=TRUE;SetFlag(PS_NEED_CARRIER_TO_GENERATE);nMinTrys=0x7FFFFFFF;nLifeSpan=8;nLifeOfPhase=1;fpSize=.5f;
  createFunc=&CParticle::HealingSingleCreate;segmentFunc=&CParticle::HealingSingleSegment;locSourceSegmentFunc=&CParticle::HealingLocSourceSegmentFunc;
  strcpy(pszTexName,"RikArrowSmoke");locSources.SetSize(0);break;
	case PTID_FIREBALLDARK: 
	case PTID_FIREBALLLIGHT: 
	{
		nBornProbability = 88;
		nMinTrys = 1;
		fpLenToSingleDice = 0.09f;
		nLifeSpan = 15;
		nAnimPhases = 15;
		nLifeOfPhase = 1;
		posRespectMinRnd = CPlot3D(-0.2f, -0.2f, -1.f);
		posRespectMaxRnd = CPlot3D(0.2f, 0.2f, -0.f);
		fpSize = 0.8f;
		fpSizeFactor = 0.94f;
		createFunc = &CParticle::FireballSingleCreate;
		segmentFunc = &CParticle::FireballSingleSegment;
		strcpy(pszTexName, "FireSmoke2");
		if (typeId == PTID_FIREBALLLIGHT)
			strcpy(pszTexName, "FireSmoke3");
		break; }
	case PTID_FIREARROWDARK:
	case PTID_FIREARROWLIGHT:
	{
		nMinTrys = 1;
		fpLenToSingleDice = 0.06f;
		nLifeSpan = 15;
		nAnimPhases = 15;
		nLifeOfPhase = 1;
		posRespectMinRnd = CPlot3D(-0.2f, -0.2f, -0.2f);
		posRespectMaxRnd = CPlot3D(0.2f, 0.2f, 0.2f);
		fpSize = 0.3f;
		fpSizeFactor = 1.03f;
		dwPFlags = 0;
		createFunc = &CParticle::FireArrowSingleCreate;
		segmentFunc = &CParticle::FireArrowSingleSegment;
		if (typeId == PTID_RICKARROW)
			strcpy(pszTexName, "RikArrowSmoke");
		else if (typeId == PTID_FIREARROWDARK)
			strcpy(pszTexName, "FireArrowSmoke2");
		else if (typeId == PTID_FIREARROWLIGHT)
			strcpy(pszTexName, "FireArrowSmoke3");
		else
			strcpy(pszTexName, "FireArrowSmoke");
		break; }
	case PTID_ACIDRAYDARK:
	case PTID_ACIDRAYLIGHT: 
		{
		nMinTrys = 1;
		fpLenToSingleDice = 0.06f;
		nLifeSpan = 15;
		nAnimPhases = 15;
		nLifeOfPhase = 1;
		posRespectMinRnd = CPlot3D(-1, -1, -1);
		posRespectMaxRnd = CPlot3D(1, 1, 1);
		fpSize = 0.3f;
		createFunc = &CParticle::AcidRaySingleCreate;
		segmentFunc = &CParticle::AcidRaySingleSegment;
		if (typeId == PTID_ACIDRAYDARK)
			strcpy(pszTexName, "AcidRay2");
		else if (typeId == PTID_ACIDRAYLIGHT)
			strcpy(pszTexName, "AcidRay3");
		else
			strcpy(pszTexName, "AcidRay");
		break;
	}
	case PTID_FIREWALLDARK:
	case PTID_FIREWALLLIGHT:
	{
		nBornProbability = 88;
		nMinTrys = 6;
		nLifeSpan = 32;
		nAnimPhases = 15;
		nLifeOfPhase = 1;
		posRespectMinRnd = CPlot3D(-0.2f, -0.2f, 0);
		posRespectMaxRnd = CPlot3D(0.2f, 0.2f, 0);
		fpSize = 0.8f;
		fpSpeedFactor = 0.30f; 
		fpSizeFactor = 0.85f;
		createFunc = &CParticle::CampfireSingleCreate;
		segmentFunc = &CParticle::CampfireSingleSegment;
		if (GetTypeID() == PTID_FIREWALL)
		{
			nMinTrys = 20;
			nParticlesMax = 1500;
			locSources.SetSize(1);
			createFunc = &CParticle::FireWallSingleCreate;
			locSourceSegmentFunc = &CParticle::FireLocSourceSegmentFunc;
		}
		strcpy(pszTexName, "TendriSmoke");
		if (typeId == PTID_STAFFBALLFIRE)
		{
			strcpy(pszTexName, "TendriSmoke");
			
			fpSizeFactor = 0.67f;
		}
		if (typeId == PTID_STAFFBALLLIGHT)
		{
			
			strcpy(pszTexName, "TendriLight");
			fpSizeFactor = 0.67f;
			fpSpeedFactor = 0.0f; 
		}
		if (typeId == PTID_STAFFBALLWATER)
		{
			strcpy(pszTexName, "acidray");
			nAnimPhases = 10;
			fpSizeFactor = 0.67f;
			fpSpeedFactor = -0.10f; 
		}
		if (typeId == PTID_EYEBALLFIRE)
			strcpy(pszTexName, "TendriSmoke");
		if (typeId == PTID_EYEBALLWATER)
			strcpy(pszTexName, "TendriSmoke1");
		if (typeId == PTID_EYEBALLACID)
			strcpy(pszTexName, "TendriSmoke2");
		if (typeId == PTID_FIREWALLDARK)
			strcpy(pszTexName, "TendriSmoke2");
		if (typeId == PTID_FIREWALLLIGHT)
			strcpy(pszTexName, "TendriSmoke3");

		break; }
	case PTID_FIREBLASTDARK:
	case PTID_FIREBLASTLIGHT:
	{
		nBornProbability = 100;
		nMinTrys = 60;
		nParticlesMax = 600;
		nLifeSpan = 15;
		nLifeOfPhase = 1;
		posRespectMinRnd = CPlot3D(-0.02f, -0.02f, -0.02f);
		posRespectMaxRnd = CPlot3D(0.02f, 0.02f, 0.02f);
		fpSize = 0.6f;
		fpSpeedFactor = 1.0f; 
		fpSizeFactor = 0.8f;
		createFunc = &CParticle::FireBlastSingleCreate;
		segmentFunc = &CParticle::FireBlastSingleSegment;
		locSourceSegmentFunc = &CParticle::FireBlastLocSourceSegmentFunc;
		strcpy(pszTexName, "Explosion");
		if (typeId == PTID_FIREBLASTDARK)
			strcpy(pszTexName, "Explosion2");
		if (typeId == PTID_FIREBLASTLIGHT)
			strcpy(pszTexName, "Explosion3");
		
		locSources.SetSize(15);
	}
	break;
	case PTID_POISONFOGDARK:
	case PTID_POISONFOGLIGHT:
{
		nBornProbability = 50;
		nMinTrys = 1;
		nParticlesMax = 0x7FFFFFFF;
		nLifeSpan = 3236;
		nAnimPhases = 15;
		nLifeOfPhase = 1;
		createFunc = &CParticle::PoisonFogSingleCreate;
		segmentFunc = &CParticle::PoisonFogSingleSegment;
		strcpy(pszTexName, "PoisonCloud");
		if (typeId == PTID_FIREBLASTDARK)
			strcpy(pszTexName, "PoisonCloud2");
		if (typeId == PTID_FIREBLASTDARK)
			strcpy(pszTexName, "PoisonCloud3");
		break; }
	case PTID_NUKEDARK:
	case PTID_NUKELIGHT:
	{
		nMinTrys = 128;
		nParticlesMax = 400;
		nLifeSpan = -1;
		nLifeOfPhase = 1;
		fpSpeedFactor = 1;							
		createFunc = &CParticle::NukeSingleCreate;
		segmentFunc = &CParticle::NukeSingleSegment;
		locSourceSegmentFunc = &CParticle::NukeLocSourceSegmentFunc;
		if (typeId == PTID_FIREBLASTDARK)
			strcpy(pszTexName, "AcidRay2"); 
		else if (typeId == PTID_FIREBLASTDARK)
			strcpy(pszTexName, "AcidRay3"); 
		else
			strcpy(pszTexName, "AcidRay"); 
		locSources.SetSize(1);
		locSources[0].nLifeTime = 0;			
		break; }
	case PTID_LIGHTNINGBLASTDARK: 
	case PTID_LIGHTNINGBLASTLIGHT: {
		nBornProbability = 88;
		nMinTrys = 250;
		nParticlesMax = 600;
		nLifeSpan = 15;
		nAnimPhases = 15;
		nLifeOfPhase = 1;
		posRespectMinRnd = CPlot3D(-0.2f, -0.2f, -0.2f);
		posRespectMaxRnd = CPlot3D(0.2f, 0.2f, 0.2f);
		fpSize = 1;
		
		createFunc = &CParticle::LightningBlastSingleCreate;
		segmentFunc = &CParticle::LightningBlastSingleSegment;
		locSourceSegmentFunc = &CParticle::LightningBlastLocSourceSegmentFunc;
		strcpy(pszTexName, "TendriLight");
		if (typeId == PTID_LIGHTNINGBLASTDARK)
			strcpy(pszTexName, "TendriLight");
		if (typeId == PTID_LIGHTNINGBLASTLIGHT)
			strcpy(pszTexName, "TendriLight");
		bUseAddEffect = TRUE;
		break; }
	case PTID_ACIDSTARDARK:
	case PTID_ACIDSTARLIGHT: 
		{
		SetFlag(PS_NEED_CARRIER_TO_GENERATE);
		SetFlag(PS_BIND_TO_BODYPART);
		bindPlace = bpBody;
		nMinTrys = 1;
		createFunc = &CParticle::StarSingleCreate;
		segmentFunc = &CParticle::StarSingleSegment;
		locSourceSegmentFunc = &CParticle::StarLocSourceSegmentFunc;
		switch (typeId)
		{
		case PTID_FIRESTAR: strcpy(pszTexName, "FireArrowSmoke"); nLifeSpan = 15; break;
		case PTID_ACIDSTAR: strcpy(pszTexName, "AcidRay"); nLifeSpan = 15; break;
		case PTID_ACIDSTARDARK: strcpy(pszTexName, "AcidRay2"); nLifeSpan = 15; break;
		case PTID_ACIDSTARLIGHT: strcpy(pszTexName, "AcidRay3"); nLifeSpan = 15; break;
		}
		break; }
 default:return false;
 }return true;
}

void CParticle::InitializeExtendedSpellSources()
{
 if(GetTypeID()!=PTID_FIREBLASTDARK&&GetTypeID()!=PTID_FIREBLASTLIGHT)return;
 int i;
		locSources.SetSize(15);
		for (i = 0; i < locSources.GetSize(); i++)
		{
			locSources[i].respectCP = CPlot3D(0, 0, 0);
			CPlot3D vec;
			vec = GetRandomSpherePoint(CPlot3D(0, 0, 0.7f));
			

			locSources[i].vSpeed = vec * random.GetFloat(0.8f, 1.2f) * 0.8f;
			locSources[i].nPhase = 0;
			locSources[i].reserved.x = 0;
			locSources[i].reserved.y = 0.4f;
			locSources[i].reserved.z = 0.7f;
			locSources[i].reserved2.x = random.Get(0, 5);
			locSources[i].reserved2.y = random.Get(0.7f, 1.4f);
		}
}
