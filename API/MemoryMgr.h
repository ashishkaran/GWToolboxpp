#pragma once

#include <Windows.h>
#include <stdio.h>

namespace GWAPI{

	struct MemoryMgr{

		template <typename T>
		class gw_array {
		protected:
			T* m_array;
			DWORD m_allocatedsize;
			DWORD m_currentsize;
		public:
			T GetIndex(DWORD index)
			{
				if (index > m_currentsize || index < 0) throw 1;
				return m_array[index];
			}

			T operator[](DWORD index)
			{
				return GetIndex(index);
			}

			DWORD size() const { return m_currentsize; }
		};

		// Agent shit
		static BYTE* agArrayPtr;
		static BYTE* PlayerAgentIDPtr;
		static BYTE* TargetAgentIDPtr;

		// Map Id
		static BYTE* MapIDPtr;


		// Send Packet Shit
		static BYTE* CtoGSObjectPtr;
		static BYTE* CtoGSSendFunction;

		// Base ptr to get context ptr for gameworld
		static BYTE* BasePointerLocation;

		// Disable/Enable Rendering
		static BYTE* RenderLoopLocation;

		// For gamethread calls
		static BYTE* GameLoopLocation;
		static BYTE* GameLoopReturn;
		static BYTE* GameLoopRestore;

		// For writing PM's in chat.
		static BYTE* WriteChatFunction;

		// Skill timer for effects.
		static BYTE* SkillTimerPtr;

		// Skill structure array.

		static BYTE* SkillArray;
		static BYTE* UseSkillFunction;

		// Basics
		static bool Scan();
		static void *Detour(BYTE *src, const BYTE *dst, const int len, BYTE** restore = NULL);
		static void Retour(BYTE *src, BYTE *restore, const int len);
		template <typename T> static T ReadPtrChain(DWORD pBase,DWORD AmountofOffsets,...)
		{
			va_list vl;

			va_start(vl, AmountofOffsets);
			while (AmountofOffsets--)
			{
				pBase = (*(DWORD*)pBase) + va_arg(vl, DWORD);
			}
			va_end(vl);

			return (T)pBase;
		}



		// Memory Reads.
		static DWORD GetContextPtr(){ return (*(DWORD*)BasePointerLocation) + 0x18; }
		static DWORD GetSkillTimer(){ return *(DWORD*)SkillTimerPtr; }
	};
}