#include "Hooks_Other.h"
#include "GameAPI.h"
#include "Commands_UI.h"
#include "ScriptUtils.h"
#include "SafeWrite.h"
#include "Commands_UI.h"
#include "Hooks_Gameplay.h"
#include "LambdaManager.h"

#if RUNTIME

namespace OtherHooks
{
	thread_local TESObjectREFR* g_lastScriptOwnerRef = nullptr;
	__declspec(naked) void TilesDestroyedHook()
	{
		__asm
		{
			mov g_tilesDestroyed, 1
			// original asm
			pop ecx
			mov esp, ebp
			pop ebp
			ret
		}
	}

	__declspec(naked) void TilesCreatedHook()
	{
		// Eddoursol reported a problem where stagnant deleted tiles got cached
		__asm
		{
			mov g_tilesDestroyed, 1
			pop ecx
			mov esp, ebp
			pop ebp
			ret
		}
	}

	void CleanUpNVSEVars(ScriptEventList* eventList)
	{
		// Prevent leakage of variables that's ScriptEventList gets deleted (e.g. in Effect scripts)
		auto* scriptVars = g_nvseVarGarbageCollectionMap.GetPtr(eventList);
		if (!scriptVars)
			return;
		ScopedLock lock(g_gcCriticalSection);
		auto* node = eventList->m_vars;
		while (node)
		{
			if (node->var)
			{
				const auto id = node->var->id;
				const auto type = scriptVars->Get(id);
				switch (type)
				{
				case NVSEVarType::kVarType_String:
					// make sure not to delete string vars from lambdas as the parent script owns them; not necessary for arrays since the have ref counting
					if (!LambdaManager::IsScriptLambda(eventList->m_script))
						g_StringMap.MarkTemporary(static_cast<int>(node->var->data), true);
					break;
				case NVSEVarType::kVarType_Array:
					//g_ArrayMap.MarkTemporary(static_cast<int>(node->var->data), true);
					g_ArrayMap.RemoveReference(&node->var->data, eventList->m_script->GetModIndex());
					break;
				default:
					break;
				}
			}
			node = node->next;
		}
		g_nvseVarGarbageCollectionMap.Erase(eventList);
	}

	void DeleteEventList(ScriptEventList* eventList)
	{
		LambdaManager::MarkParentAsDeleted(eventList); // deletes if exists
		CleanUpNVSEVars(eventList);
		ThisStdCall(0x5A8BC0, eventList);
		GameHeapFree(eventList);
	}
	
	ScriptEventList* __fastcall ScriptEventListsDestroyedHook(ScriptEventList *eventList, int EDX, bool doFree)
	{
		DeleteEventList(eventList);
		return eventList;
	}

	void __fastcall SaveLastScriptOwnerRef(TESObjectREFR* ref)
	{
		g_lastScriptOwnerRef = ref;
#if _DEBUG
		//if (ref && !LookupFormByID(ref->refID))
		//	DebugBreak();
#endif
	}

	__declspec(naked) void SaveScriptOwnerRefHook()
	{
		const static auto hookedCall = 0x702FC0;
		const static auto retnAddr = 0x5E0D56;
		__asm
		{
			mov ecx, [ebp+0xC]
			call SaveLastScriptOwnerRef
			call hookedCall
			jmp retnAddr
		}
	}

	__declspec(naked) void SaveScriptOwnerRefHook2()
	{
		const static auto hookedCall = 0x4013E0;
		const static auto retnAddr = 0x5E119F;
		__asm
		{
			push ecx
			mov ecx, [ebp+0xC]
			call SaveLastScriptOwnerRef
			pop ecx
			call hookedCall
			jmp retnAddr
		}
	}

	void Hooks_Other_Init()
	{
		WriteRelJump(0x9FF5FB, UInt32(TilesDestroyedHook));
		WriteRelJump(0x709910, UInt32(TilesCreatedHook));
		WriteRelJump(0x41AF70, UInt32(ScriptEventListsDestroyedHook));
		
		WriteRelJump(0x5E0D51, UInt32(SaveScriptOwnerRefHook));
		WriteRelJump(0x5E119A, UInt32(SaveScriptOwnerRefHook2));
	}
}
#endif