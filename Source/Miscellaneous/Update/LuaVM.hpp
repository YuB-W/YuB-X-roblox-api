/* Updated for version-e00a4ca39fb04359 */

#pragma once

#include <Windows.h>
#include <cmath>

#include "Core/Structure.hpp"

#define REBASE(x) ((x) + (uintptr_t)GetModuleHandle(nullptr))

// Separators
#define LUAU_COMMA_SEP ,
#define LUAU_SEMICOLON_SEP ;

#define LUAU_SHUFFLE3(sep, a1, a2, a3) a1 sep a3 sep a2
#define LUAU_SHUFFLE4(sep, a1, a2, a3, a4) a3 sep a2 sep a4 sep a1
#define LUAU_SHUFFLE5(sep, a1, a2, a3, a4, a5) a1 sep a2 sep a5 sep a4 sep a3
#define LUAU_SHUFFLE6(sep, a1, a2, a3, a4, a5, a6) a2 sep a4 sep a3 sep a1 sep a5 sep a6
#define LUAU_SHUFFLE7(sep, a1, a2, a3, a4, a5, a6, a7) a6 sep a2 sep a7 sep a3 sep a1 sep a4 sep a5
#define LUAU_SHUFFLE8(sep, a1, a2, a3, a4, a5, a6, a7, a8) a5 sep a4 sep a8 sep a3 sep a2 sep a1 sep a7 sep a6
#define LUAU_SHUFFLE9(sep, a1, a2, a3, a4, a5, a6, a7, a8, a9) a9 sep a7 sep a3 sep a5 sep a1 sep a6 sep a2 sep a8 sep a4

#define PROTO_MEMBER2_ENC    vmval4 
#define PROTO_DEBUGISN_ENC   vmval3 
#define PROTO_TYPEINFO_ENC   vmval2 
#define PROTO_DEBUGNAME_ENC  vmval1 
#define LSTATE_STACKSIZE_ENC vmval2 
#define CLOSURE_CONT_ENC     vmval3
#define CLOSURE_DEBUGNAME_ENC vmval4
#define TSTRING_HASH_ENC     vmval1 
#define UDATA_META_ENC       vmval3 
#define PROTO_MEMBER1_ENC   vmval0
#define CLOSURE_FUNC_ENC    vmval0 
#define LSTATE_GLOBAL_ENC   vmval0
#define TSTRING_LEN_ENC     vmval0 
#define TABLE_META_ENC      vmval0 
#define TABLE_MEMBER_ENC    vmval0 
#define GSTATE_TTNAME_ENC   vmval0
#define GSTATE_TMNAME_ENC   vmval0

namespace Update {
    namespace LuaVM {
		const uintptr_t LuaH_DummyNode = REBASE(0x46BB828);
		const uintptr_t LuaO_NilObject = REBASE(0x46BBE08);
		const uintptr_t Luau_Execute = REBASE(0x2773CE0);
    }
}

// updated
