﻿#include "stdafx.h"
#include "Map.h"
#include "KnightsManager.h"
#include "KingSystem.h"

void CUser::ItemRepair(Packet & pkt)
{
	if (isDead())
		return;

	Packet result(WIZ_ITEM_REPAIR);
	uint32_t money, itemid;
	uint16_t durability, quantity/*, sNpcID*/;
	_ITEM_TABLE* pTable = nullptr;
	CNpc *pNpc = nullptr;
	uint8_t sPos, sSlot;

	pkt >> sPos >> sSlot >> /*sNpcID >>*/ itemid;
	if (sPos == 1 ) {	// SLOT
		if (sSlot >= SLOT_MAX) goto fail_return;
		if (m_sItemArray[sSlot].nNum != itemid) goto fail_return;
	}
	else if (sPos == 2 ) {	// INVEN
		if (sSlot >= HAVE_MAX) goto fail_return;
		if (m_sItemArray[SLOT_MAX+sSlot].nNum != itemid) goto fail_return;
	}

	//pNpc = g_pMain->GetNpcPtr(sNpcID);
	//if (pNpc == nullptr || !isInRange(pNpc, MAX_NPC_RANGE))
	//	return;

	//if (pNpc->GetType() == NPC_TINKER || pNpc->GetType() == NPC_MERCHANT)
	//{
		pTable = g_pMain->GetItemPtr( itemid );
		if (pTable == nullptr
			|| pTable->m_iSellPrice == SellTypeNoRepairs) 
			goto fail_return;

		durability = pTable->m_sDuration;
		if( durability == 1 ) goto fail_return;
		if( sPos == 1 )
			quantity = pTable->m_sDuration - m_sItemArray[sSlot].sDuration;
		else if( sPos == 2 ) 
			quantity = pTable->m_sDuration - m_sItemArray[SLOT_MAX+sSlot].sDuration;

		money = (uint32_t)((((pTable->m_iBuyPrice-10) / 10000.0f) + pow((float)pTable->m_iBuyPrice, 0.75f)) * quantity / (double)durability);

		if (GetPremiumProperty(PremiumRepairDiscountPercent) > 0)
			money = money * GetPremiumProperty(PremiumRepairDiscountPercent) / 100;

		if (!GoldLose(money, false))
			goto fail_return;

		if (sPos == 1)
			m_sItemArray[sSlot].sDuration = durability;
		else if( sPos == 2 )
			m_sItemArray[SLOT_MAX+sSlot].sDuration = durability;

		result << uint8_t(1) << GetCoins();
		Send(&result);
		SendItemMove(1);
		return;
	//}

fail_return:
	result << uint8_t(0) << GetCoins();
	Send(&result);
}

void CUser::ClientEvent(
	uint16_t sNpcID)
{
	// Ensure AI's loaded
	if (!g_pMain->m_bPointCheckFlag
		|| isDead())
		return;

	CNpc* pNpc = g_pMain->GetNpcPtr(sNpcID);
	if (pNpc == nullptr
		|| !isInRange(pNpc, MAX_NPC_RANGE))
		return;

	m_sEventNid = sNpcID;
	m_sEventSid = pNpc->GetProtoID(); // For convenience purposes with Lua scripts.

	if (pNpc->GetType() == NPC_KISS)
	{
		KissUser();
		return;
	}

	int32_t iEventID = g_pMain->GetInitialEventIDForNpc(pNpc);

	// NOTE: GetInitialEventIDForNpc() can expectedly return -1.
	// In this case, as per official behaviour, the script won't run at all.
	if (iEventID >= 0)
		RunZoneQuestScript(iEventID);
}

void CUser::KissUser()
{
	Packet result(WIZ_KISS);
	result << uint32_t(GetID()) << m_sEventNid;
	GiveItem(910014000); // aw, you got a 'Kiss'. How literal.
	SendToRegion(&result);
}

void CUser::ClassChange(Packet & pkt, bool bFromClient /*= true */)
{
	Packet result(WIZ_CLASS_CHANGE);
	bool bSuccess = false;
	uint8_t opcode = pkt.read<uint8_t>();
	if (opcode == CLASS_CHANGE_REQ)	
	{
		ClassChangeReq();
		return;
	}
	else if (opcode == ALL_POINT_CHANGE)	
	{
		AllPointChange();
		return;
	}
	else if (opcode == ALL_SKILLPT_CHANGE)	
	{
		AllSkillPointChange();
		return;
	}
	else if (opcode == CHANGE_MONEY_REQ)	
	{
		uint8_t sub_type = pkt.read<uint8_t>(); // type is irrelevant
		uint32_t money = (uint32_t)pow((GetLevel() * 2.0f), 3.4f);

		if (GetLevel() < 30)	
			money = (uint32_t)(money * 0.4f);
		else if (GetLevel() >= 60)
			money = (uint32_t)(money * 1.5f);

		// If nation discounts are enabled (1), and this nation has won the last war, get it half price.
		// If global discounts are enabled (2), everyone can get it for half price.
		if ((g_pMain->m_sDiscount == 1 && g_pMain->m_byOldVictory == GetNation())
			|| g_pMain->m_sDiscount == 2)
			money /= 2;

		result << uint8_t(CHANGE_MONEY_REQ) << money;
		Send(&result);
		return;
	}
	// If this packet was sent from the client, ignore it.
	else if (bFromClient)
		return;

	m_sClass = pkt.read<uint8_t>();
	if (isInParty())
	{
		// TODO: Move this somewhere better.
		result.Initialize(WIZ_PARTY);
		result << uint8_t(PARTY_CLASSCHANGE) << GetSocketID() << uint16_t(GetClass());
		g_pMain->Send_PartyMember(GetPartyID(), &result);
	}
}

void CUser::RecvSelectMsg(Packet & pkt)	// Receive menu reply from client.
{
	uint8_t bMenuID = pkt.read<uint8_t>();
	if (!AttemptSelectMsg(bMenuID))
		memset(&m_iSelMsgEvent, -1, sizeof(m_iSelMsgEvent));
}

bool CUser::AttemptSelectMsg(
	uint8_t byMenuID)
{
	if (byMenuID >= MAX_MESSAGE_EVENT
		|| isDead())
		return false;

	// Get the event number that needs to be processed next.
	int32_t iEventID = m_iSelMsgEvent[byMenuID];
	if (iEventID < 0)
		return false;

	return RunZoneQuestScript(iEventID);
}

void CUser::SendSay(int32_t nTextID[10])
{
	Packet result(WIZ_NPC_SAY);

	result << int32_t(-1) << int32_t(-1);
	for (int i = 0; i < 10; i++)
		result << int32_t(nTextID[i]);

	Send(&result);
}

void CUser::SelectMsg(
	int32_t menuHeaderText, 
	int32_t menuButtonText[MAX_MESSAGE_EVENT],
	int32_t menuButtonEvents[MAX_MESSAGE_EVENT])
{
	// Send the menu to the client
	Packet result(WIZ_SELECT_MSG);
	result
		<< uint16_t(m_sEventNid)
		<< menuHeaderText;

	for (int i = 0; i < MAX_MESSAGE_EVENT; i++)
		result << menuButtonText[i];

	Send(&result);

	// and store the corresponding event IDs.
	memcpy(&m_iSelMsgEvent, menuButtonEvents, sizeof(int32_t) * MAX_MESSAGE_EVENT);
}

void CUser::NpcEvent(Packet & pkt)
{
	// Ensure AI is loaded first
	if (!g_pMain->m_bPointCheckFlag
		|| isDead())
		return;	

	Packet result;
	uint16_t sNpcID = pkt.read<uint16_t>();

	CNpc* pNpc = g_pMain->GetNpcPtr(sNpcID);
	if (pNpc == nullptr
		|| !isInRange(pNpc, MAX_NPC_RANGE))
		return;

	switch (pNpc->GetType())
	{
	case NPC_MERCHANT:
	case NPC_TINKER:
		result.Initialize(pNpc->GetType() == NPC_MERCHANT ? WIZ_TRADE_NPC : WIZ_REPAIR_NPC);
		result << pNpc->m_iSellingGroup;
		Send(&result);
		break;

	case NPC_MARK:
		result.Initialize(WIZ_KNIGHTS_PROCESS);
		result << uint8_t(KNIGHTS_CAPE_NPC);
		Send(&result);
		break;

	case NPC_RENTAL:
		result.Initialize(WIZ_RENTAL);
		result	<< uint8_t(RENTAL_NPC) 
			<< uint16_t(1) // 1 = enabled, -1 = disabled 
			<< pNpc->m_iSellingGroup;
		Send(&result);
		break;

	case NPC_ELECTION:
	case NPC_TREASURY:
		{
			CKingSystem * pKingSystem = g_pMain->m_KingSystemArray.GetData(GetNation());
			result.Initialize(WIZ_KING);
			if (pNpc->GetType() == NPC_ELECTION)
			{
				// Ensure this still works as per official without a row in the table.
				std::string strKingName = (pKingSystem == nullptr ? "" : pKingSystem->m_strKingName);
				result.SByte();
				result	<< uint8_t(KING_NPC) << strKingName;
			}
			else
			{
				// Ensure this still works as per official without a row in the table.
				uint32_t nTribute = (pKingSystem == nullptr ? 0 : pKingSystem->m_nTribute + pKingSystem->m_nTerritoryTax);
				uint32_t nTreasury = (pKingSystem == nullptr ? 0 : pKingSystem->m_nNationalTreasury);
				result	<< uint8_t(KING_TAX) << uint8_t(1) // success
					<< uint16_t(isKing() ? 1 : 2) // 1 enables king-specific stuff (e.g. scepter), 2 is normal user stuff
					<< nTribute << nTreasury;
			}
			Send(&result);
		} break;

	case NPC_SIEGE:
		{
		_KNIGHTS_SIEGE_WARFARE *pKnightSiegeWarFare = g_pMain->GetSiegeMasterKnightsPtr(1);
		result.Initialize(WIZ_SIEGE);
		result << uint8_t(3) << uint8_t(7);
		Send(&result);
		}
		break;

	case NPC_SIEGE_MAINTAIN:
		{
		_KNIGHTS_SIEGE_WARFARE *pKnightSiegeWarFare = g_pMain->GetSiegeMasterKnightsPtr(1);
		if (pKnightSiegeWarFare->sMasterKnights == GetClanID())
		{
			result.Initialize(WIZ_SIEGE);
			result << uint8_t(4) << uint8_t(1) 
			<< pKnightSiegeWarFare->nDungeonCharge 
			<< pKnightSiegeWarFare->nMoradonTax 
			<< pKnightSiegeWarFare->nDellosTax;
		Send(&result);
		}
		}
		break;

	case NPC_VICTORY_GATE:
		switch (g_pMain->m_bVictory)
		{
		case KARUS:
			if(GetNation() == KARUS)
				ZoneChange(2,222,1846);
			   break;
		case ELMORAD:
			if(GetNation() == ELMORAD)
				ZoneChange(1,1865,168);
			   break;
		}
		break;

	case NPC_WAREHOUSE:
		result.Initialize(WIZ_WAREHOUSE);
		result << uint8_t(WAREHOUSE_REQ);
		Send(&result);
		break;

	case NPC_CHAOTIC_GENERATOR:
		SendAnvilRequest(sNpcID, ITEM_BIFROST_REQ);
		break;

	case NPC_CLAN: // this HAS to go.
		result << uint16_t(0); // page 0
		CKnightsManager::AllKnightsList(this, result);

	default:
		ClientEvent(sNpcID);
	}
}

// NPC shops
void CUser::ItemTrade(Packet & pkt)
{
	Packet result(WIZ_ITEM_TRADE);
	uint32_t transactionPrice;
	int itemid = 0, money = 0, group = 0;
	uint16_t npcid;
	uint16_t count, real_count = 0;
	_ITEM_TABLE* pTable = nullptr;
	CNpc* pNpc = nullptr;
	uint8_t type, pos, destpos, errorCode = 1;
	bool bSuccess = false;
	_KNIGHTS_SIEGE_WARFARE *pSiegeWar = g_pMain->GetSiegeMasterKnightsPtr(1);
	CKingSystem *pKingSystem = g_pMain->m_KingSystemArray.GetData(GetNation());

	if (isDead())
	{
		errorCode = 1;
		goto fail_return;
	}

	pkt >> type;

	// NOTE(srmeier): the binary client doesn't send the information necessary to make this check
	// Buy == 1, Sell == 2
	if (type == 1) //|| type == 2)
	{
		pkt >> group >> npcid;
		if (!g_pMain->m_bPointCheckFlag
			|| (pNpc = g_pMain->GetNpcPtr(npcid)) == nullptr
			|| (pNpc->GetType() != NPC_MERCHANT && pNpc->GetType() != NPC_TINKER)
			|| pNpc->m_iSellingGroup != group
			|| !isInRange(pNpc, MAX_NPC_RANGE))
			goto fail_return;
	}

	pkt >> itemid >> pos;

	if (type == 3) 	// Move only (this is so useless mgame -- why not just handle it with the CUser::ItemMove(). Gah.)
		pkt >> destpos;
	else
		pkt >> count;

	// Moving an item in the inventory
	if (type == 3)
	{
		if (pos >= HAVE_MAX || destpos >= HAVE_MAX
			|| itemid != m_sItemArray[SLOT_MAX+pos].nNum)
		{
			errorCode = 4;
			goto fail_return;
		}

		int16_t duration = m_sItemArray[SLOT_MAX+pos].sDuration;
		int16_t itemcount = m_sItemArray[SLOT_MAX+pos].sCount;
		m_sItemArray[SLOT_MAX+pos].nNum = m_sItemArray[SLOT_MAX+destpos].nNum;
		m_sItemArray[SLOT_MAX+pos].sDuration = m_sItemArray[SLOT_MAX+destpos].sDuration;
		m_sItemArray[SLOT_MAX+pos].sCount = m_sItemArray[SLOT_MAX+destpos].sCount;
		m_sItemArray[SLOT_MAX+destpos].nNum = itemid;
		m_sItemArray[SLOT_MAX+destpos].sDuration = duration;
		m_sItemArray[SLOT_MAX+destpos].sCount = itemcount;

		result << uint8_t(3);
		Send(&result);
		return;
	}

	if (isTrading()
		|| (pTable = g_pMain->GetItemPtr(itemid)) == nullptr
		|| (type == 2 // if we're selling an item...
		&& (itemid >= ITEM_NO_TRADE // Cannot be traded, sold or stored.
		|| pTable->m_bRace == RACE_UNTRADEABLE))) // Cannot be traded or sold.
		goto fail_return;

	if (pos >= HAVE_MAX
		|| count <= 0 || count > MAX_ITEM_COUNT)
	{
		errorCode = 2;
		goto fail_return;
	}

	// Buying from an NPC
	if (type == 1)
	{	
		if (m_sItemArray[SLOT_MAX+pos].nNum != 0)
		{
			if (m_sItemArray[SLOT_MAX+pos].nNum != itemid)
			{
				errorCode = 2;
				goto fail_return;
			}

			if (!pTable->m_bCountable || count <= 0)
			{
				errorCode = 2;
				goto fail_return;
			}

			if (pTable->m_bCountable 
				&& (count + m_sItemArray[SLOT_MAX+pos].sCount) > MAX_ITEM_COUNT)
			{
				errorCode = 4;
				goto fail_return;				
			}
		}

		if ((pKingSystem->m_nTerritoryTariff > 0 || pSiegeWar->nDellosTax > 0 || pSiegeWar->nMoradonTax > 0) && pNpc->m_iSellingGroup == 253000)
		{
			int32_t tariffTax = 0;
			uint32_t BuyPrice = pTable->m_iBuyPrice;
			switch (GetZoneID())
			{

			case ZONE_KARUS:
			tariffTax = BuyPrice*pKingSystem->m_nTerritoryTariff/10 + BuyPrice*pKingSystem->m_nTerritoryTariff/100;
			if (pKingSystem->m_nTerritoryTariff < 10)
			BuyPrice -= tariffTax - BuyPrice*pKingSystem->m_nTerritoryTariff/100;
			else
			BuyPrice += tariffTax + BuyPrice*pKingSystem->m_nTerritoryTariff/100;
			pKingSystem->m_nTerritoryTax += tariffTax;
			pKingSystem->m_nNationalTreasury += tariffTax;
			InsertTaxUpEvent(ZONE_KARUS, tariffTax);
				break;
			case ZONE_ELMORAD:
			tariffTax = BuyPrice*pKingSystem->m_nTerritoryTariff/10;
			if (pKingSystem->m_nTerritoryTariff < 10)
			BuyPrice -= tariffTax - BuyPrice*pKingSystem->m_nTerritoryTariff/100;
			else
			BuyPrice += tariffTax + BuyPrice*pKingSystem->m_nTerritoryTariff/100;
			pKingSystem->m_nTerritoryTax += tariffTax;
			pKingSystem->m_nNationalTreasury += tariffTax;
			InsertTaxUpEvent(ZONE_ELMORAD, tariffTax);
				break;
			case ZONE_MORADON:
			if (pSiegeWar) {
				tariffTax = BuyPrice*pSiegeWar->sMoradonTariff / 10;
				if (pSiegeWar->sMoradonTariff < 10)
					BuyPrice -= tariffTax - BuyPrice*pSiegeWar->sMoradonTariff / 100;
				else
					BuyPrice += tariffTax + BuyPrice*pSiegeWar->sMoradonTariff / 100;
				pSiegeWar->nMoradonTax += tariffTax;
				InsertTaxUpEvent(ZONE_MORADON, tariffTax);
			}
				break;
			case ZONE_DELOS:
			if (pSiegeWar) {
				tariffTax = BuyPrice*pSiegeWar->sDellosTariff / 10;
				if (pSiegeWar->sDellosTariff < 10)
					BuyPrice -= tariffTax - BuyPrice*pSiegeWar->sDellosTariff / 100;
				else
					BuyPrice += tariffTax + BuyPrice*pSiegeWar->sDellosTariff / 100;
				pSiegeWar->nDellosTax += tariffTax;
				InsertTaxUpEvent(ZONE_DELOS, tariffTax);
			}
				break;
			default:
				break;
			}
			transactionPrice = ((uint32_t)BuyPrice * count);
		}
		else
			transactionPrice = ((uint32_t)pTable->m_iBuyPrice * count);
		
		if (m_bPremiumType > 0)
		transactionPrice -= (uint32_t)(pTable->m_iBuyPrice * count / 11.1115f);


		if(pTable->m_bSellingGroup == 0)
		{
			errorCode = 3;
			goto fail_return;
		}

		if(pNpc->m_iSellingGroup != (pTable->m_bSellingGroup*1000))
			if(pNpc->m_iSellingGroup != (pTable->m_bSellingGroup*1000+1))
				if(pNpc->m_iSellingGroup != (pTable->m_bSellingGroup*1000+2))
					if(pNpc->m_iSellingGroup != (pTable->m_bSellingGroup*1000+72))
					{
						errorCode = 3;
						goto fail_return;
					}

					if (!hasCoins(transactionPrice))
					{
						errorCode = 3;
						goto fail_return;
					}

					if (((pTable->m_sWeight * count) + m_sItemWeight) > m_sMaxWeight)
					{
						errorCode = 4;
						goto fail_return;
					}

					m_sItemArray[SLOT_MAX+pos].nNum = itemid;
					m_sItemArray[SLOT_MAX+pos].sDuration = pTable->m_sDuration;
					m_sItemArray[SLOT_MAX+pos].sCount += count;

					m_iGold -= transactionPrice;

					if (!pTable->m_bCountable)
						m_sItemArray[SLOT_MAX+pos].nSerialNum = g_pMain->GenerateItemSerial();

					SetUserAbility(false);
					SendItemWeight();
	}
	// Selling an item to an NPC
	else
	{
		_ITEM_DATA *pItem = &m_sItemArray[SLOT_MAX+pos];
		if (pItem->nNum != itemid
			|| pItem->isRented())
		{
			errorCode = 2;
			goto fail_return;
		}

		if (pItem->sCount < count)
		{
			errorCode = 3;
			goto fail_return;
		}

		int16_t oldDurability = pItem->sDuration;
		if (pTable->m_iSellPrice != SellTypeFullPrice)
			if (m_bPremiumType > 0)
				transactionPrice = ((pTable->m_iBuyPrice / 4) * count); //4 for prem/discount
			else
				transactionPrice = ((pTable->m_iBuyPrice / 6) * count); // /6 is normal, /4 for prem/discount
		else
			transactionPrice = (pTable->m_iBuyPrice * count);

		GoldGain(transactionPrice, false);

		if (count >= pItem->sCount)
			memset(pItem, 0, sizeof(_ITEM_DATA));
		else
			pItem->sCount -= count;

		SetUserAbility(false);
		SendItemWeight();
	}

	bSuccess = true;

fail_return:
	result << bSuccess;
	if (!bSuccess)
		result << errorCode;
	else 
		result << m_iGold << transactionPrice <<  pTable->m_bSellingGroup ; // price bought or sold for
	Send(&result);
}

/**
* @brief	Handles the name change response packet
* 			containing the specified new name.
*
* @param	pkt	The packet.
*/
void CUser::HandleNameChange(Packet & pkt)
{
	uint8_t opcode;
	pkt >> opcode;

	switch (opcode)
	{
	case NameChangePlayerRequest:
		HandlePlayerNameChange(pkt);
		break;
	}
}

/**
* @brief	Handles the character name change response packet
* 			containing the specified new character's name.
*
* @param	pkt	The packet.
*/
void CUser::HandlePlayerNameChange(Packet & pkt)
{
	NameChangeOpcode response = NameChangeSuccess;
	std::string strUserID;
	pkt >> strUserID;

	if (strUserID.empty() || strUserID.length() > MAX_ID_SIZE)
		response = NameChangeInvalidName;
	else if (isInClan())
		response = NameChangeInClan;
	else if (isKing())
		response = NameChangeKing; 

	if (response != NameChangeSuccess)
	{
		SendNameChange(response);
		return;
	}

	// Ensure we have the scroll before handling this request.
	if (!CheckExistItem(ITEM_SCROLL_OF_IDENTITY))
		return;

	Packet result(WIZ_NAME_CHANGE);
	result << uint8_t(NameChangePlayerRequest) << strUserID;
	g_pMain->AddDatabaseRequest(result, this);
}

/**
* @brief	Sends a name change packet.
*
* @param	opcode	Name change packet opcode.
* 					NameChangeShowDialog shows the dialog where you can set your name.
* 					NameChangeSuccess confirms the name was changed.
* 					NameChangeInvalidName throws an error reporting the name is invalid.
* 					NameChangeInClan throws an error reporting the user's still in a clan (and needs to leave).
*					NameChangeIsKing if the user is king
*/
void CUser::SendNameChange(NameChangeOpcode opcode /*= NameChangeShowDialog*/)
{
	Packet result(WIZ_NAME_CHANGE);
	result << uint8_t(opcode);
	Send(&result);
}

void CUser::HandleCapeChange(Packet & pkt)
{
	Packet result(WIZ_CAPE);
	CKnights* pKnights = nullptr;
	_KNIGHTS_CAPE* pCape = nullptr;
	uint32_t nReqCoins = 0;
	int16_t sErrorCode = 0, sCapeID;

	pkt >> sCapeID;

	// If we're not a clan leader, what are we doing changing the cape?
	if (!isClanLeader()
		|| isDead())
	{
		sErrorCode = -1;
		goto fail_return;
	}

	// Does the clan exist?
	if ((pKnights = g_pMain->GetClanPtr(GetClanID())) == nullptr)
	{
		sErrorCode = -2;
		goto fail_return;
	}

	// Make sure we're promoted
	if (!pKnights->isPromoted()
		// and that if we're in an alliance, we're the primary clan in the alliance.
			|| (pKnights->isInAlliance() && !pKnights->isAllianceLeader()))
	{
		sErrorCode = -1;
		goto fail_return;
	}

	if (sCapeID >= 0)
	{
		// Does this cape type exist?
		if ((pCape = g_pMain->m_KnightsCapeArray.GetData(sCapeID)) == nullptr || sCapeID == 99)
		{
			sErrorCode = -5;
			goto fail_return;
		}

		// Is our clan allowed to use this cape?
		if (pCape->byGrade != 0
			&& pKnights->m_byGrade > pCape->byGrade)
		{
			sErrorCode = -6;
			goto fail_return;
		}

		// NOTE: Error code -8 is for nDuration
		// It applies if we do not have the required item ('nDuration', awful name).
		// Since no capes seem to use it, we'll ignore it...

		// Can we even afford this cape?
		if (!hasCoins(pCape->nReqCoins))
		{
			sErrorCode = -7;
			goto fail_return;
		}

		nReqCoins = pCape->nReqCoins;
	}

	if (nReqCoins > 0)
		GoldLose(nReqCoins);

	// Are we changing the cape?
	if (sCapeID >= 0)
		pKnights->m_sCape = sCapeID;

	result
		<< uint16_t(1) // success
		<< pKnights->GetAllianceID()
		<< pKnights->GetID()
		<< pKnights->m_sCape;

	Send(&result);

	// TODO:
	// When we implement alliances, this should send to the alliance
	// if the clan is part of one. Also, their capes should be updated.
	pKnights->SendUpdate();

	// TODO: Send to other servers via UDP.

	// Now tell Aujard to save (we don't particularly care whether it was able to do so or not).
	result.Initialize(WIZ_CAPE);
	result	<< pKnights->GetID() << pKnights->m_sCape;
	g_pMain->AddDatabaseRequest(result, this);
	return;

fail_return:
	result << sErrorCode;
	Send(&result);
}
