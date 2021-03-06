﻿/*	-------------------------------------------------------------------------------------------------------
	� 1991-2012 Take-Two Interactive Software and its subsidiaries.  Developed by Firaxis Games.  
	Sid Meier's Civilization V, Civ, Civilization, 2K Games, Firaxis Games, Take-Two Interactive Software 
	and their respective logos are all trademarks of Take-Two interactive Software, Inc.  
	All other marks and trademarks are the property of their respective owners.  
	All rights reserved. 
	------------------------------------------------------------------------------------------------------- */
#include "CvGameCoreDLLPCH.h"
#include "FFastVector.h"
#include "CvGameCoreUtils.h"
#include "CvAStar.h"
#include "CvEconomicAI.h"
#include "CvEnumSerialization.h"
#include "CvUnitCombat.h"
#include "cvStopWatch.h"
#include "CvMilitaryAI.h"
#include "CvTypes.h"
#if defined(MOD_BALANCE_CORE_MILITARY)
#include "CvDiplomacyAI.h"
#include "CvBarbarians.h"
#endif

#include <cmath>
#include "LintFree.h"

#if defined(MOD_BALANCE_CORE_MILITARY)
//for easier debugging
int g_currentUnitToTrack = 0;
#endif

CvTacticalUnit::CvTacticalUnit() :
	m_iID(0)
	, m_iAttackStrength(0)
	, m_iHealthPercent(0)
	, m_iMovesToTarget(0)
	, m_iExpectedTargetDamage(0)
	, m_iExpectedSelfDamage(0)
#ifdef MOD_AI_SMART_AIR_TACTICS 
	, m_pInterceptor(0)
#endif
{
}

//=====================================
// CvTacticalMoveXMLEntry
//=====================================
/// Constructor
CvTacticalMoveXMLEntry::CvTacticalMoveXMLEntry(void)
{
	m_iPriority = -1;
	m_iOffenseWeight = 0;
	m_iOffenseWeight = 0;
	m_bOperationsCanRecruit = false;
}

/// Destructor
CvTacticalMoveXMLEntry::~CvTacticalMoveXMLEntry(void)
{
}

/// Load from XML (or cache)
bool CvTacticalMoveXMLEntry::CacheResults(Database::Results& kResults, CvDatabaseUtility& kUtility)
{
	if(!CvBaseInfo::CacheResults(kResults, kUtility))
		return false;

	const char* szMoveType = kResults.GetText("Type");
	m_eMoveType = (TacticalAIMoveTypes)GC.getInfoTypeForString(szMoveType);

	m_iPriority = kResults.GetInt("Priority");

	m_iOffenseWeight = kResults.GetInt("OffenseFlavorWeight");
	m_iDefenseWeight = kResults.GetInt("DefenseFlavorWeight");

	m_bOperationsCanRecruit = kResults.GetInt("OperationsCanRecruit");
	m_bDominanceZoneMove = kResults.GetInt("DominanceZoneMove");

	return true;
}
//=====================================
// CvTacticalMoveXMLEntries
//=====================================
/// Constructor
CvTacticalMoveXMLEntries::CvTacticalMoveXMLEntries(void)
{

}

/// Destructor
CvTacticalMoveXMLEntries::~CvTacticalMoveXMLEntries(void)
{
	DeleteArray();
}

/// Returns vector of AIStrategy entries
std::vector<CvTacticalMoveXMLEntry*>& CvTacticalMoveXMLEntries::GetTacticalMoveEntries()
{
	return m_paTacticalMoveEntries;
}

/// Number of defined AIStrategies
int CvTacticalMoveXMLEntries::GetNumTacticalMoves()
{
	return m_paTacticalMoveEntries.size();
}

/// Get a specific entry
CvTacticalMoveXMLEntry* CvTacticalMoveXMLEntries::GetEntry(int index)
{
#if defined(MOD_BALANCE_CORE)
	return (index!=NO_TACTICAL_MOVE) ? m_paTacticalMoveEntries[index] : NULL;
#else
	return m_paTacticalMoveEntries[index];
#endif
}

/// Clear entries
void CvTacticalMoveXMLEntries::DeleteArray()
{
	for(std::vector<CvTacticalMoveXMLEntry*>::iterator it = m_paTacticalMoveEntries.begin(); it != m_paTacticalMoveEntries.end(); ++it)
	{
		SAFE_DELETE(*it);
	}

	m_paTacticalMoveEntries.clear();
}

//=====================================
// CvTacticalTarget
//=====================================

bool CvTacticalTarget::IsReadyForCapture()
{
	AITacticalTargetType eType = GetTargetType();
	if(eType == AI_TACTICAL_TARGET_CITY)
	{
		CvPlot *pPlot = GC.getMap().plot(m_iTargetX, m_iTargetY);
		CvCity *pCity = pPlot->getPlotCity();
		if(pCity != NULL)
		{
			//if there's only one hitpoint left
			int iCurHp = pCity->GetMaxHitPoints() - pCity->getDamage();
			return iCurHp<=1;
		}
	}

	return false;
}

/// Still a living target?
bool CvTacticalTarget::IsTargetStillAlive(PlayerTypes eAttackingPlayer)
{
	bool bRtnValue = false;

	AITacticalTargetType eType = GetTargetType();
	if(eType == AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT ||
	        eType == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT ||
	        eType == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT)
	{
		CvPlot* pPlot = GC.getMap().plot(m_iTargetX, m_iTargetY);
		CvUnit* pUnit = pPlot->getVisibleEnemyDefender(eAttackingPlayer);
		if(pUnit != NULL && !pUnit->isDelayedDeath())
		{
			bRtnValue = true;
		}
	}
	else if(eType == AI_TACTICAL_TARGET_CITY)
	{
		CvPlot *pPlot = GC.getMap().plot(m_iTargetX, m_iTargetY);
		CvCity *pCity = pPlot->getPlotCity();
		if(pCity != NULL && pCity->getOwner() == GetTargetPlayer())
		{
			bRtnValue = true;
		}
	}
	return bRtnValue;
}

/// This target make sense for this domain of unit/zone?
bool CvTacticalTarget::IsTargetValidInThisDomain(DomainTypes eDomain)
{
	bool bRtnValue = false;

	switch(GetTargetType())
	{
	case AI_TACTICAL_TARGET_DEFENSIVE_BASTION:
	case AI_TACTICAL_TARGET_BARBARIAN_CAMP:
	case AI_TACTICAL_TARGET_IMPROVEMENT:
	case AI_TACTICAL_TARGET_IMPROVEMENT_TO_DEFEND:
	case AI_TACTICAL_TARGET_ANCIENT_RUINS:
	case AI_TACTICAL_TARGET_TRADE_UNIT_LAND:
	case AI_TACTICAL_TARGET_TRADE_UNIT_LAND_PLOT:
	case AI_TACTICAL_TARGET_CITADEL:
	case AI_TACTICAL_TARGET_IMPROVEMENT_RESOURCE:
		bRtnValue = (eDomain == DOMAIN_LAND);
		break;

	case AI_TACTICAL_TARGET_BLOCKADE_RESOURCE_POINT:
	case AI_TACTICAL_TARGET_BOMBARDMENT_ZONE:
	case AI_TACTICAL_TARGET_EMBARKED_CIVILIAN:
	case AI_TACTICAL_TARGET_EMBARKED_MILITARY_UNIT:
	case AI_TACTICAL_TARGET_TRADE_UNIT_SEA:
	case AI_TACTICAL_TARGET_TRADE_UNIT_SEA_PLOT:
		bRtnValue = (eDomain == DOMAIN_SEA);
		break;

	case AI_TACTICAL_TARGET_CITY:
	case AI_TACTICAL_TARGET_CITY_TO_DEFEND:
	case AI_TACTICAL_TARGET_LOW_PRIORITY_CIVILIAN:
	case AI_TACTICAL_TARGET_MEDIUM_PRIORITY_CIVILIAN:
	case AI_TACTICAL_TARGET_HIGH_PRIORITY_CIVILIAN:
	case AI_TACTICAL_TARGET_VERY_HIGH_PRIORITY_CIVILIAN:
	case AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT:
	case AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT:
	case AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT:
		bRtnValue = true;
		break;
	}

	return bRtnValue;
}

FDataStream& operator<<(FDataStream& saveTo, const CvTemporaryZone& readFrom)
{
	uint uiVersion = 1;
	saveTo << uiVersion;
	MOD_SERIALIZE_INIT_WRITE(saveTo);

	saveTo << (int)readFrom.GetTargetType();
	saveTo << readFrom.GetLastTurn();
	saveTo << readFrom.GetX();
	saveTo << readFrom.GetY();
	saveTo << (int)readFrom.IsNavalInvasion();
	return saveTo;
}

FDataStream& operator>>(FDataStream& loadFrom, CvTemporaryZone& writeTo)
{
	uint uiVersion;
	loadFrom >> uiVersion;
	MOD_SERIALIZE_INIT_READ(loadFrom);

	int iTemp;
	loadFrom >> iTemp;
	writeTo.SetTargetType((AITacticalTargetType)iTemp);
	loadFrom >> iTemp;
	writeTo.SetLastTurn(iTemp);
	loadFrom >> iTemp;
	writeTo.SetX(iTemp);
	loadFrom >> iTemp;
	writeTo.SetY(iTemp);
	loadFrom >> iTemp;
	writeTo.SetNavalInvasion(iTemp != 0);
	return loadFrom;
}

//=====================================
// CvTacticalAI
//=====================================

/// Constructor
CvTacticalAI::CvTacticalAI(void)
{
	m_iCurrentZoneIndex = -1;
}

/// Destructor
CvTacticalAI::~CvTacticalAI(void)
{
	Uninit();
}

/// Initialize
void CvTacticalAI::Init(CvPlayer* pPlayer)
{
	// Store off the pointer to the objects we need elsewhere in the game engine
	m_pPlayer = pPlayer;

	//do this after setting the player pointer!
	Reset();

	// Initialize AI constants from XML
	m_iRecruitRange = GC.getAI_TACTICAL_RECRUIT_RANGE();
	m_iLandBarbarianRange = GC.getGame().getHandicapInfo().getBarbarianLandTargetRange();
	m_iSeaBarbarianRange = GC.getGame().getHandicapInfo().getBarbarianSeaTargetRange();
	m_iRepositionRange = GC.getAI_TACTICAL_REPOSITION_RANGE();
	m_iDeployRadius = GC.getAI_OPERATIONAL_CITY_ATTACK_DEPLOY_RANGE();
	m_iRandomRange = GC.getAI_TACTICAL_MOVE_PRIORITY_RANDOMNESS();
	m_fFlavorDampening = GC.getAI_TACTICAL_FLAVOR_DAMPENING_FOR_MOVE_PRIORITIZATION();

	// cache TypeInfos rather than doing a hash map look up of the string every time it is being used
	m_CachedInfoTypes[eTACTICAL_UNASSIGNED] = GC.getInfoTypeForString("TACTICAL_UNASSIGNED");
	m_CachedInfoTypes[eTACTICAL_POSTURE_SIT_AND_BOMBARD] = GC.getInfoTypeForString("TACTICAL_POSTURE_SIT_AND_BOMBARD");
	m_CachedInfoTypes[eTACTICAL_POSTURE_ATTRIT_FROM_RANGE] = GC.getInfoTypeForString("TACTICAL_POSTURE_ATTRIT_FROM_RANGE");
	m_CachedInfoTypes[eTACTICAL_POSTURE_EXPLOIT_FLANKS] = GC.getInfoTypeForString("TACTICAL_POSTURE_EXPLOIT_FLANKS");
	m_CachedInfoTypes[eTACTICAL_POSTURE_STEAMROLL] = GC.getInfoTypeForString("TACTICAL_POSTURE_STEAMROLL");
	m_CachedInfoTypes[eTACTICAL_POSTURE_SURGICAL_CITY_STRIKE] = GC.getInfoTypeForString("TACTICAL_POSTURE_SURGICAL_CITY_STRIKE");
	m_CachedInfoTypes[eTACTICAL_POSTURE_COUNTERATTACK] = GC.getInfoTypeForString("TACTICAL_POSTURE_COUNTERATTACK");
	m_CachedInfoTypes[eTACTICAL_MOVE_NONCOMBATANTS_TO_SAFETY] = GC.getInfoTypeForString("TACTICAL_MOVE_NONCOMBATANTS_TO_SAFETY");
	m_CachedInfoTypes[eTACTICAL_CAPTURE_CITY] = GC.getInfoTypeForString("TACTICAL_CAPTURE_CITY");
	m_CachedInfoTypes[eTACTICAL_DAMAGE_CITY] = GC.getInfoTypeForString("TACTICAL_DAMAGE_CITY");
	m_CachedInfoTypes[eTACTICAL_DESTROY_HIGH_UNIT] = GC.getInfoTypeForString("TACTICAL_DESTROY_HIGH_UNIT");
	m_CachedInfoTypes[eTACTICAL_DESTROY_MEDIUM_UNIT] = GC.getInfoTypeForString("TACTICAL_DESTROY_MEDIUM_UNIT");
	m_CachedInfoTypes[eTACTICAL_DESTROY_LOW_UNIT] = GC.getInfoTypeForString("TACTICAL_DESTROY_LOW_UNIT");
	m_CachedInfoTypes[eTACTICAL_TO_SAFETY] = GC.getInfoTypeForString("TACTICAL_TO_SAFETY");
	m_CachedInfoTypes[eTACTICAL_ATTRIT_HIGH_UNIT] = GC.getInfoTypeForString("TACTICAL_ATTRIT_HIGH_UNIT");
	m_CachedInfoTypes[eTACTICAL_ATTRIT_MEDIUM_UNIT] = GC.getInfoTypeForString("TACTICAL_ATTRIT_MEDIUM_UNIT");
	m_CachedInfoTypes[eTACTICAL_ATTRIT_LOW_UNIT] = GC.getInfoTypeForString("TACTICAL_ATTRIT_LOW_UNIT");
	m_CachedInfoTypes[eTACTICAL_REPOSITION] = GC.getInfoTypeForString("TACTICAL_REPOSITION");
	m_CachedInfoTypes[eTACTICAL_BARBARIAN_CAMP] = GC.getInfoTypeForString("TACTICAL_BARBARIAN_CAMP");
	m_CachedInfoTypes[eTACTICAL_PILLAGE] = GC.getInfoTypeForString("TACTICAL_PILLAGE");
	m_CachedInfoTypes[eTACTICAL_ATTACK_VERY_HIGH_PRIORITY_CIVILIAN] = GC.getInfoTypeForString("TACTICAL_ATTACK_VERY_HIGH_PRIORITY_CIVILIAN");
	m_CachedInfoTypes[eTACTICAL_ATTACK_HIGH_PRIORITY_CIVILIAN] = GC.getInfoTypeForString("TACTICAL_ATTACK_HIGH_PRIORITY_CIVILIAN");
	m_CachedInfoTypes[eTACTICAL_ATTACK_MEDIUM_PRIORITY_CIVILIAN] = GC.getInfoTypeForString("TACTICAL_ATTACK_MEDIUM_PRIORITY_CIVILIAN");
	m_CachedInfoTypes[eTACTICAL_ATTACK_LOW_PRIORITY_CIVILIAN] = GC.getInfoTypeForString("TACTICAL_ATTACK_LOW_PRIORITY_CIVILIAN");
	m_CachedInfoTypes[eTACTICAL_SAFE_BOMBARDS] = GC.getInfoTypeForString("TACTICAL_SAFE_BOMBARDS");
	m_CachedInfoTypes[eTACTICAL_HEAL] = GC.getInfoTypeForString("TACTICAL_HEAL");
	m_CachedInfoTypes[eTACTICAL_ANCIENT_RUINS] = GC.getInfoTypeForString("TACTICAL_ANCIENT_RUINS");
	m_CachedInfoTypes[eTACTICAL_GARRISON_TO_ALLOW_BOMBARD] = GC.getInfoTypeForString("TACTICAL_GARRISON_TO_ALLOW_BOMBARD");
	m_CachedInfoTypes[eTACTICAL_GARRISON_ALREADY_THERE] = GC.getInfoTypeForString("TACTICAL_GARRISON_ALREADY_THERE");
	m_CachedInfoTypes[eTACTICAL_BASTION_ALREADY_THERE] = GC.getInfoTypeForString("TACTICAL_BASTION_ALREADY_THERE");
	m_CachedInfoTypes[eTACTICAL_GUARD_IMPROVEMENT_ALREADY_THERE] = GC.getInfoTypeForString("TACTICAL_GUARD_IMPROVEMENT_ALREADY_THERE");
	m_CachedInfoTypes[eTACTICAL_GARRISON_1_TURN] = GC.getInfoTypeForString("TACTICAL_GARRISON_1_TURN");
	m_CachedInfoTypes[eTACTICAL_BASTION_1_TURN] = GC.getInfoTypeForString("TACTICAL_BASTION_1_TURN");
	m_CachedInfoTypes[eTACTICAL_GUARD_IMPROVEMENT_1_TURN] = GC.getInfoTypeForString("TACTICAL_GUARD_IMPROVEMENT_1_TURN");
	m_CachedInfoTypes[eTACTICAL_AIR_INTERCEPT] = GC.getInfoTypeForString("TACTICAL_AIR_INTERCEPT");
	m_CachedInfoTypes[eTACTICAL_AIR_SWEEP] = GC.getInfoTypeForString("TACTICAL_AIR_SWEEP");
	m_CachedInfoTypes[eTACTICAL_POSTURE_HEDGEHOG] = GC.getInfoTypeForString("TACTICAL_POSTURE_HEDGEHOG");
	m_CachedInfoTypes[eTACTICAL_POSTURE_WITHDRAW] = GC.getInfoTypeForString("TACTICAL_POSTURE_WITHDRAW");
	m_CachedInfoTypes[eTACTICAL_POSTURE_SHORE_BOMBARDMENT] = GC.getInfoTypeForString("TACTICAL_POSTURE_SHORE_BOMBARDMENT");
	m_CachedInfoTypes[eTACTICAL_CLOSE_ON_TARGET] = GC.getInfoTypeForString("TACTICAL_CLOSE_ON_TARGET");
	m_CachedInfoTypes[eTACTICAL_MOVE_OPERATIONS] = GC.getInfoTypeForString("TACTICAL_MOVE_OPERATIONS");
	m_CachedInfoTypes[eTACTICAL_EMERGENCY_PURCHASES] = GC.getInfoTypeForString("TACTICAL_EMERGENCY_PURCHASES");
	m_CachedInfoTypes[eTACTICAL_ESCORT_EMBARKED_UNIT] = GC.getInfoTypeForString("TACTICAL_ESCORT_EMBARKED_UNIT");
	m_CachedInfoTypes[eTACTICAL_PLUNDER_TRADE_UNIT] = GC.getInfoTypeForString("TACTICAL_PLUNDER_TRADE_UNIT");
	m_CachedInfoTypes[eTACTICAL_PARK_ON_TRADE_ROUTE] = GC.getInfoTypeForString("TACTICAL_PARK_ON_TRADE_ROUTE");
	m_CachedInfoTypes[eTACTICAL_DEFENSIVE_AIRLIFT] = GC.getInfoTypeForString("TACTICAL_DEFENSIVE_AIRLIFT");
	m_CachedInfoTypes[eTACTICAL_PILLAGE_CITADEL] = GC.getInfoTypeForString("TACTICAL_PILLAGE_CITADEL");
	m_CachedInfoTypes[eTACTICAL_PILLAGE_RESOURCE] = GC.getInfoTypeForString("TACTICAL_PILLAGE_RESOURCE");
	m_CachedInfoTypes[eTACTICAL_PILLAGE_CITADEL_NEXT_TURN] = GC.getInfoTypeForString("TACTICAL_PILLAGE_CITADEL_NEXT_TURN");
	m_CachedInfoTypes[eTACTICAL_PILLAGE_RESOURCE_NEXT_TURN] = GC.getInfoTypeForString("TACTICAL_PILLAGE_RESOURCE_NEXT_TURN");
	m_CachedInfoTypes[eTACTICAL_PILLAGE_NEXT_TURN] = GC.getInfoTypeForString("TACTICAL_PILLAGE_NEXT_TURN");
	m_CachedInfoTypes[eMUPOSITION_CIVILIAN_SUPPORT] = GC.getInfoTypeForString("MUPOSITION_CIVILIAN_SUPPORT");
	m_CachedInfoTypes[eMUPOSITION_NAVAL_ESCORT] = GC.getInfoTypeForString("MUPOSITION_NAVAL_ESCORT");
	m_CachedInfoTypes[eMUPOSITION_BOMBARD] = GC.getInfoTypeForString("MUPOSITION_BOMBARD");
	m_CachedInfoTypes[eMUPOSITION_FRONT_LINE] = GC.getInfoTypeForString("MUPOSITION_FRONT_LINE");

#if defined(MOD_BALANCE_CORE)
	//needed for better debugging - can't use ID here because it's not set yet!
	m_CurrentMoveHighPriorityUnits.setPlayer(pPlayer);
	m_CurrentMoveUnits.setPlayer(pPlayer);
#endif
}

/// Deallocate memory created in initialize
void CvTacticalAI::Uninit()
{
}

/// Reset variables
void CvTacticalAI::Reset()
{
	m_tacticalMap.Init(m_pPlayer ? m_pPlayer->GetID() : NO_PLAYER);
}

/// Serialization read
void CvTacticalAI::Read(FDataStream& kStream)
{
	// Version number to maintain backwards compatibility
	uint uiVersion;
	kStream >> uiVersion;
	MOD_SERIALIZE_INIT_READ(kStream);

	int phony = 0;
	kStream >> phony;
	kStream >> m_TempZones;
	kStream >> m_tacticalMap;

#if defined(MOD_BALANCE_CORE)
	int iCount;
	m_HealingUnits.clear();
	kStream >> iCount;
	for (int i=0; i<iCount; i++)
	{
		int iTemp;
		kStream >> iTemp;
		m_HealingUnits.insert(iTemp);
	}
	m_Postures.clear();
	kStream >> iCount;
	for (int i=0; i<iCount; i++)
	{
		int iPlayer, iCityID, iPosture;
		bool bWater;
		kStream >> iPlayer;
		kStream >> iCityID;
		kStream >> iPosture;
		kStream >> bWater;
		CvTacticalPosture temp( (PlayerTypes)iPlayer, bWater, iCityID, (AITacticalPosture)iPosture );
		m_Postures.push_back(temp);
	}
#endif
}

/// Serialization write
void CvTacticalAI::Write(FDataStream& kStream)
{
	// Current version number
	uint uiVersion = 1;
	kStream << uiVersion;
	MOD_SERIALIZE_INIT_WRITE(kStream);
	int phony = 0;
	kStream << phony;
	kStream << m_TempZones;
	kStream << m_tacticalMap;

#if defined(MOD_BALANCE_CORE)
	kStream << m_HealingUnits.size();
	for (std::set<int>::iterator it=m_HealingUnits.begin(); it!=m_HealingUnits.end(); ++it)
		kStream << *it;

	kStream << m_Postures.size();
	for(unsigned int iI = 0; iI < m_Postures.size(); iI++)
	{
		kStream << m_Postures[iI].GetPlayer();
		kStream << m_Postures[iI].GetCityID();
		kStream << m_Postures[iI].GetPosture();
		kStream << m_Postures[iI].IsWater();
	}
#endif
}

/// Mark all the units that will be under tactical AI control this turn
void CvTacticalAI::CommandeerUnits()
{
	int iLoop;

	m_CurrentTurnUnits.clear();

	// Loop through our units
	for(CvUnit* pLoopUnit = m_pPlayer->firstUnit(&iLoop); pLoopUnit; pLoopUnit = m_pPlayer->nextUnit(&iLoop))
	{
		// debugging hook
		if (g_currentUnitToTrack == pLoopUnit->GetID())
		{
			pLoopUnit->DumpDangerInNeighborhood();
		}

		// reset mission AI so we don't see stale information
		if (pLoopUnit->GetMissionAIType()==MISSIONAI_ASSAULT)
			pLoopUnit->SetMissionAI(NO_MISSIONAI,NULL,NULL);

		//if we cannot heal in the capital, we can heal nowhere ...
		CvCity* pCapital = m_pPlayer->getCapitalCity();
		bool bCanHeal = pCapital ? pLoopUnit->healRate( pCapital->plot() ) > 0 : false;
		bool bHasTarget = TacticalAIHelpers::IsCaptureTargetInRange(pLoopUnit);

		// is the unit healing?
		if (m_HealingUnits.find( pLoopUnit->GetID() ) != m_HealingUnits.end())
		{
			if ( pLoopUnit->getDamage()>30 && bCanHeal && !bHasTarget )
				//need to continue healing
				continue;
			else
				//done healing
				m_HealingUnits.erase( pLoopUnit->GetID() );
		}
		else if (bCanHeal && !bHasTarget)
		{
			//does it need healing? unless barbarian or japanese!
			if ((pLoopUnit->getDamage()>80 || pLoopUnit->isProjectedToDieNextTurn()) && !m_pPlayer->isBarbarian() && !m_pPlayer->GetPlayerTraits()->IsFightWellDamaged() && !pLoopUnit->IsStrongerDamaged())
			{
				//need to start healing
				m_HealingUnits.insert( pLoopUnit->GetID() );
				continue;
			}
		}

		// Never want immobile/dead units, explorers, ones that have already moved or automated human units
		if(pLoopUnit->TurnProcessed() || pLoopUnit->isDelayedDeath() || !pLoopUnit->canMove() || 
			pLoopUnit->AI_getUnitAIType() == UNITAI_UNKNOWN ||  pLoopUnit->AI_getUnitAIType() == UNITAI_EXPLORE || pLoopUnit->isHuman())
		{
			continue;
		}
#if defined(MOD_BALANCE_CORE)
		//Don't poach garrisons.
		if(pLoopUnit->getArmyID() == -1)
		{
			if(pLoopUnit->getHomelandMove() == AI_HOMELAND_MOVE_GARRISON || pLoopUnit->getHomelandMove() == AI_HOMELAND_MOVE_GARRISON_CITY_STATE || pLoopUnit->IsGarrisoned())
				continue;
		}
#endif

		// We want ALL the barbarians and air units (that are combat ready)
		if(pLoopUnit->isBarbarian() || (pLoopUnit->getDomainType() == DOMAIN_AIR && pLoopUnit->getDamage() < 50 && !ShouldRebase(pLoopUnit)))
		{
			if (pLoopUnit->getTacticalMove() == NO_TACTICAL_MOVE)
			{
				pLoopUnit->setTacticalMove((TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_UNASSIGNED]);
			}
			m_CurrentTurnUnits.push_back(pLoopUnit->GetID());
		}

		// Now down to land and sea units ... in these groups our unit must have a base combat strength ... or be a great general
		else if( !pLoopUnit->IsCombatUnit() && !pLoopUnit->IsGreatGeneral() && !pLoopUnit->IsGreatAdmiral() && !pLoopUnit->IsCityAttackSupport())
		{
			continue;
		}
		else
		{
			 //if it's a general or admiral and not a field commander, we don't want it
			if( pLoopUnit->IsGreatGeneral() || pLoopUnit->IsGreatAdmiral() || pLoopUnit->IsCityAttackSupport())
			{
				GreatPeopleDirectiveTypes eDirective = pLoopUnit->GetGreatPeopleDirective();
				if (eDirective != GREAT_PEOPLE_DIRECTIVE_FIELD_COMMAND)
				{
					continue;
				}
			}
			bool bNearVisibleEnemy = NearVisibleEnemy(pLoopUnit, GetRecruitRange());
			// Is this one in an operation we can't interrupt?
			int iArmyID = pLoopUnit->getArmyID();
			const CvArmyAI* army = m_pPlayer->getArmyAI(iArmyID);
			if(iArmyID != -1 && NULL != army && (!army->CanTacticalAIInterruptUnit(pLoopUnit->GetID()) && !bNearVisibleEnemy))
			{
				pLoopUnit->setTacticalMove((TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_MOVE_OPERATIONS]);
			}
			else
			{
				// Non-zero danger value or near enemy, or recently deployed out of an operation?
				int iDanger = m_pPlayer->GetPlotDanger(*(pLoopUnit->plot()),pLoopUnit);
				if (iDanger > 0 || bNearVisibleEnemy || pLoopUnit->IsRecentlyDeployedFromOperation())
				{
					if (iArmyID != -1)
					{
						pLoopUnit->setTacticalMove((TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_MOVE_OPERATIONS]);
					}
					else
					{
						if (pLoopUnit->getTacticalMove() == NO_TACTICAL_MOVE)
							pLoopUnit->setTacticalMove((TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_UNASSIGNED]);
					}
					m_CurrentTurnUnits.push_back(pLoopUnit->GetID());
				}
				else if (pLoopUnit->canParadrop(pLoopUnit->plot(),false))
				{
					if (pLoopUnit->getTacticalMove()==NO_TACTICAL_MOVE)
						pLoopUnit->setTacticalMove((TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_UNASSIGNED]);
					m_CurrentTurnUnits.push_back(pLoopUnit->GetID());
				}
			}
		}
	}

#if defined(MOD_CORE_DEBUGGING)
	if (MOD_CORE_DEBUGGING)
	{
		for(list<int>::iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); ++it)
		{
			CvUnit* pUnit = m_pPlayer->getUnit(*it);
			CvString msg = CvString::format("current turn tactical unit %s %d at %d,%d\n", pUnit->getName().c_str(), pUnit->GetID(), pUnit->getX(), pUnit->getY() );
			LogTacticalMessage( msg );
		}
	}
#endif
}

/// Set up for a turn of tactical moves
void CvTacticalAI::DoTurn()
{
}

/// Update the AI for units
void CvTacticalAI::Update()
{
	AI_PERF_FORMAT("AI-perf.csv", ("Tactical AI, Turn %03d, %s", GC.getGame().getElapsedGameTurns(), m_pPlayer->getCivilizationShortDescription()) );

	//do this after updating the danger plots (happens in CvPlayer::doTurnPostDiplomacy)
	m_tacticalMap.Refresh();

	FindTacticalTargets();

	// Loop through each dominance zone assigning moves
	ProcessDominanceZones();
}

#if defined(MOD_BALANCE_CORE)
CvCity* CvTacticalAI::GetNearestTargetCity(CvPlot* pPlot)
{
	CvCity* pBestCity = NULL;
	int iBestValue = 0;
	if(pPlot == NULL)
		return pBestCity;
	
	CvTacticalDominanceZone* pZone;
	CvCity* pClosestCity = NULL;

	// Loop through all the zones we have this turn
	for(int iI = 0; iI < GetTacticalAnalysisMap()->GetNumZones(); iI++)
	{
		pZone = GetTacticalAnalysisMap()->GetZoneByIndex(iI);

		// Check to make sure we want to use this zone
		if(UseThisDominanceZone(pZone))
		{
			pClosestCity = pZone->GetZoneCity();

			if(pClosestCity != NULL && pClosestCity->getOwner() != m_pPlayer->GetID() && GET_TEAM(m_pPlayer->getTeam()).isAtWar(pClosestCity->getTeam()))
			{
				int iValue = 100;
				int iDistance = plotDistance(pClosestCity->getX(), pClosestCity->getY(), pPlot->getX(), pPlot->getY());
				iValue -= (iDistance * 3);

				if(pZone->GetFriendlyStrength() > 0)
				{
					iValue += pZone->GetFriendlyStrength();
				}
				if(IsTemporaryZoneCity(pClosestCity))
				{
					iValue *= 5;
				}
				if(iValue > iBestValue)
				{
					iBestValue = iValue;
					pBestCity = pClosestCity;
				}
			}
		}
	}
	return pBestCity;
}
#endif

// TEMPORARY DOMINANCE ZONES

/// Retrieve first temporary dominance zone (follow with calls to GetNextTemporaryZone())
CvTemporaryZone* CvTacticalAI::GetFirstTemporaryZone()
{
	CvTemporaryZone* pRtnValue = NULL;

	m_iCurrentTempZoneIndex = 0;
	if((int)m_TempZones.size() > m_iCurrentTempZoneIndex)
	{
		pRtnValue = &m_TempZones[m_iCurrentTempZoneIndex];
	}

	return pRtnValue;
}

/// Retrieve next temporary dominance zone, NULL if no more (should follow a call to GetFirstTemporaryZone())
CvTemporaryZone* CvTacticalAI::GetNextTemporaryZone()
{
	CvTemporaryZone* pRtnValue = NULL;

	m_iCurrentTempZoneIndex++;
	if((int)m_TempZones.size() > m_iCurrentTempZoneIndex)
	{
		pRtnValue = &m_TempZones[m_iCurrentTempZoneIndex];
	}

	return pRtnValue;
}

/// Add a temporary dominance zone around a short-term target
void CvTacticalAI::AddTemporaryZone(CvTemporaryZone zone)
{
	m_TempZones.push_back(zone);
}

/// Remove a temporary dominance zone we no longer need to track
void CvTacticalAI::DeleteTemporaryZone(CvPlot* pPlot)
{
	FStaticVector<CvTemporaryZone, SAFE_ESTIMATE_NUM_TEMP_ZONES, true, c_eCiv5GameplayDLL, 0> zonesCopy;

	// Copy zones over
	zonesCopy = m_TempZones;

	m_TempZones.clear();

	// Copy back to original vector any whose coords don't match
	for(unsigned int iI = 0; iI < zonesCopy.size(); iI++)
	{
		if(zonesCopy[iI].GetX() != pPlot->getX() ||
		        zonesCopy[iI].GetY() != pPlot->getY())
		{
			m_TempZones.push_back(zonesCopy[iI]);
		}
	}
}

/// Remove temporary zones that have expired
void CvTacticalAI::DropObsoleteZones()
{
	FStaticVector<CvTemporaryZone, SAFE_ESTIMATE_NUM_TEMP_ZONES, true, c_eCiv5GameplayDLL, 0> zonesCopy;

	// Copy zones over
	zonesCopy = m_TempZones;

	m_TempZones.clear();

	// Copy back to original vector any that haven't expired
	for(unsigned int iI = 0; iI < zonesCopy.size(); iI++)
	{
		if(zonesCopy[iI].GetLastTurn() >= GC.getGame().getGameTurn())
		{
			m_TempZones.push_back(zonesCopy[iI]);
		}
	}
}

/// Is this a city that an operation just deployed in front of?
bool CvTacticalAI::IsTemporaryZoneCity(CvCity* pCity)
{
	for(unsigned int iI = 0; iI < m_TempZones.size(); iI++)
	{
		if(m_TempZones[iI].GetX() == pCity->getX() &&
		        m_TempZones[iI].GetY() == pCity->getY() &&
		        m_TempZones[iI].GetTargetType() == AI_TACTICAL_TARGET_CITY)
		{
			return true;
		}
	}
	return false;
}

/// Pushes the mission to launch an attack and logs this activity
bool CvTacticalAI::PerformAttack(CvCity* pAttacker, CvTacticalTarget* pTarget)
{
	if(GC.getLogging() && GC.getAILogging())
	{
		CvString strMsg;
		CvString strTemp = pAttacker->getName();
		int iX = pAttacker->getX();
		int	iY = pAttacker->getY();
		strMsg.Format("Made ranged attack with %s, at X: %d, Y: %d, vs. target at X: %d, Y: %d", strTemp.GetCString(), iX, iY, pTarget->GetTargetX(), pTarget->GetTargetY());

		LogTacticalMessage(strMsg);
	}

	pAttacker->doTask(TASK_RANGED_ATTACK, pTarget->GetTargetX(), pTarget->GetTargetY(), 0);

	return !pTarget->IsTargetStillAlive(m_pPlayer->GetID());
}

bool CvTacticalAI::PerformAttack(CvUnit* pAttacker, CvTacticalTarget* pTarget)
{
	bool bSuccess = false; //killed the target
	int iAttacksLeft = pAttacker->getNumAttacks() - pAttacker->getNumAttacksMadeThisTurn();
	int iMovesLeft = pAttacker->getMoves();

	if(GC.getLogging() && GC.getAILogging())
	{
		CvString strMsg;
		CvString strTemp = pAttacker->getUnitInfo().GetDescription();
		int iX = pAttacker->getX();
		int	iY = pAttacker->getY();

		if (iAttacksLeft<1 || iMovesLeft<1)
		{
			strMsg.Format("Cannot attack with %s, at X: %d, Y: %d, vs. target at X: %d, Y: %d", strTemp.GetCString(), iX, iY, pTarget->GetTargetX(), pTarget->GetTargetY());
		}
		else
		{
			if(pAttacker->isRanged())
				strMsg.Format("Making ranged attack with %s, at X: %d, Y: %d, vs. target at X: %d, Y: %d", strTemp.GetCString(), iX, iY, pTarget->GetTargetX(), pTarget->GetTargetY());
			else
				strMsg.Format("Making melee attack with %s, at X: %d, Y: %d, vs. target at X: %d, Y: %d", strTemp.GetCString(), iX, iY, pTarget->GetTargetX(), pTarget->GetTargetY());
		}

		LogTacticalMessage(strMsg);
	}

	CvPlot* pTargetPlot = GC.getMap().plot( pTarget->GetTargetX(), pTarget->GetTargetY() );

	while (iMovesLeft>0 && iAttacksLeft>0 && !pAttacker->isDelayedDeath())
	{
		//just for inspection in GUI
		pAttacker->SetMissionAI(MISSIONAI_ASSAULT,pTargetPlot,NULL);

		// Height of stupidity: Air attack is ranged, but it goes through the 'move to' mission.
		if(pAttacker->isRanged() && pAttacker->getDomainType() != DOMAIN_AIR)
			pAttacker->PushMission(CvTypes::getMISSION_RANGE_ATTACK(), pTarget->GetTargetX(), pTarget->GetTargetY());
		else
			pAttacker->PushMission(CvTypes::getMISSION_MOVE_TO(), pTarget->GetTargetX(), pTarget->GetTargetY());

		iMovesLeft -= pAttacker->canMoveAfterAttacking() ? GC.getMOVE_DENOMINATOR() : iMovesLeft;
		iAttacksLeft -= 1;

		//ranged units can't capture, so give up when a city is down to one hitpoint
		if (!pTarget->IsTargetStillAlive(m_pPlayer->GetID()) || (pAttacker->isRanged() && pTarget->IsReadyForCapture()))
		{
			bSuccess = true;
			break;
		}
	}

	//parthian move. retreat after attacking
	if (iMovesLeft>0 && iAttacksLeft<1 && !pAttacker->isDelayedDeath())
	{
		CvPlot* pBestPlot = TacticalAIHelpers::FindSafestPlotInReach(pAttacker,true);
		if(pBestPlot != NULL)
		{
			pAttacker->PushMission(CvTypes::getMISSION_MOVE_TO(), pBestPlot->getX(), pBestPlot->getY(), CvUnit::MOVEFLAG_IGNORE_DANGER);

			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strMsg;
				CvString strTemp = pAttacker->getUnitInfo().GetDescription();
				strMsg.Format("Retreated after attack with %s, at X: %d, Y: %d, vs. target at X: %d, Y: %d", 
					strTemp.GetCString(), pAttacker->getX(), pAttacker->getY(), pTarget->GetTargetX(), pTarget->GetTargetY());
				LogTacticalMessage(strMsg);
			}
		}
	}

	//we've given our best
	if(iMovesLeft<1)
		UnitProcessed(pAttacker->GetID());

	return bSuccess;
}

// PRIVATE METHODS

/// Establish postures for each dominance zone (taking into account last posture)
void CvTacticalAI::UpdatePostures()
{
	CvTacticalDominanceZone* pZone;
	AITacticalPosture eLastPosture, eNewPosture;

	FStaticVector<CvTacticalPosture, SAFE_ESTIMATE_NUM_CITIES, true, c_eCiv5GameplayDLL, 0> newPostures;

	// Loop through all the zones we have this turn
	for(int iI = 0; iI < GetTacticalAnalysisMap()->GetNumZones(); iI++)
	{
		pZone = GetTacticalAnalysisMap()->GetZoneByIndex(iI);

		// Check to make sure we want to use this zone
		if(UseThisDominanceZone(pZone))
		{
			int iCityID = -1;
			if(pZone->GetZoneCity() != NULL)
			{
				iCityID = pZone->GetZoneCity()->GetID();
			}

			eLastPosture = FindPosture(pZone);
#if defined(MOD_BALANCE_CORE)
			if (pZone->IsWater())
			{
				eNewPosture = SelectNavalPosture(pZone, eLastPosture);
			}
			else
			{
#endif
				eNewPosture = SelectPosture(pZone, eLastPosture);
#if defined(MOD_BALANCE_CORE)
			}
#endif
			newPostures.push_back(CvTacticalPosture(pZone->GetOwner(), pZone->IsWater(), iCityID, eNewPosture));

			if(GC.getLogging() && GC.getAILogging() && eNewPosture != AI_TACTICAL_POSTURE_NONE)
			{
				CvString szPostureMsg;
				szPostureMsg.Format("Zone ID: %d, %s, ", pZone->GetDominanceZoneID(), pZone->GetZoneCity() ? pZone->GetZoneCity()->getName().c_str() : "none");

				switch(eNewPosture)
				{
				case AI_TACTICAL_POSTURE_SIT_AND_BOMBARD:
					szPostureMsg += "Sit and Bombard";
					break;
				case AI_TACTICAL_POSTURE_ATTRIT_FROM_RANGE:
					szPostureMsg += "Attrit from Range";
					break;
				case AI_TACTICAL_POSTURE_EXPLOIT_FLANKS:
					szPostureMsg += "Exploit Flanks";
					break;
				case AI_TACTICAL_POSTURE_STEAMROLL:
					szPostureMsg += "Steamroll";
					break;
				case AI_TACTICAL_POSTURE_SURGICAL_CITY_STRIKE:
					szPostureMsg += "Surgical City Strike";
					break;
				case AI_TACTICAL_POSTURE_HEDGEHOG:
					szPostureMsg += "Hedgehog";
					break;
				case AI_TACTICAL_POSTURE_COUNTERATTACK:
					szPostureMsg += "Counterattack";
					break;
				case AI_TACTICAL_POSTURE_WITHDRAW:
					szPostureMsg += "Withdraw";
					break;
				case AI_TACTICAL_POSTURE_SHORE_BOMBARDMENT:
					szPostureMsg += "Shore Bombardment";
					break;
				}
				LogTacticalMessage(szPostureMsg);
			}
		}
	}

	// New postures become current ones
	m_Postures.clear();
	m_Postures = newPostures;
}

/// Select a posture for a specific zone
AITacticalPosture CvTacticalAI::SelectPosture(CvTacticalDominanceZone* pZone, AITacticalPosture eLastPosture)
{
	AITacticalPosture eChosenPosture = AI_TACTICAL_POSTURE_NONE;
	int iDominancePercentage = GetTacticalAnalysisMap()->GetDominancePercentage();
	eTacticalDominanceFlags eRangedDominance = TACTICAL_DOMINANCE_EVEN;
	eTacticalDominanceFlags eUnitCountDominance = TACTICAL_DOMINANCE_EVEN;
	int iRatio;

	// Compute who is dominant in various areas
	//   Ranged strength
#if defined(MOD_BALANCE_CORE_MILITARY)
	if (((pZone->GetEnemyRangedStrength() <= 0) || (pZone->GetEnemyRangedUnitCount() <= 0)) && ((pZone->GetFriendlyRangedStrength() > 0) || (pZone->GetFriendlyRangedUnitCount() > 0)))
#else
	if(pZone->GetEnemyRangedStrength() <= 0)
#endif
	{
		eRangedDominance = TACTICAL_DOMINANCE_FRIENDLY;
	}
	else
	{
		iRatio = (pZone->GetFriendlyRangedStrength() * 100) / max(1, pZone->GetEnemyRangedStrength());
		if(iRatio > 100 + iDominancePercentage)
		{
			eRangedDominance = TACTICAL_DOMINANCE_FRIENDLY;
		}
		else if(iRatio < 100 - iDominancePercentage)
		{
			eRangedDominance = TACTICAL_DOMINANCE_ENEMY;
		}
	}

	//   Number of units
	if(pZone->GetEnemyUnitCount() <= 0 && pZone->GetFriendlyUnitCount() > 0)
	{
		eUnitCountDominance = TACTICAL_DOMINANCE_FRIENDLY;
	}
	else
	{
		iRatio = (pZone->GetFriendlyUnitCount()  * 100) / max(1, pZone->GetEnemyUnitCount());
		if(iRatio > 100 + iDominancePercentage)
		{
			eUnitCountDominance = TACTICAL_DOMINANCE_FRIENDLY;
		}
		else if(iRatio < 100 - iDominancePercentage)
		{
			eUnitCountDominance = TACTICAL_DOMINANCE_ENEMY;
		}
	}

	// Choice based on whose territory this is
	switch(pZone->GetTerritoryType())
	{
	case TACTICAL_TERRITORY_ENEMY:
	case TACTICAL_TERRITORY_TEMP_ZONE:
	{
		bool bTemporaryZone = false;
		CvCity *pClosestCity = pZone->GetZoneCity();

		// Always push again for a few turns after each operation reaches destination
		if (pClosestCity)
		{
			bTemporaryZone = IsTemporaryZoneCity(pClosestCity);
		}
		// Withdraw if enemy dominant overall or we have nothing but ranged left
		if(!bTemporaryZone && (pZone->GetDominanceFlag() == TACTICAL_DOMINANCE_ENEMY || pZone->GetFriendlyRangedUnitCount() == pZone->GetFriendlyUnitCount()))
		{
			eChosenPosture = AI_TACTICAL_POSTURE_WITHDRAW;
		}
		// Temporary zone: want Steamroll or Surgical Strike so we close in on city
		else if (bTemporaryZone)
		{
			if (pZone->GetDominanceFlag() == TACTICAL_DOMINANCE_ENEMY)
			{
				eChosenPosture = AI_TACTICAL_POSTURE_STEAMROLL;
			}
			else
			{
				eChosenPosture = AI_TACTICAL_POSTURE_SURGICAL_CITY_STRIKE;
			}
		}

		// Destroy units then assault - for first time need dominance in total strength but not enemy dominance in ranged units OR just double total strength
		else if(pZone->GetEnemyUnitCount() > 0 && pZone->GetDominanceFlag() == TACTICAL_DOMINANCE_FRIENDLY &&
		        (eRangedDominance != TACTICAL_DOMINANCE_ENEMY || pZone->GetFriendlyStrength() > pZone->GetEnemyStrength() * 2))
		{
			eChosenPosture = AI_TACTICAL_POSTURE_STEAMROLL;
		}

		//                 - less stringent if continuing this from a previous turn
		else if(eLastPosture == AI_TACTICAL_POSTURE_STEAMROLL && pZone->GetDominanceFlag() == TACTICAL_DOMINANCE_FRIENDLY && pZone->GetEnemyUnitCount() > 0)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_STEAMROLL;
		}

		// Sit and bombard - for first time need dominance in ranged strength and total unit count
#if defined(MOD_BALANCE_CORE)
		//We need to have plenty of ranged units in order for this to make sense.
		else if(eRangedDominance == TACTICAL_DOMINANCE_FRIENDLY && eUnitCountDominance != TACTICAL_DOMINANCE_ENEMY && pZone->GetFriendlyRangedStrength() >= (pZone->GetFriendlyStrength() / 2))
#else
		else if(eRangedDominance == TACTICAL_DOMINANCE_FRIENDLY && eUnitCountDominance != TACTICAL_DOMINANCE_ENEMY)
#endif
		{
			eChosenPosture = AI_TACTICAL_POSTURE_SIT_AND_BOMBARD;
		}

		//                 - less stringent if continuing this from a previous turn
#if defined(MOD_BALANCE_CORE)
		//We need to have plenty of ranged units in order for this to make sense.
		else if(eLastPosture == AI_TACTICAL_POSTURE_SIT_AND_BOMBARD && eRangedDominance != TACTICAL_DOMINANCE_ENEMY && eUnitCountDominance != TACTICAL_DOMINANCE_ENEMY && pZone->GetFriendlyRangedStrength() >= (pZone->GetFriendlyStrength() / 2))
#else
		else if(eLastPosture == AI_TACTICAL_POSTURE_SIT_AND_BOMBARD && eRangedDominance != TACTICAL_DOMINANCE_ENEMY && eUnitCountDominance != TACTICAL_DOMINANCE_ENEMY)
#endif
		{
			eChosenPosture = AI_TACTICAL_POSTURE_SIT_AND_BOMBARD;
		}

		// Go right after the city - need tactical dominance
		else if(pZone->GetDominanceFlag() == TACTICAL_DOMINANCE_FRIENDLY)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_SURGICAL_CITY_STRIKE;
		}

		// Exploit flanks - for first time need dominance in unit count
		else if(eUnitCountDominance == TACTICAL_DOMINANCE_FRIENDLY && pZone->GetEnemyUnitCount() > 1)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_EXPLOIT_FLANKS;
		}

		//                 - less stringent if continuing this from a previous turn
		else if(eLastPosture == AI_TACTICAL_POSTURE_EXPLOIT_FLANKS && eUnitCountDominance != TACTICAL_DOMINANCE_ENEMY && pZone->GetEnemyUnitCount() > 1)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_EXPLOIT_FLANKS;
		}

		// Default for this zone
		else
		{
			eChosenPosture = AI_TACTICAL_POSTURE_STEAMROLL;
		}
		break;
	}

	case TACTICAL_TERRITORY_NEUTRAL:
	case TACTICAL_TERRITORY_NO_OWNER:
	{
		if(eRangedDominance == TACTICAL_DOMINANCE_FRIENDLY && eUnitCountDominance != TACTICAL_DOMINANCE_ENEMY)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_ATTRIT_FROM_RANGE;
		}

		//                 - less stringent if continuing this from a previous turn
		else if(eLastPosture == AI_TACTICAL_POSTURE_ATTRIT_FROM_RANGE && eRangedDominance != TACTICAL_DOMINANCE_ENEMY)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_ATTRIT_FROM_RANGE;
		}

		// Exploit flanks - for first time need dominance in unit count
		else if(eUnitCountDominance == TACTICAL_DOMINANCE_FRIENDLY && pZone->GetEnemyUnitCount() > 0)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_EXPLOIT_FLANKS;
		}

		//                 - less stringent if continuing this from a previous turn
		else if(eLastPosture == AI_TACTICAL_POSTURE_EXPLOIT_FLANKS && eUnitCountDominance != TACTICAL_DOMINANCE_ENEMY && pZone->GetEnemyUnitCount() > 0)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_EXPLOIT_FLANKS;
		}

		// Default for this zone
		else
		{
			eChosenPosture = AI_TACTICAL_POSTURE_EXPLOIT_FLANKS;
		}
		break;
	}
	case TACTICAL_TERRITORY_FRIENDLY:
	{
#if defined(MOD_BALANCE_CORE)
		if(m_pPlayer->isMinorCiv())
		{
			eChosenPosture = AI_TACTICAL_POSTURE_HEDGEHOG;
		}
		else if(eLastPosture == AI_TACTICAL_POSTURE_HEDGEHOG && pZone->GetEnemyUnitCount() > pZone->GetFriendlyUnitCount())
		{
			eChosenPosture = AI_TACTICAL_POSTURE_HEDGEHOG;
		}
		else
#endif
		if(eRangedDominance == TACTICAL_DOMINANCE_FRIENDLY && pZone->GetFriendlyRangedUnitCount() > pZone->GetFriendlyMeleeUnitCount())
		{
			eChosenPosture = AI_TACTICAL_POSTURE_ATTRIT_FROM_RANGE;
		}

		//                 - less stringent if continuing this from a previous turn
		else if(eLastPosture == AI_TACTICAL_POSTURE_ATTRIT_FROM_RANGE && pZone->GetFriendlyRangedUnitCount() > pZone->GetEnemyRangedUnitCount() && eRangedDominance != TACTICAL_DOMINANCE_ENEMY)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_ATTRIT_FROM_RANGE;
		}

		// Exploit flanks - for first time need dominance in unit count
		else if(eUnitCountDominance == TACTICAL_DOMINANCE_FRIENDLY && pZone->GetEnemyUnitCount() > 0)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_EXPLOIT_FLANKS;
		}

		//                 - less stringent if continuing this from a previous turn
		else if(eLastPosture == AI_TACTICAL_POSTURE_EXPLOIT_FLANKS && eUnitCountDominance != TACTICAL_DOMINANCE_ENEMY && pZone->GetEnemyUnitCount() > 0)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_EXPLOIT_FLANKS;
		}

		// Counterattack - for first time must be stronger or even with enemy having a ranged advantage
		else if(pZone->GetDominanceFlag() == TACTICAL_DOMINANCE_FRIENDLY ||
		        pZone->GetDominanceFlag() == TACTICAL_DOMINANCE_EVEN && eRangedDominance == TACTICAL_DOMINANCE_ENEMY)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_COUNTERATTACK;
		}

		//                 - less stringent if continuing this from a previous turn
		else if(eLastPosture == AI_TACTICAL_POSTURE_COUNTERATTACK && pZone->GetDominanceFlag() != TACTICAL_DOMINANCE_ENEMY)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_COUNTERATTACK;
		}

		// Default for this zone
		else
		{
			eChosenPosture = AI_TACTICAL_POSTURE_HEDGEHOG;
		}
		break;
	}
	}

	return eChosenPosture;
}
#if defined(MOD_BALANCE_CORE)
//Postures for naval zones!
AITacticalPosture CvTacticalAI::SelectNavalPosture(CvTacticalDominanceZone* pZone, AITacticalPosture eLastPosture)
{
	AITacticalPosture eChosenPosture = AI_TACTICAL_POSTURE_NONE;
	int iDominancePercentage = GetTacticalAnalysisMap()->GetDominancePercentage();
	eTacticalDominanceFlags eRangedDominance = TACTICAL_DOMINANCE_EVEN;
	eTacticalDominanceFlags eUnitCountDominance = TACTICAL_DOMINANCE_EVEN;
	int iRatio;

	// Compute who is dominant in various areas
	//   Ranged strength
	if ((pZone->GetEnemyNavalRangedStrength() <= 0) && (pZone->GetFriendlyNavalRangedStrength() > 0))
	{
		eRangedDominance = TACTICAL_DOMINANCE_FRIENDLY;
	}
	else
	{
		iRatio = (pZone->GetFriendlyNavalRangedStrength() * 100) / max(1, pZone->GetEnemyNavalRangedStrength());
		if (iRatio > 100 + iDominancePercentage)
		{
			eRangedDominance = TACTICAL_DOMINANCE_FRIENDLY;
		}
		else if (iRatio < 100 - iDominancePercentage)
		{
			eRangedDominance = TACTICAL_DOMINANCE_ENEMY;
		}
	}

	//   Number of units
	if (pZone->GetEnemyNavalUnitCount() <= 0 && pZone->GetFriendlyNavalUnitCount() > 0)
	{
		eUnitCountDominance = TACTICAL_DOMINANCE_FRIENDLY;
	}
	else
	{
		iRatio = (pZone->GetFriendlyNavalUnitCount() * 100) / max(1, pZone->GetEnemyNavalUnitCount());
		if (iRatio > 100 + iDominancePercentage)
		{
			eUnitCountDominance = TACTICAL_DOMINANCE_FRIENDLY;
		}
		else if (iRatio < 100 - iDominancePercentage)
		{
			eUnitCountDominance = TACTICAL_DOMINANCE_ENEMY;
		}
		else
		{
			eUnitCountDominance = TACTICAL_DOMINANCE_EVEN;
		}
	}

	// Choice based on whose territory this is
	switch (pZone->GetTerritoryType())
	{
	case TACTICAL_TERRITORY_ENEMY:
	case TACTICAL_TERRITORY_TEMP_ZONE:
	{
		bool bTemporaryZone = false;
		CvCity *pClosestCity = pZone->GetZoneCity();

		// Always push again for a few turns after each operation reaches destination
		if (pClosestCity)
		{
			bTemporaryZone = IsTemporaryZoneCity(pClosestCity);
		}
		// Withdraw if enemy dominant overall or we have nothing but ranged left
		if (!bTemporaryZone && (pZone->GetDominanceFlag() == TACTICAL_DOMINANCE_ENEMY || pZone->GetFriendlyNavalRangedStrength() == pZone->GetFriendlyNavalUnitCount()))
		{
			eChosenPosture = AI_TACTICAL_POSTURE_WITHDRAW;
		}
		// Temporary zone: want Steamroll or Surgical Strike so we close in on city
		else if (bTemporaryZone)
		{
			if (pZone->GetDominanceFlag() == TACTICAL_DOMINANCE_ENEMY)
			{
				eChosenPosture = AI_TACTICAL_POSTURE_STEAMROLL;
			}
			else
			{
				eChosenPosture = AI_TACTICAL_POSTURE_SURGICAL_CITY_STRIKE;
			}
		}

		// Destroy units then assault - for first time need dominance in total strength but not enemy dominance in ranged units OR just double total strength
		else if (pZone->GetEnemyNavalUnitCount() > 0 && pZone->GetDominanceFlag() == TACTICAL_DOMINANCE_FRIENDLY &&
			(eRangedDominance != TACTICAL_DOMINANCE_ENEMY || pZone->GetFriendlyNavalStrength() > pZone->GetEnemyNavalStrength() * 2))
		{
			eChosenPosture = AI_TACTICAL_POSTURE_STEAMROLL;
		}

		//                 - less stringent if continuing this from a previous turn
		else if (eLastPosture == AI_TACTICAL_POSTURE_STEAMROLL && pZone->GetDominanceFlag() == TACTICAL_DOMINANCE_FRIENDLY && pZone->GetEnemyNavalUnitCount() > 0)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_STEAMROLL;
		}

		// Sit and bombard - for first time need dominance in ranged strength and total unit count
#if defined(MOD_BALANCE_CORE)
		//We need to have plenty of ranged units in order for this to make sense.
		else if (eRangedDominance == TACTICAL_DOMINANCE_FRIENDLY && eUnitCountDominance != TACTICAL_DOMINANCE_ENEMY && pZone->GetFriendlyNavalRangedStrength() >= (pZone->GetFriendlyNavalStrength() / 2))
#else
		else if (eRangedDominance == TACTICAL_DOMINANCE_FRIENDLY && eUnitCountDominance != TACTICAL_DOMINANCE_ENEMY)
#endif
		{
			eChosenPosture = AI_TACTICAL_POSTURE_SIT_AND_BOMBARD;
		}

		//                 - less stringent if continuing this from a previous turn
#if defined(MOD_BALANCE_CORE)
		//We need to have plenty of ranged units in order for this to make sense.
		else if (eLastPosture == AI_TACTICAL_POSTURE_SIT_AND_BOMBARD && eRangedDominance != TACTICAL_DOMINANCE_ENEMY && eUnitCountDominance != TACTICAL_DOMINANCE_ENEMY && pZone->GetFriendlyNavalRangedStrength() >= (pZone->GetFriendlyNavalStrength() / 2))
#else
		else if (eLastPosture == AI_TACTICAL_POSTURE_SIT_AND_BOMBARD && eRangedDominance != TACTICAL_DOMINANCE_ENEMY && eUnitCountDominance != TACTICAL_DOMINANCE_ENEMY)
#endif
		{
			eChosenPosture = AI_TACTICAL_POSTURE_SIT_AND_BOMBARD;
		}

		// Go right after the city - need tactical dominance
		else if (pZone->GetDominanceFlag() == TACTICAL_DOMINANCE_FRIENDLY)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_SURGICAL_CITY_STRIKE;
		}

		// Exploit flanks - for first time need dominance in unit count
		else if (eUnitCountDominance == TACTICAL_DOMINANCE_FRIENDLY && pZone->GetEnemyNavalUnitCount() > 1)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_EXPLOIT_FLANKS;
		}

		//                 - less stringent if continuing this from a previous turn
		else if (eLastPosture == AI_TACTICAL_POSTURE_EXPLOIT_FLANKS && eUnitCountDominance != TACTICAL_DOMINANCE_ENEMY && pZone->GetEnemyNavalUnitCount() > 1)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_EXPLOIT_FLANKS;
		}

		// Default for this zone
		else
		{
			eChosenPosture = AI_TACTICAL_POSTURE_STEAMROLL;
		}
		break;
	}

	case TACTICAL_TERRITORY_NEUTRAL:
	case TACTICAL_TERRITORY_NO_OWNER:
	{
		if (eRangedDominance == TACTICAL_DOMINANCE_FRIENDLY && eUnitCountDominance != TACTICAL_DOMINANCE_ENEMY)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_ATTRIT_FROM_RANGE;
		}

		//                 - less stringent if continuing this from a previous turn
		else if (eLastPosture == AI_TACTICAL_POSTURE_ATTRIT_FROM_RANGE && eRangedDominance != TACTICAL_DOMINANCE_ENEMY)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_ATTRIT_FROM_RANGE;
		}

		// Exploit flanks - for first time need dominance in unit count
		else if (eUnitCountDominance == TACTICAL_DOMINANCE_FRIENDLY && pZone->GetEnemyNavalUnitCount() > 0)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_EXPLOIT_FLANKS;
		}

		//                 - less stringent if continuing this from a previous turn
		else if (eLastPosture == AI_TACTICAL_POSTURE_EXPLOIT_FLANKS && eUnitCountDominance != TACTICAL_DOMINANCE_ENEMY && pZone->GetEnemyNavalUnitCount() > 0)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_EXPLOIT_FLANKS;
		}

		// Default for this zone
		else
		{
			eChosenPosture = AI_TACTICAL_POSTURE_EXPLOIT_FLANKS;
		}
		break;
	}
	case TACTICAL_TERRITORY_FRIENDLY:
	{
		if (eRangedDominance == TACTICAL_DOMINANCE_FRIENDLY && pZone->GetFriendlyNavalRangedStrength() > pZone->GetFriendlyNavalStrength())
		{
			eChosenPosture = AI_TACTICAL_POSTURE_ATTRIT_FROM_RANGE;
		}

		//                 - less stringent if continuing this from a previous turn
		else if (eLastPosture == AI_TACTICAL_POSTURE_ATTRIT_FROM_RANGE && pZone->GetFriendlyNavalRangedStrength() > pZone->GetEnemyNavalRangedStrength() && eRangedDominance != TACTICAL_DOMINANCE_ENEMY)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_ATTRIT_FROM_RANGE;
		}

		// Exploit flanks - for first time need dominance in unit count
		else if (eUnitCountDominance == TACTICAL_DOMINANCE_FRIENDLY && pZone->GetEnemyNavalUnitCount() > 0)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_EXPLOIT_FLANKS;
		}

		//                 - less stringent if continuing this from a previous turn
		else if (eLastPosture == AI_TACTICAL_POSTURE_EXPLOIT_FLANKS && eUnitCountDominance != TACTICAL_DOMINANCE_ENEMY && pZone->GetEnemyNavalUnitCount() > 0)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_EXPLOIT_FLANKS;
		}

		// Counterattack - for first time must be stronger or even with enemy having a ranged advantage
		else if (pZone->GetDominanceFlag() == TACTICAL_DOMINANCE_FRIENDLY ||
			pZone->GetDominanceFlag() == TACTICAL_DOMINANCE_EVEN && eRangedDominance == TACTICAL_DOMINANCE_ENEMY)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_COUNTERATTACK;
		}

		//                 - less stringent if continuing this from a previous turn
		else if (eLastPosture == AI_TACTICAL_POSTURE_COUNTERATTACK && pZone->GetDominanceFlag() != TACTICAL_DOMINANCE_ENEMY)
		{
			eChosenPosture = AI_TACTICAL_POSTURE_COUNTERATTACK;
		}

		// Default for this zone
		else
		{
			eChosenPosture = AI_TACTICAL_POSTURE_COUNTERATTACK;
		}
		break;
	}
	}
	return eChosenPosture;
}
#endif
/// Find last posture for a specific zone
AITacticalPosture CvTacticalAI::FindPosture(CvTacticalDominanceZone* pZone)
{
	if(pZone != NULL)
	{
		for(unsigned int iI = 0; iI < m_Postures.size(); iI++)
		{
			if(m_Postures[iI].GetPlayer() == pZone->GetOwner() &&
			        m_Postures[iI].IsWater() == pZone->IsWater())
			{
				int iCityID = -1;
				if(pZone->GetZoneCity() != NULL)
				{
					iCityID = pZone->GetZoneCity()->GetID();
				}
				if(m_Postures[iI].GetCityID() == iCityID)
				{
					return m_Postures[iI].GetPosture();
				}
			}
		}
	}

	return AI_TACTICAL_POSTURE_NONE;
}

/// Choose which tactics to emphasize this turn
void CvTacticalAI::EstablishTacticalPriorities()
{
	CvTacticalMove move;

	m_MovePriorityList.clear();

	// Loop through each possible tactical move
	for(int iI = 0; iI < GC.getNumTacticalMoveInfos(); iI++)
	{
		const TacticalAIMoveTypes eTacticalAIMove = static_cast<TacticalAIMoveTypes>(iI);
		CvTacticalMoveXMLEntry* pkTacticalMoveInfo = GC.getTacticalMoveInfo(eTacticalAIMove);
		if(pkTacticalMoveInfo)
		{
			move.m_eMoveType = eTacticalAIMove;
			move.m_iPriority = pkTacticalMoveInfo->GetPriority();
			move.m_name = pkTacticalMoveInfo->GetType();

			if(move.m_iPriority >= 0)
			{
				m_MovePriorityList.push_back(move);
			}
		}
	}

	// Now sort the moves in priority order
	std::stable_sort(m_MovePriorityList.begin(), m_MovePriorityList.end());

#if defined(MOD_CORE_DEBUGGING)
	if(MOD_CORE_DEBUGGING)
	{
		//Debugging: Check order of tactical moves ... this is independent of the player
		if (GC.getLogging() && GC.getAILogging() && m_pPlayer->GetID()==1 && GC.getGame().getGameTurn()==1)
		{
			for (size_t i=0; i<m_MovePriorityList.size(); i++)
			{
				OutputDebugString( CvString::format("Turn %03d - Player %02d - Move %02d: %s - Prio %d%s - %s\n", 
					GC.getGame().getGameTurn(), m_pPlayer->GetID(), 
					m_MovePriorityList[i].m_eMoveType, GC.getTacticalMoveInfo(m_MovePriorityList[i].m_eMoveType)->GetType(), 
					m_MovePriorityList[i].m_iPriority,
					GC.getTacticalMoveInfo(m_MovePriorityList[i].m_eMoveType)->CanRecruitForOperations() ? "" : " (not interruptible)",
					GC.getTacticalMoveInfo(m_MovePriorityList[i].m_eMoveType)->IsDominanceZoneMove() ? "dominance zone move" : "" ).c_str() );
			}
		}
	}
#endif

}


/// Choose which tactics the barbarians should emphasize this turn
void CvTacticalAI::EstablishBarbarianPriorities()
{
	int iPriority = 0;
	m_MovePriorityList.clear();

	// Loop through each possible tactical move (other than "none" or "unassigned")
	for(int iI = 0; iI < NUM_AI_BARBARIAN_MOVES; iI++)
	{
		if((AIBarbarianTacticalMove)iI == AI_TACTICAL_BARBARIAN_NONE)
			continue;

		// Set base value
		switch((AIBarbarianTacticalMove)iI)
		{
		case AI_TACTICAL_BARBARIAN_CAPTURE_CITY:
			iPriority = GC.getAI_TACTICAL_BARBARIAN_PRIORITY_CAPTURE_CITY();
			break;
		case AI_TACTICAL_BARBARIAN_DAMAGE_CITY:
			iPriority = GC.getAI_TACTICAL_BARBARIAN_PRIORITY_DAMAGE_CITY();
			break;
		case AI_TACTICAL_BARBARIAN_DESTROY_HIGH_PRIORITY_UNIT:
			iPriority = GC.getAI_TACTICAL_BARBARIAN_PRIORITY_DESTROY_HIGH_UNIT();
			break;
		case AI_TACTICAL_BARBARIAN_DESTROY_MEDIUM_PRIORITY_UNIT:
			iPriority = GC.getAI_TACTICAL_BARBARIAN_PRIORITY_DESTROY_MEDIUM_UNIT();
			break;
		case AI_TACTICAL_BARBARIAN_DESTROY_LOW_PRIORITY_UNIT:
			iPriority = GC.getAI_TACTICAL_BARBARIAN_PRIORITY_DESTROY_LOW_UNIT();
			break;
		case AI_TACTICAL_BARBARIAN_MOVE_TO_SAFETY:
			iPriority = GC.getAI_TACTICAL_BARBARIAN_PRIORITY_TO_SAFETY();
			break;
		case AI_TACTICAL_BARBARIAN_ATTRIT_HIGH_PRIORITY_UNIT:
			iPriority = GC.getAI_TACTICAL_BARBARIAN_PRIORITY_ATTRIT_HIGH_UNIT();
			break;
		case AI_TACTICAL_BARBARIAN_ATTRIT_MEDIUM_PRIORITY_UNIT:
			iPriority = GC.getAI_TACTICAL_BARBARIAN_PRIORITY_ATTRIT_MEDIUM_UNIT();
			break;
		case AI_TACTICAL_BARBARIAN_ATTRIT_LOW_PRIORITY_UNIT:
			iPriority = GC.getAI_TACTICAL_BARBARIAN_PRIORITY_ATTRIT_LOW_UNIT();
			break;
		case AI_TACTICAL_BARBARIAN_PILLAGE:
			iPriority = GC.getAI_TACTICAL_BARBARIAN_PRIORITY_PILLAGE();
			break;
		case AI_TACTICAL_BARBARIAN_PILLAGE_CITADEL:
			iPriority = GC.getAI_TACTICAL_BARBARIAN_PRIORITY_PILLAGE_CITADEL();
			break;
		case AI_TACTICAL_BARBARIAN_PILLAGE_NEXT_TURN:
			iPriority = GC.getAI_TACTICAL_BARBARIAN_PRIORITY_PILLAGE_NEXT_TURN();
			break;
		case AI_TACTICAL_BARBARIAN_PRIORITY_BLOCKADE_RESOURCE:
			iPriority = GC.getAI_TACTICAL_BARBARIAN_PRIORITY_BLOCKADE_RESOURCE();
			break;
		case AI_TACTICAL_BARBARIAN_CIVILIAN_ATTACK:
			iPriority = GC.getAI_TACTICAL_BARBARIAN_PRIORITY_CIVILIAN_ATTACK();
			break;
		case AI_TACTICAL_BARBARIAN_AGGRESSIVE_MOVE:
			iPriority = GC.getAI_TACTICAL_BARBARIAN_PRIORITY_AGGRESSIVE_MOVE();
			break;
		case AI_TACTICAL_BARBARIAN_PASSIVE_MOVE:
			iPriority = GC.getAI_TACTICAL_BARBARIAN_PRIORITY_PASSIVE_MOVE();
			break;
		case AI_TACTICAL_BARBARIAN_CAMP_DEFENSE:
			iPriority = GC.getAI_TACTICAL_BARBARIAN_PRIORITY_CAMP_DEFENSE();
			break;
		case AI_TACTICAL_BARBARIAN_DESPERATE_ATTACK:
			iPriority = GC.getAI_TACTICAL_BARBARIAN_PRIORITY_DESPERATE_ATTACK();
			break;
		case AI_TACTICAL_BARBARIAN_ESCORT_CIVILIAN:
			iPriority = GC.getAI_TACTICAL_BARBARIAN_PRIORITY_ESCORT_CIVILIAN();
			break;
		case AI_TACTICAL_BARBARIAN_PLUNDER_TRADE_UNIT:
			iPriority = GC.getAI_TACTICAL_BARBARIAN_PRIORITY_PLUNDER_TRADE_UNIT();
			break;
		}

		// Make sure base priority is not negative
		if(iPriority >= 0)
		{
			// Finally, add a random die roll to each priority
#if defined(MOD_CORE_REDUCE_RANDOMNESS)
			iPriority += GC.getGame().getSmallFakeRandNum(GC.getAI_TACTICAL_MOVE_PRIORITY_RANDOMNESS(),iI);
#else
			iPriority += GC.getGame().getJonRandNum(GC.getAI_TACTICAL_MOVE_PRIORITY_RANDOMNESS(), "Tactical AI Move Priority");
#endif

			// Store off this move and priority
			CvTacticalMove move;
			move.m_eMoveType = (TacticalAIMoveTypes)iI;
			move.m_iPriority = iPriority;
			move.m_name = barbarianMoveNames[iI];

			m_MovePriorityList.push_back(move);
		}
	}

	// Now sort the moves in priority order
	std::stable_sort(m_MovePriorityList.begin(), m_MovePriorityList.end());

#if defined(MOD_BALANCE_CORE_MILITARY)
	if(MOD_BALANCE_CORE_MILITARY_LOGGING)
	{
		//Debugging: Check order of tactical moves ...
		if (GC.getAILogging() && GC.getGame().getGameTurn()==1)
			for (size_t i=0; i<m_MovePriorityList.size(); i++)
				OutputDebugString( CvString::format("Turn %03d - Barbarian Move %s - Prio %d\n", 
					GC.getGame().getGameTurn(), barbarianMoveNames[m_MovePriorityList[i].m_eMoveType], m_MovePriorityList[i].m_iPriority).c_str() );
	}
#endif
}


/// Make lists of everything we might want to target with the tactical AI this turn
void CvTacticalAI::FindTacticalTargets()
{
	int iI;
	CvPlot* pLoopPlot;
	CvTacticalTarget newTarget;
	bool bValidPlot;
	bool bEnemyDominatedPlot;
	CvPlayerTrade* pPlayerTrade = m_pPlayer->GetTrade();

	// Clear out target list since we rebuild it each turn
	m_AllTargets.clear();

	bool bBarbsAllowedYet = GC.getGame().getGameTurn() >= GC.getGame().GetBarbarianReleaseTurn();

	// Look at every tile on map
	for(iI = 0; iI < GC.getMap().numPlots(); iI++)
	{
		pLoopPlot = GC.getMap().plotByIndexUnchecked(iI);
		bValidPlot = false;

#if defined(MOD_BALANCE_CORE_MILITARY)
		if(pLoopPlot->isRevealed(m_pPlayer->getTeam()))
#else
		if(pLoopPlot->isVisible(m_pPlayer->getTeam()))
#endif
		{
			// Make sure I am not a barbarian who can not move into owned territory this early in the game
			if(!m_pPlayer->isBarbarian() || bBarbsAllowedYet)
			{
				bValidPlot = true;
			}
			else
			{
				if(!pLoopPlot->isOwned())
				{
					bValidPlot = true;
				}
			}
		}

		if(bValidPlot)
		{
			newTarget.Clear();
			newTarget.SetTargetX(pLoopPlot->getX());
			newTarget.SetTargetY(pLoopPlot->getY());
			newTarget.SetDominanceZone(GetTacticalAnalysisMap()->GetCell(iI)->GetDominanceZone());

			bEnemyDominatedPlot = GetTacticalAnalysisMap()->IsInEnemyDominatedZone(pLoopPlot);

			// Have a ...
			// ... friendly city?
			CvCity* pCity = pLoopPlot->getPlotCity();
			if(pCity != NULL)
			{
				if(m_pPlayer->GetID() == pCity->getOwner())
				{
					newTarget.SetTargetType(AI_TACTICAL_TARGET_CITY_TO_DEFEND);
					newTarget.SetAuxData((void*)pCity);
					newTarget.SetAuxIntData(pCity->getThreatValue());
					m_AllTargets.push_back(newTarget);
				}

				// ... enemy city
				else if(atWar(m_pPlayer->getTeam(), pCity->getTeam()))
				{
					newTarget.SetTargetType(AI_TACTICAL_TARGET_CITY);
					newTarget.SetTargetPlayer(pCity->getOwner());
					newTarget.SetAuxData((void*)pCity);
#if defined(MOD_BALANCE_CORE)
					newTarget.SetAuxIntData(100);
#endif
					m_AllTargets.push_back(newTarget);
				}
			}
			else
			{
				// ... enemy unit?
				CvUnit* pUnit = pLoopPlot->getVisibleEnemyDefender(m_pPlayer->GetID());
				if(pUnit != NULL)
				{
#if defined(MOD_BALANCE_CORE_MILITARY)
					if(pLoopPlot->getImprovementType() == GC.getBARBARIAN_CAMP_IMPROVEMENT())
						newTarget.SetTargetType(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT);
					else
						newTarget.SetTargetType(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT);
#else
					newTarget.SetTargetType(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT);
#endif
					//note that the HIGH/MEDIUM/LOW classification is changed later in IdentifyPriorityTargets
					newTarget.SetTargetPlayer(pUnit->getOwner());
					newTarget.SetAuxData((void*)pUnit);
					newTarget.SetAuxIntData(50);
					m_AllTargets.push_back(newTarget);
				}
				// ... undefended camp?
				else if(pLoopPlot->getImprovementType() == GC.getBARBARIAN_CAMP_IMPROVEMENT() && !m_pPlayer->isMinorCiv())
				{
					newTarget.SetTargetType(AI_TACTICAL_TARGET_BARBARIAN_CAMP);
					newTarget.SetTargetPlayer(BARBARIAN_PLAYER);
					newTarget.SetAuxData((void*)pLoopPlot);
					newTarget.SetAuxIntData( m_pPlayer->isBarbarian() ? 5 : 30 );
					m_AllTargets.push_back(newTarget);
				}

				// ... goody hut?
				else if(!m_pPlayer->isMinorCiv() && pLoopPlot->isGoody())
				{
					newTarget.SetTargetType(AI_TACTICAL_TARGET_ANCIENT_RUINS);
					newTarget.SetAuxData((void*)pLoopPlot);
#if defined(MOD_BALANCE_CORE)
					newTarget.SetAuxIntData(50);
#endif
					m_AllTargets.push_back(newTarget);
				}

				// Or citadels!
#if defined(MOD_BALANCE_CORE_MILITARY)
				else if(atWar(m_pPlayer->getTeam(), pLoopPlot->getTeam()) &&
					pLoopPlot->getRevealedImprovementType(m_pPlayer->getTeam()) != NO_IMPROVEMENT &&
					GC.getImprovementInfo(pLoopPlot->getRevealedImprovementType(m_pPlayer->getTeam()))->GetNearbyEnemyDamage() > 0)
#else
				else if(atWar(m_pPlayer->getTeam(), pLoopPlot->getTeam()) &&
					pLoopPlot->getImprovementType() != NO_IMPROVEMENT &&
					GC.getImprovementInfo(pLoopPlot->getImprovementType())->GetNearbyEnemyDamage() > 0)
#endif
				{
					newTarget.SetTargetType(AI_TACTICAL_TARGET_CITADEL);
					newTarget.SetTargetPlayer(pLoopPlot->getOwner());
					newTarget.SetAuxData((void*)pLoopPlot);
#if defined(MOD_BALANCE_CORE)
					newTarget.SetAuxIntData(80);
#endif
					m_AllTargets.push_back(newTarget);
				}

				// ... enemy improvement?
#if defined(MOD_BALANCE_CORE_MILITARY)
				else if(atWar(m_pPlayer->getTeam(), pLoopPlot->getTeam()) &&
				        pLoopPlot->getRevealedImprovementType(m_pPlayer->getTeam()) != NO_IMPROVEMENT &&
				        !pLoopPlot->IsImprovementPillaged())
#else
				else if(atWar(m_pPlayer->getTeam(), pLoopPlot->getTeam()) &&
				        pLoopPlot->getImprovementType() != NO_IMPROVEMENT &&
				        !pLoopPlot->IsImprovementPillaged())
#endif
				{
					ResourceUsageTypes eRUT = (ResourceUsageTypes)-1;
					ResourceTypes eResource = pLoopPlot->getResourceType();
					if (eResource != NO_RESOURCE)
					{
						eRUT = GC.getResourceInfo(eResource)->getResourceUsage();
					}

					// On land, civs prioritize improvements built on resources
					if (eRUT == RESOURCEUSAGE_STRATEGIC || eRUT == RESOURCEUSAGE_LUXURY || pLoopPlot->isWater() || m_pPlayer->isBarbarian())
					{
						// Barbarians can't target naval improvements
						if(m_pPlayer->isBarbarian() && pLoopPlot->isWater())
						{
							continue;
						}
						else
						{
							newTarget.SetTargetType(AI_TACTICAL_TARGET_IMPROVEMENT_RESOURCE);
							newTarget.SetTargetPlayer(pLoopPlot->getOwner());
							newTarget.SetAuxData((void*)pLoopPlot);
#if defined(MOD_BALANCE_CORE)
							newTarget.SetAuxIntData(40);
#endif
							m_AllTargets.push_back(newTarget);
						}
					}
					else
					{
						newTarget.SetTargetType(AI_TACTICAL_TARGET_IMPROVEMENT);
						newTarget.SetTargetPlayer(pLoopPlot->getOwner());
						newTarget.SetAuxData((void*)pLoopPlot);
#if defined(MOD_BALANCE_CORE)
						newTarget.SetAuxIntData(5);
#endif
						m_AllTargets.push_back(newTarget);
					}
				}

				// ... enemy trade route? (city connection - not caravan)
				// checking for city connection is not enough, some people (iroquois) don't need roads, so there isn't anything to pillage 
				else if(atWar(m_pPlayer->getTeam(), pLoopPlot->getTeam()) &&
				        pLoopPlot->getRouteType() != NO_ROUTE && !pLoopPlot->IsRoutePillaged() && pLoopPlot->IsCityConnection()/* && !bEnemyDominatedPlot*/)
				{
					newTarget.SetTargetType(AI_TACTICAL_TARGET_IMPROVEMENT);
					newTarget.SetTargetPlayer(pLoopPlot->getOwner());
					newTarget.SetAuxData((void*)pLoopPlot);
#if defined(MOD_BALANCE_CORE)
					newTarget.SetAuxIntData(10);
#endif
					m_AllTargets.push_back(newTarget);
				}

				// ... enemy civilian (or embarked) unit?
				else if(pLoopPlot->isVisibleOtherUnit(m_pPlayer->GetID()))
				{
					CvUnit* pTargetUnit = pLoopPlot->getUnitByIndex(0);
					if(!pTargetUnit->isDelayedDeath() && atWar(m_pPlayer->getTeam(), pTargetUnit->getTeam()) && !pTargetUnit->IsCanDefend())
					{
						newTarget.SetTargetType(AI_TACTICAL_TARGET_LOW_PRIORITY_CIVILIAN);
						newTarget.SetTargetPlayer(pTargetUnit->getOwner());
						newTarget.SetAuxData((void*)pTargetUnit);

						if(pTargetUnit->isEmbarked())
						{
							if(pTargetUnit->IsCombatUnit())
							{
								newTarget.SetTargetType(AI_TACTICAL_TARGET_EMBARKED_MILITARY_UNIT);
							}
							else
							{
								newTarget.SetTargetType(AI_TACTICAL_TARGET_EMBARKED_CIVILIAN);
							}
						}
						else
						{
							if(IsVeryHighPriorityCivilianTarget(&newTarget))
							{
								newTarget.SetTargetType(AI_TACTICAL_TARGET_VERY_HIGH_PRIORITY_CIVILIAN);
							}
							else if(IsHighPriorityCivilianTarget(&newTarget))
							{
								newTarget.SetTargetType(AI_TACTICAL_TARGET_HIGH_PRIORITY_CIVILIAN);
							}
							else if(IsMediumPriorityCivilianTarget(&newTarget))
							{
								newTarget.SetTargetType(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_CIVILIAN);
							}
						}
#if defined(MOD_BALANCE_CORE)
						newTarget.SetAuxIntData(25);
#endif
						m_AllTargets.push_back(newTarget);
					}
				}

				// ... trade unit
#if defined(MOD_BALANCE_CORE_MILITARY)
				else if (pLoopPlot->isVisible(m_pPlayer->getTeam()) && pPlayerTrade->ContainsEnemyTradeUnit(pLoopPlot))
#else
				else if (pPlayerTrade->ContainsEnemyTradeUnit(pLoopPlot))
#endif
				{
					if (pLoopPlot->isWater())
					{
						newTarget.SetTargetType(AI_TACTICAL_TARGET_TRADE_UNIT_SEA);
					}
					else
					{
						newTarget.SetTargetType(AI_TACTICAL_TARGET_TRADE_UNIT_LAND);
					}

					newTarget.SetAuxData((void*)pLoopPlot);
#if defined(MOD_BALANCE_CORE)
					newTarget.SetAuxIntData(35);
#endif
					m_AllTargets.push_back(newTarget);
				}

				// ... defensive bastion?
#if defined(MOD_BALANCE_CORE_MILITARY)
				else if(m_pPlayer->GetID() == pLoopPlot->getOwner() &&
				        pLoopPlot->defenseModifier(m_pPlayer->getTeam(), false, false) >= 20 &&
						m_pPlayer->GetPlotDanger(*pLoopPlot) > 0)
				{
					CvCity* pDefenseCity = pLoopPlot->GetAdjacentFriendlyCity(m_pPlayer->getTeam(), true/*bLandOnly*/);
					if(pDefenseCity || pLoopPlot->IsChokePoint() || pLoopPlot->defenseModifier(m_pPlayer->getTeam(), false, false)>=50)
#else
				else if(m_pPlayer->GetID() == pLoopPlot->getOwner() &&
				        pLoopPlot->defenseModifier(m_pPlayer->getTeam(), true) > 0 &&
				        m_pPlayer->GetPlotDanger(*pLoopPlot) > 0)
				{
					CvCity* pDefenseCity = pLoopPlot->GetAdjacentFriendlyCity(m_pPlayer->getTeam(), true/*bLandOnly*/);
					if(pDefenseCity)
#endif
					{
						newTarget.SetTargetType(AI_TACTICAL_TARGET_DEFENSIVE_BASTION);
						newTarget.SetAuxData((void*)pLoopPlot);
#if defined(MOD_BALANCE_CORE_MILITARY)
						if(pDefenseCity)
						{
							newTarget.SetAuxIntData(pDefenseCity->getThreatValue() + m_pPlayer->GetPlotDanger(*pLoopPlot));
						}
						else
						{
							newTarget.SetAuxIntData(m_pPlayer->GetPlotDanger(*pLoopPlot));
						}
#else
						newTarget.SetAuxIntData(pDefenseCity->getThreatValue() + m_pPlayer->GetPlotDanger(*pLoopPlot));
#endif
						m_AllTargets.push_back(newTarget);
					}
				}

				// ... friendly improvement?
				else if(m_pPlayer->GetID() == pLoopPlot->getOwner() &&
				        pLoopPlot->getImprovementType() != NO_IMPROVEMENT &&
				        !pLoopPlot->IsImprovementPillaged() && !pLoopPlot->isGoody())
				{
					newTarget.SetTargetType(AI_TACTICAL_TARGET_IMPROVEMENT_TO_DEFEND);
					newTarget.SetAuxData((void*)pLoopPlot);
#if defined(MOD_BALANCE_CORE)
					newTarget.SetAuxIntData(1);
#endif
					m_AllTargets.push_back(newTarget);
				}

				// ... trade plot (for getting units to park on trade routes to try to get them to plunder enemy trade routes)
				else if (pLoopPlot->isVisible(m_pPlayer->getTeam()) && 
						 pPlayerTrade->ContainsEnemyTradePlot(pLoopPlot))
				{
					if (pLoopPlot->isWater())
					{
						newTarget.SetTargetType(AI_TACTICAL_TARGET_TRADE_UNIT_SEA_PLOT);
					}
					else
					{
						newTarget.SetTargetType(AI_TACTICAL_TARGET_TRADE_UNIT_LAND_PLOT);
					}

					newTarget.SetAuxData((void*)pLoopPlot);
#if defined(MOD_BALANCE_CORE)
					newTarget.SetAuxIntData(15);
#endif
					m_AllTargets.push_back(newTarget);
				}
#if defined(MOD_BALANCE_CORE)
				//Enemy water plots?
				else if (pLoopPlot->isWater() && atWar(m_pPlayer->getTeam(), pLoopPlot->getTeam()))
				{
					CvCity* pWorkingCity = pLoopPlot->getWorkingCity();
					if (pWorkingCity != NULL && pWorkingCity->isCoastal())
					{
						int iDistance = GET_PLAYER(pWorkingCity->getOwner()).GetCityDistanceInEstimatedTurns(pLoopPlot);

						if (iDistance > 4)
							continue;

						int iWeight = 0;

						if (pLoopPlot->getImprovementType() != NO_IMPROVEMENT)
						{
							iWeight += 50;
						}

						if (iDistance == 4)
						{
							iWeight += 50;
						}
						if (iDistance == 3)
						{
							iWeight += 75;
						}
						else if (iDistance == 2)
						{
							iWeight += 100;
						}
						else if (iDistance == 1)
						{
							iWeight += 25;
						}
						if (pLoopPlot->getNumUnits() > 0)
						{
							CvUnit* pUnit = pLoopPlot->getUnitByIndex(0);
							if (pUnit != NULL)
							{
								if (pUnit->IsCivilianUnit())
								{
									iWeight += 25;
								}
								else if (pUnit->isEmbarked() && pUnit->getDomainType() != DOMAIN_SEA)
								{
									iWeight += 50;
								}
							}
						}


						if (GET_PLAYER(pWorkingCity->getOwner()).getNumCities() > 1 && pWorkingCity->GetThreatCriteria() != -1)
						{
							//More cities = more threat.
							int iThreat = (GET_PLAYER(pWorkingCity->getOwner()).getNumCities() - pWorkingCity->GetThreatCriteria()) * 10;
							if (iThreat > 0)
							{
								iWeight += iThreat;
							}
						}
						if (pWorkingCity->isInDangerOfFalling() || pWorkingCity->isUnderSiege() || pWorkingCity->IsBlockaded(true))
						{
							iWeight *= 10;
						}
						if (pWorkingCity->IsBastion())
						{
							iWeight *= 5;
						}
						if (pWorkingCity->getDamage() > 0)
						{
							iWeight *= 2;
						}
						if (iWeight > 0)
						{
							newTarget.SetTargetType(AI_TACTICAL_TARGET_BLOCKADE_RESOURCE_POINT);

							newTarget.SetAuxData((void*)pLoopPlot);
							newTarget.SetAuxIntData(iWeight);
							m_AllTargets.push_back(newTarget);
						}
					}
				}
#endif
			}
		}
	}

	// POST-PROCESSING ON TARGETS

	// Mark enemy units threatening our cities (or camps) as priority targets
	if(m_pPlayer->isBarbarian())
	{
		IdentifyPriorityBarbarianTargets();
	}
	else
	{
		IdentifyPriorityTargets();
	}

	// Also add some priority targets that we'd like to hit just because of their unit type (e.g. catapults)
	IdentifyPriorityTargetsByType();

	// make sure high prio units have the higher scores
	UpdateTargetScores();

	// Sort remaining targets by aux data (if used for that target type)
	std::stable_sort(m_AllTargets.begin(), m_AllTargets.end());

#if defined(MOD_CORE_DEBUGGING)
	if(MOD_CORE_DEBUGGING)
	{
		// mark the targets in the tactical map
		for (TacticalList::const_iterator i=m_AllTargets.begin(); i!=m_AllTargets.end(); ++i)
			GetTacticalAnalysisMap()->GetCell( GC.getMap().plotNum( i->GetTargetX(), i->GetTargetY() ) )->SetTargetType( i->GetTargetType() ); 

		GetTacticalAnalysisMap()->Dump();
	}
#endif

}

void CvTacticalAI::ProcessDominanceZones()
{
	FStaticVector<CvTacticalMove, 256, true, c_eCiv5GameplayDLL >::iterator it;

	// Barbarian processing is straightforward -- just one big list of priorites and everything is considered at once
	if(m_pPlayer->isBarbarian())
	{
		EstablishBarbarianPriorities();
		ExtractTargetsForZone(NULL);
		AssignBarbarianMoves();
	}
	else
	{
		EstablishTacticalPriorities();
		UpdatePostures();

		// Proceed in priority order
		for(it = m_MovePriorityList.begin(); it != m_MovePriorityList.end(); it++)
		{
			CvTacticalMove move = *it;

			if(move.m_iPriority >= 0)
			{
				CvTacticalMoveXMLEntry* pkTacticalMoveInfo = GC.getTacticalMoveInfo(move.m_eMoveType);
				if(pkTacticalMoveInfo && pkTacticalMoveInfo->IsDominanceZoneMove())
				{
					for(int iI = 0; iI < GetTacticalAnalysisMap()->GetNumZones(); iI++)
					{
						m_iCurrentZoneIndex = iI;
						CvTacticalDominanceZone* pZone = GetTacticalAnalysisMap()->GetZoneByIndex(iI);

						//no units -> nothing to do
						if (pZone->GetFriendlyStrength()+pZone->GetFriendlyRangedStrength()==0)
							continue;

						AITacticalPosture ePosture = FindPosture(pZone);
						CvString strMoveName = (CvString)pkTacticalMoveInfo->GetType();
						CvTacticalMove moveToPassOn = move;

						// Is this move of the right type for this zone?
						bool bMatch = false;
						if(ePosture != AI_TACTICAL_POSTURE_WITHDRAW && strMoveName == "TACTICAL_CLOSE_ON_TARGET")
						{
							bMatch = true;
						}
						else if(ePosture == AI_TACTICAL_POSTURE_WITHDRAW && strMoveName == "TACTICAL_POSTURE_WITHDRAW")
						{
							bMatch = true;
						}
						else if(ePosture == AI_TACTICAL_POSTURE_HEDGEHOG && strMoveName == "TACTICAL_POSTURE_HEDGEHOG")
						{
							bMatch = true;
						}
						else if(ePosture == AI_TACTICAL_POSTURE_SHORE_BOMBARDMENT && strMoveName == "TACTICAL_POSTURE_SHORE_BOMBARDMENT")
						{
							bMatch = true;
						}
						else if(pZone->GetDominanceFlag() != TACTICAL_DOMINANCE_FRIENDLY && pZone->GetTerritoryType() == TACTICAL_TERRITORY_FRIENDLY && strMoveName == "TACTICAL_EMERGENCY_PURCHASES")
						{
							bMatch = true;
						}
						else if(pZone->GetDominanceFlag() != TACTICAL_DOMINANCE_FRIENDLY && pZone->GetTerritoryType() == TACTICAL_TERRITORY_FRIENDLY && strMoveName == "TACTICAL_DEFENSIVE_AIRLIFT")
						{
							bMatch = true;
						}
						else if (strMoveName == "TACTICAL_OFFENSIVE_POSTURE_MOVES")
						{
							// If this is an offensive posture move, map us to the detailed move type.  Why?  Well we want these offensive moves
							// processed in order of zone priority. This allows us to set them all to the same priority but then break them out
							// into individual move types that get passed into AssignTacticalMove()
							bMatch = true;
							
							switch (ePosture)
							{
							case AI_TACTICAL_POSTURE_SIT_AND_BOMBARD:
								moveToPassOn.m_eMoveType = (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_POSTURE_SIT_AND_BOMBARD];
								break;
							case AI_TACTICAL_POSTURE_ATTRIT_FROM_RANGE:
								moveToPassOn.m_eMoveType = (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_POSTURE_ATTRIT_FROM_RANGE];
								break;
							case AI_TACTICAL_POSTURE_EXPLOIT_FLANKS:
								moveToPassOn.m_eMoveType = (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_POSTURE_EXPLOIT_FLANKS];
								break;
							case AI_TACTICAL_POSTURE_STEAMROLL:
								moveToPassOn.m_eMoveType = (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_POSTURE_STEAMROLL];
								break;
							case AI_TACTICAL_POSTURE_SURGICAL_CITY_STRIKE:
								moveToPassOn.m_eMoveType = (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_POSTURE_SURGICAL_CITY_STRIKE];
								break;
							case AI_TACTICAL_POSTURE_COUNTERATTACK:
								moveToPassOn.m_eMoveType = (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_POSTURE_COUNTERATTACK];
								break;
							default:
								bMatch = false;
								break;
							}
						}

						if(bMatch)
						{
							if(!UseThisDominanceZone(pZone))
							{
								continue;
							}

							ExtractTargetsForZone(pZone);

							// Must have some moves to continue or it must be land around an enemy city (which we always want to process because
							// we might have an operation targeting it)
							if(m_ZoneTargets.size() <= 0 && pZone->GetTerritoryType() != TACTICAL_TERRITORY_TEMP_ZONE && (pZone->GetTerritoryType() != TACTICAL_TERRITORY_ENEMY || pZone->IsWater()))
							{
								continue;
							}


							if (GC.getLogging() && GC.getAILogging())
							{
								CvString strLogString;
								CvCity* pZoneCity = pZone->GetZoneCity();
								CvTacticalMoveXMLEntry* pkMoveInfo = GC.getTacticalMoveInfo(moveToPassOn.m_eMoveType);
								strLogString.Format("Using move %s for zone %d (city %s - %s)", pkMoveInfo ? pkMoveInfo->GetType() : "unknown", 
									pZone->GetDominanceZoneID(), pZoneCity ? pZoneCity->getName().c_str() : "none", postureNames[ePosture]);
								LogTacticalMessage(strLogString);
								
							}

							AssignTacticalMove(moveToPassOn);
						}
					}
				}
				else
				{
					ExtractTargetsForZone(NULL);
					AssignTacticalMove(move);
				}
			}
		}
	}

	//failsafe
	ReviewUnassignedUnits();
}

/// Choose which tactics to run and assign units to it
void CvTacticalAI::AssignTacticalMove(CvTacticalMove move)
{
#pragma warning ( push )
#pragma warning ( disable : 6011 ) // Dereferencing NULL pointer
	AI_PERF_FORMAT("AI-perf-tact.csv", ("Move Type: %s (%d), Turn %03d, %s", GC.getTacticalMoveInfo(move.m_eMoveType)->GetType(), (int)move.m_eMoveType, GC.getGame().getElapsedGameTurns(), m_pPlayer->getCivilizationShortDescription()) );
#pragma warning ( pop )

#if defined(MOD_BALANCE_CORE_MILITARY)
	m_CurrentMoveUnits.setCurrentTacticalMove(move);
	m_CurrentMoveHighPriorityUnits.setCurrentTacticalMove(move);
#endif

	//the order in which the moves are listed here is not at all the order in which they are executed!

	if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_MOVE_NONCOMBATANTS_TO_SAFETY])
	{
		PlotMovesToSafety(false /*bCombatUnits*/);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_CAPTURE_CITY])
	{
		PlotCaptureCityMoves();
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_DAMAGE_CITY])
	{
		PlotDamageCityMoves();
		PlotNavalDamageCityMoves();
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_DESTROY_HIGH_UNIT])
	{
		PlotDestroyUnitMoves(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT, true, true);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_DESTROY_MEDIUM_UNIT])
	{
		PlotDestroyUnitMoves(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT, true);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_DESTROY_LOW_UNIT])
	{
		PlotDestroyUnitMoves(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT, true);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_TO_SAFETY])
	{
		PlotMovesToSafety(true /*bCombatUnits*/);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_ATTRIT_HIGH_UNIT])
	{
		PlotDestroyUnitMoves(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT, false);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_ATTRIT_MEDIUM_UNIT])
	{
		PlotDestroyUnitMoves(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT, false);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_ATTRIT_LOW_UNIT])
	{
		PlotDestroyUnitMoves(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT, false);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_REPOSITION])
	{
		PlotBlockadeImprovementMoves();
		PlotRepositionMoves();
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_BARBARIAN_CAMP])
	{
		PlotBarbarianCampMoves();
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_PILLAGE_CITADEL])
	{
		PlotPillageMoves(AI_TACTICAL_TARGET_CITADEL, true /*bFirstPass*/);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_PILLAGE_RESOURCE])
	{
		PlotPillageMoves(AI_TACTICAL_TARGET_IMPROVEMENT_RESOURCE, true /*bFirstPass*/);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_PILLAGE])
	{
		PlotPillageMoves(AI_TACTICAL_TARGET_IMPROVEMENT, true /*bFirstPass*/);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_PILLAGE_CITADEL_NEXT_TURN])
	{
		PlotPillageMoves(AI_TACTICAL_TARGET_CITADEL, false /*bFirstPass*/);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_PILLAGE_RESOURCE_NEXT_TURN])
	{
		PlotPillageMoves(AI_TACTICAL_TARGET_IMPROVEMENT_RESOURCE, false /*bFirstPass*/);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_PILLAGE_NEXT_TURN])
	{
		PlotPillageMoves(AI_TACTICAL_TARGET_IMPROVEMENT, false /*bFirstPass*/);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_PLUNDER_TRADE_UNIT])
	{
		PlotPlunderTradeUnitMoves(DOMAIN_LAND);
		PlotPlunderTradeUnitMoves(DOMAIN_SEA);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_PARK_ON_TRADE_ROUTE])
	{
		PlotPlunderTradePlotMoves(DOMAIN_LAND);
		PlotPlunderTradePlotMoves(DOMAIN_SEA);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_ATTACK_VERY_HIGH_PRIORITY_CIVILIAN])
	{
		PlotCivilianAttackMoves(AI_TACTICAL_TARGET_VERY_HIGH_PRIORITY_CIVILIAN);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_ATTACK_HIGH_PRIORITY_CIVILIAN])
	{
		PlotCivilianAttackMoves(AI_TACTICAL_TARGET_HIGH_PRIORITY_CIVILIAN);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_ATTACK_MEDIUM_PRIORITY_CIVILIAN])
	{
		PlotCivilianAttackMoves(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_CIVILIAN);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_ATTACK_LOW_PRIORITY_CIVILIAN])
	{
		PlotCivilianAttackMoves(AI_TACTICAL_TARGET_LOW_PRIORITY_CIVILIAN);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_SAFE_BOMBARDS])
	{
		PlotSafeBombardMoves();
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_HEAL])
	{
		PlotHealMoves();
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_ANCIENT_RUINS])
	{
		PlotAncientRuinMoves(1);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_GARRISON_TO_ALLOW_BOMBARD])
	{
		PlotGarrisonMoves(1, true /*bMustAllowRangedAttack*/);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_GARRISON_ALREADY_THERE])
	{
		PlotGarrisonMoves(0);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_BASTION_ALREADY_THERE])
	{
		PlotBastionMoves(0);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_GUARD_IMPROVEMENT_ALREADY_THERE])
	{
		PlotGuardImprovementMoves(0);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_GARRISON_1_TURN])
	{
		PlotGarrisonMoves(1);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_BASTION_1_TURN])
	{
		PlotBastionMoves(1);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_GUARD_IMPROVEMENT_1_TURN])
	{
		PlotGuardImprovementMoves(1);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_AIR_INTERCEPT])
	{
		PlotAirInterceptMoves();
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_AIR_SWEEP])
	{
		PlotAirSweepMoves();
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_POSTURE_SIT_AND_BOMBARD])
	{
		PlotSitAndBombardMoves();
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_POSTURE_ATTRIT_FROM_RANGE])
	{
		PlotAttritFromRangeMoves();
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_POSTURE_EXPLOIT_FLANKS])
	{
		PlotExploitFlanksMoves();
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_POSTURE_STEAMROLL])
	{
		//important
		PlotSteamrollMoves();
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_POSTURE_SURGICAL_CITY_STRIKE])
	{
		PlotSurgicalCityStrikeMoves();
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_POSTURE_HEDGEHOG])
	{
		PlotHedgehogMoves();
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_POSTURE_COUNTERATTACK])
	{
		PlotCounterattackMoves();
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_POSTURE_WITHDRAW])
	{
		PlotWithdrawMoves();
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_POSTURE_SHORE_BOMBARDMENT])
	{
		PlotShoreBombardmentMoves();
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_CLOSE_ON_TARGET])
	{
		PlotCloseOnTarget(true /*bCheckDominance*/);
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_MOVE_OPERATIONS])
	{
		//this is in fact a very important move
		PlotOperationalArmyMoves();
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_EMERGENCY_PURCHASES])
	{
		PlotEmergencyPurchases();
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_DEFENSIVE_AIRLIFT])
	{
		PlotDefensiveAirlifts();
	}
	else if(move.m_eMoveType == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_ESCORT_EMBARKED_UNIT])
	{
		PlotEscortEmbarkedMoves();
	}
}

/// Choose which tactics to run and assign units to it (barbarian version)
void CvTacticalAI::AssignBarbarianMoves()
{
	FStaticVector<CvTacticalMove, 256, true, c_eCiv5GameplayDLL >::iterator it;

	// Proceed in priority order
	for(it = m_MovePriorityList.begin(); it != m_MovePriorityList.end(); it++)
	{
		CvTacticalMove move = *it;

#if defined(MOD_BALANCE_CORE_MILITARY)
		//if all units have moved we can stop - this is ok here because barbarians don't have operations
		if (m_CurrentTurnUnits.empty())
			break;

		//debugging
		m_CurrentMoveUnits.setCurrentTacticalMove(move);
#endif

		AI_PERF_FORMAT("AI-perf-tact.csv", ("Barb Move: %d, Turn %03d, %s", (int)move.m_eMoveType, GC.getGame().getElapsedGameTurns(), m_pPlayer->getCivilizationShortDescription()) );

		switch(move.m_eMoveType)
		{
		case AI_TACTICAL_BARBARIAN_CAPTURE_CITY:
			PlotCaptureCityMoves();
			break;
		case AI_TACTICAL_BARBARIAN_DAMAGE_CITY:
			PlotDamageCityMoves();
			break;
		case AI_TACTICAL_BARBARIAN_DESTROY_HIGH_PRIORITY_UNIT:
			PlotDestroyUnitMoves(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT, false, true);
			break;
		case AI_TACTICAL_BARBARIAN_DESTROY_MEDIUM_PRIORITY_UNIT:
			PlotDestroyUnitMoves(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT, false, true);
			break;
		case AI_TACTICAL_BARBARIAN_DESTROY_LOW_PRIORITY_UNIT:
			PlotDestroyUnitMoves(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT, false, true);
			break;
		case AI_TACTICAL_BARBARIAN_MOVE_TO_SAFETY:
			PlotMovesToSafety(true /*bCombatUnits*/);
			break;
		case AI_TACTICAL_BARBARIAN_ATTRIT_HIGH_PRIORITY_UNIT:
			PlotDestroyUnitMoves(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT, false);
			break;
		case AI_TACTICAL_BARBARIAN_ATTRIT_MEDIUM_PRIORITY_UNIT:
			PlotDestroyUnitMoves(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT, false);
			break;
		case AI_TACTICAL_BARBARIAN_ATTRIT_LOW_PRIORITY_UNIT:
			PlotDestroyUnitMoves(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT, false);
			break;
		case AI_TACTICAL_BARBARIAN_PILLAGE:
			PlotPillageMoves(AI_TACTICAL_TARGET_IMPROVEMENT_RESOURCE, true/*bFirstPass*/);
			break;
		case AI_TACTICAL_BARBARIAN_PILLAGE_CITADEL:
			PlotPillageMoves(AI_TACTICAL_TARGET_CITADEL, true/*bFirstPass*/);
			break;
		case AI_TACTICAL_BARBARIAN_PILLAGE_NEXT_TURN:
			PlotPillageMoves(AI_TACTICAL_TARGET_CITADEL, false/*bFirstPass*/);
			PlotPillageMoves(AI_TACTICAL_TARGET_IMPROVEMENT_RESOURCE, false/*bFirstPass*/);
			break;
		case AI_TACTICAL_BARBARIAN_PRIORITY_BLOCKADE_RESOURCE:
			PlotBlockadeImprovementMoves();
			break;
		case AI_TACTICAL_BARBARIAN_CIVILIAN_ATTACK:
			PlotCivilianAttackMoves(AI_TACTICAL_TARGET_VERY_HIGH_PRIORITY_CIVILIAN);
			PlotCivilianAttackMoves(AI_TACTICAL_TARGET_HIGH_PRIORITY_CIVILIAN);
			PlotCivilianAttackMoves(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_CIVILIAN);
			PlotCivilianAttackMoves(AI_TACTICAL_TARGET_LOW_PRIORITY_CIVILIAN);
			break;
		case AI_TACTICAL_BARBARIAN_CAMP_DEFENSE:
			PlotCampDefenseMoves();
			break;
		case AI_TACTICAL_BARBARIAN_AGGRESSIVE_MOVE:
			PlotBarbarianMove(true /*bAggressive*/);
			break;
		case AI_TACTICAL_BARBARIAN_PASSIVE_MOVE:
			PlotBarbarianMove(false /*bAggressive*/);
			break;
		case AI_TACTICAL_BARBARIAN_DESPERATE_ATTACK:
			PlotDestroyUnitMoves(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT, false, true);
			break;
		case AI_TACTICAL_BARBARIAN_ESCORT_CIVILIAN:
			PlotBarbarianCivilianEscortMove();
			break;
		case AI_TACTICAL_BARBARIAN_PLUNDER_TRADE_UNIT:
			PlotBarbarianPlunderTradeUnitMove(DOMAIN_LAND);
			PlotBarbarianPlunderTradeUnitMove(DOMAIN_SEA);
			break;
		}
	}
}

/// Assign a group of units to take down each city we can capture
bool CvTacticalAI::PlotCaptureCityMoves()
{
	int iRequiredDamage;
	bool bAttackMade = false;

	// See how many moves of this type we can execute
	CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_CITY);
	while(pTarget != NULL)
	{
		// See what units we have who can reach target this turn
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
		if(pPlot != NULL && pPlot->isCity())
		{
			m_CurrentMoveCities.clear();
			CvCity* pCity = pPlot->getPlotCity();

			CvTacticalDominanceZone* pZone = GetTacticalAnalysisMap()->GetZoneByCity(pCity, false);
			if (pZone && pZone->GetDominanceFlag() == TACTICAL_DOMINANCE_ENEMY && !pCity->isInDangerOfFalling())
			{
				if(GC.getLogging() && GC.getAILogging())
				{
					CvString strPlayerName, strCityName, strLogString, strTemp;
					strPlayerName = m_pPlayer->getCivilizationShortDescription();
					strCityName = pCity->getName();
					strLogString.Format("City of ");
					strLogString += strCityName;
					strTemp.Format(", is in enemy dominated zone - won't attack, X: %d, Y: %d, ", pCity->getX(), pCity->getY());
					strLogString += strTemp + strPlayerName;
					LogTacticalMessage(strLogString);
				}

				pTarget = GetNextZoneTarget();
				continue;
			}

			//If don't have units to actually conquer, get out.
			if(!FindUnitsWithinStrikingDistance(pPlot, true /*bNoRanged*/, false /*bNavalOnly*/, false /*bMustMoveThrough*/, true /*bIncludeBlocked*/))
			{
				if(GC.getLogging() && GC.getAILogging())
				{
					CvString strPlayerName, strCityName, strLogString, strTemp;
					strPlayerName = GET_PLAYER(pCity->getOwner()).getCivilizationShortDescription();
					strCityName = pCity->getName();
					strLogString.Format("No units to capture city of ");
					strLogString += strCityName;
					strTemp.Format(", withdrawing, X: %d, Y: %d, ", pCity->getX(), pCity->getY());
					strLogString += strTemp + strPlayerName;
				}
				pTarget = GetNextZoneTarget();
				continue;
			}

			// Do we have enough firepower to destroy it?
			iRequiredDamage = pCity->GetMaxHitPoints() - pCity->getDamage();
			pTarget->SetAuxIntData(iRequiredDamage);
			// If we have the city already down to minimum, don't use ranged... Only try to capture.
			bool bNoRangedUnits = (iRequiredDamage <= 1);
			if(FindUnitsWithinStrikingDistance(pPlot, bNoRangedUnits, false /*bNavalOnly*/, true /*bMustMoveThrough*/, false /*bIncludeBlockedUnits*/))
			{
				if(ComputeTotalExpectedDamage(pTarget, pPlot) >= iRequiredDamage) 
				{
					// If so, execute moves to take it
					ExecuteAttack(pTarget, pPlot, false);
					bAttackMade = true;

					// Did it work?  If so, don't need a temporary dominance zone if had one here
					if(pPlot->getOwner() == m_pPlayer->GetID())
					{
						DeleteTemporaryZone(pPlot);
					}
					else
					{
						MoveUpReliefUnits(*pTarget);
					}
				}
			}
		}

		pTarget = GetNextZoneTarget();
	}
	return bAttackMade;
}

/// Assign a group of units to take down each city we can capture
bool CvTacticalAI::PlotDamageCityMoves()
{
	int iRequiredDamage;
	bool bAttackMade = false;

	// See how many moves of this type we can execute
	CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_CITY);
	while(pTarget != NULL)
	{
		// See what units we have who can reach target this turn
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
		if(pPlot != NULL && pPlot->isCity() )
		{
			m_CurrentMoveCities.clear();
			CvCity* pCity = pPlot->getPlotCity();

			CvTacticalDominanceZone* pZone = GetTacticalAnalysisMap()->GetZoneByCity(pCity, false);

			if (!pZone)
			{
				pTarget = GetNextZoneTarget();
				continue;
			}

			if (pZone && pZone->GetDominanceFlag() == TACTICAL_DOMINANCE_ENEMY && !IsTemporaryZoneCity(pCity))
			{
				if(GC.getLogging() && GC.getAILogging())
				{
					CvString strPlayerName, strCityName, strLogString, strTemp;
					strPlayerName = m_pPlayer->getCivilizationShortDescription();
					strCityName = pCity->getName();
					strLogString.Format("City of ");
					strLogString += strCityName;
					strTemp.Format(", is in enemy dominated zone - won't attack, X: %d, Y: %d, ", pCity->getX(), pCity->getY());
					strLogString += strTemp + strPlayerName;
					LogTacticalMessage(strLogString);
				}

				pTarget = GetNextZoneTarget();
				continue;
			}

			//If don't have units nearby to actually conquer, and bad dominance flag, get out.
			if (pZone && pZone->GetDominanceFlag() != TACTICAL_DOMINANCE_FRIENDLY && !IsTemporaryZoneCity(pCity) && !FindUnitsWithinStrikingDistance(pPlot, true /*bNoRanged*/, false /*bNavalOnly*/, false /*bMustMoveThrough*/, true /*bIncludeBlocked*/, true))
			{
				if(GC.getLogging() && GC.getAILogging())
				{
					CvString strPlayerName, strCityName, strLogString, strTemp;
					strPlayerName = m_pPlayer->getCivilizationShortDescription();
					strCityName = pCity->getName();
					strLogString.Format("Pulling back from City of ");
					strLogString += strCityName;
					strTemp.Format(", no melee support, X: %d, Y: %d, ", pCity->getX(), pCity->getY());
					strLogString += strTemp + strPlayerName;
					LogTacticalMessage(strLogString);
				}

				pTarget = GetNextZoneTarget();
				continue;
			}

			iRequiredDamage = pCity->GetMaxHitPoints() - pCity->getDamage();
			pTarget->SetAuxIntData(iRequiredDamage);
			// If we have the city already down to minimum, don't use ranged... Only try to capture.
			bool bNoRangedUnits = (iRequiredDamage <= 1);
			//ideally we should check unit danger, respectively if it can survive an attack
			if(FindUnitsWithinStrikingDistance(pPlot, bNoRangedUnits, false /*bNavalOnly*/, false /*bMustMoveThrough*/, true /*bIncludeBlockedUnits*/, true))
			{
				int iExpectedDamage = ComputeTotalExpectedDamage(pTarget, pPlot);

				// Don't want to hammer away to try and take down a city for more than 12 turns
				if ((iExpectedDamage - GC.getCITY_HIT_POINTS_HEALED_PER_TURN()) > (iRequiredDamage / 12))
				{
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("Laying siege to %s, required damage %d, expected damage %d", pCity->getName().c_str(), iRequiredDamage, iExpectedDamage);
						LogTacticalMessage(strLogString);
					}

					//see whether we need to preserve melee units for capturing
					int iRangedCount = 0, iMeleeCount = 0;
					for (unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
					{
						CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
						if (!pUnit || !pUnit->canMove())
							continue;

						// Are we a melee unit
						if (pUnit->IsCanAttackRanged())
							iRangedCount++;
						else
							iMeleeCount++;
					}

					// Fire away!
					ExecuteAttack(pTarget, pPlot, iMeleeCount<3);
					bAttackMade = true;

					MoveUpReliefUnits(*pTarget);
				}
				else
				{
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("Siege of %s is pointless, required damage %d, expected damage %d", pCity->getName().c_str(), iRequiredDamage, iExpectedDamage);
						LogTacticalMessage(strLogString);
					}
				}
			}
		}

		pTarget = GetNextZoneTarget();
	}
	return bAttackMade;
}

/// Assign a group of units to take down each city we can capture
bool CvTacticalAI::PlotNavalDamageCityMoves()
{
	int iRequiredDamage;
	bool bAttackMade = false;

	// See how many moves of this type we can execute
	CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_CITY);
	while (pTarget != NULL)
	{
		// See what units we have who can reach target this turn
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
		if (pPlot != NULL && pPlot->isCity())
		{
			m_CurrentMoveCities.clear();
			CvCity* pCity = pPlot->getPlotCity();

			CvTacticalDominanceZone* pZone = GetTacticalAnalysisMap()->GetZoneByCity(pCity, true);

			if (!pZone)
			{
				pTarget = GetNextZoneTarget();
				continue;
			}

			if (pZone && !pZone->IsNavalInvasion() && pZone->GetDominanceFlag() == TACTICAL_DOMINANCE_ENEMY && !IsTemporaryZoneCity(pCity))
			{
				if (GC.getLogging() && GC.getAILogging())
				{
					CvString strPlayerName, strCityName, strLogString, strTemp;
					strPlayerName = m_pPlayer->getCivilizationShortDescription();
					strCityName = pCity->getName();
					strLogString.Format("City of ");
					strLogString += strCityName;
					strTemp.Format(", is in enemy dominated COASTAL zone - won't attack, X: %d, Y: %d, ", pCity->getX(), pCity->getY());
					strLogString += strTemp + strPlayerName;
					LogTacticalMessage(strLogString);
				}

				pTarget = GetNextZoneTarget();
				continue;
			}

			//If don't have units nearby to actually conquer, and bad dominance flag, get out.
			if (pZone && !FindUnitsWithinStrikingDistance(pPlot, true /*bNoRanged*/, true /*bNavalOnly*/, false /*bMustMoveThrough*/, true /*bIncludeBlocked*/, true))
			{
				if (GC.getLogging() && GC.getAILogging())
				{
					CvString strPlayerName, strCityName, strLogString, strTemp;
					strPlayerName = m_pPlayer->getCivilizationShortDescription();
					strCityName = pCity->getName();
					strLogString.Format("NAVY Pulling back from City of ");
					strLogString += strCityName;
					strTemp.Format(", no melee support, X: %d, Y: %d, ", pCity->getX(), pCity->getY());
					strLogString += strTemp + strPlayerName;
					LogTacticalMessage(strLogString);
				}

				pTarget = GetNextZoneTarget();
				continue;
			}

			iRequiredDamage = pCity->GetMaxHitPoints() - pCity->getDamage();
			pTarget->SetAuxIntData(iRequiredDamage);
			// If we have the city already down to minimum, don't use ranged... Only try to capture.
			bool bNoRangedUnits = (iRequiredDamage <= 1);
			//ideally we should check unit danger, respectively if it can survive an attack
			if (FindUnitsWithinStrikingDistance(pPlot, bNoRangedUnits, true /*bNavalOnly*/, false /*bMustMoveThrough*/, true /*bIncludeBlockedUnits*/, true))
			{
				int iExpectedDamage = ComputeTotalExpectedDamage(pTarget, pPlot);

				// Don't want to hammer away to try and take down a city for more than 20 turns
				if ((iExpectedDamage - GC.getCITY_HIT_POINTS_HEALED_PER_TURN()) > (iRequiredDamage / 20))
				{
					if (GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("Laying NAVAL siege to %s, required damage %d, expected damage %d", pCity->getName().c_str(), iRequiredDamage, iExpectedDamage);
						LogTacticalMessage(strLogString);
					}

					//see whether we need to preserve melee units for capturing
					int iRangedCount = 0, iMeleeCount = 0;
					for (unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
					{
						CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
						if (!pUnit || !pUnit->canMove())
							continue;

						if (pUnit->getDomainType() != DOMAIN_SEA)
							continue;

						// Are we a melee unit
						if (pUnit->IsCanAttackRanged())
							iRangedCount++;
						else
							iMeleeCount++;
					}

					// Fire away!
					ExecuteAttack(pTarget, pPlot, iMeleeCount<3);
					bAttackMade = true;

					MoveUpReliefUnits(*pTarget);
				}
				else
				{
					if (GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("NAVAL Siege of %s is pointless, required damage %d, expected damage %d", pCity->getName().c_str(), iRequiredDamage, iExpectedDamage);
						LogTacticalMessage(strLogString);
					}
				}
			}
		}

		pTarget = GetNextZoneTarget();
	}
	return bAttackMade;
}

/// Assign a unit to capture an undefended barbarian camp
void CvTacticalAI::PlotBarbarianCampMoves()
{
	bool bLog = GC.getLogging() && GC.getAILogging();
	CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_BARBARIAN_CAMP);
	while(pTarget != NULL)
	{
		// See what units we have who can reach target this turn
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
		if(FindClosestUnit(pPlot,8,false))
		{
			// Queue best one up to capture it
			ExecuteBarbarianCampMove(pPlot);
			if(bLog)
			{
				CvString strLogString;
				strLogString.Format("Removing barbarian camp, X: %d, Y: %d", pPlot->getX(), pPlot->getY());
				LogTacticalMessage(strLogString);
			}
			DeleteTemporaryZone(pPlot);
		}
		pTarget = GetNextZoneTarget();
	}
}

/// Assign a group of units to attack each unit we think we can destroy
void CvTacticalAI::PlotDestroyUnitMoves(AITacticalTargetType targetType, bool bMustBeAbleToKill, bool bAttackAtPoorOdds)
{
	int iRequiredDamage;
	int iExpectedDamage;

	// See how many moves of this type we can execute
	CvTacticalTarget* pTarget = GetFirstZoneTarget(targetType);
	while(pTarget != NULL)
	{
		bool bUnitCanAttack = false;
		bool bCityCanAttack = false;

		// See what units we have who can reach targets this turn
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
		CvUnit* pDefender = pPlot->getVisibleEnemyDefender(m_pPlayer->GetID());
		if(pDefender)
		{
			bUnitCanAttack = FindUnitsWithinStrikingDistance(pPlot);
			bCityCanAttack = FindCitiesWithinStrikingDistance(pPlot);
			if(bUnitCanAttack || bCityCanAttack)
			{
				iExpectedDamage = ComputeTotalExpectedDamage(pTarget, pPlot);
				iExpectedDamage += ComputeTotalExpectedCityBombardDamage(pDefender);
				iRequiredDamage = pDefender->GetCurrHitPoints();
				pTarget->SetAuxIntData(iRequiredDamage);

				if(!bMustBeAbleToKill)
				{
					// Put in any attacks where we'll inflict at least equal damage
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;

						CvString strPlayerName = GET_PLAYER(pDefender->getOwner()).getCivilizationShortDescription();

						CvUnitEntry* pkUnitInfo = GC.getUnitInfo(pDefender->getUnitType());
						CvString strTemp = (pkUnitInfo != NULL)? pkUnitInfo->GetDescription() : "Unknown Unit Type";

						switch(targetType)
						{
						case AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT:
							strLogString.Format("Looking at damaging high priority %s, X: %d, Y: %d,", strTemp.GetCString(),
							                    pDefender->getX(), pDefender->getY());
							break;
						case AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT:
							strLogString.Format("Looking at damaging medium priority %s, X: %d, Y: %d,", strTemp.GetCString(),
							                    pDefender->getX(), pDefender->getY());
							break;
						case AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT:
							strLogString.Format("Looking at damaging low priority %s, X: %d, Y: %d,", strTemp.GetCString(),
							                    pDefender->getX(), pDefender->getY());
							break;
						}
						strLogString += strPlayerName;
						LogTacticalMessage(strLogString);
					}

					ExecuteAttack(pTarget, pPlot, !bAttackAtPoorOdds);
				}
				// Do we have enough firepower to destroy it?
				else
				{
					if(iExpectedDamage > iRequiredDamage)
					{
						// If so, execute enough moves to destroy it
						if(GC.getLogging() && GC.getAILogging())
						{
							CvString strLogString, strTemp, strPlayerName;
							strPlayerName = GET_PLAYER(pDefender->getOwner()).getCivilizationShortDescription();
							strTemp = GC.getUnitInfo(pDefender->getUnitType())->GetDescription();
							switch(targetType)
							{
							case AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT:
								strLogString.Format("Looking at killing high priority %s, X: %d, Y: %d,", strTemp.GetCString(),
								                    pDefender->getX(), pDefender->getY());
								break;
							case AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT:
								strLogString.Format("Looking at killing medium priority %s, X: %d, Y: %d,", strTemp.GetCString(),
								                    pDefender->getX(), pDefender->getY());
								break;
							case AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT:
								strLogString.Format("Looking at killing low priority %s, X: %d, Y: %d,", strTemp.GetCString(),
								                    pDefender->getX(), pDefender->getY());
								break;
							}
							strLogString += strPlayerName;
							LogTacticalMessage(strLogString);
						}
						ExecuteAttack(pTarget, pPlot, false);
					}
				}
			}
		}
		pTarget = GetNextZoneTarget();
	}
}

/// Moved endangered units to safe hexes
void CvTacticalAI::PlotMovesToSafety(bool bCombatUnits)
{
	int iDangerLevel;

	list<int>::iterator it;
	m_CurrentMoveUnits.clear();

	// Loop through all recruited units
	for(it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if(pUnit)
		{
			// Danger value of plot must be greater than 0
			CvPlot* pPlot = pUnit->plot();

			iDangerLevel = m_pPlayer->GetPlotDanger(*pPlot,pUnit);
			if(iDangerLevel > 0)
			{
				bool bAddUnit = false;
				if(bCombatUnits)
				{
					// If under 100% health, might flee to safety
					if(pUnit->GetCurrHitPoints() < pUnit->GetMaxHitPoints())
					{
#if defined(MOD_BALANCE_CORE)
						int iDamage = pUnit->plot()->getTurnDamage(pUnit->ignoreTerrainDamage(), pUnit->ignoreFeatureDamage(), pUnit->extraTerrainDamage(), pUnit->extraFeatureDamage());
#endif
						if(pUnit->isBarbarian())
						{
							// Barbarian combat units - only naval units flee (but they flee if have taken ANY damage)
							if(pUnit->getDomainType() == DOMAIN_SEA)
							{
								bAddUnit = true;
							}
						}

#if defined(MOD_AI_SMART_FLEE_FROM_DANGER)
						else if(iDamage > 0 && (((pUnit->getDamage()*100)/pUnit->GetMaxHitPoints())>50))
						{
							bAddUnit = true;
						}
						// Everyone else flees at more than 70% damage
						else if(MOD_AI_SMART_FLEE_FROM_DANGER && (((pUnit->getDamage()*100)/pUnit->GetMaxHitPoints())>70) )
						{
							bAddUnit = true;
						}
						// Everyone else flees at less than or equal to 50% combat strength
						else if(pUnit->GetBaseCombatStrengthConsideringDamage() * 2 <= pUnit->GetBaseCombatStrength())
						{
							bAddUnit = true;
						}
						// Everyone flees under (heavy) enemy fire
						else if(pUnit->isProjectedToDieNextTurn())
						{
							bAddUnit = true;
						}

						//But not if we're in a city!
						if(pUnit->IsGarrisoned())
						{
							bAddUnit = false;
						}
						if(pUnit->getArmyID() != -1)
						{
							bAddUnit = false;
						}

#else
						// Everyone else flees at less than or equal to 50% combat strength
						else if(pUnit->IsUnderEnemyRangedAttack() || pUnit->GetBaseCombatStrengthConsideringDamage() * 2 <= pUnit->GetBaseCombatStrength())
						{
							bAddUnit = true;
						}
#endif
					}

					// Also flee if danger is really high in current plot (but not if we're barbarian)
					else if(!pUnit->isBarbarian())
					{
						if(iDangerLevel*1.5 > pUnit->GetCurrHitPoints())
						{
							bAddUnit = true;
						}
					}
				}
				else
				{
					// Civilian (or embarked) units always flee from danger
					if(!pUnit->IsCanDefend())
					{
						bAddUnit = true;
					}
#if defined(MOD_BALANCE_CORE)
					//GGs and GAs need to stay in the DANGER ZONE, but only if there are units near it to support it.
					int iUnits = 0;
					if(pUnit->IsGreatAdmiral() || pUnit->IsGreatGeneral())
					{
						//can't use garrison check here. while non-combat units may be in a city, they are not the garrison
						if(pUnit->plot()->isCity())
						{
							bAddUnit = false;
						}
						else if(pUnit->plot()->getNumDefenders(pUnit->getOwner()) > 0)
						{
							CvUnit* pUnit2 = pUnit->plot()->getUnitByIndex(0);
							//Are we under a relatively healthy unit? Alright, let's drill down and see what's around us.
							if(pUnit2 != NULL && pUnit2->GetCurrHitPoints() > (pUnit2->GetMaxHitPoints() / 2))
							{
								// If there a hex adjacent to city they can airlift to?
								for (int iI = 0; iI < NUM_DIRECTION_TYPES; iI++)
								{
									CvPlot *pLoopPlot = plotDirection(pUnit->getX(), pUnit->getY(), ((DirectionTypes)iI));
									if (pLoopPlot != NULL)
									{
										if(pLoopPlot->getNumDefenders(pUnit->getOwner()) > 0)
										{
											iUnits++;
										}
									}
								}
							}
						}
					}
					//Two or more units nearby? We're staying in the DANGER ZONE.
					if(iUnits > 1)
					{
						bAddUnit = false;
					}
#endif
				}

				if(bAddUnit)
				{
					// Just one unit involved in this move to execute
					CvTacticalUnit unit;
					unit.SetID(pUnit->GetID());
					m_CurrentMoveUnits.push_back(unit);
				}
			}
		}
	}

	if(m_CurrentMoveUnits.size() > 0)
	{
		ExecuteMovesToSafestPlot();
	}
}

/// Move units to a better location
void CvTacticalAI::PlotRepositionMoves()
{
	list<int>::iterator it;
	CvTacticalUnit unit;

	m_CurrentMoveUnits.clear();

	// Loop through all recruited units
	for(it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if(pUnit)
		{
			if (!pUnit->canMove())
				continue;
			
			if (pUnit->hasCurrentTacticalMove())
				continue;

			// Never use this (default) move for Great Admirals or Generals
			if (pUnit->IsGreatGeneral() || pUnit->IsGreatAdmiral() || pUnit->IsCityAttackSupport())
			{
				continue;
			}

			unit.SetID(pUnit->GetID());
			m_CurrentMoveUnits.push_back(unit);
		}
	}

	if(m_CurrentMoveUnits.size() > 0)
	{
		ExecuteRepositionMoves();
	}
}

/// Move barbarians across the map
void CvTacticalAI::PlotBarbarianMove(bool bAggressive)
{
	list<int>::iterator it;
	CvTacticalUnit unit;

	if(m_pPlayer->isBarbarian())
	{
		m_CurrentMoveUnits.clear();

		// Loop through all recruited units
		for(it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
		{
			CvUnit* pUnit = m_pPlayer->getUnit(*it);
			if(pUnit)
			{
				unit.SetID(pUnit->GetID());
				m_CurrentMoveUnits.push_back(unit);
			}
		}

		if(m_CurrentMoveUnits.size() > 0)
		{
			ExecuteBarbarianMoves(bAggressive);
		}
	}
}

/// Escort captured civilians back to barbarian camps
void CvTacticalAI::PlotBarbarianCivilianEscortMove()
{
	list<int>::iterator it;
	CvTacticalUnit unit;

	if(m_pPlayer->isBarbarian())
	{
		m_CurrentMoveUnits.clear();

		for(it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
		{
			CvUnit* pUnit = m_pPlayer->getUnit(*it);

			// Find any civilians we may have "acquired" from the civs
			if(!pUnit->IsCombatUnit())
			{
				unit.SetID(pUnit->GetID());
				m_CurrentMoveUnits.push_back(unit);
			}
		}

		if(m_CurrentMoveUnits.size() > 0)
		{
			ExecuteBarbarianCivilianEscortMove();
		}
	}
}

/// Plunder trade routes
void CvTacticalAI::PlotBarbarianPlunderTradeUnitMove(DomainTypes eDomain)
{
	AITacticalTargetType eTargetType = AI_TACTICAL_TARGET_NONE;
	bool bNavalOnly = false;
	if (eDomain == DOMAIN_LAND)
	{
		eTargetType = AI_TACTICAL_TARGET_TRADE_UNIT_LAND;
	}
	else if (eDomain == DOMAIN_SEA)
	{
		eTargetType = AI_TACTICAL_TARGET_TRADE_UNIT_SEA;
		bNavalOnly = true;
	}

	CvAssertMsg(eTargetType != AI_TACTICAL_TARGET_NONE, "Undefined domain type for target");
	if (eTargetType == AI_TACTICAL_TARGET_NONE)
	{
		return;
	}

	CvTacticalTarget* pTarget;
	pTarget = GetFirstZoneTarget(eTargetType);
	while(pTarget != NULL)
	{
		// See what units we have who can reach target this turn
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());

		if (FindUnitsWithinStrikingDistance(pPlot, false /* bNoRangedUnits */, bNavalOnly, true /*bMustMoveThrough*/, false /*bIncludeBlockedUnits*/))
		{
			// Queue best one up to capture it
			ExecutePlunderTradeUnit(pPlot);

			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("Plundering trade unit, X: %d, Y: %d", pTarget->GetTargetX(), pTarget->GetTargetY());
				LogTacticalMessage(strLogString);
			}
		}

		pTarget = GetNextZoneTarget();
	}
}

/// Process units that we recruited out of operational moves.
void CvTacticalAI::PlotOperationalArmyMoves()
{
	std::vector<int> opsToKill;

	// move all units in operations
	CvAIOperation* nextOp = m_pPlayer->getFirstAIOperation();
	while(nextOp != NULL)
	{
		if (!nextOp->DoTurn())
			opsToKill.push_back(nextOp->GetID());

		nextOp = m_pPlayer->getNextAIOperation();
	}

	//clean up - have to do this in two steps so the iterator does not get invalidated
	for (size_t i=0; i<opsToKill.size(); i++)
		m_pPlayer->getAIOperation(opsToKill[i])->Kill();
}

/// Assigns units to pillage enemy improvements
void CvTacticalAI::PlotPillageMoves(AITacticalTargetType eTarget, bool bFirstPass)
{
	int iPillageHeal = GC.getPILLAGE_HEAL_AMOUNT();

	CvString szTargetName = "";
	if(GC.getLogging() && GC.getAILogging())
	{
		if (eTarget == AI_TACTICAL_TARGET_CITADEL)
		{
			szTargetName = "Citadel";
		}
		else if (eTarget == AI_TACTICAL_TARGET_IMPROVEMENT_RESOURCE)
		{
			szTargetName = "Improved Resource";
		}
		else
		{
			szTargetName = "Improvement";
		}
	}

	CvTacticalTarget* pTarget;
	pTarget = GetFirstZoneTarget(eTarget);
	while(pTarget != NULL)
	{
		// See what units we have who can reach target this turn
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());

		//don't pillage on water
		if (pPlot->isWater())
		{
			pTarget = GetNextZoneTarget();
			continue;
		}

		// try paratroopers first, not because they are more effective, just because it looks cooler...
		if (bFirstPass && FindParatroopersWithinStrikingDistance(pPlot))
		{
			// Queue best one up to capture it
			ExecuteParadropPillage(pPlot);

			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("Paratrooping in to pillage %s, X: %d, Y: %d", szTargetName.GetCString(), pTarget->GetTargetX(), pTarget->GetTargetY());
				LogTacticalMessage(strLogString);
			}

		}
		else if (bFirstPass && FindUnitsForPillage(pPlot,0,33,GC.getMAX_HIT_POINTS()-iPillageHeal))
		{
			// Queue best one up to capture it
			ExecutePillage(pPlot);

			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("Pillaging %s, X: %d, Y: %d", szTargetName.GetCString(), pTarget->GetTargetX(), pTarget->GetTargetY());
				LogTacticalMessage(strLogString);
			}
		}
		else if (!bFirstPass && FindUnitsForPillage(pPlot,2,33,GC.getMAX_HIT_POINTS()-iPillageHeal))
		{
			ExecuteMoveToTarget(pPlot);

			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("Moving toward %s for pillage, X: %d, Y: %d", szTargetName.GetCString(), pTarget->GetTargetX(), pTarget->GetTargetY());
				LogTacticalMessage(strLogString);
			}
		}

		pTarget = GetNextZoneTarget();
	}
}

/// Assigns units to sit on trade routes it knows about and wait for enemy trade units
void CvTacticalAI::PlotPlunderTradePlotMoves (DomainTypes eDomain)
{
	AITacticalTargetType eTargetType = AI_TACTICAL_TARGET_NONE;
	bool bNavalOnly = false;
	if (eDomain == DOMAIN_LAND)
	{
		eTargetType = AI_TACTICAL_TARGET_TRADE_UNIT_LAND_PLOT;
	}
	else if (eDomain == DOMAIN_SEA)
	{
		eTargetType = AI_TACTICAL_TARGET_TRADE_UNIT_SEA_PLOT;
		bNavalOnly = true;
	}

	CvAssertMsg(eTargetType != AI_TACTICAL_TARGET_NONE, "Undefined domain type for target");
	if (eTargetType == AI_TACTICAL_TARGET_NONE)
	{
		return;
	}

	CvTacticalTarget* pTarget;
	pTarget = GetFirstZoneTarget(eTargetType);
	while(pTarget != NULL)
	{
		// See what units we have who can reach target this turn
		CvPlot* pTargetPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
		if (FindUnitsWithinStrikingDistance(pTargetPlot, false /* bNoRangedUnits */, bNavalOnly, false /*bMustMoveThrough*/, false /*bIncludeBlockedUnits*/))
		{
			CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[0].GetID());
			if(pUnit)
			{
				ExecuteMoveToPlotIgnoreDanger(pUnit,pTargetPlot,true);
				if (pUnit->canPillage(pUnit->plot()) && pUnit->getDamage() > 0)
				{
					pUnit->PushMission(CvTypes::getMISSION_PILLAGE());
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strMsg;
						strMsg.Format("Pillaging during a plunder mission with %s, X: %d, Y: %d", pUnit->getName().GetCString(), pUnit->getX(), pUnit->getY());
						LogTacticalMessage(strMsg, false);
					}
				}
				pUnit->finishMoves();
			
				// Delete this unit from those we have to move
				UnitProcessed(pUnit->GetID());
			}
		}

		pTarget = GetNextZoneTarget();
	}
}

/// Assigns units to plunder trade units it sees 
void CvTacticalAI::PlotPlunderTradeUnitMoves (DomainTypes eDomain)
{
	AITacticalTargetType eTargetType = AI_TACTICAL_TARGET_NONE;
	bool bNavalOnly = false;
	if (eDomain == DOMAIN_LAND)
	{
		eTargetType = AI_TACTICAL_TARGET_TRADE_UNIT_LAND;
	}
	else if (eDomain == DOMAIN_SEA)
	{
		eTargetType = AI_TACTICAL_TARGET_TRADE_UNIT_SEA;
		bNavalOnly = true;
	}

	CvAssertMsg(eTargetType != AI_TACTICAL_TARGET_NONE, "Undefined domain type for target");
	if (eTargetType == AI_TACTICAL_TARGET_NONE)
	{
		return;
	}

	CvTacticalTarget* pTarget;
	pTarget = GetFirstZoneTarget(eTargetType);
	while(pTarget != NULL)
	{
		// See what units we have who can reach target this turn
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
		if (FindUnitsWithinStrikingDistance(pPlot, false /* bNoRangedUnits */, bNavalOnly, true /*bMustMoveThrough*/, false /*bIncludeBlockedUnits*/))
		{
			// Queue best one up to capture it
			ExecutePlunderTradeUnit(pPlot);

			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("Plundering trade unit, X: %d, Y: %d", pTarget->GetTargetX(), pTarget->GetTargetY());
				LogTacticalMessage(strLogString);
			}
		}

		pTarget = GetNextZoneTarget();
	}
}

/// Move barbarian ships to disrupt usage of water improvements
void CvTacticalAI::PlotBlockadeImprovementMoves()
{
	CvTacticalTarget* pTarget;
	pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_BLOCKADE_RESOURCE_POINT);
	while (pTarget != NULL)
	{
		// Grab units that make sense for this move type
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
		FindUnitsForThisMove((TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_POSTURE_SHORE_BOMBARDMENT], pPlot, -1);

		if (m_CurrentMoveHighPriorityUnits.size() + m_CurrentMoveUnits.size() > 0)
		{
			ExecuteNavalBlockadeMove(pPlot);
			if (GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("Moving into enemy territory for a naval blockade with move to, X: %d, Y: %d", pTarget->GetTargetX(), pTarget->GetTargetY());
				LogTacticalMessage(strLogString);
			}
		}
		pTarget = GetNextZoneTarget();
	}
}

/// Assigns units to capture undefended civilians
void CvTacticalAI::PlotCivilianAttackMoves(AITacticalTargetType eTargetType)
{
	CvTacticalTarget* pTarget;
	pTarget = GetFirstZoneTarget(eTargetType);
	while(pTarget != NULL)
	{
		// See what units we have who can reach target this turn
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
		if(FindUnitsWithinStrikingDistance(pPlot, false /* bNoRangedUnits */, false /*bNavalOnly*/, false /*bMustMoveThrough*/, false /*bIncludeBlockedUnits*/))
		{
			// Queue best one up to capture it
			ExecuteCivilianCapture(pPlot);

			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				switch(eTargetType)
				{
				case AI_TACTICAL_TARGET_VERY_HIGH_PRIORITY_CIVILIAN:
					strLogString.Format("Attacking very high priority civilian, X: %d, Y: %d", pTarget->GetTargetX(),
					                    pTarget->GetTargetY());
					break;
				case AI_TACTICAL_TARGET_HIGH_PRIORITY_CIVILIAN:
					strLogString.Format("Attacking high priority civilian, X: %d, Y: %d", pTarget->GetTargetX(),
					                    pTarget->GetTargetY());
					break;
				case AI_TACTICAL_TARGET_MEDIUM_PRIORITY_CIVILIAN:
					strLogString.Format("Attacking medium priority civilian, X: %d, Y: %d", pTarget->GetTargetX(),
					                    pTarget->GetTargetY());
					break;
				case AI_TACTICAL_TARGET_LOW_PRIORITY_CIVILIAN:
					strLogString.Format("Attacking low priority civilian, X: %d, Y: %d", pTarget->GetTargetX(),
					                    pTarget->GetTargetY());
					break;
				}
				LogTacticalMessage(strLogString);
			}
		}
		pTarget = GetNextZoneTarget();
	}
}

/// Find all targets that we can bombard easily
void CvTacticalAI::PlotSafeBombardMoves()
{
	CvTacticalTarget* pTarget;

	pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT);
	while(pTarget != NULL && pTarget->IsTargetStillAlive(m_pPlayer->GetID()))
	{
		ExecuteSafeBombards(*pTarget);
		pTarget = GetNextZoneTarget();
	}

	pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT);
	while(pTarget != NULL && pTarget->IsTargetStillAlive(m_pPlayer->GetID()))
	{
		ExecuteSafeBombards(*pTarget);
		pTarget = GetNextZoneTarget();
	}

	pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT);
	while(pTarget != NULL && pTarget->IsTargetStillAlive(m_pPlayer->GetID()))
	{
		ExecuteSafeBombards(*pTarget);
		pTarget = GetNextZoneTarget();
	}

	pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_EMBARKED_MILITARY_UNIT);
	while(pTarget != NULL && pTarget->IsTargetStillAlive(m_pPlayer->GetID()))
	{
		ExecuteSafeBombards(*pTarget);
		pTarget = GetNextZoneTarget();
	}
}

/// Assigns units to heal
void CvTacticalAI::PlotHealMoves()
{
	list<int>::iterator it;
	m_CurrentMoveUnits.clear();

	// Loop through all recruited units
	for(it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if(pUnit)
		{
			//the really bad cases have already been added in CommandeerUnits
			//now we need to take care of the units which are damaged and have nothing else to do

			CvPlot* pUnitPlot = pUnit->plot();
			int iAcceptableDamage = 10;
			//allow some more damage outside of our borders
			if (pUnitPlot->getOwner() != pUnit->getOwner())
				iAcceptableDamage = 20;

			if (pUnit->getDamage()>iAcceptableDamage && pUnit->getArmyID()==-1 && FindNearbyTarget(pUnit,7)==NULL)
				m_HealingUnits.insert(pUnit->GetID());
		}
	}

	if(m_HealingUnits.size() > 0)
	{
		ExecuteHeals();
	}
}

/// Assigns a barbarian to go protect an undefended camp
void CvTacticalAI::PlotCampDefenseMoves()
{
	CvTacticalTarget* pTarget;
	pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_BARBARIAN_CAMP);
	while(pTarget != NULL)
	{
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());

		//for the barbarian player AI_TACTICAL_TARGET_BARBARIAN_CAMP does not automatically mean the camp is empty of _barbarian_ defenders (check is only for enemy units)
		CvUnit* currentDefender = pPlot->getBestDefender(BARBARIAN_PLAYER);
		if (currentDefender)
		{
			if (TacticalAIHelpers::PerformRangedAttackWithoutMoving(currentDefender))
				currentDefender->finishMoves();
			else if (currentDefender->canFortify(pPlot))
				currentDefender->PushMission(CvTypes::getMISSION_FORTIFY());
			else
				currentDefender->PushMission(CvTypes::getMISSION_SKIP());
			UnitProcessed(currentDefender->GetID());

			//that's a hack but that's the way it is
			currentDefender->setTacticalMove( (TacticalAIMoveTypes)AI_TACTICAL_BARBARIAN_CAMP_DEFENSE );
		}
		else if(FindClosestUnit(pPlot,5,false,false,0,false,false,false,NULL,1))
		{
			ExecuteMoveToPlotIgnoreDanger(pPlot);
			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("Moving to protect camp, X: %d, Y: %d", pTarget->GetTargetX(), pTarget->GetTargetY());
				LogTacticalMessage(strLogString);
			}
		}
		pTarget = GetNextZoneTarget();
	}
}

/// Make a defensive move to garrison a city
void CvTacticalAI::PlotGarrisonMoves(int iNumTurnsAway, bool bMustAllowRangedAttack)
{
	CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_CITY_TO_DEFEND);
	while(pTarget != NULL)
	{
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
		CvCity* pCity = pPlot->getPlotCity();

		if(!pCity)
			continue;

		CvUnit* pGarrison = pCity->GetGarrisonedUnit();
		if (pGarrison)
		{
			//ranged garrisons are used in ExecuteSafeBombards. special handling only for melee garrisons here
			for (int i=RING0_PLOTS; i<RING1_PLOTS; i++)
			{
				CvPlot* pNeighbor = iterateRingPlots( pPlot,i ); //todo: randomize order? but the chance of multiple potential victims is low
				if (pNeighbor)
				{
					CvUnit* pEnemy = pNeighbor->getBestDefender(NO_PLAYER,m_pPlayer->GetID(),pGarrison,true);
					//attacker will not advance ...
					if (pEnemy && TacticalAIHelpers::KillUnitIfPossible(pGarrison, pEnemy))
						break;
				}
			}

			if (pGarrison->canFortify(pGarrison->plot()))
				pGarrison->PushMission(CvTypes::getMISSION_FORTIFY());
			else
				pGarrison->PushMission(CvTypes::getMISSION_SKIP());
		}
		else if ( !pCity->isInDangerOfFalling() )
		{
			// Grab units that make sense for this move type
			FindUnitsForThisMove((TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_GARRISON_ALREADY_THERE], pPlot, iNumTurnsAway, bMustAllowRangedAttack);

			if(m_CurrentMoveHighPriorityUnits.size() + m_CurrentMoveUnits.size() > 0)
			{
				ExecuteMoveToTarget(pPlot);

				if(GC.getLogging() && GC.getAILogging())
				{
					CvString strLogString;
					strLogString.Format("Garrison, X: %d, Y: %d, Priority: %d, Turns Away: %d", pTarget->GetTargetX(), pTarget->GetTargetY(), pTarget->GetAuxIntData(), iNumTurnsAway);
					if(bMustAllowRangedAttack)
					{
						strLogString += ", Allows bombard";
					}
					LogTacticalMessage(strLogString);
				}
			}
			else
			{
				if(GC.getLogging() && GC.getAILogging())
				{
					CvString strLogString;
					strLogString.Format("No unit for garrison in %s at (%d:%d)", pCity->getName().c_str(), pTarget->GetTargetX(), pTarget->GetTargetY());
					LogTacticalMessage(strLogString);
				}
			}
		}

		pTarget = GetNextZoneTarget();
	}
}

/// Establish a defensive bastion adjacent to a city
void CvTacticalAI::PlotBastionMoves(int iNumTurnsAway)
{
	CvTacticalTarget* pTarget;
	pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_DEFENSIVE_BASTION);
	while(pTarget != NULL)
	{
		// Grab units that make sense for this move type
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
		FindUnitsForThisMove((TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_BASTION_ALREADY_THERE], pPlot, iNumTurnsAway);

		if(m_CurrentMoveHighPriorityUnits.size() + m_CurrentMoveUnits.size() > 0)
		{
			ExecuteMoveToTarget(pPlot);
			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("Bastion, X: %d, Y: %d, Priority: %d, Turns Away: %d", pTarget->GetTargetX(), pTarget->GetTargetY(), pTarget->GetAuxIntData(), iNumTurnsAway);
				LogTacticalMessage(strLogString);
			}
		}
		pTarget = GetNextZoneTarget();
	}
}

/// Make a defensive move to guard an improvement
void CvTacticalAI::PlotGuardImprovementMoves(int iNumTurnsAway)
{
	CvTacticalTarget* pTarget;
	pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_IMPROVEMENT_TO_DEFEND);
	while(pTarget != NULL)
	{
		// Grab units that make sense for this move type
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
		FindUnitsForThisMove((TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_GUARD_IMPROVEMENT_ALREADY_THERE], pPlot, iNumTurnsAway);

		if(m_CurrentMoveHighPriorityUnits.size() + m_CurrentMoveUnits.size() > 0)
		{
			ExecuteMoveToTarget(pPlot);
			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("Guard Improvement, X: %d, Y: %d, Turns Away: %d", pTarget->GetTargetX(), pTarget->GetTargetY(), iNumTurnsAway);
				LogTacticalMessage(strLogString);
			}
		}
		pTarget = GetNextZoneTarget();
	}
}

/// Pop goody huts nearby
void CvTacticalAI::PlotAncientRuinMoves(int iNumTurnsAway)
{
	CvTacticalTarget* pTarget;
	pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_ANCIENT_RUINS);
	while(pTarget != NULL)
	{
		// Grab units that make sense for this move type
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
		FindUnitsForThisMove((TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_ANCIENT_RUINS], pPlot, iNumTurnsAway);

		if(m_CurrentMoveHighPriorityUnits.size() + m_CurrentMoveUnits.size() > 0)
		{
			ExecuteMoveToTarget(pPlot);

			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("Moving to goody hut, X: %d, Y: %d, Turns Away: %d", pTarget->GetTargetX(), pTarget->GetTargetY(), iNumTurnsAway);
				LogTacticalMessage(strLogString);
			}
		}
		pTarget = GetNextZoneTarget();
	}
}

/// Set fighters to intercept
void CvTacticalAI::PlotAirInterceptMoves()
{
	list<int>::iterator it;
	m_CurrentMoveUnits.clear();
	CvTacticalUnit unit;
#if defined(MOD_AI_SMART_AIR_TACTICS)
	std::vector<CvPlot*> checkedPlotList;
#endif
	CvTacticalDominanceZone *pZone;

	// Loop through all recruited units
	for(it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if(pUnit)
		{
			// Am I eligible to intercept?
#if defined(MOD_AI_SMART_AIR_TACTICS)
			// we only commandeered units which won't be rebased
			if(pUnit->canAirPatrol(NULL))
			{
				CvPlot* pUnitPlot = pUnit->plot();
				if (MOD_AI_SMART_AIR_TACTICS)
				{
					int iNumNearbyBombers = m_pPlayer->GetMilitaryAI()->GetNumEnemyAirUnitsInRange(pUnitPlot, pUnit->GetRange(), false/*bCountFighters*/, true/*bCountBombers*/);
					int iNumNearbyFighters = m_pPlayer->GetMilitaryAI()->GetNumEnemyAirUnitsInRange(pUnitPlot, pUnit->GetRange(), true/*bCountFighters*/, false/*bCountBombers*/);
					int iNumPlotNumAlreadySet = SamePlotFound(checkedPlotList, pUnitPlot);

					if (iNumNearbyBombers == 1)
					{
						// To at least intercept once if only one bomber found.
						iNumNearbyBombers++;
					}
					int maxInterceptorsWanted = ((iNumNearbyBombers / 2) + (iNumNearbyFighters / 4));

					if (iNumPlotNumAlreadySet < maxInterceptorsWanted)
					{
						checkedPlotList.push_back(pUnitPlot);
						unit.SetID(pUnit->GetID());
						m_CurrentMoveUnits.push_back(unit);

						if(GC.getLogging() && GC.getAILogging())
						{
							CvString strLogString;
							strLogString.Format("Ready to intercept enemy air units at, X: %d, Y: %d", pUnit->getX(), pUnit->getY());
							LogTacticalMessage(strLogString);
						}
					}
				}
				else
				{
#else
			if(pUnit->canAirPatrol(NULL) && !m_pPlayer->GetMilitaryAI()->WillAirUnitRebase(pUnit))
			{
				CvPlot* pUnitPlot = pUnit->plot();

#endif
					CvCity* pCity = pUnitPlot->getPlotCity();
					pZone = NULL;

					if (pCity)
						pZone = GetTacticalAnalysisMap()->GetZoneByCity(pCity, false);

					int iNumNearbyBombers = m_pPlayer->GetMilitaryAI()->GetNumEnemyAirUnitsInRange(pUnitPlot, m_iRecruitRange, false/*bCountFighters*/, true/*bCountBombers*/);

					// On a carrier or in a city where we are not dominant and near some enemy bombers?
					if (!pCity || !pZone || pZone->GetDominanceFlag() != TACTICAL_DOMINANCE_FRIENDLY)
					{
						if (iNumNearbyBombers > 0)
						{
							unit.SetID(pUnit->GetID());
							m_CurrentMoveUnits.push_back(unit);

							if(GC.getLogging() && GC.getAILogging())
							{
								CvString strLogString;
								strLogString.Format("Ready to intercept enemy air units at, X: %d, Y: %d", pUnit->getX(), pUnit->getY());
								LogTacticalMessage(strLogString);
							}
						}
					}
#if defined(MOD_AI_SMART_AIR_TACTICS)
				}
#endif
			}
		}
	}

	if(m_CurrentMoveUnits.size() > 0)
	{
		ExecuteAirInterceptMoves();
	}
}

/// Set fighters to air sweep
void CvTacticalAI::PlotAirSweepMoves()
{
	list<int>::iterator it;
	m_CurrentMoveUnits.clear();
	CvTacticalUnit unit;
	CvTacticalDominanceZone *pZone;

	// Loop through all recruited units
	for(it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
#if defined(MOD_UNITS_MAX_HP)
		if(pUnit && (pUnit->getDamage() * 2) < pUnit->GetMaxHitPoints())
#else
		if(pUnit && (pUnit->getDamage() * 2) < GC.getMAX_HIT_POINTS())
#endif
		{
			// Am I eligible to air sweep and have a target?
#if defined(MOD_BALANCE_CORE)
			// we have only units here which won't be rebased
			if(pUnit->canAirSweep() && m_pPlayer->GetMilitaryAI()->GetBestAirSweepTarget(pUnit) != NULL)
#else
			if(pUnit->canAirSweep() && !m_pPlayer->GetMilitaryAI()->WillAirUnitRebase(pUnit) && m_pPlayer->GetMilitaryAI()->GetBestAirSweepTarget(pUnit) != NULL)
#endif
			{
				CvPlot* pUnitPlot = pUnit->plot();
				CvCity* pCity = pUnitPlot->getPlotCity();
				pZone = NULL;

				// On a carrier or in a city where we are dominant?
				if (pCity)
					pZone = GetTacticalAnalysisMap()->GetZoneByCity(pCity, false);

				if (!pCity || !pZone || pZone->GetDominanceFlag() == TACTICAL_DOMINANCE_FRIENDLY)
				{
					unit.SetID(pUnit->GetID());
					m_CurrentMoveUnits.push_back(unit);

					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("Ready to air sweep enemy air units at, X: %d, Y: %d", pUnit->getX(), pUnit->getY());
						LogTacticalMessage(strLogString);
					}
				}
			}
		}
	}

	if(m_CurrentMoveUnits.size() > 0)
	{
		ExecuteAirSweepMoves();
	}
}

/// Spend money to buy defenses
void CvTacticalAI::PlotEmergencyPurchases()
{
	CvCity* pCity;
	CvUnit* pCityDefender;
	CvUnit* pUnit;

	if(m_pPlayer->isMinorCiv())
	{
		return;
	}

	// Is this a dominance zone where we're defending a city?
	CvTacticalDominanceZone* pZone = GetTacticalAnalysisMap()->GetZoneByIndex(m_iCurrentZoneIndex);
	pCity = pZone->GetZoneCity();
	if(pCity && pCity->getOwner() == m_pPlayer->GetID() && pZone->GetTerritoryType() == TACTICAL_TERRITORY_FRIENDLY && pZone->GetEnemyUnitCount() > 0)
	{
		// Make sure the city isn't about to fall.  Test by seeing if there are high priority unit targets
		for(unsigned int iI = 0; iI < m_ZoneTargets.size() && !pCity->isCapital(); iI++)
		{
			if(m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT)
			{
				return;   // Abandon hope for this city; save our money to use elsewhere
			}
		}
#if defined(MOD_BALANCE_CORE)
		if(!MOD_BALANCE_CORE_BUILDING_INVESTMENTS)
		{
#endif
		m_pPlayer->GetMilitaryAI()->BuyEmergencyBuilding(pCity);
#if defined(MOD_BALANCE_CORE)
		}
#endif
#if defined(MOD_BALANCE_CORE)
		if(!MOD_BALANCE_CORE_UNIT_INVESTMENTS)
		{
#endif
		// If two defenders, assume already have land and sea and skip this city
		if (pCity->plot()->getNumDefenders(m_pPlayer->GetID()) < 2)
		{
			bool bBuyNavalUnit = false;
			bool bBuyLandUnit = false;

			pCityDefender = pCity->plot()->getBestDefender(m_pPlayer->GetID());
			if (!pCityDefender)
			{
				bBuyLandUnit = true;
				if (pCity->isCoastal())
				{
					bBuyNavalUnit = true;
				}
			}
			else
			{
				if (pCityDefender->getDomainType() == DOMAIN_LAND)
				{
					if (pCity->isCoastal())
					{
						bBuyNavalUnit = true;
					}
				}
				else
				{
					bBuyLandUnit = true;
				}
			}

			if (bBuyLandUnit)
			{
				pUnit = m_pPlayer->GetMilitaryAI()->BuyEmergencyUnit(UNITAI_CITY_BOMBARD, pCity);
				if(!pUnit)
				{
					pUnit = m_pPlayer->GetMilitaryAI()->BuyEmergencyUnit(UNITAI_RANGED, pCity);
				}
			}

			if (bBuyNavalUnit)
			{
				pUnit = m_pPlayer->GetMilitaryAI()->BuyEmergencyUnit(UNITAI_ASSAULT_SEA, pCity);
				if (pUnit)
				{
					// Bought one, don't need to buy melee naval later
					bBuyNavalUnit = false;
				}
			}

			// Always can try to buy air units
			pUnit = m_pPlayer->GetMilitaryAI()->BuyEmergencyUnit(UNITAI_ATTACK_AIR, pCity);
			if (!pUnit)
			{
				pUnit = m_pPlayer->GetMilitaryAI()->BuyEmergencyUnit(UNITAI_DEFENSE_AIR, pCity);
			}

			// Melee naval if didn't buy Ranged naval, (or not)
			//if (bBuyNavalUnit)
			//{
			//	pUnit = m_pPlayer->GetMilitaryAI()->BuyEmergencyUnit(UNITAI_ATTACK_SEA, pCity);
			//}
		}
#if defined(MOD_BALANCE_CORE)
		}
#endif
	}
}

/// Spend money to buy defenses
void CvTacticalAI::PlotDefensiveAirlifts()
{
	vector<int> aProcessedUnits;
	list<int>::iterator it;
	CvTacticalUnit unit;
	CvCity* pCity;

	if(m_pPlayer->isMinorCiv())
	{
		return;
	}

	// Is this a dominance zone where we're defending a city and that city has an airport?
	CvTacticalDominanceZone* pZone = GetTacticalAnalysisMap()->GetZoneByIndex(m_iCurrentZoneIndex);
	pCity = pZone->GetZoneCity();
	if(pCity && pCity->getOwner() == m_pPlayer->GetID() && pZone->GetTerritoryType() == TACTICAL_TERRITORY_FRIENDLY && pCity->CanAirlift()&& pZone->GetEnemyUnitCount() > 0)
	{
		// Loop through all recruited units
		for (it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
		{
			CvUnit* pUnit = m_pPlayer->getUnit(*it);
			if(pUnit)
			{
				// If there a hex adjacent to city they can airlift to?
				for (int iI = 0; iI < NUM_DIRECTION_TYPES; iI++)
				{
					CvPlot *pLoopPlot = plotDirection(pCity->getX(), pCity->getY(), ((DirectionTypes)iI));
					if (pLoopPlot != NULL)
					{
						if (pUnit->canAirliftAt(pUnit->plot(), pLoopPlot->getX(), pLoopPlot->getY()))
						{
							pUnit->airlift(pLoopPlot->getX(), pLoopPlot->getY());

							aProcessedUnits.push_back(pUnit->GetID());

							if(GC.getLogging() && GC.getAILogging())
							{
								CvString strLogString;
								strLogString.Format("Airlifting %s to city of %s, Current X: %d, Current Y: %d", pUnit->getName().GetCString(), pCity->getName().GetCString(), pUnit->getX(), pUnit->getY());
								LogTacticalMessage(strLogString, false);
							}

							break;
						}
					}
				}
			}
		}
	}

	// Mark units processed
	vector<int>::const_iterator unitIt;
	for (unitIt = aProcessedUnits.begin(); unitIt != aProcessedUnits.end(); unitIt++)
	{
		UnitProcessed(*unitIt);
	}
}

/// Move naval units over top of unprotected embarked units
void CvTacticalAI::PlotEscortEmbarkedMoves()
{
	list<int>::iterator it;
	m_CurrentMoveUnits.clear();
	CvTacticalUnit unit;

#if defined(MOD_BALANCE_CORE_MILITARY)
	std::vector<CvUnit*> vEmbarkedUnits;
	int iLoop = 0;
	for(CvUnit* pLoopUnit = m_pPlayer->firstUnit(&iLoop); pLoopUnit != NULL; pLoopUnit = m_pPlayer->nextUnit(&iLoop))
	{
		if (pLoopUnit->isEmbarked() && pLoopUnit->getDomainType() != DOMAIN_SEA)
			vEmbarkedUnits.push_back(pLoopUnit);
	}

	// Loop through all recruited units
	for(it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if(pUnit)
		{
			// Am I a naval combat unit?
			if(pUnit->getDomainType() == DOMAIN_SEA && pUnit->IsCombatUnit())
			{
				//any embarked unit close by?
				int iMaxDist = pUnit->baseMoves()*2;
				for (size_t i=0; i<vEmbarkedUnits.size(); i++)
				{
					if (plotDistance(*pUnit->plot(),*vEmbarkedUnits[i]->plot())<=iMaxDist)
					{
						unit.SetID(pUnit->GetID());
						m_CurrentMoveUnits.push_back(unit);
						break;
					}
				}
			}
		}
	}
#else
	// Loop through all recruited units
	for(it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if(pUnit)
		{
			// Am I a naval combat unit?
#if defined(MOD_BALANCE_CORE)
			if(pUnit->getDomainType() == DOMAIN_SEA && pUnit->IsCombatUnit() && pUnit->getArmyID() == -1)
#else
			if(pUnit->getDomainType() == DOMAIN_SEA && pUnit->IsCombatUnit())
#endif
			{
				unit.SetID(pUnit->GetID());
				m_CurrentMoveUnits.push_back(unit);
			}
		}
	}
#endif

	if(m_CurrentMoveUnits.size() > 0)
	{
		ExecuteEscortEmbarkedMoves();
	}
}

// PLOT MOVES FOR ZONE TACTICAL POSTURES

/// Win an attrition campaign with bombardments
void CvTacticalAI::PlotSitAndBombardMoves()
{
	// Find primary target for zone (i.e. enemy city)
	CvTacticalTarget target;
	CvPlot* pTarget;

	CvTacticalDominanceZone* pZone = GetTacticalAnalysisMap()->GetZoneByIndex(m_iCurrentZoneIndex);
	if(pZone->GetZoneCity() != NULL)
	{
		target.SetTargetType(AI_TACTICAL_TARGET_CITY);
		target.SetTargetPlayer(pZone->GetZoneCity()->getOwner());
		target.SetTargetX(pZone->GetZoneCity()->plot()->getX());
		target.SetTargetY(pZone->GetZoneCity()->plot()->getY());
		target.SetDominanceZone(pZone->GetDominanceZoneID());
		pTarget = GC.getMap().plot(target.GetTargetX(), target.GetTargetY());

		// Attack primary target (city)
		ExecuteSafeBombards(target);
		if(target.IsTargetStillAlive(m_pPlayer->GetID()))
		{
			PlotDamageCityMoves();
			PlotNavalDamageCityMoves();
		}

		// Attack ancillary target (nearby units)
		for(unsigned int iI = 0; iI < m_ZoneTargets.size(); iI++)
		{
			// Is the target of an appropriate type?
			if(m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT ||
			        m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT ||
			        m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT)
			{
				if(m_ZoneTargets[iI].IsTargetStillAlive(m_pPlayer->GetID()))
				{
					pTarget = GC.getMap().plot(m_ZoneTargets[iI].GetTargetX(), m_ZoneTargets[iI].GetTargetY());
					ExecuteSafeBombards(m_ZoneTargets[iI]);
				}
			}
		}

		PlotCloseOnTarget(true /*bCheckDominance*/);
	}
}

/// Win an attrition campaign with bombardments
void CvTacticalAI::PlotAttritFromRangeMoves()
{
	CvPlot* pTarget;

	// Find unit targets
	for(unsigned int iI = 0; iI < m_ZoneTargets.size(); iI++)
	{
		// Is the target of an appropriate type?
		if(m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT ||
		        m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT ||
		        m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT)
		{
			if(m_ZoneTargets[iI].IsTargetStillAlive(m_pPlayer->GetID()))
			{
				pTarget = GC.getMap().plot(m_ZoneTargets[iI].GetTargetX(), m_ZoneTargets[iI].GetTargetY());
				ExecuteSafeBombards(m_ZoneTargets[iI]);
			}
		}
	}
}

/// Defeat enemy units by using our advantage in numbers
void CvTacticalAI::PlotExploitFlanksMoves()
{
	CvPlot* pTarget;
	bool bAttackUnderway = false;
	bool bAttackMade;

	m_TempTargets.clear();

	// Loop through unit targets finding attack for this turn
	for(unsigned int iI = 0; iI < m_ZoneTargets.size(); iI++)
	{
		if(m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT ||
		        m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT ||
		        m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT)
		{
			GetTacticalAnalysisMap()->ClearDynamicFlags();
			pTarget = GC.getMap().plot(m_ZoneTargets[iI].GetTargetX(), m_ZoneTargets[iI].GetTargetY());
			GetTacticalAnalysisMap()->SetTargetFlankBonusCells(pTarget);

			bAttackMade = ExecuteFlankAttack(m_ZoneTargets[iI]);
			if(bAttackMade)
			{
				bAttackUnderway = true;
			}

			// No attack, see if we can set one up on this target later
			else
			{
				m_TempTargets.push_back(m_ZoneTargets[iI]);
			}
		}
	}
}

/// We have more overall strength than enemy, defeat his army first
void CvTacticalAI::PlotSteamrollMoves()
{
	m_TempTargets.clear();

#if defined(MOD_BALANCE_CORE_NEW_TACTICAL_AI)
	// See if there are any kill attacks we can make.
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT, true, true);
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT, true, true);
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT, true);

	for(unsigned int iI = 0; iI < m_ZoneTargets.size(); iI++)
	{
		CvTacticalTarget kTarget = m_ZoneTargets[iI];
		if(!kTarget.IsTargetStillAlive(m_pPlayer->GetID()))
			continue;

		if(kTarget.GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT || kTarget.GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT)
			ExecuteSafeBombards(kTarget);
	}

	// We have superiority, so a let's attack high prio targets even with bad odds
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT, false, true);
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT, false);
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT, false);

	// See if it is time to go after the city
	PlotDamageCityMoves();
	PlotNavalDamageCityMoves();

	PlotCloseOnTarget(false /*bCheckDominance*/);
#else
	// Loop through unit targets finding advantageous attacks for this turn
	for(unsigned int iI = 0; iI < m_ZoneTargets.size(); iI++)
	{
		if(m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT ||
		        m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT ||
		        m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT)
		{
			if(m_ZoneTargets[iI].IsTargetStillAlive(m_pPlayer->GetID()))
			{
				ExecutePriorityAttacksOnUnitTarget(m_ZoneTargets[iI]);
			}
		}
	}

	// See if there are any other anti-unit attacks we can make.
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT, false);
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT, false);
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT, true);

	// See if it is time to go after the city
	PlotDamageCityMoves();

	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT, false, true);
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT, false, true);
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT, false, true);

	PlotCloseOnTarget(false /*bCheckDominance*/);
#endif
}

/// We should be strong enough to take out the city before the enemy can whittle us down with ranged attacks
void CvTacticalAI::PlotSurgicalCityStrikeMoves()
{
	CvTacticalTarget target;
	CvTacticalDominanceZone* pZone = GetTacticalAnalysisMap()->GetZoneByIndex(m_iCurrentZoneIndex);

	if(pZone->GetZoneCity() != NULL)
	{
		target.SetTargetType(AI_TACTICAL_TARGET_CITY);
		target.SetTargetPlayer(pZone->GetZoneCity()->getOwner());
		target.SetTargetX(pZone->GetZoneCity()->plot()->getX());
		target.SetTargetY(pZone->GetZoneCity()->plot()->getY());
		target.SetDominanceZone(pZone->GetDominanceZoneID());

#if defined(MOD_BALANCE_CORE_NEW_TACTICAL_AI)
		//try capture first! will only do something if we have enough firepower.
		PlotCaptureCityMoves();

		if(target.IsTargetStillAlive(m_pPlayer->GetID()))
			PlotDamageCityMoves();
			PlotNavalDamageCityMoves();

		// Take any other really good attacks we've set up
		PlotDestroyUnitMoves(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT, true);
		PlotDestroyUnitMoves(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT, true);
		PlotDestroyUnitMoves(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT, true);

		PlotCloseOnTarget(false /*bCheckDominance*/);
#else
		//try capture first! will only do something if we have enough firepower.
		PlotCaptureCityMoves();

		for(unsigned int iI = 0; iI < m_ZoneTargets.size(); iI++)
		{
			if(m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT ||
			        m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT ||
			        m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT)
			{
				if(plotDistance(m_ZoneTargets[iI].GetTargetX(), m_ZoneTargets[iI].GetTargetY(), target.GetTargetX(), target.GetTargetY()) == 1)
				{
					if(m_ZoneTargets[iI].IsTargetStillAlive(m_pPlayer->GetID()))
					{
						ExecutePriorityAttacksOnUnitTarget(m_ZoneTargets[iI]);
					}
				}
			}
		}

		if(target.IsTargetStillAlive(m_pPlayer->GetID()))
			PlotDamageCityMoves();

		// Take any other really good attacks we've set up
		PlotDestroyUnitMoves(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT, true);
		PlotDestroyUnitMoves(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT, true);
		PlotDestroyUnitMoves(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT, true);

		PlotCloseOnTarget(false /*bCheckDominance*/);
#endif
	}
}

/// Build a defensive shell around this city
void CvTacticalAI::PlotHedgehogMoves()
{
	// Attack priority unit targets
#if defined(MOD_BALANCE_CORE_NEW_TACTICAL_AI)
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT, true);
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT, true);
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT, true);
	for(unsigned int iI = 0; iI < m_ZoneTargets.size(); iI++)
	{
		CvTacticalTarget kTarget = m_ZoneTargets[iI];
		if(!kTarget.IsTargetStillAlive(m_pPlayer->GetID()))
			continue;

		if(kTarget.GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT || kTarget.GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT)
			ExecuteSafeBombards(kTarget);
	}
#else
	for(unsigned int iI = 0; iI < m_ZoneTargets.size(); iI++)
	{
		if(m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT ||
		        m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT)
		{
			if(m_ZoneTargets[iI].IsTargetStillAlive(m_pPlayer->GetID()))
			{
				ExecutePriorityAttacksOnUnitTarget(m_ZoneTargets[iI]);
			}
		}
	}
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT, true);
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT, true);
#endif

	// But after best attacks are exhausted, go right to playing defense
	CvTacticalDominanceZone* pZone = GetTacticalAnalysisMap()->GetZoneByIndex(m_iCurrentZoneIndex);
	if(pZone->GetZoneCity() != NULL)
	{
		CvTacticalTarget target;
		target.SetTargetType(AI_TACTICAL_TARGET_CITY);
		target.SetTargetPlayer(pZone->GetZoneCity()->getOwner());
		target.SetTargetX(pZone->GetZoneCity()->plot()->getX());
		target.SetTargetY(pZone->GetZoneCity()->plot()->getY());
		target.SetDominanceZone(pZone->GetDominanceZoneID());
		ExecuteCloseOnTarget(target, pZone, false);
	}
}

/// Try to push back the invader
void CvTacticalAI::PlotCounterattackMoves()
{
	// Attack priority unit targets
#if defined(MOD_BALANCE_CORE_NEW_TACTICAL_AI)
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT, true);
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT, true);
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT, true);

	for(unsigned int iI = 0; iI < m_ZoneTargets.size(); iI++)
	{
		CvTacticalTarget kTarget = m_ZoneTargets[iI];
		if(!kTarget.IsTargetStillAlive(m_pPlayer->GetID()))
			continue;

		if(kTarget.GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT || kTarget.GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT)
			ExecuteSafeBombards(kTarget);
	}

	// Now targets we can't destroy
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT, false);
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT, false);
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT, false);
#else
	for(unsigned int iI = 0; iI < m_ZoneTargets.size(); iI++)
	{
		if(m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT ||
		        m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT)
		{
			if(m_ZoneTargets[iI].IsTargetStillAlive(m_pPlayer->GetID()))
			{
				ExecutePriorityAttacksOnUnitTarget(m_ZoneTargets[iI]);
			}
		}
	}

	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT, true);
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT, true);
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT, true);

	// Now targets we can't destroy
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT, false);
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT, false);
	PlotDestroyUnitMoves(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT, false);
#endif
}

/// Withdraw out of current dominance zone
void CvTacticalAI::PlotWithdrawMoves()
{
	list<int>::iterator it;
	m_CurrentMoveUnits.clear();
	CvTacticalUnit unit;

	CvTacticalDominanceZone* pZone = GetTacticalAnalysisMap()->GetZoneByIndex(m_iCurrentZoneIndex);

	// Loop through all recruited units
	for(it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if(pUnit)
		{
			// Am I in the current dominance zone?
			int iPlotIndex = GC.getMap().plotNum(pUnit->getX(), pUnit->getY());
			if(GetTacticalAnalysisMap()->GetCell(iPlotIndex)->GetDominanceZone() == GetTacticalAnalysisMap()->GetZoneByIndex(m_iCurrentZoneIndex)->GetDominanceZoneID())
			{
				unit.SetID(pUnit->GetID());
				m_CurrentMoveUnits.push_back(unit);
			}

			// Or am I within tactical recruit range of center of zone and in danger and not in friendly territory?
			else if(pZone->GetZoneCity() != NULL)
			{
				if(plotDistance(pZone->GetZoneCity()->getX(), pZone->GetZoneCity()->getY(), pUnit->getX(), pUnit->getY()) <= m_iRecruitRange)
				{
					CvPlot* pPlot = pUnit->plot();
					if(m_pPlayer->GetPlotDanger(*pPlot) > pUnit->GetCurrHitPoints()/2)
					{
						if(pUnit->plot()->getOwner() != pUnit->getOwner())
						{
							unit.SetID(pUnit->GetID());
							m_CurrentMoveUnits.push_back(unit);
						}
					}
				}
			}

			// Or we are withdrawing from unowned lands zone and this is a plot in danger
			else if(pZone->GetTerritoryType() == TACTICAL_TERRITORY_NO_OWNER)
			{
				CvPlot* pPlot = pUnit->plot();
				if(m_pPlayer->GetPlotDanger(*pPlot) > pUnit->GetCurrHitPoints()/2)
				{
					if(pUnit->plot()->getOwner() != pUnit->getOwner())
					{
						unit.SetID(pUnit->GetID());
						m_CurrentMoveUnits.push_back(unit);
					}
				}
			}
		}
	}

	if(m_CurrentMoveUnits.size() > 0)
	{
		ExecuteWithdrawMoves();
	}
}

/// Bombard any enemy targets we can get to on shore
void CvTacticalAI::PlotShoreBombardmentMoves()
{
	for(unsigned int iI = 0; iI < m_ZoneTargets.size(); iI++)
	{
		// Is the target of an appropriate type?
		if(m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT ||
		        m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT ||
		        m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT)
		{
			if(m_ZoneTargets[iI].IsTargetStillAlive(m_pPlayer->GetID()))
			{
				ExecuteSafeBombards(m_ZoneTargets[iI]);
			}
		}
	}
	for(unsigned int iI = 0; iI < m_ZoneTargets.size(); iI++)
	{
		if(m_ZoneTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_CITY)
		{
			if(m_ZoneTargets[iI].IsTargetStillAlive(m_pPlayer->GetID()))
			{
				ExecuteSafeBombards(m_ZoneTargets[iI]);
			}
		}
	}
}

/// Close units in on primary target of this dominance zone
void CvTacticalAI::PlotCloseOnTarget(bool bCheckDominance)
{
	CvTacticalDominanceZone* pZone = GetTacticalAnalysisMap()->GetZoneByIndex(m_iCurrentZoneIndex);
	if (bCheckDominance && pZone->GetDominanceFlag() == TACTICAL_DOMINANCE_ENEMY)
	{
		return;
	}

	// Flank attacks done; if in an enemy zone, close in on target
	CvTacticalTarget target;
	if(pZone->GetTerritoryType() == TACTICAL_TERRITORY_TEMP_ZONE)
	{
		target.SetTargetType(AI_TACTICAL_TARGET_BARBARIAN_CAMP);
		target.SetTargetPlayer(BARBARIAN_PLAYER);
		target.SetTargetX(pZone->GetCenterX());
		target.SetTargetY(pZone->GetCenterY());
		target.SetDominanceZone(pZone->GetDominanceZoneID());

		ExecuteCloseOnTarget(target, pZone, true);
	}
	else if (!pZone->IsWater() && pZone->GetTerritoryType() == TACTICAL_TERRITORY_ENEMY && pZone->GetZoneCity() != NULL)
	{
		bool bCanSeeCity = pZone->GetZoneCity()->plot()->isVisible(m_pPlayer->getTeam());

		// If we can't see the city, be careful advancing on it.  We want to be sure we're not heavily outnumbered
		//Exception for temporary targets - we need to press offensiv here.
		if(IsTemporaryZoneCity(pZone->GetZoneCity()) || bCanSeeCity || pZone->GetFriendlyStrength()*3 > pZone->GetEnemyStrength()*2)
		{
			target.SetTargetType(AI_TACTICAL_TARGET_CITY);
			target.SetTargetPlayer(pZone->GetZoneCity()->getOwner());
			target.SetTargetX(pZone->GetZoneCity()->plot()->getX());
			target.SetTargetY(pZone->GetZoneCity()->plot()->getY());
			target.SetDominanceZone(pZone->GetDominanceZoneID());

			ExecuteCloseOnTarget(target, pZone, true);
		}
	}
#if defined(MOD_BALANCE_CORE)
	else if (pZone->IsWater() && pZone->GetTerritoryType() == TACTICAL_TERRITORY_ENEMY && pZone->GetZoneCity() != NULL)
	{
		bool bCanSeeCity = pZone->GetZoneCity()->plot()->isVisible(m_pPlayer->getTeam());

		// If we can't see the city, be careful advancing on it.  We want to be sure we're not heavily outnumbered
		//Exception for temporary targets - we need to press offensiv here.
		if (IsTemporaryZoneCity(pZone->GetZoneCity()) || bCanSeeCity || pZone->GetEnemyNavalUnitCount() <= pZone->GetFriendlyNavalUnitCount())
		{
			target.SetTargetType(AI_TACTICAL_TARGET_CITY);
			target.SetTargetPlayer(pZone->GetZoneCity()->getOwner());
			target.SetTargetX(pZone->GetZoneCity()->plot()->getX());
			target.SetTargetY(pZone->GetZoneCity()->plot()->getY());
			target.SetDominanceZone(pZone->GetDominanceZoneID());

			ExecuteCloseOnTarget(target, pZone, true);
		}
	}
#endif
}

/// Log that we couldn't find assignments for some units
void CvTacticalAI::ReviewUnassignedUnits()
{
	list<int>::iterator it;

	// Loop through all remaining units
	for(it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if(pUnit)
		{
#if defined(MOD_BALANCE_CORE)
			//in any case: homeland AI may use recruit the units if it wants to
			pUnit->setTacticalMove((TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_UNASSIGNED]);

			//If didn't JUST deploy, reset that so that the AI can grab it.
			if(pUnit->GetDeployFromOperationTurn() > (GC.getGame().getGameTurn() + 1))
			{
				pUnit->SetDeployFromOperationTurn(-1);
			}

			//barbarians have no homeland AI, so they end their turn here
			if (pUnit->isBarbarian())
			{
				MissionTypes eMission = pUnit->canFortify(pUnit->plot()) ? CvTypes::getMISSION_FORTIFY() : CvTypes::getMISSION_SKIP();
				pUnit->PushMission(eMission);
				pUnit->SetTurnProcessed(true);
			}
			else if ( pUnit->getDomainType()==DOMAIN_LAND )
			{
				//tactical reposition failed ... but well, maybe homeland AI can use the unit
				/*
				CvString missionInfo = (pUnit->getTacticalMove()==NO_TACTICAL_MOVE) ? "no tactical move" : GC.getTacticalMoveInfo(pUnit->getTacticalMove())->GetType();
			
				OutputDebugString( CvString::format( "unassigned %s tactical unit %s %d at %d,%d (last move: %s)\n", 
					m_pPlayer->getCivilizationAdjective(), pUnit->getName().c_str(), pUnit->GetID(), pUnit->getX(), pUnit->getY(), missionInfo.c_str() ).c_str() ); 
				*/
			}
			else if ( pUnit->getDomainType()==DOMAIN_AIR )
			{
				//homeland doesn't handle air units except for rebasing
				//units to be rebased are passed over for tactical AI, the rest simply does nothing
				pUnit->PushMission(CvTypes::getMISSION_SKIP());
				pUnit->SetTurnProcessed(true);
			}
			else if ( pUnit->getDomainType()==DOMAIN_SEA )
			{
				//don't know if homeland AI does anything with the navy ...
				pUnit->PushMission(CvTypes::getMISSION_SKIP());
				pUnit->SetTurnProcessed(true);
			}
#else
			// Barbarians and air units aren't handled by the operational or homeland AIs
			if(pUnit->isBarbarian() || pUnit->getDomainType() == DOMAIN_AIR)
			{
				pUnit->PushMission(CvTypes::getMISSION_SKIP());
				pUnit->SetTurnProcessed(true);

				if(GC.getLogging() && GC.getAILogging())
				{
					CvString strLogString;
					CvString strTemp;
					strTemp = pUnit->getUnitInfo().GetDescription();
					strLogString.Format("Unassigned %s at, X: %d, Y: %d", strTemp.GetCString(), pUnit->getX(), pUnit->getY());
					LogTacticalMessage(strLogString);
				}
			}
#endif
		}
	}
}

// OPERATIONAL AI SUPPORT FUNCTIONS

CvUnit* SwitchEscort(CvUnit* pCivilian, CvUnit* pEscort, CvArmyAI* pThisArmy)
{
	CvUnit* pPlotDefender = pCivilian->plot()->getBestDefender(pCivilian->getOwner());

	//Maybe we just make this guy our new escort, eh?
	if(pPlotDefender && pPlotDefender->getArmyID() == -1 && pPlotDefender->getDomainType() == pCivilian->getDomainType())
	{
		pThisArmy->RemoveUnit(pEscort->GetID());
		pThisArmy->AddUnit(pPlotDefender->GetID(), 1);
		if(GC.getLogging() && GC.getAILogging())
		{
			CvString strLogString;
			strLogString.Format("SingleHexOperationMoves: Switched escort to get things going.");
			GET_PLAYER(pCivilian->getOwner()).GetTacticalAI()->LogTacticalMessage(strLogString);
		}

		return pPlotDefender;
	}

	return NULL;
}

/// Move a single stack (civilian plus escort) to its destination
void CvTacticalAI::PlotArmyMovesEscort(CvArmyAI* pThisArmy)
{
	if (!pThisArmy)
		return;

	CvAIOperation* pOperation = GET_PLAYER(pThisArmy->GetOwner()).getAIOperation(pThisArmy->GetOperationID());
	if (!pOperation)
		return;

	//the unit to be escorted is always the first one
	CvUnit* pCivilian = pThisArmy->GetFirstUnit();
	//the second unit would be the first escort
	CvUnit* pEscort = pThisArmy->GetNextUnit();
	//additional escorts
	std::vector<CvUnit*> vExtraEscorts;
	CvUnit* pExtraEscort = pThisArmy->GetNextUnit();
	while (pExtraEscort)
	{
		vExtraEscorts.push_back(pExtraEscort);
		pExtraEscort = pThisArmy->GetNextUnit(); 
	}

	// No civilian? that's a problem
	if(!pCivilian || !pCivilian->IsCivilianUnit())
	{
		return;
	}

	// ESCORT AND CIVILIAN MEETING UP
	if(pThisArmy->GetArmyAIState() == ARMYAISTATE_WAITING_FOR_UNITS_TO_REINFORCE || 
		pThisArmy->GetArmyAIState() == ARMYAISTATE_WAITING_FOR_UNITS_TO_CATCH_UP)
	{
		pThisArmy->SetXY(pCivilian->getX(), pCivilian->getY());

		// Check to make sure escort can get to civilian
		if(pOperation->GetMusterPlot() != NULL)
		{
			//nothing to do?
			if(pEscort && pCivilian->plot() == pEscort->plot())
			{
				return;
			}
			//civilian and escort have not yet met up
			else if(pEscort && pEscort->plot() != pCivilian->plot())
			{
				//civilian is already there
				if(pCivilian->plot() == pOperation->GetMusterPlot())
				{
					//another military unit is blocking our escort ... find another muster plot
					if(pCivilian->plot()->GetNumCombatUnits() > 0)
					{
						CvUnit* pNewEscort = SwitchEscort(pCivilian,pEscort,pThisArmy);
						if (!pNewEscort)
						{
							//Let's have them move forward, see if that clears things up.
							MoveToUsingSafeEmbark(pCivilian, pOperation->GetTargetPlot(),true,0);
							pCivilian->finishMoves();
							UnitProcessed(pCivilian->GetID());
							MoveToUsingSafeEmbark(pEscort, pOperation->GetTargetPlot(),false,0);
							pEscort->finishMoves();
							UnitProcessed(pEscort->GetID());

							//try again next turn
							pOperation->SetMusterPlot(pCivilian->plot());

							if(GC.getLogging() && GC.getAILogging())
							{
								CvString strLogString;
								strLogString.Format("SingleHexOperationMoves: Forced movement to get things going.");
								LogTacticalMessage(strLogString);
							}
						}
					}
					else
					{
						//check if the civilian is in danger
						if ( m_pPlayer->GetPlotDanger(*(pCivilian->plot()),pCivilian) > 0 )
						{
							//try to move to safety
							CvPlot* pBetterPlot = TacticalAIHelpers::FindSafestPlotInReach(pCivilian,true);
							if (pBetterPlot)
							{
								ExecuteMoveToPlotIgnoreDanger(pCivilian,pBetterPlot);
								pOperation->SetMusterPlot(pBetterPlot);
							}
						}

						//move escort towards civilian
						if (MoveToUsingSafeEmbark(pEscort, pCivilian->plot(), false, 0))
						{
							pEscort->finishMoves();
							UnitProcessed(pEscort->GetID());
						}
						else
						{
							//d'oh. escort cannot reach us.
							CvUnit* pNewEscort = SwitchEscort(pCivilian,pEscort,pThisArmy);

							if (pEscort==pNewEscort)
								pOperation->SetToAbort(AI_ABORT_LOST_PATH);

							return;	
						}

						pCivilian->finishMoves();
						UnitProcessed(pCivilian->GetID());
					}
				}
				else
				{
					// Civilian is not yet there - both must move
					MoveToUsingSafeEmbark(pCivilian, pOperation->GetMusterPlot(),true,0);
					pCivilian->finishMoves();
					UnitProcessed(pCivilian->GetID());
					MoveToUsingSafeEmbark(pEscort, pOperation->GetMusterPlot(),false,0);
					pEscort->finishMoves();
					UnitProcessed(pEscort->GetID());
				}

				if(GC.getLogging() && GC.getAILogging())
				{
					CvString strTemp;
					CvString strLogString;
					strTemp = GC.getUnitInfo(pEscort->getUnitType())->GetDescription();
					strLogString.Format("Moving escorting %s to civilian for operation, Civilian X: %d, Civilian Y: %d, X: %d, Y: %d", strTemp.GetCString(), pCivilian->plot()->getX(), pCivilian->plot()->getY(), pEscort->getX(), pEscort->getY());
					LogTacticalMessage(strLogString);
				}
			}
			else
			{
				//no escort
				if(pCivilian->plot() == pOperation->GetMusterPlot())
				{
					//check if the civilian is in danger
					if ( pCivilian->GetDanger()>0 )
					{
						//try to move to safety
						CvPlot* pBetterPlot = TacticalAIHelpers::FindSafestPlotInReach(pCivilian,true);
						if (pBetterPlot)
						{
							ExecuteMoveToPlotIgnoreDanger(pCivilian,pBetterPlot);

							//update this so we can advance to next stage
							pOperation->SetMusterPlot(pBetterPlot);
						}
					}
					pCivilian->finishMoves();
					UnitProcessed(pCivilian->GetID());
				}
				else
					//continue moving. if this should fail, we just freeze and wait for better times
					MoveToUsingSafeEmbark(pCivilian,pOperation->GetMusterPlot(),true,0);
			}
		}
	}

	// MOVING TO TARGET ... or really close
	if(pThisArmy->GetArmyAIState() == ARMYAISTATE_MOVING_TO_DESTINATION ||
		pThisArmy->GetArmyAIState() == ARMYAISTATE_AT_DESTINATION)
	{
		// are we there yet?
		if(pCivilian->plot() == pOperation->GetTargetPlot())
			return;

		int iMoveFlags = CvUnit::MOVEFLAG_TERRITORY_NO_ENEMY;
		pThisArmy->SetXY(pCivilian->getX(), pCivilian->getY());
	
		// the escort leads the way
		bool bHavePathEscort = false;
		bool bPathFound = false;
		bool bSaveMoves = true;
		CvString strLogString;
		if(pEscort)
		{
			iMoveFlags |= CvUnit::MOVEFLAG_IGNORE_DANGER;

			bHavePathEscort = pEscort->GeneratePath(pOperation->GetTargetPlot(), iMoveFlags);
			if(bHavePathEscort)
			{
				CvPlot* pCommonPlot = pEscort->GetPathEndFirstTurnPlot();
				if(pCommonPlot != NULL)
				{
					int iTurns = INT_MAX;
					bool bHavePathCivilian = pCivilian->GeneratePath(pCommonPlot, iMoveFlags, 5, &iTurns);
					bSaveMoves = (pCommonPlot == pOperation->GetTargetPlot());
					if (bHavePathCivilian)
					{
						bPathFound = true;

						if (iTurns > 1)
						{
							//strange, escort seems to be faster than the civilian, let's hope it's better the other way around
							CvPlot* pAltPlot = pCivilian->GetPathEndFirstTurnPlot();
							if (pEscort->canMoveInto(*pAltPlot,CvUnit::MOVEFLAG_DESTINATION))
								pCommonPlot = pAltPlot;
							else
							{
								pAltPlot = pCivilian->GetPathNodeArray().GetFirstPlot();
								if (pEscort->canMoveInto(*pAltPlot, CvUnit::MOVEFLAG_DESTINATION))
									pCommonPlot = pAltPlot;
							}
						}

						ExecuteMoveToPlotIgnoreDanger(pEscort, pCommonPlot, bSaveMoves);
						ExecuteMoveToPlotIgnoreDanger(pCivilian, pEscort->plot(), bSaveMoves);
						if(GC.getLogging() && GC.getAILogging())
						{
							strLogString.Format("%s at (%d,%d). Moving towards (%d,%d) with escort %s. escort leading.", 
							pCivilian->getName().c_str(), pCivilian->getX(), pCivilian->getY(), 
							pOperation->GetTargetPlot()->getX(), pOperation->GetTargetPlot()->getY(), pEscort->getName().c_str() );
						}
					}
				}
			}
			else
			{
				bool bHavePathCivilian = pCivilian->GeneratePath(pOperation->GetTargetPlot(), iMoveFlags);
				if(bHavePathCivilian)
				{
					CvPlot* pCommonPlot = pCivilian->GetPathEndFirstTurnPlot();
					if(pCommonPlot != NULL)
					{
						int iTurns = INT_MAX;
						bool bHavePathEscort = pEscort->GeneratePath(pCommonPlot, iMoveFlags, 5, &iTurns);
						bSaveMoves = (pCommonPlot == pOperation->GetTargetPlot());
						if (bHavePathEscort)
						{
							bPathFound = true;

							if (iTurns>1)
								//strange, civilian seems to be faster than the civilian, let's hope it's better the other way around
								pCommonPlot = pEscort->GetPathEndFirstTurnPlot();

							ExecuteMoveToPlotIgnoreDanger(pEscort, pCommonPlot, bSaveMoves);
							//Did he actually move?
							if (pEscort->plot() != pCivilian->plot())
							{
								ExecuteMoveToPlotIgnoreDanger(pCivilian, pEscort->plot(), bSaveMoves);

								if (GC.getLogging() && GC.getAILogging())
								{
									strLogString.Format("%s at (%d,%d). Moving towards (%d,%d) with escort %s. Civilian leading.",
										pCivilian->getName().c_str(), pCivilian->getX(), pCivilian->getY(),
										pOperation->GetTargetPlot()->getX(), pOperation->GetTargetPlot()->getY(), pEscort->getName().c_str());
								}
							}
							else
							{
								bPathFound = false;
							}
						}
					}
				}
			}
			
			if(!bPathFound)
			{
				//we have a problem, apparently civilian and escort must split up
				if (!MoveToUsingSafeEmbark(pCivilian, pOperation->GetTargetPlot(), true, 0))
				{
					pOperation->SetToAbort(AI_ABORT_LOST_PATH);
					strLogString.Format("%s stuck at (%d,%d), cannot find safe path to target. aborting.", 
						pCivilian->getName().c_str(), pCivilian->getX(), pCivilian->getY() );
				}

				//try to stay close
				MoveToEmptySpaceNearTarget(pEscort, pCivilian->plot(), pCivilian->plot()->getDomain(), 12);
				strLogString.Format("%s at (%d,%d) separated from escort %s at (%d,%d)", 
					pCivilian->getName().c_str(), pCivilian->getX(), pCivilian->getY(), 
					pEscort->getName().c_str(), pEscort->getX(), pEscort->getY() );
			}
		}
		else //no escort
		{
			bool bHavePathCivilian = pCivilian->GeneratePath(pOperation->GetTargetPlot(), iMoveFlags);
			if(bHavePathCivilian)
			{
				CvPlot* pCommonPlot = pCivilian->GetPathEndFirstTurnPlot();
				if(pCommonPlot != NULL)
				{
					bSaveMoves = (pCommonPlot == pOperation->GetTargetPlot());
					ExecuteMoveToPlotIgnoreDanger(pCivilian, pCommonPlot, bSaveMoves); 
					if(GC.getLogging() && GC.getAILogging())
					{
						strLogString.Format("%s at (%d,%d). Moving normally towards (%d,%d) without escort.",  pCivilian->getName().c_str(), pCivilian->getX(), pCivilian->getY(), pOperation->GetTargetPlot()->getX(), pOperation->GetTargetPlot()->getY() );
					}
				}
			}
			else
			{
				if (MoveToEmptySpaceNearTarget(pCivilian, pOperation->GetTargetPlot(), DOMAIN_LAND, INT_MAX))
				{
					if(GC.getLogging() && GC.getAILogging())
						strLogString.Format("%s at (%d,%d). Moving to empty space near target (%d,%d) without escort.",  pCivilian->getName().c_str(), pCivilian->getX(), pCivilian->getY(), pOperation->GetTargetPlot()->getX(), pOperation->GetTargetPlot()->getY() );
				}
				else
				{
					pOperation->SetToAbort(AI_ABORT_LOST_PATH);
					if(GC.getLogging() && GC.getAILogging())
						strLogString.Format("%s at (%d,%d). Aborted operation. No path to target for civilian.",  pCivilian->getName().c_str(), pCivilian->getX(), pCivilian->getY() );
				}
			}
		}

		// now we're done
		UnitProcessed(pCivilian->GetID());
		if (!bSaveMoves)
			pCivilian->finishMoves();

		if (pEscort)
		{
			pEscort->finishMoves();
			UnitProcessed(pEscort->GetID());
		}

		// logging
		if(GC.getLogging() && GC.getAILogging())
		{
			LogTacticalMessage(strLogString);
		}
	}

	//move any additional escorts near the civilian
	for (size_t i=0; i<vExtraEscorts.size(); i++)
	{
		CvUnit* pUnit = vExtraEscorts[i];
		MoveToEmptySpaceNearTarget( pUnit, pCivilian->plot(), NO_DOMAIN, 23 );
		if(GC.getLogging() && GC.getAILogging())
		{
			CvString strTemp;
			CvString strLogString;
			strTemp = GC.getUnitInfo(pUnit->getUnitType())->GetDescription();
			strLogString.Format("Moving additional escorting %s to civilian for operation, Civilian X: %d, Civilian Y: %d, X: %d, Y: %d", strTemp.GetCString(), pCivilian->plot()->getX(), pCivilian->plot()->getY(), pUnit->getX(), pUnit->getY());
			LogTacticalMessage(strLogString);
		}
		pUnit->finishMoves();
		UnitProcessed(pUnit->GetID());
	}
}

/// Move a large army to its destination against an enemy target
void CvTacticalAI::PlotArmyMovesCombat(CvArmyAI* pThisArmy)
{
	if (!pThisArmy)
		return;

	CvAIOperation* pOperation = GET_PLAYER(pThisArmy->GetOwner()).getAIOperation(pThisArmy->GetOperationID());
	if (!pOperation)
		return;

	m_OperationUnits.clear();
	m_GeneralsToMove.clear();

	ClearEnemiesNearArmy(pThisArmy);

	// RECRUITING
	if(pThisArmy->GetArmyAIState() == ARMYAISTATE_WAITING_FOR_UNITS_TO_REINFORCE || 
		pThisArmy->GetArmyAIState() == ARMYAISTATE_WAITING_FOR_UNITS_TO_CATCH_UP)
	{
		// Request moves for all units
		for(int iI = 0; iI < pThisArmy->GetNumFormationEntries(); iI++)
		{
			CvArmyFormationSlot* pSlot = pThisArmy->GetFormationSlot(iI);
			if(pSlot->GetUnitID() != NO_UNIT)
			{
				CvUnit* pUnit = m_pPlayer->getUnit(pSlot->GetUnitID());
				if(pUnit && pUnit->canMove())
				{
					// Great general or admiral?
					if(pUnit->IsGreatGeneral() || pUnit->IsGreatAdmiral() || pUnit->IsCityAttackSupport())
					{
						if(pUnit->getMoves() > 0)
						{
							CvOperationUnit operationUnit;
							operationUnit.SetUnitID(pUnit->GetID());
							operationUnit.SetPosition((MultiunitPositionTypes)m_CachedInfoTypes[eMUPOSITION_CIVILIAN_SUPPORT]);
							m_GeneralsToMove.push_back(operationUnit);
						}
					}
					else
					{
						CvMultiUnitFormationInfo* pkMultiUnitFormationInfo = GC.getMultiUnitFormationInfo(pThisArmy->GetFormationIndex());
						if(pkMultiUnitFormationInfo)
						{
							const CvFormationSlotEntry& thisSlotEntry = pkMultiUnitFormationInfo->getFormationSlotEntry(iI);
							MoveWithFormation(pUnit, thisSlotEntry.m_ePositionType);
						}
					}
				}
			}
		}

		ExecuteGatherMoves(pThisArmy);
	}

	// MOVING TO TARGET
	else if(pThisArmy->GetArmyAIState() == ARMYAISTATE_MOVING_TO_DESTINATION)
	{
		// Update army's current location
		CvPlot* pThisTurnTarget = pOperation->ComputeTargetPlotForThisTurn(pThisArmy);
		if(pThisTurnTarget == NULL)
		{
			pOperation->SetToAbort(AI_ABORT_LOST_PATH);
			return;
		}

		// Request moves for all units
		for(int iI = 0; iI < pThisArmy->GetNumFormationEntries(); iI++)
		{
			CvArmyFormationSlot* pSlot = pThisArmy->GetFormationSlot(iI);
			if(pSlot->GetUnitID() != NO_UNIT)
			{
				CvUnit* pUnit = m_pPlayer->getUnit(pSlot->GetUnitID());
				if(pUnit && pUnit->canMove())
				{
					// Great general?
					if(pUnit->IsGreatGeneral() || pUnit->IsGreatAdmiral() || pUnit->IsCityAttackSupport())
					{
						if(pUnit->getMoves() > 0)
						{
							CvOperationUnit operationUnit;
							operationUnit.SetUnitID(pUnit->GetID());
							operationUnit.SetPosition((MultiunitPositionTypes)m_CachedInfoTypes[eMUPOSITION_CIVILIAN_SUPPORT]);
							m_GeneralsToMove.push_back(operationUnit);
						}
					}
					else
					{
						CvMultiUnitFormationInfo* pkMultiUnitFormationInfo = GC.getMultiUnitFormationInfo(pThisArmy->GetFormationIndex());
						if(pkMultiUnitFormationInfo)
						{
							const CvFormationSlotEntry& thisSlotEntry = pkMultiUnitFormationInfo->getFormationSlotEntry(iI);
							MoveWithFormation(pUnit, thisSlotEntry.m_ePositionType);
						}
					}
				}
			}
		}

		if (pThisArmy->GetDomainType() == DOMAIN_SEA)
		{
			if (pOperation->GetMoveType() == AI_OPERATION_MOVETYPE_ESCORT)
			{
				ExecuteNavalFormationEscortMoves(pThisArmy, pThisTurnTarget);
			}
			else
			{
				ExecuteNavalFormationMoves(pThisArmy, pThisTurnTarget);
			}
		}
		else
		{
			ExecuteFormationMoves(pThisArmy, pThisTurnTarget);
		}
	}

	if(m_GeneralsToMove.size() > 0)
	{
		MoveGreatGeneral(pThisArmy);
	}
}

/// Queues up attacks on enemy units on or adjacent to army's desired center
void CvTacticalAI::ClearEnemiesNearArmy(CvArmyAI* pArmy)
{
	int iRange = 3;
	std::vector<CvPlot*> vInterestingTargets;

	// Loop through all appropriate targets to see if any is of concern
	for(unsigned int iI = 0; iI < m_AllTargets.size(); iI++)
	{
		// Is the target of an appropriate type?
		if(m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT ||
		        m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT ||
		        m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT ||
				m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_CITY
				)
		{
			vInterestingTargets.push_back( GC.getMap().plot(m_AllTargets[iI].GetTargetX(), m_AllTargets[iI].GetTargetY()) );
		}
	}

	// perform attacks. todo: sort targets by priority?
	CvUnit* pUnit = pArmy->GetFirstUnit();
	while(pUnit)
	{
		if(!pUnit->isDelayedDeath() && pUnit->canMove())
		{
			if(std::find(m_CurrentTurnUnits.begin(), m_CurrentTurnUnits.end(), pUnit->GetID()) == m_CurrentTurnUnits.end())
			{
				for (std::vector<CvPlot*>::iterator it=vInterestingTargets.begin(); it!=vInterestingTargets.end(); ++it)
				{
					if (plotDistance((*it)->getX(),(*it)->getY(),pUnit->getX(),pUnit->getY())<=iRange)
					{
						if (TacticalAIHelpers::IsAttackNetPositive(pUnit,*it) && TacticalAIHelpers::PerformOpportunityAttack(pUnit,*it))
						{
							if (!pUnit->canMove())
								UnitProcessed(pUnit->GetID());
							break;
						}
						else if (pUnit->isProjectedToDieNextTurn())
						{
							CvPlot* pRetreatPlot = TacticalAIHelpers::FindSafestPlotInReach(pUnit,false);
							if (pRetreatPlot)
								ExecuteMoveToPlotIgnoreDanger(pUnit,pRetreatPlot);
						}

					}
				}
			}
		}
		pUnit = pArmy->GetNextUnit();
	}
}

/// Store off a new unit that needs to move as part of an operational AI formation
void CvTacticalAI::MoveWithFormation(CvUnit* pUnit, MultiunitPositionTypes ePosition)
{
	if(pUnit->getMoves() > 0)
	{
		CvOperationUnit operationUnit;
		operationUnit.SetUnitID(pUnit->GetID());
		operationUnit.SetPosition(ePosition);
		m_OperationUnits.push_back(operationUnit);
	}
}

/// Gather all units requested through calls to MoveWithFormation() to army's location
void CvTacticalAI::ExecuteGatherMoves(CvArmyAI* pArmy)
{
	CvPlot* pTarget = pArmy->Plot();
	AITacticalTargetType eTargetType;

	// Gathering - treat everyone as a melee unit; don't need ranged in the rear yet
	int iUnits = m_OperationUnits.size();
	CvAIOperation* pOperation = m_pPlayer->getAIOperation(pArmy->GetOperationID());
	if(!pOperation || iUnits==0)
		return;

	// Range around target based on number of units we need to place
	int iRange = pOperation->GetGatherTolerance(pArmy,pTarget);

	// Try one time with computed range
	bool bFoundEnoughDeploymentPlots = false;
	if (ScoreDeploymentPlots(pTarget, pArmy, iUnits, 0, iRange))
	{
		// Did we get as many possible plots as units?
		if (m_TempTargets.size() >= (unsigned)iUnits)
		{
			bFoundEnoughDeploymentPlots = true;
		}
		else
		{
			m_TempTargets.clear();
		}
	}

	if (!bFoundEnoughDeploymentPlots)
	{
		//try again with additional space
		if (!ScoreDeploymentPlots(pTarget, pArmy, iUnits, 0, iRange++))
		{
			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("Operation aborting. Army ID: %d. Not enough spaces to deploy near target", pArmy->GetID());
				LogTacticalMessage(strLogString);
			}
			m_pPlayer->getAIOperation(pArmy->GetOperationID())->SetToAbort(AI_ABORT_NO_ROOM_DEPLOY);
			return;
		}
	}

	// Compute the moves to get the best deployment
	std::stable_sort(m_TempTargets.begin(), m_TempTargets.end());
	m_PotentialBlocks.clear();
	bool bDone = false;

	// see where our units can go
	std::map<int,ReachablePlots> unitMovePlots;
	FStaticVector<CvOperationUnit, SAFE_ESTIMATE_NUM_MULTIUNITFORMATION_ENTRIES, true, c_eCiv5GameplayDLL, 0>::iterator it;
	for(it = m_OperationUnits.begin(); it != m_OperationUnits.end(); it++)
	{
		CvUnit *pOpUnit = m_pPlayer->getUnit(it->GetUnitID());
		SPathFinderUserData data(pOpUnit,CvUnit::MOVEFLAG_IGNORE_STACKING,5);
		data.ePathType = PT_UNIT_REACHABLE_PLOTS;
		unitMovePlots[it->GetUnitID()] = GC.GetPathFinder().GetPlotsInReach(pOpUnit->plot(), data);
	}

	int iUnitsToPlace = iUnits;
	for(unsigned int iI = 0; iI < m_TempTargets.size() && !bDone; iI++)
	{
		eTargetType = m_TempTargets[iI].GetTargetType();

		CvPlot* pLoopPlot = GC.getMap().plot(m_TempTargets[iI].GetTargetX(), m_TempTargets[iI].GetTargetY());
		
		// Don't use if there's already a unit not in the army here
		CvUnit* pUnitAlreadyThere = pLoopPlot->getBestDefender(NO_PLAYER);
		if (!pUnitAlreadyThere || pUnitAlreadyThere->getArmyID() == pArmy->GetID())
		{
			if(FindClosestOperationUnit(pLoopPlot, unitMovePlots, true /*bIncludeRanged*/, false /*bMustBeRangedUnit*/, false))
			{
				for(unsigned int jJ = 0; jJ < m_CurrentMoveUnits.size(); jJ++)
				{
					CvBlockingUnit block;
					block.SetUnitID(m_CurrentMoveUnits[jJ].GetID());
					block.SetPlot(pLoopPlot);
					block.SetNumChoices((int)m_CurrentMoveUnits.size());
					block.SetDistanceToTarget(m_CurrentMoveUnits[jJ].GetMovesToTarget());
					m_PotentialBlocks.push_back(block);
				}
				iUnitsToPlace--;
				if(iUnitsToPlace == 0)
				{
					bDone = true;
				}	
			}
		}
	}

	// Now ready to make the assignments
	AssignDeployingUnits(iUnits - iUnitsToPlace);

	PerformChosenMoves();

	// Log if someone in army didn't get a move assigned (how do we address this in the future?)
	if(m_ChosenBlocks.size() < (unsigned int)(iUnits))
	{
		if(GC.getLogging() && GC.getAILogging())
		{
			CvString strMsg;
			strMsg.Format("No gather move for %d units", iUnits - m_ChosenBlocks.size());
			LogTacticalMessage(strMsg);
		}

		// Loop through all units available to operation and move them blindly
		FStaticVector<CvOperationUnit, SAFE_ESTIMATE_NUM_MULTIUNITFORMATION_ENTRIES, true, c_eCiv5GameplayDLL, 0>::iterator it;
		for (it = m_OperationUnits.begin(); it != m_OperationUnits.end(); it++)
		{
			CvUnit* pLoopUnit = m_pPlayer->getUnit(it->GetUnitID());
			if (!pLoopUnit || !pLoopUnit->canMove())
				continue;

			if (MoveToEmptySpaceNearTarget(pLoopUnit,pTarget,NO_DOMAIN,23))
				UnitProcessed(pLoopUnit->GetID());
			else
				pArmy->RemoveUnit(pLoopUnit->GetID());

		}
	}
}

/// Complete moves for all units requested through calls to MoveWithFormation()
void CvTacticalAI::ExecuteFormationMoves(CvArmyAI* pArmy, CvPlot *pTurnTarget)
{
	FStaticVector<CvOperationUnit, SAFE_ESTIMATE_NUM_MULTIUNITFORMATION_ENTRIES, true, c_eCiv5GameplayDLL, 0>::iterator it;

	if(m_OperationUnits.size() ==  0 || !pArmy)
	{
		return;
	}

	CvPlot* pCurrent = pArmy->Plot();
	if (!pCurrent)
		return;

	int iMeleeUnits = 0;
	int iRangedUnits = 0;
	std::map<int,ReachablePlots> unitMovePlots;
	for(it = m_OperationUnits.begin(); it != m_OperationUnits.end(); it++)
	{
		CvUnit *pOpUnit = m_pPlayer->getUnit(it->GetUnitID());
		if (pOpUnit->IsCanAttackRanged())
		{
			iRangedUnits++;
		}
		else
		{
			iMeleeUnits++;
		}

		// see where our units can go
		SPathFinderUserData data(pOpUnit,CvUnit::MOVEFLAG_IGNORE_STACKING,5);
		data.ePathType = PT_UNIT_REACHABLE_PLOTS;
		unitMovePlots[it->GetUnitID()] = GC.GetPathFinder().GetPlotsInReach(pOpUnit->plot(), data);
	}

#if defined(MOD_BALANCE_CORE)
	//the direction we are coming from
	DirectionTypes eFromDir = estimateDirection(pTurnTarget->getX(),pTurnTarget->getY(),pCurrent->getX(),pCurrent->getY());
	if(GC.getLogging() && GC.getAILogging())
	{
		CvString strMsg;
		strMsg.Format("Trying to move army %d from (%d:%d), to (%d:%d)",pArmy->GetID(),pCurrent->getX(),pCurrent->getY(),pTurnTarget->getX(),pTurnTarget->getY());
		LogTacticalMessage(strMsg);
	}
#endif

	int iRange = OperationalAIHelpers::GetGatherRangeForXUnits(iMeleeUnits+iRangedUnits);
	if (pArmy->GetDomainType() == DOMAIN_SEA)
		iRange++;

	// See if we have enough places to put everyone
	if(!ScoreDeploymentPlots(pTurnTarget, pArmy, iMeleeUnits, iRangedUnits, iRange))
	{
		if(GC.getLogging() && GC.getAILogging())
		{
			CvString strLogString;
			strLogString.Format("Operation aborting. Army ID: %d. Not enough spaces to deploy along formation's path", pArmy->GetID());
			LogTacticalMessage(strLogString);
		}
		m_pPlayer->getAIOperation(pArmy->GetOperationID())->SetToAbort(AI_ABORT_NO_ROOM_DEPLOY);
		return;
	}

	// Compute the moves to get the best deployment
	std::stable_sort(m_TempTargets.begin(), m_TempTargets.end());

	// First loop for melee units who should be out front
	int iMeleeUnitsToPlace = iMeleeUnits;
	bool bDone = false;
	if(iMeleeUnitsToPlace > 0)
	{
		for(unsigned int iI = 0; iI < m_TempTargets.size() && !bDone; iI++)
		{
			CvPlot* pLoopPlot = GC.getMap().plot(m_TempTargets[iI].GetTargetX(), m_TempTargets[iI].GetTargetY());

			// Don't use if there's already someone here
			if (!pLoopPlot->getBestDefender(NO_PLAYER))
			{
				//find a unit that's a little bit further back, and then try to move it to this good plot
				CvPlot* pIdealUnitPlot = plotDirection(pLoopPlot->getX(),pLoopPlot->getY(),eFromDir);
				if(FindClosestOperationUnit( pIdealUnitPlot, unitMovePlots, false /*bIncludeRanged*/, false /*bMustBeRangedUnit*/, false))
				{
					CvUnit* pInnerUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[0].GetID());
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strMsg;
						strMsg.Format("Deploying melee unit (first pass) %d (%s), to %d,%d, from %d,%d", pInnerUnit->GetID(), pInnerUnit->getName().GetCString(), pLoopPlot->getX(), pLoopPlot->getY(), pInnerUnit->getX(), pInnerUnit->getY());
						LogTacticalMessage(strMsg);
					}
					MoveToUsingSafeEmbark(pInnerUnit, pLoopPlot, false, 0);
					pInnerUnit->finishMoves();
					iMeleeUnitsToPlace--;
				}
			}
			if (iMeleeUnitsToPlace == 0)
			{
				bDone = true;
			}
		}
	}

	// Second loop for ranged units
	int iRangedUnitsToPlace = iRangedUnits;
	if(iRangedUnitsToPlace > 0)
	{
		bDone = false;
		for(unsigned int iI = 0; iI < m_TempTargets.size() && !bDone; iI++)
		{
			AITacticalTargetType eTargetType = m_TempTargets[iI].GetTargetType();
			CvPlot* pLoopPlot = GC.getMap().plot(m_TempTargets[iI].GetTargetX(), m_TempTargets[iI].GetTargetY());

			if (eTargetType == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT)
			{
				// Don't use if there's already someone here
				if (!pLoopPlot->getBestDefender(NO_PLAYER))
				{
					//find a unit that's a little bit further back, and then try to move it to this good plot
					CvPlot* pIdealUnitPlot = plotDirection(pLoopPlot->getX(),pLoopPlot->getY(),eFromDir);
					if(FindClosestOperationUnit( pIdealUnitPlot, unitMovePlots, true /*bIncludeRanged*/, true /*bMustBeRangedUnit*/, false))
					{
						CvUnit* pInnerUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[0].GetID());
						if(GC.getLogging() && GC.getAILogging())
						{
							CvString strMsg;
							strMsg.Format("Deploying ranged unit (first pass) %d (%s), to %d,%d, from %d,%d", pInnerUnit->GetID(), pInnerUnit->getName().GetCString(), pLoopPlot->getX(), pLoopPlot->getY(), pInnerUnit->getX(), pInnerUnit->getY());
							LogTacticalMessage(strMsg);
						}
						MoveToUsingSafeEmbark(pInnerUnit, pLoopPlot, false, 0);
						TacticalAIHelpers::PerformRangedAttackWithoutMoving(pInnerUnit);
						pInnerUnit->finishMoves();
						iRangedUnitsToPlace--;
					}
				}
			}
			if (iRangedUnitsToPlace == 0)
			{
				bDone = true;
			}
		}
	}

	// Third loop for all units we couldn't put in an ideal spot
	for(unsigned int iI = 0; iI < m_TempTargets.size() && !bDone; iI++)
	{
		CvPlot* pLoopPlot = GC.getMap().plot(m_TempTargets[iI].GetTargetX(), m_TempTargets[iI].GetTargetY());

		// Don't use if there's already someone here
		if (!pLoopPlot->getBestDefender(NO_PLAYER))
		{
			//find a unit that's a little bit further back, and then try to move it to this good plot
			CvPlot* pIdealUnitPlot = plotDirection(pLoopPlot->getX(),pLoopPlot->getY(),eFromDir);
			if(FindClosestOperationUnit( pIdealUnitPlot, unitMovePlots, true /*bIncludeRanged*/, false /*bMustBeRangedUnit*/, false))
			{
				CvUnit* pInnerUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[0].GetID());
				if(GC.getLogging() && GC.getAILogging())
				{
					CvString strMsg;
					strMsg.Format("Deploying third wave unit %d (%s), second pass, to %d,%d, from %d,%d", pInnerUnit->GetID(), pInnerUnit->getName().GetCString(), pLoopPlot->getX(), pLoopPlot->getY(), pInnerUnit->getX(), pInnerUnit->getY());
					LogTacticalMessage(strMsg);
				}

				MoveToUsingSafeEmbark(pInnerUnit, pLoopPlot, false, 0);
				TacticalAIHelpers::PerformRangedAttackWithoutMoving(pInnerUnit);
				pInnerUnit->finishMoves();
				if(!pInnerUnit->isRanged())
				{
					iMeleeUnitsToPlace--;
				}
				else
				{
					iRangedUnitsToPlace--;
				}
			}
		}
#if defined(MOD_BALANCE_CORE)
		if (iRangedUnitsToPlace == 0 && iMeleeUnitsToPlace == 0)
#else
		if (iRangedUnitsToPlace == 0)
#endif
		{
			bDone = true;
		}
	}

	// Log if someone in army didn't get a move assigned
#if defined(MOD_BALANCE_CORE)
	if (iRangedUnitsToPlace > 0 || iMeleeUnitsToPlace > 0)
#else
	if (iRangedUnitsToPlace > 0)
#endif
	{
		if(GC.getLogging() && GC.getAILogging())
		{
			CvString strMsg;
#if defined(MOD_BALANCE_CORE)
			strMsg.Format("No army deployment move for %d ranged units, %d melee units.", iRangedUnitsToPlace, iMeleeUnitsToPlace);
#else
			strMsg.Format("No army deployment move for %d ranged units", iRangedUnitsToPlace);
#endif
			LogTacticalMessage(strMsg);
		}

		// Loop through all units available to operation and move them blindly
		FStaticVector<CvOperationUnit, SAFE_ESTIMATE_NUM_MULTIUNITFORMATION_ENTRIES, true, c_eCiv5GameplayDLL, 0>::iterator it;
		for (it = m_OperationUnits.begin(); it != m_OperationUnits.end(); it++)
		{
			CvUnit* pLoopUnit = m_pPlayer->getUnit(it->GetUnitID());
			if (!pLoopUnit || !pLoopUnit->canMove())
				continue;

			MoveToEmptySpaceNearTarget(pLoopUnit,pTurnTarget,NO_DOMAIN,12);
		}
	}
}

bool CvTacticalAI::ScoreDeploymentPlots(CvPlot* pTarget, CvArmyAI* pArmy, int iNumMeleeUnits, int iNumRangedUnits, int iRange)
{
	int iScore;
	int iNumSafePlotsFound = 0;
	int iNumDeployPlotsFound = 0;
	CvTacticalTarget target;

	// We'll store the hexes we've found here
	m_TempTargets.clear();

	for(int iDX = -(iRange); iDX <= iRange; iDX++)
	{
		for(int iDY = -(iRange); iDY <= iRange; iDY++)
		{
			CvPlot* pPlot = plotXYWithRangeCheck(pTarget->getX(), pTarget->getY(), iDX, iDY, iRange);
			if (!pPlot)
				continue;

			bool bSafeForDeployment = true;
			bool bForcedToUseWater = false;

			int iPlotDistance = plotDistance(pPlot->getX(), pPlot->getY(), pTarget->getX(), pTarget->getY());
			int iPlotIndex = GC.getMap().plotNum(pPlot->getX(), pPlot->getY());
			CvTacticalAnalysisCell* pCell = GetTacticalAnalysisMap()->GetCell(iPlotIndex);
			CvAIOperation* pOperation = m_pPlayer->getAIOperation(pArmy->GetOperationID());

			bool bValid = false;
			if(pOperation->IsNavalOperation() && pCell->CanUseForOperationGatheringCheckWater(true /*bWater*/))
			{
				bValid = true;
			}
			else if(!pOperation->IsNavalOperation() && (pCell->CanUseForOperationGatheringCheckWater(false /*bWater*/) || GC.getMap().GetAIMapHint() & ciMapHint_Naval))
			{
				bValid = true;
				if (pCell->IsWater())
				{
					bForcedToUseWater = true;
				}
			}

			if(pOperation->IsNavalOperation())
			{
				if(!pArmy->IsAllOceanGoing() && pCell->IsOcean())
				{
					bValid = false;
				}
			}

			if (!bValid)
				continue;

			// Skip this plot if friendly unit that isn't in this army
			CvUnit* pFriendlyUnit = pPlot->getBestDefender(pArmy->GetOwner());
			if(pFriendlyUnit)
			{
				if(pFriendlyUnit->getArmyID() != pArmy->GetID())
				{
					continue;
				}
			}

			if (pPlot->isCity())
			{
				continue;
			}

			if (bForcedToUseWater)
			{
				iScore = 100 - (iPlotDistance * 10);
			}
			else
			{
				iScore = 1000 - (iPlotDistance * 100);
			}

			if(pCell->IsSubjectToAttack())
			{
				iScore -= 100;
				if (pArmy)
				{
					CvPlot* pPlot = pArmy->GetGoalPlot();
					//Let's only care if we're dealing with 
					if (pPlot != NULL && pPlot->getOwner() != NO_PLAYER && GET_PLAYER(pPlot->getOwner()).IsAtWarWith(m_pPlayer->GetID()))
					{
						bSafeForDeployment = false;
					}
				}
			}

			if(pCell->IsEnemyCanMovePast())
			{
				iScore -= 100;
			}
			else
			{
				iScore += pPlot->defenseModifier(m_pPlayer->getTeam(),false,false) * 2;
			}

			//todo: maybe avoid "slow" plot? but ending the turn there is usually good.
			//need to know a unit's previous position before making the call ...

			pCell->SetSafeForDeployment(bSafeForDeployment);
			pCell->SetDeploymentScore(iScore);

			// Save this in our list of potential targets
			target.SetTargetX(pPlot->getX());
			target.SetTargetY(pPlot->getY());
			target.SetAuxIntData(iScore);

			// A bit of a hack -- use high priority targets to indicate safe plots for ranged units
			if(bSafeForDeployment)
			{
				iNumSafePlotsFound++;
				iNumDeployPlotsFound++;
				target.SetTargetType(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT);
				pCell->SetTargetType(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT);
			}
			else
			{
				iNumDeployPlotsFound++;
				target.SetTargetType(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT);
				pCell->SetTargetType(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT);
			}

			m_TempTargets.push_back(target);
		}
	}

	// Make sure we found enough
	if(iNumRangedUnits > 0)
	{
		if(iNumSafePlotsFound < iNumRangedUnits && iNumDeployPlotsFound < (iNumMeleeUnits + iNumRangedUnits))
		{
			return false;
		}
	}
	else if(iNumDeployPlotsFound < iNumMeleeUnits)
	{
		return false;
	}

	return true;
}

void CvTacticalAI::ExecuteNavalFormationEscortMoves(CvArmyAI* pArmy, CvPlot* pTurnTarget)
{
	FStaticVector<CvOperationUnit, SAFE_ESTIMATE_NUM_MULTIUNITFORMATION_ENTRIES, true, c_eCiv5GameplayDLL, 0>::iterator it;

	if (m_OperationUnits.size() == 0)
	{
		return;
	}

#if defined(MOD_BALANCE_CORE)
	//Let's make sure we aren't excluding units here - assume 2 moves per turn (with embarkation changes ...)
	int iMaxTurns = max(5, pArmy->GetFurthestUnitDistance(pTurnTarget) / 2 + 1);
#endif

	int iNavalUnits = 0;
	int iEscortedUnits = 0;
	std::map<int, ReachablePlots> unitMovePlots;
	for (it = m_OperationUnits.begin(); it != m_OperationUnits.end(); it++)
	{
		CvUnit *pOpUnit = m_pPlayer->getUnit(it->GetUnitID());
		if (pOpUnit)
		{
			if (pOpUnit->getDomainType() == DOMAIN_LAND)
			{
				iEscortedUnits++;
			}
			else
			{
				if (pOpUnit->IsGreatAdmiral())
				{
					iEscortedUnits++;
				}
				else
				{
					iNavalUnits++;
				}
			}
		}

		// see where our units can go
		SPathFinderUserData data(pOpUnit, CvUnit::MOVEFLAG_IGNORE_STACKING, iMaxTurns);
		data.ePathType = PT_UNIT_REACHABLE_PLOTS;
		unitMovePlots[it->GetUnitID()] = GC.GetPathFinder().GetPlotsInReach(pOpUnit->plot(), data);
	}

	CvAIOperation* pOperation = m_pPlayer->getAIOperation(pArmy->GetOperationID());
	if (!pOperation)
		return;

	// Range around target based on number of units we need to place
	int iRange = pOperation->GetGatherTolerance(pArmy, pTurnTarget);

	// See if we have enough places to put everyone
	if (!ScoreDeploymentPlots(pTurnTarget, pArmy, iNavalUnits, iEscortedUnits, iRange) &&
		!ScoreDeploymentPlots(pTurnTarget, pArmy, iNavalUnits, iEscortedUnits, iRange + 1))
	{
		if (GC.getLogging() && GC.getAILogging())
		{
			CvString strLogString;
			strLogString.Format("Operation aborting. Army ID: %d. Not enough spaces to deploy near turn target", pArmy->GetID());
			LogTacticalMessage(strLogString);
		}
		m_pPlayer->getAIOperation(pArmy->GetOperationID())->SetToAbort(AI_ABORT_NO_ROOM_DEPLOY);
	}
	// Compute moves forescorted units first
	else
	{
		std::stable_sort(m_TempTargets.begin(), m_TempTargets.end());
		m_PotentialBlocks.clear();
		bool bDone = false;
		int iMostUnitsToPlace = iEscortedUnits;

		for (unsigned int iI = 0; iI < m_TempTargets.size() && !bDone; iI++)
		{
			CvPlot* pLoopPlot = GC.getMap().plot(m_TempTargets[iI].GetTargetX(), m_TempTargets[iI].GetTargetY());
			if (FindClosestNavalOperationUnit(pLoopPlot, unitMovePlots, true))
			{
				for (unsigned int jJ = 0; jJ < m_CurrentMoveUnits.size(); jJ++)
				{
					CvBlockingUnit block;
					block.SetUnitID(m_CurrentMoveUnits[jJ].GetID());
					block.SetPlot(pLoopPlot);
					block.SetNumChoices((int)m_CurrentMoveUnits.size());
					block.SetDistanceToTarget(m_CurrentMoveUnits[jJ].GetMovesToTarget());
					m_PotentialBlocks.push_back(block);
				}
				iMostUnitsToPlace--;
				if (iMostUnitsToPlace == 0)
				{
					bDone = true;
				}
			}
		}

		// Now ready to make the assignments
		AssignDeployingUnits(iEscortedUnits - iMostUnitsToPlace);

		PerformChosenMoves();


		// Log if someone in army didn't get a move assigned (how do we address this in the future?)
		if (m_ChosenBlocks.size() < (unsigned int)iEscortedUnits)
		{
			if (GC.getLogging() && GC.getAILogging())
			{
				CvString strMsg;
				strMsg.Format("No naval deployment move for %d units in first pass", iEscortedUnits - m_ChosenBlocks.size());
				LogTacticalMessage(strMsg);
			}
		}

		if (iNavalUnits > 0)
		{
			// Now repeat for the naval escorts, using new target plots of where the embarked units ended up.
			m_TempTargets.clear();
			CvTacticalTarget temp;

			for (it = m_OperationUnits.begin(); it != m_OperationUnits.end(); it++)
			{
				CvUnit *pOpUnit = m_pPlayer->getUnit(it->GetUnitID());
				if (pOpUnit)
				{
					if (pOpUnit->getDomainType() == DOMAIN_LAND && pOpUnit->isEmbarked())
					{
						temp.SetTargetX(pOpUnit->plot()->getX());
						temp.SetTargetY(pOpUnit->plot()->getY());
						m_TempTargets.push_back(temp);
					}
				}
			}
			//Not enough plots? Add in our chosen plots from the above function.
			if (m_TempTargets.size() < (unsigned int)iNavalUnits)
			{
				for (unsigned int iI = 0; iI < m_ChosenBlocks.size(); iI++)
				{
					temp.SetTargetX(m_ChosenBlocks[iI].GetPlot()->getX());
					temp.SetTargetY(m_ChosenBlocks[iI].GetPlot()->getY());
					m_TempTargets.push_back(temp);
				}
			}

			m_PotentialBlocks.clear();
			bDone = false;
			int iLeastUnitsToPlace = iNavalUnits;

			for (unsigned int iI = 0; iI < m_TempTargets.size() && !bDone; iI++)
			{
				CvPlot* pLoopPlot = GC.getMap().plot(m_TempTargets[iI].GetTargetX(), m_TempTargets[iI].GetTargetY());
				if (FindClosestNavalOperationUnit(pLoopPlot, unitMovePlots, false))
				{
					for (unsigned int jJ = 0; jJ < m_CurrentMoveUnits.size(); jJ++)
					{
						CvBlockingUnit block;
						block.SetUnitID(m_CurrentMoveUnits[jJ].GetID());
						block.SetPlot(pLoopPlot);
						block.SetNumChoices((int)m_CurrentMoveUnits.size());
						block.SetDistanceToTarget(m_CurrentMoveUnits[jJ].GetMovesToTarget());
						m_PotentialBlocks.push_back(block);
					}
					iLeastUnitsToPlace--;
					if (iLeastUnitsToPlace == 0)
					{
						bDone = true;
					}
				}
			}

			// Now ready to make the assignments
			AssignDeployingUnits(iNavalUnits - iLeastUnitsToPlace);
			PerformChosenMoves();

			if (m_ChosenBlocks.size() < (unsigned int)iNavalUnits)
			{
				if (GC.getLogging() && GC.getAILogging())
				{
					CvString strMsg;
					strMsg.Format("No naval deployment move for %d units in second pass", iNavalUnits - m_ChosenBlocks.size());
					LogTacticalMessage(strMsg);
				}
			}
		}
	}
}
/// Complete moves for all units requested through calls to MoveWithFormation()
void CvTacticalAI::ExecuteNavalFormationMoves(CvArmyAI* pArmy, CvPlot* pTurnTarget)
{
	FStaticVector<CvOperationUnit, SAFE_ESTIMATE_NUM_MULTIUNITFORMATION_ENTRIES, true, c_eCiv5GameplayDLL, 0>::iterator it;

	if(m_OperationUnits.size() == 0 || !pArmy)
	{
		return;
	}

	CvPlot* pCurrent = pArmy->Plot();
	if (!pCurrent)
		return;

	int iMeleeUnits = 0;
	int iRangedUnits = 0;
	std::map<int,ReachablePlots> unitMovePlots;
	for(it = m_OperationUnits.begin(); it != m_OperationUnits.end(); it++)
	{
		CvUnit *pOpUnit = m_pPlayer->getUnit(it->GetUnitID());
		if (pOpUnit->IsCanAttackRanged())
		{
			iRangedUnits++;
		}
		else
		{
			iMeleeUnits++;
		}

		// see where our units can go
		SPathFinderUserData data(pOpUnit,CvUnit::MOVEFLAG_IGNORE_STACKING,5);
		data.ePathType = PT_UNIT_REACHABLE_PLOTS;
		unitMovePlots[it->GetUnitID()] = GC.GetPathFinder().GetPlotsInReach(pOpUnit->plot(), data);
	}

#if defined(MOD_BALANCE_CORE)
	//the direction we are coming from
	DirectionTypes eFromDir = estimateDirection(pTurnTarget->getX(),pTurnTarget->getY(),pCurrent->getX(),pCurrent->getY());
	if(GC.getLogging() && GC.getAILogging())
	{
		CvString strMsg;
		strMsg.Format("Trying to move navy %d from (%d:%d), to (%d:%d)",pArmy->GetID(),pCurrent->getX(),pCurrent->getY(),pTurnTarget->getX(),pTurnTarget->getY());
		LogTacticalMessage(strMsg);
	}
#endif

	int iRange = OperationalAIHelpers::GetGatherRangeForXUnits(iMeleeUnits + iRangedUnits);
	if (pArmy->GetDomainType() == DOMAIN_SEA)
		iRange++;

	// See if we have enough places to put everyone
	if (!ScoreDeploymentPlots(pTurnTarget, pArmy, iMeleeUnits, iRangedUnits, iRange))
	{
		if(GC.getLogging() && GC.getAILogging())
		{
			CvString strLogString;
			strLogString.Format("Operation aborting. Army ID: %d. Not enough spaces to deploy along formation's path", pArmy->GetID());
			LogTacticalMessage(strLogString);
		}
		m_pPlayer->getAIOperation(pArmy->GetOperationID())->SetToAbort(AI_ABORT_NO_ROOM_DEPLOY);
		return;
	}

	// Compute the moves to get the best deployment
	std::stable_sort(m_TempTargets.begin(), m_TempTargets.end());

	// First loop for melee units who should be out front
	int iMeleeUnitsToPlace = iMeleeUnits;
	bool bDone = false;
	if(iMeleeUnitsToPlace > 0)
	{
		for(unsigned int iI = 0; iI < m_TempTargets.size() && !bDone; iI++)
		{
			CvPlot* pLoopPlot = GC.getMap().plot(m_TempTargets[iI].GetTargetX(), m_TempTargets[iI].GetTargetY());

			// Don't use if there's already someone here
			if (!pLoopPlot->getBestDefender(NO_PLAYER))
			{
				//find a unit that's a little bit further back, and then try to move it to this good plot
				CvPlot* pIdealUnitPlot = plotDirection(pLoopPlot->getX(),pLoopPlot->getY(),eFromDir);
				if(FindClosestOperationUnit( pIdealUnitPlot, unitMovePlots, false /*bIncludeRanged*/, false /*bMustBeRangedUnit*/, false))
				{
					CvUnit* pInnerUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[0].GetID());
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strMsg;
						strMsg.Format("Deploying melee unit (first pass) %d (%s), to %d,%d, from %d,%d", pInnerUnit->GetID(), pInnerUnit->getName().GetCString(), pLoopPlot->getX(), pLoopPlot->getY(), pInnerUnit->getX(), pInnerUnit->getY());
						LogTacticalMessage(strMsg);
					}
					MoveToUsingSafeEmbark(pInnerUnit, pLoopPlot, false, 0);
					pInnerUnit->finishMoves();
					iMeleeUnitsToPlace--;
				}
			}
			if (iMeleeUnitsToPlace == 0)
			{
				bDone = true;
			}
		}
	}

	// Second loop for ranged units
	int iRangedUnitsToPlace = iRangedUnits;
	if(iRangedUnitsToPlace > 0)
	{
		bDone = false;
		for(unsigned int iI = 0; iI < m_TempTargets.size() && !bDone; iI++)
		{
			AITacticalTargetType eTargetType = m_TempTargets[iI].GetTargetType();
			CvPlot* pLoopPlot = GC.getMap().plot(m_TempTargets[iI].GetTargetX(), m_TempTargets[iI].GetTargetY());

			if (eTargetType == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT)
			{
				// Don't use if there's already someone here
				if (!pLoopPlot->getBestDefender(NO_PLAYER))
				{
					//find a unit that's a little bit further back, and then try to move it to this good plot
					CvPlot* pIdealUnitPlot = plotDirection(pLoopPlot->getX(),pLoopPlot->getY(),eFromDir);
					if(FindClosestOperationUnit( pIdealUnitPlot, unitMovePlots, true /*bIncludeRanged*/, true /*bMustBeRangedUnit*/, false))
					{
						CvUnit* pInnerUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[0].GetID());
						if(GC.getLogging() && GC.getAILogging())
						{
							CvString strMsg;
							strMsg.Format("Deploying ranged unit (first pass) %d (%s), to %d,%d, from %d,%d", pInnerUnit->GetID(), pInnerUnit->getName().GetCString(), pLoopPlot->getX(), pLoopPlot->getY(), pInnerUnit->getX(), pInnerUnit->getY());
							LogTacticalMessage(strMsg);
						}
						MoveToUsingSafeEmbark(pInnerUnit, pLoopPlot, false, 0);
						TacticalAIHelpers::PerformRangedAttackWithoutMoving(pInnerUnit);
						pInnerUnit->finishMoves();
						iRangedUnitsToPlace--;
					}
				}
			}
			if (iRangedUnitsToPlace == 0)
			{
				bDone = true;
			}
		}
	}

	// Third loop for all units we couldn't put in an ideal spot
	for(unsigned int iI = 0; iI < m_TempTargets.size() && !bDone; iI++)
	{
		CvPlot* pLoopPlot = GC.getMap().plot(m_TempTargets[iI].GetTargetX(), m_TempTargets[iI].GetTargetY());

		// Don't use if there's already someone here
		if (!pLoopPlot->getBestDefender(NO_PLAYER))
		{
			//find a unit that's a little bit further back, and then try to move it to this good plot
			CvPlot* pIdealUnitPlot = plotDirection(pLoopPlot->getX(),pLoopPlot->getY(),eFromDir);
			if(FindClosestOperationUnit( pIdealUnitPlot, unitMovePlots, true /*bIncludeRanged*/, false /*bMustBeRangedUnit*/, false))
			{
				CvUnit* pInnerUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[0].GetID());
				if(GC.getLogging() && GC.getAILogging())
				{
					CvString strMsg;
					strMsg.Format("Deploying third wave unit %d (%s), second pass, to %d,%d, from %d,%d", pInnerUnit->GetID(), pInnerUnit->getName().GetCString(), pLoopPlot->getX(), pLoopPlot->getY(), pInnerUnit->getX(), pInnerUnit->getY());
					LogTacticalMessage(strMsg);
				}

				MoveToUsingSafeEmbark(pInnerUnit, pLoopPlot, false, 0);
				TacticalAIHelpers::PerformRangedAttackWithoutMoving(pInnerUnit);
				pInnerUnit->finishMoves();
				if(!pInnerUnit->isRanged())
				{
					iMeleeUnitsToPlace--;
				}
				else
				{
					iRangedUnitsToPlace--;
				}
			}
		}
#if defined(MOD_BALANCE_CORE)
		if (iRangedUnitsToPlace == 0 && iMeleeUnitsToPlace == 0)
#else
		if (iRangedUnitsToPlace == 0)
#endif
		{
			bDone = true;
		}
	}

	// Log if someone in army didn't get a move assigned
#if defined(MOD_BALANCE_CORE)
	if (iRangedUnitsToPlace > 0 || iMeleeUnitsToPlace > 0)
#else
	if (iRangedUnitsToPlace > 0)
#endif
	{
		if(GC.getLogging() && GC.getAILogging())
		{
			CvString strMsg;
#if defined(MOD_BALANCE_CORE)
			strMsg.Format("No army deployment move for %d ranged units, %d melee units.", iRangedUnitsToPlace, iMeleeUnitsToPlace);
#else
			strMsg.Format("No army deployment move for %d ranged units", iRangedUnitsToPlace);
#endif
			LogTacticalMessage(strMsg);
		}

		// Loop through all units available to operation and move them blindly
		FStaticVector<CvOperationUnit, SAFE_ESTIMATE_NUM_MULTIUNITFORMATION_ENTRIES, true, c_eCiv5GameplayDLL, 0>::iterator it;
		for (it = m_OperationUnits.begin(); it != m_OperationUnits.end(); it++)
		{
			CvUnit* pLoopUnit = m_pPlayer->getUnit(it->GetUnitID());
			if (!pLoopUnit || !pLoopUnit->canMove())
				continue;

			MoveToEmptySpaceNearTarget(pLoopUnit,pTurnTarget,DOMAIN_SEA,12);
		}
	}
}

// ROUTINES TO PROCESS AND SORT TARGETS

/// Mark units that can damage key items as priority targets
void CvTacticalAI::IdentifyPriorityTargets()
{
	CvCity* pLoopCity;
	int iCityLoop;
	FFastVector<CvTacticalTarget> possibleAttackers;
	int iExpectedDamage;
	int iExpectedTotalDamage;

	// Loop through each of our cities
	for(pLoopCity = m_pPlayer->firstCity(&iCityLoop); pLoopCity != NULL; pLoopCity = m_pPlayer->nextCity(&iCityLoop))
	{
		// Compile a list of units that can attack it this turn and what their expected damage is
#if defined(MOD_BALANCE_CORE)
		if(pLoopCity == NULL)
			continue;
#endif
		possibleAttackers.clear();
		iExpectedTotalDamage = 0;

		CvTacticalTarget* pTarget;
		pTarget = GetFirstUnitTarget();
		while(pTarget != NULL)
		{
			CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
			CvUnit* pEnemyUnit = pPlot->getVisibleEnemyDefender(m_pPlayer->GetID());
			if(pEnemyUnit)
			{
				iExpectedDamage = 0;

				if(pEnemyUnit->IsCanAttackRanged() && pEnemyUnit->GetMaxRangedCombatStrength(NULL, /*pCity*/ NULL, true, true) > pEnemyUnit->GetMaxAttackStrength(NULL, pLoopCity->plot(), NULL))
				{
					if(plotDistance(pEnemyUnit->getX(), pEnemyUnit->getY(), pLoopCity->getX(), pLoopCity->getY()) <= pEnemyUnit->GetRange())
					{
						if(pEnemyUnit->canEverRangeStrikeAt(pLoopCity->getX(), pLoopCity->getY()))
						{
							iExpectedDamage = pEnemyUnit->GetRangeCombatDamage(NULL, pLoopCity, false);
						}
					}
				}
				else if(pEnemyUnit->CanReachInXTurns(pLoopCity->plot(), 1))
				{
					int iAttackerStrength = pEnemyUnit->GetMaxAttackStrength(NULL, pLoopCity->plot(), NULL);
					int iDefenderStrength = pLoopCity->getStrengthValue();
					CvUnit* pFireSupportUnit = CvUnitCombat::GetFireSupportUnit(pLoopCity->getOwner(), pLoopCity->getX(), pLoopCity->getY(), pEnemyUnit->getX(), pEnemyUnit->getY());
					int iDefenderFireSupportCombatDamage = 0;
					if(pFireSupportUnit != NULL)
					{
						iDefenderFireSupportCombatDamage = pFireSupportUnit->GetRangeCombatDamage(pEnemyUnit, NULL, false);
					}
					iExpectedDamage = pEnemyUnit->getCombatDamage(iAttackerStrength, iDefenderStrength, pEnemyUnit->getDamage() + iDefenderFireSupportCombatDamage, /*bIncludeRand*/ false, /*bAttackerIsCity*/ false, /*bDefenderIsCity*/ true);
				}
				if(iExpectedDamage > 0)
				{
					iExpectedTotalDamage += iExpectedDamage;
					possibleAttackers.push_back(*pTarget);
				}
			}
			pTarget = GetNextUnitTarget();
		}

		// If they can take the city down and they are a melee unit, then they are a high priority target
		if(iExpectedTotalDamage > (pLoopCity->GetMaxHitPoints() - pLoopCity->getDamage()))
		{
			unsigned int iAttackerIndex = 0;

			// Loop until we've found all the attackers in the unit target list
			pTarget = GetFirstUnitTarget();
			while(iAttackerIndex < possibleAttackers.size() && pTarget != NULL)
			{
				// Match based on X, Y
				if(pTarget->GetTargetX() == possibleAttackers[iAttackerIndex].GetTargetX() &&
				        pTarget->GetTargetY() == possibleAttackers[iAttackerIndex].GetTargetY())
				{
					CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
					CvUnit* pEnemyUnit = pPlot->getVisibleEnemyDefender(m_pPlayer->GetID());
					if(pEnemyUnit && pEnemyUnit->IsCanAttackRanged())
					{
						pTarget->SetTargetType(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT);
					}
					else
					{
						pTarget->SetTargetType(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT);
					}
					iAttackerIndex++;
				}
				pTarget = GetNextUnitTarget();
			}
		}

		// If they can damage a city they are a medium priority target
		else if(possibleAttackers.size() > 0)
		{
			unsigned int iAttackerIndex = 0;

			// Loop until we've found all the attackers in the unit target list
			pTarget = GetFirstUnitTarget();
			while(iAttackerIndex < possibleAttackers.size() && pTarget != NULL)
			{
				// Match based on X, Y
				if(pTarget->GetTargetX() == possibleAttackers[iAttackerIndex].GetTargetX() &&
				        pTarget->GetTargetY() == possibleAttackers[iAttackerIndex].GetTargetY())
				{
					if(pTarget->GetTargetType() != AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT)
					{
						pTarget->SetTargetType(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT);
					}
					iAttackerIndex++;
				}
				pTarget = GetNextUnitTarget();
			}
		}
	}
}

/// Mark units that can damage our barbarian camps as priority targets
void CvTacticalAI::IdentifyPriorityBarbarianTargets()
{
	CvPlot* pLoopPlot;
	CvTacticalTarget* pTarget;

	for(int iI = 0; iI < GC.getMap().numPlots(); iI++)
	{
		pLoopPlot = GC.getMap().plotByIndexUnchecked(iI);
		if(pLoopPlot->getImprovementType() == GC.getBARBARIAN_CAMP_IMPROVEMENT())
		{
			pTarget = GetFirstUnitTarget();
			while(pTarget != NULL)
			{
				bool bPriorityTarget = false;

				// Skip if already a priority target (because was able to strike another camp)
				if(pTarget->GetTargetType() != AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT)
				{
					CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
					CvUnit* pEnemyUnit = pPlot->getVisibleEnemyDefender(m_pPlayer->GetID());
					if(pEnemyUnit->IsCanAttackRanged() && pEnemyUnit->GetMaxRangedCombatStrength(NULL, /*pCity*/ NULL, true, true) > pEnemyUnit->GetMaxAttackStrength(NULL, pLoopPlot, NULL))
					{
						if(plotDistance(pEnemyUnit->getX(), pEnemyUnit->getY(), pLoopPlot->getX(), pLoopPlot->getY()) <= pEnemyUnit->GetRange())
						{
							if(pEnemyUnit->canEverRangeStrikeAt(pLoopPlot->getX(), pLoopPlot->getY()))
							{
								bPriorityTarget = true;
							}
						}
					}
					else if(pEnemyUnit->CanReachInXTurns(pLoopPlot, 1))
					{
						bPriorityTarget = true;
					}
					if(bPriorityTarget)
					{
						pTarget->SetTargetType(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT);
					}
				}
				pTarget = GetNextUnitTarget();
			}
		}
	}
}

/// Mark units that we'd like to make opportunity attacks on because of their unit type (e.g. catapults)
void CvTacticalAI::IdentifyPriorityTargetsByType()
{
	// Look through all the enemies we can see
	for(unsigned int iI = 0; iI < m_AllTargets.size(); iI++)
	{
		// Don't consider units that are already medium priority
		if(m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT ||
		        m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT)
		{
			// Ranged units will always be medium priority targets
			CvUnit* pUnit = (CvUnit*)m_AllTargets[iI].GetAuxData();
			if(pUnit->IsCanAttackRanged())
			{
				m_AllTargets[iI].SetTargetType(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT);
			}
		}

		// Don't consider units that are already high priority
		if(m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT ||
		        m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT)
		{
			// Units defending citadels will always be high priority targets
			CvUnit* pUnit = (CvUnit*)m_AllTargets[iI].GetAuxData();
			ImprovementTypes eImprovement = pUnit->plot()->getImprovementType();
			if(pUnit->plot()->getOwner() == pUnit->getOwner() &&
			        eImprovement != NO_IMPROVEMENT && GC.getImprovementInfo(eImprovement)->GetNearbyEnemyDamage() > 0)
			{
				m_AllTargets[iI].SetTargetType(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT);
			}
		}
#if defined(MOD_BALANCE_CORE_MILITARY)
		if (MOD_BALANCE_CORE_MILITARY) 
		{
			// Don't consider units that are already higher than low priority
			if(m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT ||
					m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT)
			{
				//units which are in the front line should be medium at least
				CvUnit* pUnit = (CvUnit*)m_AllTargets[iI].GetAuxData();
				if (pUnit->plot()->GetNumSpecificPlayerUnitsAdjacent(m_pPlayer->GetID())>0)
					m_AllTargets[iI].SetTargetType(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT);
			}

			// Don't consider units that are already high priority
			if(m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT ||
					m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT)
			{
				// Units defending forts will always be high priority targets
				CvUnit* pUnit = (CvUnit*)m_AllTargets[iI].GetAuxData();
				ImprovementTypes eImprovement = pUnit->plot()->getImprovementType();
				if(eImprovement != NO_IMPROVEMENT && GC.getImprovementInfo(eImprovement)->GetDefenseModifier() > 0)
				{
					m_AllTargets[iI].SetTargetType(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT);
				}
				TerrainTypes eTerrain = pUnit->plot()->getTerrainType();
				//Units in weak terrain will be a high target.
				if(GC.getTerrainInfo(eTerrain)->getDefenseModifier() < 0)
				{
					m_AllTargets[iI].SetTargetType(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT);
				}
				//Is a unit below 1/4 health? If so, make it a high-priority target.
				if(pUnit && pUnit->getOwner() != m_pPlayer->GetID())
				{
					int iDamage = 0;
					iDamage = pUnit->GetMaxHitPoints() - pUnit->GetCurrHitPoints();
					if(iDamage != 0 && (iDamage <= (pUnit->GetMaxHitPoints() / 4)))
					{
						m_AllTargets[iI].SetTargetType(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT);
					}
				}
			}
			// Don't consider units that are already medium priority
			if(m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT ||
					m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT)
			{
				CvUnit* pUnit = (CvUnit*)m_AllTargets[iI].GetAuxData();
				//Units in defensive terrain will be more of a target.
				TerrainTypes eTerrain = pUnit->plot()->getTerrainType();
				if(GC.getTerrainInfo(eTerrain)->getDefenseModifier() > 0)
				{
					m_AllTargets[iI].SetTargetType(AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT);
				}
			}
		}
#endif
	}
}

/// after the high/mid/low prio unit target types are set, adjust the score so that the sorting works correctly
void CvTacticalAI::UpdateTargetScores()
{
	TacticalList::iterator it;
	for(it = m_AllTargets.begin(); it != m_AllTargets.end(); ++it)
	{
		if(it->GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT)
		{
			//initially all targets were low prio, if it's higher now we give it a boost
			it->SetAuxIntData( it->GetAuxIntData() + 25 );

			CvPlot* pPlot = GC.getMap().plot(it->GetTargetX(), it->GetTargetY());
			CvUnit* pUnit = pPlot->getVisibleEnemyDefender(m_pPlayer->GetID());
			if (pUnit)
			{
				//if the target is damage, give it a further boost
				it->SetAuxIntData( it->GetAuxIntData() + pUnit->getDamage()/10 );
			}
		}

		if(it->GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT)
		{
			//initially all targets were low prio, if it's higher now we give it a boost
			it->SetAuxIntData( it->GetAuxIntData() + 50 );

			CvPlot* pPlot = GC.getMap().plot(it->GetTargetX(), it->GetTargetY());
			CvUnit* pUnit = pPlot->getVisibleEnemyDefender(m_pPlayer->GetID());
			if (pUnit)
			{
				//if the target is damage, give it a further boost
				it->SetAuxIntData( it->GetAuxIntData() + pUnit->getDamage()/10 );
			}
		}
	}
}

/// Sift through the target list and find just those that apply to the dominance zone we are currently looking at
void CvTacticalAI::ExtractTargetsForZone(CvTacticalDominanceZone* pZone /* Pass in NULL for all zones */)
{
	TacticalList::iterator it;

	m_ZoneTargets.clear();
	for(it = m_AllTargets.begin(); it != m_AllTargets.end(); ++it)
	{
		bool bValid = false;

		if(pZone == NULL)
		{
			bValid = true;
		}
		else
		{
			DomainTypes eDomain = pZone->IsWater() ? DOMAIN_SEA : DOMAIN_LAND;
			bValid = it->IsTargetValidInThisDomain(eDomain);
		}

		if(bValid)
		{
			if(pZone == NULL || it->GetDominanceZone() == pZone->GetDominanceZoneID())
			{
				m_ZoneTargets.push_back(*it);
			}

			// Not obviously in this zone, but if within 2 of city we want them anyway
			else
			{
				CvCity* pCity = pZone->GetZoneCity();
				if(pCity)
				{
					if(plotDistance(pCity->getX(), pCity->getY(), it->GetTargetX(), it->GetTargetY()) <= 2)
					{
						m_ZoneTargets.push_back(*it);
					}
				}
			}
		}
	}
}

/// Find the first target of a requested type in current dominance zone (call after ExtractTargetsForZone())
CvTacticalTarget* CvTacticalAI::GetFirstZoneTarget(AITacticalTargetType eType)
{
	m_eCurrentTargetType = eType;
	m_iCurrentTargetIndex = 0;

	while(m_iCurrentTargetIndex < (int)m_ZoneTargets.size())
	{
		if(m_eCurrentTargetType == AI_TACTICAL_TARGET_NONE || m_ZoneTargets[m_iCurrentTargetIndex].GetTargetType() == m_eCurrentTargetType)
		{
			return &m_ZoneTargets[m_iCurrentTargetIndex];
		}
		m_iCurrentTargetIndex++;
	}

	return NULL;
}

/// Find the next target of a requested type in current dominance zone (call after GetFirstZoneTarget())
CvTacticalTarget* CvTacticalAI::GetNextZoneTarget()
{
	m_iCurrentTargetIndex++;

	while(m_iCurrentTargetIndex < (int)m_ZoneTargets.size())
	{
		if(m_eCurrentTargetType == AI_TACTICAL_TARGET_NONE || m_ZoneTargets[m_iCurrentTargetIndex].GetTargetType() == m_eCurrentTargetType)
		{
			return &m_ZoneTargets[m_iCurrentTargetIndex];
		}
		m_iCurrentTargetIndex++;
	}

	return NULL;
}

/// Find the first unit target (in full list of targets -- NOT by zone)
CvTacticalTarget* CvTacticalAI::GetFirstUnitTarget()
{
	m_iCurrentUnitTargetIndex = 0;

	while(m_iCurrentUnitTargetIndex < (int)m_AllTargets.size())
	{
		if(m_AllTargets[m_iCurrentUnitTargetIndex].GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT ||
		        m_AllTargets[m_iCurrentUnitTargetIndex].GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT ||
		        m_AllTargets[m_iCurrentUnitTargetIndex].GetTargetType() == AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT)
		{
			return &m_AllTargets[m_iCurrentUnitTargetIndex];
		}
		m_iCurrentUnitTargetIndex++;
	}

	return NULL;
}

/// Find the next unit target (call after GetFirstUnitTarget())
CvTacticalTarget* CvTacticalAI::GetNextUnitTarget()
{
	m_iCurrentUnitTargetIndex++;

	while(m_iCurrentUnitTargetIndex < (int)m_AllTargets.size())
	{
		if(m_AllTargets[m_iCurrentUnitTargetIndex].GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT ||
		        m_AllTargets[m_iCurrentUnitTargetIndex].GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT ||
		        m_AllTargets[m_iCurrentUnitTargetIndex].GetTargetType() == AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT)
		{
			return &m_AllTargets[m_iCurrentUnitTargetIndex];
		}
		m_iCurrentUnitTargetIndex++;
	}

	return NULL;
}

// ROUTINES TO EXECUTE A MISSION

/// Capture the gold from a barbarian camp
void CvTacticalAI::ExecuteBarbarianCampMove(CvPlot* pTargetPlot)
{
	// Move first one to target
	CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[0].GetID());
	if(pUnit)
	{
		pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pTargetPlot->getX(), pTargetPlot->getY());
		pUnit->finishMoves();

		// Delete this unit from those we have to move
		UnitProcessed(m_CurrentMoveUnits[0].GetID());
	}
}

/// Capture an undefended civilian
void CvTacticalAI::ExecuteCivilianCapture(CvPlot* pTargetPlot)
{
	// Move first one to target
	CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[0].GetID());
	if(pUnit)
	{
		pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pTargetPlot->getX(), pTargetPlot->getY(), CvUnit::MOVEFLAG_NO_EMBARK);

		// Delete this unit from those we have to move
		if (!pUnit->canMove())
			UnitProcessed(m_CurrentMoveUnits[0].GetID());
	}
}

/// Pillage an undefended improvement
void CvTacticalAI::ExecutePillage(CvPlot* pTargetPlot)
{
	// Move first one to target
	CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[0].GetID());
	if(pUnit)
	{
		if(pUnit->canMoveInto(*pTargetPlot, CvUnit::MOVEFLAG_DESTINATION ))
		{
			pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pTargetPlot->getX(), pTargetPlot->getY());
			pUnit->PushMission(CvTypes::getMISSION_PILLAGE());
		}
		else
			MoveToEmptySpaceNearTarget(pUnit,pTargetPlot,DOMAIN_LAND,23);
		pUnit->finishMoves();

		// Delete this unit from those we have to move
		UnitProcessed(m_CurrentMoveUnits[0].GetID());
	}
}

/// Pillage an undefended improvement
void CvTacticalAI::ExecutePlunderTradeUnit(CvPlot* pTargetPlot)
{
	// Move first one to target
	CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[0].GetID());
	if(pUnit)
	{
		if(pUnit->canMoveInto(*pTargetPlot, CvUnit::MOVEFLAG_DESTINATION ))
		{
			pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pTargetPlot->getX(), pTargetPlot->getY());
			pUnit->PushMission(CvTypes::getMISSION_PLUNDER_TRADE_ROUTE());
		}
		else
			MoveToEmptySpaceNearTarget(pUnit,pTargetPlot,NO_DOMAIN,23);

		pUnit->finishMoves();
		// Delete this unit from those we have to move
		UnitProcessed(m_CurrentMoveUnits[0].GetID());
	}
}

/// Paradrop in to pillage an undefended improvement
void CvTacticalAI::ExecuteParadropPillage(CvPlot* pTargetPlot)
{
	// Move first one to target
	CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[0].GetID());
	if(pUnit)
	{
		pUnit->PushMission(CvTypes::getMISSION_PARADROP(), pTargetPlot->getX(), pTargetPlot->getY());
		pUnit->PushMission(CvTypes::getMISSION_PILLAGE());
		pUnit->finishMoves();

		// Delete this unit from those we have to move
		UnitProcessed(m_CurrentMoveUnits[0].GetID());
	}
}

void CvTacticalAI::MoveUpReliefUnits(CvTacticalTarget& kTarget)
{
	CvPlot* pTargetPlot = GC.getMap().plot(kTarget.GetTargetX(),kTarget.GetTargetY());

	// Move up some replacements - but don't tie up too many units
	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if(!pUnit || !pUnit->canMove())
			continue;

		if (pUnit->IsCanAttackWithMove())
		{
			if (!TacticalAIHelpers::HaveEnoughMeleeUnitsAroundTarget(pUnit->getOwner(), &kTarget))
			{
				if (MoveToEmptySpaceNearTarget(pUnit, pTargetPlot, pUnit->getDomainType(), 12))
				{
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strMsg;
						strMsg.Format("Moving relief unit for attack next turn, Now at X: %d, Y: %d", pUnit->getX(), pUnit->getY());
						LogTacticalMessage(strMsg);
					}

					//just for inspection in GUI
					pUnit->SetMissionAI(MISSIONAI_ASSAULT,pTargetPlot,NULL);

					UnitProcessed(pUnit->GetID());
				}
			}
		}
	}
}

/// Attack a defended space
void CvTacticalAI::ExecuteAttack(CvTacticalTarget* pTarget, CvPlot* pTargetPlot, bool bPreserveMeleeUnits)
{
#if defined(MOD_AI_SMART_AIR_TACTICS)
	if (MOD_AI_SMART_AIR_TACTICS) {
		// Start by sending possible air sweeps
		for(unsigned int iI = 0; iI < m_CurrentAirSweepUnits.size(); iI++)
		{
			CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentAirSweepUnits[iI].GetID());

			if(pUnit && pUnit->canMove())
			{
				if(pUnit->canAirSweep())
				{
					if (pTarget)
					{
						pUnit->PushMission(CvTypes::getMISSION_AIR_SWEEP(), pTargetPlot->getX(), pTargetPlot->getY());
						UnitProcessed(m_CurrentAirSweepUnits[iI].GetID(), false /*bMarkTacticalMap*/);

						if(GC.getLogging() && GC.getAILogging())
						{
							CvString strMsg;
							strMsg.Format("Sending %s to air sweep prior to attack to Target X: %d, Y: %d", pUnit->getName().GetCString(), pTargetPlot->getX(), pTargetPlot->getY());
							LogTacticalMessage(strMsg);
						}
					}
				}
			}
		}
	}
#endif

	// Start by applying damage from city bombards
	for(unsigned int iI = 0; iI < m_CurrentMoveCities.size(); iI++)
	{
		CvCity* pCity = m_pPlayer->getCity(m_CurrentMoveCities[iI].GetID());
		if(pCity != NULL)
		{
			if ( PerformAttack(pCity, pTarget) )
				//target was killed - must have been a unit, cities cannot attack other cities
				return;
		}
	}


#if defined(MOD_BALANCE_CORE_MILITARY)
	// Make a list of plots adjacent to the target, which are suitable for melee attacks
	std::vector<CvPlot*> plotListWater;
	std::vector<CvPlot*> plotListLand;
	for(int iDirectionLoop = 0; iDirectionLoop < NUM_DIRECTION_TYPES; ++iDirectionLoop)
	{
		CvPlot* pAdjacentPlot = plotDirection(pTargetPlot->getX(), pTargetPlot->getY(), ((DirectionTypes)iDirectionLoop));
		if (pAdjacentPlot && pAdjacentPlot->isValidMovePlot(m_pPlayer->GetID()))
		{
			if (pAdjacentPlot->isWater())
				plotListWater.push_back(pAdjacentPlot);
			else			
				plotListLand.push_back(pAdjacentPlot);
		}
	}

	// Loop for melee units just to reposition.
	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if(!pUnit || !pUnit->canMove())
			continue;

		// Are we a melee unit
		if(pUnit->IsCanAttackRanged())
			continue;

		bool bUnitIsDamaged = (pUnit->getDamage()>pUnit->GetCurrHitPoints());
		if(bUnitIsDamaged && m_CurrentMoveUnits[iI].GetExpectedTargetDamage() < m_CurrentMoveUnits[iI].GetExpectedSelfDamage()*0.8)
			continue;

		if(bPreserveMeleeUnits && ((m_CurrentMoveUnits[iI].GetExpectedSelfDamage() + pUnit->getDamage()) > pUnit->GetMaxHitPoints()/2))
			continue;

		// Not adjacent to target
		if (plotDistance(pUnit->getX(), pUnit->getY(), pTargetPlot->getX(), pTargetPlot->getY()) > 1)
		{
			// Find spaces adjacent to target we can move into with MP left
			std::vector<CvPlot*>::iterator it;
			std::vector<CvPlot*>& plotList = (pUnit->getDomainType()==DOMAIN_LAND) ? plotListLand : plotListWater;

			// todo: special handling for amphibious units?
			for (it = plotList.begin(); it != plotList.end(); it++)
			{
				//if there's a friendly ranged unit there, check if we can swap places
				CvUnit* pOtherUnit = (*it)->getBestDefender(NO_PLAYER);
				if(pOtherUnit && pOtherUnit->isRanged() && pOtherUnit->getOwner()==m_pPlayer->GetID())
				{
					CvPlot* pFriendlyPlot = pOtherUnit->plot();
					if(pUnit->CanSwapWithUnitHere(*pFriendlyPlot))
					{
						// Move up there
						pOtherUnit->SetActivityType(ACTIVITY_AWAKE);
						pUnit->PushMission(CvTypes::getMISSION_SWAP_UNITS(), pOtherUnit->getX(), pOtherUnit->getY());
						if(GC.getLogging() && GC.getAILogging())
						{
							CvString strMsg;
							CvString strTemp = GC.getUnitInfo(pUnit->getUnitType())->GetDescription();
							CvString strTemp2 = GC.getUnitInfo(pOtherUnit->getUnitType())->GetDescription();
							strMsg.Format("Pulling back %s and swapping in %s for proper organization (ExecuteAttack), x: %d, y: %d", strTemp2.GetCString(), strTemp.GetCString(), (*it)->getX(), (*it)->getY());
							LogTacticalMessage(strMsg);
						}

						//dangerous but we break after erasing so it's ok
						plotList.erase(it);
						break;
					}
				}

				//empty plot
				if (!pOtherUnit)
				{
					//don't walk into certain death
					if (pUnit->GetDanger(*it) > pUnit->GetCurrHitPoints()*3)
						continue;

					//see if we can go there this turn
					if (pUnit->TurnsToReachTarget(*it, false /*bIgnoreUnits*/, false /*bIgnoreStacking*/, 1) > 0)
						continue;

					// Move there directly
					pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), (*it)->getX(), (*it)->getY(), CvUnit::MOVEFLAG_IGNORE_DANGER | CvUnit::MOVEFLAG_SAFE_EMBARK  );
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strMsg;
						CvString strTemp = GC.getUnitInfo(pUnit->getUnitType())->GetDescription();
						strMsg.Format("Moving %s for proper organization, x: %d, y: %d (ExecuteAttack)", strTemp.GetCString(), (*it)->getX(), (*it)->getY());
						LogTacticalMessage(strMsg);
					}

					//dangerous but we break after erasing so it's ok
					plotList.erase(it);
					break;
				}
			}
		}
	}
#endif

	// First loop is ranged units only
	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if(!pUnit || pUnit->getMoves() == 0)
			continue;

		// Are we a ranged unit
		if(pUnit->IsCanAttackRanged() && m_CurrentMoveUnits[iI].GetExpectedTargetDamage()>0)
		{
			bool bQueueTryRangedAttack = false;

			// Are we in range and do we have LOS to the target? if we are right next to the target, also try to move
			if(pUnit->canEverRangeStrikeAt(pTargetPlot->getX(), pTargetPlot->getY()) && !pTargetPlot->isAdjacent(pUnit->plot()))
			{
				bQueueTryRangedAttack = true;
			}
			else
			{
				// Not in range/ not LOS: then lets try to reposition unit.
				CvPlot* repositionPlot = GetBestRepositionPlot(pUnit, pTargetPlot, pUnit->GetCurrHitPoints()-1);

				if (repositionPlot != NULL)
				{
					pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), repositionPlot->getX(), repositionPlot->getY(), CvUnit::MOVEFLAG_IGNORE_DANGER | CvUnit::MOVEFLAG_SAFE_EMBARK );
					bQueueTryRangedAttack = true;
				}
			}

			if (bQueueTryRangedAttack)
			{
				// Do we have LOS to the target?
				if(pUnit->canEverRangeStrikeAt(pTargetPlot->getX(), pTargetPlot->getY()))
				{
					// Do we need to set up to make a ranged attack?
					if(pUnit->canSetUpForRangedAttack(NULL))
					{
						pUnit->setSetUpForRangedAttack(true);

						if(!pUnit->canMove())
						{
							UnitProcessed(pUnit->GetID());
							continue;
						}
					}

					if (GC.getLogging() && GC.getAILogging())
					{
						CvString strMsg;
						strMsg.Format("Attacking %d,%d with %s. Have %d hitpoints, expected damage %d for target, %d for us, X: %d, Y: %d",
							pTarget->GetTargetX(), pTarget->GetTargetY(), pUnit->getName().GetCString(), pUnit->GetCurrHitPoints(), m_CurrentMoveUnits[iI].GetExpectedTargetDamage(),
							m_CurrentMoveUnits[iI].GetExpectedSelfDamage(), pUnit->getX(), pUnit->getY());
						LogTacticalMessage(strMsg);
					}

					// perform this attack
					if (PerformAttack(pUnit, pTarget))
						//target killed or down to zero hitpoints (for cities)
						break;
				}
			}
		}
	}

	//if the target was a unit, it might be dead by now
	if (!pTarget->IsTargetStillAlive(m_pPlayer->GetID()))
		return;

	// Second loop are only melee units
	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if(!pUnit || !pUnit->canMove())
			continue;

		// Are we a melee unit
		if(pUnit->IsCanAttackRanged())
			continue;

		bool bUnitIsDamaged = (pUnit->getDamage()>pUnit->GetCurrHitPoints()); //below 50hp
		if(bUnitIsDamaged && m_CurrentMoveUnits[iI].GetExpectedTargetDamage() < m_CurrentMoveUnits[iI].GetExpectedSelfDamage()*0.8)
		{
			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strMsg;
				strMsg.Format("Not attacking %d,%d with %s. Can't generate a good damage ratio. (target %d, self %d)", 
					pTarget->GetTargetX(), pTarget->GetTargetY(), pUnit->getName().c_str(), m_CurrentMoveUnits[iI].GetExpectedTargetDamage(), m_CurrentMoveUnits[iI].GetExpectedSelfDamage() );
				LogTacticalMessage(strMsg);
			}
			continue;
		}

		if (!bPreserveMeleeUnits || ((m_CurrentMoveUnits[iI].GetExpectedSelfDamage() + pUnit->getDamage()) < (pUnit->GetMaxHitPoints()*4)/10)) //at least 40hp remaining
		{
			bool bAttackNow = false;
						
			// Already adjacent?
			if (plotDistance(pUnit->getX(), pUnit->getY(), pTargetPlot->getX(), pTargetPlot->getY()) <= 1)
			{
				bAttackNow = true;
			}
			else
			{
				// Find spaces adjacent to target we can move into
				std::vector<CvPlot*>::iterator it;
				std::vector<CvPlot*>& plotList = (pUnit->getDomainType()==DOMAIN_LAND) ? plotListLand : plotListWater;

				// todo: special handling for amphibious units?
				for (it = plotList.begin(); it != plotList.end(); it++)
				{
					//see if we can go there in the immediate future
					if (pUnit->TurnsToReachTarget(*it, false /*bIgnoreUnits*/, false /*bIgnoreStacking*/, 1) > 2)
						continue;

					// Move there directly
					pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), (*it)->getX(), (*it)->getY(), CvUnit::MOVEFLAG_IGNORE_DANGER | CvUnit::MOVEFLAG_SAFE_EMBARK );

					if (pUnit->canMove() && !pUnit->isOutOfAttacks())
						bAttackNow = true;

					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strMsg;
						CvString strTemp = GC.getUnitInfo(pUnit->getUnitType())->GetDescription();
						strMsg.Format("Moving %s for %s melee attack (loop 2), x: %d, y: %d", strTemp.GetCString(), bAttackNow ? "immediate" : "next turn", (*it)->getX(), (*it)->getY());
						LogTacticalMessage(strMsg);
					}

					//dangerous but we break after erasing so it's ok
					plotList.erase(it);
					break;
				}
			}

			if (bAttackNow)
			{
				if (GC.getLogging() && GC.getAILogging())
				{
					CvString strMsg;
					strMsg.Format("Attacking %d,%d with %s. Have %d hitpoints, expected damage %d for target, %d for us, X: %d, Y: %d",
						pTarget->GetTargetX(), pTarget->GetTargetY(), pUnit->getName().GetCString(), pUnit->GetCurrHitPoints(), m_CurrentMoveUnits[iI].GetExpectedTargetDamage(),
						m_CurrentMoveUnits[iI].GetExpectedSelfDamage(), pUnit->getX(), pUnit->getY());
					LogTacticalMessage(strMsg);
				}

				// perform this attack
				if ( PerformAttack(pUnit, pTarget) )
					//target killed or captured (for cities)
					return;
			}
			else
			{
				if(GC.getLogging() && GC.getAILogging())
				{
					CvString strMsg;
					CvString strTemp = GC.getUnitInfo(pUnit->getUnitType())->GetDescription();
					strMsg.Format("Not enough free hexes next melee unit %s to attack, Target X: %d, Y: %d", strTemp.GetCString(), pTargetPlot->getX(), pTargetPlot->getY());
					LogTacticalMessage(strMsg);
				}
			}
		}
		else
		{
			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strMsg;
				strMsg.Format("Not attacking %d,%d with %s. Low on health: %d hitpoints, damage %d for target, %d for us, X: %d, Y: %d", 
					pTarget->GetTargetX(), pTarget->GetTargetY(), pUnit->getName().GetCString(), pUnit->GetCurrHitPoints(), m_CurrentMoveUnits[iI].GetExpectedTargetDamage(), 
					m_CurrentMoveUnits[iI].GetExpectedSelfDamage(), pUnit->getX(), pUnit->getY());
				LogTacticalMessage(strMsg);
			}
		}
	}
}

#if defined(MOD_BALANCE_CORE_MILITARY)
bool IsGoodPlotForStaging(CvPlayer* pPlayer, CvPlot* pCandidate, bool bWater)
{
	if (!pPlayer || !pCandidate)
		return false;

	//we want a plot that is
	//- not too close and not too far from our cities
	//- is not a route (don't block roads)
	//- doesn't have too many other units around (don't build impenetrable walls of units)

	if (pCandidate->getNumUnits()>0)
		return false;

	int iCityDistance = pPlayer->GetCityDistanceInEstimatedTurns(pCandidate);
	if (iCityDistance<2 || iCityDistance>3)
		return false;

	if (pCandidate->getRouteType()!=NO_ROUTE)
		return false;

	int iNeighboringUnits = pCandidate->GetNumSpecificPlayerUnitsAdjacent(pPlayer->GetID(),NULL,NULL,true);
	if (iNeighboringUnits>2)
		return false;

	if (pCandidate->countPassableNeighbors(bWater)<3)
		return false;

	return true;
}
#endif

/// Execute moving units to a better location
void CvTacticalAI::ExecuteRepositionMoves()
{
	CvPlot* pBestPlot = NULL;
	CvString strTemp;

	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if(pUnit)
		{
			strTemp = pUnit->getUnitInfo().GetDescription();

			// LAND MOVES
			if(pUnit->getDomainType() == DOMAIN_LAND)
			{
				pBestPlot = FindNearbyTarget(pUnit, m_iRepositionRange);
				if(pBestPlot)
				{
					if(MoveToEmptySpaceNearTarget(pUnit, pBestPlot, pUnit->getDomainType(), 12))
					{
						TacticalAIHelpers::PerformOpportunityAttack(pUnit,pBestPlot);
						pUnit->finishMoves();
						UnitProcessed(m_CurrentMoveUnits[iI].GetID(), pUnit->IsCombatUnit());
						
						if(GC.getLogging() && GC.getAILogging())
						{
							CvString strLogString;
							strLogString.Format("%s moving to empty space near offensive target (RepositionMoves), X: %d, Y: %d, Current X: %d, Current Y: %d", strTemp.GetCString(),
							                    pBestPlot->getX(), pBestPlot->getY(), pUnit->getX(), pUnit->getY());
							LogTacticalMessage(strLogString);
						}
					}
				}
#if defined(MOD_BALANCE_CORE)
				else
				{
					pBestPlot = FindNearbyTarget(pUnit, m_iRepositionRange, AI_TACTICAL_TARGET_NONE, NULL, true, false);
					if(pBestPlot)
					{
						if(MoveToEmptySpaceNearTarget(pUnit, pBestPlot, pUnit->getDomainType(), 12))
						{
							pUnit->finishMoves();
							UnitProcessed(m_CurrentMoveUnits[iI].GetID(), pUnit->IsCombatUnit());

							if(GC.getLogging() && GC.getAILogging())
							{
								CvString strLogString;
								strLogString.Format("%s moving to empty space near defensive target (RepositionMoves), X: %d, Y: %d, Current X: %d, Current Y: %d", strTemp.GetCString(),
													pBestPlot->getX(), pBestPlot->getY(), pUnit->getX(), pUnit->getY());
								LogTacticalMessage(strLogString);
							}
						}
					}
				}

				//do not automatically end the turn ... unit might be useful for homeland. at least homeland patrol move will apply and find a nice spot for it
#endif
			}
#if defined(MOD_BALANCE_CORE_MILITARY)
			else if (pUnit->getDomainType() == DOMAIN_SEA)
			{
				bool bMoveMade = false;
				//Let's find an offensive target first.
				pBestPlot = FindNearbyTarget(pUnit, m_iRepositionRange);
				if(pBestPlot)
				{
					if(MoveToEmptySpaceNearTarget(pUnit, pBestPlot, DOMAIN_SEA, 12))
					{
						TacticalAIHelpers::PerformOpportunityAttack(pUnit,pBestPlot);
						bMoveMade = true;

						if(GC.getLogging() && GC.getAILogging())
						{
							CvString strLogString;
							strLogString.Format("%s moving to empty space near offensive naval target (RepositionMoves), X: %d, Y: %d, Current X: %d, Current Y: %d", strTemp.GetCString(),
												pBestPlot->getX(), pBestPlot->getY(), pUnit->getX(), pUnit->getY());
							LogTacticalMessage(strLogString);
						}
					}
				}
				else
				{
					pBestPlot = FindNearbyTarget(pUnit, m_iRepositionRange, AI_TACTICAL_TARGET_NONE, NULL, true, false);
					if(pBestPlot)
					{
						if(MoveToEmptySpaceNearTarget(pUnit, pBestPlot, DOMAIN_SEA, 12))
						{
							bMoveMade = true;

							if(GC.getLogging() && GC.getAILogging())
							{
								CvString strLogString;
								strLogString.Format("%s moving to empty space near defensive naval target (RepositionMoves), X: %d, Y: %d, Current X: %d, Current Y: %d", strTemp.GetCString(),
													pBestPlot->getX(), pBestPlot->getY(), pUnit->getX(), pUnit->getY());
								LogTacticalMessage(strLogString);
							}
						}
					}
				}

				//stay put if we're in a good place already
				if (IsGoodPlotForStaging(m_pPlayer,pUnit->plot(),pUnit->getDomainType()==DOMAIN_SEA))
					bMoveMade=true;

				if (!bMoveMade)
				{
					ReachablePlots reachablePlots;
					TacticalAIHelpers::GetAllPlotsInReachThisTurn(pUnit,pUnit->plot(),reachablePlots,true,true,false);
					for (ReachablePlots::iterator it=reachablePlots.begin(); it!=reachablePlots.end(); ++it)
					{
						CvPlot* pLoopPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);

						if ( IsGoodPlotForStaging(m_pPlayer,pLoopPlot,pUnit->getDomainType()==DOMAIN_SEA) )
						{
							pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pLoopPlot->getX(), pLoopPlot->getY(), CvUnit::MOVEFLAG_IGNORE_DANGER);
							TacticalAIHelpers::PerformRangedAttackWithoutMoving(pUnit);
							bMoveMade = true;

							if(GC.getLogging() && GC.getAILogging())
							{
								CvString strLogString;
								strLogString.Format("%s moving to empty space to avoid being in the way (RepositionMoves), X: %d, Y: %d, Current X: %d, Current Y: %d", strTemp.GetCString(),
													pLoopPlot->getX(), pLoopPlot->getY(), pUnit->getX(), pUnit->getY());
								LogTacticalMessage(strLogString);
							}
							break;
						}
					}

					//nothing found in neighborhood? move to closest city
					if (!bMoveMade)
					{
						CvCity* pTargetCity = m_pPlayer->GetClosestCity(pUnit->plot(),INT_MAX,true);
						if (pTargetCity)
						{
							MoveToEmptySpaceNearTarget(pUnit, pTargetCity->plot(), DOMAIN_SEA, 12);

							if (GC.getLogging() && GC.getAILogging())
							{
								CvString strLogString;
								strLogString.Format("%s cannot move to empty space to avoid being in the way,  moving to city instead (RepositionMoves), X: %d, Y: %d, Current X: %d, Current Y: %d", strTemp.GetCString(),
									pTargetCity->getX(), pTargetCity->getY(), pUnit->getX(), pUnit->getY());
								LogTacticalMessage(strLogString);
							}
						}
					}
				}
				if(bMoveMade)
				{
					//the new homeland patrol moves can handle naval units also, but the simple logic above seems good enough, so end their turn
					pUnit->PushMission(CvTypes::getMISSION_SKIP());
					pUnit->finishMoves();
					UnitProcessed(m_CurrentMoveUnits[iI].GetID(), pUnit->IsCombatUnit());
				}
			}
#endif
		}
	}
}

/// Moves units to the hex with the lowest danger
void CvTacticalAI::ExecuteMovesToSafestPlot()
{
	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if(pUnit && pUnit->canMove())
		{
			//see if we can do damage before retreating
			if (pUnit->canMoveAfterAttacking() && pUnit->getMoves()>1 && pUnit->canRangeStrike())
				TacticalAIHelpers::PerformRangedAttackWithoutMoving(pUnit);

			//so easy
			CvPlot* pBestPlot = TacticalAIHelpers::FindSafestPlotInReach(pUnit,true);
			if(pBestPlot != NULL)
			{
				// Move to the lowest danger value found
				pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pBestPlot->getX(), pBestPlot->getY(), CvUnit::MOVEFLAG_IGNORE_DANGER);

				//see if we can do damage after retreating
				if (pUnit->canMove() && pUnit->canRangeStrike())
					TacticalAIHelpers::PerformRangedAttackWithoutMoving(pUnit);

				pUnit->finishMoves();
				UnitProcessed(pUnit->GetID(), pUnit->IsCombatUnit());

				if(GC.getLogging() && GC.getAILogging())
				{
					CvString strLogString;
					CvString strTemp;
					strTemp = GC.getUnitInfo(pUnit->getUnitType())->GetDescription();
					strLogString.Format("Moving %s to safety, To X: %d, Y: %d, From X: %d, Y: %d", strTemp.GetCString(), pBestPlot->getX(), pBestPlot->getY(), pUnit->getX(), pUnit->getY());
					LogTacticalMessage(strLogString);
				}
			}
			else
			{
				if(GC.getLogging() && GC.getAILogging())
				{
					CvString strLogString;
					CvString strTemp;
					strTemp = GC.getUnitInfo(pUnit->getUnitType())->GetDescription();
					strLogString.Format("Failed to find destination moving %s to safety from, X: %d, Y: %d", strTemp.GetCString(), pUnit->getX(), pUnit->getY());
					LogTacticalMessage(strLogString);
				}

#if defined(MOD_BALANCE_CORE)
				//not in an army and not at home? we really need to do something
				if(pUnit->getArmyID() == -1 && pUnit->plot()->getOwner() != pUnit->getOwner())
				{
					CvCity* pClosestCity = m_pPlayer->GetClosestCityByEstimatedTurns(pUnit->plot());
					CvPlot* pMovePlot = pClosestCity ? pClosestCity->plot() : NULL;
					if(pMovePlot != NULL)
					{
						MoveToEmptySpaceNearTarget(pUnit,pMovePlot,DOMAIN_LAND,42);
						pUnit->SetTurnProcessed(true);
						CvString strTemp;
						CvUnitEntry* pkUnitInfo = GC.getUnitInfo(pUnit->getUnitType());
						if(pkUnitInfo)
						{
							strTemp = pkUnitInfo->GetDescription();
							CvString strLogString;
							strLogString.Format("Fleeing %s wandering home, at X: %d, Y: %d", strTemp.GetCString(), pUnit->getX(), pUnit->getY());
							LogTacticalMessage(strLogString);
						}
					}
					else
					{
						pUnit->PushMission(CvTypes::getMISSION_SKIP());
						pUnit->SetTurnProcessed(true);
						CvString strTemp;
						CvUnitEntry* pkUnitInfo = GC.getUnitInfo(pUnit->getUnitType());
						if(pkUnitInfo)
						{
							strTemp = pkUnitInfo->GetDescription();
							CvString strLogString;
							strLogString.Format("Fleeing %s stuck, at X: %d, Y: %d", strTemp.GetCString(), pUnit->getX(), pUnit->getY());
							LogTacticalMessage(strLogString);
						}
					}
				}
#endif
			}
		}
	}
}

/// Heal chosen units
void CvTacticalAI::ExecuteHeals()
{
	std::vector<int> killedUnits;
	for (std::set<int>::iterator it=m_HealingUnits.begin(); it!=m_HealingUnits.end(); ++it)
	{
		CvUnit* pUnit = m_pPlayer->getUnit( *it );
		CvPlot* pBetterPlot = NULL;
		if(pUnit)
		{
			//need to split from army?
			if (pUnit->getArmyID() != -1)
			{
				CvArmyAI* pArmy = m_pPlayer->getArmyAI(pUnit->getArmyID());
				if (pArmy)
				{
					if (pArmy->GetArmyAIState() != ARMYAISTATE_WAITING_FOR_UNITS_TO_REINFORCE)
					{
						pArmy->RemoveUnit(pUnit->GetID());
					}
				}
			}

			//find a suitable spot for healing
			if (pUnit->getDomainType()==DOMAIN_LAND)
			{
				//land unit on land should move away from danger
				if (m_pPlayer->GetPlotDanger(*pUnit->plot(),pUnit)>0 && !pUnit->isEmbarked() && !pUnit->IsGarrisoned())
				{
					//unless we can eliminate the danger!
					bool bFlee = true;
					std::vector<CvUnit*> vAttackers = m_pPlayer->GetPossibleAttackers(*pUnit->plot());
					if (vAttackers.size()==1)
					{
						//try to turn the tables on him
						if (TacticalAIHelpers::KillUnitIfPossible(pUnit, vAttackers[0]))
						{
							bFlee = false;
							if(GC.getLogging() && GC.getAILogging())
							{
								CvString strLogString;
								strLogString.Format("Healing unit %s (%d) counterattacked pursuer at X: %d, Y: %d", 
									pUnit->getName().GetCString(), pUnit->GetID(), vAttackers[0]->getX(), vAttackers[0]->getY());
								LogTacticalMessage(strLogString);
							}
						}
					}

					if (bFlee)
					{
						pBetterPlot = TacticalAIHelpers::FindSafestPlotInReach(pUnit,true);
						if (pBetterPlot)
							ExecuteMoveToPlotIgnoreDanger( pUnit, pBetterPlot );
					}
				}
				else if (pUnit->isEmbarked() || !pUnit->canHeal(pUnit->plot()))
				{
					//need to find land to heal
					pBetterPlot = TacticalAIHelpers::FindClosestSafePlotForHealing(pUnit,false);
					if (!pBetterPlot)
						pBetterPlot = TacticalAIHelpers::FindSafestPlotInReach(pUnit,false);
					if (!pBetterPlot || pBetterPlot==pUnit->plot())
					{
						if (pUnit->canHeal(pUnit->plot()))
							//try to heal in spite of danger
							pUnit->PushMission(CvTypes::getMISSION_HEAL());
						else
							pUnit->PushMission(CvTypes::getMISSION_SKIP());
					}
					else
						ExecuteMoveToPlotIgnoreDanger( pUnit, pBetterPlot );
				}
				else if (pUnit->canFortify(pUnit->plot()))
				{
					pUnit->PushMission(CvTypes::getMISSION_FORTIFY());
				}
				else if (pUnit->canHeal(pUnit->plot()))
				{
					pUnit->PushMission(CvTypes::getMISSION_HEAL());
				}
				else
				{
					//good old skip is our last resort
					pUnit->PushMission(CvTypes::getMISSION_SKIP());
				}
			}
			else if (pUnit->getDomainType()==DOMAIN_SEA)
			{
				if (m_pPlayer->GetPlotDanger(*pUnit->plot(),pUnit)>0 || !pUnit->canHeal(pUnit->plot()))
				{
					//find a friendly sea tile
					pBetterPlot = TacticalAIHelpers::FindClosestSafePlotForHealing(pUnit,true);
					if (!pBetterPlot)
						pBetterPlot = TacticalAIHelpers::FindSafestPlotInReach(pUnit,false);
					if (!pBetterPlot || pBetterPlot==pUnit->plot())
					{
						if (pUnit->canHeal(pUnit->plot()))
							pUnit->PushMission(CvTypes::getMISSION_HEAL());
						else
							pUnit->PushMission(CvTypes::getMISSION_SKIP());
					}
					else
						ExecuteMoveToPlotIgnoreDanger( pUnit, pBetterPlot );
				}
				else if (pUnit->canHeal(pUnit->plot()))
				{
					pUnit->PushMission(CvTypes::getMISSION_HEAL());
				}
				else
				{
					pUnit->PushMission(CvTypes::getMISSION_SKIP());
				}
			}
			else if (pUnit->getDomainType()==DOMAIN_AIR)
			{
				//nothing to do here. just stay in base
				pUnit->PushMission(CvTypes::getMISSION_SKIP());
			}

			pUnit->setTacticalMove((TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_HEAL]);
			UnitProcessed( *it );
		}
		else
		{
			//unit does not seem to exist anymore
			killedUnits.push_back( *it );
		}
	}

	//erase the zombies
	for (std::vector<int>::iterator i2=killedUnits.begin(); i2!=killedUnits.end(); ++i2)
		m_HealingUnits.erase(*i2);
}

/// Move barbarian to a new location
void CvTacticalAI::ExecuteBarbarianMoves(bool bAggressive)
{
	CvPlot* pBestPlot = NULL;
	CvString strTemp;

	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if(pUnit)
		{
			if(pUnit->isBarbarian())
			{
				strTemp = pUnit->getUnitInfo().GetDescription();

				// LAND MOVES
				if(pUnit->getDomainType() == DOMAIN_LAND)
				{
					AI_PERF_FORMAT("AI-perf-tact.csv", ("Barb Land Move, Turn %03d, %s", GC.getGame().getElapsedGameTurns(), m_pPlayer->getCivilizationShortDescription()) );

#if defined(MOD_BALANCE_CORE)
					CvPlot* pPlot = pUnit->plot();
					if(pPlot && (pPlot->getImprovementType() == GC.getBARBARIAN_CAMP_IMPROVEMENT() || pPlot->isCity()))
					{
						pUnit->setTacticalMove((TacticalAIMoveTypes)AI_TACTICAL_BARBARIAN_CAMP_DEFENSE);
						if (pUnit->canFortify(pPlot))
							pUnit->PushMission(CvTypes::getMISSION_FORTIFY());
						else
							pUnit->PushMission(CvTypes::getMISSION_SKIP());
						UnitProcessed(pUnit->GetID());
						if(GC.getLogging() && GC.getAILogging())
						{
							CvString strLogString;
							strLogString.Format("Pinning %s to camp/city at X: %d, Y: %d", pUnit->getName().GetCString(), pUnit->getX(), pUnit->getY());
							LogTacticalMessage(strLogString);
						}
						continue;
					}
#endif

#if defined(AUI_TACTICAL_EXECUTE_BARBARIAN_MOVES_CIVILIANS_MOVE_PASSIVELY)
					if(bAggressive && pUnit->IsCanDefend() && pUnit->GetCurrHitPoints()>pUnit->GetDanger())
#else
					if(bAggressive)
#endif
					{
						pBestPlot = FindBestBarbarianLandMove(pUnit);
					}
					else
					{
						pBestPlot = FindPassiveBarbarianLandMove(pUnit);
					}

					if(pBestPlot && MoveToEmptySpaceNearTarget(pUnit,pBestPlot,DOMAIN_LAND,12))
					{
#if defined(MOD_BALANCE_CORE)
						TacticalAIHelpers::PerformOpportunityAttack(pUnit,pBestPlot);
						if(pUnit->getMoves() > 0 && pUnit->canPillage(pUnit->plot()))
						{
							pUnit->PushMission(CvTypes::getMISSION_PILLAGE());
							if(GC.getLogging() && GC.getAILogging())
							{
								CvString strMsg;
								strMsg.Format("Pillaging during a barbarian move order. %s, X: %d, Y: %d", pUnit->getName().GetCString(),
									pUnit->plot()->getX(), pUnit->plot()->getY());
								LogTacticalMessage(strMsg, false);
							}
						}
#endif
						pUnit->finishMoves();
						UnitProcessed(m_CurrentMoveUnits[iI].GetID());
						if(GC.getLogging() && GC.getAILogging())
						{
							CvString strLogString;
							strLogString.Format("%s moved to empty space near target, X: %d, Y: %d, Current X: %d, Current Y: %d", strTemp.GetCString(),
							                    pBestPlot->getX(), pBestPlot->getY(), pUnit->getX(), pUnit->getY());
							LogTacticalMessage(strLogString);
						}
					}
					else
					{
						pUnit->finishMoves();
						UnitProcessed(m_CurrentMoveUnits[iI].GetID());
						if(GC.getLogging() && GC.getAILogging())
						{
							CvString strLogString;
							strLogString.Format("No target for %s at position, Current X: %d, Current Y: %d", strTemp.GetCString(), pUnit->getX(), pUnit->getY());
							LogTacticalMessage(strLogString);
						}
					}
				}

				// NAVAL MOVES
				else
				{

					AI_PERF_FORMAT("AI-perf-tact.csv", ("Barb Naval Move, Turn %03d, %s", GC.getGame().getElapsedGameTurns(), m_pPlayer->getCivilizationShortDescription()) );

					pBestPlot = FindBestBarbarianSeaMove(pUnit);

					if(pBestPlot && MoveToEmptySpaceNearTarget(pUnit,pBestPlot,DOMAIN_SEA,12))
					{
						TacticalAIHelpers::PerformOpportunityAttack(pUnit,pBestPlot);

						pUnit->finishMoves();
						UnitProcessed(m_CurrentMoveUnits[iI].GetID());

						if(GC.getLogging() && GC.getAILogging())
						{
							CvString strLogString;
							strLogString.Format("Moving %s to new position, X: %d, Y: %d, Current X: %d, Current Y: %d", strTemp.GetCString(),
							                    pBestPlot->getX(), pBestPlot->getY(), pUnit->getX(), pUnit->getY());
							LogTacticalMessage(strLogString);
						}
					}
					else
					{
						pUnit->finishMoves();
						UnitProcessed(m_CurrentMoveUnits[iI].GetID());

						if(GC.getLogging() && GC.getAILogging())
						{
							CvString strLogString;
							strLogString.Format("No target for %s at position, Current X: %d, Current Y: %d", strTemp.GetCString(), pUnit->getX(), pUnit->getY());
							LogTacticalMessage(strLogString);
						}
					}
				}
			}
		}
	}
}

/// Move Barbarian civilian to a camp (with escort if possible)
void CvTacticalAI::ExecuteBarbarianCivilianEscortMove()
{
	CvUnit* pCivilian = NULL;
	CvUnit* pEscort = NULL;
	CvPlot* pTarget = NULL;
	CvPlot* pCurrent = NULL;
	CvPlot* pCivilianMove = NULL;
	CvPlot* pEscortMove = NULL;
	CvUnit* pLoopUnit = NULL;

	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		pCivilian = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if(pCivilian)
		{
			//for the barbarian player AI_TACTICAL_TARGET_BARBARIAN_CAMP does not automatically mean the camp is empty of _barbarian_ defenders (check is only for enemy units)
			pTarget = FindNearbyTarget(pCivilian, 23, AI_TACTICAL_TARGET_BARBARIAN_CAMP);
			if(pTarget)
			{
				// If we're not there yet, we have work to do
				pCurrent = pCivilian->plot();
				if(pCurrent == pTarget)
				{
					pCivilian->finishMoves();
					UnitProcessed(pCivilian->GetID());
				}
				else
				{
#if defined(MOD_BALANCE_CORE)
					int iLoop;
					int iBestDistance = MAX_INT;
					ImprovementTypes eCamp = (ImprovementTypes)GC.getBARBARIAN_CAMP_IMPROVEMENT();
					for(pLoopUnit = GET_PLAYER(BARBARIAN_PLAYER).firstUnit(&iLoop); pLoopUnit; pLoopUnit = m_pPlayer->nextUnit(&iLoop))
					{
						if(!pLoopUnit || pLoopUnit->IsCivilianUnit())
							continue;

						if(pLoopUnit->getDomainType() == pCivilian->getDomainType() || pLoopUnit->getArea() != pCivilian->getArea())
							continue;

						if(pLoopUnit->plot()->getImprovementType()==eCamp)
							continue;

						if(pLoopUnit->plot() == pCivilian->plot())
						{
							pEscort = pLoopUnit;
							break;
						}
						else
						{
							int iDistance = plotDistance(pLoopUnit->getX(), pLoopUnit->getY(), pCivilian->getX(), pCivilian->getY());
							if(iDistance < iBestDistance)
							{
								pEscort = pLoopUnit;
								iBestDistance = iDistance;
							}
						}
					}
#else

					if(pCurrent->getNumUnits() > 1)
					{
						for(int iJ = 0; iJ < pCurrent->getNumUnits(); iJ++)
						{
							pLoopUnit = pCurrent->getUnitByIndex(iJ);
							if(pLoopUnit->GetID() != pCivilian->GetID() &&
							        pLoopUnit->getOwner() == pCivilian->getOwner())
							{
								pEscort = CvUnit*(pLoopUnit);
								break;
							}
						}
					}
#endif

					// Look at where we'd move this turn taking units into consideration
					int iFlags = 0;
					if(pEscort)
					{
						iFlags = CvUnit::MOVEFLAG_IGNORE_DANGER;
					}

					// Handle case of no path found at all for civilian
					if(!pCivilian->GeneratePath(pTarget, iFlags))
					{
#if defined(MOD_BALANCE_CORE)
						UnitProcessed(pCivilian->GetID());
#endif
						pCivilian->finishMoves();
						if(pEscort)
						{
#if defined(MOD_BALANCE_CORE)
							ExecuteMoveToPlotIgnoreDanger(pEscort, pCivilian->plot());
							UnitProcessed(pEscort->GetID());
#endif
							pEscort->finishMoves();
						}
						if(GC.getLogging() && GC.getAILogging())
						{
							CvString strLogString;
							strLogString.Format("Civilian cannot reach target, X: %d, Y: %d", pTarget->getX(), pTarget->getY());
							LogTacticalMessage(strLogString);
						}
					}

					else
					{
						pCivilianMove = pCivilian->GetPathEndFirstTurnPlot();

						// Can we reach our target this turn?
						if(pCivilianMove == pTarget)
						{
							// See which defender is stronger
							CvUnit* pCampDefender = pCivilianMove->getBestDefender(m_pPlayer->GetID());
							if(!pCampDefender || (pEscort && pEscort->GetPower() > pCampDefender->GetPower()))
							{
								if(pEscort && (!pCampDefender || ExecuteMoveOfBlockingUnit(pCampDefender)))
								{
									ExecuteMoveToPlotIgnoreDanger(pEscort, pCivilianMove);
									ExecuteMoveToPlotIgnoreDanger(pCivilian, pCivilianMove);
									if(GC.getLogging() && GC.getAILogging())
									{
										CvString strTemp;
										CvString strLogString;
										strTemp = pEscort->getUnitInfo().GetDescription();
										strLogString.Format("Moving escorting %s to take over camp defense, X: %d, Y: %d", strTemp.GetCString(), pEscort->getX(), pEscort->getY());
										LogTacticalMessage(strLogString);
										strTemp = pCivilian->getUnitInfo().GetDescription();
										strLogString.Format("Moving %s to camp, X: %d, Y: %d", strTemp.GetCString(), pCivilian->getX(), pCivilian->getY());
										LogTacticalMessage(strLogString);
									}
								}
								else
								{
									ExecuteMoveToPlotIgnoreDanger(pCivilian, pCivilianMove);
									if(GC.getLogging() && GC.getAILogging())
									{
										CvString strTemp;
										CvString strLogString;
										strTemp = pCivilian->getUnitInfo().GetDescription();
										strLogString.Format("Moving %s to camp, X: %d, Y: %d", strTemp.GetCString(), pCivilian->getX(), pCivilian->getY());
										LogTacticalMessage(strLogString);
									}
								}
							}
							else
							{
								ExecuteMoveToPlotIgnoreDanger(pCivilian, pCivilianMove);
								if(GC.getLogging() && GC.getAILogging())
								{
									CvString strTemp;
									CvString strLogString;
									strTemp = pCivilian->getUnitInfo().GetDescription();
									strLogString.Format("Moving %s to camp, X: %d, Y: %d", strTemp.GetCString(), pCivilian->getX(), pCivilian->getY());
									LogTacticalMessage(strLogString);
								}
							}
						}

						// Can't reach target and don't have escort...
						else if(!pEscort)
						{
							ExecuteMoveToPlotIgnoreDanger(pCivilian, pCivilianMove);
							if(GC.getLogging() && GC.getAILogging())
							{
								CvString strTemp;
								CvString strLogString;
								strTemp = pCivilian->getUnitInfo().GetDescription();
								strLogString.Format("Moving %s without escort to target, X: %d, Y: %d", strTemp.GetCString(), pCivilian->plot()->getX(), pCivilian->plot()->getY());
								LogTacticalMessage(strLogString);
							}
						}

						// Can't reach target and DO have escort...
						else
						{
							// See if escort can move to the same location in one turn
							if(pEscort->TurnsToReachTarget(pCivilianMove) <= 1)
							{
								ExecuteMoveToPlotIgnoreDanger(pEscort, pCivilianMove);
								ExecuteMoveToPlotIgnoreDanger(pCivilian, pCivilianMove);
								if(GC.getLogging() && GC.getAILogging())
								{
									CvString strTemp;
									CvString strLogString;
									strTemp = pEscort->getUnitInfo().GetDescription();
									strLogString.Format("Moving escorting %s to target, X: %d, Y: %d", strTemp.GetCString(), pEscort->getX(), pEscort->getY());
									LogTacticalMessage(strLogString);
									strTemp = pCivilian->getUnitInfo().GetDescription();
									strLogString.Format("Moving %s to target, X: %d, Y: %d", strTemp.GetCString(), pCivilian->getX(), pCivilian->getY());
									LogTacticalMessage(strLogString);
								}
							}

							else
							{
								CvUnit* pBlockingUnit = pCivilianMove->getBestDefender(m_pPlayer->GetID());

								// See if friendly blocking unit is ending the turn there, or if no blocking unit (which indicates this is somewhere civilian
								// can move that escort can't), then find a new path based on moving the escort
								if(!pBlockingUnit || pBlockingUnit->getMoves() == 0)
								{
									if(!pEscort->GeneratePath(pTarget))
									{
										pCivilian->finishMoves();
										pEscort->finishMoves();
										if(GC.getLogging() && GC.getAILogging())
										{
											CvString strLogString;
											strLogString.Format("Escort cannot move with civilian, X: %d, Y: %d", pTarget->getX(), pTarget->getY());
											LogTacticalMessage(strLogString);
										}
									}
									else
									{
										pEscortMove = pCivilian->GetPathEndFirstTurnPlot();

										// See if civilian can move to the same location in one turn
										if(pCivilian->TurnsToReachTarget(pEscortMove) <= 1)
										{
											ExecuteMoveToPlotIgnoreDanger(pEscort, pEscortMove);
											ExecuteMoveToPlotIgnoreDanger(pCivilian, pEscortMove);
											if(GC.getLogging() && GC.getAILogging())
											{
												CvString strTemp;
												CvString strLogString;
												strTemp = pEscort->getUnitInfo().GetDescription();
												strLogString.Format("Moving escorting %s to target, X: %d, Y: %d", strTemp.GetCString(), pEscort->getX(), pEscort->getY());
												LogTacticalMessage(strLogString);
												strTemp = pCivilian->getUnitInfo().GetDescription();
												strLogString.Format("Moving %s to target, X: %d, Y: %d", strTemp.GetCString(), pCivilian->getX(), pCivilian->getY());
												LogTacticalMessage(strLogString);
											}
										}
										else
										{
											pCivilian->finishMoves();
											pEscort->finishMoves();
											if(GC.getLogging() && GC.getAILogging())
											{
												CvString strLogString;
												strLogString.Format("Civilian cannot move with escort. Too many blocking units.");
												LogTacticalMessage(strLogString);
											}
										}
									}
								}

								// Looks like we should be able to move the blocking unit out of the way
								else
								{
									if(ExecuteMoveOfBlockingUnit(pBlockingUnit))
									{
										ExecuteMoveToPlotIgnoreDanger(pEscort, pCivilianMove);
										ExecuteMoveToPlotIgnoreDanger(pCivilian, pCivilianMove);
										if(GC.getLogging() && GC.getAILogging())
										{
											CvString strTemp;
											CvString strLogString;
											strTemp = pEscort->getUnitInfo().GetDescription();
											strLogString.Format("Moving escorting %s to target, X: %d, Y: %d", strTemp.GetCString(), pEscort->getX(), pEscort->getY());
											LogTacticalMessage(strLogString);
											strTemp = pCivilian->getUnitInfo().GetDescription();
											strLogString.Format("Moving %s to target, X: %d, Y: %d", strTemp.GetCString(), pCivilian->getX(), pCivilian->getY());
											LogTacticalMessage(strLogString);
										}
									}
									else
									{
										pCivilian->finishMoves();
										pEscort->finishMoves();
										if(GC.getLogging() && GC.getAILogging())
										{
											CvString strLogString;
											strLogString.Format("Could not move blocking unit for escorted civilian.");
											LogTacticalMessage(strLogString);
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
}

/// Move unit to a specific tile (retrieve unit from first entry in m_CurrentMoveUnits)
void CvTacticalAI::ExecuteMoveToPlotIgnoreDanger(CvPlot* pTarget, bool bSaveMoves)
{
	// Move first one to target
	CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[0].GetID());
	if(pUnit)
	{
		ExecuteMoveToPlotIgnoreDanger(pUnit, pTarget, bSaveMoves);
	}
}

/// Move unit to a specific tile (unit passed explicitly)
void CvTacticalAI::ExecuteMoveToPlotIgnoreDanger(CvUnit* pUnit, CvPlot* pTarget, bool bSaveMoves)
{
	if(!pUnit || !pTarget)
		return;

	// Unit already at target plot?
	if(pTarget == pUnit->plot())
	{
		// Fortify if possible
		if (!TacticalAIHelpers::PerformRangedAttackWithoutMoving(pUnit))
		{
			if( pUnit->canFortify(pUnit->plot()))
			{
				pUnit->PushMission(CvTypes::getMISSION_FORTIFY());
				pUnit->SetFortifiedThisTurn(true);
			}
			else
				pUnit->PushMission(CvTypes::getMISSION_SKIP());
		}
		else
			pUnit->PushMission(CvTypes::getMISSION_SKIP());

		//don't call finish moves, otherwise we won't heal!
	}
	else
	{
		pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pTarget->getX(), pTarget->getY(), CvUnit::MOVEFLAG_IGNORE_DANGER);

		if(!bSaveMoves)
		{
			TacticalAIHelpers::PerformRangedAttackWithoutMoving(pUnit);
			pUnit->finishMoves();
		}
	}

	UnitProcessed(pUnit->GetID(), pUnit->IsCombatUnit());
}
/// Find an adjacent hex to move a blocking unit to
bool CvTacticalAI::ExecuteMoveOfBlockingUnit(CvUnit* pBlockingUnit, CvPlot* pPreferredDirection)
{
	if(!pBlockingUnit->canMove())
	{
		return false;
	}

	CvPlot* pOldPlot = pBlockingUnit->plot();

	std::vector<SPlotWithScore> vCandidates;

	for(int iI = 0; iI < NUM_DIRECTION_TYPES; iI++)
	{
		CvPlot* pPlot = plotDirection(pBlockingUnit->getX(), pBlockingUnit->getY(), ((DirectionTypes)iI));
		if(pPlot != NULL)
		{
			if (pPreferredDirection)
				vCandidates.push_back( SPlotWithScore(pPlot,plotDistance(pPreferredDirection->getX(),pPreferredDirection->getY(),pPlot->getX(),pPlot->getY())) );
			else
				vCandidates.push_back( SPlotWithScore(pPlot,0) );
		}
	}

	std::stable_sort(vCandidates.begin(),vCandidates.end());

	for (std::vector<SPlotWithScore>::iterator it=vCandidates.begin(); it!=vCandidates.end(); ++it)
	{
		CvPlot* pPlot = it->pPlot;

		// Don't embark for one of these moves
		if (!pOldPlot->isWater() && pPlot->isWater() && pBlockingUnit->getDomainType() == DOMAIN_LAND)
		{
			continue;
		}

		// Has to be somewhere we can move and be empty of other units/enemy cities
		if(!pPlot->getVisibleEnemyDefender(m_pPlayer->GetID()) && !pPlot->isEnemyCity(*pBlockingUnit) && pBlockingUnit->GeneratePath(pPlot))
		{
			ExecuteMoveToPlotIgnoreDanger(pBlockingUnit, pPlot);
			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strTemp, strLogString;
				strTemp = pBlockingUnit->getUnitInfo().GetDescription();
				strLogString.Format("Moving blocking %s out of way, Leaving X: %d, Y: %d, Now At X: %d, Y: %d", strTemp.GetCString(), pOldPlot->getX(), pOldPlot->getY(), pBlockingUnit->getX(), pBlockingUnit->getY());
				LogTacticalMessage(strLogString);
			}
			return true;
		}
	}
	return false;
}

/// Move unit to protect a specific tile
void CvTacticalAI::ExecuteNavalBlockadeMove(CvPlot* pTarget)
{
	// Move first one to target

	CvUnit* pUnit = NULL;

	if (m_CurrentMoveHighPriorityUnits.size() > 0 && m_CurrentMoveUnits.size() > 0)
	{
		if (m_CurrentMoveUnits.begin()->GetMovesToTarget() < m_CurrentMoveHighPriorityUnits.begin()->GetMovesToTarget())
			pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits.begin()->GetID());
		else
			pUnit = m_pPlayer->getUnit(m_CurrentMoveHighPriorityUnits.begin()->GetID());
	}
	else if (m_CurrentMoveHighPriorityUnits.size() > 0)
	{
		pUnit = m_pPlayer->getUnit(m_CurrentMoveHighPriorityUnits.begin()->GetID());
	}
	else if (m_CurrentMoveUnits.size() > 0)
	{
		pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits.begin()->GetID());
	}

	if (pUnit)
	{
		pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pTarget->getX(), pTarget->getY(), CvUnit::MOVEFLAG_APPROX_TARGET_RING1);
		if (pUnit->IsCanAttackRanged())
		{
			TacticalAIHelpers::PerformRangedAttackWithoutMoving(pUnit);
		}
		else
		{
			CvUnit *pEnemy = pUnit->plot()->GetAdjacentEnemyUnit(pUnit->getTeam(), pUnit->getDomainType());
			if (pEnemy && TacticalAIHelpers::IsAttackNetPositive(pUnit, pEnemy->plot()))
			{
				if (GC.getLogging() && GC.getAILogging())
				{
					CvString strMsg;
					strMsg.Format("In position for blockade %d, %d, but %s found an enemy unit to attack at X: %d, Y: %d",
						pUnit->plot()->getX(), pUnit->plot()->getY(), pUnit->getName().GetCString(), pEnemy->plot()->getX(), pEnemy->plot()->getY());
					LogTacticalMessage(strMsg);
				}
				TacticalAIHelpers::PerformOpportunityAttack(pUnit, pEnemy->plot());
			}
		}	
		pUnit->pillage();
		pUnit->finishMoves();
		UnitProcessed(m_CurrentMoveUnits[0].GetID());
		pUnit->SetTacticalAIPlot(NULL);
	}
}

/// Find one unit to move to target, starting with high priority list
void CvTacticalAI::ExecuteMoveToTarget(CvPlot* pTarget, bool bSaveMoves)
{
	CvUnit* pUnit = NULL;

	if (m_CurrentMoveHighPriorityUnits.size() > 0 && m_CurrentMoveUnits.size() > 0)
	{
		if (m_CurrentMoveUnits.begin()->GetMovesToTarget() < m_CurrentMoveHighPriorityUnits.begin()->GetMovesToTarget())
			pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits.begin()->GetID());
		else
			pUnit = m_pPlayer->getUnit(m_CurrentMoveHighPriorityUnits.begin()->GetID());
	}
	else if (m_CurrentMoveHighPriorityUnits.size() > 0)
	{
		pUnit = m_pPlayer->getUnit(m_CurrentMoveHighPriorityUnits.begin()->GetID());
	}
	else if (m_CurrentMoveUnits.size() > 0)
	{
		pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits.begin()->GetID());
	}

	if (pUnit)
		ExecuteMoveToPlotIgnoreDanger(pUnit,pTarget,bSaveMoves);
}

/// Set up fighters to intercept enemy air units
void CvTacticalAI::ExecuteAirInterceptMoves()
{
	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if(pUnit)
		{
			if(pUnit->canAirPatrol(NULL))
			{
				pUnit->PushMission(CvTypes::getMISSION_AIRPATROL());
				UnitProcessed(m_CurrentMoveUnits[iI].GetID(), false /*bMarkTacticalMap*/);
			}
		}
	}
}

/// Set up fighters to air sweep to suppress enemy air units/AA
void CvTacticalAI::ExecuteAirSweepMoves()
{
	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if(pUnit)
		{
			if(pUnit->canAirSweep())
			{
				CvPlot *pTarget = m_pPlayer->GetMilitaryAI()->GetBestAirSweepTarget(pUnit);
				if (pTarget)
				{
					pUnit->PushMission(CvTypes::getMISSION_AIR_SWEEP(), pTarget->getX(), pTarget->getY());
					UnitProcessed(m_CurrentMoveUnits[iI].GetID(), false /*bMarkTacticalMap*/);
				}
			}
		}
	}
}

/// Bombard enemy units from plots they can't reach (return true if some attack made)
bool CvTacticalAI::ExecuteSafeBombards(CvTacticalTarget& kTarget)
{
	CvTacticalUnit unit;
	CvUnit* pDefender;
	CvCity* pTargetCity = 0;
	int iRequiredDamage = 0;

	CvPlot* pTargetPlot = GC.getMap().plot(kTarget.GetTargetX(), kTarget.GetTargetY());

	// Get required damage on unit target
	pDefender = pTargetPlot->getVisibleEnemyDefender(m_pPlayer->GetID());
	if(pDefender)
	{
		iRequiredDamage = pDefender->GetCurrHitPoints();
		if(iRequiredDamage <= 0)
		{
			return false;
		}

		// If this is a unit target we might also be able to hit it with a city
		bool bCityCanAttack = FindCitiesWithinStrikingDistance(pTargetPlot);
		if(bCityCanAttack)
		{
			ComputeTotalExpectedCityBombardDamage(pDefender);

			// Start by applying damage from city bombards
			for(unsigned int iI = 0; iI < m_CurrentMoveCities.size() && iRequiredDamage > 0; iI++)
			{
				CvCity* pCity = m_pPlayer->getCity(m_CurrentMoveCities[iI].GetID());
				if(pCity != NULL)
					PerformAttack(pCity, &kTarget);
			}

			// Need to keep hitting target?
			iRequiredDamage = pDefender->GetCurrHitPoints();
			if(iRequiredDamage <= 0)
			{
				return true;
			}
		}
	}

	// Get required damage on city target
	else
	{
		pTargetCity = pTargetPlot->getPlotCity();
		if(pTargetCity != NULL)
		{
			iRequiredDamage = pTargetCity->GetMaxHitPoints() - pTargetCity->getDamage();

			// Can't eliminate a city with ranged fire, so don't target one if that low on health
			if(iRequiredDamage <= 1)
			{
				return false;
			}
		}
	}

	m_CurrentMoveUnits.clear();

	//find out which of our units might be useful
	//have to do this in two steps - otherwise UnitProcessed below may invalidate our iterator
	std::list<int>::iterator it;
	for(it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); ++it)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if(pUnit && pUnit->IsCanAttackRanged() && !pUnit->isOutOfAttacks())
		{
			//do this only if the unit is not too far out
			if (plotDistance(pUnit->getX(),pUnit->getY(),pTargetPlot->getX(),pTargetPlot->getY())>15)
				continue;

			//if it's a fighter plane, don't use it here, we need it for interceptions etc
			if (pUnit->getUnitInfo().GetDefaultUnitAIType() == UNITAI_DEFENSE_AIR)
				continue;

			unit.SetID(*it);
			m_CurrentMoveUnits.push_back(unit);
		}
	}

	//see which unit would do how much damage and sort accordingly
	ComputeTotalExpectedDamage(&kTarget,pTargetPlot);

	//for each of our ranged units, see if they are already in or close to a plot that can bombard but can't be attacked.
	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if (pUnit)
		{
			//special handling for garrison
			if (pUnit->IsGarrisoned())
			{
				TacticalAIHelpers::PerformRangedAttackWithoutMoving(pUnit);
				continue;
			}

			//get plots that could be used as a base for attacking
			std::vector<CvPlot*> vAttackPlots;
			if (pUnit->getDomainType()==DOMAIN_AIR)
			{
				if (plotDistance(*pTargetPlot,*pUnit->plot())<=pUnit->GetRange())
					vAttackPlots.push_back(pUnit->plot());
			}
			else
				TacticalAIHelpers::GetPlotsForRangedAttack( pTargetPlot, pUnit, pUnit->GetRange(), false, vAttackPlots);

			//get plots we can move into with enough movement left
			int iMinMovesLeft = pUnit->isMustSetUpToRangedAttack() ? GC.getMOVE_DENOMINATOR()+1 : 1;
			ReachablePlots movePlots;
			TacticalAIHelpers::GetAllPlotsInReachThisTurn( pUnit, pUnit->plot(), movePlots, true, true, false, iMinMovesLeft);

			//check the overlap between attack plots and move plots
			std::set<int> candidates;
			for (std::vector<CvPlot*>::iterator it=vAttackPlots.begin(); it!=vAttackPlots.end(); ++it)
			{
				//can only do range attacks from native domain (no ships in harbor, embarked etc)
				if ( *it == NULL || !pUnit->isNativeDomain(*it) )
					continue;

				//must be halfway safe
				bool bIsSafeEnough = (pUnit->GetDanger(*it) < pUnit->GetCurrHitPoints()*0.8) || pUnit->isSuicide();
				if( bIsSafeEnough && IsExpectedToDamageWithRangedAttack(pUnit, pTargetPlot, 3))
					candidates.insert( (*it)->GetPlotIndex() );
			}

			for (ReachablePlots::iterator it=movePlots.begin(); it!=movePlots.end(); ++it)
			{
				//simple greedy algorithm. use first match, even if we are about to block another unit ...
				//todo: count possibilities per unit and start with the most constrained one
				if (candidates.find(it->iPlotIndex) != candidates.end())
				{
					CvPlot* pBasePlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);

					if (pUnit->getDomainType()!=DOMAIN_AIR)
					{
						//Check for presence of movable friendly units
						CvUnit* pBlockingUnit = pBasePlot->getBestDefender(m_pPlayer->GetID());
						if (pBlockingUnit && pBlockingUnit->getDomainType()==pUnit->getDomainType())
						{
							if (pBlockingUnit->IsCanAttackRanged())
								continue; //don't shuffle around other ranged units
							else if (ExecuteMoveOfBlockingUnit(pBlockingUnit,pTargetPlot))
								pBlockingUnit = NULL;
							else
								continue; //can't remove block
						}
						else
						{ 
							//any other unit there? can't use the plot then
							if (!pUnit->canMoveInto(*pBasePlot,CvUnit::MOVEFLAG_DESTINATION ))
								continue;
						}

						pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pBasePlot->getX(), pBasePlot->getY());
						if (pUnit->plot()!=pBasePlot)
							continue; //movement failed
					}

					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strMsg;
						strMsg.Format("Performing safe bombard with %s %d, Target X: %d, Y: %d, Bombard From X: %d, Y: %d", 
							pUnit->getName().GetCString(), pUnit->GetID(), kTarget.GetTargetX(), kTarget.GetTargetY(), pUnit->getX(), pUnit->getY());
						LogTacticalMessage(strMsg, false);
					}

					if(pUnit->canSetUpForRangedAttack(NULL))
						pUnit->setSetUpForRangedAttack(true);

					while (!pUnit->isOutOfAttacks() && pUnit->canMove() && iRequiredDamage>0)
					{
						pUnit->PushMission(CvTypes::getMISSION_RANGE_ATTACK(), pTargetPlot->getX(), pTargetPlot->getY());

						int iOldRequiredDamage = iRequiredDamage;
						if(pDefender)
							iRequiredDamage = pDefender->GetCurrHitPoints();
						else if (pTargetCity)
							iRequiredDamage = pTargetCity->GetMaxHitPoints() - pTargetCity->getDamage();

						//sanity check: if we're not doing any damage, bail out
						if (iRequiredDamage==iOldRequiredDamage)
							break;
					}

					//do not finish moves though, we might want to move out of the way for another unit!
					UnitProcessed(pUnit->GetID());

					//continue with next unit
					break;
				}
			}
		}
	}

	return true;
}

/// Take a multi-hex attack on an enemy unit this turn
bool CvTacticalAI::ExecuteFlankAttack(CvTacticalTarget& kTarget)
{
	CvTacticalAnalysisCell* pCell;
	CvPlot* pLoopPlot;
	int iPlotIndex;
	int iPossibleFlankHexes = 0;
	CvTacticalTarget target;

	// Count number of possible flank attack spaces around target
	for(int iI = 0; iI < NUM_DIRECTION_TYPES; iI++)
	{
		pLoopPlot = plotDirection(kTarget.GetTargetX(), kTarget.GetTargetY(), ((DirectionTypes)iI));
		if(pLoopPlot != NULL)
		{
			iPlotIndex = GC.getMap().plotNum(pLoopPlot->getX(), pLoopPlot->getY());
			pCell = GetTacticalAnalysisMap()->GetCell(iPlotIndex);
			if(pCell->IsHelpsProvidesFlankBonus())
			{
				iPossibleFlankHexes++;
			}
		}
	}

	// If more than 1, find how many we can fill with units
	if(iPossibleFlankHexes > 1)
	{
		int iFillableHexes = 0;
		int iNumAttackers = 0;
		m_TempTargets.clear();
		for(int iI = 0; iI < NUM_DIRECTION_TYPES; iI++)
		{
			pLoopPlot = plotDirection(kTarget.GetTargetX(), kTarget.GetTargetY(), ((DirectionTypes)iI));
			if(pLoopPlot != NULL)
			{
				iPlotIndex = GC.getMap().plotNum(pLoopPlot->getX(), pLoopPlot->getY());
				pCell = GetTacticalAnalysisMap()->GetCell(iPlotIndex);
				if(pCell->IsHelpsProvidesFlankBonus())
				{
#if defined(MOD_BALANCE_CORE_MILITARY)
					if(FindClosestUnit(pLoopPlot, 0, true/*bMustHaveHalfHP*/, false /*bMustBeRangedUnit*/, 0 /*iRangeRequired*/, false /*bNeedsIgnoreLOS*/, true /*bMustBeMeleeUnit*/, false, NULL, 1 /*iMaxUnits*/))
#else
					if(FindClosestUnit(pLoopPlot, 0, true/*bMustHaveHalfHP*/, false /*bMustBeRangedUnit*/, 0 /*iRangeRequired*/, false /*bNeedsIgnoreLOS*/, true /*bMustBeMeleeUnit*/))
#endif
					{
						iFillableHexes++;
						iNumAttackers++;
						target.SetTargetX(pLoopPlot->getX());
						target.SetTargetY(pLoopPlot->getY());
						m_TempTargets.push_back(target);
					}
					// What about next turn?
#if defined(MOD_BALANCE_CORE_MILITARY)
					else if (FindClosestUnit(pLoopPlot, 1, false/*bMustHaveHalfHP*/, false /*bMustBeRangedUnit*/, 0 /*iRangeRequired*/, false /*bNeedsIgnoreLOS*/, true /*bMustBeMeleeUnit*/, false, NULL, 1 /*iMaxUnits*/))
#else
					else if(FindClosestUnit(pLoopPlot, 1, false/*bMustHaveHalfHP*/, false /*bMustBeRangedUnit*/, 0 /*iRangeRequired*/, false /*bNeedsIgnoreLOS*/, true /*bMustBeMeleeUnit*/))
#endif
					{
						iFillableHexes++;
						target.SetTargetX(pLoopPlot->getX());
						target.SetTargetY(pLoopPlot->getY());
						m_TempTargets.push_back(target);
					}
				}
			}
		}

		// As long as we either get three hexes filled with one attacker, or two we can attack from, then this multi-hex attack is worth considering
		if((iNumAttackers >= 1 && iFillableHexes >= 3) || iNumAttackers >= 2)
		{
			// Compute best way to fill the hexes
			m_PotentialBlocks.clear();
			for(unsigned int iI = 0; iI < m_TempTargets.size(); iI++)
			{
				pLoopPlot = GC.getMap().plot(m_TempTargets[iI].GetTargetX(), m_TempTargets[iI].GetTargetY());
#if defined(MOD_BALANCE_CORE)
				if(!FindClosestUnit(pLoopPlot, 1, false/*bMustHaveHalfHP*/, false /*bMustBeRangedUnit*/, 0 /*iRangeRequired*/, false /*bNeedsIgnoreLOS*/, true /*bMustBeMeleeUnit*/, false, NULL, 4))
#else
				if(!FindClosestUnit(pLoopPlot, 1, false/*bMustHaveHalfHP*/, false /*bMustBeRangedUnit*/, 0 /*iRangeRequired*/, false /*bNeedsIgnoreLOS*/, true /*bMustBeMeleeUnit*/))
#endif
				{
					iFillableHexes--;
					if(iFillableHexes < 2)
					{
						return false;
					}
				}
				else
				{
					// Save off the units that could get here
					for(unsigned int jJ = 0; jJ < m_CurrentMoveUnits.size(); jJ++)
					{
						CvBlockingUnit block;
						block.SetUnitID(m_CurrentMoveUnits[jJ].GetID());
						block.SetPlot(pLoopPlot);
						block.SetNumChoices((int)m_CurrentMoveUnits.size());
						block.SetDistanceToTarget(m_CurrentMoveUnits[jJ].GetMovesToTarget());
						m_PotentialBlocks.push_back(block);
					}
				}
			}

			if(AssignFlankingUnits(iFillableHexes))
			{
				// Make moves up into hexes
				for(unsigned int iI = 0; iI < m_ChosenBlocks.size(); iI++)
				{
					CvUnit* pUnit = m_pPlayer->getUnit(m_ChosenBlocks[iI].GetUnitID());
					if(pUnit)
					{
						if(pUnit->plot() == m_ChosenBlocks[iI].GetPlot())
						{
							if(GC.getLogging() && GC.getAILogging())
							{
								CvString strMsg;
								strMsg.Format("Already in a flanking position with %s, X: %d, Y: %d", pUnit->getName().GetCString(),
								              m_ChosenBlocks[iI].GetPlot()->getX(), m_ChosenBlocks[iI].GetPlot()->getY());
								LogTacticalMessage(strMsg, false);
							}
						}
						else
						{
							pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), m_ChosenBlocks[iI].GetPlot()->getX(), m_ChosenBlocks[iI].GetPlot()->getY(), CvUnit::MOVEFLAG_IGNORE_DANGER);
							if(GC.getLogging() && GC.getAILogging())
							{
								CvString strMsg;
								strMsg.Format("Moving into a flanking position with %s, X: %d, Y: %d", pUnit->getName().GetCString(),
								              m_ChosenBlocks[iI].GetPlot()->getX(), m_ChosenBlocks[iI].GetPlot()->getY());
								LogTacticalMessage(strMsg, false);
							}

							if(pUnit->getMoves() <= 0)
							{
								UnitProcessed(pUnit->GetID());
							}
#if defined(MOD_BALANCE_CORE)
							if (pUnit->canPillage(pUnit->plot()) && pUnit->getDamage() > 0)
							{
								pUnit->PushMission(CvTypes::getMISSION_PILLAGE());
								if(GC.getLogging() && GC.getAILogging())
								{
									CvString strMsg;
									strMsg.Format("Pillaging during a flanking mission with %s, X: %d, Y: %d", pUnit->getName().GetCString(),
										pUnit->getX(), pUnit->getY());
									LogTacticalMessage(strMsg, false);
								}
							}
#endif
						}
					}
				}

				// Make attacks
				CvPlot* pTargetPlot = GC.getMap().plot(kTarget.GetTargetX(), kTarget.GetTargetY());
				CvUnit* pDefender = pTargetPlot->getVisibleEnemyDefender(m_pPlayer->GetID());
				if(pDefender)
				{
					kTarget.SetAuxIntData(pDefender->GetCurrHitPoints());
					m_CurrentMoveCities.clear();
					if(FindUnitsWithinStrikingDistance(pTargetPlot))
					{
						ComputeTotalExpectedDamage(&kTarget, pTargetPlot);
						ExecuteAttack(&kTarget, pTargetPlot, true /*bMustSurviveAttack*/);
					}
				}
			}
		}
	}

	return false;
}

/// Move forces in toward our target
void CvTacticalAI::ExecuteCloseOnTarget(CvTacticalTarget& kTarget, CvTacticalDominanceZone* pZone, bool bOffensive)
{
	CvOperationUnit unit;
	int iDistance;
	CvPlot* pTargetPlot;
	int iRangedUnits = 0;
	int iMeleeUnits = 0;
	int iGenerals = 0;
	list<int>::iterator it;
	int iTacticalRadius = GetTacticalAnalysisMap()->GetTacticalRange();

	pTargetPlot = GC.getMap().plot(kTarget.GetTargetX(), kTarget.GetTargetY());
	m_OperationUnits.clear();
	m_GeneralsToMove.clear();

	for(it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if(pUnit)
		{
			// If not naval invasion, proper domain of unit?
			if(pZone->IsNavalInvasion() ||
			        (pZone->IsWater() && pUnit->getDomainType() == DOMAIN_SEA || !pZone->IsWater() && pUnit->getDomainType() == DOMAIN_LAND))
			{	
				// Find units really close to target or somewhat close that just came out of an operation
				iDistance = plotDistance(pUnit->getX(), pUnit->getY(), kTarget.GetTargetX(), kTarget.GetTargetY());
				if(iDistance <= iTacticalRadius || (iDistance <= iTacticalRadius*2 && pUnit->IsRecentlyDeployedFromOperation()))
				{
					unit.SetUnitID(pUnit->GetID());

					if(pUnit->IsGreatGeneral() || pUnit->IsGreatAdmiral() || pUnit->IsCityAttackSupport())
					{
						unit.SetPosition((MultiunitPositionTypes)m_CachedInfoTypes[eMUPOSITION_CIVILIAN_SUPPORT]);
						iGenerals++;
						m_GeneralsToMove.push_back(unit);
					}
					else if(pUnit->IsCanAttackRanged() && pUnit->GetRange() > 1)
					{
						unit.SetPosition((MultiunitPositionTypes)m_CachedInfoTypes[eMUPOSITION_BOMBARD]);
						iRangedUnits++;
						m_OperationUnits.push_back(unit);
					}
					else
					{
						unit.SetPosition((MultiunitPositionTypes)m_CachedInfoTypes[eMUPOSITION_FRONT_LINE]);
						iMeleeUnits++;
						m_OperationUnits.push_back(unit);
					}
				}
			}
		}
	}

	// If have any units to move...
	if(m_OperationUnits.size() > 0)
	{
		if (bOffensive)
			ScoreCloseOnPlots(pTargetPlot);
		else
			ScoreHedgehogPlots(pTargetPlot);

		// Compute the moves to get the best deployment
		std::stable_sort(m_TempTargets.begin(), m_TempTargets.end());
		m_PotentialBlocks.clear();

		int iRangedUnitsToPlace = iRangedUnits;
		int iMeleeUnitsToPlace = iMeleeUnits;

		// see where our units can go
		std::map<int,ReachablePlots> unitMovePlots;
		FStaticVector<CvOperationUnit, SAFE_ESTIMATE_NUM_MULTIUNITFORMATION_ENTRIES, true, c_eCiv5GameplayDLL, 0>::iterator it;
		for(it = m_OperationUnits.begin(); it != m_OperationUnits.end(); it++)
		{
			CvUnit *pOpUnit = m_pPlayer->getUnit(it->GetUnitID());
			SPathFinderUserData data(pOpUnit,CvUnit::MOVEFLAG_IGNORE_STACKING,5);
			data.ePathType = PT_UNIT_REACHABLE_PLOTS;
			unitMovePlots[it->GetUnitID()] = GC.GetPathFinder().GetPlotsInReach(pOpUnit->plot(), data);
		}

		// First loop for ranged unit spots
		bool bDone = (iRangedUnitsToPlace==0);
		for(unsigned int iI = 0; iI < m_TempTargets.size() && !bDone; iI++)
		{
			AITacticalTargetType eTargetType = m_TempTargets[iI].GetTargetType();
			if (eTargetType == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT)
			{
				CvPlot* pLoopPlot = GC.getMap().plot(m_TempTargets[iI].GetTargetX(), m_TempTargets[iI].GetTargetY());
				if(FindClosestOperationUnit(pLoopPlot, unitMovePlots, true /*bIncludeRanged*/, true /*bMustBeRangedUnit*/, true))
				{
					for(unsigned int jJ = 0; jJ < m_CurrentMoveUnits.size(); jJ++)
					{
						CvBlockingUnit block;
						block.SetUnitID(m_CurrentMoveUnits[jJ].GetID());
						block.SetPlot(pLoopPlot);
						block.SetNumChoices((int)m_CurrentMoveUnits.size());

						//try to prevent embarkation - unit can still embark to go somewhere else, but shouldn't actually target the plot
						CvUnit *pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[jJ].GetID());
						if (!pUnit->isNativeDomain(pLoopPlot))
							continue;

						block.SetDistanceToTarget(m_CurrentMoveUnits[jJ].GetMovesToTarget());
						m_PotentialBlocks.push_back(block);
					}

					iRangedUnitsToPlace--;
					if(iRangedUnitsToPlace == 0)
					{
						bDone = true;
					}
				}
			}
		}
		AssignDeployingUnits(iRangedUnits - iRangedUnitsToPlace);
		PerformChosenMoves(pTargetPlot);

		// Second loop for everyone else (including remaining ranged units)
		if (bOffensive)
			ScoreCloseOnPlots(pTargetPlot);
		else
			ScoreHedgehogPlots(pTargetPlot);
		std::stable_sort(m_TempTargets.begin(), m_TempTargets.end());

		m_PotentialBlocks.clear();
		iMeleeUnits += iRangedUnitsToPlace;
		iMeleeUnitsToPlace += iRangedUnitsToPlace;
		bDone = (iMeleeUnitsToPlace==0);
		for(unsigned int iI = 0; iI < m_TempTargets.size() && !bDone; iI++)
		{
			//don't care about prio/safety now
			if (true)
			{
				CvPlot* pLoopPlot = GC.getMap().plot(m_TempTargets[iI].GetTargetX(), m_TempTargets[iI].GetTargetY());
				if(FindClosestOperationUnit(pLoopPlot, unitMovePlots, true /*bIncludeRanged*/, false /*bMustBeRangedUnit*/, true))
				{
					for(unsigned int jJ = 0; jJ < m_CurrentMoveUnits.size(); jJ++)
					{
						CvBlockingUnit block;
						block.SetUnitID(m_CurrentMoveUnits[jJ].GetID());
						block.SetPlot(pLoopPlot);
						block.SetNumChoices((int)m_CurrentMoveUnits.size());

						//try to prevent embarkation - unit can still embark to go somewhere else, but shouldn't actually target the plot
						CvUnit *pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[jJ].GetID());
						if (!pUnit->isNativeDomain(pLoopPlot))
							continue;

						block.SetDistanceToTarget(m_CurrentMoveUnits[jJ].GetMovesToTarget());
						m_PotentialBlocks.push_back(block);
					}

					iMeleeUnitsToPlace--;
					if(iMeleeUnitsToPlace == 0)
					{
						bDone = true;
					}
				}
			}
		}

		AssignDeployingUnits(iMeleeUnits - iMeleeUnitsToPlace);
		PerformChosenMoves(pTargetPlot);
	}

	if(m_GeneralsToMove.size() > 0)
	{
		MoveGreatGeneral();
	}
}

/// Bombard and flank attacks (whatever is applicable) against a unit target
void CvTacticalAI::ExecutePriorityAttacksOnUnitTarget(CvTacticalTarget& kTarget)
{
	bool bAttackUnderway = false;
	bool bAttackMade = false;
	CvPlot* pTarget;

	pTarget = GC.getMap().plot(kTarget.GetTargetX(), kTarget.GetTargetY());

	if(kTarget.IsTargetStillAlive(m_pPlayer->GetID()))
		ExecuteSafeBombards(kTarget);

	// Then try for a flank attack
	if(kTarget.IsTargetStillAlive(m_pPlayer->GetID()))
	{
		GetTacticalAnalysisMap()->ClearDynamicFlags();
		GetTacticalAnalysisMap()->SetTargetFlankBonusCells(pTarget);

		bAttackMade = ExecuteFlankAttack(kTarget);
		if(bAttackMade)
		{
			bAttackUnderway = true;
		}
	}
}

/// Move units out of current dominance zone
void CvTacticalAI::ExecuteWithdrawMoves()
{
	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if(pUnit)
		{
			// Compute moves to nearest city and use as sort criteria
			CvCity* pNearestCity = m_pPlayer->GetClosestCityByEstimatedTurns(pUnit->plot());
			if(pNearestCity != NULL && pUnit->CanReachInXTurns(pNearestCity->plot(),5))
			{
				if(MoveToEmptySpaceNearTarget(pUnit, pNearestCity->plot(), pUnit->getDomainType(), 42))
				{
					pUnit->finishMoves();
					UnitProcessed(m_CurrentMoveUnits[iI].GetID(), pUnit->IsCombatUnit());

					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("%s withdrew toward %s, Current X: %d, Current Y: %d", pUnit->getName().GetCString(), pNearestCity->getName().GetCString(),
											pUnit->getX(), pUnit->getY());
						LogTacticalMessage(strLogString, false);
					}
				}
			}
		}
	}

	//now move all units which didn't find a path to a city
	ExecuteMovesToSafestPlot();
}

/// Move naval units on top of embarked units in danger
void CvTacticalAI::ExecuteEscortEmbarkedMoves()
{
	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if(pUnit)
		{
			CvPlot *pBestTarget = NULL;
			int iHighestDanger = -1;

			// Loop through all my embarked units that are: alone and within range
			CvUnit* pLoopUnit;
			int iLoop;
			for(pLoopUnit = m_pPlayer->firstUnit(&iLoop); pLoopUnit; pLoopUnit = m_pPlayer->nextUnit(&iLoop))
			{
				if (pLoopUnit->getDomainType() != DOMAIN_LAND)
				{
					continue;
				}
				else if (!pLoopUnit->isEmbarked())
				{
					continue;
				}
				else if (plotDistance(*pUnit->plot(), *pLoopUnit->plot()) > pUnit->baseMoves()*2)
				{
					continue;
				}
				else if(pLoopUnit->plot()->getBestDefender(m_pPlayer->GetID()))
				{
					continue;
				}
				//Ignore guys that can still move.
				else if(pLoopUnit->getMoves() > 0)
				{
					continue;
				}
			
				CvPlot *pTarget = pLoopUnit->plot();
				// Can this unit get to the embarked unit in two moves?
				int iTurns = pUnit->TurnsToReachTarget(pTarget);
				if (iTurns <= 1)
				{
					//note: civilian in danger have INT_MAX
					int iDanger = m_pPlayer->GetPlotDanger(*pTarget,pUnit);
					if (iDanger > iHighestDanger)
					{
						iHighestDanger = iDanger;
						pBestTarget = pTarget;
					}
				}
			}

			if (pBestTarget)
			{
				ExecuteMoveToPlotIgnoreDanger(pUnit, pBestTarget, true);

				//If we can shoot while doing this, do it!
				if (TacticalAIHelpers::PerformRangedAttackWithoutMoving(pUnit))
				{
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("%s escort opportunity range attack, Current X: %d, Current Y: %d", pUnit->getName().GetCString(), pUnit->getX(), pUnit->getY());
						LogTacticalMessage(strLogString, false);
					}
				}

				pUnit->finishMoves();
				UnitProcessed(m_CurrentMoveUnits[iI].GetID());

				if(GC.getLogging() && GC.getAILogging())
				{
					CvString strLogString;
					strLogString.Format("%s escorted embarked unit at, Current X: %d, Current Y: %d", pUnit->getName().GetCString(), pUnit->getX(), pUnit->getY());
					LogTacticalMessage(strLogString, false);
				}
			}
		}
	}
}

// Get best plot of the array of possible plots, based on plot danger.
CvPlot* CvTacticalAI::GetBestRepositionPlot(CvUnit* pUnit, CvPlot* plotTarget, int iAcceptableDanger)
{
	//safety: barbarians don't leave camp
	if (pUnit->isBarbarian() && pUnit->plot()->getImprovementType() == GC.getBARBARIAN_CAMP_IMPROVEMENT())
		return NULL;

	//don't pull units out of cities for repositioning
	if (pUnit->IsGarrisoned())
		return NULL;

	ReachablePlots reachablePlots;
	TacticalAIHelpers::GetAllPlotsInReachThisTurn(pUnit,pUnit->plot(),reachablePlots,true,true,false);

	if (reachablePlots.empty())
		return NULL;

	CvCity* pTargetCity = plotTarget->getPlotCity();
	CvUnit* pTargetUnit = NULL;
	if (!pTargetCity)
		pTargetUnit = plotTarget->getBestDefender(NO_PLAYER, m_pPlayer->GetID());

	//done with the preparation, now start for real
	std::vector<SPlotWithTwoScores> vStats;
	int iHighestAttack = 0;
	int iLowestDanger = INT_MAX;
	bool bIsRanged = pUnit->isRanged();

	for (ReachablePlots::iterator moveTile=reachablePlots.begin(); moveTile!=reachablePlots.end(); ++moveTile)
	{
		CvPlot* pMoveTile = GC.getMap().plotByIndexUnchecked(moveTile->iPlotIndex);

		//already occupied?
		if (!pUnit->canMoveInto(*pMoveTile,CvUnit::MOVEFLAG_DESTINATION ))
			continue;

		bool bBetterPass = false;
		if (bIsRanged)
		{
			//don't fly too close to the sun ...
			if ( pUnit->GetRange()>1 && plotDistance(*pMoveTile,*plotTarget)<2 )
				bBetterPass = true;
		}

		int iCurrentDanger = m_pPlayer->GetPlotDanger(*pMoveTile, pUnit);

		int iCurrentAttack = 0; //these methods take into account embarkation so we don't have to check for it
		if (bIsRanged && pUnit->canEverRangeStrikeAt(plotTarget->getX(),plotTarget->getY(),pMoveTile))
			iCurrentAttack = pUnit->GetMaxRangedCombatStrength(pTargetUnit, pTargetCity, true, true, plotTarget, pMoveTile);
		else if (!bIsRanged && (pUnit->GetNumEnemyUnitsAdjacent()>0 || pMoveTile->IsFriendlyUnitAdjacent(pUnit->getTeam(),true)) )
			iCurrentAttack = pUnit->GetMaxAttackStrength(pMoveTile, plotTarget, pTargetUnit);

		if (bBetterPass)
			iCurrentAttack /= 2;

		if (iCurrentDanger<=iAcceptableDanger && iCurrentAttack>0)
		{
			vStats.push_back( SPlotWithTwoScores(pMoveTile,iCurrentAttack,iCurrentDanger) );

			iHighestAttack = max( iHighestAttack, iCurrentAttack );
			iLowestDanger = min( iLowestDanger, iCurrentDanger );
		}
	}

	//we want to find the best combination of attack potential and danger
	float fBestScore = 0;
	CvPlot* pBestRepositionPlot = NULL;
	for (std::vector<SPlotWithTwoScores>::iterator it=vStats.begin(); it!=vStats.end(); ++it)
	{
		//be conservative: danger counts twice as much as attack strength
		float fScore = it->score1 / float(iHighestAttack) + 2 * float(iLowestDanger) / it->score2;

		if (fScore > fBestScore)
		{
			pBestRepositionPlot = it->pPlot;
			fBestScore = fScore;
		}
	}

	return pBestRepositionPlot;
}

#ifdef MOD_AI_SMART_AIR_TACTICS
//AMS: Fills m_CurrentAirSweepUnits with all units able to sweep at target plot.
void CvTacticalAI::FindAirUnitsToAirSweep(CvPlot* pTarget)
{
	list<int>::iterator it;
	CvUnit* pLoopUnit;
	m_CurrentAirSweepUnits.clear();
	int interceptionsOnPlot = m_pPlayer->GetMilitaryAI()->GetMaxPossibleInterceptions(pTarget, true);

	// Loop through all units available to tactical AI this turn
	for (it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end() && interceptionsOnPlot > 0; ++it)
	{
		pLoopUnit = m_pPlayer->getUnit(*it);

		if (pLoopUnit)
		{
			// Is an air unit.
			if (pLoopUnit->getDomainType() == DOMAIN_AIR && pLoopUnit->canMove())
			{
				// Is able to sweep at target
				if (pLoopUnit->canAirSweepAt(pTarget->getX(), pTarget->getY()))
				{

					int iAttackStrength = pLoopUnit->GetMaxAttackStrength(pLoopUnit->plot(), pTarget, GetProbableInterceptor(pTarget));
					// Mod to air sweep strength
					iAttackStrength *= (100 + pLoopUnit->GetAirSweepCombatModifier());
					iAttackStrength /= 100;
					CvTacticalUnit unit;
					unit.SetID(pLoopUnit->GetID());
					unit.SetAttackStrength(iAttackStrength);
					unit.SetHealthPercent(pLoopUnit->GetCurrHitPoints(), pLoopUnit->GetMaxHitPoints());
					m_CurrentAirSweepUnits.push_back(unit);

					interceptionsOnPlot--;

					// We also remove the unit from m_CurrentMoveUnits
					std::vector<CvTacticalUnit>::iterator itM;

					for (itM = m_CurrentMoveUnits.begin(); itM != m_CurrentMoveUnits.end(); itM++)
					{
						if ((*itM).GetID() == pLoopUnit->GetID())
						{
							m_CurrentMoveUnits.erase(itM);
							break;
						}
					}
				}
			}
		}
	}

	std::stable_sort(m_CurrentAirSweepUnits.begin(), m_CurrentAirSweepUnits.end());
}

CvUnit* CvTacticalAI::GetProbableInterceptor(CvPlot* pTargetPlot) const
{
	int iLoopUnit;
	CvUnit* pBestUnit = NULL;
	CvUnit* pLoopUnit;

	// Loop through all the players
	for (int iI = 0; iI < MAX_PLAYERS; iI++)
	{
		CvPlayer& kPlayer = GET_PLAYER((PlayerTypes)iI);

		if (kPlayer.isAlive() && kPlayer.GetID() != m_pPlayer->GetID())
		{
			if (atWar(kPlayer.getTeam(), m_pPlayer->getTeam()))
			{
				// Loop through their units looking for intercept capable units
				iLoopUnit = 0;
				for (pLoopUnit = kPlayer.firstUnit(&iLoopUnit); pLoopUnit != NULL; pLoopUnit = kPlayer.nextUnit(&iLoopUnit))
				{
					// Must be able to intercept this turn
					if (!pLoopUnit->isDelayedDeath() && pLoopUnit->canAirDefend() && !pLoopUnit->isInCombat() && !pLoopUnit->isOutOfInterceptions())
					{
						// Must either be a non-air Unit, or an air Unit that hasn't moved this turn and is on intercept duty
						if ((pLoopUnit->getDomainType() != DOMAIN_AIR) || !(pLoopUnit->hasMoved() && pLoopUnit->GetActivityType() == ACTIVITY_INTERCEPT))
						{
							// Test range
							if (plotDistance(pLoopUnit->getX(), pLoopUnit->getY(), pTargetPlot->getX(), pTargetPlot->getY()) <= pLoopUnit->getUnitInfo().GetAirInterceptRange())
							{
								if (pLoopUnit->currInterceptionProbability() > 0)
								{
									if (pBestUnit)
									{
										if (pLoopUnit->currInterceptionProbability() > pBestUnit->currInterceptionProbability())
										{
											pBestUnit = pLoopUnit;
										}
										else if (pLoopUnit->currInterceptionProbability() == pBestUnit->currInterceptionProbability() &&
											pLoopUnit->GetBaseCombatStrengthConsideringDamage() > pBestUnit->GetBaseCombatStrengthConsideringDamage())
										{
											pBestUnit = pLoopUnit;
										}
									}
									else
									{
										pBestUnit = pLoopUnit;
									}
								}
							}
						}
					}
				}
			}
		}
	}

	return pBestUnit;
}
#endif

/// Finds both high and normal priority units we can use for this move (returns true if at least 1 unit found)
bool CvTacticalAI::FindUnitsForThisMove(TacticalAIMoveTypes eMove, CvPlot* pTarget, int iNumTurnsAway /* = -1 if any distance okay */, bool bRangedOnly)
{
	bool rtnValue = false;

	list<int>::iterator it;
	m_CurrentMoveUnits.clear();
	m_CurrentMoveHighPriorityUnits.clear();

	// Loop through all units available to tactical AI this turn
	for(it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pLoopUnit = m_pPlayer->getUnit(*it);
		if(pLoopUnit && pLoopUnit->getDomainType() != DOMAIN_AIR && pLoopUnit->IsCombatUnit())
		{
			// Make sure domain matches
			if(pLoopUnit->getDomainType() == DOMAIN_SEA && !pTarget->isWater() ||
			        pLoopUnit->getDomainType() == DOMAIN_LAND && pTarget->isWater())
			{
				continue;
			}

#if defined(MOD_BALANCE_CORE)
			if(!pLoopUnit->canMove() || !pLoopUnit->IsCanAttack() || pLoopUnit->GetDanger(pTarget)>pLoopUnit->GetCurrHitPoints())
			{
				continue;
			}
#endif

			bool bSuitableUnit = false;
			bool bHighPriority = false;

			if(eMove == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_GARRISON_ALREADY_THERE] ||
			        eMove == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_GARRISON_1_TURN])
			{
				// Want to put ranged units in cities to give them a ranged attack (but siege units should be used for offense)
				if (pLoopUnit->isRanged() && pLoopUnit->getUnitInfo().GetUnitAIType(UNITAI_CITY_BOMBARD)==false)
					bHighPriority = true;
				else if(bRangedOnly)
					continue;

				if(pLoopUnit->IsHurt())
					bHighPriority = true;

				// Don't put units with a defense boosted from promotions in cities, these boosts are ignored
				if(pLoopUnit->getDefenseModifier() == 0)
					bSuitableUnit = true;
			}

			else if(eMove == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_GUARD_IMPROVEMENT_ALREADY_THERE] ||
			        eMove == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_GUARD_IMPROVEMENT_1_TURN] ||
			        eMove == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_BASTION_ALREADY_THERE] ||
			        eMove == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_BASTION_1_TURN])
			{
				// No ranged units or units without defensive bonuses as plot defenders
				if(!pLoopUnit->isRanged() && !pLoopUnit->noDefensiveBonus())
				{
					bSuitableUnit = true;

					// Units with defensive promotions are especially valuable
					if(pLoopUnit->getDefenseModifier() > 0 || pLoopUnit->getExtraCombatPercent() > 0)
					{
						bHighPriority = true;
					}
				}

				//Let's not pull out garrisons to do this.
				if(pLoopUnit->IsGarrisoned())
					continue;
			}
			else if(eMove == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_ANCIENT_RUINS])
			{
				// Fast movers are top priority
				if(pLoopUnit->getUnitInfo().GetUnitAIType(UNITAI_FAST_ATTACK))
					bHighPriority = true;

				bSuitableUnit = true;
			}
#if defined(MOD_BALANCE_CORE)
			else if (eMove == (TacticalAIMoveTypes)m_CachedInfoTypes[eTACTICAL_POSTURE_SHORE_BOMBARDMENT])
			{
				if (pLoopUnit->getDomainType() == DOMAIN_SEA && pLoopUnit->IsCombatUnit())
				{
					if (pLoopUnit->getUnitInfo().GetUnitAIType(UNITAI_ASSAULT_SEA))
						bHighPriority = true;
				}

				bSuitableUnit = true;
			}
#endif

			if(bSuitableUnit)
			{
				int iMoves = pLoopUnit->TurnsToReachTarget(pTarget, false, false, (iNumTurnsAway == -1 ? MAX_INT : iNumTurnsAway));
				if(iMoves != MAX_INT && (iNumTurnsAway == -1 ||
						                    (iNumTurnsAway == 0 && pLoopUnit->plot() == pTarget) || iMoves <= iNumTurnsAway))
				{
					CvTacticalUnit unit;
					unit.SetID(pLoopUnit->GetID());
					unit.SetHealthPercent(pLoopUnit->GetCurrHitPoints(), pLoopUnit->GetMaxHitPoints());
					unit.SetMovesToTarget(iMoves);

					if(bHighPriority)
					{
						m_CurrentMoveHighPriorityUnits.push_back(unit);
					}
					else
					{
						m_CurrentMoveUnits.push_back(unit);
					}

					rtnValue = true;
				}
			}
		}
	}

	return rtnValue;
}

/// Fills m_CurrentMoveUnits with all units within X turns of a target (returns TRUE if 1 or more found)
bool CvTacticalAI::FindUnitsWithinStrikingDistance(CvPlot* pTarget, bool bNoRangedUnits, bool bNavalOnly, bool bMustMoveThrough, bool bIncludeBlockedUnits, bool bIncludeInactiveUnits)
{
	bool rtnValue = false;
	m_CurrentMoveUnits.clear();

	bool bIsCityTarget = pTarget->isCity();
	bool bAirUnitsAdded = false;
	CvUnit* pDefender = pTarget->getBestDefender(NO_PLAYER, m_pPlayer->GetID());

	//todo: check if defender can be damaged at all or if an attacker would die

	// Loop through all units available to tactical AI this turn
	for(list<int>::iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pLoopUnit = m_pPlayer->getUnit(*it);
		if(pLoopUnit)
		{
			//To effectively skip all non-naval units
			if(bNavalOnly && pLoopUnit->getDomainType() != DOMAIN_SEA)
				continue;

			// To effectively skip all ranged units...
			if (bNoRangedUnits && pLoopUnit->IsCanAttackRanged())
				continue;

			// don't use non-combat units or ones that are out of moves
			if (!bIncludeInactiveUnits && (!pLoopUnit->IsCanAttack() || !pLoopUnit->canMove()))
				continue;

			if (!bIsCityTarget && pLoopUnit->IsCityAttackSupport())
				continue;	

			// Don't pull melee units out of camps to attack.
			if(pLoopUnit->isBarbarian() && !pLoopUnit->isRanged() && (pLoopUnit->plot()->getImprovementType() == GC.getBARBARIAN_CAMP_IMPROVEMENT()))
				continue;

			// Only time ranged units aren't selected for city attack is when it needs to be captured
			if (pLoopUnit->isNoCapture() && bIsCityTarget && bNoRangedUnits)
				continue;

			// Don't bother with pathfinding if we're very far away
			if (plotDistance(*pLoopUnit->plot(),*pTarget) > pLoopUnit->baseMoves()*4)
				continue;

			//if it's a fighter plane, don't use it here, we need it for interceptions etc
			if (pLoopUnit->getUnitInfo().GetDefaultUnitAIType() == UNITAI_DEFENSE_AIR)
				continue;

			//if there is an enemy right next to the unit, don't try to attack something else next turn
			int iMaxTurns = pLoopUnit->GetNumEnemyUnitsAdjacent()>0 ? 1 : 2;
			int iDistance = INT_MAX;
			int iTurnsToReach = INT_MAX;

			if ( (pDefender || bIsCityTarget) && pLoopUnit->IsCanAttackRanged() )
			{
				//can attack without moving ... for aircraft and other long-range units
				if (pLoopUnit->canEverRangeStrikeAt(pTarget->getX(), pTarget->getY()))
					iTurnsToReach = 0;
				else if (pLoopUnit->canMove() && pLoopUnit->getDomainType()!=DOMAIN_AIR)
				{
					//try to attack from as far back as possible - if we can't reach those plots, the other are too far anyway
					std::vector<CvPlot*> vPlots;
					TacticalAIHelpers::GetPlotsForRangedAttack(pTarget,pLoopUnit,pLoopUnit->GetRange(),true,vPlots);

					for (std::vector<CvPlot*>::iterator it=vPlots.begin(); it!=vPlots.end(); ++it)
					{
						iDistance = ::plotDistance(pLoopUnit->getX(),pLoopUnit->getY(),(*it)->getX(),(*it)->getY());
						iTurnsToReach = pLoopUnit->TurnsToReachTarget(*it, false /*bIgnoreUnits*/, false /*bIgnoreStacking*/, iMaxTurns);
						if (iTurnsToReach<INT_MAX || iDistance<4)
							break;
					}
				}
			}
			else
			{
				if (pLoopUnit->getDomainType() == DOMAIN_SEA)
				{
					//normal handling
					iDistance = ::plotDistance(pLoopUnit->getX(), pLoopUnit->getY(), pTarget->getX(), pTarget->getY());
					iTurnsToReach = pLoopUnit->TurnsToReachTarget(pTarget, CvUnit::MOVEFLAG_APPROX_TARGET_RING1, iMaxTurns);
				}
				else
				{
					//normal handling
					iDistance = ::plotDistance(pLoopUnit->getX(), pLoopUnit->getY(), pTarget->getX(), pTarget->getY());
					iTurnsToReach = pLoopUnit->TurnsToReachTarget(pTarget, false /*bIgnoreUnits*/, false /*bIgnoreStacking*/, iMaxTurns);
				}
			}

			//no path ... maybe we're blocked by our own units?
			if (iTurnsToReach==INT_MAX)
			{
				if (pLoopUnit->getDomainType() == DOMAIN_SEA)
				{
					int iFlags = CvUnit::MOVEFLAG_APPROX_TARGET_RING1 | CvUnit::MOVEFLAG_IGNORE_STACKING | CvUnit::MOVEFLAG_IGNORE_ZOC | CvUnit::MOVEFLAG_IGNORE_DANGER;
					iTurnsToReach = pLoopUnit->TurnsToReachTarget(pTarget, iFlags, iMaxTurns);

					if (iTurnsToReach == INT_MAX)
						continue;
				}
				else
				{
					if (bIncludeBlockedUnits && pLoopUnit->CanReachInXTurns(pTarget, iMaxTurns, true /*bIgnoreUnits*/))
						iTurnsToReach = 2;
					else
						continue;
				}
			}

			if(pLoopUnit->IsCanAttackRanged())
			{
				// Will we do a significant amount of damage
				int iTargetHitpoints = pDefender ? pDefender->GetCurrHitPoints() : 0;
				if(IsExpectedToDamageWithRangedAttack(pLoopUnit, pTarget, MIN(iTargetHitpoints/20, 3)))
				{
					//first-line ranged and air
					if( (pLoopUnit->getDomainType()==DOMAIN_AIR && iTurnsToReach==0) || (pLoopUnit->getDomainType()!=DOMAIN_AIR && iTurnsToReach<2) )
					{
						CvTacticalUnit unit;
						unit.SetID(pLoopUnit->GetID());
						if (bIsCityTarget)
							unit.SetAttackStrength(pLoopUnit->GetMaxRangedCombatStrength(NULL, pTarget->getPlotCity(), true, true));
						else
							unit.SetAttackStrength(pLoopUnit->GetMaxRangedCombatStrength(pDefender, NULL, true, true));

						unit.SetHealthPercent(pLoopUnit->GetCurrHitPoints(), pLoopUnit->GetMaxHitPoints());
						m_CurrentMoveUnits.push_back(unit);
						rtnValue = true;

						if (pLoopUnit->getDomainType()==DOMAIN_AIR)
							bAirUnitsAdded = true;
					}
					//also check second-line ranged units (non-air only)
					//but give them half priority so they bring up the rear
					else if (iTurnsToReach==2 && pLoopUnit->getDomainType()!=DOMAIN_AIR)
					{
						CvTacticalUnit unit;
						unit.SetID(pLoopUnit->GetID());
						if (bIsCityTarget)
							unit.SetAttackStrength(pLoopUnit->GetMaxRangedCombatStrength(NULL, pTarget->getPlotCity(), true, true)/2);
						else
							unit.SetAttackStrength(pLoopUnit->GetMaxRangedCombatStrength(pDefender, NULL, true, true)/2);
						unit.SetHealthPercent(pLoopUnit->GetCurrHitPoints(), pLoopUnit->GetMaxHitPoints());
						m_CurrentMoveUnits.push_back(unit);
						rtnValue = true;
					}
				}
			}
			else //melee
			{
				int iAttackStrength = pLoopUnit->GetMaxAttackStrength(NULL, pTarget, bIsCityTarget ? NULL : pDefender);

				if (bMustMoveThrough && iTurnsToReach==0)
				{
					CvTacticalUnit unit;
					unit.SetID(pLoopUnit->GetID());
					unit.SetAttackStrength(iAttackStrength);
					unit.SetHealthPercent(pLoopUnit->GetCurrHitPoints(), pLoopUnit->GetMaxHitPoints());
					m_CurrentMoveUnits.push_back(unit);
					rtnValue = true;
				}
				else if (iTurnsToReach<=1)
				{
					CvTacticalUnit unit;
					unit.SetID(pLoopUnit->GetID());
					unit.SetAttackStrength(iAttackStrength);
					unit.SetHealthPercent(pLoopUnit->GetCurrHitPoints(), pLoopUnit->GetMaxHitPoints());
					m_CurrentMoveUnits.push_back(unit);
					rtnValue = true;
				}
				// Units that can make it when others get out of the way are also potentially useful
				// but give them half priority so they bring up the rear
				else if (iTurnsToReach<=2)
				{
					CvTacticalUnit unit;
					unit.SetID(pLoopUnit->GetID());
					unit.SetAttackStrength(iAttackStrength / 2);
					unit.SetHealthPercent(pLoopUnit->GetCurrHitPoints(), pLoopUnit->GetMaxHitPoints());
					m_CurrentMoveUnits.push_back(unit);
					rtnValue = true;
				}
			}
		}
	}

	// As we have air units on the attack targets we should also check possible air sweeps
	if (bAirUnitsAdded)
		FindAirUnitsToAirSweep(pTarget);
	else
		m_CurrentAirSweepUnits.clear();

	// Now sort them in the order we'd like them to attack
	std::stable_sort(m_CurrentMoveUnits.begin(), m_CurrentMoveUnits.end());

	return rtnValue;
}

bool CvTacticalAI::FindUnitsForPillage(CvPlot* pTarget, int iNumTurnsAway, int iMinHitpoints, int iMaxHitpoints)
{
	list<int>::iterator it;
	CvUnit* pLoopUnit;

	bool rtnValue = false;
	m_CurrentMoveUnits.clear();

	// Loop through all units available to tactical AI this turn
	for(it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		pLoopUnit = m_pPlayer->getUnit(*it);
		if(pLoopUnit)
		{
			if (!pLoopUnit->canPillage(pTarget))
				continue;

#if defined(MOD_BALANCE_CORE)
			if(pLoopUnit->isSetUpForRangedAttack() && pLoopUnit->GetCurrHitPoints() > 50)
				continue;
#endif

			if(iMinHitpoints>0 && pLoopUnit->GetCurrHitPoints()<iMinHitpoints)
				continue;

			if(iMaxHitpoints>0 && pLoopUnit->GetCurrHitPoints()>iMaxHitpoints)
				continue;

			int iTurnsCalculated = -1;	// If CanReachInXTurns does an actual pathfind, save the result so we don't just do the same one again.
			if (pLoopUnit->CanReachInXTurns(pTarget, iNumTurnsAway, false /*bIgnoreUnits*/, &iTurnsCalculated))
			{
				CvTacticalUnit unit;
				unit.SetID(pLoopUnit->GetID());
				unit.SetAttackStrength( 1000 - iTurnsCalculated );
				unit.SetHealthPercent(pLoopUnit->GetCurrHitPoints(), pLoopUnit->GetMaxHitPoints());
				m_CurrentMoveUnits.push_back(unit);
				rtnValue = true;
			}
		}
	}

	// Now sort them in the order we'd like them to attack
	std::stable_sort(m_CurrentMoveUnits.begin(), m_CurrentMoveUnits.end());

	return rtnValue;
}

/// Fills m_CurrentMoveCities with all cities within bombard range of a target (returns TRUE if 1 or more found)
bool CvTacticalAI::FindCitiesWithinStrikingDistance(CvPlot* pTargetPlot)
{
	list<int>::iterator it;
	CvCity* pLoopCity;
	int iLoop;

	bool rtnValue = false;
	m_CurrentMoveCities.clear();

	// Loop through all of our cities
	for(pLoopCity = m_pPlayer->firstCity(&iLoop); pLoopCity != NULL; pLoopCity = m_pPlayer->nextCity(&iLoop))
	{
		if(pLoopCity->canRangeStrikeAt(pTargetPlot->getX(), pTargetPlot->getY()) && !pLoopCity->isMadeAttack())
		{
			CvTacticalCity city;
			city.SetID(pLoopCity->GetID());
			m_CurrentMoveCities.push_back(city);
			rtnValue = true;
		}
	}

	// Now sort them in the order we'd like them to attack
	std::stable_sort(m_CurrentMoveCities.begin(), m_CurrentMoveCities.end());

	return rtnValue;
}


/// Fills m_CurrentMoveUnits with all paratrooper units (available to jump) to the target (returns TRUE if 1 or more found)
bool CvTacticalAI::FindParatroopersWithinStrikingDistance(CvPlot* pTarget)
{
	list<int>::iterator it;
	CvUnit* pLoopUnit;

	bool rtnValue = false;
	m_CurrentMoveUnits.clear();

	// Loop through all units available to tactical AI this turn
	for(it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		pLoopUnit = m_pPlayer->getUnit(*it);
		if(pLoopUnit && pLoopUnit->canParadropAt(pLoopUnit->plot(), pTarget->getX(), pTarget->getY()))
		{
			CvTacticalUnit unit;
			unit.SetID(pLoopUnit->GetID());
#ifdef AUI_TACTICAL_FIX_FIND_PARATROOPER_WITHIN_STRIKING_DISTANCE_MELEE_STRENGTH
			unit.SetAttackStrength(pLoopUnit->GetMaxAttackStrength(NULL, pTarget, (pTarget->getPlotCity() != NULL ? NULL : pTarget->getBestDefender(NO_PLAYER, m_pPlayer->GetID()))));
#else
			unit.SetAttackStrength(pLoopUnit->GetMaxAttackStrength(NULL, NULL, NULL));
#endif // AUI_TACTICAL_FIX_FIND_PARATROOPER_WITHIN_STRIKING_DISTANCE_MELEE_STRENGTH
			unit.SetHealthPercent(pLoopUnit->GetCurrHitPoints(), pLoopUnit->GetMaxHitPoints());
			m_CurrentMoveUnits.push_back(unit);
			rtnValue = true;
		}
	}

	// Now sort them in the order we'd like them to attack
	std::stable_sort(m_CurrentMoveUnits.begin(), m_CurrentMoveUnits.end());

	return rtnValue;
}


// search radius for units depending on map size and game turn
int CvTacticalAI::GetRecruitRange() const
{
	int iResult = m_iRecruitRange;
	//add some for duration
	iResult += (4*GC.getGame().getGameTurn()) / max(400, GC.getGame().getMaxTurns());
	//add some for map size
	if (GC.getMap().getWorldSize() == WORLDSIZE_LARGE)
		iResult += 1;
	if (GC.getMap().getWorldSize() == WORLDSIZE_HUGE)
		iResult += 2;

	return iResult;
}

/// Fills m_CurrentMoveUnits with all units within X turns of a target (returns TRUE if 1 or more found)
bool CvTacticalAI::FindClosestUnit(CvPlot* pTarget, int iNumTurnsAway, bool bMustHaveHalfHP, 
	bool bMustBeRangedUnit, int iRangeRequired, bool bNeedsIgnoreLOS, bool bMustBeMeleeUnit, 
	bool bIgnoreUnits, CvPlot* pRangedAttackTarget, int iMaxUnits)
{
	bool rtnValue = false;
	m_CurrentMoveUnits.clear();

	if (pTarget == NULL)
		return false;

	//temporary container - don't add to m_CurrentMoveUnits directly as it would overwrite the unit's tactical move
	std::vector< std::pair<int, int> > vUnitsByDistance;

	// Loop through all units available to tactical AI this turn
	for (list<int>::iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pLoopUnit = m_pPlayer->getUnit(*it);
		if (!pLoopUnit)
			continue;

		// don't use non-combat units (but consider embarked for now)
		if (pLoopUnit->getUnitInfo().GetCombat() == 0)
			continue;
#if defined(MOD_UNITS_MAX_HP)
		if (bMustHaveHalfHP && (pLoopUnit->getDamage() * 2 > pLoopUnit->GetMaxHitPoints()))
#else
		if (bMustHaveHalfHP && (pLoopUnit->getDamage() * 2 > GC.getMAX_HIT_POINTS()))
#endif
			continue;

		if (!pLoopUnit->canMove())
			continue;

		if (bMustBeRangedUnit && ((pTarget->isWater() && pLoopUnit->getDomainType() == DOMAIN_LAND) ||
			(!pTarget->isWater() && !pTarget->isCoastalLand() && pLoopUnit->getDomainType() == DOMAIN_SEA)))
			continue;

		if (bMustBeRangedUnit && !pLoopUnit->IsCanAttackRanged())
			continue;

		if (bMustBeRangedUnit && pLoopUnit->GetRange() < iRangeRequired)
			continue;

		if (bMustBeRangedUnit && !pLoopUnit->canEverRangeStrikeAt(pRangedAttackTarget->getX(), pRangedAttackTarget->getY()))
			continue;

		if (bMustBeRangedUnit && pLoopUnit->isOutOfAttacks())
			continue;

		if (pRangedAttackTarget != NULL && bMustBeRangedUnit && !IsExpectedToDamageWithRangedAttack(pLoopUnit, pRangedAttackTarget))
			continue;

		if (bNeedsIgnoreLOS && !pLoopUnit->IsRangeAttackIgnoreLOS())
			continue;

		if (bMustBeMeleeUnit && pLoopUnit->IsCanAttackRanged())
			continue;

		if (bMustBeMeleeUnit && !pTarget->isCity() && pLoopUnit->IsCityAttackSupport())
			continue;

		//barbarians stay in camp ...
		if (GET_PLAYER(pLoopUnit->getOwner()).isBarbarian() && pLoopUnit->plot()->getImprovementType() == GC.getBARBARIAN_CAMP_IMPROVEMENT())
			continue;

		int iDistance = plotDistance(pLoopUnit->getX(), pLoopUnit->getY(), pTarget->getX(), pTarget->getY());
		if (iDistance > GetRecruitRange())
			continue;

		vUnitsByDistance.push_back(std::make_pair(iDistance, pLoopUnit->GetID()));
	}

	//default sort is by ascending first member of the pair, then by the second - important we don't store pointers here!
	std::stable_sort(vUnitsByDistance.begin(), vUnitsByDistance.end());

	for (std::vector< std::pair<int, int> >::iterator it = vUnitsByDistance.begin(); it != vUnitsByDistance.end(); ++it)
	{
		CvUnit* pLoopUnit = m_pPlayer->getUnit(it->second);

		int iTurns = pLoopUnit->TurnsToReachTarget(pTarget, bIgnoreUnits, (iNumTurnsAway == 0), iNumTurnsAway);
		if (iTurns <= iNumTurnsAway)
		{
			CvTacticalUnit unit;
			unit.SetID(pLoopUnit->GetID());
			unit.SetAttackStrength(1000 - iTurns);
			unit.SetHealthPercent(10, 10);
			unit.SetMovesToTarget(iTurns);
			m_CurrentMoveUnits.push_back(unit);
			rtnValue = true;

			if (m_CurrentMoveUnits.size() == iMaxUnits)
				break;
		}
	}

	return rtnValue;
}

/// Fills m_CurrentMoveUnits with all units in operation that can get to target (returns TRUE if 1 or more found)
bool CvTacticalAI::FindClosestOperationUnit(CvPlot* pTarget, const std::map<int,ReachablePlots>& movePlots, 
	bool bIncludeRanged, bool bRangedOnly, bool bOffensiveCombatExpected)
{
	bool rtnValue = false;
	m_CurrentMoveUnits.clear();

	if (pTarget == NULL)
		return false;

	int iPlotIndex = GC.getMap().plotNum(pTarget->getX(), pTarget->getY());
	CvTacticalAnalysisCell* pCell = GetTacticalAnalysisMap()->GetCell(iPlotIndex);

	// Loop through all units available to operation
	FStaticVector<CvOperationUnit, SAFE_ESTIMATE_NUM_MULTIUNITFORMATION_ENTRIES, true, c_eCiv5GameplayDLL, 0>::iterator it;
	for (it = m_OperationUnits.begin(); it != m_OperationUnits.end(); it++)
	{
		CvUnit* pLoopUnit = m_pPlayer->getUnit(it->GetUnitID());
		if (!pLoopUnit)
			continue;

		if (pLoopUnit->hasMoved())
			continue;
		
		if (pLoopUnit->IsCanAttackRanged())
		{
			if (!bIncludeRanged)
				continue;

			if (bOffensiveCombatExpected)
			{
				if (pCell->GetTargetDistance()>pLoopUnit->GetRange())
					continue;

				if (!pLoopUnit->IsRangeAttackIgnoreLOS() && !pCell->GetHasLOS())
					continue;
			}
		}
		else //melee
		{
			if (bOffensiveCombatExpected)
				if (pCell->GetTargetDistance()>1 && pTarget->GetNumEnemyUnitsAdjacent(pLoopUnit->getTeam(),pLoopUnit->getDomainType())==0)
					continue;
		}

		if (bRangedOnly && !pLoopUnit->IsCanAttackRanged())
			continue;
		
		//avoid embarkation if we need to fight!
		if (bOffensiveCombatExpected && !pLoopUnit->isNativeDomain(pTarget))
			continue;

		SMovePlot dummy(pTarget->GetPlotIndex(),0,0);
		std::map<int,ReachablePlots>::const_iterator it = movePlots.find(pLoopUnit->GetID());
		if (it!=movePlots.end())
		{
			std::map<int,ReachablePlots>::value_type::second_type::const_iterator it2 = it->second.find(dummy);
			if (it2!=it->second.end())
			{
				int iTurns = it2->iTurns;

				CvTacticalUnit unit;
				unit.SetID(pLoopUnit->GetID());
				unit.SetAttackStrength(1000 - iTurns);
				unit.SetHealthPercent(10, 10);
				unit.SetMovesToTarget(iTurns);
				m_CurrentMoveUnits.push_back(unit);
				rtnValue = true;
			}
		}
	}

	// Now sort them by turns to reach
	std::stable_sort(m_CurrentMoveUnits.begin(), m_CurrentMoveUnits.end());

	return rtnValue;
}

/// Fills m_CurrentMoveUnits with all units in naval operation that can get to target (returns TRUE if 1 or more found)
bool CvTacticalAI::FindClosestNavalOperationUnit(CvPlot* pTarget, const std::map<int,ReachablePlots>& movePlots, bool bEscortedUnits)
{
	FStaticVector<CvOperationUnit, SAFE_ESTIMATE_NUM_MULTIUNITFORMATION_ENTRIES, true, c_eCiv5GameplayDLL, 0>::iterator it;

	bool rtnValue = false;
	m_CurrentMoveUnits.clear();

	// Loop through all units available to operation
	std::vector< std::pair<int, int> > vUnitsByDistance;
	for(it = m_OperationUnits.begin(); it != m_OperationUnits.end(); it++)
	{
		CvUnit* pLoopUnit = m_pPlayer->getUnit(it->GetUnitID());
		if(!pLoopUnit)
			continue;

		if(bEscortedUnits && (!pLoopUnit->IsGreatAdmiral() && pLoopUnit->getDomainType() != DOMAIN_LAND))
			continue;

		if(!bEscortedUnits && (pLoopUnit->getDomainType() != DOMAIN_SEA || pLoopUnit->IsGreatAdmiral()))
			continue;

		SMovePlot dummy(pTarget->GetPlotIndex(),0,0);
		std::map<int,ReachablePlots>::const_iterator it = movePlots.find(pLoopUnit->GetID());
		if (it!=movePlots.end())
		{
			std::map<int,ReachablePlots>::value_type::second_type::const_iterator it2 = it->second.find(dummy);
			if (it2!=it->second.end())
			{
				int iTurns = it2->iTurns;

				CvTacticalUnit unit;
				unit.SetID(pLoopUnit->GetID());
				unit.SetAttackStrength(1000-iTurns);
				unit.SetHealthPercent(10,10);
				unit.SetMovesToTarget(iTurns);
				m_CurrentMoveUnits.push_back(unit);
				rtnValue = true;
			}
		}
	}

	// Now sort them by turns to reach
	std::stable_sort(m_CurrentMoveUnits.begin(), m_CurrentMoveUnits.end());

	return rtnValue;
}

#if defined(MOD_AI_SMART_AIR_TACTICS)

// helper function to iterate vector that is of CvPlot Type.
int CvTacticalAI::SamePlotFound(vector<CvPlot*> plotData, CvPlot* plotXy)
{
	int methodResult = 0;
	std::vector<CvPlot*>::iterator it;

	for (it = plotData.begin(); it != plotData.end(); it++)
	{
		if (((*it)->getX() == plotXy->getX()) && ((*it)->getY() == plotXy->getY()))
		{
			methodResult++;
		}
	}

	return methodResult;
}
#endif

/// Estimates the damage we can apply to a target
int CvTacticalAI::ComputeTotalExpectedDamage(CvTacticalTarget* pTarget, CvPlot* pTargetPlot)
{
	int rtnValue = 0;
	int iExpectedDamage;
	int iExpectedSelfDamage;
#if defined(MOD_AI_SMART_RANGED_UNITS)
	int farAwayUnitDivisor;
#endif

	// Loop through all units who can reach the target
	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pAttacker = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
#if defined(MOD_AI_SMART_RANGED_UNITS)
		// If the unit is out of initial range, may not reach successfully to target (we still not know), so value is halved.
		farAwayUnitDivisor = MOD_AI_SMART_RANGED_UNITS ? ((plotDistance(pAttacker->getX(), pAttacker->getY(), pTargetPlot->getX(), pTargetPlot->getY()) > pAttacker-> GetRange()) ? 2 : 1) : 1;
#endif

		// Is target a unit?
		switch(pTarget->GetTargetType())
		{
		case AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT:
		case AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT:
		case AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT:
		{
			CvUnit* pDefender = pTargetPlot->getVisibleEnemyDefender(m_pPlayer->GetID());
			if(pDefender)
			{
#ifdef AUI_TACTICAL_FIX_COMPUTE_EXPECTED_DAMAGE_AIR_UNITS
				if (pAttacker->getDomainType() == DOMAIN_AIR)
				{
					iExpectedDamage = pAttacker->GetAirCombatDamage(pDefender, NULL, false);
					iExpectedSelfDamage = pDefender->GetAirStrikeDefenseDamage(pAttacker, false);
				}
				else
#endif
				if(pAttacker->IsCanAttackRanged())
				{
#if defined(MOD_AI_SMART_RANGED_UNITS)
					iExpectedDamage = ((pAttacker->GetRangeCombatDamage(pDefender, NULL, false)) / farAwayUnitDivisor);
#else
					iExpectedDamage = pAttacker->GetRangeCombatDamage(pDefender, NULL, false);
#endif
					iExpectedSelfDamage = 0;
				}
				else
				{
#ifdef AUI_TACTICAL_FIX_COMPUTE_EXPECTED_DAMAGE_MELEE
					int iAttackerStrength;
					if (plotDistance(pAttacker->getX(), pAttacker->getY(), pTargetPlot->getX(), pTargetPlot->getY()) > 1)
					{
						iAttackerStrength = pAttacker->GetMaxAttackStrength(NULL, pTargetPlot, pDefender);
					}
					else
					{
						iAttackerStrength = pAttacker->GetMaxAttackStrength(pAttacker->plot(), pTargetPlot, pDefender);
					}
#else
					int iAttackerStrength = pAttacker->GetMaxAttackStrength(NULL, pTargetPlot, NULL);
#endif // AUI_TACTICAL_FIX_COMPUTE_EXPECTED_DAMAGE_MELEE
					int iDefenderStrength = pDefender->GetMaxDefenseStrength(pTargetPlot, pAttacker);
					CvUnit* pFireSupportUnit = CvUnitCombat::GetFireSupportUnit(pDefender->getOwner(), pTargetPlot->getX(), pTargetPlot->getY(), pAttacker->getX(), pAttacker->getY());
					int iDefenderFireSupportCombatDamage = 0;
					if(pFireSupportUnit)
					{
						iDefenderFireSupportCombatDamage = pFireSupportUnit->GetRangeCombatDamage(pAttacker, NULL, false);
					}
					iExpectedDamage = pAttacker->getCombatDamage(iAttackerStrength, iDefenderStrength, pAttacker->getDamage() + iDefenderFireSupportCombatDamage, /*bIncludeRand*/ false, /*bAttackerIsCity*/ false, /*bDefenderIsCity*/ false);
					iExpectedSelfDamage = pDefender->getCombatDamage(iDefenderStrength, iAttackerStrength, pDefender->getDamage(), /*bIncludeRand*/ false, /*bAttackerIsCity*/ false, /*bDefenderIsCity*/ false);
				}
				m_CurrentMoveUnits[iI].SetExpectedTargetDamage(iExpectedDamage);
				m_CurrentMoveUnits[iI].SetExpectedSelfDamage(iExpectedSelfDamage);
				rtnValue += iExpectedDamage;
			}
		}
		break;

		case AI_TACTICAL_TARGET_CITY:
		{
			CvCity* pCity = pTargetPlot->getPlotCity();
			if(pCity != NULL)
			{
#ifdef AUI_TACTICAL_FIX_COMPUTE_EXPECTED_DAMAGE_MELEE
				if (pAttacker->IsCanAttackRanged() && pAttacker->GetMaxRangedCombatStrength(NULL, /*pCity*/ pCity, true, true) > pAttacker->GetMaxAttackStrength(NULL, pTargetPlot, NULL))
#else
				if(pAttacker->IsCanAttackRanged() && pAttacker->GetMaxRangedCombatStrength(NULL, /*pCity*/ NULL, true, true) > pAttacker->GetMaxAttackStrength(NULL, pTargetPlot, NULL))
#endif
				{
#if defined(MOD_AI_SMART_RANGED_UNITS)
					iExpectedDamage = ((pAttacker->GetRangeCombatDamage(NULL, pCity, false)) / farAwayUnitDivisor);
#else
					iExpectedDamage = pAttacker->GetRangeCombatDamage(NULL, pCity, false);
#endif
					iExpectedSelfDamage = 0;
				}
				else
				{
#ifdef AUI_TACTICAL_FIX_COMPUTE_EXPECTED_DAMAGE_MELEE
					int iAttackerStrength;
					if (plotDistance(pAttacker->getX(), pAttacker->getY(), pTargetPlot->getX(), pTargetPlot->getY()) > 1)
					{
						iAttackerStrength = pAttacker->GetMaxAttackStrength(NULL, pTargetPlot, NULL);
					}
					else
					{
						iAttackerStrength = pAttacker->GetMaxAttackStrength(pAttacker->plot(), pTargetPlot, NULL);
					}
#else
					int iAttackerStrength = pAttacker->GetMaxAttackStrength(NULL, pTargetPlot, NULL);
#endif
					int iDefenderStrength = pCity->getStrengthValue();
					CvUnit* pFireSupportUnit = CvUnitCombat::GetFireSupportUnit(pCity->getOwner(), pTargetPlot->getX(), pTargetPlot->getY(), pAttacker->getX(), pAttacker->getY());
					int iDefenderFireSupportCombatDamage = 0;
					if(pFireSupportUnit != NULL)
					{
						iDefenderFireSupportCombatDamage = pFireSupportUnit->GetRangeCombatDamage(pAttacker, NULL, false);
					}
					iExpectedDamage = pAttacker->getCombatDamage(iAttackerStrength, iDefenderStrength, pAttacker->getDamage() + iDefenderFireSupportCombatDamage, /*bIncludeRand*/ false, /*bAttackerIsCity*/ false, /*bDefenderIsCity*/ true);
#ifdef AUI_TACTICAL_FIX_NO_CAPTURE
					int iMaxRangedCityDamage = pCity->GetMaxHitPoints() - pCity->getDamage() - 1;
					if (pAttacker->isNoCapture() && iExpectedDamage > iMaxRangedCityDamage)
						iExpectedDamage = iMaxRangedCityDamage;
#endif
					iExpectedSelfDamage = pAttacker->getCombatDamage(iDefenderStrength, iAttackerStrength, pCity->getDamage(), /*bIncludeRand*/ false, /*bAttackerIsCity*/ true, /*bDefenderIsCity*/ false);
				}
				m_CurrentMoveUnits[iI].SetExpectedTargetDamage(iExpectedDamage);
				m_CurrentMoveUnits[iI].SetExpectedSelfDamage(iExpectedSelfDamage);
				rtnValue += iExpectedDamage;
			}
		}
		break;
		}
	}

	//sort by expected damage to target!
	std::stable_sort(m_CurrentMoveUnits.begin(), m_CurrentMoveUnits.end(), TacticalAIHelpers::SortByExpectedTargetDamageDescending);

	return rtnValue;
}

/// Estimates the bombard damage we can apply to a target
int CvTacticalAI::ComputeTotalExpectedCityBombardDamage(CvUnit* pTarget)
{
	int rtnValue = 0;
	int iExpectedDamage;

	// Now loop through all the cities that can bombard it
	for(unsigned int iI = 0; iI < m_CurrentMoveCities.size(); iI++)
	{
		CvCity* pAttackingCity = m_pPlayer->getCity(m_CurrentMoveCities[iI].GetID());
		iExpectedDamage = pAttackingCity->rangeCombatDamage(pTarget, NULL, false);
		m_CurrentMoveCities[iI].SetExpectedTargetDamage(iExpectedDamage);
		rtnValue += iExpectedDamage;
	}

	return rtnValue;
}

bool CvTacticalAI::IsExpectedToDamageWithRangedAttack(CvUnit* pAttacker, CvPlot* pTargetPlot, int iMinDamage)
{
	int iExpectedDamage = 0;

	if(pTargetPlot->isCity())
	{
		CvCity* pCity = pTargetPlot->getPlotCity();
		iExpectedDamage = pAttacker->GetRangeCombatDamage(NULL, pCity, /*bIncludeRand*/ false);
	}
	else
	{
		CvUnit* pDefender = pTargetPlot->getBestDefender(NO_PLAYER, m_pPlayer->GetID());
		if(pDefender)
		{
			iExpectedDamage = pAttacker->GetRangeCombatDamage(pDefender, NULL, false);
		}
	}

	return iExpectedDamage >= iMinDamage;
}

/// Move up close to our target avoiding our own units if possible
bool CvTacticalAI::MoveToEmptySpaceNearTarget(CvUnit* pUnit, CvPlot* pTarget, DomainTypes eDomain, int iMaxTurns)
{
	if (!pUnit || !pTarget)
		return false;

	//nothing to do?
	if ( (plotDistance(pUnit->getX(), pUnit->getY(), pTarget->getX(), pTarget->getY()) < 3) && 
		 (eDomain == NO_DOMAIN || pTarget->getDomain() == eDomain) &&
		 (pUnit->plot()->isAdjacentToArea(pTarget->getArea()) ) )
	{
		pUnit->PushMission(CvTypes::getMISSION_SKIP());
		return true;
	}

	int iFlags = CvUnit::MOVEFLAG_APPROX_TARGET_RING2 | CvUnit::MOVEFLAG_SAFE_EMBARK;
	if (eDomain==pTarget->getDomain())
		iFlags |= CvUnit::MOVEFLAG_APPROX_TARGET_NATIVE_DOMAIN;

	int iTurns = pUnit->TurnsToReachTarget(pTarget,iFlags,iMaxTurns);
	if (iTurns <= iMaxTurns)
	{
		bool bResult = MoveToUsingSafeEmbark(pUnit, pTarget, true, iFlags);
		TacticalAIHelpers::PerformRangedAttackWithoutMoving(pUnit);
		return bResult;
	}

	return false;
}

/// Low-level wrapper on CvUnit::PushMission() for move to missions that avoids embarking if dangerous. Returns true if any move made
bool CvTacticalAI::MoveToUsingSafeEmbark(CvUnit* pUnit, CvPlot* pTargetPlot, bool bMustBeSafeOnLandToo, int iFlags)
{
	int iMoveFlags = iFlags | CvUnit::MOVEFLAG_SAFE_EMBARK;
	if (!bMustBeSafeOnLandToo)
		iMoveFlags |= CvUnit::MOVEFLAG_IGNORE_DANGER;

	int iActualTurns = 0;
	if(pUnit->GeneratePath(pTargetPlot,iMoveFlags,INT_MAX,&iActualTurns))
	{
		//don't act on it for now
		if (MOD_BALANCE_CORE_MILITARY_LOGGING)
		{
			//get a feel for how long it should take to get there (don't overwrite unit path cache)
			SPathFinderUserData data(pUnit,CvUnit::MOVEFLAG_IGNORE_STACKING);
			int iRefTurns =  GC.GetPathFinder().GetPathLengthInTurns(pUnit->plot(),pTargetPlot,data);
			
			if (iActualTurns>2*iRefTurns)
				OutputDebugString("warning: unit taking a long detour!\n");
		}

		pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pTargetPlot->getX(), pTargetPlot->getY(), iMoveFlags);
		//for debugging ...
		pUnit->SetMissionAI(MISSIONAI_ASSAULT,pTargetPlot,NULL);
		return true;
	}
	else
	{
		//try again without the safe embarkation and maybe start moving in the right direction at least
		SPathFinderUserData data(pUnit,iFlags & ~CvUnit::MOVEFLAG_SAFE_EMBARK);
		SPath path = GC.GetPathFinder().GetPath(pUnit->plot(),pTargetPlot,data);
		if (!!path)
		{
			CvPlot* pThisTurnTarget = PathHelpers::GetPathEndFirstTurnPlot(path);
			if (pThisTurnTarget && pUnit->GetDanger(pThisTurnTarget)==0)
			{
				pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pThisTurnTarget->getX(), pThisTurnTarget->getY());
				//for debugging ...
				pUnit->SetMissionAI(MISSIONAI_ASSAULT,pTargetPlot,NULL);
				return true;
			}
		}

		//otherwise, bad luck. stay put and fortify until life improves for you.
		TacticalAIHelpers::PerformRangedAttackWithoutMoving(pUnit);
		if (pUnit->canFortify(pUnit->plot()))
			pUnit->PushMission(CvTypes::getMISSION_FORTIFY());
		else
			pUnit->PushMission(CvTypes::getMISSION_SKIP());

		return false;
	}
}

/// Find a multi-turn target for a land barbarian to wander towards
CvPlot* CvTacticalAI::FindBestBarbarianLandMove(CvUnit* pUnit)
{
	CvPlot* pBestMovePlot = FindNearbyTarget(pUnit, m_iLandBarbarianRange);

	// move toward trade routes
	if (pBestMovePlot == NULL)
	{
		pBestMovePlot = FindBarbarianGankTradeRouteTarget(pUnit);
	}

	// explore wander
	if(pBestMovePlot == NULL)
	{
		pBestMovePlot = FindBarbarianExploreTarget(pUnit);
	}

	// if nothing to explore, go back to camp
	if (pBestMovePlot == NULL)
	{
		pBestMovePlot = FindNearbyTarget(pUnit, m_iLandBarbarianRange, AI_TACTICAL_TARGET_BARBARIAN_CAMP);
	}

	return pBestMovePlot;
}

/// Find a multi-turn target for a land barbarian to wander towards
CvPlot* CvTacticalAI::FindPassiveBarbarianLandMove(CvUnit* pUnit)
{
	CvPlot* pBestMovePlot = NULL;
	int iBestValue;
	int iValue;

	iBestValue = MAX_INT;
	pBestMovePlot = NULL;

	for(unsigned int iI = 0; iI < m_AllTargets.size(); iI++)
	{
		// Is this target a camp?
		if (m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_BARBARIAN_CAMP || m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_CITY_TO_DEFEND)
		{
			iValue = plotDistance(pUnit->getX(), pUnit->getY(), m_AllTargets[iI].GetTargetX(), m_AllTargets[iI].GetTargetY());
			if(iValue < iBestValue)
			{
				iBestValue = iValue;
				pBestMovePlot = GC.getMap().plot(m_AllTargets[iI].GetTargetX(), m_AllTargets[iI].GetTargetY());
			}
		}
	}

	if(pBestMovePlot == NULL)
	{
		pBestMovePlot = FindBarbarianExploreTarget(pUnit);
	}

	return pBestMovePlot;
}

/// Find a multi-turn target for a sea barbarian to wander towards
CvPlot* CvTacticalAI::FindBestBarbarianSeaMove(CvUnit* pUnit)
{
	CvPlot* pBestMovePlot = NULL;
	int iBestValue = MAX_INT;
	CvTacticalTarget* pTarget;

	SPathFinderUserData data(pUnit, 0, m_iSeaBarbarianRange/2);
	data.ePathType = PT_UNIT_REACHABLE_PLOTS;
	ReachablePlots movePlots = GC.GetPathFinder().GetPlotsInReach(pUnit->plot(), data);

	// Loop through all unit targets to find the closest
	pTarget = GetFirstUnitTarget();
	while(pTarget != NULL)
	{
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());

		SMovePlot dummy(pPlot->GetPlotIndex(), 0, 0);
		ReachablePlots::const_iterator itPlot = movePlots.find(dummy);
		if (itPlot != movePlots.end() && itPlot->iTurns < iBestValue)
		{
			iBestValue = itPlot->iTurns;
			pBestMovePlot = pPlot;
		}

		pTarget = GetNextUnitTarget();
	}

	// move toward trade routes
	if (pBestMovePlot == NULL)
	{
		pBestMovePlot = FindBarbarianGankTradeRouteTarget(pUnit);
	}

	// No units to pick on, so sail to a tile adjacent to the second closest barbarian camp
	if(pBestMovePlot == NULL)
	{
		CvPlot* pNearestCamp = NULL;
		int iBestCampDistance = MAX_INT;

		// Start by finding the very nearest camp
		pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_BARBARIAN_CAMP);
		while(pTarget != NULL)
		{
			CvPlot* pCamp = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
			if (pCamp->isAdjacentToShallowWater())
			{
				int iDistance = plotDistance(pUnit->getX(), pUnit->getY(), pTarget->GetTargetX(), pTarget->GetTargetY());
				if (iDistance < iBestCampDistance)
				{
					pNearestCamp = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
					iBestCampDistance = iDistance;
				}
			}

			pTarget = GetNextZoneTarget();
		}

		// Try to sail to the second closest camp - this should result in patrolling behavior
		pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_BARBARIAN_CAMP);
		while(pTarget != NULL)
		{
			CvPlot* pCamp = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
			if(pCamp != pNearestCamp && pCamp->isAdjacentToShallowWater())
			{
				for (ReachablePlots::const_iterator it = movePlots.begin(); it != movePlots.end(); ++it)
				{
					CvPlot* pTestPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);
					if (plotDistance(*pTestPlot, *pCamp) == 1)
					{
						int iValue = it->iTurns;
						if (iValue < iBestValue)
						{
							iBestValue = iValue;
							pBestMovePlot = pTestPlot;
						}
					}
				}
			}
			pTarget = GetNextZoneTarget();
		}
	}

	// No obvious target ... next try
	if (pBestMovePlot == NULL)
		pBestMovePlot = FindBarbarianExploreTarget(pUnit);

	return pBestMovePlot;
}

/// Scan nearby tiles for the best choice, borrowing code from the explore AI
CvPlot* CvTacticalAI::FindBarbarianExploreTarget(CvUnit* pUnit)
{
	CvPlot* pBestMovePlot = 0;
	int iBestValue = 0;

	ReachablePlots reachablePlots;
	TacticalAIHelpers::GetAllPlotsInReachThisTurn(pUnit, pUnit->plot(), reachablePlots, true, true, false, 0);
	for (ReachablePlots::iterator it = reachablePlots.begin(); it != reachablePlots.end(); ++it)
	{
		CvPlot* pConsiderPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);

		if (pUnit->atPlot(*pConsiderPlot))
			continue;

		if (!pConsiderPlot->isRevealed(pUnit->getTeam()))
			continue;

		// Value them based on their explore value
#if defined(MOD_CORE_ALTERNATIVE_EXPLORE_SCORE)
		int iValue = CvEconomicAI::ScoreExplorePlot2(pConsiderPlot, m_pPlayer, pUnit->getDomainType(), false);
#else
		int iValue = CvEconomicAI::ScoreExplorePlot(pConsiderPlot, pUnit->getTeam(), pUnit->getUnitInfo().GetBaseSightRange(), eDomain);
#endif

		// disembark if possible
		if (pUnit->isNativeDomain(pConsiderPlot))
		{
			iValue += 200;
		}

		// Add special value enemy lands
		if (pConsiderPlot->isAdjacentOwned() || pConsiderPlot->isOwned())
		{
			iValue += 100;
		}

		//magic knowledge - gravitate towards cities
		int iCityDistance = GC.getGame().GetClosestCityDistanceInTurns(pConsiderPlot);
		if (iCityDistance<10)
			iValue += (10 - iCityDistance);

		// If still have no value, score equal to distance from my current plot
		if (iValue == 0)
			iValue = plotDistance(pUnit->getX(), pUnit->getY(), pConsiderPlot->getX(), pConsiderPlot->getY());

		if (iValue > iBestValue)
		{
			pBestMovePlot = pConsiderPlot;
			iBestValue = iValue;
		}
	}

	return pBestMovePlot;
}

/// Scan nearby tiles for a trade route to sit and gank from
CvPlot* CvTacticalAI::FindBarbarianGankTradeRouteTarget(CvUnit* pUnit)
{
	CvPlot* pBestMovePlot = NULL;
	int iBestDistance = INT_MAX;
	int iRange = m_iLandBarbarianRange/2+1;

	for(int iX = -iRange; iX <= iRange; iX++)
	{
		for(int iY = -iRange; iY <= iRange; iY++)
		{
			CvPlot* pPlot = plotXYWithRangeCheck(pUnit->getX(), pUnit->getY(), iX, iY, iRange);
			if(!pPlot)
			{
				continue;
			}

			if(pUnit->atPlot(*pPlot))
			{
				continue;
			}

			if(!pPlot->isRevealed(pUnit->getTeam()))
			{
				continue;
			}

			if(pPlot->area() != pUnit->area())
			{
				continue;
			}

			if(!pUnit->CanReachInXTurns( pPlot, 2))
			{
				continue;
			}

			int iDistance = plotDistance(*pPlot,*pUnit->plot());
			if(pPlot->IsTradeUnitRoute() && iDistance<iBestDistance)
			{
				pBestMovePlot = pPlot;
				iBestDistance = iDistance;
			}
		}
	}
	return pBestMovePlot;
}

#if defined(MOD_BALANCE_CORE_MILITARY)
/// Do we want to move this air unit to a new base?
bool CvTacticalAI::ShouldRebase(CvUnit* pUnit) const
{
	if (!pUnit || pUnit->getDomainType()!=DOMAIN_AIR)
		return false;

	CvPlot* pUnitPlot = pUnit->plot();
	if (!pUnitPlot)
		return false;

	// Is this unit in a base in danger?
	if (pUnitPlot->isCity())
	{
		if (pUnitPlot->getPlotCity()->isInDangerOfFalling())
			return true;

		if (IsUnitHealing(pUnit->GetID()) && m_pPlayer->GetPlotDanger(*pUnitPlot,pUnitPlot->getPlotCity())>0)
			return true;
	}
	else
	{
		CvUnit *pCarrier = pUnit->getTransportUnit();
		if (pCarrier && pCarrier->isProjectedToDieNextTurn())
			return true;

		if (IsUnitHealing(pUnit->GetID()) && m_pPlayer->GetPlotDanger(*pUnitPlot,pCarrier)>0)
			return true;
	}

	bool bIsNeeded = false;
	if (!m_pPlayer->GetPlayersAtWarWith().empty())
	{
		switch (pUnit->getUnitInfo().GetDefaultUnitAIType())
		{
		case UNITAI_DEFENSE_AIR:
			// Is this a fighter that doesn't have any useful missions nearby
			{
				int iNumNearbyEnemyAirUnits = m_pPlayer->GetMilitaryAI()->GetNumEnemyAirUnitsInRange(pUnitPlot, pUnit->GetRange(), true /*bCountFighters*/, true /*bCountBombers*/);
				if (iNumNearbyEnemyAirUnits > 0  || m_pPlayer->GetMilitaryAI()->GetBestAirSweepTarget(pUnit))
				{
					bIsNeeded = true;
				}
			}
			break;
		case UNITAI_ATTACK_AIR:
		case UNITAI_ICBM:
		case UNITAI_MISSILE_AIR:
			//Is this a bomber or a missile that lacks useful target?
			{
				//check for targets in tactical map
				for(unsigned int iI = 0; iI < m_AllTargets.size(); iI++)
				{
					// Is the target of an appropriate type?
					if(m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT ||
							m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT ||
							m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT ||
							m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_CITY
							)
					{
						// Is this target near enough?
						if(plotDistance(pUnit->getX(), pUnit->getY(), m_AllTargets[iI].GetTargetX(), m_AllTargets[iI].GetTargetY()) <= pUnit->GetRange())
						{
							bIsNeeded = true;
							break;
						}
					}
				}
			}
			break;
		}

	}

	return !bIsNeeded;
}
#endif

/// Find a multi-turn target for a land unit to wander towards
#if defined(MOD_BALANCE_CORE_MILITARY)
CvPlot* CvTacticalAI::FindNearbyTarget(CvUnit* pUnit, int iRange, AITacticalTargetType eType, CvUnit* pNoLikeUnit, bool bAllowDefensiveTargets, bool bHighPriorityOnly)
#else
CvPlot* CvTacticalAI::FindNearbyTarget(CvUnit* pUnit, int iRange, AITacticalTargetType eType, CvUnit* pNoLikeUnit)
#endif
{
	CvPlot* pBestMovePlot = NULL;
	int iBestValue = 0;
	int iMaxTurns = iRange/2+3;

	// fill this in once we have our first match
	ReachablePlots reachablePlots;

	// Loop through all appropriate targets to find the closest
	for(unsigned int iI = 0; iI < m_AllTargets.size(); iI++)
	{
		CvTacticalTarget target = m_AllTargets[iI];

		// Is the target of an appropriate type?
		bool bTypeMatch = false;
		if(eType == AI_TACTICAL_TARGET_NONE)
		{
#if defined(MOD_BALANCE_CORE)
			if(bHighPriorityOnly)
			{
				if(target.GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT || 
					target.GetTargetType() == AI_TACTICAL_TARGET_CITY)
				{
					bTypeMatch = true;
				}
			}
			else if(m_pPlayer->isMinorCiv() || m_pPlayer->isBarbarian())
			{
				if(target.GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT ||
			        target.GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT ||
			        target.GetTargetType() == AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT ||
			        target.GetTargetType() == AI_TACTICAL_TARGET_CITY ||
					target.GetTargetType() == AI_TACTICAL_TARGET_IMPROVEMENT)
				{
					bTypeMatch = true;
				}
			}
			else
			{
#endif
			if(target.GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT ||
			        target.GetTargetType() == AI_TACTICAL_TARGET_MEDIUM_PRIORITY_UNIT ||
			        target.GetTargetType() == AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT ||
			        target.GetTargetType() == AI_TACTICAL_TARGET_CITY ||
			        target.GetTargetType() == AI_TACTICAL_TARGET_BARBARIAN_CAMP ||
					target.GetTargetType() == AI_TACTICAL_TARGET_IMPROVEMENT)
			{
				bTypeMatch = true;
			}

#if defined(MOD_BALANCE_CORE_MILITARY)
			}
			if (bAllowDefensiveTargets || m_pPlayer->isMinorCiv())
			{
				if(target.GetTargetType() == AI_TACTICAL_TARGET_CITY_TO_DEFEND ||
					target.GetTargetType() == AI_TACTICAL_TARGET_IMPROVEMENT_TO_DEFEND ||
					target.GetTargetType() == AI_TACTICAL_TARGET_DEFENSIVE_BASTION)
				{
					bTypeMatch = true;
				}
			}
#endif

		}
		else if(target.GetTargetType() ==  eType)
		{
			bTypeMatch = true;
		}

		// Is this unit near enough?
		if(bTypeMatch)
		{
			CvPlot* pPlot = GC.getMap().plot(target.GetTargetX(), target.GetTargetY());
			if(!pPlot)
				continue;

			if(pNoLikeUnit && pPlot->getMaxFriendlyUnitsOfType(pNoLikeUnit)>0)
				continue;

			//Naval unit? Let's get a water plot (if ranged)
			if(!pPlot->isWater() && pUnit->getDomainType() == DOMAIN_SEA)
				pPlot = pUnit->isRanged() ? MilitaryAIHelpers::GetCoastalPlotNearPlot(pPlot) : NULL;

			//Since we redefined it above, let's check things.
			if(!pPlot)
				continue;

			//shortcut, may happen often
			if (pUnit->plot()==pPlot)
				return pPlot;
	
			//see how far the unit can go
			if (reachablePlots.empty())
			{
				SPathFinderUserData data(pUnit,0,iMaxTurns);
				data.ePathType = PT_UNIT_REACHABLE_PLOTS;
				reachablePlots = GC.GetPathFinder().GetPlotsInReach(pUnit->plot(), data);
			}

			//how long would it take to go to the target
			SMovePlot test(pPlot->GetPlotIndex());
			ReachablePlots::iterator it = reachablePlots.find(test);
			if (it==reachablePlots.end())
				continue;

			int iValue = target.GetAuxIntData() / max(1,it->iTurns);
			if( iValue > iBestValue)
			{
				pBestMovePlot = pPlot;
				iBestValue = iValue;
			}
		}
	}

	return pBestMovePlot;
}


/// Am I within range of an enemy?
bool CvTacticalAI::NearVisibleEnemy(CvUnit* pUnit, int iRange)
{
	CvUnit* pLoopUnit;
	CvCity* pLoopCity = NULL;
	int iLoop;

	// Loop through enemies
	for(int iI = 0; iI < MAX_PLAYERS; iI++)
	{
		CvPlayerAI& kPlayer = GET_PLAYER((PlayerTypes)iI);
		if(kPlayer.isAlive() && !kPlayer.isBarbarian() && atWar(kPlayer.getTeam(), m_pPlayer->getTeam()))
		{
			// Loop through their units
			for(pLoopUnit = kPlayer.firstUnit(&iLoop); pLoopUnit; pLoopUnit = kPlayer.nextUnit(&iLoop))
			{
				// Make sure this tile is visible to us
				if(pLoopUnit->plot()->isVisible(m_pPlayer->getTeam()))
				{
					// Check distance
					if(plotDistance(pLoopUnit->getX(), pLoopUnit->getY(), pUnit->getX(), pUnit->getY()) <= iRange)
					{
						return true;
					}
				}
			}

			// Loop through their cities
			for(pLoopCity = kPlayer.firstCity(&iLoop); pLoopCity != NULL; pLoopCity = kPlayer.nextCity(&iLoop))
			{
#if defined(MOD_BALANCE_CORE_MILITARY)
				// Cities can't move, revealed is good enough
				if(pLoopCity->plot()->isRevealed(m_pPlayer->getTeam()))
#else
				// Make sure this tile is visible to us
				if(pLoopCity->plot()->isVisible(m_pPlayer->getTeam()))
#endif
				{
					// Check distance
					if(plotDistance(pLoopCity->getX(), pLoopCity->getY(), pUnit->getX(), pUnit->getY()) <= iRange)
					{
						return true;
					}
				}
			}
		}
		else if(kPlayer.isBarbarian())
		{
			for(int iI = 0; iI < GC.getMap().numPlots(); iI++)
			{
				CvPlot* pLoopPlot = GC.getMap().plotByIndexUnchecked(iI);
				if(pLoopPlot == NULL)
					continue;

				if(pLoopPlot->getImprovementType() != GC.getBARBARIAN_CAMP_IMPROVEMENT() && pLoopPlot->getNumDefenders(BARBARIAN_PLAYER) <= 0)
					continue;

				if(!pLoopPlot->isVisible(m_pPlayer->getTeam()))
					continue;

				if(plotDistance(pLoopPlot->getX(), pLoopPlot->getY(), pUnit->getX(), pUnit->getY()) <= iRange)
				{
					return true;
				}
			}
		}
	}

	return false;
}


/// Uses information from m_PotentialBlocks to make final assignments to put flanking unit around a target
bool CvTacticalAI::AssignFlankingUnits(int iNumUnitsRequiredToFlank)
{
	m_TemporaryBlocks.clear();
	m_ChosenBlocks.clear();

	return ChooseRemainingAssignments(iNumUnitsRequiredToFlank, 2/*iNumUnitsAcceptable*/);
}

/// Uses information from m_PotentialBlocks to make final assignments to put deploying unit on target
bool CvTacticalAI::AssignDeployingUnits(int iNumUnitsRequiredToDeploy)
{
	bool bChoseOne = true;
	bool bRtnValue = true;

	m_TemporaryBlocks.clear();
	m_ChosenBlocks.clear();

	// Loop through potential blocks looking for assignments we MUST make (only one possibility)
	while(bChoseOne)
	{
		bChoseOne = false;
		m_NewlyChosen.clear();

		for(unsigned int iI = 0; iI < m_PotentialBlocks.size(); iI++)
		{
			if(m_PotentialBlocks[iI].GetNumChoices() == 1)
			{
				m_NewlyChosen.push_back(m_PotentialBlocks[iI]);
				bChoseOne = true;
			}
		}

		if(bChoseOne)
		{
			// Do we have the same unit in m_NewlyChosen twice?
			if(HaveDuplicateUnit())
			{
				return false;   // Not going to work
			}
			else
			{
				// Copy to final list
				for(unsigned int jJ = 0; jJ < m_NewlyChosen.size(); jJ++)
				{
					m_ChosenBlocks.push_back(m_NewlyChosen[jJ]);
				}

				RemoveChosenUnits();

				// Do we have enough units left to cover everything?
				if(NumUniqueUnitsLeft() < (iNumUnitsRequiredToDeploy - (int)m_ChosenBlocks.size()))
				{
					return false;
				}
			}
		}
	}

	// Pick closest unit for highest priority assignment until all processed
	while(m_PotentialBlocks.size() > 0)
	{
		bChoseOne = false;
		m_NewlyChosen.clear();

		m_NewlyChosen.push_back(m_PotentialBlocks[0]);
		m_ChosenBlocks.push_back(m_PotentialBlocks[0]);

		// Don't copy the other entries for this hex so pass in the number of choices here
		RemoveChosenUnits(m_PotentialBlocks[0].GetNumChoices());

		// Do we have enough units left to cover everything?
		if(NumUniqueUnitsLeft() < (iNumUnitsRequiredToDeploy - (int)m_ChosenBlocks.size()))
		{
			// Used to abort here, but better if we get the moves in we can
			bRtnValue = false;
		}
	}

	return bRtnValue;
}

/// Make and log selected movements
void CvTacticalAI::PerformChosenMoves(CvPlot* pFinalTarget)
{
	// Make moves up into hexes, starting with units already close to their final destination
	std::stable_sort(m_ChosenBlocks.begin(), m_ChosenBlocks.end(), TacticalAIHelpers::SortBlockingUnitByDistanceAscending);

	// First loop through is for units that have a unit moving into their hex.  They need to leave first!
	for(unsigned int iI = 0; iI < m_ChosenBlocks.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_ChosenBlocks[iI].GetUnitID());
		if(pUnit)
		{
			if(pUnit->plot() != m_ChosenBlocks[iI].GetPlot() && IsInChosenMoves(pUnit->plot()) && m_ChosenBlocks[iI].GetPlot()->getMaxFriendlyUnitsOfType(pUnit) == 0)
			{
				if (MoveToUsingSafeEmbark(pUnit, m_ChosenBlocks[iI].GetPlot(), false, 0))
				{
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strMsg;
						strMsg.Format("Deploying %s %d (to get out of way), To X: %d, To Y: %d, At X: %d, At Y: %d, Distance Before Move: %d",
									  pUnit->getName().GetCString(), pUnit->GetID(), m_ChosenBlocks[iI].GetPlot()->getX(), m_ChosenBlocks[iI].GetPlot()->getY(),
									  pUnit->getX(), pUnit->getY(), m_ChosenBlocks[iI].GetDistanceToTarget());
						LogTacticalMessage(strMsg);
					}

					// Use number of choices field to indicate already moved
					m_ChosenBlocks[iI].SetNumChoices(-1);
				}
			}
		}
	}

	// Second loop is for units moving into their chosen spot normally
	for(unsigned int iI = 0; iI < m_ChosenBlocks.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_ChosenBlocks[iI].GetUnitID());
		if(pUnit)
		{
			if(pUnit->plot() == m_ChosenBlocks[iI].GetPlot())
			{
				m_ChosenBlocks[iI].SetNumChoices(-1);
			}
			else
			{
				// Someone we didn't move above?
				if(m_ChosenBlocks[iI].GetNumChoices() != -1)
				{
					CvPlot* pPlotBeforeMove = pUnit->plot();
					//For naval escorts, lets try to move to the target plot.
					if(pUnit->getDomainType() == DOMAIN_SEA)
					{
						if(m_ChosenBlocks[iI].GetPlot()->getNumNavalDefenders(m_pPlayer->GetID()) > 0 || !MoveToUsingSafeEmbark(pUnit, m_ChosenBlocks[iI].GetPlot(), false, 0))
						{
							ExecuteMoveToPlotIgnoreDanger(pUnit, m_ChosenBlocks[iI].GetPlot());
						}		
					}
					else
					{
						ExecuteMoveToPlotIgnoreDanger(pUnit, m_ChosenBlocks[iI].GetPlot());
					}

					// Use number of choices field to indicate already moved
					if(pPlotBeforeMove != pUnit->plot())
					{
						if(GC.getLogging() && GC.getAILogging())
						{
							CvString strMsg;
							strMsg.Format("Deploying %s %d, To X: %d, To Y: %d, At X: %d, At Y: %d, Distance Before Move: %d",
										  pUnit->getName().GetCString(), pUnit->GetID(), m_ChosenBlocks[iI].GetPlot()->getX(), m_ChosenBlocks[iI].GetPlot()->getY(),
										  pUnit->getX(), pUnit->getY(), m_ChosenBlocks[iI].GetDistanceToTarget());
							LogTacticalMessage(strMsg);
						}
						m_ChosenBlocks[iI].SetNumChoices(-1);
					}
				}
			}
		}
	}

	// Third loop is for units we still haven't been able to move (other units must be blocking their target for this turn)
	for(unsigned int iI = 0; iI < m_ChosenBlocks.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_ChosenBlocks[iI].GetUnitID());
		if(pUnit)
		{
			// Someone we didn't move above?
			if(m_ChosenBlocks[iI].GetNumChoices() != -1)
			{
				CvPlot* pPlotBeforeMove = pUnit->plot();
				if(MoveToUsingSafeEmbark(pUnit, m_ChosenBlocks[iI].GetPlot(), false, 0))
				{
					if(pPlotBeforeMove != pUnit->plot())
					{
						if(GC.getLogging() && GC.getAILogging())
						{
							CvString strMsg;
							strMsg.Format("Deploying %s %d to space near target, Target X: %d, Target Y: %d, At X: %d, At Y: %d, Distance Before Move: %d",
								pUnit->getName().GetCString(), pUnit->GetID(), m_ChosenBlocks[iI].GetPlot()->getX(), m_ChosenBlocks[iI].GetPlot()->getY(),
								pUnit->getX(), pUnit->getY(), m_ChosenBlocks[iI].GetDistanceToTarget());
							LogTacticalMessage(strMsg);
						}
						m_ChosenBlocks[iI].SetNumChoices(-1);
					}
				}
#if defined(MOD_BALANCE_CORE)
				else if(m_ChosenBlocks[iI].GetPlot() != NULL && (!m_pPlayer->isBarbarian()))
				{
					for(int iDirectionLoop = 0; iDirectionLoop < NUM_DIRECTION_TYPES; ++iDirectionLoop)
					{
						CvPlot* pAdjacentPlot = plotDirection(m_ChosenBlocks[iI].GetPlot()->getX(), m_ChosenBlocks[iI].GetPlot()->getY(), ((DirectionTypes)iDirectionLoop));
						if (pAdjacentPlot)
						{
							if(pAdjacentPlot != NULL && pAdjacentPlot != m_ChosenBlocks[iI].GetPlot())
							{
								if(pAdjacentPlot->isCity() && pAdjacentPlot->getOwner() != pUnit->getOwner())
									continue;
								if(pUnit->getDomainType()==DOMAIN_LAND && pAdjacentPlot->isWater())
									continue;

								CvUnit* pFriendlyUnit = pAdjacentPlot->getUnitByIndex(0);
								if(pFriendlyUnit == NULL)
								{
									if (pUnit->TurnsToReachTarget(pAdjacentPlot, false /*bIgnoreUnits*/, true /*bIgnoreStacking*/, 1) <= 1)
									{
										// Move up there
										pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pAdjacentPlot->getX(), pAdjacentPlot->getY(), CvUnit::MOVEFLAG_IGNORE_DANGER);
										if(pPlotBeforeMove != pUnit->plot())
										{
											m_ChosenBlocks[iI].SetNumChoices(-1);

											if(GC.getLogging() && GC.getAILogging())
											{
												CvString strTemp;
												CvString strMsg;
												strTemp = GC.getUnitInfo(pUnit->getUnitType())->GetDescription();
												strMsg.Format("Moving %s for proper organization, x: %d, y: %d (PerformChosenMoves)", strTemp.GetCString(), pAdjacentPlot->getX(), pAdjacentPlot->getY());
												LogTacticalMessage(strMsg);
											}
										}
										break;
									}
								}
								else if(pFriendlyUnit != NULL && pFriendlyUnit->getOwner() == pUnit->getOwner() && (pFriendlyUnit->getMoves() > 0))
								{
									CvPlot* pFriendlyPlot = pFriendlyUnit->plot();
									CvPlot* pCurrentPlot = pUnit->plot();
									if(m_pPlayer->GetPlotDanger(*pFriendlyPlot) > m_pPlayer->GetPlotDanger(*pCurrentPlot) && !pUnit->isRanged() && pFriendlyUnit->isRanged())
									{
										if(pUnit->CanSwapWithUnitHere(*pFriendlyPlot))
										{
											// Move up there
											pFriendlyUnit->SetActivityType(ACTIVITY_AWAKE);
											pUnit->PushMission(CvTypes::getMISSION_SWAP_UNITS(), pFriendlyUnit->getX(), pFriendlyUnit->getY());
											if(pPlotBeforeMove != pUnit->plot())
											{
												m_ChosenBlocks[iI].SetNumChoices(-1);

												if(GC.getLogging() && GC.getAILogging())
												{
													CvString strTemp;
													CvString strTemp2;
													CvString strMsg;
													strTemp = GC.getUnitInfo(pUnit->getUnitType())->GetDescription();
													strTemp2 = GC.getUnitInfo(pFriendlyUnit->getUnitType())->GetDescription();
													strMsg.Format("Moving %s and repositioning %s in its place for proper organization, x: %d, y: %d (PerformChosenMoves)", strTemp2.GetCString(), strTemp.GetCString(), pFriendlyPlot->getX(), pFriendlyPlot->getY());
													LogTacticalMessage(strMsg);
												}
											}
											break;
										}
									}
								}
							}
						}
					}
				}
#endif
			}
		}
	}

	// Finish moves for all units
	for(unsigned int iI = 0; iI < m_ChosenBlocks.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_ChosenBlocks[iI].GetUnitID());

		//just for inspection in GUI
		pUnit->SetMissionAI(MISSIONAI_ASSAULT,pFinalTarget,NULL);

		if(!pUnit->isDelayedDeath())
		{
#if defined(MOD_BALANCE_CORE)
			TacticalAIHelpers::PerformRangedAttackWithoutMoving(pUnit);
#endif
			if (pUnit->getMoves() > 0)
			{
				if (pUnit->canPillage(pUnit->plot()) && pUnit->getDamage() > 0)
				{
					pUnit->PushMission(CvTypes::getMISSION_PILLAGE());
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strMsg;
						strMsg.Format("Already in position, will pillage with %s, X: %d, Y: %d", pUnit->getName().GetCString(),
							m_ChosenBlocks[iI].GetPlot()->getX(), m_ChosenBlocks[iI].GetPlot()->getY());
						LogTacticalMessage(strMsg);
					}

				}

#if defined(MOD_BALANCE_CORE)
				CvUnit *pEnemy = pUnit->plot()->GetAdjacentEnemyUnit(pUnit->getTeam(),pUnit->getDomainType());
				if (pEnemy && TacticalAIHelpers::IsAttackNetPositive(pUnit,pEnemy->plot()))
				{
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strMsg;
						strMsg.Format("Already in position at %d, %d, but %s found an enemy unit to attack at X: %d, Y: %d", 
							m_ChosenBlocks[iI].GetPlot()->getX(), m_ChosenBlocks[iI].GetPlot()->getY(), pUnit->getName().GetCString(), pEnemy->plot()->getX(), pEnemy->plot()->getY() );
						LogTacticalMessage(strMsg);
					}
					TacticalAIHelpers::PerformOpportunityAttack(pUnit,pEnemy->plot());
				}

				//check neighboring plots if we can steal a civilian
				CvPlot** aPlotsToCheck = GC.getMap().getNeighborsShuffled(pUnit->plot());
				for(int iCount=0; iCount<NUM_DIRECTION_TYPES; iCount++)
				{
					const CvPlot* pNeighborPlot = aPlotsToCheck[iCount];
					if (pNeighborPlot)
					{
						CvUnit* pUnit2 = pNeighborPlot->getUnitByIndex(0);
						if(pUnit2 && pUnit2->IsCivilianUnit() && GET_TEAM(pUnit2->getTeam()).isAtWar(pUnit->getTeam()))
						{
							pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pNeighborPlot->getX(), pNeighborPlot->getY());
							if(GC.getLogging() && GC.getAILogging())
							{
								CvString strMsg;
								strMsg.Format("Already in position at %d, %d, but %s found an enemy civilan to capture at X: %d, Y: %d", 
									m_ChosenBlocks[iI].GetPlot()->getX(), m_ChosenBlocks[iI].GetPlot()->getY(), pUnit->getName().GetCString(), pNeighborPlot->getX(), pNeighborPlot->getY() );
								LogTacticalMessage(strMsg);
							}
							break;
						}
					}
				}
#endif
				if(pUnit->canFortify(pUnit->plot()))
				{
					pUnit->PushMission(CvTypes::getMISSION_FORTIFY());
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strMsg;
						strMsg.Format("Already in position, will fortify with %s, X: %d, Y: %d", pUnit->getName().GetCString(),
							m_ChosenBlocks[iI].GetPlot()->getX(), m_ChosenBlocks[iI].GetPlot()->getY());
						LogTacticalMessage(strMsg);
					}
				}
#if defined(MOD_BALANCE_CORE)
				else if(pUnit->getDamage() > 0 && pUnit->canHeal(pUnit->plot()))
				{
					pUnit->PushMission(CvTypes::getMISSION_HEAL());
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strMsg;
						strMsg.Format("Already in position, will heal up with %s, X: %d, Y: %d", pUnit->getName().GetCString(),
							m_ChosenBlocks[iI].GetPlot()->getX(), m_ChosenBlocks[iI].GetPlot()->getY());
						LogTacticalMessage(strMsg);
					}
				}
#endif
				else
				{
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strMsg;
						strMsg.Format("Already in position, no move for %s, X: %d, Y: %d", pUnit->getName().GetCString(),
							m_ChosenBlocks[iI].GetPlot()->getX(), m_ChosenBlocks[iI].GetPlot()->getY());
						LogTacticalMessage(strMsg);
					}
				}
				pUnit->finishMoves();
			}
			UnitProcessed(pUnit->GetID());
		}
	}
}
/// Move a great general with an operation
void CvTacticalAI::MoveGreatGeneral(CvArmyAI* pArmyAI)
{
	for (unsigned int iI = 0; iI < m_GeneralsToMove.size(); iI++)
	{
		CvPlot* pBestPlot = NULL;
		int iBestScore = -1;
		int iMoveFlags = 0;
		CvUnit* pGeneral = m_pPlayer->getUnit(m_GeneralsToMove[iI].GetUnitID());

		if(!pGeneral)
			continue;

		//Should we consider using our heal?
		if(pGeneral->canRepairFleet(pGeneral->plot()))
		{
			int iInjured = 0;
			for (int iI = 0; iI < NUM_DIRECTION_TYPES; iI++)
			{
				CvPlot *pLoopPlot = plotDirection(pGeneral->plot()->getX(), pGeneral->plot()->getY(), ((DirectionTypes)iI));
				if (pLoopPlot != NULL && pLoopPlot->getNumUnits() > 0)
				{
					CvUnit* pUnit = pLoopPlot->getUnitByIndex(0);
					if(pUnit != NULL && pUnit->getOwner() == pGeneral->getOwner() && (pUnit->GetCurrHitPoints() <= (pUnit->GetMaxHitPoints() / 2)))
					{
						iInjured++;
					}
				}
			}
			CvUnit* pUnit = pGeneral->plot()->getUnitByIndex(0);
			if(pUnit != NULL && pUnit->getOwner() == pGeneral->getOwner() && (pUnit->GetCurrHitPoints() <= (pUnit->GetMaxHitPoints() / 2)))
			{
				iInjured++;
			}
			//A lot of injured ships around us
			if(iInjured >= 3)
			{
				pGeneral->PushMission(CvTypes::getMISSION_REPAIR_FLEET());
				if(GC.getLogging() && GC.getAILogging())
				{
					CvString strMsg;
					strMsg.Format("Expending for REPAIR FLEET ACTION - Great Admiral - %s, At X: %d, At Y: %d",
						pGeneral->getName().GetCString(), pGeneral->getX(), pGeneral->getY());
					LogTacticalMessage(strMsg);
				}
				UnitProcessed(pGeneral->GetID());
				pGeneral->finishMoves();
				continue;
			}
		}

		CvPlot* pArmyCOM = pArmyAI ? pArmyAI->GetCenterOfMass() : NULL;

		ReachablePlots reachablePlots;
		TacticalAIHelpers::GetAllPlotsInReachThisTurn(pGeneral,pGeneral->plot(),reachablePlots,true,true,false);
		for (ReachablePlots::const_iterator it=reachablePlots.begin(); it!=reachablePlots.end(); ++it)
		{
			CvPlot* pEvalPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);
			if (!pEvalPlot)
				continue;

			int iScore = ScoreGreatGeneralPlot(pGeneral, pEvalPlot);

			if (pArmyCOM && iScore>0)
			{
				//try to stay with the army center
				int iDistance = plotDistance(pEvalPlot->getX(), pEvalPlot->getY(), pArmyCOM->getX(), pArmyCOM->getY());
				iScore *= MapToPercent(iDistance,6,0);
			}

			if(iScore > iBestScore)
			{
				iBestScore = iScore;
				pBestPlot = pEvalPlot;
			}
		}

		if (pBestPlot == NULL)
		{
			//if this will be a multi-turn move, be careful
			iMoveFlags = CvUnit::MOVEFLAG_SAFE_EMBARK;

			//nothing near us, check all possible defenders
			int iHighestScore = 0;
			int iUnitLoop=0;
			for(CvUnit* pLoopUnit = m_pPlayer->firstUnit(&iUnitLoop); pLoopUnit != NULL; pLoopUnit = m_pPlayer->nextUnit(&iUnitLoop))				
			{
				if (!pLoopUnit->IsCombatUnit())
					continue;

				//only consider units from our own army
				if (pArmyAI && pLoopUnit->getArmyID()!=pArmyAI->GetID())
					continue;

				CvPlot* pLoopPlot = pLoopUnit->plot();
				int iScore = ScoreGreatGeneralPlot(pGeneral,pLoopPlot);

				//we don't want to adjust our position too much
				int iDistance = plotDistance(pLoopPlot->getX(), pLoopPlot->getY(), pGeneral->getX(), pGeneral->getY());
				iScore *= MapToPercent(iDistance,15,2);

				if (iScore>iHighestScore)
				{
					pBestPlot = pLoopPlot;
					iHighestScore = iScore;
				}
			}
		}

		//ok, one last attempt
		if(pBestPlot == NULL)
		{
			//try to go to a city
			CvCity* pCity = m_pPlayer->GetMilitaryAI()->GetMostThreatenedCity();
			if(!pCity)
				pCity = m_pPlayer->GetClosestCityByEstimatedTurns(pGeneral->plot());

			if(pCity != NULL)
			{
				pBestPlot = pCity->plot();

				if(GC.getLogging() && GC.getAILogging())
				{
					CvString strMsg;
					strMsg.Format("No good plot, so %s is moving to a threatened city, To X: %d, To Y: %d, At X: %d, At Y: %d",
									pGeneral->getName().GetCString(), pBestPlot->getX(), pBestPlot->getY(),
									pGeneral->getX(), pGeneral->getY());
					LogTacticalMessage(strMsg);
				}
			}
		}

		if(pBestPlot != NULL)
		{
			CvUnit* pDefender(NULL);
			pGeneral->GeneratePath(pBestPlot,iMoveFlags);
			CvPlot *pMovePlot = pGeneral->GetPathEndFirstTurnPlot();
			if(pMovePlot != NULL)
			{
				pDefender = pMovePlot->getBestDefender(m_pPlayer->GetID());
				if(pDefender || pGeneral->GetDanger(pMovePlot)==0)
				{
					ExecuteMoveToPlotIgnoreDanger(pGeneral, pMovePlot);
					pGeneral->finishMoves();
					UnitProcessed(pGeneral->GetID());

					//defender must stay here now, whether he wants to or not
					if(pDefender && pDefender->canMove())
					{
						TacticalAIHelpers::PerformRangedAttackWithoutMoving(pDefender);
						pDefender->finishMoves();
						UnitProcessed(pDefender->GetID());
					}

					continue;
				}
			}

			//if we get here, pathfinding failed or the target plot was not acceptable
			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strMsg;
				strMsg.Format("Deploying %s %d to assist troops, but couldn't find a safe plot. Going to flee. To X: %d, To Y: %d, At X: %d, At Y: %d",
								pGeneral->getName().GetCString(), pGeneral->GetID(), pBestPlot->getX(), pBestPlot->getY(),
								pGeneral->getX(), pGeneral->getY());
				LogTacticalMessage(strMsg);

				CvPlot* pSafestPlot = TacticalAIHelpers::FindSafestPlotInReach(pGeneral,true);
				if(pSafestPlot != NULL)
				{
					pGeneral->PushMission(CvTypes::getMISSION_MOVE_TO(), pSafestPlot->getX(), pSafestPlot->getY(), CvUnit::MOVEFLAG_IGNORE_DANGER);
					pGeneral->finishMoves();
					UnitProcessed(pGeneral->GetID());						
				}
			}
		}
	}

	return;
}

// Is one of the chosen moves to this plot?
bool CvTacticalAI::IsInChosenMoves(CvPlot* pPlot)
{
	for(unsigned int iI = 0; iI < m_ChosenBlocks.size(); iI++)
	{
		if(m_ChosenBlocks[iI].GetPlot() == pPlot)
		{
			return true;
		}
	}

	return false;
}

/// Were we forced to select the same unit to block twice?
bool CvTacticalAI::HaveDuplicateUnit()
{
	for(unsigned int iI = 0; iI < m_NewlyChosen.size()- 1; iI++)
	{
		for(unsigned int jJ = iI + 1; jJ < m_NewlyChosen.size(); jJ++)
		{
			if(m_NewlyChosen[iI].GetUnitID() == m_NewlyChosen[jJ].GetUnitID())
			{
				return true;
			}
		}
	}
	return false;
}

/// Pull the units we just assigned out of the list of potential assignments
void CvTacticalAI::RemoveChosenUnits(int iStartIndex)
{
	m_TemporaryBlocks.clear();
	m_TemporaryBlocks = m_PotentialBlocks;
	m_PotentialBlocks.clear();

	for(unsigned int iI = iStartIndex; iI < m_TemporaryBlocks.size(); iI++)
	{
		bool bCopyIt = true;

		CvBlockingUnit block = m_TemporaryBlocks[iI];

		// Loop through chosen array looking for occurrences of this unit
		for(unsigned int jJ = 0; jJ < m_NewlyChosen.size() && bCopyIt; jJ++)
		{
			if(block.GetUnitID() == m_NewlyChosen[jJ].GetUnitID())
			{
				bCopyIt = false;
			}
		}

		if(bCopyIt)
		{
			m_PotentialBlocks.push_back(block);
		}
	}

	// Rebuild number of choices
	for(unsigned int iI = 0; iI < m_PotentialBlocks.size(); iI++)
	{
		int iNumFound = 0;
		CvPlot* pPlot = m_PotentialBlocks[iI].GetPlot();

		for(unsigned int jJ = 0; jJ < m_PotentialBlocks.size(); jJ++)
		{
			if(pPlot == m_PotentialBlocks[jJ].GetPlot())
			{
				iNumFound++;
			}
		}
		m_PotentialBlocks[iI].SetNumChoices(iNumFound);
	}
}

/// How many units are left unassigned for a blocking position?
int CvTacticalAI::NumUniqueUnitsLeft()
{
	int iRtnValue = 1;
	int iCurrentID;

	if(m_PotentialBlocks.size() < 2)
	{
		return m_PotentialBlocks.size();
	}

	// Copy data over and sort it so in unit ID order
	m_TemporaryBlocks.clear();
	m_TemporaryBlocks = m_PotentialBlocks;
	std::stable_sort(m_TemporaryBlocks.begin(), m_TemporaryBlocks.end());

	iCurrentID = m_TemporaryBlocks[0].GetUnitID();
	for(unsigned int iI = 1; iI < m_TemporaryBlocks.size(); iI++)
	{
		if(m_TemporaryBlocks[iI].GetUnitID() != iCurrentID)
		{
			iRtnValue++;
			iCurrentID = m_TemporaryBlocks[iI].GetUnitID();
		}
	}

	return iRtnValue;
}

/// No clear cut blocking assignments left, have to make search possibilities and score most preferred
bool CvTacticalAI::ChooseRemainingAssignments(int iNumUnitsDesired, int iNumUnitsAcceptable)
{
	int iBestScore = 0;
	int iScore;
	int iCurrent[NUM_DIRECTION_TYPES];
	int iFirst[NUM_DIRECTION_TYPES];
	int iLast[NUM_DIRECTION_TYPES];

	int iBlocksToCreate = iNumUnitsDesired - (int)m_ChosenBlocks.size();

	if(iBlocksToCreate == 0)
	{
		return true;
	}

	FAssertMsg(iBlocksToCreate <= NUM_DIRECTION_TYPES, "More than NUM_DIRECTION_TYPES hexes to block. Will cause array overflows and performance issues!");

	if(iBlocksToCreate > 0 && iBlocksToCreate < NUM_DIRECTION_TYPES)
	{
		m_NewlyChosen.clear();

		// Set up indexes pointing to the possible choices
		int iLevel = 0;
		int iCurIndex = 0;
		while(iLevel < iBlocksToCreate)
		{
			iFirst[iLevel] = iCurIndex;
			int iNumChoices = m_PotentialBlocks[iCurIndex].GetNumChoices();

			FAssertMsg(iNumChoices > 0, "Invalid number of tactical AI move choices. Will cause array overflows and performance issues!");
			FAssertMsg(iNumChoices + iCurIndex <= (int)m_PotentialBlocks.size(), "Invalid number of tactical AI move choices. Will cause array overflows and performance issues!");

			iLast[iLevel] = iCurIndex + iNumChoices - 1;
			iCurIndex = iLast[iLevel] + 1;
			iLevel++;
		}

		for(int iI = 0; iI < iBlocksToCreate; iI++)
		{
			iCurrent[iI] = iFirst[iI];
		}

		// Loop through each possibility
		bool bDone = false;
		while(!bDone)
		{
			m_TemporaryBlocks.clear();

			// Create this choice
			for(int iI = 0; iI < iBlocksToCreate; iI++)
			{
				if(iI >= (int)m_PotentialBlocks.size())
				{
					FAssertMsg(false, "Invalid fast vector index - show Ed");
				}
				m_TemporaryBlocks.push_back(m_PotentialBlocks[iCurrent[iI]]);
			}

			iScore = ScoreAssignments(iNumUnitsDesired != iNumUnitsAcceptable /*bCanLeaveOpenings*/);

			// If best so far, save it off
			if(iScore > iBestScore)
			{
				m_NewlyChosen.clear();
				for(unsigned int iI = 0; iI < m_TemporaryBlocks.size(); iI++)
				{
					if(m_TemporaryBlocks[iI].GetDistanceToTarget() != MAX_INT)
					{
						m_NewlyChosen.push_back(m_TemporaryBlocks[iI]);
					}
				}
				iBestScore = iScore;
			}

			// Increment proper index
			bool bIncrementDone = false;
			for(int iLevelIndex = iBlocksToCreate - 1; !bIncrementDone && iLevelIndex >= 0 && iLevelIndex < NUM_DIRECTION_TYPES; iLevelIndex--)
			{
				// See if at end of line for this index
				if(iCurrent[iLevelIndex] + 1 > iLast[iLevelIndex])
				{
					// Reset to first one and keep iterating
					iCurrent[iLevelIndex] = iFirst[iLevelIndex];
				}

				else
				{
					iCurrent[iLevelIndex]++;
					bIncrementDone = true;
				}
			}

			if(!bIncrementDone)
			{
				bDone = true;
			}
		}

		// Copy final choices into output
		for(unsigned int iI = 0; iI < m_NewlyChosen.size(); iI++)
		{
			m_ChosenBlocks.push_back(m_NewlyChosen[iI]);
		}

		return (int)m_ChosenBlocks.size() >= iNumUnitsAcceptable;
	}

	return false;
}

/// Score for this set of chosen blocks in m_TemporaryBlocks (-1 if illegal)
int CvTacticalAI::ScoreAssignments(bool bCanLeaveOpenings)
{
	int iScore = 0;

	// Any assignment appear twice?
	for(unsigned int iI = 0; iI < m_TemporaryBlocks.size(); iI++)
	{
		for(unsigned int jJ = iI + 1; jJ < m_TemporaryBlocks.size(); jJ++)
		{
			if(m_TemporaryBlocks[iI].GetUnitID() == m_TemporaryBlocks[jJ].GetUnitID())
			{
				if(!bCanLeaveOpenings)
				{
					return -1;
				}
				else
				{
					// "Clear" the move with greater distance by setting it to MAX_INT distance
					if(m_TemporaryBlocks[iI].GetDistanceToTarget() < m_TemporaryBlocks[jJ].GetDistanceToTarget())
					{
						m_TemporaryBlocks[jJ].SetDistanceToTarget(MAX_INT);
					}
					else
					{
						m_TemporaryBlocks[iI].SetDistanceToTarget(MAX_INT);
					}
				}
			}
		}
	}

	// Legal, so let's score it
	for(unsigned int iI = 0; iI < m_TemporaryBlocks.size(); iI++)
	{
		if(m_TemporaryBlocks[iI].GetDistanceToTarget() != MAX_INT)
		{
			iScore += (10000 - (m_TemporaryBlocks[iI].GetDistanceToTarget() * 1000));
			iScore += m_pPlayer->getUnit(m_TemporaryBlocks[iI].GetUnitID())->GetPower();
		}
	}

	return iScore;
}

/// Pick best hexes for deployment around a target. Returns number of ranged unit plots found
int CvTacticalAI::ScoreCloseOnPlots(CvPlot* pTarget)
{
	int iRtnValue = 0;

	// We'll store the hexes we've found here
	m_TempTargets.clear();
	GetTacticalAnalysisMap()->ClearDynamicFlags();

	for (int iDX = -m_iDeployRadius; iDX <= m_iDeployRadius; iDX++)
	{
		for (int iDY = -m_iDeployRadius; iDY <= m_iDeployRadius; iDY++)
		{
			CvPlot* pPlot = plotXYWithRangeCheck(pTarget->getX(), pTarget->getY(), iDX, iDY, m_iDeployRadius);

			if (!pPlot)
				continue;

			//don't walk into traps
			int iEnemies = pPlot->GetNumEnemyUnitsAdjacent(m_pPlayer->getTeam(),pTarget->getDomain());
			int iFriendlies = pPlot->GetNumFriendlyUnitsAdjacent(m_pPlayer->getTeam(), pTarget->getDomain());
			if (iEnemies>2+iFriendlies)
				continue;

			int iPlotIndex = GC.getMap().plotNum(pPlot->getX(), pPlot->getY());
			CvTacticalAnalysisCell* pCell = GetTacticalAnalysisMap()->GetCell(iPlotIndex);

			if (pCell->CanUseForOperationGathering())
			{
				bool bChoiceBombardSpot = false;
				int iDistance = plotDistance(pTarget->getX(), pTarget->getY(), pPlot->getX(), pPlot->getY());

				//don't know the unit here, so use the generic danger calculation
				int	iScore = 10000 - m_pPlayer->GetPlotDanger(*pPlot);

				//we want to be close to the target 
				iScore -= iDistance * 50;

				// Top priority is hexes to bombard from (within range but not adjacent)
				pCell->SetTargetDistance(iDistance);
				if (iDistance > 1 && pPlot->GetNumEnemyUnitsAdjacent(m_pPlayer->getTeam(), pPlot->getDomain(), NULL, false)==0)
					bChoiceBombardSpot = true;

				if (pPlot->canSeePlot(pTarget,m_pPlayer->getTeam(),iDistance,NO_DIRECTION))
					pCell->SetHasLOS(true);

				pCell->SetSafeForDeployment(bChoiceBombardSpot);
				pCell->SetDeploymentScore(iScore);

				// Save this in our list of potential targets
				CvTacticalTarget target;
				target.SetTargetX(pPlot->getX());
				target.SetTargetY(pPlot->getY());
				target.SetAuxIntData(iScore);

				// A bit of a hack -- use high priority targets to indicate good plots for ranged units
				if (bChoiceBombardSpot)
				{
					target.SetTargetType(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT);
					iRtnValue++;
				}
				else
				{
					target.SetTargetType(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT);
				}

				m_TempTargets.push_back(target);
			}
		}
	}
	return iRtnValue;
}

int CvTacticalAI::ScoreHedgehogPlots(CvPlot* pTarget)
{
	int iRtnValue = 0;

	// We'll store the hexes we've found here
	m_TempTargets.clear();

	for (int iDX = -m_iDeployRadius; iDX <= m_iDeployRadius; iDX++)
	{
		for (int iDY = -m_iDeployRadius; iDY <= m_iDeployRadius; iDY++)
		{
			CvPlot* pPlot = plotXYWithRangeCheck(pTarget->getX(), pTarget->getY(), iDX, iDY, m_iDeployRadius);

			if (!pPlot)
				continue;

			//exclude plots in the wrong area
			if (pTarget->isCity())
			{
				CvCity* pCity = pTarget->getPlotCity();
				if (!pCity->isMatchingArea(pPlot))
					continue;
			}

			int iPlotIndex = GC.getMap().plotNum(pPlot->getX(), pPlot->getY());
			CvTacticalAnalysisCell* pCell = GetTacticalAnalysisMap()->GetCell(iPlotIndex);

			if (pCell->CanUseForOperationGathering())
			{
				bool bChoiceBombardSpot = false;
				int iDistance = plotDistance(pTarget->getX(), pTarget->getY(), pPlot->getX(), pPlot->getY());

				//don't know the unit here, so use the generic danger calculation
				int	iScore = 10000 - m_pPlayer->GetPlotDanger(*pPlot);
				
				//we want to be close to the target 
				iScore -= iDistance * 100;

				bool bMyCity = (pPlot->isCity() && pPlot->getOwner()==m_pPlayer->GetID());
				if (bMyCity)
					iScore += 1000;

				//we want to be able to attack a lot of plots
				if (pPlot->GetNumEnemyUnitsAdjacent(m_pPlayer->getTeam(), pPlot->getDomain())>0 && !bMyCity)
				{
					//good plot for melee
					iScore += pPlot->countPassableNeighbors(pPlot->isWater(), NULL) * 30;
				}
				else
				{
					//good plot for ranged
					bChoiceBombardSpot = true;

					std::vector<CvPlot*> vAttackablePlots;
					pPlot->GetPlotsAtRangeX(2, true, true, vAttackablePlots);
					iScore += vAttackablePlots.size() * 30;
				}

				pCell->SetSafeForDeployment(bChoiceBombardSpot);
				pCell->SetDeploymentScore(iScore);
				pCell->SetTargetDistance(iDistance);

				// Save this in our list of potential targets
				CvTacticalTarget target;
				target.SetTargetX(pPlot->getX());
				target.SetTargetY(pPlot->getY());
				target.SetAuxIntData(iScore);

				// A bit of a hack -- use high priority targets to indicate good plots for ranged units
				if (bChoiceBombardSpot)
				{
					target.SetTargetType(AI_TACTICAL_TARGET_HIGH_PRIORITY_UNIT);
					iRtnValue++;
				}
				else
				{
					target.SetTargetType(AI_TACTICAL_TARGET_LOW_PRIORITY_UNIT);
				}

				m_TempTargets.push_back(target);
			}
		}
	}
	return iRtnValue;
}

/// Support function to pick best hex for a great general to move to
int CvTacticalAI::ScoreGreatGeneralPlot(CvUnit* pGeneral, CvPlot* pLoopPlot)
{
	int iAura = 0;

	if(pLoopPlot == NULL || !pLoopPlot->isValidMovePlot(m_pPlayer->GetID()) || !pLoopPlot->isRevealed(m_pPlayer->getTeam()))
		return 0;

	if(pLoopPlot->isWater() && pGeneral->getDomainType() == DOMAIN_LAND)
		return 0;

	if(!pLoopPlot->isWater() && pGeneral->getDomainType() == DOMAIN_SEA)
		return 0;

	if(!pGeneral->IsCityAttackSupport() && pGeneral->IsNearGreatGeneral(iAura, pLoopPlot, pGeneral)) //near another general
		return 0;

	if(pGeneral->IsCityAttackSupport() && pGeneral->IsNearCityAttackSupport(pLoopPlot, pGeneral)) // Near another sapper
		return 0;

	const CvUnit* pDefender = pLoopPlot->getBestDefender(m_pPlayer->GetID());
	CvCity* pPlotCity = pLoopPlot->getPlotCity();
	if ( (!pDefender || pDefender->isProjectedToDieNextTurn()) && (!pPlotCity || pPlotCity->isInDangerOfFalling()) )
		return 0;

	//avoid the front line
	int iBaseMultiplier = 3;
	if(!pGeneral->IsCityAttackSupport())
	{
		if(pLoopPlot->GetNumEnemyUnitsAdjacent(pGeneral->getTeam(),pGeneral->getDomainType()) > 0 && !pLoopPlot->isCity() )
			iBaseMultiplier = 1;
	}

	int iTotalScore = 0;
	CvPlot** aPlotsToCheck = GC.getMap().getNeighborsUnchecked(pLoopPlot);
	for(int iCount=0; iCount < NUM_DIRECTION_TYPES; iCount++)
	{
		const CvPlot* pNeighborPlot = aPlotsToCheck[iCount];
		if(!pNeighborPlot)
			continue;

		CvUnit* pSupportedUnit = pNeighborPlot->getBestDefender(m_pPlayer->GetID());

		int iMultiplier = iBaseMultiplier;
		if (pSupportedUnit && (pSupportedUnit->getDomainType() == pGeneral->getDomainType()))
		{
			CvTacticalAnalysisCell* pCell = GetTacticalAnalysisMap()->GetCell( pSupportedUnit->plot()->GetPlotIndex() );
			bool bTempZone = false;
			if(pCell)
			{
				CvTacticalDominanceZone* pZone = GetTacticalAnalysisMap()->GetZoneByID( pCell->GetDominanceZone() );
				
				if (pZone && pZone->GetZoneCity()!= NULL && pZone->GetOwner() != m_pPlayer->GetID())
				{
					CvCity *pClosestCity = pZone->GetZoneCity();

					// Always push again for a few turns after each operation reaches destination
					if (pClosestCity)
					{
						bTempZone = m_pPlayer->GetTacticalAI()->IsTemporaryZoneCity(pClosestCity);
					}
				}
			}
			if(pGeneral->IsCityAttackSupport())
			{
				if(pSupportedUnit->IsEnemyCityAdjacent())
				{
					iMultiplier *= 10;
				}
				if(!pSupportedUnit->isRanged())
				{
					iMultiplier *= 5;
				}
				//recent operation
				if(pSupportedUnit->IsRecentlyDeployedFromOperation())
					iMultiplier++;

				//many friendly units around
				if(pSupportedUnit->GetNumSpecificPlayerUnitsAdjacent(pDefender)>1)
					iMultiplier++;
			}
			else
			{
				//if enemies are nearby the general is more useful
				if(pSupportedUnit->GetNumEnemyUnitsAdjacent() > 0)
					iMultiplier++;
				if(pSupportedUnit->IsEnemyCityAdjacent())
					iMultiplier++;
				//recent operation
				if(pSupportedUnit->IsRecentlyDeployedFromOperation())
					iMultiplier++;
				//many friendly units around
				if(pSupportedUnit->GetNumSpecificPlayerUnitsAdjacent(pDefender)>2)
					iMultiplier++;
			}
			if(bTempZone)
			{
				iMultiplier *= 10;
			}

			iTotalScore += iMultiplier*pSupportedUnit->GetPower();
			//Same army? Emphasize.
			if(pGeneral->getArmyID() != -1)
			{
				if(pSupportedUnit->getArmyID() == pGeneral->getArmyID())
				{
					iTotalScore *= 10;
				}
				else
				{
					iTotalScore /= 5;
				}
			}
		}
	}

	if(!pGeneral->IsCityAttackSupport() && pDefender)
	{
		int iDefenderPower = pDefender->GetPower();
		if(pLoopPlot->isCity())
			iDefenderPower *= 2;
		iTotalScore += iDefenderPower;
	}

	CvCity* pClosestEnemyCity = m_pPlayer->GetTacticalAI()->GetNearestTargetCity(pGeneral->plot());
	if(pClosestEnemyCity != NULL)
	{
		if(GC.getLogging() && GC.getAILogging())
		{
			CvString strLogString;
			strLogString.Format("Found a nearby city target for our General: X: %d, Y: %d", pClosestEnemyCity->getX(), pClosestEnemyCity->getY());
			LogTacticalMessage(strLogString);
		}
		if (pDefender)
			iTotalScore += (250 - (plotDistance(pDefender->getX(), pDefender->getY(), pClosestEnemyCity->getX(), pClosestEnemyCity->getY()) * 3));
	}

	return iTotalScore;
}


/// Remove a unit that we've allocated from list of units to move this turn
void CvTacticalAI::UnitProcessed(int iID, bool bMarkTacticalMap)
{
	CvUnit* pUnit;
	int iPlotIndex;
	CvTacticalAnalysisCell* pCell;

	m_CurrentTurnUnits.remove(iID);
	pUnit = m_pPlayer->getUnit(iID);

#if defined(MOD_BALANCE_CORE)
	if (!pUnit)
		return;
#endif

	CvAssert(pUnit);
	pUnit->SetTurnProcessed(true);

	if(bMarkTacticalMap)
	{
		CvTacticalAnalysisMap* pMap = GetTacticalAnalysisMap();
		if(pMap->IsUpToDate())
		{
			iPlotIndex = GC.getMap().plotNum(pUnit->getX(), pUnit->getY());
			pCell = pMap->GetCell(iPlotIndex);
			pCell->SetFriendlyTurnEndTile(true);
		}
	}
}

/// Do we want to process moves for this dominance zone?
bool CvTacticalAI::UseThisDominanceZone(CvTacticalDominanceZone* pZone)
{
	bool bIsOurCapital = false;
	bool bTempZone = false;
	int iCityID = -1;
	if(pZone->GetZoneCity() != NULL)
	{
		iCityID = pZone->GetZoneCity()->GetID();
		bIsOurCapital = (pZone->GetZoneCity() == m_pPlayer->getCapitalCity());
		bTempZone = IsTemporaryZoneCity(pZone->GetZoneCity());
	}
	bool bWeHaveUnitsNearEnemy = pZone->GetFriendlyUnitCount() > 0 && pZone->GetTerritoryType() == TACTICAL_TERRITORY_ENEMY;
	bool bTheyHaveUnitsNearUs = pZone->GetDominanceFlag() != TACTICAL_DOMINANCE_NO_UNITS_VISIBLE && pZone->GetTerritoryType() == TACTICAL_TERRITORY_FRIENDLY;
	bool bBothHaveUnits = pZone->GetFriendlyUnitCount() > 0 && pZone->GetEnemyUnitCount() > 0;

	return (bTempZone || bIsOurCapital || bWeHaveUnitsNearEnemy || bTheyHaveUnitsNearUs || bBothHaveUnits);
}


/// Is this civilian target of the highest priority?
bool CvTacticalAI::IsVeryHighPriorityCivilianTarget(CvTacticalTarget* pTarget)
{
	bool bRtnValue = false;
	CvUnit* pUnit = (CvUnit*)pTarget->GetAuxData();
	if(pUnit)
	{
#if defined(MOD_BALANCE_CORE_MILITARY)
		if(pUnit->IsGreatGeneral() || pUnit->IsGreatAdmiral() || pUnit->IsCityAttackSupport())
#else
		if(pUnit->AI_getUnitAIType() == UNITAI_GENERAL || pUnit->AI_getUnitAIType() == UNITAI_ADMIRAL)
#endif
		{
			bRtnValue = true;
		}
	}
	return bRtnValue;
}

/// Is this civilian target of high priority?
bool CvTacticalAI::IsHighPriorityCivilianTarget(CvTacticalTarget* pTarget)
{
	bool bRtnValue = false;
	CvUnit* pUnit = (CvUnit*)pTarget->GetAuxData();
	if(pUnit)
	{
		CvUnitEntry* pkUnitInfo = GC.getUnitInfo(pUnit->getUnitType());
		int iEstimatedEndTurn = GC.getGame().getEstimateEndTurn();
		if(pkUnitInfo)
		{
			// Priorities defined in XML
			if(pkUnitInfo->GetCivilianAttackPriority() == CIVILIAN_ATTACK_PRIORITY_HIGH)
			{
				bRtnValue = true;
			}
			else if(pkUnitInfo->GetCivilianAttackPriority() == CIVILIAN_ATTACK_PRIORITY_HIGH_EARLY_GAME_ONLY)
			{
				if(GC.getGame().getGameTurn() < (iEstimatedEndTurn / 3))
				{
					bRtnValue =  true;
				}
			}
			if(!bRtnValue && pUnit->AI_getUnitAIType() == UNITAI_SETTLE)
			{
				//Settlers
				if(GET_PLAYER(pUnit->getOwner()).getNumCities() < 5)  //small player?
				{
					bRtnValue = true;
				}
				else if(GC.getGame().getGameTurn() < (iEstimatedEndTurn / 3))  //early game?
				{
					bRtnValue =  true;
				}
				else if(GET_PLAYER(pUnit->getOwner()).IsCramped())  //player really needs to expand?
				{
					bRtnValue = true;
				}
			}
			if(!bRtnValue && m_pPlayer->isBarbarian())
			{
				bRtnValue = true; //always high priority for barbs
			}
#if defined(MOD_BALANCE_CORE_MILITARY)
			if (MOD_BALANCE_CORE_MILITARY) 
			{
				//Let's not be too aggressive around our own cities.
				if(!pUnit->isBarbarian() && pUnit->plot()->GetAdjacentCity() != NULL)
				{
					if(pUnit->plot()->GetAdjacentCity()->getOwner() == m_pPlayer->GetID())
					{
						bRtnValue = false;
					}
				}
				if(!pUnit->isBarbarian())
				{
					//Is he trying to lure us to come out of a garrison?
					for (int iI = 0; iI < NUM_DIRECTION_TYPES; iI++)
					{
						CvPlot *pLoopPlot = plotDirection(pUnit->plot()->getX(), pUnit->plot()->getY(), ((DirectionTypes)iI));
						if (pLoopPlot != NULL)
						{
							if((pLoopPlot->defenseModifier(m_pPlayer->getTeam(), false, false) > 10) && pLoopPlot->getNumUnits() > 0)
							{
								if(pLoopPlot->getUnitByIndex(0)->getOwner() == m_pPlayer->GetID())
								{
									bRtnValue = false;
								}
							}
						}
					}
				}
			}
#endif
		}
	}
	return bRtnValue;
}

/// Is this civilian target of medium priority?
bool CvTacticalAI::IsMediumPriorityCivilianTarget(CvTacticalTarget* pTarget)
{
	bool bRtnValue = false;
	CvUnit* pUnit = (CvUnit*)pTarget->GetAuxData();
	if(pUnit)
	{
		int iEstimatedEndTurn = GC.getGame().getEstimateEndTurn();
		if(pUnit->isEmbarked() && !pUnit->IsCombatUnit())  //embarked civilians
		{
			bRtnValue = true;
		}
		else if(pUnit->AI_getUnitAIType() == UNITAI_SETTLE)
		{
			if(GC.getGame().getGameTurn() >= (iEstimatedEndTurn / 3))
			{
				bRtnValue =  true;
			}
		}
		else if(pUnit->AI_getUnitAIType() == UNITAI_WORKER)
		{
			if(GC.getGame().getGameTurn() < (iEstimatedEndTurn / 3))  //early game?
			{
				bRtnValue =  true;
			}
		}
#if defined(MOD_BALANCE_CORE_MILITARY)
			if (MOD_BALANCE_CORE_MILITARY) 
			{
				//Let's not be too aggressive around our own cities.
				if(pUnit->plot()->GetAdjacentCity() != NULL)
				{
					if(pUnit->plot()->GetAdjacentCity()->getOwner() == m_pPlayer->GetID())
					{
						bRtnValue = false;
					}
				}
				//Is he trying to lure us to come out of a garrison?
				for (int iI = 0; iI < NUM_DIRECTION_TYPES; iI++)
				{
					CvPlot *pLoopPlot = plotDirection(pUnit->plot()->getX(), pUnit->plot()->getY(), ((DirectionTypes)iI));
					if (pLoopPlot != NULL)
					{
						if((pLoopPlot->defenseModifier(m_pPlayer->getTeam(), false, false) > 15) && pLoopPlot->getNumUnits() > 0)
						{
							if(pLoopPlot->getUnitByIndex(0)->getOwner() == m_pPlayer->GetID())
							{
								bRtnValue = false;
							}
						}
					}
				}
			}
#endif
	}
	return bRtnValue;
}

/// Log current status of the operation
void CvTacticalAI::LogTacticalMessage(const CvString& strMsg, bool bSkipLogDominanceZone)
{
	if(GC.getLogging() && GC.getAILogging())
	{
		CvString strOutBuf;
		CvString strBaseString;
		CvString strPlayerName;
		FILogFile* pLog;

		strPlayerName = m_pPlayer->getCivilizationShortDescription();
		pLog = LOGFILEMGR.GetLog(GetLogFileName(strPlayerName), FILogFile::kDontTimeStamp | FILogFile::kDontFlushOnWrite );

		// Get the leading info for this line
		strBaseString.Format("%03d, ", GC.getGame().getElapsedGameTurns());
		strBaseString += strPlayerName + ", ";
		if(!bSkipLogDominanceZone)
		{
			CvString strTemp = "no zone, ";
			CvTacticalDominanceZone* pZone = GetTacticalAnalysisMap()->GetZoneByIndex(m_iCurrentZoneIndex);
			if(pZone != NULL)
			{
				strTemp.Format("Zone ID: %d, ", pZone->GetDominanceZoneID());
			}
			strBaseString += strTemp;
		}

		strOutBuf = strBaseString + strMsg;
		pLog->Msg(strOutBuf);
	}
}

/// Build log filename
CvString CvTacticalAI::GetLogFileName(CvString& playerName) const
{
	CvString strLogName;

	// Open the log file
	if(GC.getPlayerAndCityAILogSplit())
	{
		strLogName = "PlayerTacticalAILog_" + playerName + ".csv";
	}
	else
	{
		strLogName = "PlayerTacticalAILog.csv";
	}

	return strLogName;
}

/// Build log filename
CvString CvTacticalAI::GetTacticalMissionName(AITacticalMission eMission) const
{
	CvString strMissionName;

	// Open the log file
	switch(eMission)
	{
	case AI_TACTICAL_MISSION_NONE:
		strMissionName = "No Tactic";
		break;
	case AI_TACTICAL_MISSION_ATTACK_STATIONARY_TARGET:
		strMissionName = "Attack Stationary Target";
		break;
	case AI_TACTICAL_MISSION_PILLAGE_ENEMY_IMPROVEMENTS:
		strMissionName = "Pillage Enemy Improvements";
	}

	return strMissionName;
}

FDataStream& operator<<(FDataStream& saveTo, const AITacticalMission& readFrom)
{
	int v = static_cast<int>(readFrom);
	saveTo << v;
	return saveTo;
}

FDataStream& operator>>(FDataStream& loadFrom, AITacticalMission& writeTo)
{
	int v;
	loadFrom >> v;
	writeTo = static_cast<AITacticalMission>(v);
	return loadFrom;
}

// HELPER FUNCTIONS

/// Sort CvBlockingUnit by a non-standard criteria
bool TacticalAIHelpers::SortBlockingUnitByDistanceAscending(const CvBlockingUnit& obj1, const CvBlockingUnit& obj2)
{
	return obj1.GetDistanceToTarget() < obj2.GetDistanceToTarget();
}

bool TacticalAIHelpers::SortByExpectedTargetDamageDescending(const CvTacticalUnit& obj1, const CvTacticalUnit& obj2)
{
	return obj1.GetExpectedTargetDamage()*2-obj1.GetExpectedSelfDamage() > obj2.GetExpectedTargetDamage()*2-obj2.GetExpectedSelfDamage();
}

bool CvTacticalAI::IsUnitHealing(int iUnitID) const
{
	return m_HealingUnits.find(iUnitID) != m_HealingUnits.end();
}

//	--------------------------------------------------------------------------------
/// get all tiles a unit can reach in one turn - this ignores friendly stacking. need to check the result by hand!
int TacticalAIHelpers::GetAllPlotsInReachThisTurn(const CvUnit* pUnit, const CvPlot* pStartPlot, ReachablePlots& resultSet, 
	bool bCheckTerritory, bool bCheckZOC, bool bAllowEmbark, int iMinMovesLeft)
{
	if (!pUnit || !pStartPlot)
		return false;

	resultSet.clear();

	int iFlags = CvUnit::MOVEFLAG_IGNORE_STACKING;

	if (!bCheckTerritory)
		iFlags |= CvUnit::MOVEFLAG_IGNORE_RIGHT_OF_PASSAGE;
	if (!bCheckZOC)
		iFlags |= CvUnit::MOVEFLAG_IGNORE_ZOC;
	if (!bAllowEmbark)
		iFlags |= CvUnit::MOVEFLAG_NO_EMBARK;

	SPathFinderUserData data(pUnit,iFlags,1);
	data.ePathType = PT_UNIT_REACHABLE_PLOTS;
	data.iMinMovesLeft = iMinMovesLeft;
	resultSet = GC.GetPathFinder().GetPlotsInReach(pStartPlot->getX(), pStartPlot->getY(), data);

	return (int)resultSet.size();
}

int TacticalAIHelpers::GetPlotsUnderRangedAttackFrom(const CvUnit* pUnit, const CvPlot* pBasePlot, std::set<int>& resultSet)
{
	if (!pUnit || !pBasePlot)
		return false;

	resultSet.clear();
	int iRange = pUnit->GetRange();

	for(int iX = -iRange; iX <= iRange; iX++)
	{
		for(int iY = -iRange; iY <= iRange; iY++)
		{
			CvPlot* pLoopPlot = plotXYWithRangeCheck(pBasePlot->getX(), pBasePlot->getY(), iX, iY, iRange);
			if (pLoopPlot && pUnit->canEverRangeStrikeAt(pLoopPlot->getX(), pLoopPlot->getY(), pBasePlot))
				resultSet.insert(pLoopPlot->GetPlotIndex());
		}
	}

	return (int)resultSet.size();
}

int TacticalAIHelpers::GetPlotsUnderRangedAttackFrom(const CvUnit* pUnit, ReachablePlots& basePlots, std::set<int>& resultSet)
{
	if (!pUnit || !pUnit->IsCanAttackRanged())
		return false;

	resultSet.clear();
	int iRange = pUnit->GetRange();

	for (ReachablePlots::iterator base=basePlots.begin(); base!=basePlots.end(); ++base)
	{
		CvPlot* pBasePlot = GC.getMap().plotByIndexUnchecked( base->iPlotIndex );
		int iPlotMoves = base->iMovesLeft;

		if (pUnit->isMustSetUpToRangedAttack())
			iPlotMoves -= GC.getMOVE_DENOMINATOR();

		if (iPlotMoves<=0)
			continue;

		//we have enough moves for an attack ...
		for(int iX = -iRange; iX <= iRange; iX++)
		{
			for(int iY = -iRange; iY <= iRange; iY++)
			{
				CvPlot* pLoopPlot = plotXYWithRangeCheck(pBasePlot->getX(), pBasePlot->getY(), iX, iY, iRange);

				//if the plot is already know to be attackable, don't check again
				//the reverse is not true: from another base plot the attack might work!
				if (!pLoopPlot || resultSet.find(pLoopPlot->GetPlotIndex())!=resultSet.end())
					continue;

				if (pUnit->canEverRangeStrikeAt(pLoopPlot->getX(), pLoopPlot->getY(), pBasePlot))
					resultSet.insert(pLoopPlot->GetPlotIndex());
			}
		}
	}

	return (int)resultSet.size();
}

bool TacticalAIHelpers::IsAttackNetPositive(CvUnit* pUnit, const CvPlot* pTargetPlot)
{
	if (!pUnit || !pTargetPlot)
		return false;

	//target can be city or a unit
	CvCity* pTargetCity = pTargetPlot->getPlotCity();
	CvUnit* pTargetUnit = pTargetPlot->getVisibleEnemyDefender( pUnit->getOwner() );

	int iDamageDealt = 0, iDamageReceived = 1;
	if (pTargetCity)
		iDamageDealt = GetSimulatedDamageFromAttackOnCity(pTargetCity,pUnit,iDamageReceived);
	else if (pTargetUnit)
		iDamageDealt = GetSimulatedDamageFromAttackOnUnit(pTargetUnit,pUnit,iDamageReceived);
	
	return (iDamageDealt>iDamageReceived);
}

//attack the target plot with the given unit if possible this turn
bool TacticalAIHelpers::PerformOpportunityAttack(CvUnit* pUnit, const CvPlot* pTargetPlot)
{
	if (!pUnit || !pTargetPlot)
		return false;

	//target can be city or a unit
	CvCity* pTargetCity = pTargetPlot->getPlotCity();
	CvUnit* pTargetUnit = pTargetPlot->getVisibleEnemyDefender( pUnit->getOwner() );

	//cannot attack if the city changed owner
	CvPlayer& kPlayer = GET_PLAYER(pUnit->getOwner());
	if (pTargetCity && kPlayer.IsAtPeaceWith(pTargetCity->getOwner()))
		return false;

	//cannot attack if unit has been killed
	if (pTargetUnit && (pTargetUnit->isDelayedDeath() || GET_PLAYER(pUnit->getOwner()).IsAtPeaceWith(pTargetUnit->getOwner())))
		return false;

	//ranged attack. 
	if (pUnit->isRanged() && pUnit->getDomainType()!=DOMAIN_AIR)
	{
		std::vector<SPlotWithScore> vBasePlots;

		//for ranged every plot we can enter with movement left is a base for attack
		int iMinMovesLeft = pUnit->isMustSetUpToRangedAttack() ? GC.getMOVE_DENOMINATOR()+1 : 1;

		ReachablePlots reachablePlots;
		TacticalAIHelpers::GetAllPlotsInReachThisTurn(pUnit,pUnit->plot(),reachablePlots,true,true,false,iMinMovesLeft);
		for (ReachablePlots::iterator it=reachablePlots.begin(); it!=reachablePlots.end(); ++it)
		{
			CvPlot* pCandidate = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);

			//valid base plot for attack?
			if (!pUnit->canEverRangeStrikeAt(pTargetPlot->getX(), pTargetPlot->getY(), pCandidate))
				continue;

			if (!pUnit->canMoveInto(*pCandidate,CvUnit::MOVEFLAG_DESTINATION ))
				continue;

			vBasePlots.push_back( SPlotWithScore(pCandidate,kPlayer.GetPlotDanger(*pCandidate,pUnit)) );
		}

		//cannot attack
		if (vBasePlots.empty())
			return false;

		//lowest danger first
		std::stable_sort(vBasePlots.begin(), vBasePlots.end());

		pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(),vBasePlots[0].pPlot->getX(),vBasePlots[0].pPlot->getY(), CvUnit::MOVEFLAG_IGNORE_DANGER);

		if (pUnit->plot()==vBasePlots[0].pPlot)
		{
			if (pUnit->canSetUpForRangedAttack(vBasePlots[0].pPlot))
				pUnit->setSetUpForRangedAttack(true);
			pUnit->PushMission(CvTypes::getMISSION_RANGE_ATTACK(),pTargetPlot->getX(), pTargetPlot->getY());
			return true;
		}
		else
			return false;
	}
	else //melee
	{
		int iTurns = pUnit->TurnsToReachTarget( pTargetPlot, CvUnit::MOVEFLAG_ATTACK, 1);
		if (iTurns <=1 )
		{
			pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(),pTargetPlot->getX(), pTargetPlot->getY());
			return true;
		}
		else
			return false;
	}
}

//see if we can hit anything from our current plot - without moving
bool TacticalAIHelpers::PerformRangedAttackWithoutMoving(CvUnit* pUnit)
{
	if (!pUnit || !pUnit->IsCanAttackRanged() || pUnit->isMustSetUpToRangedAttack() || pUnit->getMoves()==0 )
		return false;

	int iRange = pUnit->GetRange();
	CvPlot* pBasePlot = pUnit->plot();
	bool bIsAirUnit = pUnit->getDomainType()==DOMAIN_AIR;

	int iMaxDamage = 0;
	CvPlot* pBestTarget = NULL;

	for(int iX = -iRange; iX <= iRange; iX++)
	{
		for(int iY = -iRange; iY <= iRange; iY++)
		{
			CvPlot* pLoopPlot = plotXYWithRangeCheck(pBasePlot->getX(), pBasePlot->getY(), iX, iY, iRange);
			if (!pLoopPlot)
				continue;

			if (pLoopPlot->isCity() && atWar( GET_PLAYER(pUnit->getOwner()).getTeam(), GET_PLAYER(pLoopPlot->getOwner()).getTeam() ) )
			{
				//assume low damage so that we prefer attacking units if present
				if (iMaxDamage==0)
					pBestTarget = pLoopPlot;
				continue;
			}

			CvUnit* pOtherUnit = pLoopPlot->getBestDefender(NO_PLAYER, pUnit->getOwner(), pUnit, true /*testWar*/);

			//don't blindly attack the first one we find, check how much damage we can do
			if (pOtherUnit && !pOtherUnit->isDelayedDeath() && pUnit->canEverRangeStrikeAt(pLoopPlot->getX(), pLoopPlot->getY()))
			{
				int iDamage = bIsAirUnit ? pUnit->GetAirCombatDamage(pOtherUnit,NULL,false) : pUnit->GetRangeCombatDamage(pOtherUnit,NULL,false);
				if (iDamage>iMaxDamage)
				{
					pBestTarget = pLoopPlot;
					iMaxDamage = iDamage;
				}
			}
		}
	}

	if (pBestTarget)
	{
		pUnit->PushMission(bIsAirUnit ? CvTypes::getMISSION_MOVE_TO() : CvTypes::getMISSION_RANGE_ATTACK(), pBestTarget->getX(), pBestTarget->getY());
		return true;
	}

	return false;
}

///Returns false if insufficient free plots around the target
bool TacticalAIHelpers::CountDeploymentPlots(TeamTypes eTeam, const CvPlot* pTarget, int iNumUnits, int iRange)
{
	int iDX, iDY;
	CvPlot* pPlot;
	int iNumDeployPlotsFound = 0;

	for(iDX = -(iRange); iDX <= iRange; iDX++)
	{
		for(iDY = -(iRange); iDY <= iRange; iDY++)
		{
			pPlot = plotXY(pTarget->getX(), pTarget->getY(), iDX, iDY);
			if(pPlot != NULL)
			{
				int iPlotDistance = plotDistance(pPlot->getX(), pPlot->getY(), pTarget->getX(), pTarget->getY());
				if(iPlotDistance <= iRange)
				{
					if(!pPlot->isValidMovePlot(NO_PLAYER))
					{
						continue;
					}
					if(!GET_TEAM(eTeam).canEmbark() && (pPlot->isShallowWater() || pPlot->isWater()))
					{
						continue;
					}
					else if(!GET_TEAM(eTeam).canEmbarkAllWaterPassage() && (pPlot->isWater()))
					{
						continue;
					}
					if(pPlot->getNumUnits() > 0)
					{
						CvUnit* pUnit = pPlot->getUnitByIndex(0);
						if(pUnit && pUnit->IsCombatUnit())
						{
							continue;
						}
					}

					iNumDeployPlotsFound++;
				}
			}
		}
	}

	// Make sure we found enough
	if(iNumDeployPlotsFound < iNumUnits)
	{
		return false;
	}

	return true;
}

CvPlot* TacticalAIHelpers::FindSafestPlotInReach(const CvUnit* pUnit, bool bAllowEmbark)
{
	CvWeightedVector<CvPlot*, 8, true> aCityList;
	CvWeightedVector<CvPlot*, 8, true> aZeroDangerList;
	CvWeightedVector<CvPlot*, 8, true> aCoverList;
	CvWeightedVector<CvPlot*, 8, true> aDangerList;

	ReachablePlots eligiblePlots;
	TacticalAIHelpers::GetAllPlotsInReachThisTurn(pUnit, pUnit->plot(), eligiblePlots, true, true, true);

	for (ReachablePlots::iterator it=eligiblePlots.begin(); it!=eligiblePlots.end(); ++it)
	{
		CvPlot* pPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);
		if(!pUnit->canMoveInto(*pPlot,CvUnit::MOVEFLAG_DESTINATION ))
			continue;

		//   prefer being in a city with the lowest danger value
		//   prefer being in a plot with no danger value
		//   prefer being under a unit with the lowest danger value
		//   prefer being in your own territory with the lowest danger value
		//   prefer the lowest danger value

		CvPlayer& kPlayer = GET_PLAYER(pUnit->getOwner());
		int iDanger = kPlayer.GetPlotDanger(*pPlot, pUnit);

		int iCityDistance = kPlayer.GetCityDistanceInEstimatedTurns(pPlot);
		//when in doubt, prefer to move
		if (pUnit->atPlot(*pPlot))
			iCityDistance++;

		bool bIsZeroDanger = (iDanger <= 0);
		bool bIsInCity = pPlot->isFriendlyCity(*pUnit, false);
		bool bIsInCover = (pPlot->getNumDefenders(pUnit->getOwner()) > 0) && !pUnit->IsCanDefend(pPlot); // only move to cover if I'm defenseless here
		bool bIsInTerritory = (pPlot->getTeam() == kPlayer.getTeam());

		bool bWrongDomain = pPlot->needsEmbarkation(pUnit);
		bool bWouldEmbark = bWrongDomain && !pUnit->isEmbarked();

		//avoid overflow further down and useful handling for civilians
		if (iDanger == INT_MAX)
			iDanger = 10000;

		//we can't heal after moving and lose fortification bonus, so the current plot gets a bonus
		if (pPlot != pUnit->plot() && pUnit->canHeal(pUnit->plot()))
			iDanger++;

		//try to hide - if there are few enemy units, this might be a tiebreaker
		//this is cheating a bit, really we need to check if the plot is visible for the enemy units visible to us
		if (pPlot->isVisibleToEnemy(pUnit->getOwner()))
			iDanger+=10;

		//use city distance as tiebreaker
		iDanger = iDanger * 10 + iCityDistance;

		//discourage water tiles for land units
		//note that zero danger status has already been established, this is only for sorting now
		if (bWrongDomain)
			iDanger += 500;

		if(bIsInCity)
		{
			if (!pPlot->getPlotCity()->isInDangerOfFalling())
				aCityList.push_back(pPlot, iDanger);
		}
		else if(bIsZeroDanger)
		{
			//if danger is zero, look at distance to closest owned city instead
			//idea: could also look at number of plots reachable from pPlot to avoid dead ends
			aZeroDangerList.push_back(pPlot, bIsInTerritory ? iCityDistance : iCityDistance*2 );
		}
		else if(bIsInCover) //mostly relevant for civilians - when in doubt go home
		{
			aCoverList.push_back(pPlot, iDanger - iCityDistance);
		}
		else if(!bWouldEmbark || bAllowEmbark)
		{
			aDangerList.push_back(pPlot, iDanger);
		}
	}

	//this makes the highest weight come first!
	aCityList.SortItems();
	aZeroDangerList.SortItems();
	aCoverList.SortItems();
	aDangerList.SortItems();

	// Now that we've gathered up our lists of destinations, pick the most promising one
	if (aCityList.size()>0)
		return aCityList.GetElement( aCityList.size()-1 );
	else if (aZeroDangerList.size()>0)
		return aZeroDangerList.GetElement(  aZeroDangerList.size()-1 );
	else if (aCoverList.size()>0)
		return aCoverList.GetElement( aCoverList.size()-1 );
	else if (aDangerList.size()>0)
		return aDangerList.GetElement( aDangerList.size()-1 );

	return NULL;
}

void CTacticalUnitArray::push_back(const CvTacticalUnit& unit)
{
	m_vec.push_back(unit);

	CvUnit* pUnit = m_owner ? m_owner->getUnit( unit.GetID() ) : NULL;

	if (pUnit)
	{
		//not a nice design to use a global variable here, but it's easier than modifying the code in 30 places
		pUnit->setTacticalMove( m_currentTacticalMove.m_eMoveType );

		if (unit.GetID()==g_currentUnitToTrack)
		{
			CvPlayer& owner = GET_PLAYER(pUnit->getOwner());
			OutputDebugString( CvString::format("turn %03d: using %s %s %d for tactical move %s. hitpoints %d, pos (%d,%d), danger %d\n", 
				GC.getGame().getGameTurn(), owner.getCivilizationAdjective(), pUnit->getName().c_str(), g_currentUnitToTrack,
				pUnit->isBarbarian() ? barbarianMoveNames[m_currentTacticalMove.m_eMoveType] : GC.getTacticalMoveInfo(m_currentTacticalMove.m_eMoveType)->GetType(), 
				pUnit->GetCurrHitPoints(), pUnit->getX(), pUnit->getY(), owner.GetPlotDanger(*(pUnit->plot()),pUnit) ) );
		}
	}
}

CvPlot* TacticalAIHelpers::FindClosestSafePlotForHealing(CvUnit* pUnit, bool bWithinOwnTerritory, int iMaxDistance)
{
	if (!pUnit)
		return NULL;

	//work outwards in rings
	for (int iRing=0; iRing<min(iMaxDistance,5); iRing++)
	{
		std::vector<SPlotWithScore> vCandidates;

		//check all neighbors which haven't been checked before
		for (int iI=RING_PLOTS[iRing]; iI<RING_PLOTS[iRing+1]; iI++)
		{
			CvPlot* pPlot = iterateRingPlots(pUnit->getX(),pUnit->getY(),iI);
			if (!pPlot)
				continue;

			//can we go there?
			if (!pUnit->canMoveInto(*pPlot))
				continue;

			if (!pUnit->canHeal(pPlot))
				continue;

			//this is for healing naval units
			if (bWithinOwnTerritory && pPlot->getOwner()!=pUnit->getOwner() )
				continue;

			//this is for healing land units
			if (!bWithinOwnTerritory && pPlot->getOwner() != NO_PLAYER && !pUnit->canEnterTerritory( GET_PLAYER(pPlot->getOwner()).getTeam() ))
				continue;

			if (pPlot->GetNumCombatUnits()>0)
				continue;

			if ( GET_PLAYER( pUnit->getOwner() ).GetPlotDanger(*pPlot,pUnit) > 0)
				continue;

			int iScore = pUnit->healRate(pPlot) - GET_PLAYER(pUnit->getOwner()).GetCityDistanceInEstimatedTurns(pPlot);
			vCandidates.push_back( SPlotWithScore(pPlot, iScore) );
		}

		//start with the plot that is closest to one of our cities
		std::stable_sort( vCandidates.begin(), vCandidates.end() );

		for (size_t iI=0; iI<vCandidates.size(); iI++)
		{
			if (pUnit->GeneratePath( vCandidates[iI].pPlot ))
				return vCandidates[iI].pPlot;
		}
	}

	return NULL;
}

bool TacticalAIHelpers::GetPlotsForRangedAttack(const CvPlot* pTarget, const CvUnit* pUnit, int iRange, bool bCheckOccupied, std::vector<CvPlot*>& vPlots)
{
	vPlots.clear();

	if (!pTarget || !pUnit)
		return false;

	// Aircraft and special promotions make us ignore LOS
	bool bIgnoreLOS = pUnit->IsRangeAttackIgnoreLOS() || pUnit->getDomainType()==DOMAIN_AIR;
	// Can only bombard in domain? (used for Subs' torpedo attack)
	bool bOnlyInDomain = pUnit->getUnitInfo().IsRangeAttackOnlyInDomain();

	std::vector<CvPlot*> vCandidates;
	pTarget->GetPlotsAtRangeX(iRange, false, !bIgnoreLOS, vCandidates);

	//filter and take only the half closer to origin
	CvPlot* pRefPlot = pUnit->plot();
	if(pRefPlot == NULL)
		return false;

	int iRefDist = plotDistance(*pRefPlot,*pTarget);
	std::vector<SPlotWithScore> vIntermediate;
	for (size_t i=0; i<vCandidates.size(); i++)
	{
		if((vCandidates[i]) == NULL)
			continue;

		int iDistance = plotDistance(*pRefPlot,*(vCandidates[i]));
		if (iDistance>iRefDist)
			continue;

		if (!vCandidates[i]->isRevealed(pUnit->getTeam()))
			continue;

		if (bCheckOccupied && vCandidates[i]!=pRefPlot && vCandidates[i]->getBestDefender(NO_PLAYER))
			continue;

		//Respect domains.
		if (pUnit->getDomainType() != vCandidates[i]->getDomain())
			continue;

		if(bOnlyInDomain)
			//subs can only attack within their (water) area or adjacent cities
			if (pRefPlot->getArea() != vCandidates[i]->getArea())
			{
				CvCity *pCity = vCandidates[i]->getPlotCity();
				if (!pCity || !pCity->isAdjacentToArea(pRefPlot->getArea()))
					continue;
			}

		vIntermediate.push_back( SPlotWithScore(vCandidates[i],iDistance) );
	}

	//sort by increasing distance
	std::stable_sort(vIntermediate.begin(), vIntermediate.end());

	for (size_t i=0; i<vIntermediate.size(); i++)
		vPlots.push_back(vIntermediate[i].pPlot);

	return true;
}

//helper function for city threat calculation
int TacticalAIHelpers::GetSimulatedDamageFromAttackOnCity(CvCity* pCity, const CvUnit* pAttacker, int& iAttackerDamage)
{
	if (!pAttacker || !pCity || pAttacker->isDelayedDeath() || pAttacker->IsDead())
		return 0;
		
	int iDamage = 0;
	if (pAttacker->IsCanAttackRanged())
	{
		if (pAttacker->getDomainType() == DOMAIN_AIR)
			iDamage += pAttacker->GetAirCombatDamage(NULL, pCity, false);
		else
			iDamage += pAttacker->GetRangeCombatDamage(NULL, pCity, false);

		iAttackerDamage = 0;
	}
	else
	{
		if (pAttacker->isRangedSupportFire())
			iDamage += pAttacker->GetRangeCombatDamage(NULL, pCity, false);

		//just assume the unit can attack from it's current location - modifiers might be different, but thats acceptable
		iDamage += pAttacker->getCombatDamage(pAttacker->GetMaxAttackStrength(pAttacker->plot(), pCity->plot(), NULL),
			pCity->getStrengthValue(), pAttacker->getDamage(), false, false, true);

		iAttackerDamage = pAttacker->getCombatDamage(pCity->getStrengthValue(),
			pAttacker->GetMaxAttackStrength(pAttacker->plot(), pCity->plot(), NULL),
			pCity->getDamage(), false, true, false);
	}

	return iDamage;
}

//helper function for unit threat calculation
int TacticalAIHelpers::GetSimulatedDamageFromAttackOnUnit(CvUnit* pDefender, const CvUnit* pAttacker, int& iAttackerDamage)
{
	if (!pAttacker || !pDefender || pDefender->isDelayedDeath() || pDefender->IsDead() || pAttacker->isDelayedDeath() || pAttacker->IsDead())
		return 0;
		
	int iDamage = 0;
	if (pAttacker->IsCanAttackRanged())
	{
		if (pAttacker->getDomainType() == DOMAIN_AIR)
			iDamage += pAttacker->GetAirCombatDamage(pDefender, NULL, false);
		else
			iDamage += pAttacker->GetRangeCombatDamage(pDefender, NULL, false);

		iAttackerDamage = 0;
	}
	else
	{
		if (pAttacker->isRangedSupportFire())
			iDamage += pAttacker->GetRangeCombatDamage(pDefender, NULL, false);

		//just assume the unit can attack from it's current location - modifiers might be different, but thats acceptable
		iDamage += pAttacker->getCombatDamage(pAttacker->GetMaxAttackStrength(pAttacker->plot(), pDefender->plot(), pDefender),
			pDefender->GetMaxDefenseStrength(pDefender->plot(), pAttacker), pAttacker->getDamage(), false, false, false);

		iAttackerDamage = pDefender->getCombatDamage(pDefender->GetMaxAttackStrength(pDefender->plot(), pAttacker->plot(), pAttacker),
			pAttacker->GetMaxDefenseStrength(pAttacker->plot(), pDefender), pDefender->getDamage(), false, false, false);
	}

	return iDamage;
}

bool TacticalAIHelpers::KillUnitIfPossible(CvUnit* pAttacker, CvUnit* pDefender)
{
	if (!pAttacker || !pDefender)
		return false;

	//aircraft are different
	if (pAttacker->getDomainType()==DOMAIN_AIR || pDefender->getDomainType()==DOMAIN_AIR)
		return false;

	//see how the attack would go
	int iDamageDealt = 0, iDamageReceived = 0;
	iDamageDealt = TacticalAIHelpers::GetSimulatedDamageFromAttackOnUnit(pDefender, pAttacker, iDamageReceived);

	//is it worth it?
	if ( iDamageDealt > pDefender->GetCurrHitPoints() && iDamageReceived < 3*pAttacker->GetCurrHitPoints()/4 )
	{
		if (pAttacker->isRanged())
		{
			//can we attack directly
			if (pAttacker->canRangeStrikeAt(pDefender->getX(),pDefender->getY()))
			{
				pAttacker->PushMission(CvTypes::getMISSION_RANGE_ATTACK(),pDefender->getX(),pDefender->getY());
				return true;
			}
			
			//need to move and shoot
			std::vector<CvPlot*> vAttackPlots;
			bool bIgnoreLOS = pAttacker->IsRangeAttackIgnoreLOS();
			pDefender->plot()->GetPlotsAtRangeX(pAttacker->GetRange(), false, !bIgnoreLOS, vAttackPlots);
			for (std::vector<CvPlot*>::iterator it=vAttackPlots.begin(); it!=vAttackPlots.end(); ++it)
			{
				if (pAttacker->TurnsToReachTarget(*it,false,false,1)==0)
				{
					pAttacker->PushMission(CvTypes::getMISSION_MOVE_TO(),(*it)->getX(),(*it)->getY(), CvUnit::MOVEFLAG_IGNORE_DANGER);
					pAttacker->PushMission(CvTypes::getMISSION_RANGE_ATTACK(),pDefender->getX(),pDefender->getY());
					return true;
				}
			}
		}
		else //melee
		{
			if (pAttacker->TurnsToReachTarget(pDefender->plot(),false,false,1)<=1)
			{
				pAttacker->PushMission(CvTypes::getMISSION_MOVE_TO(),pDefender->getX(),pDefender->getY(), CvUnit::MOVEFLAG_IGNORE_DANGER | CvUnit::MOVEFLAG_ATTACK );
				return true;
			}
		}
	}

	return false;
}

bool TacticalAIHelpers::HaveEnoughMeleeUnitsAroundTarget(PlayerTypes ePlayer, CvTacticalTarget* pTarget)
{
	if (!pTarget)
		return true;

	int iCount = 0;
	int iReqUnits = 2;
	if (pTarget->GetTargetType()==AI_TACTICAL_TARGET_CITY || pTarget->GetTargetType()==AI_TACTICAL_TARGET_CITY_TO_DEFEND)
		iReqUnits = 4;

	// Look at spaces adjacent to target
	for(int iI = RING0_PLOTS; iI < RING2_PLOTS; iI++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(pTarget->GetTargetX(), pTarget->GetTargetY(), iI);
		if (!pLoopPlot)
			continue;

		int iDistanceToTarget = plotDistance(pLoopPlot->getX(), pLoopPlot->getY(), pTarget->GetTargetX(), pTarget->GetTargetY());
		if (iDistanceToTarget>2)
			continue;

		CvUnit* pUnit = pLoopPlot->getBestDefender(ePlayer);
		//melee combat 
		if (pUnit && !pUnit->isRanged() && pUnit->IsCanAttackWithMove())
			iCount++;

		if (iCount>=iReqUnits)
			return true;
	}

	return false;
}

bool TacticalAIHelpers::IsCaptureTargetInRange(CvUnit * pUnit)
{
	if (pUnit && pUnit->IsCombatUnit() && !pUnit->isRanged() && !pUnit->isNoCapture())
	{
		ReachablePlots reachablePlots;
		TacticalAIHelpers::GetAllPlotsInReachThisTurn(pUnit,pUnit->plot(),reachablePlots,true,true,false);

		for (ReachablePlots::iterator it=reachablePlots.begin(); it!=reachablePlots.end(); ++it)
		{
			CvPlot* pPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);

			CvCity* pNeighboringCity = pPlot->getPlotCity();
			if (pNeighboringCity && GET_PLAYER(pUnit->getOwner()).IsAtWarWith(pNeighboringCity->getOwner()) && pNeighboringCity->isInDangerOfFalling())
				return true;

			if (pPlot->getImprovementType()==GC.getBARBARIAN_CAMP_IMPROVEMENT() && !GET_PLAYER(pUnit->getOwner()).IsAtWar())
			{
				CvUnit* pDefender = pPlot->getBestDefender(BARBARIAN_PLAYER);
				if (!pDefender || TacticalAIHelpers::IsAttackNetPositive(pUnit,pPlot))
					return true;
			}
		}
	}

	return false;
}

//-------------------------------------------------------
// this is experimental code
//-------------------------------------------------------
bool TacticalAIHelpers::GetPreferredPlotsForUnit(CvUnit* pUnit, CvPlot* pTargetPlot, bool bOffensive, std::vector<STacticalPlot>& vResult)
{
	UnitMovementStrategy eStrategy = MS_NONE;
	vResult.clear();

	switch (pUnit->getUnitInfo().GetDefaultUnitAIType())
	{
		//front line units
		case UNITAI_ATTACK:
		case UNITAI_DEFENSE:
		case UNITAI_COUNTER:
		case UNITAI_PARADROP:
		case UNITAI_ATTACK_SEA:
		case UNITAI_RESERVE_SEA:
		case UNITAI_ESCORT_SEA:
		case UNITAI_FAST_ATTACK:
		//ranged units
		case UNITAI_RANGED:
		case UNITAI_CITY_BOMBARD:
		case UNITAI_ASSAULT_SEA:
			eStrategy = (pUnit->GetRange()>1) ? MS_SECONDLINE : ((pUnit->canMoveAfterAttacking() && pUnit->baseMoves()>2) ? MS_HITANDRUN : MS_FIRSTLINE);
			break;

		//combat support, stay out of danger
		case UNITAI_GENERAL:
		case UNITAI_ADMIRAL:
		case UNITAI_CITY_SPECIAL:
		case UNITAI_CARRIER_SEA:
			eStrategy = MS_SUPPORT;
			break;

		//air units. ignore here, rebasing is handled elsewhere
		case UNITAI_ATTACK_AIR:
		case UNITAI_DEFENSE_AIR:
		case UNITAI_MISSILE_AIR:
		case UNITAI_ICBM:
		default:
			//invalid result
			return false;
	}

	int iMaxRange = 0;
	switch (eStrategy)
	{
		case MS_FIRSTLINE:
			iMaxRange = 1;
			break;
		case MS_SECONDLINE:
			iMaxRange = pUnit->GetRange();
			break;
		case MS_HITANDRUN:
			iMaxRange = 1;
			break;
		case MS_SUPPORT:
			iMaxRange = 0;
			break;
	}

	//minMovesLeft is checked later to determine if we can attack from there
	ReachablePlots eligiblePlots;
	TacticalAIHelpers::GetAllPlotsInReachThisTurn(pUnit, pUnit->plot(), eligiblePlots, true, true, false, 0);
	int iMaxAttacks = pUnit->getNumAttacks() - pUnit->getNumAttacksMadeThisTurn();

	for (ReachablePlots::iterator it=eligiblePlots.begin(); it!=eligiblePlots.end(); ++it)
	{
		CvPlot* pPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);
		int iMovesLeft = it->iMovesLeft;
		if (pUnit->isMustSetUpToRangedAttack())
			iMovesLeft = max(0,iMovesLeft-GC.getMOVE_DENOMINATOR());

		int iAttacksHere = min( (iMovesLeft+GC.getMOVE_DENOMINATOR()-1)/GC.getMOVE_DENOMINATOR(), iMaxAttacks );
		bool bCanRetreat = (eStrategy==MS_HITANDRUN) && ((iAttacksHere*GC.getMOVE_DENOMINATOR())>iMovesLeft);

		STacticalPlot::PlotState eState = STacticalPlot::PS_FREE;
		CvUnit* pDefender = pPlot->getBestDefender(NO_PLAYER);
		if (pDefender && pDefender!=pUnit)
		{
			if ( GET_PLAYER(pUnit->getOwner()).IsAtWarWith(pDefender->getOwner()) )
				eState = STacticalPlot::PS_BLOCKED_ENEMY;
			else if (pUnit->getOwner()==pDefender->getOwner())
				eState = STacticalPlot::PS_BLOCKED_FRIENDLY;
			else //neutral unit
				continue;
		}

		int iDistance = pTargetPlot ? plotDistance(*pPlot,*pTargetPlot) : 10;
		int iDistanceScore = max(0,10-iDistance);

		switch (eStrategy)
		{
		case MS_FIRSTLINE:
		case MS_HITANDRUN:
		case MS_SECONDLINE:
			if (bOffensive)
			{
				int iDamageScore = 0;
				int iNeighboringEnemyCount = 0;

				if (iAttacksHere>0)
				{
					std::vector<int> vDamageRatios;
					std::vector<CvPlot*> vAttackPlots;
					for (int iRange=1; iRange<=iMaxRange; iRange++)
					{
						pPlot->GetPlotsAtRangeX(iRange,true,pUnit->IsRangeAttackIgnoreLOS(),vAttackPlots);
						for(size_t iCount=0; iCount<vAttackPlots.size(); iCount++)
						{
							CvPlot* pLoopPlot = vAttackPlots[iCount];
							if (pLoopPlot && (pUnit->isRanged() || pUnit->canMoveInto(*pLoopPlot,CvUnit::MOVEFLAG_ATTACK)))
							{
								CvUnit* pEnemy = pLoopPlot->getBestDefender(NO_PLAYER,pUnit->getOwner(),pUnit,true);
								if (pEnemy)
								{
									int iDamageDealt = 0, iDamageReceived = 0;
									iDamageDealt = TacticalAIHelpers::GetSimulatedDamageFromAttackOnUnit(pEnemy,pUnit,iDamageReceived);

									vDamageRatios.push_back( (iDamageDealt*100)/(iDamageReceived+1) );
									if (iRange==1)
										iNeighboringEnemyCount++;
								}
							}
						}
					}

					std::sort( vDamageRatios.begin(), vDamageRatios.end() );
					std::reverse( vDamageRatios.begin(), vDamageRatios.end() );
					for (size_t i=0; i<min(vDamageRatios.size(),(size_t)iAttacksHere); i++)
					{
						iDamageScore += vDamageRatios[i];
						//count it twice if we can retreat after attacking
						if (bCanRetreat)
							iDamageScore += vDamageRatios[i];
					}
				}

				//todo: take into account mobility at the proposed plot

				int iDanger = pUnit->GetDanger(pPlot);
				int iScore = (iDamageScore*100 + iDistanceScore*10) / (iDanger+1);

				//don't walk into traps
				if (iNeighboringEnemyCount<3)
					vResult.push_back( STacticalPlot(pPlot,eState,iScore) );
			}
			else
			{
			}
			break;

		case MS_SUPPORT:
			break;
		}
	}

	std::sort( vResult.begin(), vResult.end() );
	return !vResult.empty();
}


const char* barbarianMoveNames[] =
{
	"BARBARIAN_CAPTURE_CITY",
	"BARBARIAN_DAMAGE_CITY",
	"BARBARIAN_DESTROY_HIGH_PRIORITY_UNIT",
	"BARBARIAN_DESTROY_MEDIUM_PRIORITY_UNIT",
	"BARBARIAN_DESTROY_LOW_PRIORITY_UNIT",
	"BARBARIAN_MOVE_TO_SAFETY",
	"BARBARIAN_ATTRIT_HIGH_PRIORITY_UNIT", //6
	"BARBARIAN_ATTRIT_MEDIUM_PRIORITY_UNIT",
	"BARBARIAN_ATTRIT_LOW_PRIORITY_UNIT",
	"BARBARIAN_PILLAGE",
	"BARBARIAN_PRIORITY_BLOCKADE_RESOURCE",
	"BARBARIAN_CIVILIAN_ATTACK",
	"BARBARIAN_AGGRESSIVE_MOVE", //12
	"BARBARIAN_PASSIVE_MOVE",
	"BARBARIAN_CAMP_DEFENSE",
	"BARBARIAN_DESPERATE_ATTACK",
	"BARBARIAN_ESCORT_CIVILIAN",
	"BARBARIAN_PLUNDER_TRADE_UNIT",
	"BARBARIAN_PILLAGE_CITADEL",
	"BARBARIAN_PILLAGE_NEXT_TURN",
};

const char* postureNames[] =
{
    "P_WITHDRAW",
    "P_SIT_AND_BOMBARD",
    "P_ATTRIT_FROM_RANGE",
    "P_EXPLOIT_FLANKS",
    "P_STEAMROLL",
    "P_SURGICAL_CITY_STRIKE",
    "P_HEDGEHOG",
    "P_COUNTERATTACK",
    "P_SHORE_BOMBARDMENT",
};