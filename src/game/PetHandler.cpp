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

#include "BattleSystem.h"
#include "Common.h"
#include "WorldPacket.h"
#include "WorldSocket.h"
#include "WorldSession.h"
#include "Log.h"
#include "Opcodes.h"
#include "Pet.h"
#include "Player.h"

void WorldSession::HandlePetCommandOpcodes( WorldPacket & recv_data )
{
	sLog.outDebug( "WORLD: Recvd CMSG_COMMAND_PET Message" );
	CHECK_PACKET_SIZE( recv_data, 1 );

	uint8 command;

	recv_data >> command;

	Player* battleMaster = _player->GetBattleMaster();
	BattleSystem* engine = NULL;

	if( battleMaster )
		engine = battleMaster->PlayerBattleClass;

	if( engine )
	{
		uint8 atk_col, atk_row;
		engine->GetPosFor(_player, atk_col, atk_row);
		if( !engine->isActionComplete() )
		{
			///- Tell next attacker to move if available
			WorldPacket data;
			data.Initialize( 0x35 );
			data << (uint8 ) 0x05;
			data << (uint8 ) atk_col;
			data << (uint8 ) atk_row;
			SendPacket(&data);
		}
	}

	switch( command )
	{
		case 0x01:  // Battle command
		{
			CHECK_PACKET_SIZE( recv_data, 2+2 );
			uint16 petModelId;
			recv_data >> petModelId;

			Pet* pet = _player->GetPetByModelId( petModelId );
			if( !pet )
				return;

			///- in battle handler deactivate any battle pet
			if( engine )
				if( engine->DeactivatePetFor(_player) )
					engine->IncAction();

			_player->SetBattlePet( NULL );
			_player->UpdateBattlePet();
			_player->SetBattlePet( pet );

			///- in battle handler activate selected battle pet
			if( engine && pet )
				if( engine->ActivatePetFor(_player, pet) )
					engine->IncAction();

			break;
		}

		case 0x02:  // Rest command
		{

			///- in battle handler deactivate any battle pet
			Pet* pet = _player->GetBattlePet();
			if( engine && pet )
			{
				if( engine->DeactivatePetFor(_player) )
					engine->IncAction(); // action for pet
				engine->IncAction(); // action for player
			}

			_player->SetBattlePet( NULL );

			break;
		}
	}

	_player->UpdateBattlePet();

	///- in battle handler, final check is action complete ?
	if( engine )
		if( engine->isActionComplete() )
			engine->BuildActions();
}

void WorldSession::HandlePetReleaseOpcode( WorldPacket & recv_data )
{
	sLog.outDebug( "WORLD: Recvd CMSG_PET_COMMAND_RELEASE Message" );
	CHECK_PACKET_SIZE( recv_data, 1+1 );

	uint8 subcmd;
	uint8 petslot;

	recv_data >> subcmd;
	recv_data >> petslot;

	if( petslot <= PET_SLOT_START || petslot > MAX_PET_SLOT )
		return;

	switch ( subcmd )
	{
		case 0x01: // unknown
		{
		} break;

		case 0x02:
		{
			_player->ReleasePet(petslot-1);
		} break;
	}
}

