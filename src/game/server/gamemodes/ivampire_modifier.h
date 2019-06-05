#ifndef GAME_SERVER_GAMEMODES_IVAMPIRE_H
#define GAME_SERVER_GAMEMODES_IVAMPIRE_H

#include <base/vmath.h>

class CCharacter;
class CGameContext;
class IGameController;
class CLaser;

struct CIvampireModifier
{
	class CGameContext *m_pGameServer;
	class IServer *m_pServer;
	
	bool m_IsInstagib;
	bool m_IsGrenade;
	bool m_IsIVamp;
	char m_aGameType[64];

	CIvampireModifier()
	{
		m_pGameServer = 0;
		m_IsInstagib = false;
		m_IsGrenade = false;
		m_IsIVamp = false;
	}

	const char *GetGameType() const { return m_aGameType; }
	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const { return m_pServer; }

	bool IsInstagib() const { return m_IsInstagib; }
	bool IsIVamp() const { return m_IsIVamp; }

	bool IsFriendlyFire(int ClientID1, int ClientID2);
	
	void ScanGametypeForActivation(CGameContext *pGameServer, char *pGameType);

	void OnInit();
	void OnTick();
	void OnCharacterSpawn(CCharacter *pChr);
	bool OnCharacterTakeDamage(CCharacter *pChr, vec2 Source, int Dmg, int FromCID, int Weapon);
	void OnCharacterDeath(CCharacter *pChr, int Killer);
	void OnCharacterHandleWeapons(CCharacter *pChr);
	bool OnChatMsg(int ChatterClientID, int Mode, int To, const char *pText);
	bool OnLaserBounce(CLaser *pLaser, vec2 From, vec2 To);

	void SpreeAdd(CCharacter *pChr);
	void SpreeEnd(CCharacter *pChr, bool Timeout);
	void IndicateSpreeTimeout(CCharacter *pChr);
};

#endif