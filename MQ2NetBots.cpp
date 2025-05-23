
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=//
// Projet: MQ2NetBots.cpp
// Author: s0rCieR
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=//
//
// Deadchicken added .Duration on or about September 2007 and tried not to
// mangle s0rCieR's code to much.  Thanks to Kroak for helping debug.
//  Updated for 10/9 release of MQ2
// CombatState member added  Thanks mijuki.
// .Stacks member added
//
// v2.1 woobs
//    - Upped most of the maximum values to handle more buffs.
//    - Add Detrimental information to merge in MQ2Debuffs functions
// v2.2 eqmule
//    - Added String Safety
// v3.0 woobs
//    - Updated .Stacks
//    - Added   .StacksPet
// v3.1 woobs
//    - Updated for Spell Blocker (148) fix from swifty.
// v3.2 woobs
//    - Added NBGetEffectAmt to get adjusted values for compare.
//    - Removed 85/419 from triggering formula. These Procs stack/don't stack based on
//      the normal base check (for these, it is SpellID). It seems newer spells simply
//      overwrite older spells.

#define        PLUGIN_NAME "MQ2NetBots"
#define        PLUGIN_DATE     20190715

#define        GEMS_MAX              NUM_SPELL_GEMS
#define        BUFF_MAX              NUM_LONG_BUFFS
#define        SONG_MAX              NUM_SHORT_BUFFS
#define        PETS_MAX              65
#define        NOTE_MAX		    500
#define        DETR_MAX		     30

#define        NETTICK               50
#define        REFRESH            60000
#define        UPDATES             6000

#define        DEBUGGING             0

// FIXME:  Why is all this redefined here?  Even without that, get rid of it.
#define ISINDEX() (Index[0])
#define ISNUMBER() (IsNumber(Index))
#define GETNUMBER() (atoi(Index))
#define GETFIRST() Index

#include <mq/Plugin.h>
PreSetup(PLUGIN_NAME);
PLUGIN_VERSION(3.2);
#include <string>
#include "../Blech/Blech.h"

enum {
	STATE_DEAD = 0x0001, STATE_FEIGN = 0x0002, STATE_DUCK = 0x0004, STATE_BIND = 0x0008,
	STATE_STAND = 0x0010, STATE_SIT = 0x0020, STATE_MOUNT = 0x0040, STATE_INVIS = 0x0080,
	STATE_LEV = 0x0100, STATE_ATTACK = 0x0200, STATE_MOVING = 0x0400, STATE_STUN = 0x0800,
	STATE_RAID = 0x1000, STATE_GROUP = 0x2000, STATE_LFG = 0x4000, STATE_AFK = 0x8000
};

enum {
	BUFFS, CASTD, ENDUS, EXPER, LEADR, LEVEL, LIFES, MANAS,
	PBUFF, PETIL, SPGEM, SONGS, STATE, TARGT, ZONES, DURAS, SHORTDURAS,
	LOCAT, HEADN, AAPTS, OOCST, NOTE, DETR, ESIZE
};

enum {
	RESERVED, DETRIMENTALS, COUNTERS, BLINDED, CASTINGLEVEL, CHARMED, CURSED, DISEASED, ENDUDRAIN,
	FEARED, HEALING, INVULNERABLE, LIFEDRAIN, MANADRAIN, MESMERIZED, POISONED, RESISTANCE, ROOTED,
	SILENCED, SLOWED, SNARED, SPELLCOST, SPELLDAMAGE, SPELLSLOWED, TRIGGR, CORRUPTED, NOCURE, DSIZE
};

class BotInfo {
public:
	CHAR              Name[0x40];          // Client NAME
	CHAR              Leader[0x40];        // Leader Name
	WORD              State;               // State
	long              ZoneID;              // Zone ID
	long              InstID;              // Instance ID
	long              SpawnID;             // Spawn ID
	long              ClassID;             // Class ID
	long              Level;               // Level
	long              CastID;              // Casting Spell ID
	long              LifeCur;             // HP Current
	long              LifeMax;             // HP Maximum
	long              EnduCur;             // ENDU Current
	long              EnduMax;             // ENDU Maximum
	long              ManaCur;             // MANA Current
	long              ManaMax;             // MANA Maximum
	long              PetID;               // PET ID
	long              PetHP;               // PET HP Percentage
	long              TargetID;            // Target ID
	long              TargetHP;            // Target HP Percentage
  //  long              Gem[GEMS_MAX];       // Spell Memorized
	long              Pets[PETS_MAX];      // Spell Pets
	long              Song[SONG_MAX];      // Spell Song
	long              Buff[BUFF_MAX];      // Spell Buff
    int               Duration[BUFF_MAX];  // Buff duration
	int               ShortDuration[SONG_MAX];  // Song duration
	long              FreeBuff;            // FreeBuffSlot;
#if HAS_LEADERSHIP_EXPERIENCE
	double            glXP;                // glXP
#endif
	DWORD             aaXP;                // aaXP
	DWORD             XP;                  // XP
	DWORD             Updated;             // Update
	CHAR              Location[0x40];      // Y,X,Z
	CHAR		    Heading[0x40];       // Heading
	long              TotalAA;             // totalAA
	long              UsedAA;              // usedAA
	long              UnusedAA;            // unusedAA
	DWORD             CombatState;         // CombatState
	CHAR              Note[NOTE_MAX];      // User Mesg
	int               Detrimental[DSIZE];
};

long                NetInit = 0;           // Plugin Initialized?
long                NetStat = 0;           // Plugin is On?
long                NetGrab = 0;           // Grab Information?
long                NetSend = 0;           // Send Information?
long                NetLast = 0;           // Last Send Time Mark
char                NetNote[NOTE_MAX];   // Network Note

std::map<std::string, std::shared_ptr<BotInfo>> NetMap;              // BotInfo Mapped List
Blech               Packet('#');         // BotInfo Event Triggers
BotInfo            *CurBot = 0;            // BotInfo Current

long                sTimers[ESIZE];      // Save Timers
char                sBuffer[ESIZE][2048]; // Save Buffer
char                wBuffer[ESIZE][2048]; // Work Buffer
bool                wChange[ESIZE];      // Work Change
bool                wUpdate[ESIZE];      // Work Update

int                 dValues[DSIZE];

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=//

bool EQBCConnected() {
	typedef WORD(__cdecl *fEqbcIsConnected)(VOID);
	auto pLook = pPlugins;
	while (pLook && _strnicmp(pLook->szFilename, "mq2eqbc", 8)) pLook = pLook->pNext;
	if (pLook)
		if (fEqbcIsConnected checkf = (fEqbcIsConnected)GetProcAddress(pLook->hModule, "isConnected"))
			if (checkf()) return true;
	return false;
}

void EQBCBroadCast(PCHAR Buffer) {
	typedef VOID(__cdecl *fEqbcNetBotSendMsg)(PCHAR);
	if (strlen(Buffer) > 9) {
		auto pLook = pPlugins;
		while (pLook && _strnicmp(pLook->szFilename, "mq2eqbc", 8)) pLook = pLook->pNext;
		if (pLook)
			if (fEqbcNetBotSendMsg requestf = (fEqbcNetBotSendMsg)GetProcAddress(pLook->hModule, "NetBotSendMsg")) {
#if    DEBUGGING>1
				DebugSpewAlways("%s->BroadCasting(%s)", PLUGIN_NAME, Buffer);
#endif DEBUGGING
				requestf(Buffer);
			}
	}
}

std::shared_ptr<BotInfo> BotLoad(PCHAR Name) {
	BotInfo RecInfo;
	ZeroMemory(&RecInfo.Name, sizeof(BotInfo));
	strcpy_s(RecInfo.Name, Name);
	auto& [f, _] = NetMap.emplace(RecInfo.Name, std::make_shared<BotInfo>(RecInfo));
	return (*f).second;
}

void BotQuit(PCHAR Name) {
	auto f = NetMap.find(Name);
	if (NetMap.end() != f) NetMap.erase(f);
}

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=//

bool inGroup(unsigned long ID) {
	auto pID = (PSPAWNINFO)GetSpawnByID(ID);
	if (pID && pID->MasterID) pID = (PSPAWNINFO)GetSpawnByID(pID->MasterID);
	if (pID && GetCharInfo() && GetCharInfo()->pSpawn) {
		if (GetCharInfo()->pSpawn->SpawnID == pID->SpawnID)   return true;
		if (GetCharInfo()->pGroupInfo) for (int G = 1; G < 6; G++)
			if (auto pSpawn = GetGroupMember(G))
				if (pID->SpawnID == pSpawn->SpawnID)  return true;
	}
	return false;
}

bool inZoned(unsigned long zID, unsigned long iID) {
	return (GetCharInfo() && GetCharInfo()->zoneId == zID && GetCharInfo()->instance == iID);
}

int64_t NBGetEffectAmt(PSPELL pSpell, int i)
{
	int spa = GetSpellAttrib(pSpell, i);
	int64_t base = GetSpellBase(pSpell, i);
	int64_t max = GetSpellMax(pSpell, i);
	int calc = GetSpellCalc(pSpell, i);

	if (spa == SPA_NOSPELL)
		return 0;

	if (spa == SPA_CHA && (base <= 1 || base > 255))
		return 0;

	switch (spa)
	{
	case SPA_HASTE:
	case SPA_HEIGHT:
	case SPA_BARD_HASTE: // Adjust for Base=100
		base -= 100;
		max -= 100;
		break;
	case SPA_SUMMON_CORPSE: // Adjust for base/max swapped
		max = base;
		base = 0;
		break;
	case SPA_FOCUS_DAMAGE_MOD:
	case SPA_FOCUS_HEAL_MOD:
	case SPA_FOCUS_MANACOST_MOD: // Adjust for base2 used as max
		max = GetSpellBase2(pSpell, i);
		break;
	case SPA_FOCUS_REAGENT_MOD:
	case SPA_FOCUS_DAMAGE_AMT_DETRIMENTAL: // Adjust for base2 used as base
		base = GetSpellBase2(pSpell, i);
		break;
	}

	int64_t value = CalcValue(calc, (spa == SPA_STACKING_BLOCK) ? max : base, max, 1);
	return value;
}

// ***************************************************************************
// Function:    LargerEffectTest
// Description: Return boolean true if the spell effect is to be ignored
//              for stacking purposes
// ***************************************************************************
BOOL NBLargerEffectTest(PSPELL aSpell, PSPELL bSpell, int i)
{
	LONG aAttrib = GetSpellNumEffects(aSpell) > i ? GetSpellAttrib(aSpell, i) : 254;
	LONG bAttrib = GetSpellNumEffects(bSpell) > i ? GetSpellAttrib(bSpell, i) : 254;
	if (aAttrib == bAttrib)
		return abs(NBGetEffectAmt(aSpell, i)) >= abs(NBGetEffectAmt(bSpell, i));
	return false;
}

// ***************************************************************************
// Function:    TriggeringEffectSpell
// Description: Return boolean true if the spell effect is to be ignored
//              for stacking purposes
// ***************************************************************************
BOOL NBTriggeringEffectSpell(PSPELL aSpell, int i)
{
	LONG aAttrib = GetSpellNumEffects(aSpell) > i ? GetSpellAttrib(aSpell, i) : 254;
	return (aAttrib == 374		// Trigger Spell
		|| aAttrib == 442 	// Trigger Effect
		|| aAttrib == 443 	// Trigger Effect
		|| aAttrib == 453 	// Trigger Effect
		|| aAttrib == 454 	// Trigger Effect
		|| aAttrib == 470 	// Trigger Effect
		|| aAttrib == 475 	// Trigger Spell Non-Item
		|| aAttrib == 481); 	// Trigger Spell
}

// ***************************************************************************
// Function:    DurationWindowTest
// Description: Return boolean true if the spell effect is to be ignored
//              for stacking purposes
// ***************************************************************************
BOOL NBDurationWindowTest(PSPELL aSpell, PSPELL bSpell, int i)
{
	LONG aAttrib = GetSpellNumEffects(aSpell) > i ? GetSpellAttrib(aSpell, i) : 254;
	LONG bAttrib = GetSpellNumEffects(bSpell) > i ? GetSpellAttrib(bSpell, i) : 254;
	if ((aSpell->SpellType != 1 && aSpell->SpellType != 2) || (bSpell->SpellType != 1 && bSpell->SpellType != 2) || (aSpell->DurationWindow == bSpell->DurationWindow))
		return false;
	return (!(aAttrib == bAttrib &&
		(aAttrib == 2		// Attack Mod
			|| aAttrib == 162))); 	// Mitigate Melee Damage
}

// ***************************************************************************
// Function:    SpellEffectTest
// Description: Return boolean true if the spell effect is to be ignored
//              for stacking purposes
// ***************************************************************************
BOOL NBSpellEffectTest(PSPELL aSpell, PSPELL bSpell, int i, BOOL bIgnoreTriggeringEffects, BOOL bIgnoreCrossDuration)
{
	LONG aAttrib = GetSpellNumEffects(aSpell) > i ? GetSpellAttrib(aSpell, i) : 254;
	LONG bAttrib = GetSpellNumEffects(bSpell) > i ? GetSpellAttrib(bSpell, i) : 254;
	return ((aAttrib == 57 || bAttrib == 57)		// Levitate
		|| (aAttrib == 79 || bAttrib == 79)		// +HP when cast (priest buffs that have heal component, DoTs with DDs)
		|| (aAttrib == 134 || bAttrib == 134)		// Limit: Max Level
		|| (aAttrib == 135 || bAttrib == 135)		// Limit: Resist
		|| (aAttrib == 136 || bAttrib == 136)		// Limit: Target
		|| (aAttrib == 137 || bAttrib == 137)		// Limit: Effect
		|| (aAttrib == 138 || bAttrib == 138)		// Limit: SpellType
		|| (aAttrib == 139 || bAttrib == 139)		// Limit: Spell
		|| (aAttrib == 140 || bAttrib == 140)		// Limit: Min Duraction
		|| (aAttrib == 141 || bAttrib == 141)		// Limit: Instant
		|| (aAttrib == 142 || bAttrib == 142)		// Limit: Min Level
		|| (aAttrib == 143 || bAttrib == 143)		// Limit: Min Cast Time
		|| (aAttrib == 144 || bAttrib == 144)		// Limit: Max Cast Time
		|| (aAttrib == 254 || bAttrib == 254)		// Placeholder
		|| (aAttrib == 311 || bAttrib == 311)		// Limit: Combat Skills not Allowed
		|| (aAttrib == 339 || bAttrib == 339)		// Trigger DoT on cast
		|| (aAttrib == 340 || bAttrib == 340)		// Trigger DD on cast
		|| (aAttrib == 348 || bAttrib == 348)		// Limit: Min Mana
		|| (aAttrib == 385 || bAttrib == 385)		// Limit: Spell Group
		|| (aAttrib == 391 || bAttrib == 391)		// Limit: Max Mana
		|| (aAttrib == 403 || bAttrib == 403)		// Limit: Spell Class
		|| (aAttrib == 404 || bAttrib == 404)		// Limit: Spell Subclass
		|| (aAttrib == 411 || bAttrib == 411)		// Limit: Player Class
		|| (aAttrib == 412 || bAttrib == 412)		// Limit: Race
		|| (aAttrib == 414 || bAttrib == 414)		// Limit: Casting Skill
		|| (aAttrib == 415 || bAttrib == 415)		// Limit: Item Class
		|| (aAttrib == 420 || bAttrib == 420)		// Limit: Use
		|| (aAttrib == 421 || bAttrib == 421)		// Limit: Use Amt
		|| (aAttrib == 422 || bAttrib == 422)		// Limit: Use Min
		|| (aAttrib == 423 || bAttrib == 423)		// Limit: Use Type
		|| (aAttrib == 428 || bAttrib == 428)		// Limit: Skill
		|| (aAttrib == 460 || bAttrib == 460)		// Limit: Include Non-Focusable
		|| (aAttrib == 479 || bAttrib == 479)		// Limit: Value
		|| (aAttrib == 480 || bAttrib == 480)		// Limit: Value
		|| (aAttrib == 485 || bAttrib == 485)		// Limit: Caster Class
		|| (aAttrib == 486 || bAttrib == 486)		// Limit: Caster
		|| (NBLargerEffectTest(aSpell, bSpell, i))	// Ignore if the new effect is greater than the old effect
		|| (bIgnoreTriggeringEffects && (NBTriggeringEffectSpell(aSpell, i) || NBTriggeringEffectSpell(bSpell, i)))  // Ignore triggering effects validation
		|| (bIgnoreCrossDuration && NBDurationWindowTest(aSpell, bSpell, i)));	// Ignore if the effects cross Long/Short Buff windows (with exceptions)
}

// ***************************************************************************
// Function:    BuffStackTest
// Description: Return boolean true if the two spells will stack
// ***************************************************************************
BOOL NBBuffStackTest(PSPELL aSpell, PSPELL bSpell, BOOL bIgnoreTriggeringEffects, BOOL bIgnoreCrossDuration)
{
	if (aSpell->ID == bSpell->ID)
		return true;

	// We need to loop over the largest of the two, this may seem silly but one could have stacking command blocks
	// which we will always need to check.
	int effects = std::max(GetSpellNumEffects(aSpell), GetSpellNumEffects(bSpell));
	for (int i = 0; i < effects; i++) {
		// Compare 1st Buff to 2nd. If Attrib[i]==254 its a place holder. If it is 10 it
		// can be 1 of 3 things: PH(Base=0), CHA(Base>0), Lure(Base=-6). If it is Lure or
		// Placeholder, exclude it so slots don't match up. Now Check to see if the slots
		// have equal attribute values to check for stacking.
		int aAttrib = 254, bAttrib = 254; // Default to placeholder ...
		int64_t aBase = 0, bBase = 0, aBase2 = 0, bBase2 = 0;
		if (GetSpellNumEffects(aSpell) > i) {
			aAttrib = GetSpellAttrib(aSpell, i);
			aBase = GetSpellBase(aSpell, i);
			aBase2 = GetSpellBase2(aSpell, i);
		}
		if (GetSpellNumEffects(bSpell) > i) {
			bAttrib = GetSpellAttrib(bSpell, i);
			bBase = GetSpellBase(bSpell, i);
			bBase2 = GetSpellBase2(bSpell, i);
		}
		//		if (TriggeringEffectSpell(aSpell, i) || TriggeringEffectSpell(bSpell, i)) {
		//			if (!BuffStackTest(GetSpellByID(TriggeringEffectSpell(aSpell, i) ? aBase2 : aSpell->ID), GetSpellByID(TriggeringEffectSpell(bSpell, i) ? bBase2 : bSpell->ID), bIgnoreTriggeringEffects))
		//				return false;
		//		}
		if (bAttrib == aAttrib && !NBSpellEffectTest(aSpell, bSpell, i, bIgnoreTriggeringEffects, bIgnoreCrossDuration)) {
			if (!((bAttrib == 10 && (bBase == -6 || bBase == 0)) ||
				(aAttrib == 10 && (aBase == -6 || aBase == 0)) ||
				(bAttrib == 79 && bBase > 0 && bSpell->TargetType == 6) ||
				(aAttrib == 79 && aBase > 0 && aSpell->TargetType == 6) ||
				(bAttrib == 0 && bBase < 0) ||
				(aAttrib == 0 && aBase < 0) ||
				(bAttrib == 148 || bAttrib == 149) ||
				(aAttrib == 148 || aAttrib == 149))) {
				return false;
			}
		}
		// Check to see if second buff blocks first buff:
		// 148: Stacking: Block new spell if slot %d is effect
		// 149: Stacking: Overwrite existing spell if slot %d is effect
		if (bAttrib == 148 || bAttrib == 149) {
			// in this branch we know bSpell has enough slots
			int tmpSlot = (int)(bAttrib == 148 ? bBase2 - 1 : GetSpellCalc(bSpell, i) - 200 - 1);
			int64_t tmpAttrib = bBase;
			if (GetSpellNumEffects(aSpell) > tmpSlot) { // verify aSpell has that slot
				if (GetSpellMax(bSpell, i) > 0) {
					int64_t tmpVal = abs(GetSpellMax(bSpell, i));
					if (GetSpellAttrib(aSpell, tmpSlot) == tmpAttrib && GetSpellBase(aSpell, tmpSlot) < tmpVal) {
						return false;
					}
				}
				else if (GetSpellAttrib(aSpell, tmpSlot) == tmpAttrib) {
					return false;
				}
			}
		}
		/*
		// Now Check to see if the first buff blocks second buff. This is necessary
		// because only some spells carry the Block Slot. Ex. Brells and Spiritual
		// Vigor don't stack Brells has 1 slot total, for HP. Vigor has 4 slots, 2
		// of which block Brells.
		if (aAttrib == 148 || aAttrib == 149) {
			// in this branch we know aSpell has enough slots
			int tmpSlot = (aAttrib == 148 ? aBase2 - 1 : GetSpellCalc(aSpell, i) - 200 - 1);
			int tmpAttrib = aBase;
			if (GetSpellNumEffects(bSpell) > tmpSlot) { // verify bSpell has that slot
				if (GetSpellMax(aSpell, i) > 0) {
					int tmpVal = abs(GetSpellMax(aSpell, i));
					if (GetSpellAttrib(bSpell, tmpSlot) == tmpAttrib && GetSpellBase(bSpell, tmpSlot) < tmpVal) {
						return false;
					}
				}
				else if (GetSpellAttrib(bSpell, tmpSlot) == tmpAttrib) {
					return false;
				}
			}
		}
		*/
	}
	return true;
}

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=//

void InfoSong(BotInfo* botInfo, const char* Line) {
	char Buf[MAX_STRING];
	for (long Idx = 0; Idx < SONG_MAX; Idx++) {
		GetArg(Buf, Line, Idx + 1, FALSE, FALSE, FALSE, ':');
		botInfo->Song[Idx] = atol(Buf);
	}
}

void InfoPets(BotInfo* botInfo, const char* Line) {
	char Buf[MAX_STRING];
	for (long Idx = 0; Idx < PETS_MAX; Idx++) {
		GetArg(Buf, Line, Idx + 1, FALSE, FALSE, FALSE, ':');
		botInfo->Pets[Idx] = atol(Buf);
	}
}

/*
void InfoGems(PCHAR Line) {
  char Buf[MAX_STRING];
  for(long Idx=0; Idx<GEMS_MAX; Idx++) {
	GetArg(Buf,Line,Idx+1,FALSE,FALSE,FALSE,':');
	CurBot->Gem[Idx]=atol(Buf);
  }
}
*/

void InfoBuff(BotInfo* botInfo, const char* Line) {
	char Buf[MAX_STRING];
	for (long Idx = 0; Idx < BUFF_MAX; Idx++) {
		GetArg(Buf, Line, Idx + 1, FALSE, FALSE, FALSE, ':');
		botInfo->Buff[Idx] = atol(Buf);
	}
}


void InfoDura(BotInfo* botInfo, const char* Line) {
	char Buf[MAX_STRING];
	//WriteChatf("We got=%s", Line);
	for (long Idx = 0; Idx < BUFF_MAX; Idx++) {
		GetArg(Buf, Line, Idx + 1, FALSE, FALSE, FALSE, ':');
		botInfo->Duration[Idx] = atol(Buf);

		//if (Buf[0] != '\0') {
		//	WriteChatf("Set Duration to %s", Buf);
		//	WriteChatf("botInfo->Duration is %d", botInfo->Duration[Idx]);
		//}
	}
}

void InfoShortDura(BotInfo* botInfo, const char* Line) {
	char Buf[MAX_STRING];
	//  WriteChatf("We got=%s", Line);
	for (long Idx = 0; Idx < SONG_MAX; Idx++) {
		GetArg(Buf, Line, Idx + 1, FALSE, FALSE, FALSE, ':');
		botInfo->ShortDuration[Idx] = atol(Buf);
		//if (Buf[0] != '\0') {
		//	WriteChatf("Set Duration to %s", Buf);
		//	WriteChatf("botInfo->Duration is %d", botInfo->ShortDuration[Idx]);
		//}
	}
}

void InfoDetr(BotInfo* botInfo, const char* Line) {
	char Buf[MAX_STRING];
	for (long Idx = 0; Idx < DSIZE; Idx++) {
		GetArg(Buf, Line, Idx + 1, FALSE, FALSE, FALSE, ':');
		botInfo->Detrimental[Idx] = atoi(Buf);
	}
}

int SlotCalculate(PSPELL spell, int slot) {
	char Buf[MAX_STRING] = { 0 };
	SlotValueCalculate(Buf, spell, slot, 1);
	return atoi(Buf);
}

void __stdcall ParseInfo(unsigned int ID, void *pData, PBLECHVALUE pValues) {
	if (CurBot) while (pValues) {
		//WriteChatf("Parsing=%s", pValues->Name);
		int tmpInt = GetIntFromString(pValues->Value, 0);
		switch (GetIntFromString(pValues->Name, 0)) {
			case  1:
				CurBot->ZoneID = tmpInt;
				break;
			case  2:
				CurBot->InstID = tmpInt;
				break;
			case  3:
				CurBot->SpawnID = tmpInt;
				break;
			case  4:
				CurBot->Level = tmpInt;
				break;
			case  5:
				CurBot->ClassID = tmpInt;
				break;
			case  6:
				CurBot->LifeCur = tmpInt;
				break;
			case  7:
				CurBot->LifeMax = tmpInt;
				break;
			case  8:
				CurBot->EnduCur = tmpInt;
				break;
			case  9:
				CurBot->EnduMax = tmpInt;
				break;
			case 10:
				CurBot->ManaCur = tmpInt;
				break;
			case 11:
				CurBot->ManaMax = tmpInt;
				break;
			case 12:
				CurBot->PetID = tmpInt;
				break;
			case 13:
				CurBot->PetHP = tmpInt;
				break;
			case 14:
				CurBot->TargetID = tmpInt;
				break;
			case 15:
				CurBot->TargetHP = tmpInt;
				break;
			case 16:
				CurBot->CastID = tmpInt;
				break;
			case 17:
				CurBot->State = (WORD)tmpInt;
				break;
			case 18:
				CurBot->XP = (DWORD)tmpInt;
				break;
			case 19:
				CurBot->aaXP = (DWORD)tmpInt;
				break;
#if HAS_LEADERSHIP_EXPERIENCE
			case 20:
				CurBot->glXP = GetDoubleFromString(pValues->Value, 0.0);
				break;
#endif
			case 21:
				CurBot->FreeBuff = tmpInt;
				break;
			case 22:
				strcpy_s(CurBot->Leader, pValues->Value.c_str());
				break;
			//      case 30: InfoGems(pValues->Value);                      break;
			case 31:
				InfoBuff(CurBot, pValues->Value.c_str());
				break;
			case 32:
				InfoSong(CurBot, pValues->Value.c_str());
				break;
			case 33:
				InfoPets(CurBot, pValues->Value.c_str());
				break;
			case 34:
				InfoDura(CurBot, pValues->Value.c_str());
				break;
			case 35:
				CurBot->TotalAA = tmpInt;
				break;
			case 36:
				CurBot->UsedAA = tmpInt;
				break;
			case 37:
				CurBot->UnusedAA = tmpInt;
				break;
			case 38:
				CurBot->CombatState = tmpInt;
				break;
			case 39:
				strcpy_s(CurBot->Note, pValues->Value.c_str());
				break;
			case 40:
				InfoDetr(CurBot, pValues->Value.c_str());
				break;
			case 89:
				strcpy_s(CurBot->Location, pValues->Value.c_str());
				break;
			case 90:
				strcpy_s(CurBot->Heading, pValues->Value.c_str());
				break;
			case 155: 
				InfoShortDura(CurBot, pValues->Value.c_str());
				break;
			default:
				break;
		}
		pValues = pValues->pNext;
	}
}

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=//

template <unsigned int _Size>
PSTR MakeShortDURAS(CHAR(&Buffer)[_Size]) {
	int Duration; int SpellID; char tmp[MAX_STRING] = { 0 }; Buffer[0] = '\0';
	//char tmp2[MAX_STRING] = { 0 }; char tmp3[MAX_STRING] = { 0 };
	for (int b = 0; b < SONG_MAX; b++) {
		if ((SpellID = GetPcProfile()->GetTempEffect(b).SpellID) > 0) {
			//sprintf_s(tmp3, "%d: ", SpellID);
			//strcat_s(tmp2, tmp3);
			if (auto buffInfo = pSongWnd->GetBuffInfoBySpellID(SpellID)) {
				if ((Duration = buffInfo.GetBuffTimer()) != 0) {
					if (Duration < 0)
					{
						Duration = 999999000;
					}
					sprintf_s(tmp, "%d:", Duration);
					strcat_s(Buffer, tmp);

					//sprintf_s(tmp3, "%0.1f, ", ((float)Duration)/1000.0f);
					//strcat_s(tmp2, tmp3);
				}
			}
		}
	}
	//WriteChatf(tmp2);
	return Buffer;
}

template <unsigned int _Size>
PSTR MakeDURAS(CHAR(&Buffer)[_Size]) {
	int Duration; int SpellID; char tmp[MAX_STRING] = { 0 }; Buffer[0] = '\0';
	//char tmp2[MAX_STRING] = { 0 }; char tmp3[MAX_STRING] = { 0 };
	for (int b = 0; b < BUFF_MAX; b++) {
		if ((SpellID = GetPcProfile()->GetEffect(b).SpellID) > 0) {
			//sprintf_s(tmp3, "%d: ", SpellID);
			//strcat_s(tmp2, tmp3);
			if (auto buffInfo = pBuffWnd->GetBuffInfoBySpellID(SpellID)) {
				if ((Duration = buffInfo.GetBuffTimer()) != 0) {
					if (Duration < 0)
					{
						Duration = 999999000;
					}
					sprintf_s(tmp, "%d:", Duration);
					strcat_s(Buffer, tmp);

					//sprintf_s(tmp3, "%0.1f, ", ((float)Duration)/1000.0f);
					//strcat_s(tmp2, tmp3);
				}
			}
		}
	}
	//WriteChatf(tmp2);
	return Buffer;
}


template <unsigned int _Size>
PSTR MakeBUFFS(CHAR(&Buffer)[_Size]) {
	int SpellID; char tmp[MAX_STRING] = { 0 }; Buffer[0] = '\0';
	for (int b = 0; b < BUFF_MAX; b++) {
		if ((SpellID = GetPcProfile()->GetEffect(b).SpellID) > 0) {
			if (auto buffInfo = pBuffWnd->GetBuffInfoBySpellID(SpellID)) {
				if (buffInfo.GetBuffTimer() != 0) {
					sprintf_s(tmp, "%d:", SpellID);
					strcat_s(Buffer, tmp);
				}
			}
		}
	}
	if (strlen(Buffer)) {
		sprintf_s(tmp, "|F=${Me.FreeBuffSlots}");
		ParseMacroData(tmp, sizeof(tmp));
		strcat_s(Buffer, tmp);
	}
	return Buffer;
}

template <unsigned int _Size>PSTR MakeSONGS(CHAR(&Buffer)[_Size]) {
	int SpellID = 0; char tmp[MAX_STRING] = { 0 }; Buffer[0] = '\0';
	for (int b = 0; b < NUM_TEMP_BUFFS; b++) {
		if ((SpellID = GetPcProfile()->GetTempEffect(b).SpellID) > 0) {
			if (auto buffInfo = pSongWnd->GetBuffInfoBySpellID(SpellID)) {
				if (buffInfo.GetBuffTimer() != 0) {
					sprintf_s(tmp, "%d:", SpellID);
					strcat_s(Buffer, tmp);
				}
			}
		}
	}
	return Buffer;
}

template <unsigned int _Size>PSTR MakeCASTD(CHAR(&Buffer)[_Size]) {
	long Casting = GetCharInfo()->pSpawn->CastingData.SpellID;
	if (Casting > 0)
		_itoa_s(Casting, Buffer, 10);
	else
		Buffer[0] = 0;
	return Buffer;
}

template <unsigned int _Size>PSTR MakeHEADN(CHAR(&Buffer)[_Size]) {
	if (strlen(Buffer)) {
		sprintf_s(Buffer, "%4.2f", GetCharInfo()->pSpawn->Heading);
	}
	return Buffer;
}

template <unsigned int _Size>PSTR MakeLOCAT(CHAR(&Buffer)[_Size]) {
	if (strlen(Buffer)) {
		sprintf_s(Buffer, "%4.2f:%4.2f:%4.2f:%d", GetCharInfo()->pSpawn->Y, GetCharInfo()->pSpawn->X, GetCharInfo()->pSpawn->Z, GetCharInfo()->pSpawn->Animation);
	}
	return Buffer;
}

template <unsigned int _Size>PSTR MakeENDUS(CHAR(&Buffer)[_Size]) {
	if (long EnduMax = GetMaxEndurance()) sprintf_s(Buffer, "%d/%d", GetPcProfile()->Endurance, EnduMax);
	else strcpy_s(Buffer, "/");
	return Buffer;
}

template <unsigned int _Size>PSTR MakeEXPER(CHAR(&Buffer)[_Size]) {
#if HAS_LEADERSHIP_EXPERIENCE
	sprintf_s(Buffer, "%I64d:%d:%02.3f", GetCharInfo()->Exp, GetCharInfo()->AAExp, GetCharInfo()->GroupLeadershipExp);
#else
	sprintf_s(Buffer, "%I64d:%d", GetCharInfo()->Exp, GetCharInfo()->AAExp);
#endif
	return Buffer;
}

template <unsigned int _Size>PSTR MakeLEADR(CHAR(&Buffer)[_Size]) {
	if (auto pChar = GetCharInfo()) {
		if (pChar->pGroupInfo && pChar->pGroupInfo->pLeader && !pChar->pGroupInfo->pLeader->Name.empty()) {
			strcpy_s(Buffer, pChar->pGroupInfo->pLeader->Name.c_str());
		}
		else {
			Buffer[0] = 0;
		}
		return Buffer;
	}
	else {
		Buffer[0] = 0;
	}
	return Buffer;
}

template <unsigned int _Size>PSTR MakeNOTE(CHAR(&Buffer)[_Size]) {
	strcpy_s(Buffer, NetNote);
	return Buffer;
}

template <unsigned int _Size>PSTR MakeLEVEL(CHAR(&Buffer)[_Size]) {
	sprintf_s(Buffer, "%d:%d", GetPcProfile()->Level, GetPcProfile()->Class);
	return Buffer;
}

template <unsigned int _Size>PSTR MakeLIFES(CHAR(&Buffer)[_Size]) {
	sprintf_s(Buffer, "%d/%d", GetCurHPS(), GetMaxHPS());
	return Buffer;
}

template <unsigned int _Size>PSTR MakeMANAS(CHAR(&Buffer)[_Size]) {
	if (long ManaMax = GetMaxMana()) sprintf_s(Buffer, "%d/%d", GetPcProfile()->Mana, ManaMax);
	else strcpy_s(Buffer, "/");
	return Buffer;
}

template <unsigned int _Size>PSTR MakePBUFF(CHAR(&Buffer)[_Size]) {
	Buffer[0] = '\0';
	auto Pet = GetSpawnByID(pLocalPlayer->PetID);
	if (Pet && pPetInfoWnd)
	{
		// SpellID is an int, so the longest is probably 11.
		char tmp[20] = { 0 };
		for (int b = 0; b < pPetInfoWnd->GetMaxBuffs(); b++)
		{
			int SpellID = pPetInfoWnd->GetBuff(b);
			if (SpellID > 0) {
				sprintf_s(tmp, "%d:", SpellID);
				strcat_s(Buffer, tmp);
			}
		}
	}
	//  WriteChatf("MakePBUFF: [%s]", Buffer);
	return Buffer;
}

template <unsigned int _Size>PSTR MakePETIL(CHAR(&Buffer)[_Size]) {
	auto Pet = (PSPAWNINFO)GetSpawnByID(GetCharInfo()->pSpawn->PetID);
	if (pPetInfoWnd && Pet) {
		sprintf_s(Buffer, "%d:%d", Pet->SpawnID,
			static_cast<int32_t>(Pet->HPCurrent * 100 / Pet->HPMax));
	} else {
		strcpy_s(Buffer, ":");
	}
	return Buffer;
}

/*
PSTR MakeSPGEM(CHAR(&Buffer)[_Size]) {
  long SpellID; char tmp[32]; Buffer[0]=0;
  for(int g=0; g<GEMS_MAX; g++)
	if((SpellID=(pCastSpellWnd && GetMaxMana()>0)?GetCharInfo2()->MemorizedSpells[g]:0)>0) {
	  sprintf_s(tmp,"%d:",SpellID);
	  strcat_s(Buffer,tmp);
	}
  return Buffer;
}
*/

template <unsigned int _Size>PSTR MakeSTATE(CHAR(&Buffer)[_Size]) {
	WORD Status = 0;
	if (pEverQuestInfo->bAutoAttack)                       Status |= STATE_ATTACK;
	if (pRaid && pRaid->RaidMemberCount)                   Status |= STATE_RAID;
	if (pLocalPC->Stunned)                                 Status |= STATE_STUN;
	if (pLocalPC->pGroupInfo)                              Status |= STATE_GROUP;
	if (FindSpeed(pLocalPC->pSpawn))                       Status |= STATE_MOVING;
	if (pLocalPC->pSpawn->Mount)                           Status |= STATE_MOUNT;
	if (pLocalPC->pSpawn->AFK)                             Status |= STATE_AFK;
	if (pLocalPC->pSpawn->HideMode)                        Status |= STATE_INVIS;
	if (pLocalPC->pSpawn->mPlayerPhysicsClient.Levitate)   Status |= STATE_LEV;
	if (pLocalPC->pSpawn->LFG)                             Status |= STATE_LFG;
	if (pLocalPC->pSpawn->StandState == STANDSTATE_DEAD)   Status |= STATE_DEAD;
	if (pLocalPC->pSpawn->StandState == STANDSTATE_FEIGN)  Status |= STATE_FEIGN;
	if (pLocalPC->pSpawn->StandState == STANDSTATE_DUCK)   Status |= STATE_DUCK;
	if (pLocalPC->pSpawn->StandState == STANDSTATE_BIND)   Status |= STATE_BIND;
	if (pLocalPC->pSpawn->StandState == STANDSTATE_STAND)  Status |= STATE_STAND;
	if (pLocalPC->pSpawn->StandState == STANDSTATE_SIT)    Status |= STATE_SIT;
	_itoa_s(Status, Buffer, 10);
	return Buffer;
}

template <unsigned int _Size>PSTR MakeOOCST(CHAR(&Buffer)[_Size]) {
	_itoa_s(pPlayerWnd->CombatState, Buffer, 10);
	return Buffer;
}

template <unsigned int _Size>PSTR MakeAAPTS(CHAR(&Buffer)[_Size]) {
	sprintf_s(Buffer, "%d:%d:%d", GetPcProfile()->AAPoints + GetPcProfile()->AAPointsSpent, GetPcProfile()->AAPointsSpent, GetPcProfile()->AAPoints);
	return Buffer;
}

template <unsigned int _Size>PSTR MakeTARGT(CHAR(&Buffer)[_Size]) {
	auto Tar = pTarget ? ((PSPAWNINFO)pTarget) : NULL;
	if (Tar) {
		sprintf_s(Buffer, "%d:%d", Tar->SpawnID,
			static_cast<int32_t>(Tar->HPCurrent * 100 / Tar->HPMax));
	}
	else {
		strcpy_s(Buffer, ":");
	}
	return Buffer;
}

template <unsigned int _Size>PSTR MakeZONES(CHAR(&Buffer)[_Size]) {
	sprintf_s(Buffer, "%d:%d>%d", GetCharInfo()->zoneId, GetCharInfo()->instance, GetCharInfo()->pSpawn->SpawnID);
	return Buffer;
}

template <unsigned int _Size>PSTR MakeDETR(CHAR(&Buffer)[_Size]) {
	char tmp[MAX_STRING]; Buffer[0] = 0;
	ZeroMemory(&dValues, sizeof(dValues));
	for (int b = 0; b < BUFF_MAX; b++) {
		if (auto spell = GetSpellByID(GetPcProfile()->GetEffect(b).SpellID)) {
			if (!spell->SpellType) {
				bool d = false;
				bool r = false;
				for (int s = 0; s < GetSpellNumEffects(spell); s++) switch (GetSpellAttrib(spell, s)) {
				case   0: if (GetSpellBase(spell, s) < 0) { dValues[LIFEDRAIN] += SlotCalculate(spell, s); d = true; } break;
				case   3: if (GetSpellBase(spell, s) < 0) { dValues[SNARED]++; d = true; } break;
				case  11: if (((GetSpellMax(spell, s)) ? GetSpellMax(spell, s) : GetSpellBase(spell, s)) - 100 < 0) { dValues[SLOWED]++; d = true; } break;
				case  15: if (GetSpellBase(spell, s) < 0) { dValues[MANADRAIN] += SlotCalculate(spell, s); d = true; } break;
				case  20: dValues[BLINDED]++; d = true; break;
				case  22: dValues[CHARMED]++;  d = true; break;
				case  23: dValues[FEARED]++; d = true; break;
				case  31: dValues[MESMERIZED]++; d = true; break;
				case  35: dValues[DISEASED] += (int)GetSpellBase(spell, s); d = true; break;
				case  36: dValues[POISONED] += (int)GetSpellBase(spell, s); d = true; break;
				case  40: dValues[INVULNERABLE]++; d = true; break;
				case  46: if (GetSpellBase(spell, s) < 0) { r = true; d = true; } break;
				case  47: if (GetSpellBase(spell, s) < 0) { r = true; d = true; } break;
				case  48: if (GetSpellBase(spell, s) < 0) { r = true; d = true; } break;
				case  49: if (GetSpellBase(spell, s) < 0) { r = true; d = true; } break;
				case  50: if (GetSpellBase(spell, s) < 0) { r = true; d = true; } break;
				case  96: dValues[SILENCED]++; d = true; break;
				case  99: dValues[ROOTED]++; d = true; break;
				case 112: if (GetSpellBase(spell, s) < 0) { dValues[CASTINGLEVEL]++; d = true; } break;
				case 116: dValues[CURSED] += (int)GetSpellBase(spell, s); d = true; break;
				case 120: if (GetSpellBase(spell, s) < 0) { dValues[HEALING]++; d = true; } break;
				case 124: if (GetSpellBase(spell, s) < 0) { dValues[SPELLDAMAGE]++; d = true; } break;
				case 127: if (GetSpellBase(spell, s) < 0) { dValues[SPELLSLOWED]++; d = true; } break;
				case 132: if (GetSpellBase(spell, s) < 0) { dValues[SPELLCOST]++; d = true; } break;
				case 189: if (GetSpellBase(spell, s) < 0) { dValues[ENDUDRAIN] += SlotCalculate(spell, s); d = true; } break;
				case 289: dValues[TRIGGR]++; d = true; break;
				case 369: dValues[CORRUPTED] += (int)GetSpellBase(spell, s); d = true; break;
				}
				if (d) {
					dValues[DETRIMENTALS]++;
					switch (spell->ID) {
					case 45473:  /* Putrid Infection is curable, but missing WearOff message */
						break;
					default:
						/*CastByMe,CastByOther,CastOnYou,CastOnAnother,WearOff*/
						if ((spell->IsNoDispell() && GetSpellString(spell->ID, 4) != nullptr) || spell->TargetType == 6) dValues[NOCURE]++;
						break;
					}
				}
				if (r) dValues[RESISTANCE]++;
			}
		}
	}
	dValues[COUNTERS] = dValues[CURSED] + dValues[DISEASED] + dValues[POISONED] + dValues[CORRUPTED];
	for (int a = 0; a < DSIZE; a++) {
		sprintf_s(tmp, "%d:", dValues[a]);
		strcat_s(Buffer, tmp);
	}
	return Buffer;
}

void BroadCast() {
	char Buffer[MAX_STRING];
	long nChange = false;
	long nUpdate = false;
	ZeroMemory(wBuffer, sizeof(wBuffer));
	ZeroMemory(wChange, sizeof(wChange));
	ZeroMemory(wUpdate, sizeof(wUpdate));
	sprintf_s(wBuffer[BUFFS], "B=%s|", MakeBUFFS(Buffer));
	sprintf_s(wBuffer[CASTD], "C=%s|", MakeCASTD(Buffer));
	sprintf_s(wBuffer[ENDUS], "E=%s|", MakeENDUS(Buffer));
	sprintf_s(wBuffer[EXPER], "X=%s|", MakeEXPER(Buffer));
	sprintf_s(wBuffer[LEADR], "N=%s|", MakeLEADR(Buffer));
	sprintf_s(wBuffer[LEVEL], "L=%s|", MakeLEVEL(Buffer));
	sprintf_s(wBuffer[LIFES], "H=%s|", MakeLIFES(Buffer));
	sprintf_s(wBuffer[MANAS], "M=%s|", MakeMANAS(Buffer));
	sprintf_s(wBuffer[PBUFF], "W=%s|", MakePBUFF(Buffer));
	sprintf_s(wBuffer[PETIL], "P=%s|", MakePETIL(Buffer));
	//  sprintf_s(wBuffer[SPGEM],"G=%s|",MakeSPGEM(Buffer));
	sprintf_s(wBuffer[SONGS], "S=%s|", MakeSONGS(Buffer));
	sprintf_s(wBuffer[STATE], "Y=%s|", MakeSTATE(Buffer));
	sprintf_s(wBuffer[TARGT], "T=%s|", MakeTARGT(Buffer));
	sprintf_s(wBuffer[ZONES], "Z=%s|", MakeZONES(Buffer));
	sprintf_s(wBuffer[DURAS], "D=%s|", MakeDURAS(Buffer));
	sprintf_s(wBuffer[SHORTDURAS], "F=%s|", MakeShortDURAS(Buffer));
	sprintf_s(wBuffer[AAPTS], "A=%s|", MakeAAPTS(Buffer));
	sprintf_s(wBuffer[OOCST], "O=%s|", MakeOOCST(Buffer));
	sprintf_s(wBuffer[NOTE],  "U=%s|", MakeNOTE(Buffer));
	sprintf_s(wBuffer[DETR],  "R=%s|", MakeDETR(Buffer));
	sprintf_s(wBuffer[LOCAT], "@=%s|", MakeLOCAT(Buffer));
	sprintf_s(wBuffer[HEADN], "$=%s|", MakeHEADN(Buffer));

	//  WriteChatf("D=%s|", Buffer);
	for (int i = 0; i < ESIZE; i++)
		if ((clock() > sTimers[i] && clock() > sTimers[i] + UPDATES) || 0 != strcmp(wBuffer[i], sBuffer[i])) {
			wChange[i] = true;
			nChange++;
		}
		else if (clock() < sTimers[i] && clock() + UPDATES > sTimers[i]) {
			wUpdate[i] = true;
			nUpdate++;
		}
	if (nChange) {
		strcpy_s(Buffer, "[NB]|");
		for (int i = 0; i < ESIZE; i++)
			if (wChange[i] || wUpdate[i] && (strlen(Buffer) + strlen(wBuffer[i])) < MAX_STRING - 5) {
				strcat_s(Buffer, wBuffer[i]);
				sTimers[i] = (long)clock() + REFRESH;
			}
		strcat_s(Buffer, "[NB]");
		// WriteChatf("Broadcast %s", Buffer);

		EQBCBroadCast(Buffer);
		memcpy(sBuffer, wBuffer, sizeof(wBuffer));
		if (CurBot = BotLoad(GetCharInfo()->Name).get()) {
			Packet.Feed(Buffer);
			CurBot->Updated = clock();
			CurBot = 0;
		}
	}
}

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=//

class MQ2NetBotsType *pNetBotsType = 0;
class MQ2NetBotsType : public MQ2Type {

private:
	char Temps[MAX_STRING];
	char Works[MAX_STRING];
	long Cpt;

public:
	enum Information {
		Enable = 1,
		Listen = 2,
		Output = 3,
		Counts = 5,
		Client = 6,
		Name = 7,
		Zone = 8,
		Instance = 9,
		ID = 10,
		Class = 11,
		Level = 12,
		PctExp = 13,
		PctAAExp = 14,
#if HAS_LEADERSHIP_EXPERIENCE
		PctGroupLeaderExp = 15,
#endif
		CurrentHPs = 16,
		MaxHPs = 17,
		PctHPs = 18,
		CurrentEndurance = 19,
		MaxEndurance = 20,
		PctEndurance = 21,
		CurrentMana = 22,
		MaxMana = 23,
		PctMana = 24,
		PetID = 25,
		PetHP = 26,
		TargetID = 27,
		TargetHP = 28,
		Casting = 29,
		State = 30,
		Attacking = 31,
		AFK = 32,
		Binding = 33,
		Ducking = 34,
		Invis = 35,
		Feigning = 36,
		Grouped = 37,
		Levitating = 38,
		LFG = 39,
		Mounted = 40,
		Moving = 41,
		Raid = 42,
		Sitting = 43,
		Standing = 44,
		Stunned = 45,
		//    Gem=46,
		Buff = 47,
		ShortBuff = 48,
		PetBuff = 49,
		FreeBuffSlots = 50,
		InZone = 51,
		InGroup = 52,
		Leader = 53,
		Updated = 54,
		Duration=55,
		TotalAA = 56,
		UsedAA = 57,
		UnusedAA = 58,
		CombatState = 59,
		Stacks = 60,
		Note = 61,
		Detrimentals = 62,
		Counters = 63,
		Cursed = 64,
		Diseased = 65,
		Poisoned = 66,
		Corrupted = 67,
		EnduDrain = 68,
		LifeDrain = 69,
		ManaDrain = 70,
		Blinded = 71,
		CastingLevel = 72,
		Charmed = 73,
		Feared = 74,
		Healing = 75,
		Invulnerable = 76,
		Mesmerized = 77,
		Rooted = 78,
		Silenced = 79,
		Slowed = 80,
		Snared = 81,
		SpellCost = 82,
		SpellSlowed = 83,
		SpellDamage = 84,
		Trigger = 85,
		Resistance = 86,
		Detrimental = 87,
		NoCure = 88,
		Location = 89,
		Heading = 90,
		StacksPet = 91,
		ShortDuration=155,
	};

	MQ2NetBotsType() :MQ2Type("NetBots") {
		TypeMember(Enable);
		TypeMember(Listen);
		TypeMember(Output);
		TypeMember(Counts);
		TypeMember(Client);
		TypeMember(Name);
		TypeMember(Zone);
		TypeMember(Instance);
		TypeMember(ID);
		TypeMember(Class);
		TypeMember(Level);
		TypeMember(PctExp);
		TypeMember(PctAAExp);
#if HAS_LEADERSHIP_EXPERIENCE
		TypeMember(PctGroupLeaderExp);
#endif
		TypeMember(CurrentHPs);
		TypeMember(MaxHPs);
		TypeMember(PctHPs);
		TypeMember(CurrentEndurance);
		TypeMember(MaxEndurance);
		TypeMember(PctEndurance);
		TypeMember(CurrentMana);
		TypeMember(MaxMana);
		TypeMember(PctMana);
		TypeMember(PetID);
		TypeMember(PetHP);
		TypeMember(TargetID);
		TypeMember(TargetHP);
		TypeMember(Casting);
		TypeMember(State);
		TypeMember(Attacking);
		TypeMember(AFK);
		TypeMember(Binding);
		TypeMember(Ducking);
		TypeMember(Invis);
		TypeMember(Feigning);
		TypeMember(Grouped);
		TypeMember(Levitating);
		TypeMember(LFG);
		TypeMember(Mounted);
		TypeMember(Moving);
		TypeMember(Raid);
		TypeMember(Sitting);
		TypeMember(Standing);
		TypeMember(Stunned);
		//    TypeMember(Gem);
		TypeMember(Buff);
		TypeMember(ShortBuff);
		TypeMember(PetBuff);
		TypeMember(FreeBuffSlots);
		TypeMember(InZone);
		TypeMember(InGroup);
		TypeMember(Leader);
		TypeMember(Updated);
		TypeMember(Duration);
		TypeMember(ShortDuration);
		TypeMember(TotalAA);
		TypeMember(UsedAA);
		TypeMember(UnusedAA);
		TypeMember(CombatState);
		TypeMember(Stacks);
		TypeMember(Note);
		TypeMember(Detrimentals);
		TypeMember(Counters);
		TypeMember(Cursed);
		TypeMember(Diseased);
		TypeMember(Poisoned);
		TypeMember(Corrupted);
		TypeMember(EnduDrain);
		TypeMember(LifeDrain);
		TypeMember(ManaDrain);
		TypeMember(Blinded);
		TypeMember(CastingLevel);
		TypeMember(Charmed);
		TypeMember(Feared);
		TypeMember(Healing);
		TypeMember(Invulnerable);
		TypeMember(Mesmerized);
		TypeMember(Rooted);
		TypeMember(Silenced);
		TypeMember(Slowed);
		TypeMember(Snared);
		TypeMember(SpellCost);
		TypeMember(SpellSlowed);
		TypeMember(SpellDamage);
		TypeMember(Trigger);
		TypeMember(Resistance);
		TypeMember(Detrimental);
		TypeMember(NoCure);
		TypeMember(StacksPet);
		TypeMember(Location);
		TypeMember(Heading);
	}

	virtual bool GetMember(MQVarPtr VarPtr, const char* Member, char* Index, MQTypeVar& Dest) override {
		if (auto pMember = MQ2NetBotsType::FindMember(Member)) {
			switch ((Information)pMember->ID) {
			case Enable:
				Dest.Type = mq::datatypes::pBoolType;
				Dest.DWord = NetStat;
				return true;
			case Listen:
				Dest.Type = mq::datatypes::pBoolType;
				Dest.DWord = NetGrab;
				return true;
			case Output:
				Dest.Type = mq::datatypes::pBoolType;
				Dest.DWord = NetSend;
				return true;
			case Counts:
				Cpt = 0;
				if (NetStat && NetGrab)
					for (auto& [_, botInfo] : NetMap) {
						if (botInfo->SpawnID == 0) 
							continue;
						Cpt++;
					}
				Dest.Type = mq::datatypes::pIntType;
				Dest.Int = Cpt;
				return true;
			case Client:
				Cpt = 0; Temps[0] = 0;
				if (NetStat && NetGrab)
					for (auto& [_, botInfo] : NetMap) {
						if (botInfo->SpawnID == 0) 
							continue;
						if (Cpt++) strcat_s(Temps, " ");
						strcat_s(Temps, botInfo->Name);
					}
				if (IsNumber(Index)) {
					int n = atoi(Index);
					if (n<0 || n>Cpt) break;
					strcpy_s(Temps, GetArg(Works, Temps, n));
				}

				Dest.Type = mq::datatypes::pStringType;
				Dest.Ptr = Temps;
				return true;
			}
			if (auto botRec = VarPtr.Get<BotInfo>()) {
				switch ((Information)pMember->ID) {
				case Name:
					Dest.Type = mq::datatypes::pStringType;
					Dest.Ptr = Temps;
					strcpy_s(Temps, botRec->Name);
					return true;
				case Zone:
					Dest.Type = mq::datatypes::pIntType;
					Dest.DWord = botRec->ZoneID;
					return true;
				case Instance:
					Dest.Type = mq::datatypes::pIntType;
					Dest.DWord = botRec->InstID;
					return true;
				case ID:
					Dest.Type = mq::datatypes::pIntType;
					Dest.DWord = botRec->SpawnID;
					return true;
				case Class:
					Dest.Type = mq::datatypes::pClassType;
					Dest.DWord = botRec->ClassID;
					return true;
				case Level:
					Dest.Type = mq::datatypes::pIntType;
					Dest.DWord = botRec->Level;
					return true;
				case PctExp:
					Dest.Type = mq::datatypes::pFloatType;
					Dest.Float = (float)(botRec->XP / 3.30f);
					return true;
				case PctAAExp:
					Dest.Type = mq::datatypes::pFloatType;
					Dest.Float = (float)(botRec->aaXP / 3.30f);
					return true;
#if HAS_LEADERSHIP_EXPERIENCE
				case PctGroupLeaderExp:
					Dest.Type = mq::datatypes::pFloatType;
					Dest.Float = (float)(botRec->glXP / 10.0f);
					return true;
#endif
				case CurrentHPs:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->LifeCur;
					return true;
				case MaxHPs:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->LifeMax;
					return true;
				case PctHPs:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = (botRec->LifeMax < 1 || botRec->LifeCur < 1) ? 0 : botRec->LifeCur * 100 / botRec->LifeMax;
					return true;
				case CurrentEndurance:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->EnduCur;
					return true;
				case MaxEndurance:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->EnduMax;
					return true;
				case PctEndurance:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = (botRec->EnduMax < 1 || botRec->EnduCur < 1) ? 0 : botRec->EnduCur * 100 / botRec->EnduMax;
					return true;
				case CurrentMana:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->ManaCur;
					return true;
				case MaxMana:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->ManaMax;
					return true;
				case PctMana:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = (botRec->ManaMax < 1 || botRec->ManaCur < 1) ? 0 : botRec->ManaCur * 100 / botRec->ManaMax;
					return true;
				case PetID:
					Dest.Type = mq::datatypes::pIntType;
					Dest.DWord = botRec->PetID;
					return true;
				case PetHP:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->PetHP;
					return true;
				case TargetID:
					Dest.Type = mq::datatypes::pIntType;
					Dest.DWord = botRec->TargetID;
					return true;
				case TargetHP:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->TargetHP;
					return true;
				case Casting:
					if (botRec->CastID) {
						Dest.Type = mq::datatypes::pSpellType;
						Dest.Ptr = GetSpellByID(botRec->CastID);
						return true;
					}
					break;
				case State:
					Dest.Type = mq::datatypes::pStringType;
					Dest.Ptr = Temps;
					if (botRec->State & STATE_STUN)       strcpy_s(Temps, "STUN");
					else if (botRec->State & STATE_STAND) strcpy_s(Temps, "STAND");
					else if (botRec->State & STATE_SIT)   strcpy_s(Temps, "SIT");
					else if (botRec->State & STATE_DUCK)  strcpy_s(Temps, "DUCK");
					else if (botRec->State & STATE_BIND)  strcpy_s(Temps, "BIND");
					else if (botRec->State & STATE_FEIGN) strcpy_s(Temps, "FEIGN");
					else if (botRec->State & STATE_DEAD)  strcpy_s(Temps, "DEAD");
					else strcpy_s(Temps, "UNKNOWN");
					return true;
				case Attacking:
					Dest.Type = mq::datatypes::pBoolType;
					Dest.DWord = botRec->State & STATE_ATTACK;
					return true;
				case AFK:
					Dest.Type = mq::datatypes::pBoolType;
					Dest.DWord = botRec->State & STATE_AFK;
					return true;
				case Binding:
					Dest.Type = mq::datatypes::pBoolType;
					Dest.DWord = botRec->State & STATE_BIND;
					return true;
				case Ducking:
					Dest.Type = mq::datatypes::pBoolType;
					Dest.DWord = botRec->State & STATE_DUCK;
					return true;
				case Feigning:
					Dest.Type = mq::datatypes::pBoolType;
					Dest.DWord = botRec->State & STATE_FEIGN;
					return true;
				case Grouped:
					Dest.Type = mq::datatypes::pBoolType;
					Dest.DWord = botRec->State & STATE_GROUP;
					return true;
				case Invis:
					Dest.Type = mq::datatypes::pBoolType;
					Dest.DWord = botRec->State & STATE_INVIS;
					return true;
				case Levitating:
					Dest.Type = mq::datatypes::pBoolType;
					Dest.DWord = botRec->State & STATE_LEV;
					return true;
				case LFG:
					Dest.Type = mq::datatypes::pBoolType;
					Dest.DWord = botRec->State & STATE_LFG;
					return true;
				case Mounted:
					Dest.Type = mq::datatypes::pBoolType;
					Dest.DWord = botRec->State & STATE_MOUNT;
					return true;
				case Moving:
					Dest.Type = mq::datatypes::pBoolType;
					Dest.DWord = botRec->State & STATE_MOVING;
					return true;
				case Raid:
					Dest.Type = mq::datatypes::pBoolType;
					Dest.DWord = botRec->State & STATE_RAID;
					return true;
				case Sitting:
					Dest.Type = mq::datatypes::pBoolType;
					Dest.DWord = botRec->State & STATE_SIT;
					return true;
				case Standing:
					Dest.Type = mq::datatypes::pBoolType;
					Dest.DWord = botRec->State & STATE_STAND;
					return true;
				case Stunned:
					Dest.Type = mq::datatypes::pBoolType;
					Dest.DWord = botRec->State & STATE_STUN;
					return true;
				case FreeBuffSlots:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->FreeBuff;
					return true;
				case InZone:
					Dest.Type = mq::datatypes::pBoolType;
					Dest.DWord = (inZoned(botRec->ZoneID, botRec->InstID));
					return true;
				case InGroup:
					Dest.Type = mq::datatypes::pBoolType;
					Dest.DWord = (inZoned(botRec->ZoneID, botRec->InstID) && inGroup(botRec->SpawnID));
					return true;
				case Leader:
					Dest.Type = mq::datatypes::pStringType;
					Dest.Ptr = Temps;
					strcpy_s(Temps, botRec->Leader);
					return true;
				case Note:
					Dest.Type = mq::datatypes::pStringType;
					Dest.Ptr = Temps;
					strcpy_s(Temps, botRec->Note);
					return true;
				case Location:
					Dest.Type = mq::datatypes::pStringType;
					Dest.Ptr = Temps;
					strcpy_s(Temps, botRec->Location);
					return true;
				case Heading:
					Dest.Type = mq::datatypes::pStringType;
					Dest.Ptr = Temps;
					strcpy_s(Temps, botRec->Heading);
					return true;
				case Updated:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = clock() - botRec->Updated;
					return true;
					/*
							case Gem:
							  if(!Index[0]) {
								Temps[0]=0;
								for (Cpt=0; Cpt<GEMS_MAX; Cpt++) {
								  sprintf_s(Works,"%d ",botRec->Gem[Cpt]);
								  strcat_s(Temps,Works);
								}
								Dest.Ptr=Temps;
								Dest.Type=pStringType;
								return true;
							  }
							  Cpt=atoi(Index);
							  if(Cpt<GEMS_MAX && Cpt>-1)
								if(Dest.Ptr=GetSpellByID(botRec->Gem[Cpt])) {
								  Dest.Type=pSpellType;
								  return true;
								}
								break;
					*/
				case Buff:
					if (!Index[0]) {
						Temps[0] = '\0';
						for (Cpt = 0; Cpt < BUFF_MAX && botRec->Buff[Cpt]; Cpt++) {
							sprintf_s(Works, "%d ", botRec->Buff[Cpt]);
							strcat_s(Temps, Works);
						}
						Dest.Ptr = Temps;
						Dest.Type = mq::datatypes::pStringType;
						return true;
					}
					Cpt = atoi(Index);
					if (Cpt<BUFF_MAX && Cpt>-1)
						if (Dest.Ptr = GetSpellByID(botRec->Buff[Cpt])) {
							Dest.Type = mq::datatypes::pSpellType;
							return true;
						}
					break;
					
				case Duration:
					if (!Index[0]) {
						Temps[0] = '\0';
						for (Cpt = 0; Cpt < BUFF_MAX && botRec->Duration[Cpt]; Cpt++) {
							sprintf_s(Works, "%0.1f ", botRec->Duration[Cpt] == 28 || botRec->Duration[Cpt] < 0 ? 999999.0f : ((float)botRec->Duration[Cpt]) / 1000.0f);
							strcat_s(Temps, Works);
						}
						Dest.Ptr = Temps;
						Dest.Type = mq::datatypes::pStringType;
						return true;
					}
					Cpt=atoi(Index);
					if (Cpt<BUFF_MAX && Cpt>-1) {
						Dest.Float = 0.0f;
						Dest.Type = mq::datatypes::pFloatType;

						// some permanent buffs report 28 all the time or are negative
						if (botRec->Duration[Cpt] == 28 || botRec->Duration[Cpt] < 0) { 
							Dest.Float = 999999.0f;
						}
						else if (botRec->Duration[Cpt] > 0) {
							Dest.Float = ((float)botRec->Duration[Cpt]) / 1000.0f;
						}
						return true;
					}
				break;
					
				case ShortDuration:
					if (!Index[0]) {
						Temps[0] = '\0';
						for (Cpt = 0; Cpt < SONG_MAX && botRec->ShortDuration[Cpt]; Cpt++) {
							sprintf_s(Works, "%0.1f ", botRec->ShortDuration[Cpt] == 28 || botRec->ShortDuration[Cpt] < 0 ? 999999.0f : ((float)botRec->ShortDuration[Cpt]) / 1000.0f);
							strcat_s(Temps, Works);
						}
						Dest.Ptr = Temps;
						Dest.Type = mq::datatypes::pStringType;
						return true;
					}
					Cpt=atoi(Index);
					if (Cpt<SONG_MAX && Cpt>-1) {
						Dest.Float = 0.0f;
						Dest.Type = mq::datatypes::pFloatType;

						// some permanent buffs report 28 all the time or are negative
						if (botRec->ShortDuration[Cpt] == 28 || botRec->ShortDuration[Cpt] < 0) {
							Dest.Float = 999999.0f;
						}
						else if (botRec->ShortDuration[Cpt] > 0) {
							Dest.Float = ((float)botRec->ShortDuration[Cpt]) / 1000.0f;
						}
						return true;
					}
				break;
				case ShortBuff:
					if (!Index[0]) {
						Temps[0] = 0;
						for (Cpt = 0; Cpt < SONG_MAX && botRec->Song[Cpt]; Cpt++) {
							sprintf_s(Works, "%d ", botRec->Song[Cpt]);
							strcat_s(Temps, Works);
						}
						Dest.Ptr = Temps;
						Dest.Type = mq::datatypes::pStringType;
						return true;
					}
					Cpt = atoi(Index);
					if (Cpt<SONG_MAX && Cpt>-1)
						if (Dest.Ptr = GetSpellByID(botRec->Song[Cpt])) {
							Dest.Type = mq::datatypes::pSpellType;
							return true;
						}
					break;
				case PetBuff:
					if (!Index[0]) {
						Temps[0] = '\0';
						for (Cpt = 0; Cpt < PETS_MAX && botRec->Pets[Cpt]; Cpt++) {
							sprintf_s(Works, "%d ", botRec->Pets[Cpt]);
							strcat_s(Temps, Works);
						}
						Dest.Ptr = Temps;
						Dest.Type = mq::datatypes::pStringType;
						return true;
					}
					Cpt = atoi(Index);
					if (Cpt<PETS_MAX && Cpt>-1)
						if (Dest.Ptr = GetSpellByID(botRec->Pets[Cpt])) {
							Dest.Type = mq::datatypes::pSpellType;
							return true;
						}
					break;
				case TotalAA:
					Dest.Type = mq::datatypes::pIntType;
					Dest.DWord = botRec->TotalAA;
					return true;
				case UsedAA:
					Dest.Type = mq::datatypes::pIntType;
					Dest.DWord = botRec->UsedAA;
					return true;
				case UnusedAA:
					Dest.Type = mq::datatypes::pIntType;
					Dest.DWord = botRec->UnusedAA;
					return true;
				case CombatState:
					Dest.Type = mq::datatypes::pIntType;
					Dest.DWord = botRec->CombatState;
					return true;
				case Stacks:
				{
					Dest.Type = mq::datatypes::pBoolType;
					Dest.DWord = false;
					if (!ISINDEX())
						return true;
					PSPELL tmpSpell = NULL;
					if (ISNUMBER())
						tmpSpell = GetSpellByID(GETNUMBER());
					else
						tmpSpell = GetSpellByName(GETFIRST());
					if (!tmpSpell)
						return true;
					Dest.DWord = true;
					// Check Buffs
					for (Cpt = 0; Cpt < BUFF_MAX; Cpt++) {
						if (botRec->Buff[Cpt]) {
							if (auto buffSpell = GetSpellByID(botRec->Buff[Cpt])) {
								if (!NBBuffStackTest(tmpSpell, buffSpell, TRUE, TRUE) || (buffSpell == tmpSpell)) {
									Dest.DWord = false;
									return true;
								}
							}
						}
					}
					// Check Songs
					for (Cpt = 0; Cpt < SONG_MAX; Cpt++) {
						if (botRec->Song[Cpt]) {
							if (auto buffSpell = GetSpellByID(botRec->Song[Cpt])) {
								if (!IsBardSong(buffSpell) && !((IsSPAEffect(tmpSpell, SPA_CHANGE_FORM) && !tmpSpell->DurationWindow))) {
									if (!NBBuffStackTest(tmpSpell, buffSpell, TRUE, TRUE) || (buffSpell == tmpSpell)) {
										Dest.DWord = false;
										return true;
									}
								}
							}
						}
					}
					return true;
				}
				case StacksPet:
				{
					Dest.Type = mq::datatypes::pBoolType;
					Dest.DWord = false;
					if (!ISINDEX())
						return true;
					PSPELL tmpSpell = NULL;
					if (ISNUMBER())
						tmpSpell = GetSpellByID(GETNUMBER());
					else
						tmpSpell = GetSpellByName(GETFIRST());
					if (!tmpSpell)
						return true;
					Dest.DWord = true;
					// Check Pet Buffs
					for (Cpt = 0; Cpt < PETS_MAX; Cpt++) {
						if (botRec->Pets[Cpt]) {
							if (auto buffSpell = GetSpellByID(botRec->Pets[Cpt])) {
								if (!NBBuffStackTest(tmpSpell, buffSpell, TRUE, FALSE) || (buffSpell == tmpSpell)) {
									Dest.DWord = false;
									return true;
								}
							}
						}
					}
					return true;
				}
				case Detrimentals:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[DETRIMENTALS];
					return true;
				case Counters:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[COUNTERS];
					return true;
				case Cursed:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[CURSED];
					return true;
				case Diseased:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[DISEASED];
					return true;
				case Poisoned:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[POISONED];
					return true;
				case Corrupted:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[CORRUPTED];
					return true;
				case EnduDrain:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[ENDUDRAIN];
					return true;
				case LifeDrain:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[LIFEDRAIN];
					return true;
				case ManaDrain:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[MANADRAIN];
					return true;
				case Blinded:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[BLINDED];
					return true;
				case CastingLevel:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[CASTINGLEVEL];
					return true;
				case Charmed:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[CHARMED];
					return true;
				case Feared:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[FEARED];
					return true;
				case Healing:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[HEALING];
					return true;
				case Invulnerable:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[INVULNERABLE];
					return true;
				case Mesmerized:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[MESMERIZED];
					return true;
				case Rooted:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[ROOTED];
					return true;
				case Silenced:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[SILENCED];
					return true;
				case Slowed:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[SLOWED];
					return true;
				case Snared:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[SNARED];
					return true;
				case SpellCost:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[SPELLCOST];
					return true;
				case SpellSlowed:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[SPELLSLOWED];
					return true;
				case SpellDamage:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[SPELLDAMAGE];
					return true;
				case Trigger:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[TRIGGR];
					return true;
				case Resistance:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[RESISTANCE];
					return true;
				case Detrimental:
					Temps[0] = 0;
					if (botRec->Detrimental[CURSED])       strcat_s(Temps, "Cursed ");
					if (botRec->Detrimental[DISEASED])     strcat_s(Temps, "Diseased ");
					if (botRec->Detrimental[POISONED])     strcat_s(Temps, "Poisoned ");
					if (botRec->Detrimental[ENDUDRAIN])    strcat_s(Temps, "EnduDrain ");
					if (botRec->Detrimental[LIFEDRAIN])    strcat_s(Temps, "LifeDrain ");
					if (botRec->Detrimental[MANADRAIN])    strcat_s(Temps, "ManaDrain ");
					if (botRec->Detrimental[BLINDED])      strcat_s(Temps, "Blinded ");
					if (botRec->Detrimental[CASTINGLEVEL]) strcat_s(Temps, "CastingLevel ");
					if (botRec->Detrimental[CHARMED])      strcat_s(Temps, "Charmed ");
					if (botRec->Detrimental[FEARED])       strcat_s(Temps, "Feared ");
					if (botRec->Detrimental[HEALING])      strcat_s(Temps, "Healing ");
					if (botRec->Detrimental[INVULNERABLE]) strcat_s(Temps, "Invulnerable ");
					if (botRec->Detrimental[MESMERIZED])   strcat_s(Temps, "Mesmerized ");
					if (botRec->Detrimental[ROOTED])       strcat_s(Temps, "Rooted ");
					if (botRec->Detrimental[SILENCED])     strcat_s(Temps, "Silenced ");
					if (botRec->Detrimental[SLOWED])       strcat_s(Temps, "Slowed ");
					if (botRec->Detrimental[SNARED])       strcat_s(Temps, "Snared ");
					if (botRec->Detrimental[SPELLCOST])    strcat_s(Temps, "SpellCost ");
					if (botRec->Detrimental[SPELLDAMAGE])  strcat_s(Temps, "SpellDamage ");
					if (botRec->Detrimental[SPELLSLOWED])  strcat_s(Temps, "SpellSlowed ");
					if (botRec->Detrimental[TRIGGR])       strcat_s(Temps, "Trigger ");
					if (botRec->Detrimental[CORRUPTED])    strcat_s(Temps, "Corrupted ");
					if (botRec->Detrimental[RESISTANCE])   strcat_s(Temps, "Resistance ");
					if (size_t len = strlen(Temps)) Temps[--len] = 0;
					Dest.Type = mq::datatypes::pStringType;
					Dest.Ptr = Temps;
					return true;
				case NoCure:
					Dest.Type = mq::datatypes::pIntType;
					Dest.Int = botRec->Detrimental[NOCURE];
					return true;

				}
			}
		}
		strcpy_s(Temps, "NULL");
		Dest.Type = mq::datatypes::pStringType;
		Dest.Ptr = Temps;
		return true;
	}

	bool ToString(MQVarPtr VarPtr, PCHAR Destination) {
		strcpy_s(Destination, MAX_STRING, "TRUE");
		return true;
	}

	~MQ2NetBotsType() {
	}
};

bool dataNetBots(const char* szIndex, MQTypeVar& Ret) {
	Ret.Type = pNetBotsType;
	if (szIndex != nullptr && szIndex[0] != '\0')
	{
		auto f = NetMap.find(szIndex);
		if (f != NetMap.end())
		{
			Ret.Set(f->second);
			return true;
		}
		return false;
	}

	return true;
}

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=//

void Command(PSPAWNINFO pChar, PCHAR Cmd) {
	char Tmp[MAX_STRING]; BYTE Parm = 1;
	char Var[MAX_STRING];
	char Set[MAX_STRING];
	do {
		GetArg(Tmp, Cmd, Parm++);
		GetArg(Var, Tmp, 1, FALSE, FALSE, FALSE, '=');
		GetArg(Set, Tmp, 2, FALSE, FALSE, FALSE, '=');
		if (!_strnicmp(Tmp, "on", 2) || !_strnicmp(Tmp, "off", 3)) {
			NetStat = _strnicmp(Tmp, "off", 3);
			WritePrivateProfileString(PLUGIN_NAME, "Stat", NetStat ? "1" : "0", INIFileName);
		}
		else if (!_strnicmp(Var, "stat", 4) || !_strnicmp(Var, "plugin", 6)) {
			NetStat = _strnicmp(Set, "off", 3);
			WritePrivateProfileString(PLUGIN_NAME, "Stat", NetStat ? "1" : "0", INIFileName);

		}
		else if (!_strnicmp(Var, "grab", 4)) {
			NetGrab = _strnicmp(Set, "off", 3);
			WritePrivateProfileString(PLUGIN_NAME, "Grab", NetGrab ? "1" : "0", INIFileName);
		}
		else if (!_strnicmp(Var, "send", 4)) {
			NetSend = _strnicmp(Set, "off", 3);
			WritePrivateProfileString(PLUGIN_NAME, "Send", NetSend ? "1" : "0", INIFileName);
		}
	} while (strlen(Tmp));
	WriteChatf("%s:: (%s) Grab (%s) Send (%s).", PLUGIN_NAME, NetStat ? "\agon\ax" : "\aroff\ax", NetGrab ? "\agon\ax" : "\aroff\ax", NetSend ? "\agon\ax" : "\aroff\ax");
}

void CommandNote(PSPAWNINFO pChar, PCHAR Cmd) {
	strcpy_s(NetNote, Cmd);
}

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=//

PLUGIN_API VOID OnBeginZone(VOID) {
#if    DEBUGGING>0
	DebugSpewAlways("%s->OnBeginZone()", PLUGIN_NAME);
#endif DEBUGGING
	ZeroMemory(sTimers, sizeof(sTimers));
	if (NetStat && NetSend && EQBCConnected()) EQBCBroadCast("[NB]|Z=:>|[NB]");
}

PLUGIN_API VOID OnNetBotEVENT(PCHAR Msg) {
#if    DEBUGGING>0
	DebugSpewAlways("%s->OnNetBotEVENT(%s)", PLUGIN_NAME, Msg);
#endif DEBUGGING
	if (!strncmp(Msg, "NBQUIT=", 7))      BotQuit(&Msg[7]);
	else if (!strncmp(Msg, "NBJOIN=", 7)) ZeroMemory(sTimers, sizeof(sTimers));
	else if (!strncmp(Msg, "NBEXIT", 6))  NetMap.clear();
}

PLUGIN_API VOID OnNetBotMSG(PCHAR Name, PCHAR Msg) {
	if (NetStat && NetGrab && !strncmp(Msg, "[NB]|", 5) && GetCharInfo() && GetCharInfo()->pSpawn && strcmp(GetCharInfo()->Name, Name)) {
#if    DEBUGGING>1
		DebugSpewAlways("%s->OnNetBotMSG(From:[%s] Msg[%s])", PLUGIN_NAME, Name, Msg);
#endif DEBUGGING
		CHAR szCmd[MAX_STRING] = { 0 };
		strcpy_s(szCmd, Msg);
		if (CurBot = BotLoad(Name).get()) {
			Packet.Feed(szCmd);
			CurBot->Updated = clock();
			CurBot = 0;
		}
	}
}

PLUGIN_API VOID OnPulse(VOID) {
	if (NetStat && NetSend && gbInZone && (long)clock() > NetLast) {
		NetLast = (long)clock() + NETTICK;
		if (EQBCConnected() && GetCharInfo() && GetCharInfo()->pSpawn && GetPcProfile()) BroadCast();
	}
}

PLUGIN_API VOID SetGameState(DWORD GameState) {
#if    DEBUGGING>0
	DebugSpewAlways("%s->SetGameState(%d)", PLUGIN_NAME, GameState);
#endif DEBUGGING
	if (GameState == GAMESTATE_INGAME) {
		if (!NetInit) {
#if    DEBUGGING>0
			DebugSpewAlways("%s->SetGameState(%d)->Loading", PLUGIN_NAME, GameState);
#endif DEBUGGING
			sprintf_s(INIFileName, "%s\\%s_%s.ini", gPathConfig, GetServerShortName(), pLocalPC->Name);
			NetStat = GetPrivateProfileInt(PLUGIN_NAME, "Stat", 0, INIFileName);
			NetGrab = GetPrivateProfileInt(PLUGIN_NAME, "Grab", 0, INIFileName);
			NetSend = GetPrivateProfileInt(PLUGIN_NAME, "Send", 0, INIFileName);
			NetInit = true;
		}
	}
	else if (GameState != GAMESTATE_LOGGINGIN) {
		if (NetInit) {
#if    DEBUGGING>0
			DebugSpewAlways("%s->SetGameState(%d)->Flushing", PLUGIN_NAME, GameState);
#endif DEBUGGING
			NetStat = false;
			NetGrab = false;
			NetSend = false;
			NetInit = false;
		}
	}
}

PLUGIN_API VOID InitializePlugin(VOID) {
#if    DEBUGGING>0
	DebugSpewAlways("%s->ShutdownPlugin()", PLUGIN_NAME);
#endif DEBUGGING
	Packet.Reset();
	NetMap.clear();
	Packet.AddEvent("#*#[NB]#*#|Z=#1#:#2#>#3#|#*#[NB]", ParseInfo, (void *)3);
	Packet.AddEvent("#*#[NB]#*#|L=#4#:#5#|#*#[NB]", ParseInfo, (void *)5);
	Packet.AddEvent("#*#[NB]#*#|H=#6#/#7#|#*#[NB]", ParseInfo, (void *)7);
	Packet.AddEvent("#*#[NB]#*#|E=#8#/#9#|#*#[NB]", ParseInfo, (void *)9);
	Packet.AddEvent("#*#[NB]#*#|M=#10#/#11#|#*#[NB]", ParseInfo, (void *)11);
	Packet.AddEvent("#*#[NB]#*#|P=#12#:#13#|#*#[NB]", ParseInfo, (void *)13);
	Packet.AddEvent("#*#[NB]#*#|T=#14#:#15#|#*#[NB]", ParseInfo, (void *)15);
	Packet.AddEvent("#*#[NB]#*#|C=#16#|#*#[NB]", ParseInfo, (void *)16);
	Packet.AddEvent("#*#[NB]#*#|Y=#17#|#*#[NB]", ParseInfo, (void *)17);
#if HAS_LEADERSHIP_EXPERIENCE
	Packet.AddEvent("#*#[NB]#*#|X=#18#:#19#:#20#|#*#[NB]", ParseInfo, (void *)20);
#else
	Packet.AddEvent("#*#[NB]#*#|X=#18#:#19#|#*#[NB]", ParseInfo, (void *)19);
#endif
	Packet.AddEvent("#*#[NB]#*#|F=#21#:|#*#[NB]", ParseInfo, (void *)21);
	Packet.AddEvent("#*#[NB]#*#|N=#22#|#*#[NB]", ParseInfo, (void *)22);
	Packet.AddEvent("#*#[NB]#*#|G=#30#|#*#[NB]", ParseInfo, (void *)30);
	Packet.AddEvent("#*#[NB]#*#|B=#31#|#*#[NB]", ParseInfo, (void *)31);
	Packet.AddEvent("#*#[NB]#*#|S=#32#|#*#[NB]", ParseInfo, (void *)32);
	Packet.AddEvent("#*#[NB]#*#|W=#33#|#*#[NB]", ParseInfo, (void *)33);
	Packet.AddEvent("#*#[NB]#*#|D=#34#|#*#[NB]", ParseInfo, (void *)34);
	Packet.AddEvent("#*#[NB]#*#|A=#35#:#36#:#37#|#*#[NB]", ParseInfo, (void *)37);
	Packet.AddEvent("#*#[NB]#*#|O=#38#|#*#[NB]", ParseInfo, (void *)38);
	Packet.AddEvent("#*#[NB]#*#|U=#39#|#*#[NB]", ParseInfo, (void *)39);
	Packet.AddEvent("#*#[NB]#*#|R=#40#|#*#[NB]", ParseInfo, (void *)40);
	Packet.AddEvent("#*#[NB]#*#|@=#89#|#*#[NB]", ParseInfo, (void *)89);
	Packet.AddEvent("#*#[NB]#*#|$=#90#|#*#[NB]", ParseInfo, (void *)90);
	Packet.AddEvent("#*#[NB]#*#|F=#155#|#*#[NB]", ParseInfo, (void *)155);

	ZeroMemory(sTimers, sizeof(sTimers));
	ZeroMemory(sBuffer, sizeof(sBuffer));
	ZeroMemory(wBuffer, sizeof(wBuffer));
	ZeroMemory(wChange, sizeof(wChange));
	ZeroMemory(wUpdate, sizeof(wUpdate));
	pNetBotsType = new MQ2NetBotsType;
	NetNote[0] = '\0';
	AddMQ2Data("NetBots", dataNetBots);
	AddCommand("/netbots", Command);
	AddCommand("/netnote", CommandNote);
}

PLUGIN_API VOID ShutdownPlugin(VOID) {
#if    DEBUGGING>0
	DebugSpewAlways("%s->ShutdownPlugin()", PLUGIN_NAME);
#endif DEBUGGING
	RemoveCommand("/netbots");
	RemoveCommand("/netnote");
	RemoveMQ2Data("NetBots");
	delete pNetBotsType;
}