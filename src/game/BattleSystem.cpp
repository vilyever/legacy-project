/*
 * Copyright (C) 2008-2008 LeGACY <http://code.google.com/p/legacy-project/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "Common.h"
#include "BattleSystem.h"
#include "Log.h"
#include "WorldSession.h"
#include "WorldPacket.h"
#include "World.h"
#include "Player.h"
#include "Pet.h"
#include "Creature.h"

#define MAX_ACTION_TIME 220 /* 20 sec countdown, 2 sec tolerance */

///- comparing agility for sorting descending
bool compare_agility(BattleAction* first, BattleAction* second)
{
	//sLog.outString(" >> Comparing %u with %u", first->GetAgility(), second->GetAgility());
	if(first->GetAgility() > second->GetAgility())
		return true;
	else
		return false;
}

///- comparing target position for sorting ascending
bool compare_target_position(BattleAction* first, BattleAction* second)
{
	if( (first->GetTargetCol() > second->GetTargetCol()) ||
		(first->GetTargetRow() > second->GetTargetRow()) )
		return true;
	else
		return false;
}


///- comparing position for sorting ascending
bool compare_position(ItemDropped *first, ItemDropped* second)
{
	//sLog.outString(" >> Comparing %u,%u with %u,%u", first->GetContributorCol(), first->GetContributorRow(), second->GetContributorCol(), second->GetContributorRow());
	if( (first->GetContributorCol() < second->GetContributorCol()) ||
		(first->GetContributorRow() < second->GetContributorRow()) )
		return true;
	else
		return false;
}

bool BattleAction::isLinkable()
{
	if( _skill == SPELL_BASIC ) // basic attack is linkable
		return true;

	const SpellInfo* sinfo = GetProto();
	return (sinfo->Type > 0 ? true : false);
}

void BattleSystem::DumpBattlePosition()
{
	sLog.outDebugInLine("\n");
	sLog.outDebugInLine("\t\tDEFENDER\t\t\t\tATTACKER\n");
	for(int row = 0; row < BATTLE_ROW_MAX; row++)
	{
		for(int col = 0; col < BATTLE_COL_MAX; col++)
		{
			if( col == 2) sLog.outDebugInLine("| ");
			Creature* unit = (Creature*) m_BattleUnit[col][row];
			if( !unit )
				sLog.outDebugInLine("[%-7.7s %4s %3s] ", "", "", "");
			else
				sLog.outDebugInLine("[%-7.7s %4u/%3u] ",
						unit->GetName(),
						unit->GetUInt32Value(UNIT_FIELD_HP),
						unit->GetUInt32Value(UNIT_FIELD_SP));
		}
		sLog.outDebugInLine("\n");
	}

	sLog.outDebugInLine("Rate level attacker %-3.2f\n", m_AtkRateLvl);
	sLog.outDebugInLine("Rate level defender %-3.2f\n", m_DefRateLvl);
	sLog.outDebugInLine("\n");
}
										        
//BattleSystem::BattleSystem(WorldSession *Session)
BattleSystem::BattleSystem(Player* master)
{
	//pSession = Session;
	// battle ground map 357 - 434
	// 367 not exist battle ground
	//m_BattleGroundId = 357 + master->GetBattleCount(); //381;

	m_BattleGroundId = urand( 357, 434 );

	///- Fix incorrect battle map
	if( m_BattleGroundId == 367 )
		m_BattleGroundId = 357;

	m_PlayerActionNeed = 0;
	m_PlayerActionCount = 0;
	m_actionTime = 0;
	m_animationTime = 0;
	m_animationTimeItemDropped = 0;

	m_creatureKilled = false;
	m_waitForAction = false;

	i_master = master;

	m_teamBattle          = false;
	m_teamLowestLevel     = 0;
	m_teamHighestLevel    = 0;
	m_teamLowerLevelCount = 0;
}

///- Clean up all mess :(
BattleSystem::~BattleSystem()
{
	sLog.outDebug("BATTLE: Clean me up, master!");

	i_master = NULL;

	UnitActionTurn::iterator itr;
	for(itr = m_unitTurn.begin(); itr != m_unitTurn.end(); ++itr)
	{
		BattleAction* act = *itr;
		if( act )
		{
			//sLog.outDebug("BATTLE: Destroying uncleaned action");
			delete act;
		}
	}
	
	for(uint8 row = 0; row < BATTLE_ROW_MAX; row++)
	{
		for(uint8 col = 0; col < BATTLE_COL_MAX; col++)
		{
			Unit* unit = m_BattleUnit[col][row];
			if( !unit )
				continue;
			if( unit->isType(TYPE_PLAYER) || unit->isType(TYPE_PET ) )
				continue;

			//sLog.outDebug("BATTLE: Destroying creature '%s'", unit->GetName());
			delete unit;
		}
	}
	for(uint8 i = 0; i < 20; i++)
	{
		if( !m_Actions[i].empty() )
		{
			//sLog.outDebug("BATTLE: Cleaning up action sequence #%u", i);
			m_Actions[i].clear();
		}
	}

	m_PlayerList.clear();
	m_unitTurn.clear();

	sLog.outDebug("BATTLE: I'm cleaned, my master kungfu is very good");

	sLog.outDebug("++++++++++++ BATTLE BACKGROUND %u ++++++++++++", m_BattleGroundId);
}

bool BattleSystem::FindNewMaster()
{
	for(PlayerListMap::const_iterator itr = m_PlayerList.begin(); itr != m_PlayerList.end(); ++itr)
	{
		if( i_master == (*itr) ) // my old humble master, ignore it
			continue;

		i_master = (*itr);
		(*itr)->PlayerBattleClass = this;
		sLog.outDebug("BATTLE: My new master is '%s'", (*itr)->GetName());

		for(PlayerListMap::const_iterator itr2 = m_PlayerList.begin(); itr2 != m_PlayerList.end(); ++itr2)
			(*itr2)->SetBattleMaster(i_master);

		return true;
	}

	sLog.outDebug("BATTLE: New master kungfu is not very good");
	return false;
}

void BattleSystem::Engage( Player* player, Creature* enemy )
{
	m_PlayerActionNeed = 0;
	m_PlayerActionCount = 0;
	m_actionTime = 0;
	m_waitForAction = false;
	///- Reset all Unit
	for(uint8 row = 0; row < BATTLE_ROW_MAX; row++)
		for (uint8 col = 0; col < BATTLE_COL_MAX; col++)
			m_BattleUnit[col][row] = NULL;

	const CreatureData* edata = objmgr.GetCreatureData( enemy->GetGUIDLow() );

	///- Define all creature team
	DefenderTeam[0][0] = edata->team00;
	DefenderTeam[0][1] = edata->team01;
	DefenderTeam[0][2] = enemy->GetGUIDLow();  // Defender Leader Position
	DefenderTeam[0][3] = edata->team03;
	DefenderTeam[0][4] = edata->team04;

	DefenderTeam[1][0] = edata->team10;
	DefenderTeam[1][1] = edata->team11;
	DefenderTeam[1][2] = edata->team12;
	DefenderTeam[1][3] = edata->team13;
	DefenderTeam[1][4] = edata->team14;




	///- Define only player row
	//   pet will be define later. See InitAttackerPosition
	AttackerTeam[3][0] = player->GetTeamGuid(3);
	AttackerTeam[3][1] = player->GetTeamGuid(2);
	AttackerTeam[3][2] = player->GetGUIDLow();
	AttackerTeam[3][3] = player->GetTeamGuid(1);
	AttackerTeam[3][4] = player->GetTeamGuid(4);

	InitDefenderPosition();
	InitAttackerPosition();

	BattleStart();

	///- Update Smoke
	WorldPacket data;
	data.Initialize( 0x0B );
	data << (uint8 ) 0x04;
	data << (uint8 ) 0x02;
	data << (uint32) player->GetAccountId();
	data << (uint16) 0; //unknown
	data << (uint8 ) enemy->GetMapNpcId(); // still guessing
	player->SendMessageToSet(&data, false);
}

void BattleSystem::Engage( Player* player, Player* enemy )
{
	m_PlayerActionNeed = 0;
	m_PlayerActionCount = 0;
	m_actionTime = 0;
	m_waitForAction = false;
	///- Reset all Unit
	for(uint8 row = 0; row < BATTLE_ROW_MAX; row++)
		for(uint8 col = 0; col < BATTLE_COL_MAX; col++)
			m_BattleUnit[col][row] = NULL;

	///- Define all player defender team
	//   pet will be define later. See InitDefenderPosition
	DefenderTeam[0][0] = enemy->GetTeamGuid(3);
	DefenderTeam[0][1] = enemy->GetTeamGuid(2);
	DefenderTeam[0][2] = enemy->GetGUIDLow();
	DefenderTeam[0][3] = enemy->GetTeamGuid(1);
	DefenderTeam[0][4] = enemy->GetTeamGuid(4);

	///- Define all player attacker team
	//   pet will be define later. See InitAttackerPosition
	AttackerTeam[3][0] = player->GetTeamGuid(3);
	AttackerTeam[3][1] = player->GetTeamGuid(2);
	AttackerTeam[3][2] = player->GetGUIDLow();
	AttackerTeam[3][3] = player->GetTeamGuid(1);
	AttackerTeam[3][4] = player->GetTeamGuid(4);

	InitDefenderPosition2();
	InitAttackerPosition();

	BattleStart();

}

void BattleSystem::BattleStart()
{
	sLog.outString("");
	sLog.outString( "BattleSystem::BattleStart" );
	sLog.outString("");

	WorldPacket data;

	data.Initialize( 0x14 );
	data << (uint8 ) 0x0C;
	//pSession->SendPacket(&data);
	SendMessageToSet(&data);

	data.Initialize( 0x06 );
	data << (uint8 ) 0x02;
	//pSession->SendPacket(&data);
	SendMessageToSet(&data);

	//pSession->SetLogging(false);

	///- Battle Initiator, Send order is CRITICAL !!!!
	PlayerListMap::const_iterator itr = m_PlayerList.begin();
	for(itr = m_PlayerList.begin(); itr != m_PlayerList.end(); ++itr)
		(*itr)->UpdatePlayer();

	BattleScreenTrigger();

	itr = m_PlayerList.begin();
	for(itr = m_PlayerList.begin(); itr != m_PlayerList.end(); ++itr)
		if((*itr))
			(*itr)->UpdatePetBattle();

	SendPetPosition();

	SendDefenderPosition();

	SendTurnComplete();
}

///- Reset Action
void BattleSystem::ResetAction()
{
	m_actionTime = 0;
	m_PlayerActionCount = 0;
	m_PlayerActionNeed = 0;
	for(uint8 row = 0; row < BATTLE_ROW_MAX; row++)
		for(uint8 col = 0; col < BATTLE_COL_MAX; col++)
		{
			Unit* unit = m_BattleUnit[col][row];
			if( !unit )
				continue;

			if( unit->isType(TYPE_PLAYER) || unit->isType(TYPE_PET))
				if( !unit->isDead() && !unit->isDisabled() )
					m_PlayerActionNeed++;

			if( unit->isReleaseFromDisabledState() )
			{
				///- disabled unit state released
				WorldPacket data;
				data.Initialize( 0x35 );
				data << (uint8 ) 1;
				data << (uint8 ) col;
				data << (uint8 ) row;
				data << (uint8 ) 1; // release disabled flag = 1
				data << (uint16) 0; // unknown
				SendMessageToSet(&data, true);
			}

			if( unit->isPositiveBufExpired() )
			{
				///- (+) buf unit state released
				WorldPacket data;
				data.Initialize( 0x35 );
				data << (uint8 ) 1;
				data << (uint8 ) col;
				data << (uint8 ) row;
				data << (uint8 ) 2; // release (+) buf flag = 2
				data << (uint16) 0; //unknown
				SendMessageToSet(&data, true);
			}

			///- Clear Defensive stance
			unit->clearUnitState(UNIT_STATE_DEFEND);
		}

}

///- Set Turn Ready
void BattleSystem::SendTurnComplete()
{
	ResetAction();
	m_waitForAction = true;

	WorldPacket data;
	data.Initialize( 0x14 );
	data << (uint8 ) 0x09;
	//SendMessageToSet(&data);

	data.Initialize( 0x34 );
	data << (uint8 ) 0x01;
	SendMessageToSet(&data);

	DumpBattlePosition();
}

///- Initialize Battle Screen Mode
void BattleSystem::BattleScreenTrigger()
{
	sLog.outDebug("BATTLE: Screen Trigger");
	WorldPacket data;
/*
	Player *p = pSession->GetPlayer();
	data.Initialize( 0x0B );
	data << (uint8 ) 0xFA;    // initiate battle on screen, i think
	data << (uint16) m_BattleGroundId; // battle background stage id
	data << (uint8 ) 0x01;
	data << (uint8 ) 0x02;
	data << (uint32) pSession->GetAccountId();
	data << (uint16) 0x0000;
	data << (uint16) 0x0000;
	data << (uint16) 0x0000;
	data << (uint8 ) 3;       // player col position
	data << (uint8 ) 2;       // player row position
	data << (uint16) p->GetUInt32Value(UNIT_FIELD_HP_MAX); // player hp max
	data << (uint16) p->GetUInt32Value(UNIT_FIELD_SP_MAX); // player sp max
	data << (uint16) p->GetUInt32Value(UNIT_FIELD_HP);     // player current hp
	data << (uint16) p->GetUInt32Value(UNIT_FIELD_SP);     // player current sp
	data << (uint16) p->GetUInt32Value(UNIT_FIELD_LEVEL);   // player level
	data << (uint16) p->GetUInt32Value(UNIT_FIELD_ELEMENT); // player element
	pSession->SendPacket(&data);
*/

	data.Initialize( 0x0B );
	data << (uint8 ) 0xFA;
	data << (uint16) m_BattleGroundId;

	uint8 col = 3; // only for player unit
	for(uint8 row = 0; row < BATTLE_ROW_MAX; row++)
	{
		Player* p = (Player*) GetBattleUnit(col, row);

		if( !p ) continue;

		if( !p->isType(TYPE_PLAYER) ) continue;

		sLog.outDebug("BATTLE: Trigger add for '%s'", p->GetName());
		data << (uint8 ) 0x01; // unknown
		data << (uint8 ) 0x02; // unknown
		data << (uint32) p->GetAccountId();
		data << (uint16) 0x0000;
		data << (uint16) 0x0000;
		data << (uint16) 0x0000;
		data << (uint8 ) col;       // player col position
		data << (uint8 ) row;       // player row position
		data << (uint16) p->GetUInt32Value(UNIT_FIELD_HP_MAX);  // hp max
		data << (uint16) p->GetUInt32Value(UNIT_FIELD_SP_MAX);  // sp max
		data << (uint16) p->GetUInt32Value(UNIT_FIELD_HP);      // current hp
		data << (uint16) p->GetUInt32Value(UNIT_FIELD_SP);      // current sp
		data << (uint8 ) p->GetUInt32Value(UNIT_FIELD_LEVEL);   // level
		data << (uint8 ) p->GetUInt32Value(UNIT_FIELD_ELEMENT); // element

	}

	sLog.outDebug("BATTLE: Triggering battle to Attackers");
	//SendMessageToSet(&data, true);
	SendMessageToAttacker(&data, true);

	BattleScreenTrigger2();

	///- Start the battle IMPORTANT!!
	data.Initialize( 0x0B );
	data << (uint8 ) 0x0A;
	data << (uint8 ) 0x01;
	//pSession->SendPacket(&data);
	SendMessageToSet(&data);

	sLog.outDebug("BATTLE: Screen Trigger Complete");
}

///- Battle Screen Trigger for Defenders
void BattleSystem::BattleScreenTrigger2()
{
	WorldPacket data;
	data.Initialize( 0x0B );
	data << (uint8 ) 0xFA;
	data << (uint16) m_BattleGroundId;

	for(uint8 col = 0; col < BATTLE_COL_MAX; col++)
		for(uint8 row = 0; row < BATTLE_ROW_MAX; row++)
		{
			Player* p = (Player*) GetBattleUnit(col, row);

			if( !p ) continue;

			if( !p->isType(TYPE_PLAYER) ) continue;

			sLog.outDebug("BATTLE: Trigger2 add for '%s'", p->GetName());
			data << (uint8 ) 0x05; // unknown
			data << (uint8 ) 0x02; // unknown
			data << (uint32) p->GetAccountId();
			data << (uint16) 0;
			data << (uint16) 0;
			data << (uint16) 0;
			data << (uint8 ) col;
			data << (uint8 ) row;
			data << (uint16) p->GetUInt32Value(UNIT_FIELD_HP_MAX);
			data << (uint16) p->GetUInt32Value(UNIT_FIELD_SP_MAX);
			data << (uint16) p->GetUInt32Value(UNIT_FIELD_HP);
			data << (uint16) p->GetUInt32Value(UNIT_FIELD_SP);
			data << (uint8 ) p->GetUInt32Value(UNIT_FIELD_LEVEL);
			data << (uint8 ) p->GetUInt32Value(UNIT_FIELD_ELEMENT);
		}

	sLog.outDebug("BATTLE: Triggering battle to Defenders");
	SendMessageToDefender(&data, true);
}

///- Send Only TYPE_PET Unit position
void BattleSystem::SendPetPosition()
{
	WorldPacket data;
/*
	data.Initialize( 0x0B );
	data << (uint8 ) 0x05;
	data << (uint8 ) 0x05;
	data << (uint8 ) 0x04;
	data << (uint16) 12027; //41027; //47023;   // Pet Npc Id
	data << (uint16) 0x0000;
	data << (uint16) 0x0004;
	data << pSession->GetAccountId(); // pet owner id (0 for AI)
	data << (uint8 ) 2;       // pet row position
	data << (uint8 ) 2;       // pet column position
	data << (uint16) 4000;    // pet hp max
	data << (uint16) 500;     // pet sp max
	data << (uint16) 3900;    // pet current hp
	data << (uint16) 400;     // pet current sp
	data << (uint8 ) 120;     // pet level
	data << (uint8 ) 4;       // pet element
	pSession->SendPacket(&data);
	return;
*/

	///- Pet Column position only
	for(uint8 col = 1; col <= 2; col++)
		for(uint8 row = 0; row < BATTLE_ROW_MAX; row++)
		{
			Pet* pet = (Pet*) m_BattleUnit[col][row];
			if( !pet ) continue;
/*
			data.Initialize( 0x0B );
			data << (uint8 ) 0x05;
			data << (uint8 ) 0x05;
			data << (uint8 ) 0x04;
			data << (uint16) pet->GetModelId(); // Pet ModelId
			data << (uint16) 0x0000;
			data << (uint16) 0x0004;
			data << pet->GetOwnerAccountId();// pet owner id (0 for AI)
			data << (uint8 ) col;// pet col position
			data << (uint8 ) row;// pet row position
			data << (uint16) pet->GetUInt32Value(UNIT_FIELD_HP_MAX);//pet hp max
			data << (uint16) pet->GetUInt32Value(UNIT_FIELD_SP_MAX);//pet sp max
			data << (uint16) pet->GetUInt32Value(UNIT_FIELD_HP);    //pet hp
			data << (uint16) pet->GetUInt32Value(UNIT_FIELD_SP);    //pet sp
			data << (uint16) pet->GetUInt32Value(UNIT_FIELD_LEVEL); //pet level
			data << (uint16) pet->GetUInt32Value(UNIT_FIELD_ELEMENT);//pet elmn
*/
			BuildUpdateBlockPetPosition(&data, pet, col, row);
			//pSession->SendPacket(&data);
			SendMessageToSet(&data);
		}
}

///- Init Attacker position
void BattleSystem::InitAttackerPosition()
{
	m_AtkRateLvl       = 0;

	uint8  unit_count  = 0;
	uint32 level_count = 0;

	uint8 col = 3; // set col for player row only

	for(uint8 row = 0; row < BATTLE_ROW_MAX; row++)
	{
		Player* pPlayer = objmgr.GetPlayer(AttackerTeam[col][row]);
		if( !pPlayer )
			continue;

		SetPosition(pPlayer, col, row);
		//PlayerList[unit_count] = pPlayer;
		JoinBattle(pPlayer);

		if( pPlayer->getLevel() > m_teamHighestLevel )
			m_teamHighestLevel = pPlayer->getLevel();

		if( pPlayer->getLevel() < m_teamLowestLevel )
			m_teamLowestLevel = pPlayer->getLevel();

		if( pPlayer->getLevel() + 10 < m_teamHighestLevel )
			m_teamLowerLevelCount++;

		unit_count++;
		level_count += pPlayer->getLevel();

		Pet* pet = pPlayer->GetBattlePet();
		if( !pet )
			continue;

		SetPosition(pet, col - 1, row);

		unit_count++;
		level_count += pet->getLevel();
	}

	m_AtkRateLvl = level_count / unit_count;

	if( unit_count > 2 )
		m_teamBattle = true;
	else
		m_teamBattle = false;
}

///- Init Enemy position
void BattleSystem::InitDefenderPosition()
{
	m_DefRateLvl       = 0;

	uint8  unit_count  = 0;
	uint32 level_count = 0;

	for(uint8 row = 0; row < BATTLE_ROW_MAX; row++)
	{
		for(uint8 col = 0; col < DEFENDER_COL_MAX; col++)
		{
			uint32 guid = DefenderTeam[col][row];
			if( !guid )
				continue;

			Creature* unit = new Creature( NULL, true );
			if( !unit->LoadFromDB( guid ) )
			{
				delete unit;
				continue;
			}

			SetPosition( unit, col, row );

			unit_count++;
			level_count += unit->getLevel();
		}
	}
	m_DefRateLvl = level_count / unit_count;
}

///- Init Enemy Position (for player type)
void BattleSystem::InitDefenderPosition2()
{
	m_DefRateLvl       = 0;

	uint8  unit_count  = 0;
	uint32 level_count = 0;

	uint8 col = 0; // set col for player row only

	for(uint8 row = 0; row < BATTLE_ROW_MAX; row++)
	{
		Player* pPlayer = objmgr.GetPlayer(DefenderTeam[col][row]);
		if( !pPlayer )
			continue;

		SetPosition(pPlayer, col, row);
		unit_count++;
		level_count += pPlayer->getLevel();

		JoinBattle(pPlayer);

		Pet* pet = pPlayer->GetBattlePet();
		if( !pet )
			continue;

		SetPosition(pet, col + 1, row);
		unit_count++;
		level_count += pet->getLevel();
	}
	m_DefRateLvl = level_count / unit_count;
}

void BattleSystem::SetPosition(Unit *unit, uint8 col, uint8 row)
{
	m_BattleUnit[col][row] = unit;
}

void BattleSystem::SendAttackerPosition()
{
	///- Only send for TYPE_PLAYER
}

void BattleSystem::SendDefenderPosition()
{
	WorldPacket data;
/*
	data.Initialize( 0x0B );
	data << (uint8 ) 0x05;
	data << (uint8 ) 0x01;
	data << (uint8 ) 0x07;
	data << guidlow;              // npc id
	data << count;              // unknown, still guessing
	data << (uint8 ) 0x00;
	data << (uint16) 0x0000;
	data << (uint16) 0x0000;
	data << col;
	data << row;
	data << (uint16) 1200;      // max hp
	data << (uint16) 100;       // max sp
	data << (uint16) 1200;      // current hp
	data << (uint16) 100;       // current sp
	data << (uint8 ) 13;        // level
	data << (uint8 ) 3;         // element
	pSession->SendPacket(&data);
*/
	uint8 count = 0;
	///- Only back row to send
	for(uint8 col = 0; col < 1; col++)
		for(uint8 row = 0; row < BATTLE_ROW_MAX; row++)
		{
			//Creature *unit = (Creature*) m_BattleUnit[col][row];
			Unit *unit = m_BattleUnit[col][row];
			if( !unit )
				continue;

			if( unit->isType(TYPE_PET) )
				continue;

			data.Initialize( 0x0B );
			data << (uint8 ) 0x05;
			data << (uint8 ) (unit->isType(TYPE_PLAYER) ? 0x05 : 0x01);
			data << (uint8 ) (unit->isType(TYPE_PLAYER) ? 0x02 : 0x07);
			data << (uint16) (unit->isType(TYPE_PLAYER) ? ((Player*)unit)->GetAccountId() : ((Creature*)unit)->GetModelId());// npc ModelId/player AccId
			data << (uint16) 0x0000;
			data << count++;              // unknown, still guessing
			data << (uint8 ) 0x00;
			data << (uint16) 0x0000;
			data << (uint16) 0x0000;
			data << col;
			data << row;
			data << (uint16) unit->GetUInt32Value(UNIT_FIELD_HP_MAX); // max hp
			data << (uint16) unit->GetUInt32Value(UNIT_FIELD_SP_MAX); // max sp
			data << (uint16) unit->GetUInt32Value(UNIT_FIELD_HP); // current hp
			data << (uint16) unit->GetUInt32Value(UNIT_FIELD_SP); // current sp
			data << (uint8 ) unit->GetUInt32Value(UNIT_FIELD_LEVEL); // level
			data << (uint8 ) unit->GetUInt32Value(UNIT_FIELD_ELEMENT);// element
			//pSession->SendPacket(&data);
			SendMessageToSet(&data);
		}
}

void BattleSystem::AIMove()
{
	uint16 spell = SPELL_BASIC;

	bool   actionTaken = false;

	for( uint8 row = 0; row < BATTLE_ROW_MAX; row++)
		for( uint8 col = 0; col < DEFENDER_COL_MAX; col++)
		{
			Unit *attacker = m_BattleUnit[col][row];
			if( !attacker ) continue;

			if( attacker->isType(TYPE_PLAYER) || attacker->isType(TYPE_PET) )
				continue;

			if( attacker->isDead() || attacker->isDisabled() ) continue;

			uint8 ai_difficulty = 50;

			double c = rand_chance();
			if( c < ai_difficulty )
			{
				spell = attacker->GetRandomSpell(ai_difficulty);
				sLog.outDebug("AIMOVE: Chanced %.2f%% '%s' using <%s>", c, attacker->GetName(), objmgr.GetSpellTemplate(spell)->Name);
			}

			actionTaken = false;

			for( uint8 row2 = 0; row2 < BATTLE_ROW_MAX; row2++)
			{
				for( uint8 col2 = ATTACKER_COL_MIN; col2 < BATTLE_COL_MAX; col2++)
				{
					Unit *target = m_BattleUnit[col2][row2];
					if( !target ) continue;

					if( target->isDead() ) continue;

					BattleAction* action = new BattleAction(col, row, spell, col2, row2);
					AddBattleAction(action);
					actionTaken = true;
					break;
				}
				if(actionTaken) break;
			}
		}
}

Unit* BattleSystem::GetAttacker(const BattleAction *action)
{
	if( !action )
	{
		sLog.outDebug("BATTLE: GetAttacker Empty Action");
		return NULL;
	}

	uint8 col = action->GetAttackerCol();
	uint8 row = action->GetAttackerRow();
	Unit* unit = GetBattleUnit(col, row);
	if( !unit )
		sLog.outDebug("BATTLE: Empty attacker @[%u,%u]", col, row);

	return unit;
}

Unit* BattleSystem::GetVictim(const BattleAction *action)
{
	if( !action )
	{
		sLog.outDebug("BATTLE: GetVictim Empty Action");
		return NULL;
	}
	uint8 col = action->GetTargetCol();
	uint8 row = action->GetTargetRow();
	Unit* unit = GetBattleUnit(col, row);
	if( !unit )
		sLog.outDebug("BATTLE: Empty victim @[%u,%u]", col, row);

	return unit;
}

bool BattleSystem::NeedRedirectTarget(const BattleAction *action)
{
	if( !isUnitAvail( action->GetTargetCol(), action->GetTargetRow() ))
		return true;
	//const SpellInfo* sinfo = objmgr.GetSpellTemplate(action->GetSkill());

	// for now only check death state primary target
	Unit* victim = GetVictim(action);
	if( victim->isDead() )
		return true;
	else
		return false;
}

BattleAction* BattleSystem::RedirectTarget(BattleAction *action)
{
	uint8 col = action->GetAttackerCol();
	Unit* pAttacker = GetAttacker(action);
	///- Detect direction
	if( isDefPos( col ) )
	{
		for(uint8 row = 0; row < BATTLE_ROW_MAX; row++)
			for(uint8 col2 = ATTACKER_COL_MIN; col2 < BATTLE_COL_MAX; col2++)
			{
				Unit* unit = m_BattleUnit[col2][row];
				if( !unit ) continue;
				if( unit->isDead() ) continue;

				sLog.outDebug("BATTLE: Defender '%s' redirect to new victim '%s'(%u)", pAttacker->GetName(), unit->GetName(), unit->GetUInt32Value(UNIT_FIELD_HP));
				action->SetTargetCol(col2);
				action->SetTargetRow(row);
				return action;
			}
	}
	else if( isAtkPos( col ) )
	{
		for(uint8 row = 0; row < BATTLE_ROW_MAX; row++)
			for(uint8 col2 = 0; col2 < DEFENDER_COL_MAX; col2++)
			{
				Unit* unit = m_BattleUnit[col2][row];
				if( !unit ) continue;
				if( unit->isDead() ) continue;

				sLog.outDebug("BATTLE: Attacker '%s' redirect to new victim '%s'(%u)", pAttacker->GetName(), unit->GetName(), unit->GetUInt32Value(UNIT_FIELD_HP));
				action->SetTargetCol(col2);
				action->SetTargetRow(row);
				return action;
			}
	}
	sLog.outDebug("BATTLE: Attacker '%s' failed to redirect", pAttacker->GetName());
	return action;
}

void BattleSystem::AddBattleAction(BattleAction* action)
{
	sLog.outDebug("COMBAT: >> Add Action for %u,%u to %u,%u", action->GetAttackerCol(), action->GetAttackerRow(), action->GetTargetCol(), action->GetTargetRow());
	Unit* pAttacker = GetAttacker(action);

	if( !pAttacker )
		return;

	///- Do not check dead target here, only check while performing turns
//	Unit* pTarget = GetVictim(action);
//	if( !pTarget || pTarget->isDead() )
//		action = RedirectTarget(action);

	int32 agility = pAttacker->GetUInt32Value(UNIT_FIELD_AGI) + pAttacker->GetInt32Value(UNIT_FIELD_AGI_MOD);

	///- TODO:
	// Check Golem status buf
	// Check for Bros Agility modifier here for PLAYER_TYPE


	action->SetAgility(agility);

	sLog.outString("COMBAT: >> Push Back Unit '%s' Agility: %d", pAttacker->GetName(), action->GetAgility());
	//BattleAction *act = new BattleAction(action);
	m_unitTurn.push_back(action);
}

bool BattleSystem::CanLink(BattleAction act1, BattleAction act2)
{
	sLog.outString("COMBAT: >> LINKING for [%u,%u](%u) & [%u,%u](%u) to [%u,%u] & [%u,%u]", act1.GetAttackerCol(), act1.GetAttackerRow(), act1.GetProto()->Type, act2.GetAttackerCol(), act2.GetAttackerRow(), act2.GetProto()->Type, act1.GetTargetCol(), act1.GetTargetRow(), act2.GetTargetCol(), act2.GetTargetRow());

	if( act1.GetTargetCol() != act2.GetTargetCol() ||
	    act1.GetTargetRow() != act2.GetTargetRow() )
	{
		sLog.outString("COMBAT: >> NOT LINKED, target not match");
		return false;
	}

	if( act1.GetAttackerCol() == act2.GetAttackerCol() && 
	    act1.GetAttackerRow() == act2.GetAttackerRow() )
	{
		sLog.outString("COMBAT: >> NOT LINKED, same attacker");
		return false;
	}

	if( !act1.isLinkable() || !act2.isLinkable() )
	{
		sLog.outString("COMBAT: >> NOT LINKED, spells is not linkable");
		return false;
	}

	double dRoll = rand_chance();
	double diff  = 0;

	if( act1.GetAttackerCol() >= ATTACKER_COL_MIN )
		diff = m_AtkRateLvl - m_DefRateLvl;
	else
		diff = m_DefRateLvl - m_AtkRateLvl;

	if( dRoll < (50 - diff) )
	{
		sLog.outString("COMBAT: >> NOT LINKED, dice roll %2.2f below chance value %-2.2f", dRoll, 50 - diff);
		return false;
	}

	float rate_linked_agility = sWorld.getRate(RATE_LINKED_AGILITY);

	float agi1 = act1.GetAgility();
	float agi2 = act2.GetAgility();
	float agi_min = agi1 - rate_linked_agility;
	float agi_max = agi1 + rate_linked_agility;
	sLog.outString("COMBAT: >> COMPARING AGI RANGE %3.2f - %3.2f with %3.2f ", agi_min, agi_max, agi2 );
	if( agi_min > agi2 || agi_max < agi2 )
	{
		sLog.outString("COMBAT: >> NOT LINKED, agility not tolerance");
		return false;
	}

	sLog.outString("COMBAT: >> LINKED !");
	return true;
}

void BattleSystem::BuildActions()
{
	m_waitForAction = false;
	m_actionTime = 0;

	for(uint8 i = 0; i < 20; i++)
		if( !m_Actions[i].empty() )
			m_Actions[i].clear();

	AIMove();

	m_unitTurn.sort(compare_agility);

	for(UnitActionTurn::const_iterator itr = m_unitTurn.begin(); itr != m_unitTurn.end(); ++itr)
	{
		sLog.outString("COMBAT: == Action Turn Agility: %d", (*itr)->GetAgility());
	}

	uint8 i_action = 0;

	BattleAction *lastAction = NULL;
	bool brokenLink = false;

	while( !m_unitTurn.empty() )
	{
		BattleAction *bAction = m_unitTurn.front();
		m_unitTurn.pop_front();

		if( !SameSide(lastAction, bAction) )
		{
			if( !m_Actions[i_action].empty() )
				i_action++;

			m_Actions[i_action].push_back(bAction);
		}
		else
		{
			if( lastAction )
			{
				if( CanLink(*lastAction, *bAction) )
				{
					m_Actions[i_action].push_back(bAction);
				}
				else
				{
					if( !m_Actions[i_action].empty() )
						i_action++;

					m_Actions[i_action].push_back(bAction);
				}
			}
			else
				m_Actions[i_action].push_back(bAction);
		}

		lastAction = bAction;

		sLog.outString("COMBAT: >> Turn #%u size %u", i_action, m_Actions[i_action].size());

	}
/*
	while( !m_unitTurn.empty() )
	{
		BattleAction *bAction1 = m_unitTurn.front();
		m_unitTurn.pop_front();

		if( !SameSide(lastAction, bAction1) )
		{
			brokenLink = true;
			if( !m_Actions[i_action].empty() )
				i_action++;
		}
		else
		{
			brokenLink = false;
			if(lastAction)
			{
				if( CanLink(*lastAction, *bAction1) )
				{
					sLog.outString("COMBAT: >> ACTION LINKED LAST");
					// push linkable action to stack
					m_Actions[i_action].push_back(bAction1);

					if(m_unitTurn.empty())
					{
						sLog.outString("COMBAT: >> Turn #%u size %u", i_action, m_Actions[i_action].size());
						break;
					}

					bAction1 = m_unitTurn.front();

					m_unitTurn.pop_front();

				}
			}
		}

		if(m_unitTurn.empty())
			break;

		BattleAction *bAction2 = m_unitTurn.front();

		if( !SameSide(bAction1, bAction2) )
		{
			brokenLink = true;
			if( !m_Actions[i_action].empty() )
				i_action++;
		}
		else
			brokenLink = false;
		
		lastAction = bAction2;

		if( CanLink(*bAction1, *bAction2) && !brokenLink )
		{
			sLog.outString("COMBAT: >> ACTION LINKED");

			// push linkable action to stack
			m_Actions[i_action].push_back(bAction1);
			m_Actions[i_action].push_back(bAction2);

			m_unitTurn.pop_front();

			brokenLink = false;
		}
		else
		{
			if( !brokenLink )
				sLog.outString("COMBAT: >> ACTION NOT LINKED");
			else
				sLog.outString("COMBAT: >> ACTION NOT LINKED - BROKEN");

			if( !m_Actions[i_action].empty() ) // check if action is empty
				i_action++;

			// push only last action to stack
			m_Actions[i_action].push_back(bAction1);

			i_action++;

			brokenLink = true;
		}

		sLog.outString("COMBAT: >> Turn #%u size %u", i_action, m_Actions[i_action].size());

	}
	*/

	/*
	WorldPacket data;
	//pSession->SetLogging(true);
	for(uint8 i = 0; i <= i_action; i++)
	{
		sLog.outString("COMBAT: >> Action #%u of total %u", i, i_action);
		data.clear();
		data.Initialize( 0x32 );
		data << (uint8 ) 0x01;
		while( !m_Actions[i].empty() )
		{
			BattleAction* bAction = m_Actions[i].front();
			m_Actions[i].pop_front();
			BuildUpdateBlockAction(&data, bAction);
		}
		//pSession->SendPacket(&data);
		SendMessageToSet(&data);
		WaitForAnimation(12000);
	}
	//pSession->SetLogging(false);
	sLog.outString("COMBAT: >> Turn Complete.");

	m_unitTurn.clear();

	if(BattleConcluded())
	{
		BattleStop();
		return;
	}

	SendTurnComplete();
	*/
}

bool BattleSystem::SendAction()
{
	UnitActionTurn action;
	uint8 i_action = 0;
	for(i_action = 0; i_action < 20; i_action++)
	{
		if( m_Actions[i_action].empty() )
			continue;

		action = m_Actions[i_action];
		m_Actions[i_action].clear();
		break;
	}

	///- double check 
	if( action.empty() )
	{
		return false;
	}

	///- Check dead before applying linked action
	///  if victim is dead, break it up and push it back
	Unit* victim = GetVictim(action.front());

	//if( victim->isDead() && NeedRedirectTarget(action.front()) )
	if( NeedRedirectTarget(action.front()) )
	{
		BattleAction* tmpAction = action.front();
		action.pop_front();

		tmpAction = RedirectTarget(tmpAction);

		if( !action.empty() )
		{
			if( m_Actions[i_action].empty() )
			{
				sLog.outDebug("COMBAT: Push back unlinked to %u, size %u", i_action, action.size() );
				m_Actions[i_action] = action;
			}
			action.clear();
		}

		action.push_back(tmpAction);
	}

	bool   valid_action = false;
	uint16 skillId = SPELL_BASIC;

	WorldPacket data;
	data.Initialize( 0x32 );
	data << (uint8 ) 0x01;
	//data << (uint16) 0x000F;         // data length, guess default 0x000F

	bool linked = false;
	///- Here we are, we will have a live target to deals with
	while( !action.empty() )
	{
		BattleAction* bAction = action.front();
		action.pop_front();

		if( !action.empty() )
			linked = true;

		Unit* pAttacker = GetAttacker(bAction);

		///- double check
		if( !pAttacker ) continue;

		///- Am i dead yet ?
		if( pAttacker->isDead() )
		{
			sLog.outDebug("BATTLE: Fuck!, '%s' is dead, skipped", pAttacker->GetName());
			continue;
		}

		if( pAttacker->isDisabled() )
		{
			sLog.outDebug("BATTLE: Fuck!, '%s' is disabled, skipped", pAttacker->GetName());
			continue;
		}

		sLog.outDebug("BATTLE: Processing action #%u for '%s'", i_action, pAttacker->GetName());

		switch( bAction->GetSkill() )
		{
			case SPELL_DEFENSE:
			{
				///- Need no animation, skipped
				///- TODO: Set DEFENSE Stance to actor
				pAttacker->addUnitState(UNIT_STATE_DEFEND);
				return true;
			} break;

			case SPELL_ESCAPE:
			{
				if( rand_chance() > 65 )
				{
					Escaped(bAction);
					break;
				}
			};

			default:
			{
				BuildUpdateBlockAction(&data, bAction, linked);

				///- TODO: Determine animation time by skill id
				if( bAction->GetSkill() != skillId )
					skillId = bAction->GetSkill();

				valid_action = true;
			} break;
		}

	}


	if( valid_action )
	{
		SendMessageToSet(&data, true);
		WaitForAnimation( skillId );
	}

	return true; // everything is ok
}

void BattleSystem::UpdateBattleAction()
{
	if( m_animationTimeItemDropped > 0 )
	{
		//sLog.outDebug("BATTLE: Item Dropped Animation, %u tick", m_animationTimeItemDropped);
		m_animationTimeItemDropped--;
		return;
	}

	if( BattleConcluded() && m_animationTimeItemDropped == 0 )
	{
		BattleStop();
		return;
	}

	if( m_waitForAction )
		return;

	if( m_animationTime > 0 )
	{
		//sLog.outDebug("BATTLE: Animation, %u tick", m_animationTime);
		m_animationTime--;
		return;
	}

	if( m_creatureKilled )
	{
		m_creatureKilled = false;

		///- Give item dropped after a creature is killed and turn > 1
		GiveItemDropped();
	}

	if( !SendAction() )
	{
		SendTurnComplete();
	}


	PlayerListMap::const_iterator itr = m_PlayerList.begin();
	for(itr; itr != m_PlayerList.end(); ++itr)
	{
		(*itr)->UpdatePlayer();
		(*itr)->UpdatePetBattle();
	}
}

void BattleSystem::BuildUpdateBlockAction(WorldPacket *data, BattleAction* action, bool linked)
{
	int32  point_inflicted = 0;
	uint8  point_modifier  = 0;

	const SpellInfo* sinfo = action->GetProto();

	uint16 data_len = data->size();

	*data << (uint16) 0x000F; // data length for spell attack default 0x000F

	*data << (uint8 ) action->GetAttackerCol(); // attacker col
	*data << (uint8 ) action->GetAttackerRow(); // attacker row

	bool catched = false;
	switch( action->GetSkill() )
	{
		case SPELL_ESCAPE:
		{
			///- TODO: Calculate escape chance
			*data << (uint16) SPELL_ESCAPE_FAILED;
		} break;

		case SPELL_CATCH:
		case SPELL_CATCH2:
		{
			uint8 tcol = action->GetTargetCol();
			uint8 trow = action->GetTargetRow();
			Unit *victim = GetBattleUnit(tcol, trow);
			if( !victim->isType(TYPE_PLAYER) ||
				!victim->isType(TYPE_PET) )
			{
				uint8 acol = action->GetAttackerCol();
				uint8 arow = action->GetAttackerRow();
				Unit* attacker = GetBattleUnit(acol, arow);
				if( attacker->isType(TYPE_PLAYER) || attacker->isType(TYPE_PET))
				{

					Player* catcher = NULL;
					if(attacker->isType(TYPE_PLAYER))
						catcher = (Player*) attacker;
					else if(attacker->isType(TYPE_PET))
						catcher = (Player*) attacker->GetOwner();

					if( CanCatchPet(catcher, victim) )
					{
						Pet* pet = catcher->CreatePet( ((Creature*)victim)->GetEntry() );
						uint8 dest;
						if( pet )
						{
							if( catcher->CanSummonPet( NULL_PET_SLOT, dest, pet, false ) == PET_ERR_OK )
							{
								*data << (uint16) SPELL_SUCCESS_CATCH;
								catched = true;
								pet->SetLoyalty(60);
								catcher->SummonPet(dest, pet);
								catcher->UpdatePetCarried();
								WorldPacket data2;
								catcher->BuildUpdateBlockTeam(&data2);
								catcher->SendMessageToSet(&data2, true);
								break;
							}
							sLog.outDebug("CAPTURE: Cannot summon pet '%s'", pet->GetName());
							delete pet;
						}
					}
				}
			}
		}

		default:
		{
			if( action->GetSkill() < 26000 )
				*data << (uint16) action->GetSkill(); // 0x2710 = basic spell
			else
				*data << (uint16) 19001;
		}
	}

	*data << (uint8 ) 0x01;

	*data << (uint8 ) sinfo->hit;

	UnitActionTurn hitInfo;
	BattleAction* hit;

	hitInfo.clear();
	hitInfo = ParseSpell(action, sinfo->hit, linked);
	while( !hitInfo.empty() )
	{
		hit = hitInfo.front();

		///- Override incorrect target for self target spell type, double check
		if( SPELL_DEFENSE == action->GetSkill() ||
			SPELL_ESCAPE  == action->GetSkill() )
		{
			*data << (uint8 ) hit->GetAttackerCol();
			*data << (uint8 ) hit->GetAttackerRow();
		} else {
			*data << (uint8 ) hit->GetTargetCol();
			*data << (uint8 ) hit->GetTargetRow();
		}

		Unit* attacker  = GetAttacker(hit);
		Unit* victim    = GetVictim(hit);
		point_inflicted = GetDamage(attacker, victim, sinfo, linked);
		point_modifier  = point_inflicted < 0 ? 1 : 0;
/*
		uint8 anim_impact = 0;
		uint8 anim_target = 0;
		switch( sinfo->DamageMod )
		{
			case SPELL_MOD_BUF:
			case SPELL_MOD_DISABLED_BUF:
			case SPELL_MOD_POSITIVE_BUF:
			case SPELL_MOD_NEGATIVE_BUF:
			case SPELL_MOD_DISABLED_HURT:
			{
				anim_impact = 1;
				anim_target = 1;
			} break;

			case SPELL_MOD_HURT:
			case SPELL_MOD_BUF_HURT:
			{
				double dice_missed = rand_chance();
				if( dice_missed > 90 && !catched )
				{
					point_inflicted = 0;
					point_modifier  = 0;
					anim_impact = 0;
					anim_target = 2;
				}
				else
				{
					anim_impact = 1;
					anim_target = 0;
				}
			} break;

			default:
			{
				anim_impact = 1;
				anim_target = 0;
			} break;
		}

		//impact animation. 0=miss; 1=hit
		//anim_impact = point_inflicted < 0 ? 1 : 0;
		*data << (uint8 ) anim_impact;

		//target animation. 0=hit; 1=def; 2=dodge;
		//uint8 anim_target = point_inflicted == 0 ? 2 : 0;
		// TODO: Fix for defensive stance
		*data << (uint8 ) anim_target;
*/
		sLog.outDebug("COMBAT: '%s' use <%s> to '%s'[%u,%u] deal %i dmg, %s", attacker->GetName(), sinfo->Name, victim->GetName(), hit->GetTargetCol(), hit->GetTargetRow(), point_inflicted, linked ? "linked" : "singled");
/*
		if( isDealDamageToAndKill(victim, point_inflicted) )
		{
			m_creatureKilled = true;
			AddKillExpGained(attacker, victim, linked);
			AddKillItemDropped(hit);
		}
		else
		{
			AddHitExpGained(attacker, victim, linked);
		}
*/
		/*
		// spell effect count, 1 for now, HP only
		*data << (uint8 ) 1;

		// affect status code, 0x19 = HP, 0xDD = (-) buf, 0xDE = (+) buf
		*data << (uint8 ) (point_inflicted == 0 ? 0xDD : 0x19);

		// affect value
		*data << (uint16) abs(point_inflicted);

		// affect modifier flag 0 = healing, 1 = hurting
		*data << (uint8 ) point_modifier;
		*/
		BuildUpdateBlockSpellMod(data, hit, sinfo, attacker, victim, point_inflicted, point_modifier, linked, catched);

		if( catched )
			m_BattleUnit[hit->GetTargetCol()][hit->GetTargetRow()] = NULL;

		hitInfo.pop_front();
	}

	///- Fixing action data length
	uint16 sz = data->size() - data_len - sizeof(data_len);
	data->put(data_len, ENCODE(sz));
	//sLog.outDebug("COMBAT: Action data size %u", sz);


	/*
	*data << (uint8 ) action->GetTargetCol();   // target col
	*data << (uint8 ) action->GetTargetRow();   // target row

	*data << (uint16) 0x0001;                   // unknown

	Unit* pAttacker = GetAttacker(action);
	Unit* pVictim   = GetVictim(action);
	point_inflicted = GetDamage(pAttacker, pVictim, point_inflicted);
	point_modifier  = point_inflicted < 0 ? 1 : 0;

	DealDamageTo(pVictim, point_inflicted);

	*data << (uint8 ) 0x01;    // spell effect count

	*data << (uint8 ) 0x19;    // spell status code effect 19 = HP
	*data << (uint16) abs(point_inflicted);  // spell effect value
	*data << (uint8 ) point_modifier;   // spell effect modifier flag 0 = heal, 1 = hurt

	*/

}

///- Building block for spell mod
void BattleSystem::BuildUpdateBlockSpellMod(WorldPacket *data, BattleAction* action, const SpellInfo* sinfo, Unit* attacker, Unit* victim, int32 damage, uint8 modifier, bool linked, bool catched)
{
	bool missed = false;
	uint8 ani_i = ANI_IMPACT_HIT;
	uint8 ani_t = ANI_TARGET_HIT;

	///- Determine missed attack
	double dice_missed = rand_chance();
	if( (dice_missed > 90 && !catched &&
		 sinfo->DamageMod != SPELL_MOD_POSITIVE_BUF &&
		 sinfo->DamageMod != SPELL_MOD_HEAL)
		|| victim->hasUnitState(UNIT_STATE_DODGE))
	{
		damage = 0;
		modifier = 0;
		missed = true;
	}

	uint8 stcode = 0;
	switch( sinfo->DamageMod )
	{
	case SPELL_MOD_DISABLED_HURT:
	case SPELL_MOD_DISABLED_BUF: stcode = 0xDD; ani_t = ANI_TARGET_HIT; break;
	case SPELL_MOD_POSITIVE_BUF: stcode = 0xDE; ani_t = ANI_TARGET_DEF; break;
	case SPELL_MOD_NEGATIVE_BUF: stcode = 0xDF; ani_t = ANI_TARGET_DEF; break;
	case SPELL_MOD_HEAL:         stcode = 0x19; ani_t = ANI_TARGET_DEF; break;

	case SPELL_MOD_HURT:
	default: stcode = 0x19; ani_t = ANI_TARGET_HIT; break;
	}

	int32 state = 0;
	uint8 dur = 0;
	switch( sinfo->Entry )
	{
		///- Disabled state
		case SPELL_TREESPIRIT: state = UNIT_STATE_ROOT;      dur = 3; break;
		case SPELL_FREEZING:   state = UNIT_STATE_FREEZE;    dur = 5; break;
		case SPELL_STUN:       state = UNIT_STATE_STUN;      dur = 2; break;
		case SPELL_CYCLONE:    state = UNIT_STATE_CYCLONE;   dur = 3; break;

		///- (+) Buf state
		case SPELL_ICEWALL:    state = UNIT_STATE_ICEWALL;   dur = 2; break;
		case SPELL_AURA:       state = UNIT_STATE_AURA;      dur = 4; break;
		case SPELL_MIRROR:     state = UNIT_STATE_MIRROR;    dur = 4; break;
		case SPELL_VIGOR:      state = UNIT_STATE_VIGOR;     dur = 4; break;
		case SPELL_DODGE:      state = UNIT_STATE_DODGE;     dur = 2; break;
		case SPELL_INVISIBLE:  state = UNIT_STATE_INVISIBLE; dur = 4; break;
		case SPELL_MAGNIFY:    state = UNIT_STATE_MAGNIFY;   dur = 4; break;

		///- (-) Buf state
		case SPELL_MINIFY:     state = UNIT_STATE_MINIFY;    dur = 4; break;
	}

	///- Check if unit state hitted and apply it
	if( !missed && stcode != 0x19 && state && dur )
		if( !victim->addUnitState(state, dur) )
		{
			missed = true;
			stcode = 0;
		}

	if( !missed )
		if( isDealDamageToAndKill(victim, damage) )
		{
			m_creatureKilled = true;
			AddKillExpGained(attacker, victim, linked);
			AddKillItemDropped(action);
		}
		else
			AddHitExpGained(attacker, victim, linked);

	if(!missed && victim->hasUnitState(UNIT_STATE_DEFEND | UNIT_STATE_ICEWALL))
		ani_t = ANI_TARGET_DEF;

	if(!missed && victim->hasUnitState(UNIT_STATE_AURA | UNIT_STATE_MIRROR))
	{
		ani_t = ANI_TARGET_NONE;
		damage = 0;
	}

	if(!missed && sinfo->Entry == SPELL_ESCAPE)
		missed = true;

	*data << (uint8 ) (missed ? ANI_IMPACT_MISS : ani_i);
	*data << (uint8 ) (missed ? ANI_TARGET_DODGE : ani_t);

	// spell effect count
	if( damage != 0 && stcode != 0x19 )
	{
		*data << (uint8 ) 2;
		*data << (uint8 ) 0x19;
		*data << (uint16) abs(damage);
		*data << (uint8 ) modifier;
	}
	else
		*data << (uint8 ) 1;

	// affect status code, 0x19 = HP, 0xDD = (-) buf, 0xDE = (+) buf
	//*data << (uint8 ) (damage == 0 ? 0xDD : 0x19);
	*data << (uint8 ) stcode;

	// affect value
	*data << (uint16) (stcode == 0x19 ? abs(damage) : 0);

	// affect modifier flag, 0 = healing, 1 = hurting
	*data << (uint8 ) modifier;
}

///- Building block for area spell hit, including single hit
UnitActionTurn BattleSystem::ParseSpell(BattleAction* action, uint8 hit, bool linked)
{
	UnitActionTurn hitInfo;

	uint8  a = action->GetAttackerCol();
	uint8  b = action->GetAttackerRow();
	uint16 s = action->GetSkill();
	uint8  x = action->GetTargetCol();
	uint8  y = action->GetTargetRow();

	sLog.outDebug("COMBAT: Parsing spell <%s> targeting %u primary [%u,%u]", action->GetProto()->Name, hit, x, y);
	switch( hit )
	{
	///- Rolling stone model type
	//   add primary and front/back
	case 2:
	{
		/// Primary
		sLog.outDebug("PARSER: %u,%u Primary hit", x, y);
		hitInfo.push_back(action);

		if( isDefPos( x ) )
		{
			/// Front
			if( isDefPosBackRow( x ) )
			{
				if( isUnitAvail( x+1, y) )
				{
				sLog.outDebug("PARSER: %u,%u Front hit", x+1, y);
					hitInfo.push_back(new BattleAction(a,b,s,x+1,y));
				}
			}
			/// Back
			else 
			{
				if( isUnitAvail( x-1, y) )
				{
					sLog.outDebug("PARSER: %u,%u Back hit", x-1, y);
					hitInfo.push_back(new BattleAction(a,b,s,x-1,y));
			}
			}
		}
		else if (isAtkPos( x ) )
		{
			/// Front
			if( isAtkPosBackRow( x ) )
			{
				if( isUnitAvail( x+1, y ) )
				{
					sLog.outDebug("PARSER: %u,%u Front hit", x+1, y);
					hitInfo.push_back(new BattleAction(a,b,s,x+1,y));
				}
			}
			/// Back
			else
			{
				if( isUnitAvail( x-1, y ) )
				{
					sLog.outDebug("PARSER: %u,%u Back hit", x-1, y);
					hitInfo.push_back(new BattleAction(a,b,s,x-1,y));
				}
			}
		}
		break;
	}

	///- Wild Fire Chop model type
	//   add front upper + bottom / back upper + bottom, rest will handled in
	//   4 & 3 hit model type
	case 6:
	{
		if( isDefPos( x ) )
		{
			if( isDefPosBackRow( x ) )
			{
				/// Front Upper
				if( isUnitAvail( x+1, y-1 ) )
				{
					sLog.outDebug("PARSER: %u,%u Front Upper hit", x+1, y-1);
					hitInfo.push_back(new BattleAction(a,b,s,x+1,y-1));
				}
				/// Front Bottom
				if( isUnitAvail( x+1, y+1 ) )
				{
					sLog.outDebug("PARSER: %u,%u Front Bottom hit", x+1, y+1);
					hitInfo.push_back(new BattleAction(a,b,s,x+1,y+1));
				}
			}
			else
			{
				/// Back Upper
				if( isUnitAvail( x-1, y-1 ) )
				{
					sLog.outDebug("PARSER: %u,%u Back Upper hit", x-1, y-1);
					hitInfo.push_back(new BattleAction(a,b,s,x-1,y-1));
				}
				/// Back Bottom
				if( isUnitAvail( x-1, y+1 ) )
				{
					sLog.outDebug("PARSER: %u,%u Back Bottom hit", x-1, y+1);
					hitInfo.push_back(new BattleAction(a,b,s,x-1,y+1));
				}
			}
		}
		else if( isAtkPos( x ) )
		{
			if( isAtkPosBackRow( x ) )
			{
				/// Front Upper
				if( isUnitAvail( x-1, y-1 ) )
				{
					sLog.outDebug("PARSER: %u,%u Front Upper hit", x-1, y-1);
					hitInfo.push_back(new BattleAction(a,b,s,x-1,y-1));
				}
				/// Front Bottom
				if( isUnitAvail( x-1, y+1 ) )
				{
					sLog.outDebug("PARSER: %u,%u Front Bottom hit", x-1, y+1);
					hitInfo.push_back(new BattleAction(a,b,s,x-1,y+1));
				}
			}
			else
			{
				/// Back Upper
				if( isUnitAvail( x+1, y-1 ) )
				{
					sLog.outDebug("PARSER: %u,%u Back Upper hit", x+1, y-1);
					hitInfo.push_back(new BattleAction(a,b,s,x+1,y-1));
				}
				/// Back Bottom
				if( isUnitAvail( x+1, y+1 ) )
				{
					sLog.outDebug("PARSER: %u,%u Back Bottom hit", x+1, y+1);
					hitInfo.push_back(new BattleAction(a,b,s,x+1,y+1));
				}
			}
		}
	}

	///- Hail stone model type
	//   add front/back only, rest will added in 3 hit model type
	case 4:
	{
		if( isDefPos( x ) )
		{
			if( isDefPosBackRow( x ) )
			{
				/// Front
				if( isUnitAvail( x+1, y ) )
				{
					sLog.outDebug("PARSER: %u,%u Front hit", x+1, y);
					hitInfo.push_back(new BattleAction(a,b,s,x+1,y));
				}
			}
			else
			{
				/// Back
				if( isUnitAvail( x-1, y ) )
				{
					sLog.outDebug("PARSER: %u,%u Back hit", x-1, y);
					hitInfo.push_back(new BattleAction(a,b,s,x-1,y));
				}
			}
		}
		else if( isAtkPos( x ) )
		{
			if( isAtkPosBackRow( x ) )
			{
				/// Front
				if( isUnitAvail( x-1, y ) )
				{
					sLog.outDebug("PARSER: %u,%u Front hit", x-1, y);
					hitInfo.push_back(new BattleAction(a,b,s,x-1,y));
				}
			}
			else
			{
				/// Back
				if( isUnitAvail( x+1, y ) )
				{
					sLog.outDebug("PARSER: %u,%u Back hit", x+1, y);
					hitInfo.push_back(new BattleAction(a,b,s,x+1,y));
				}
			}
		}
	//	break;
	}

	///- Fire Arrow hit model type, add left and right from primary target
	//   Berserker will have a special case when linking action
	case 3:
	{
		/// Primary
		sLog.outDebug("PARSER: %u,%u Primary hit", x, y);
		hitInfo.push_back(action);

		/// Upper
		if( isUnitAvail( x, y-1 ) || linked )
		{
			// berserker special case when linked action
			if( s == SPELL_BERSERKER && !isUnitAvail( x, y-1 ) && !linked )
			{
				sLog.outDebug("PARSER: %u,%u Upper to Primary hit", x, y);
				hitInfo.push_back(action);
			}
			else
			{
				if( s == SPELL_BERSERKER && !isUnitAvail( x, y-1 ) && linked )
				{
					sLog.outDebug("PARSER: %u,%u Upper to Primary hit", x, y);
					hitInfo.push_back(action);
				}
				else
				{
					if( isUnitAvail( x, y-1, linked ) )
					{
						sLog.outDebug("PARSER: %u,%u Upper hit", x, y-1);
						hitInfo.push_back(new BattleAction(a,b,s,x,y-1));
					}
				}
			}
		}
		else if( s == SPELL_BERSERKER )
		{
			sLog.outDebug("PARSER: %u,%u Upper to Primary hit", x, y);
			hitInfo.push_back(action);
		}

		/// Bottom
		if( isUnitAvail( x, y+1 ) || linked )
		{
			// berserker special case when linked action
			if( s == SPELL_BERSERKER && !isUnitAvail( x, y+1 ) && !linked )
			{
				sLog.outDebug("PARSER: %u,%u Bottom to Primary hit", x, y);
				hitInfo.push_back(action);
			}
			else
			{
				if( s == SPELL_BERSERKER && !isUnitAvail( x, y+1 ) && linked )
				{
					sLog.outDebug("PARSER: %u,%u Bottom to Primary hit", x, y);
					hitInfo.push_back(action);
				}
				else
				{
					if( isUnitAvail( x, y+1, linked ) )
					{
						sLog.outDebug("PARSER: %u,%u Bottom hit", x, y+1);
						hitInfo.push_back(new BattleAction(a,b,s,x,y+1));
					}
				}
			}
		}
		else if ( s == SPELL_BERSERKER )
		{
			sLog.outDebug("PARSER: %u,%u Bottom to Primary hit", x, y);
			hitInfo.push_back(action);
		}

		break;
	}

	///- Flood model type
	case 5:
	{
		/// Primary
		sLog.outDebug("PARSER: %u,%u Primary hit", x, y);
		hitInfo.push_back(action);

		/// Column line
		for(uint8 r = 0; r < BATTLE_ROW_MAX; r++)
			if( r == y )
				continue; // primary already pushed
			else
			{
				if( isUnitAvail(x, r) )
				{
					sLog.outDebug("PARSER: %u,%u Column hit", x, r);
					hitInfo.push_back(new BattleAction(a,b,s,x,r));
				}
			}
		break;
	}

	///- Inferno model type
	//   just push all target side
	case 10:
	{
		/// Primary
		sLog.outDebug("PARSER: %u,%u Primary hit", x, y);
		hitInfo.push_back(action);

		/// The Rest all of them
		if( isDefPos( x ) )
		{
			for(uint8 r = 0; r < BATTLE_ROW_MAX; r++)
				for(uint8 c = 0; c < DEFENDER_COL_MAX; c++)
					if( isUnitAvail( c, r ) )
						if( c == x && r == y )
							continue; // primary already pushed
						else
						{
							sLog.outDebug("PARSER: %u,%u Area hit", c, r);
							hitInfo.push_back(new BattleAction(a,b,s,c,r));
						}

		}
		else if( isAtkPos( x ) )
		{
			for(uint8 r = 0; r < BATTLE_ROW_MAX; r++)
				for(uint8 c = ATTACKER_COL_MIN; c < BATTLE_COL_MAX; c++)
					if( isUnitAvail( c, r ) )
						if( c == x && r == y )
							continue; // already pushed
						else
						{
							sLog.outDebug("PARSER: %u,%u Area hit", c, r);
							hitInfo.push_back(new BattleAction(a,b,s,c,r));
						}
		}

		break;
	}

	///- Basic hit model type
	case 1:
	default:
	{
		if( isUnitAvail( x, y ) )
		{
			sLog.outDebug("PARSER: %u,%u Primary hit", x, y);
			hitInfo.push_back(action);
		}
		break;
	}

	} // end switch

	///- sort it before sending back, for better animation
	//hitInfo.sort(compare_target_position);

	return hitInfo;
}

///- Calculate point inflicted to victim
//   negative: hurt modifier
//   positive: heal modifier
//   zero    : buf/miss modifier
int32 BattleSystem::GetDamage(Unit* attacker, Unit* victim, const SpellInfo* sinfo, bool linked)
{
	///- Check Aura spell buf
	if( victim->hasUnitState(UNIT_STATE_AURA) )
		return 0;

	int32 dmg_mod = 0;

	switch( sinfo->DamageMod )
	{
		case SPELL_MOD_NONE:         // mod none = 0
		case SPELL_MOD_BUF:          // mod buf = 1
		case SPELL_MOD_DISABLED_BUF: // mod disabled = 8
		case SPELL_MOD_POSITIVE_BUF: // mod (+) buf = 16
		case SPELL_MOD_NEGATIVE_BUF: // mod (-) buf = 32
		{
			return 0;
		} break;
		case SPELL_MOD_HURT:     // mod hurt = 2
		case SPELL_MOD_BUF_HURT: // mod hurt + buf = 3
		{
			dmg_mod = -1;
		} break;
		case SPELL_MOD_HEAL:    // mod heal = 6
		{
			dmg_mod = 1;
		} break;

		case SPELL_MOD_DISABLED_HURT: // mod disabled + hurt = 10
		default:
		{
			dmg_mod = -1;
		} break;
	}

	int   diffLevel = attacker->getLevel() - victim->getLevel();
	diffLevel = (diffLevel > 0 ? diffLevel : 1);

	uint8 a_el = attacker->GetUInt32Value(UNIT_FIELD_ELEMENT);
	uint8 v_el = victim->GetUInt32Value(UNIT_FIELD_ELEMENT);

	float dmg_multiplier = GetDamageMultiplier(a_el, v_el, sinfo);

	float dmg_school = 0;

	uint16 atk_pow = attacker->GetAttackPower();
	uint16 mag_pow = attacker->GetMagicPower();
	uint16 def_pow = victim->GetDefensePower();
	uint8  spell_level = attacker->GetSpellLevel(sinfo->Entry);

	switch( sinfo->Type )
	{
		case SPELL_DAMAGE_SCHOOL:
		case SPELL_DAMAGE_INT:
		case SPELL_DAMAGE_MECH:
		{
			if( sinfo->Entry == SPELL_BASIC )
				dmg_school = atk_pow;
			else
				dmg_school = mag_pow;

			break;
		}

		case SPELL_DAMAGE_ATK_INT:
		{
			dmg_school = atk_pow + (mag_pow / 2);
			break;
		}

		default:
			dmg_school = atk_pow;
			break;
	}

	///- Add Level bonus to produce pure damage school
	dmg_school = dmg_school + (diffLevel * 0.01);

	sLog.outDebug("DAMAGE: Pure Damage School is %.2f", dmg_school);

	///- Damage dealed calculated by spell level, learn point and mana used
	float dmg = 0;
	if( sinfo->LearnPoint && sinfo->SP )
		dmg = dmg_school + (dmg_school * spell_level * (sinfo->LearnPoint * 0.01) * (sinfo->SP * 0.1));
	else
		dmg = dmg_school + (dmg_school * spell_level);

	dmg /= sinfo->hit;
	dmg = dmg * dmg_multiplier;

	sLog.outDebug("DAMAGE: Damage rate per hit is %.2f", dmg);

	///- Linked attack bonus damage 25%
	if( linked )
		dmg = dmg + (dmg * 0.25);

	///- Reduce damage from defense, hurt type only
	if( sinfo->DamageMod == SPELL_MOD_HURT )
	{
		dmg = dmg - (dmg * (def_pow * 0.01));

		sLog.outDebug("DAMAGE: Reduced damage is %.2f", dmg);

		if( dmg <= 0 )
			dmg = 1;
	}

	///- Min/Max damage tolerance, 25% maybe too low
	//   need adjustmest for balance game play
	float dmg_min = dmg - (dmg * 0.25);
	float dmg_max = dmg;

	///- Randomize damage using min - max
	dmg = (int32) irand(int32(dmg_min), int32(dmg_max));

	sLog.outDebug("DAMAGE: %s randomize {%4.2f <%4.2f> %4.2f}", GetDamageModText(sinfo->DamageMod), dmg_min, dmg, dmg_max);

	///- Minimum damage for hurt/heal type mod, last check
	if( dmg <= 0 && sinfo->DamageMod == SPELL_MOD_HURT )
		dmg = 1;

	return (int32) (dmg * dmg_mod);
}

///- Multiplier for element vs element
//   = 1 if no multiplier
//   < 1 if weaker
//   > 1 if stronger
float BattleSystem::GetDamageMultiplier(uint8 el1, uint8 el2, const SpellInfo* sinfo)
{
	float m = 1;

	switch( el1 )
	{
		case ELEMENT_EARTH:
		{
			if(el2 == ELEMENT_WATER)
				m += 0.75;
			else if(el2 == ELEMENT_WIND)
				m -= 0.25;
		} break;

		case ELEMENT_WATER:
		{
			if(el2 == ELEMENT_FIRE)
				m += 0.75;
			else if(el2 == ELEMENT_EARTH)
				m -= 0.25;
		} break;

		case ELEMENT_FIRE:
		{
			if(el2 == ELEMENT_WIND)
				m += 0.75;
			else if(el2 == ELEMENT_WATER)
				m -= 0.25;
		} break;

		case ELEMENT_WIND:
		{
			if(el2 == ELEMENT_EARTH)
				m += 0.75;
			else if(el2 == ELEMENT_FIRE)
				m -= 0.25;
		} break;
	}

	switch( sinfo->Element )
	{
		case ELEMENT_EARTH:
		{
			if(el2 == ELEMENT_WATER)
				m += 0.5;
			else if(el2 == ELEMENT_WIND)
				m -= 0.5;
		} break;

		case ELEMENT_WATER:
		{
			if(el2 == ELEMENT_FIRE)
				m += 0.5;
			else if(el2 == ELEMENT_EARTH)
				m -= 0.5;
		} break;

		case ELEMENT_FIRE:
		{
			if(el2 == ELEMENT_WIND)
				m += 0.5;
			else if(el2 == ELEMENT_WATER)
				m -= 0.5;
		} break;

		case ELEMENT_WIND:
		{
			if(el2 == ELEMENT_EARTH)
				m += 0.5;
			else if(el2 == ELEMENT_FIRE)
				m -= 0.5;
		} break;
	}

	return m;
}

const char* BattleSystem::GetDamageModText(uint32 dmg_mod)
{
	switch( dmg_mod )
	{
		case SPELL_MOD_NONE:
			return "NONE";
		case SPELL_MOD_BUF:
			return "BUF";
		case SPELL_MOD_HURT:
			return "HURT";
		case SPELL_MOD_BUF_HURT:
			return "BUF_HURT";
		case SPELL_MOD_HEAL:
			return "HEAL";
		default:
			return "UNKNOWN";
	}
}

bool BattleSystem::isDealDamageToAndKill(Unit* victim, int32 damage)
{
	uint32 currentHP = victim->GetUInt32Value(UNIT_FIELD_HP);

	///- Damage Type
	if(damage < 0)
	{
		if( currentHP <= abs(damage) )
		{
			victim->SetUInt32Value(UNIT_FIELD_HP, 0);
			victim->setDeathState(DEAD);
			sLog.outString("COMBAT: ## '%s' killed ##", victim->GetName());
			return true;
		}
		
		currentHP += damage;
		victim->SetUInt32Value(UNIT_FIELD_HP, currentHP);

		return false;
	}
	///- Heal Type
	else
	{
	}

	if( victim->isType(TYPE_PET) )
		((Pet*)victim)->SetState(PET_CHANGED);

	return false;
}

void BattleSystem::SendAttack(BattleAction* action, int32 damage)
{
	Unit* pAttacker = m_BattleUnit[action->GetAttackerCol()][action->GetAttackerRow()];
	Unit* pVictim = m_BattleUnit[action->GetTargetCol()][action->GetTargetRow()];
	sLog.outString(" ++ Unit action %s [%u,%u] use skill %u to %s [%u,%u] deals %i point", pAttacker->GetName(), action->GetAttackerCol(), action->GetAttackerRow(), action->GetSkill(), pVictim->GetName(), action->GetTargetCol(), action->GetTargetRow(), damage);

	WorldPacket data;
	data.Initialize( 0x32 );
	data << (uint8 ) 0x01;
	data << (uint8 ) 0x0F;
	data << (uint8 ) 0x00;
	data << action->GetAttackerCol(); // attacker col
	data << action->GetAttackerRow(); // attacker row
	data << action->GetSkill(); // 0x2710 = basic punch
	data << (uint8 ) 0x01;
	data << (uint8 ) 0x01;
	data << action->GetTargetCol(); // target col
	data << action->GetTargetRow(); // target row
	data << (uint8 ) 0x01;
	data << (uint8 ) 0x00;
	data << (uint8 ) 0x01;
	data << (uint8 ) 0x19;

	uint16 point_inflicted = abs(damage);
	uint8  point_modifier  = damage < 0 ? 1 : 0;
	data << point_inflicted; //(uint16) damage;    // actual hit value
	data << point_modifier; //(uint8 ) 0x01;      // flag 0 = heal, 1 = damage
	//pSession->SendPacket(&data);
	SendMessageToSet(&data);
	WaitForAnimation(action->GetSkill());
}

void BattleSystem::SendComboAttack(BattleAction* action, int32 damage)
{
	sLog.outString(" ++ Unit action [%u,%u] use skill %u to [%u,%u] deals %i point", action->GetAttackerCol(), action->GetAttackerRow(), action->GetSkill(), action->GetTargetCol(), action->GetTargetRow(), damage);

	WorldPacket data;
	data.Initialize( 0x32 );
	data << (uint8 ) 0x01;
	data << (uint8 ) 0x0F;
	data << (uint8 ) 0x00;
	data << (uint8 ) action->GetAttackerCol();
	data << (uint8 ) action->GetAttackerRow();
	data << (uint16) action->GetSkill();
	data << (uint8 ) 0x01;
	data << (uint8 ) 0x01;
	data << (uint8 ) action->GetTargetCol();
	data << (uint8 ) action->GetTargetRow();
	data << (uint8 ) 0x01;
	data << (uint8 ) 0x00;
	data << (uint8 ) 0x01;
	data << (uint8 ) 0x19;
	uint16 point_inflicted = abs(damage);
	uint8  point_modifier  = damage < 0 ? 1 : 0;
	data << point_inflicted; //(uint16) damage;    // actual hit value
	data << point_modifier; //(uint8 ) 0x01;      // flag 0 = heal, 1 = damage

	///- Add this for combo
/*
	data << (uint8 ) 0x01;
	data << (uint8 ) 0x0F;
	data << (uint8 ) 0x00;
	data << (uint8 ) 0x02;
	data << (uint8 ) 0x02;
	data << (uint16) 0x2710;
	data << (uint8 ) 0x01;
	data << (uint8 ) 0x01;
	data << (uint8 ) action->GetTargetCol(); //(uint8 ) 0x00;
	data << (uint8 ) action->GetTargetRow(); //(uint8 ) 0x01;
	data << (uint8 ) 0x01;
	data << (uint8 ) 0x00;
	data << (uint8 ) 0x01;
	data << (uint8 ) 0x19;
	data << (uint8 ) 0x68;
	data << (uint8 ) 0x01;
	data << (uint8 ) 0x01;
*/
	//pSession->SendPacket(&data);
	SendMessageToSet(&data);
	WaitForAnimation(action->GetSkill());
}

void BattleSystem::WaitForAnimation(uint16 spell)
{
	// 220 = 22 sec
	// 20  = 2 sec
	// 12  = 1.2 sec
	uint32 msec;
	if( spell == SPELL_DEFENSE )
		msec = 0;
	else if( spell == SPELL_BASIC ||
			 spell == SPELL_ESCAPE )
		msec = 12; //msec = 1200;
	else if( spell > 26000 )
		msec = 40;
	else
		msec = 25; //msec = 2500;
	//ZThread::Thread::sleep(msec);

	m_animationTime = msec;
}

bool BattleSystem::isActionTimedOut()
{
	if( !m_waitForAction )
		return false;

	m_actionTime++;
//	sLog.outString("Action Time %u tick", m_actionTime);
	if( m_actionTime < MAX_ACTION_TIME )
		return false;

	return true;
}
/*
void BattleSystem::Overkilled(uint8 col, uint8 row)
{
	WorldPacket data;
	data.Initialize( 0x35 );
	data << (uint8 ) 0x03; // overkilled
	data << (uint8 ) 0x00; // col
	data << (uint8 ) 0x03; // row
	pSession->SendPacket(&data);
}
*/

bool BattleSystem::BattleConcluded()
{
	///- wait for action animation
	if( m_animationTime > 0 )
		return false;

	///- Give item if battle last only 1 turn
	GiveItemDropped();

	///- Wait for item dropped animation
	if( m_animationTimeItemDropped > 0 )
		return false;

	bool lastManStanding = false;
	for(uint8 row = 0; row < BATTLE_ROW_MAX; row++)
	{
		for(uint8 col = 0; col < DEFENDER_COL_MAX; col++)
		{
			Unit* unit = m_BattleUnit[col][row];
			if( !unit )
				continue;

			if( unit->isAlive() )
			{
				lastManStanding = true;
				break;
			}
		}
		if( lastManStanding )
			break;
	}

	if( !lastManStanding )
	{
		return true;
	}

	for(uint8 row = 0; row < BATTLE_ROW_MAX; row++)
	{
		for(uint8 col = ATTACKER_COL_MIN; col < BATTLE_COL_MAX; col++)
		{
			Unit* unit = m_BattleUnit[col][row];
			if( !unit )
				continue;

			if( unit->isAlive() )
				return false;
		}
	}

	return true;
}

void BattleSystem::BattleStop()
{
	///- wait for 14 06 first

	///- wait for item dropped animation, can be skipped for faster battle end
	if( m_animationTimeItemDropped > 0 )
		return;

	WorldPacket data;

//	GiveExpGained();

	for(uint8 row = 0; row < BATTLE_ROW_MAX; row++)
		for(uint8 col = 0; col < BATTLE_COL_MAX; col++)
		{
			Unit* unit = m_BattleUnit[col][row];
			if( !unit ) continue;
			if( unit->isType(TYPE_PLAYER) || unit->isType(TYPE_PET) )
			{
				if( unit->isType(TYPE_PLAYER) )
					GiveExpGainedFor((Player*)unit);

				data.Initialize( 0x0B );
				data << (uint8 ) 0x01;
				data << (uint8 ) col;
				data << (uint8 ) row;
				data << (uint8 ) 0;
				SendMessageToSet(&data);
			}
		}

	for(PlayerListMap::const_iterator itr = m_PlayerList.begin(); itr != m_PlayerList.end(); ++itr)
	{
		(*itr)->Send1406();
		(*itr)->Send1408();
		LeaveBattle((*itr));
	}
	m_PlayerList.clear();

	///- Update Smoke dissapear
	data.Initialize( 0x0B );
	data << (uint8 ) 0;
	data << (uint32) i_master->GetAccountId();
	data << (uint16) 0; // unknown;
	i_master->SendMessageToSet(&data, false);
}

void BattleSystem::SendMessageToSet(WorldPacket * packet, bool log)
{
	for(PlayerListMap::const_iterator itr = m_PlayerList.begin(); itr != m_PlayerList.end(); ++itr)
	{
		if(!(*itr)) continue;
		if(!(*itr)->GetSession()) continue;
		SendMessageToPlayer((*itr), packet, log);

		//if( log ) (*itr)->GetSession()->SetLogging(true);
		//(*itr)->GetSession()->SendPacket(packet);
		//if( log ) (*itr)->GetSession()->SetLogging(false);
	}

}

///- Purpose only to send trigger screen to attackers
void BattleSystem::SendMessageToAttacker(WorldPacket * packet, bool log)
{
	uint8 col = 3;
	for(uint8 row = 0; row < BATTLE_ROW_MAX; row++)
	{
		Unit* unit = m_BattleUnit[col][row];
		if( !unit )
			continue;

		if( !unit->isType(TYPE_PLAYER) )
			continue;

		SendMessageToPlayer(((Player*)unit), packet, log);
	}
}

///- Purpose only to send trigger screen to defenders
void BattleSystem::SendMessageToDefender(WorldPacket * packet, bool log)
{
	uint8 col = 0;
	for(uint8 row = 0; row < BATTLE_ROW_MAX; row++)
	{
		Unit* unit = m_BattleUnit[col][row];
		if( !unit )
			continue;

		if( !unit->isType(TYPE_PLAYER) )
			continue;

		SendMessageToPlayer(((Player*)unit), packet, log);
	}
}

void BattleSystem::SendMessageToPlayer(Player* player, WorldPacket* packet, bool log)
{
	if( !player )
		return;

	if( !player->GetSession() )
		return;

	//sLog.outDebug("BATTLE: Send Message to '%s'", player->GetName());
	player->GetSession()->SendPacket(packet, log);
}

bool BattleSystem::CanJoin() const
{
	return false;
}

void BattleSystem::JoinBattle(Player* player)
{
	sLog.outDebug("BATTLE: Player '%s' joining battle", player->GetName());
	m_PlayerList.insert(player);

	player->SetBattleMaster(i_master);
}

void BattleSystem::LeaveBattle(Player* player)
{
	if(!player) return;

	sLog.outDebug("BATTLE: Player '%s' leaving battle", player->GetName());

	///- Prevent dead player walking
	if( player->isDead() || player->GetHealth() == 0 )
	{
		player->SetUInt32Value(UNIT_FIELD_HP, 1);
		player->setDeathState(ALIVE);
	}
	player->ResetAllState();
	player->UpdatePlayer();

	Pet* pet = player->GetBattlePet();
	if( pet )
	{
		if( pet->isDead() || pet->GetHealth() == 0 )
		{
			pet->SetUInt32Value(UNIT_FIELD_HP, 1);
			pet->setDeathState(ALIVE);
		}
		pet->ResetAllState();
		player->UpdatePetBattle();
	}

	///- forced to save, if database I/O load get too high, disable this
	player->SaveToDB();

	///- Clean player list, if not necessary, disable this
	PlayerListMap::iterator it = m_PlayerList.find(player);
	if( it != m_PlayerList.end() )
	{
		(*it)->SetBattleMaster(NULL);
		m_PlayerList.erase(it);
	}

	for(uint8 row = 0; row < BATTLE_ROW_MAX; row++)
		for(uint8 col = 0; col < BATTLE_COL_MAX; col++)
		{
			if( m_BattleUnit[col][row] == player )
				m_BattleUnit[col][row] = NULL;

			if( !pet ) continue;

			if( m_BattleUnit[col][row] == pet )
				m_BattleUnit[col][row] = NULL;
		}
}

bool BattleSystem::SameSide(const BattleAction* prev, const BattleAction* next)
{
	if( !prev )
	{
		sLog.outDebug("BATTLE: SAME SIDE, prev is null");
		return true;
	}

	if( isDefPos( prev->GetAttackerCol() ) )
		if( isDefPos( next->GetAttackerCol() ) )
		{
			sLog.outDebug("BATTLE: SAME SIDE, defender side");
			return true;
		}
		else
			return false;
	else
		if( isDefPos( next->GetAttackerCol() ) )
			return false;
		else
		{
			sLog.outDebug("BATTLE: SAME SIDE, attacker side");
			return true;
		}

	sLog.outDebug("BATTLE: UNKNOWN SIDE");
	return false;
}

bool BattleSystem::isDefPos(uint8 col)
{
	if ( col < DEFENDER_COL_MAX )
		return true;
	else if( col >= ATTACKER_COL_MIN )
		return false;
}

bool BattleSystem::isAtkPos(uint8 col)
{
	return !isDefPos(col);
}

bool BattleSystem::isDefPosBackRow(uint8 col)
{
	if( !isDefPos(col) )
		return false;

	if( col == DEFENDER_COL_MAX - 1 )
		return false;

	return true;
}

bool BattleSystem::isAtkPosBackRow(uint8 col)
{
	if( !isAtkPos(col) )
		return false;

	if( col == ATTACKER_COL_MIN )
		return false;

	return true;
}

Unit* BattleSystem::GetBattleUnit(uint8 col, uint8 row)
{
	if( col > BATTLE_COL_MAX || row > BATTLE_ROW_MAX )
	{
		sLog.outDebug("BATTLE: GetBattleUnit out of battle range @[%u,%u]", col, row);
		return NULL;
	}

	if( !m_BattleUnit[col][row] )
	{
		sLog.outDebug("BATTLE: GetBattleUnit empty position @[%u,%u]", col, row);
		return NULL;
	}

	return m_BattleUnit[col][row];
}

bool BattleSystem::isUnitAvail(uint8 col, uint8 row, bool linked)
{
	Unit* unit = GetBattleUnit(col, row);

	if( unit )
		if( !unit->isDead() || linked)
			return true;

	return false;
}

void BattleSystem::AddKillExpGained(Unit* killer, Unit* victim, bool linked)
{
	//sLog.outDebug("EXPERIENCE: Killer type is %u, Victim type is %u", killer->GetTypeId(), victim->GetTypeId());
	if( victim->isType(TYPE_PLAYER) || victim->isType(TYPE_PET) )
	{
		sLog.outDebug("EXPERIENCE KILL: Victim is not creature");
		return;
	}

	if( !killer->isType(TYPE_PLAYER) && !killer->isType(TYPE_PET) )
	{
		sLog.outDebug("EXPERIENCE KILL: Killer is creature");
		return;
	}

	killer->AddKillExp(victim->getLevel(), linked);
	if( killer->isType(TYPE_PET) )
		((Pet*)killer)->SetState(PET_CHANGED);

}

void BattleSystem::AddHitExpGained(Unit* hitter, Unit* victim, bool linked)
{
	if( victim->isType(TYPE_PLAYER) || victim->isType(TYPE_PET) )
	{
		sLog.outDebug("EXPERIENCE HIT: Victim is not creature");
		return;
	}

	if( !hitter->isType(TYPE_PLAYER) && !hitter->isType(TYPE_PET) )
	{
		sLog.outDebug("EXPERIENCE HIT: Killer is creature");
		return;
	}

	hitter->AddHitExp(victim->getLevel(), linked);
	if( hitter->isType(TYPE_PET) )
		((Pet*)hitter)->SetState(PET_CHANGED);
}

void BattleSystem::AddKillItemDropped(BattleAction* action)
{
	double dice = rand_chance();
	double drop_rate = 100 - (33.33 * sWorld.getRate(RATE_DROP_ITEMS));
	sLog.outDebug("ITEM DROPPED: Chance for drop is %-3.2f versus rate %-3.2f", dice, drop_rate);
	if( dice < drop_rate )
		return;

	uint8 a = action->GetAttackerCol();
	uint8 b = action->GetAttackerRow();
	uint8 x = action->GetTargetCol();
	uint8 y = action->GetTargetRow();

	Unit* receiver    = GetBattleUnit(a, b);
	Unit* contributor = GetBattleUnit(x, y);

	if( !receiver || !contributor)
		return;

	if( contributor->isType(TYPE_PLAYER) || contributor->isType(TYPE_PET) )
	{
		sLog.outDebug("ITEM DROPPED: Contributor is not creature");
		return;
	}

	if( !receiver->isType(TYPE_PLAYER) && !receiver->isType(TYPE_PET) )
	{
		sLog.outDebug("ITEM DROPPED: Receiver is creature");
		return;
	}

	Player* player;
	if( receiver->isType(TYPE_PLAYER) )
		player = (Player*) receiver;
	else if( receiver->isType(TYPE_PET) )
		player = (Player*) receiver->GetOwner();

	if( !player )
		return;

	uint32 item = 0;

	for(uint8 i = 0; i < CREATURE_DROP_ITEM_MAX; i++)
	{
		if( item = contributor->GetItemDropped(i) )
		{
			sLog.outDebug("ITEM DROPPED: '%s' dropped item [%u] for '%s'", contributor->GetName(), item, receiver->GetName());

			if( player->AddNewInventoryItem(item, 1) )
			{
				m_itemDropped.push_back(new ItemDropped(a,b,item,x,y));
				player->DumpPlayer("inventory");
			}
			break;
		}
	}
}

void BattleSystem::GiveExpGainedFor(Player* player)
{
	WorldPacket data;

	//PlayerListMap::const_iterator it = m_PlayerList.begin();
	//for(it; it != m_PlayerList.end(); ++it)
	//{
		//sLog.outDebug("EXPERIENCE: Giving '%s' experience %u", (*it)->GetName(), (*it)->GetExpGained());
/*
		data.Initialize( 0x08 );
		data << (uint8 ) 0x01;
		data << (uint8 ) 0x24;
		data << (uint8 ) 0x01;
		data << (uint32) (*it)->GetExpGained();
		data << (uint32) 0;
		(*it)->GetSession()->SetLogging(true);
		(*it)->GetSession()->SendPacket(&data);
*/
		if( player->isDead() )
			player->AddExpGained(-1);

		player->AddExpGained();

		sLog.outDebug("EXPERIENCE: Giving '%s' experience %u", player->GetName(), player->GetExpGained());

		player->_updatePlayer(UPD_FLAG_XP, 1, player->GetExpGained());

		if( player->isLevelUp() )
		{
			player->resetLevelUp();
			player->UpdatePlayerLevel();
		}


		Pet* pet = player->GetBattlePet();
	
		if( !pet )
			return;
/*
		data.Initialize( 0x08 );
		data << (uint8 ) 0x02;
		data << (uint8 ) 0x04;
		data << (uint8 ) (*it)->GetPetSlot(pet);
		data << (uint8 ) 0x00;
		data << (uint8 ) 0x24;
		data << (uint8 ) 0x01;
		data << (uint32) pet->GetExpGained();
		data << (uint32) 0;
*/

		if( pet->isDead() )
			pet->AddExpGained(0);

		pet->AddExpGained();

		sLog.outDebug("EXPERIENCE: Giving '%s' experience %u", pet->GetName(), pet->GetExpGained());

		player->_updatePet(pet->GetSlot(), UPD_FLAG_XP, 1, pet->GetExpGained());

		//(*it)->GetSession()->SendPacket(&data);

		if( pet->isLevelUp() )
		{
			player->UpdatePetLevel(pet);
			pet->resetLevelUp();

			///- TODO: Fix this for smaller data packet
			player->UpdatePetCarried();

			player->_updatePet(pet->GetSlot(), UPD_FLAG_SPELL_POINT, 1, pet->GetUInt32Value(UNIT_FIELD_SPELL_POINT));
			player->_updatePet(pet->GetSlot(), UPD_FLAG_LOYALTY, 1, pet->GetLoyalty());
		}

		if( pet->isDead() )
		{
			uint8 loyalty = pet->GetLoyalty();
			pet->SetLoyalty(loyalty - 1);

			///- TODO: Release pet if loyalty = 0
			player->_updatePet(pet->GetSlot(), UPD_FLAG_LOYALTY, 2, pet->GetLoyalty());

		}
//	}

	//data.Initialize( 0x08 );
	//data << (uint8 ) 0x01; // 1 = char position; 2 = pet position
	//data << (uint8 ) 0x24; // XP byte code
	//data << (uint8 ) 0x01; // point mod +/- 1 = [+] ; 2 = [-] ;
	//data << (uint32) 200;  // total XP gained
	//data << (uint32) 0;    // unknown

	//SendMessageToSet(&data, true);
}

void BattleSystem::GiveItemDropped()
{
	if( m_itemDropped.empty() )
		return;

	sLog.outDebug("ITEM DROPPED: Giving item dropped size %u", m_itemDropped.size());

	m_animationTimeItemDropped = 3;

	m_itemDropped.sort(compare_position);

	/*
	ItemDroppedTurn::const_iterator it = m_itemDropped.begin();
	for(it; it != m_itemDropped.end(); ++it)
	{
		sLog.outString("ITEM DROPPED: Item Dropped for [%u,%u] <%u> from [%u,%u]", (*it)->GetReceiverCol(), (*it)->GetReceiverRow(), (*it)->GetItem(), (*it)->GetContributorCol(), (*it)->GetContributorRow());
	}
	*/

	ItemDropped* item;

	uint8 x, y, last_x, last_y = 0;

	WorldPacket data;

	while( !m_itemDropped.empty() )
	{
		item = m_itemDropped.front();
		m_itemDropped.pop_front();

		///- Update player inventory
		SendItemDropped(item);

		x = item->GetContributorCol();
		y = item->GetContributorRow();

		///- Animation update stuff
		if( (x != last_x) || (y != last_y) )
		{
			if( data.size() > 6 )
			{
				//sLog.outDebug("ITEM DROPPED: Item dropped animation from %u,%u to %u Unit", last_x, last_y, (uint8)((data.size() - 6) / 2));
				SendMessageToSet(&data);
			}

			///- Initialize animation data packet
			data.Initialize( 0x35 );
			data << (uint8 ) 0x04;
			data << (uint16) item->GetItem();
			data << (uint8 ) x;
			data << (uint8 ) y;

			data << (uint8 ) item->GetReceiverCol();
			data << (uint8 ) item->GetReceiverRow();
		}
		else
		{
			data << (uint8 ) item->GetReceiverCol();
			data << (uint8 ) item->GetReceiverRow();
		}


		last_x = x;
		last_y = y;
	}

	if( data.size() > 6 )
	{
		//sLog.outDebug("ITEM DROPPED: Item dropped animation from %u,%u to %u Unit", last_x, last_y, (uint8)((data.size() - 6) / 2));
		SendMessageToSet(&data);
	}
	//else
	//	sLog.outDebug("ITEM DROPPED: NOT Sending item dropped animation data size %u", data.size());


	m_itemDropped.clear();

	///- Update the animation
	//data.Initialize( 0x35 );
	//data << (uint8 ) 0x04;

	//data << (uint16) item->GetItem(); //34011; // item entry

	//data << (uint8 ) 0;    // dropper col
	//data << (uint8 ) 1;    // dropper row

	//*data << (uint8 ) item->GetReceiverCol(); //3;    // gainer1 col
	//*data << (uint8 ) item->GetReceiverRow(); //2;    // gainer1 row

	//data << (uint8 ) 2;    // gainer2 col
	//data << (uint8 ) 2;    // gainer2 row
	//SendMessageToSet(&data, true);

	//ZThread::Thread::sleep(100);
}

void BattleSystem::SendItemDropped(ItemDropped* item)
{
	uint8  a, b = 0;
	uint16 i    = 0;
	uint8  x, y = 0;

	a = item->GetReceiverCol();
	b = item->GetReceiverRow();
	i = item->GetItem();
	x = item->GetContributorCol();
	y = item->GetContributorRow();

	///- Update the inventory
	WorldPacket data;
	data.Initialize( 0x17 );
	data << (uint8 ) 0x06;

	data << (uint16) item->GetItem(); //34011; // item entry
	data << (uint16) 0x01;  // amount dropped
	data << (uint32) 0;

	Unit* receiver    = GetBattleUnit(a, b);
	Unit* contributor = GetBattleUnit(x, y);

	//sLog.outDebug("ITEM DROPPED: Give %-10.10s: item <%u> from: %-10.10s[%u,%u]", receiver->GetName(), i, contributor->GetName(), x, y);

	Player* pPlayer;
	if( receiver->isType(TYPE_PET) )
		pPlayer = (Player*) receiver->GetOwner();
	else
		pPlayer = (Player*) receiver;

	if( !pPlayer )
		return;

	SendMessageToPlayer(pPlayer, &data);
}

void BattleSystem::Escaped(BattleAction *action)
{
	uint8 col = action->GetAttackerCol();
	uint8 row = action->GetAttackerRow();

	if( isAtkPos( col ) )
		if( !isAtkPosBackRow( col ) )
			col += 1;

	if( isDefPos( col ) )
		if( !isDefPosBackRow( col ) )
			col -= 1;

	Player* player = (Player*) GetBattleUnit(col, row);

	WorldPacket data;
	///- Smoke disapear for escapee
	data.Initialize( 0x0B );
	data << (uint8 ) 0;
	data << (uint32) player->GetAccountId();
	data << (uint16) 0;
	player->SendMessageToSet(&data, true);

	///- Leave battle screen
	data.Initialize( 0x0B );
	data << (uint8 ) 1;
	data << (uint8 ) col;
	data << (uint8 ) row;;
	data << (uint8 ) 0;
	SendMessageToSet(&data, true);

	player->Send1406();
	player->Send1408();

	LeaveBattle(player);

	DumpBattlePosition();
}

void BattleSystem::BuildUpdateBlockPetPosition(WorldPacket *data, Pet* pet, uint8 col, uint8 row)
{
	data->clear();

	if( !pet )
		return;

	data->Initialize( 0x0B );
	*data << (uint8 ) 0x05;
	*data << (uint8 ) 0x05;
	*data << (uint8 ) 0x04;
	*data << (uint16) pet->GetModelId(); // Pet ModelId
	*data << (uint16) 0x0000;
	*data << (uint16) 0x0004;
	*data << (uint32) pet->GetOwnerAccountId(); // pet owner id (0 for AI)
	*data << (uint8 ) col; // pet col position
	*data << (uint8 ) row; // pet row position
	*data << (uint16) pet->GetUInt32Value(UNIT_FIELD_HP_MAX); // pet hp max
	*data << (uint16) pet->GetUInt32Value(UNIT_FIELD_SP_MAX); // pet sp max
	*data << (uint16) pet->GetUInt32Value(UNIT_FIELD_HP);     // pet hp
	*data << (uint16) pet->GetUInt32Value(UNIT_FIELD_SP);     // pet sp
	*data << (uint16) pet->GetUInt32Value(UNIT_FIELD_LEVEL);  // pet level
	*data << (uint16) pet->GetUInt32Value(UNIT_FIELD_ELEMENT);// pet element
}

bool BattleSystem::ActivatePetFor(Player* player, Pet* pet)
{
	if( !player || !pet )
		return false;

	Pet* pet2 = player->GetBattlePet();
	if( !pet2 )
		return false;

	if( pet != pet2 )
		return false;

	uint8 col, row;

	if( !GetPetPosFor(player, col, row) )
		return false;

	SetPosition(pet, col, row);
	WorldPacket data;
	BuildUpdateBlockPetPosition(&data, pet, col, row);
	SendMessageToSet(&data, true);

	///- TODO: Recalculate all Rate Level

	return true;
}

bool BattleSystem::DeactivatePetFor(Player* player)
{
	if( !player )
		return false;

	if( !player->GetBattlePet() )
		return false;

	uint8 col, row;

	if( !GetPetPosFor(player, col, row) )
		return false;

	m_BattleUnit[col][row] = NULL;

	WorldPacket data;
	data.Initialize( 0x0B );
	data << (uint8 ) 1;
	data << (uint8 ) col;
	data << (uint8 ) row;
	SendMessageToSet(&data, true);
}

bool BattleSystem::GetPetPosFor(Player* player, uint8 &col, uint8 &row)
{
	if( !player )
		return false;

	uint8 c, r;
	if( !GetPosFor(player, c, r) )
		return false;

	if( isAtkPos( c ) )
		if( isAtkPosBackRow( c ) )
			c--;
		else
			c++;
	else if( isDefPos( c ) )
		if( isDefPosBackRow( c ) )
			c++;
		else
			c--;

	col = c;
	row = r;

	return true;

}

bool BattleSystem::GetPosFor(Unit* unit, uint8 &col, uint8 &row)
{
	for(uint8 i = 0; i < BATTLE_COL_MAX; i++)
		for(uint8 j = 0; j < BATTLE_ROW_MAX; j++)
		{
			Unit* unit2 = GetBattleUnit(i, j);
			if( !unit2 )
				continue;

			if( unit != unit2 )
				continue;

			col = i;
			row = j;
			return true;
		}

	return false;
}


///- Calculate all requirement for capturing pet, including chance
bool BattleSystem::CanCatchPet(Unit* attacker, Unit* victim)
{
	if(!attacker || !victim)
		return false;

	///- Check minimum health percentage
	//   20% current health minimum capturable
	if( victim->GetHealth() > round(victim->GetUInt32Value(UNIT_FIELD_HP_MAX) * 0.2) )
	{
		sLog.outDebug("CAPTURE: Minimum health not captureable %u <= %.2f", victim->GetHealth(), round(victim->GetUInt32Value(UNIT_FIELD_HP_MAX) * 0.2));
		return false;
	}

	///- Check level minimum
	if( attacker->getLevel() + 5 < victim->getLevel() )
	//if( 5 < victim->getLevel() - attacker->getLevel() )
	{
		sLog.outDebug("CAPTURE: Minimum level not captureable %i > %i", attacker->getLevel() + 5, victim->getLevel());
		return false;
	}

	if( rand_chance() <= 50 )
	{
		sLog.outDebug("CAPTURE: Chance failed occurs");
		return false;
	}

	return true;
}
