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

	bool IsInstagib = false;
	bool IsGrenade = false;
	bool IsIVamp = false;
	int GameTypeOffset = 0;
	const int MaxGameTypeLen = 30;

	if (str_comp_nocase_num(pGameType, "i", 1) == 0)
	{
		IsInstagib = true;
		GameTypeOffset = 1;
	}
	else if (str_comp_nocase_num(pGameType, "g", 1) == 0)
	{
		IsInstagib = IsGrenade = true;
		GameTypeOffset = 1;
	}
	else if (str_comp_nocase_num(pGameType, "vi", 2) == 0)
	{
		IsInstagib = IsIVamp = true;
		GameTypeOffset = 2;
	}
	else if (str_comp_nocase_num(pGameType, "vg", 2) == 0)
	{
		IsInstagib = IsGrenade = IsIVamp = true;
		GameTypeOffset = 2;
	}

	if (!IsInstagib)
	{
		// vanilla gametypes are not supported for mods
		IsInstagib = IsIVamp = true;

		m_aGameType[0] = 'v';
		m_aGameType[1] = 'i';
		m_aGameType[2] = 0;
		GameTypeOffset = 2;
		
		int i = 0;
		for (i = 0; i < MaxGameTypeLen; ++i)
		{
			if (pGameType[i])
				m_aGameType[i+GameTypeOffset] = pGameType[i];
		}
		m_aGameType[i+GameTypeOffset] = 0;
	}
	else
	{
		str_copy(m_aGameType, pGameType, MaxGameTypeLen);
		char aTmpStr[32];
		str_copy(aTmpStr, pGameType+GameTypeOffset, MaxGameTypeLen);
		str_copy(pGameType, aTmpStr, MaxGameTypeLen);
	}

	m_IsInstagib = IsInstagib;
	m_IsGrenade = IsGrenade;
	m_IsIVamp = IsIVamp;

	if (!IsGameTypeSupported(pGameType)) {
		MakeDefaultGameType(pGameType);
	}

	// uppercase except i
	for (int i = GameTypeOffset; i < MaxGameTypeLen; ++i)
	{
		if (m_aGameType[i])
			m_aGameType[i] = str_uppercase(m_aGameType[i]);
	}
}

bool CIvampireModifier::IsGameTypeSupported(char *pGameType)
{
	return (str_comp_nocase(pGameType, "mod") == 0
			|| str_comp_nocase(pGameType, "ctf") == 0
			|| str_comp_nocase(pGameType, "lms") == 0
			|| str_comp_nocase(pGameType, "lts") == 0
			|| str_comp_nocase(pGameType, "tdm") == 0
			|| str_comp_nocase(pGameType, "dm") == 0);
}

void CIvampireModifier::MakeDefaultGameType(char *pGameType)
{
	const int GameTypeOffset = m_IsIVamp? 2 : 1;

	pGameType[0] = m_aGameType[GameTypeOffset] = 'd';
	pGameType[1] = m_aGameType[GameTypeOffset+1] = 'm';
	pGameType[2] = m_aGameType[GameTypeOffset+2] = 0;
}

void CIvampireModifier::OnInit()
{
	str_copy(g_Config.m_SvGametype, m_aGameType, 32);
	m_pGameServer->m_pController->m_pGameType = m_aGameType;
	m_pServer = m_pGameServer->Server();
}

void CIvampireModifier::OnTick()
{
	if (!m_IsInstagib)
		return;

	// do killingspree timeouts
	CCharacter *pChr;
	for (int i = 0; i < MAX_CLIENTS; ++i)
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
	if (!m_IsInstagib)
		return;

	// default health
	pChr->m_Health = (m_IsIVamp && m_pGameServer->m_pController->m_GameFlags&GAMEFLAG_SURVIVAL)? g_Config.m_SvVampireMaxHealth : 1;
	pChr->m_Armor  = 0;

	pChr->m_aWeapons[WEAPON_HAMMER].m_Got = false;
	pChr->m_aWeapons[WEAPON_GUN].m_Got = false;
	pChr->m_aWeapons[WEAPON_SHOTGUN].m_Got = false;
	pChr->m_aWeapons[WEAPON_GRENADE].m_Got = false;
	pChr->m_aWeapons[WEAPON_LASER].m_Got = false;

	const int InstagibWeapon = m_IsGrenade? WEAPON_GRENADE : WEAPON_LASER;

	pChr->m_aWeapons[InstagibWeapon].m_Got = true;
	pChr->GiveWeapon(InstagibWeapon, m_IsGrenade? g_Config.m_SvGrenadeAmmo : -1);
	pChr->m_ActiveWeapon = InstagibWeapon;
	pChr->m_LastWeapon = InstagibWeapon;

	pChr->m_SpawnProtectionTick = Server()->Tick();
	pChr->m_SpreeTick = Server()->Tick();
	pChr->m_SpreeTimeoutTick = Server()->Tick();
}

void CIvampireModifier::OnCharacterDeath(CCharacter *pChr, int Killer)
{
	if (!m_IsInstagib)
		return;

	CCharacter* pKillerChar = GameServer()->GetPlayerChar(Killer);
	if (pKillerChar && !GameServer()->m_pController->IsFriendlyFire(pChr->GetPlayer()->GetCID(), Killer))
	{
		if (IsIVamp() && pKillerChar->m_Health < g_Config.m_SvVampireMaxHealth)
			++pKillerChar->m_Health;
		SpreeAdd(pKillerChar);
	}
	SpreeEnd(pChr, false);
}

bool CIvampireModifier::OnCharacterTakeDamage(CCharacter *pChr, vec2 Source, int Dmg, int From, int Weapon)
{
	// laserjumps (WEAPON_HAMMER) deal no damage
	// no self damage
	if (Weapon == WEAPON_HAMMER
			|| (Weapon == WEAPON_GRENADE && Dmg < g_Config.m_SvGrenadeKillThreshold)
			|| From == pChr->GetPlayer()->GetCID()
			|| (g_Config.m_SvSpawnProtection && Server()->Tick() - pChr->m_SpawnProtectionTick <= Server()->TickSpeed() * 0.8f) ) {
		return false;
	}

	int DmgDmg = IsIVamp()? 2 : pChr->m_Health;
	CCharacter *pChrFrom = GameServer()->GetPlayerChar(From);

	if (GameServer()->m_pController->IsTeamplay() && g_Config.m_SvTeamdamage == 2
			&& pChr && pChrFrom && pChr->GetPlayer()->GetTeam() == pChrFrom->GetPlayer()->GetTeam())
	{
		if (IsIVamp())
		{
			GameServer()->SendEmoticon(pChr->GetPlayer()->GetCID(), EMOTICON_HEARTS);

			// transfer one health to team mate
			if (pChrFrom->m_Health > 1 && pChr->m_Health < g_Config.m_SvVampireMaxHealth)
			{
				--pChrFrom->m_Health;
				GameServer()->CreateDamage(pChrFrom->m_Pos, From, Source, pChrFrom->m_Health, 0, false);

				++pChr->m_Health;
				GameServer()->CreateDamage(pChr->m_Pos, From, Source, pChr->m_Health, 0, false);
			}
			else
				return false;

			DmgDmg = 0; // do Hit sound, but no damage calculation etc.
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

	if(!DmgDmg) {
		return false;
	}

	pChr->m_Health -= DmgDmg;

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

	if (pChr->m_Spree % 5 == 0)
	{
		if (g_Config.m_SvKillingSpreeMsg)
		{
			static const char aaSpreeNoteInstagib[4][32] = { "is on a killing spree", "is on a rampage", "is dominating", "is unstoppable" };
			static const char aaSpreeNoteVamp[4][32] = { "is an Ascendant Vampire", "is a Hunter Vampire", "is a Death Dealer", "is a VAMPIRE LORD" };
			static const char aaSpreeColor[2][4][5] = { { "^999", "^999", "^900", "^900" }, { "^999", "^999", "^009", "^009" } };
			static const char aaSpreeNameColor[2][4][5] = { { "^900", "^900", "^999", "^999" }, { "^009", "^009", "^999", "^999" } };
			static const char aaSpreeCounterColor[2][4][5] = { { "^900", "^900", "^999", "^999" }, { "^009", "^009", "^999", "^999" } };

			int p = clamp((int)pChr->m_Spree/5 - 1, 0, 3);
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "%s%s %s%s with %s%d %skills.", aaSpreeNameColor[pChr->GetPlayer()->GetTeam()][p], Server()->ClientName(pChr->GetPlayer()->GetCID())
					, aaSpreeColor[pChr->GetPlayer()->GetTeam()][p], IsIVamp()? aaSpreeNoteVamp[p] : aaSpreeNoteInstagib[p]
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

void CIvampireModifier::OnCharacterHandleWeapons(CCharacter *pChr)
{
	if (!m_IsInstagib)
		return;

	if (!m_IsGrenade)
		return;

	// ammo regen
	int AmmoRegenTime = g_Config.m_SvGrenadeAmmoRegen;
	if(AmmoRegenTime && pChr->m_aWeapons[pChr->m_ActiveWeapon].m_Ammo >= 0)
	{
		// If equipped and not active, regen ammo?
		if (pChr->m_ReloadTimer <= 0)
		{
			if (pChr->m_aWeapons[pChr->m_ActiveWeapon].m_AmmoRegenStart < 0)
				pChr->m_aWeapons[pChr->m_ActiveWeapon].m_AmmoRegenStart = Server()->Tick();

			if ((Server()->Tick() - pChr->m_aWeapons[pChr->m_ActiveWeapon].m_AmmoRegenStart) >= AmmoRegenTime * Server()->TickSpeed() / 1000)
			{
				// Add some ammo
				pChr->m_aWeapons[pChr->m_ActiveWeapon].m_Ammo = min(pChr->m_aWeapons[pChr->m_ActiveWeapon].m_Ammo + 1,
					g_Config.m_SvGrenadeAmmo);
				pChr->m_aWeapons[pChr->m_ActiveWeapon].m_AmmoRegenStart = -1;
			}
		}
		else
		{
			pChr->m_aWeapons[pChr->m_ActiveWeapon].m_AmmoRegenStart = -1;
		}
	}
}

bool CIvampireModifier::OnChatMsg(int ChatterClientID, int Mode, int To, const char *pText)
{
	if (!m_IsInstagib)
		return false;

	if ((Mode != CHAT_ALL && Mode != CHAT_TEAM) || str_comp_num(pText, "/", 1) != 0)
		return false;

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

		if (IsIVamp())
		{
			str_format(aBufHelpMsg, sizeof(aBufHelpMsg), "Kill enemies to gain up to %d health.", g_Config.m_SvVampireMaxHealth);
			Msg.m_pMessage = aBufHelpMsg;
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ChatterClientID);
		}
		else
		{
			Msg.m_pMessage = "One-shot your enemies.";
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ChatterClientID);
		}

		if (g_Config.m_SvLaserjumps && !m_IsGrenade)
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

		if (IsIVamp())
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

bool CIvampireModifier::OnLaserBounce(CLaser *pLaser, vec2 From, vec2 To)
{
	if (!m_IsInstagib)
		return false;

	if (g_Config.m_SvLaserjumps && pLaser->m_Bounces == 1 && distance(From, To) <= 110.0f)
	{
		pLaser->m_Energy = -1;
		GameServer()->CreateExplosion(To, pLaser->m_Owner, WEAPON_HAMMER, 3);
		GameServer()->CreateSound(pLaser->m_Pos, SOUND_GRENADE_EXPLODE);
		return true;
	}

	return false;
}