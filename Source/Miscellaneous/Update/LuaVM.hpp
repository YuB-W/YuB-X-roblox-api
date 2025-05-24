/* Updated for version-e00a4ca39fb04359 */

#pragma once

#include <Windows.h>
#include <cmath>

#include "Core/Structure.hpp"

#define REBASE(x) ((x) + (uintptr_t)GetModuleHandle(nullptr))

// Separators
#define LUAU_COMMA_SEP ,
#define LUAU_SEMICOLON_SEP ;

// Shuffle macros
#define LUAU_SHUFFLE3(s, a1, a2, a3) a1 s a3 s a2
#define LUAU_SHUFFLE4(s, a1, a2, a3, a4) a4 s a3 s a1 s a2
#define LUAU_SHUFFLE5(s, a1, a2, a3, a4, a5) a4 s a3 s a2 s a5 s a1
#define LUAU_SHUFFLE6(s, a1, a2, a3, a4, a5, a6) a2 s a6 s a1 s a5 s a4 s a3
#define LUAU_SHUFFLE7(s, a1, a2, a3, a4, a5, a6, a7) a2 s a3 s a4 s a1 s a5 s a6 s a7
#define LUAU_SHUFFLE8(s, a1, a2, a3, a4, a5, a6, a7, a8) a1 s a8 s a4 s a6 s a2 s a7 s a5 s a3
#define LUAU_SHUFFLE9(s, a1, a2, a3, a4, a5, a6, a7, a8, a9) a1 s a4 s a5 s a7 s a6 s a8 s a3 s a2 s a9
// VMValue encodings

#define PROTO_MEMBER1_ENC vmval0
#define PROTO_MEMBER2_ENC vmval1
#define PROTO_DEBUGISN_ENC vmval2
#define PROTO_TYPEINFO_ENC vmval3
#define PROTO_DEBUGNAME_ENC vmval4

#define LSTATE_STACKSIZE_ENC vmval2
#define LSTATE_GLOBAL_ENC vmval0

#define CLOSURE_FUNC_ENC vmval0
#define CLOSURE_CONT_ENC vmval2
#define CLOSURE_DEBUGNAME_ENC vmval1

#define TABLE_MEMBER_ENC vmval0
#define TABLE_META_ENC vmval0

#define UDATA_META_ENC vmval3

#define TSTRING_HASH_ENC vmval1
#define TSTRING_LEN_ENC vmval0

#define GSTATE_TTNAME_ENC vmval0
#define GSTATE_TMNAME_ENC vmval0


namespace Update {
    namespace LuaVM {
        const uintptr_t LuaO_NilObject = REBASE(0x47AAE88);
        const uintptr_t LuaH_DummyNode = REBASE(0x47AA5B8);
        const uintptr_t Luau_Execute = REBASE(0x27F5E40);
    }
}

// updated
