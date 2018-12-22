#include "ivampire_modifier.h"

#include <base/system.h>

#include <engine/shared/config.h>

#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/player.h>

#include <game/server/entities/character.h>
#include <game/server/entities/laser.h>

void CIvampireModifier::ScanGametypeForActivation(CGameContext *pGameServer, char *pGameType)
{
	m_pGameServer = pGameServer;

	if(str_comp_nocase_num(pGameType, "i", 1) == 0)
	{
		m_IsInstagib = true;
		m_IsIVamp = false;
		char aTmpStr[32];
		str_copy(m_aGameType, pGameType, 32);
		str_copy(aTmpStr, pGameType+1, 31);
		str_copy(pGameType, aTmpStr, 32);

		// uppercase except i
		for(int i = 1; i < 32; i++)
		{
			if(m_aGameType[i])
				m_aGameType[i] = str_uppercase(m_aGameType[i]);
		}
	}
	else if (str_comp_nocase_num(pGameType, "vi", 2) == 0)
	{
		m_IsInstagib = true;
		m_IsIVamp = true;
		char aTmpStr[32];
		str_copy(m_aGameType, pGameType, 32);
		str_copy(aTmpStr, pGameType+2, 30);
		str_copy(pGameType, aTmpStr, 32);

		// uppercase except i
		for(int i = 2; i < 32; i++)
		{
			if(m_aGameType[i])
				m_aGameType[i] = str_uppercase(m_aGameType[i]);
		}
	}
	else
	{
		// vanilla gametypes are not supported for mods
		m_IsInstagib = true;
		m_IsIVamp = true;
		char aTmpStr[32];
		str_copy(aTmpStr, pGameType, 32);

		m_aGameType[0] = 'v';
		m_aGameType[1] = 'i';

		// uppercase except i
		for(int i = 0; i < 30; i++)
		{
			if(aTmpStr[i])
				m_aGameType[i+2] = str_uppercase(aTmpStr[i]);
		}
	}
}

void CIvampireModifier::OnInit()
{
	str_copy(g_Config.m_SvGametype, m_aGameType, 32);
	m_pGameServer->m_pController->m_pGameType = m_aGameType;
	m_pServer = m_pGameServer->Server();
}

void CIvampireModifier::OnTick()
{
	// do killingspree timeouts
	CCharacter *pChr;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if (GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
		{
			pChr = GameServer()->m_apPlayers[i]->GetCharacter();
			if (pChr && pChr->m_Spree > 0)
			{
				if (Server()->Tick() > pChr->m_SpreeTick+Server()->TickSpeed()*15.0f)
				{
					SpreeEnd(pChr, true);
				}
				else if (Server()->Tick() > pChr->m_SpreeTick+Server()->TickSpeed()*12.0f)
				{
					IndicateSpreeTimeout(pChr);
				}
			}
		}
	}
}

void CIvampireModifier::OnCharacterSpawn(CCharacter *pChr)
{
	// default health
	pChr->m_Health = 1;

	pChr->m_aWeapons[WEAPON_HAMMER].m_Got = false;
	pChr->m_aWeapons[WEAPON_GUN].m_Got = false;
	pChr->m_aWeapons[WEAPON_SHOTGUN].m_Got = false;
	pChr->m_aWeapons[WEAPON_GRENADE].m_Got = false;
	
	pChr->GiveWeapon(WEAPON_LASER, -1);
	pChr->m_ActiveWeapon = WEAPON_LASER;
	pChr->m_LastWeapon = WEAPON_LASER;

	pChr->m_SpawnProtectionTick = Server()->Tick();
	pChr->m_SpreeTick = Server()->Tick();
	pChr->m_SpreeTimeoutTick = Server()->Tick();
}

void CIvampireModifier::OnCharacterDeath(CCharacter *pChr, int Killer)
{
	CCharacter* pKillerChar = GameServer()->GetPlayerChar(Killer);
	if (pKillerChar && !GameServer()->m_pController->IsFriendlyFire(pChr->GetPlayer()->GetCID(), Killer))
	{
		if(IsIVamp() && pKillerChar->m_Health < g_Config.m_SvVampireMaxHealth)
			++pKillerChar->m_Health;
		SpreeAdd(pKillerChar);
	}
	SpreeEnd(pChr, false);
}

bool CIvampireModifier::OnCharacterTakeDamage(CCharacter *pChr, vec2 Source, int From, int Weapon)
{
	// laserjumps deal no damage
	// no self damage
	if (Weapon == WEAPON_GRENADE
			|| From == pChr->GetPlayer()->GetCID()
			|| (g_Config.m_SvSpawnProtection && Server()->Tick() - pChr->m_SpawnProtectionTick <= Server()->TickSpeed() * 0.8f) ) {
		return false;
	}

	int Dmg = IsIVamp()? 2 : pChr->m_Health;
	CCharacter *pChrFrom = GameServer()->GetPlayerChar(From);

	if(GameServer()->m_pController->IsTeamplay() && g_Config.m_SvTeamdamage == 2
			&& pChr && pChrFrom && pChr->GetPlayer()->GetTeam() == pChrFrom->GetPlayer()->GetTeam())
	{
		if(IsIVamp())
		{
			// transfer one health to team mate
			if (pChrFrom->m_Health > 1 && pChr->m_Health < g_Config.m_SvVampireMaxHealth)
			{
				--pChrFrom->m_Health;
				GameServer()->CreateDamage(pChrFrom->m_Pos, From, Source, pChrFrom->m_Health, 0, false);

				++pChr->m_Health;
				GameServer()->CreateDamage(pChr->m_Pos, From, Source, pChr->m_Health, 0, false);
				GameServer()->SendEmoticon(pChr->GetPlayer()->GetCID(), EMOTICON_HEARTS);
			}
			else
				return false;

			Dmg = 0; // do Hit sound, but no damage calculation etc.
		}
		else
			return false;
	}

	// do damage Hit sound
	if(From >= 0 && From != pChr->GetPlayer()->GetCID() && GameServer()->m_apPlayers[From])
	{
		int64 Mask = CmaskOne(From);
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && (GameServer()->m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS ||  GameServer()->m_apPlayers[i]->m_DeadSpecMode) &&
				GameServer()->m_apPlayers[i]->GetSpectatorID() == From)
				Mask |= CmaskOne(i);
		}
		GameServer()->CreateSound(GameServer()->m_apPlayers[From]->m_ViewPos, SOUND_HIT, Mask);
	}

	if(!Dmg) {
		return false;
	}

	pChr->m_Health -= Dmg;

	// check for death, always true for instagib
	if(pChr->m_Health <= 0)
	{
		pChr->Die(From, Weapon);

		// set attacker's face to happy (taunt!)
		if (From >= 0 && From != pChr->GetPlayer()->GetCID() && GameServer()->m_apPlayers[From])
		{
			CCharacter *pChr = GameServer()->m_apPlayers[From]->GetCharacter();
			if (pChr)
			{
				pChr->m_EmoteType = EMOTE_HAPPY;
				pChr->m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
			}
		}

		return false;
	}

	// create healthmod indicator: damage indicators show remaining health
	GameServer()->CreateDamage(pChr->m_Pos, pChr->GetPlayer()->GetCID(), Source, pChr->m_Health, 0, false);
        
	GameServer()->CreateSound(pChr->m_Pos, SOUND_PLAYER_PAIN_SHORT);

	pChr->m_EmoteType = EMOTE_PAIN;
	pChr->m_EmoteStop = Server()->Tick() + 500 * Server()->TickSpeed() / 1000;

	return true;
}

void CIvampireModifier::SpreeAdd(CCharacter *pChr)
{
	pChr->m_SpreeTick = Server()->Tick();
	++pChr->m_Spree;

	if (++pChr->m_SpreeIndicator > 10)
		pChr->m_SpreeIndicator = 1;
	pChr->m_Armor = pChr->m_SpreeIndicator;

	if(pChr->m_Spree % 5 == 0)
	{
		if (g_Config.m_SvKillingSpreeMsg)
		{
			static const char aaSpreeNoteInstagib[4][32] = { "is on a killing spree", "is on a rampage", "is dominating", "is unstoppable" };
			static const char aaSpreeNoteVamp[4][32] = { "is an unexperienced vampire", "is a skilled vampire", "is a superior vampire", "is a VAMPIRE LORD" };
			static const char aaSpreeColor[2][4][5] = { { "^999", "^999", "^900", "^900" }, { "^999", "^999", "^009", "^009" } };
			static const char aaSpreeCounterColor[2][4][5] = { { "^900", "^900", "^999", "^999" }, { "^009", "^009", "^999", "^999" } };

			int p = clamp((int)pChr->m_Spree/5 - 1, 0, 3);
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "%s%s %s with %s%d %skills.", aaSpreeColor[pChr->GetPlayer()->GetTeam()][p], Server()->ClientName(pChr->GetPlayer()->GetCID())
					, IsIVamp()? aaSpreeNoteVamp[p] : aaSpreeNoteInstagib[p]
					, aaSpreeCounterColor[pChr->GetPlayer()->GetTeam()][p], pChr->m_Spree, aaSpreeColor[pChr->GetPlayer()->GetTeam()][p]);
			GameServer()->SendBroadcast(aBuf, -1);
		}
	}
}

void CIvampireModifier::SpreeEnd(CCharacter *pChr, bool Timeout)
{
    if (pChr->m_Spree >= 5 && !Timeout)
    {
        GameServer()->CreateSound(pChr->m_Pos, SOUND_GRENADE_EXPLODE);
        GameServer()->CreateExplosion(pChr->m_Pos, pChr->GetPlayer()->GetCID(), WEAPON_LASER, 0);
    }
	pChr->m_Armor = pChr->m_SpreeIndicator = 0;
    pChr->m_Spree = 0;
}

void CIvampireModifier::IndicateSpreeTimeout(CCharacter *pChr)
{
	if (Server()->Tick() > pChr->m_SpreeTimeoutTick+Server()->TickSpeed()*0.5f)
	{
		pChr->m_SpreeTimeoutTick = Server()->Tick();
		pChr->m_Armor = (pChr->m_Armor == pChr->m_SpreeIndicator? 0 : pChr->m_SpreeIndicator);
	}
}

bool CIvampireModifier::IsFriendlyFire(int ClientID1, int ClientID2)
{
	// ClientID1 != ClientID2
	// IsTeamPlay
	return ((g_Config.m_SvTeamdamage == 0 || g_Config.m_SvTeamdamage == 2)
			&& GameServer()->m_apPlayers[ClientID1]->GetTeam() == GameServer()->m_apPlayers[ClientID2]->GetTeam());
}

bool CIvampireModifier::OnChatMsg(int ChatterClientID, int Mode, int To, const char *pText)
{
	if(Mode != CHAT_ALL)
		return false;

	if (str_comp_num(pText, "/", 1) == 0)
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Mode = Mode;
		Msg.m_pMessage = pText;
		Msg.m_TargetID = -1;
		Msg.m_ClientID = -1;

		if (str_comp_nocase(Msg.m_pMessage, "/info") == 0 || str_comp_nocase(Msg.m_pMessage, "/help") == 0)
		{
			char aBufHelpMsg[64];
			str_format(aBufHelpMsg, sizeof(aBufHelpMsg), "%s Mod (%s) by Slayer.", IsIVamp()? "iVampire" : "Instagib", GameServer()->ModVersion());
			Msg.m_pMessage = aBufHelpMsg;
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ChatterClientID);

			Msg.m_pMessage = "———————————————————";
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ChatterClientID);

			if(IsIVamp())
			{
				str_format(aBufHelpMsg, sizeof(aBufHelpMsg), "Kill enemies to gain up to %d health.", g_Config.m_SvVampireMaxHealth);
				Msg.m_pMessage = aBufHelpMsg;
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ChatterClientID);
			}
			else
			{
				Msg.m_pMessage = "One-shot your enemies with your laser.";
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ChatterClientID);
			}

			if(g_Config.m_SvLaserjumps)
			{
				Msg.m_pMessage = "Laserjump by hitting near ground.";
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ChatterClientID);
			}

			if (g_Config.m_SvTeamdamage == 1)
			{
				Msg.m_pMessage = "Friendly fire is on.";
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ChatterClientID);
			}
			else if (IsIVamp() && g_Config.m_SvTeamdamage == 2)
			{
				Msg.m_pMessage = "Hit teammates to transfer one health.";
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ChatterClientID);
			}


			Msg.m_pMessage = "Armor indicates your killing spree.";
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ChatterClientID);

			if(IsIVamp())
			{
				Msg.m_pMessage = "Damage stars indicate left health.";
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ChatterClientID);
			}
		}
		else
		{
			Msg.m_pMessage = "Unknown command. Type '/help' for more information about this mod.";
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ChatterClientID);
		}
		return true;
	}

	else
		return false;
}

bool CIvampireModifier::OnLaserBounce(CLaser *pLaser, vec2 From, vec2 To)
{
	if (g_Config.m_SvLaserjumps && pLaser->m_Bounces == 1 && distance(From, To) <= 110.0f)
	{
		pLaser->m_Energy = -1;
		GameServer()->CreateExplosion(To, pLaser->m_Owner, WEAPON_GRENADE, 3);
		GameServer()->CreateSound(pLaser->m_Pos, SOUND_GRENADE_EXPLODE);
		return true;
	}

	return false;
}