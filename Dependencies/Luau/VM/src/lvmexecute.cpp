// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#include "lvm.h"

#include "lstate.h"
#include "ltable.h"
#include "lfunc.h"
#include "lstring.h"
#include "lgc.h"
#include "lmem.h"
#include "ldebug.h"
#include "ldo.h"
#include "lbuiltins.h"
#include "lnumutils.h"
#include "lbytecode.h"

#include <string.h>

LUAU_FASTFLAG(LuauCurrentLineBounds)

// Disable c99-designator to avoid the warning in CGOTO dispatch table
#ifdef __clang__
#if __has_warning("-Wc99-designator")
#pragma clang diagnostic ignored "-Wc99-designator"
#endif
#endif

// When working with VM code, pay attention to these rules for correctness:
// 1. Many external Lua functions can fail; for them to fail and be able to generate a proper stack, we need to copy pc to L->ci->savedpc before the
// call
// 2. Many external Lua functions can reallocate the stack. This invalidates stack pointers in VM C stack frame, most importantly base, but also
// ra/rb/rc!
// 3. VM_PROTECT macro saves savedpc and restores base for you; most external calls need to be wrapped into that. However, it does NOT restore
// ra/rb/rc!
// 4. When copying an object to any existing object as a field, generally speaking you need to call luaC_barrier! Be careful with all setobj calls
// 5. To make 4 easier to follow, please use setobj2s for copies to stack, setobj2t for writes to tables, and setobj for other copies.
// 6. You can define HARDSTACKTESTS in llimits.h which will aggressively realloc stack; with address sanitizer this should be effective at finding
// stack corruption bugs
// 7. Many external Lua functions can call GC! GC will *not* traverse pointers to new objects that aren't reachable from Lua root. Be careful when
// creating new Lua objects, store them to stack soon.

// When calling luau_callTM, we usually push the arguments to the top of the stack.
// This is safe to do for complicated reasons:
// - stack guarantees EXTRA_STACK room beyond stack_last (see luaD_reallocstack)
// - stack reallocation copies values past stack_last

// All external function calls that can cause stack realloc or Lua calls have to be wrapped in VM_PROTECT
// This makes sure that we save the pc (in case the Lua call needs to generate a backtrace) before the call,
// and restores the stack pointer after in case stack gets reallocated
// Should only be used on the slow paths.
#define VM_PROTECT(x) \
    { \
        L->ci->savedpc = pc; \
        { \
            x; \
        }; \
        base = L->base; \
    }

// Some external functions can cause an error, but never reallocate the stack; for these, VM_PROTECT_PC() is
// a cheaper version of VM_PROTECT that can be called before the external call.
#define VM_PROTECT_PC() L->ci->savedpc = pc

#define VM_REG(i) (LUAU_ASSERT(unsigned(i) < unsigned(L->top - base)), &base[i])
#define VM_KV(i) (LUAU_ASSERT(unsigned(i) < unsigned(cl->l.p->sizek)), &k[i])
#define VM_UV(i) (LUAU_ASSERT(unsigned(i) < unsigned(cl->nupvalues)), &cl->l.uprefs[i])

#define VM_PATCH_C(pc, slot) *const_cast<Instruction*>(pc) = ((uint8_t(slot) << 24) | (0x00ffffffu & *(pc)))
#define VM_PATCH_E(pc, slot) *const_cast<Instruction*>(pc) = ((uint32_t(slot) << 8) | (0x000000ffu & *(pc)))

#define VM_INTERRUPT() \
    { \
        void (*interrupt)(lua_State*, int) = L->global->cb.interrupt; \
        if (LUAU_UNLIKELY(!!interrupt)) \
        { /* the interrupt hook is called right before we advance pc */ \
            VM_PROTECT(L->ci->savedpc++; interrupt(L, -1)); \
            if (L->status != 0) \
            { \
                L->ci->savedpc--; \
                goto exit; \
            } \
        } \
    }

#define VM_DISPATCH_OP(op) &&CASE_##op

#define VM_DISPATCH_TABLE() \
    VM_DISPATCH_OP(LOP_NOP), VM_DISPATCH_OP(LOP_BREAK), VM_DISPATCH_OP(LOP_LOADNIL), VM_DISPATCH_OP(LOP_LOADB), VM_DISPATCH_OP(LOP_LOADN), \
        VM_DISPATCH_OP(LOP_LOADK), VM_DISPATCH_OP(LOP_MOVE), VM_DISPATCH_OP(LOP_GETGLOBAL), VM_DISPATCH_OP(LOP_SETGLOBAL), \
        VM_DISPATCH_OP(LOP_GETUPVAL), VM_DISPATCH_OP(LOP_SETUPVAL), VM_DISPATCH_OP(LOP_CLOSEUPVALS), VM_DISPATCH_OP(LOP_GETIMPORT), \
        VM_DISPATCH_OP(LOP_GETTABLE), VM_DISPATCH_OP(LOP_SETTABLE), VM_DISPATCH_OP(LOP_GETTABLEKS), VM_DISPATCH_OP(LOP_SETTABLEKS), \
        VM_DISPATCH_OP(LOP_GETTABLEN), VM_DISPATCH_OP(LOP_SETTABLEN), VM_DISPATCH_OP(LOP_NEWCLOSURE), VM_DISPATCH_OP(LOP_NAMECALL), \
        VM_DISPATCH_OP(LOP_CALL), VM_DISPATCH_OP(LOP_RETURN), VM_DISPATCH_OP(LOP_JUMP), VM_DISPATCH_OP(LOP_JUMPBACK), VM_DISPATCH_OP(LOP_JUMPIF), \
        VM_DISPATCH_OP(LOP_JUMPIFNOT), VM_DISPATCH_OP(LOP_JUMPIFEQ), VM_DISPATCH_OP(LOP_JUMPIFLE), VM_DISPATCH_OP(LOP_JUMPIFLT), \
        VM_DISPATCH_OP(LOP_JUMPIFNOTEQ), VM_DISPATCH_OP(LOP_JUMPIFNOTLE), VM_DISPATCH_OP(LOP_JUMPIFNOTLT), VM_DISPATCH_OP(LOP_ADD), \
        VM_DISPATCH_OP(LOP_SUB), VM_DISPATCH_OP(LOP_MUL), VM_DISPATCH_OP(LOP_DIV), VM_DISPATCH_OP(LOP_MOD), VM_DISPATCH_OP(LOP_POW), \
        VM_DISPATCH_OP(LOP_ADDK), VM_DISPATCH_OP(LOP_SUBK), VM_DISPATCH_OP(LOP_MULK), VM_DISPATCH_OP(LOP_DIVK), VM_DISPATCH_OP(LOP_MODK), \
        VM_DISPATCH_OP(LOP_POWK), VM_DISPATCH_OP(LOP_AND), VM_DISPATCH_OP(LOP_OR), VM_DISPATCH_OP(LOP_ANDK), VM_DISPATCH_OP(LOP_ORK), \
        VM_DISPATCH_OP(LOP_CONCAT), VM_DISPATCH_OP(LOP_NOT), VM_DISPATCH_OP(LOP_MINUS), VM_DISPATCH_OP(LOP_LENGTH), VM_DISPATCH_OP(LOP_NEWTABLE), \
        VM_DISPATCH_OP(LOP_DUPTABLE), VM_DISPATCH_OP(LOP_SETLIST), VM_DISPATCH_OP(LOP_FORNPREP), VM_DISPATCH_OP(LOP_FORNLOOP), \
        VM_DISPATCH_OP(LOP_FORGLOOP), VM_DISPATCH_OP(LOP_FORGPREP_INEXT), VM_DISPATCH_OP(LOP_FASTCALL3), VM_DISPATCH_OP(LOP_FORGPREP_NEXT), \
        VM_DISPATCH_OP(LOP_NATIVECALL), VM_DISPATCH_OP(LOP_GETVARARGS), VM_DISPATCH_OP(LOP_DUPCLOSURE), VM_DISPATCH_OP(LOP_PREPVARARGS), \
        VM_DISPATCH_OP(LOP_LOADKX), VM_DISPATCH_OP(LOP_JUMPX), VM_DISPATCH_OP(LOP_FASTCALL), VM_DISPATCH_OP(LOP_COVERAGE), \
        VM_DISPATCH_OP(LOP_CAPTURE), VM_DISPATCH_OP(LOP_SUBRK), VM_DISPATCH_OP(LOP_DIVRK), VM_DISPATCH_OP(LOP_FASTCALL1), \
        VM_DISPATCH_OP(LOP_FASTCALL2), VM_DISPATCH_OP(LOP_FASTCALL2K), VM_DISPATCH_OP(LOP_FORGPREP), VM_DISPATCH_OP(LOP_JUMPXEQKNIL), \
        VM_DISPATCH_OP(LOP_JUMPXEQKB), VM_DISPATCH_OP(LOP_JUMPXEQKN), VM_DISPATCH_OP(LOP_JUMPXEQKS), VM_DISPATCH_OP(LOP_IDIV), \
        VM_DISPATCH_OP(LOP_IDIVK),

#if defined(__GNUC__) || defined(__clang__)
#define VM_USE_CGOTO 1
#else
#define VM_USE_CGOTO 0
#endif

/**
 * These macros help dispatching Luau opcodes using either case
 * statements or computed goto.
 * VM_CASE(op) Generates either a case statement or a label
 * VM_NEXT() fetch a byte and dispatch or jump to the beginning of the switch statement
 * VM_CONTINUE() Use an opcode override to dispatch with computed goto or
 * switch statement to skip a LOP_BREAK instruction.
 */
#if VM_USE_CGOTO
#define VM_CASE(op) CASE_##op:
#define VM_NEXT() goto*(SingleStep ? &&dispatch : kDispatchTable[LUAU_INSN_OP(*pc)])
#define VM_CONTINUE(op) goto* kDispatchTable[uint8_t(op)]
#else
#define VM_CASE(op) case op:
#define VM_NEXT() goto dispatch
#define VM_CONTINUE(op) \
    dispatchOp = uint8_t(op); \
    goto dispatchContinue
#endif

// Does VM support native execution via ExecutionCallbacks? We mostly assume it does but keep the define to make it easy to quantify the cost.
#define VM_HAS_NATIVE 1

LUAU_NOINLINE void luau_callhook(lua_State* L, lua_Hook hook, void* userdata)
{
    ptrdiff_t base = savestack(L, L->base);
    ptrdiff_t top = savestack(L, L->top);
    ptrdiff_t ci_top = savestack(L, L->ci->top);
    int status = L->status;

    // if the hook is called externally on a paused thread, we need to make sure the paused thread can emit Lua calls
    if (status == LUA_YIELD || status == LUA_BREAK)
    {
        L->status = 0;
        L->base = L->ci->base;
    }

    if (FFlag::LuauCurrentLineBounds)
    {
        Closure* cl = clvalue(L->ci->func);

        // note: the pc expectations of the hook are matching the general "pc points to next instruction"
        // however, for the hook to be able to continue execution from the same point, this is called with savedpc at the *current* instruction
        // this needs to be called before luaD_checkstack in case it fails to reallocate stack
        const Instruction* oldsavedpc = L->ci->savedpc;

        if (L->ci->savedpc && L->ci->savedpc != cl->l.p->code + cl->l.p->sizecode)
            L->ci->savedpc++;

        luaD_checkstack(L, LUA_MINSTACK); // ensure minimum stack size
        L->ci->top = L->top + LUA_MINSTACK;
        LUAU_ASSERT(L->ci->top <= L->stack_last);

        lua_Debug ar;
        ar.currentline = cl->isC ? -1 : luaG_getline(cl->l.p, pcRel(L->ci->savedpc, cl->l.p));
        ar.userdata = userdata;

        hook(L, &ar);

        L->ci->savedpc = oldsavedpc;
    }
    else
    {
        // note: the pc expectations of the hook are matching the general "pc points to next instruction"
        // however, for the hook to be able to continue execution from the same point, this is called with savedpc at the *current* instruction
        // this needs to be called before luaD_checkstack in case it fails to reallocate stack
        if (L->ci->savedpc)
            L->ci->savedpc++;

        luaD_checkstack(L, LUA_MINSTACK); // ensure minimum stack size
        L->ci->top = L->top + LUA_MINSTACK;
        LUAU_ASSERT(L->ci->top <= L->stack_last);

        Closure* cl = clvalue(L->ci->func);

        lua_Debug ar;
        ar.currentline = cl->isC ? -1 : luaG_getline(cl->l.p, pcRel(L->ci->savedpc, cl->l.p));
        ar.userdata = userdata;

        hook(L, &ar);

        if (L->ci->savedpc)
            L->ci->savedpc--;
    }

    L->ci->top = restorestack(L, ci_top);
    L->top = restorestack(L, top);

    // note that we only restore the paused state if the hook hasn't yielded by itself
    if (status == LUA_YIELD && L->status != LUA_YIELD)
    {
        L->status = LUA_YIELD;
        L->base = restorestack(L, base);
    }
    else if (status == LUA_BREAK)
    {
        LUAU_ASSERT(L->status != LUA_BREAK); // hook shouldn't break again

        L->status = LUA_BREAK;
        L->base = restorestack(L, base);
    }
}

inline bool luau_skipstep(uint8_t op)
{
    return op == LOP_PREPVARARGS || op == LOP_BREAK;
}

template<bool SingleStep>
static void luau_execute(lua_State* L)
{
#if VM_USE_CGOTO
    static const void* kDispatchTable[256] = {VM_DISPATCH_TABLE()};
#endif

    // the critical interpreter state, stored in locals for performance
    // the hope is that these map to registers without spilling (which is not true for x86 :/)
    Closure* cl;
    StkId base;
    TValue* k;
    const Instruction* pc;

    LUAU_ASSERT(isLua(L->ci));
    LUAU_ASSERT(L->isactive);
    LUAU_ASSERT(!isblack(obj2gco(L))); // we don't use luaC_threadbarrier because active threads never turn black

#if VM_HAS_NATIVE
    if ((L->ci->flags & LUA_CALLINFO_NATIVE) && !SingleStep)
    {
        Proto* p = clvalue(L->ci->func)->l.p;
        LUAU_ASSERT(p->execdata);

        if (L->global->ecb.enter(L, p) == 0)
            return;
    }

reentry:
#endif

    LUAU_ASSERT(isLua(L->ci));

    pc = L->ci->savedpc;
    cl = clvalue(L->ci->func);
    base = L->base;
    k = cl->l.p->k;

    VM_NEXT(); // starts the interpreter "loop"

    {
    dispatch:
        // Note: this code doesn't always execute! on some platforms we use computed goto which bypasses all of this unless we run in single-step mode
        // Therefore only ever put assertions here.
        LUAU_ASSERT(base == L->base && L->base == L->ci->base);
        LUAU_ASSERT(base <= L->top && L->top <= L->stack + L->stacksize);

        // ... and singlestep logic :)
        if (SingleStep)
        {
            if (L->global->cb.debugstep && !luau_skipstep(LUAU_INSN_OP(*pc)))
            {
                VM_PROTECT(luau_callhook(L, L->global->cb.debugstep, NULL));

                // allow debugstep hook to put thread into error/yield state
                if (L->status != 0)
                    goto exit;
            }

#if VM_USE_CGOTO
            VM_CONTINUE(LUAU_INSN_OP(*pc));
#endif
        }

#if !VM_USE_CGOTO
        size_t dispatchOp = LUAU_INSN_OP(*pc);

    dispatchContinue:
        switch (dispatchOp)
#endif
        {
            VM_CASE(LOP_NOP)
            {
                Instruction insn = *pc++;
                LUAU_ASSERT(insn == 0);
                VM_NEXT();
            }

            VM_CASE(LOP_LOADNIL)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));

                setnilvalue(ra);
                VM_NEXT();
            }

            VM_CASE(LOP_LOADB)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));

                setbvalue(ra, LUAU_INSN_B(insn));

                pc += LUAU_INSN_C(insn);
                LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                VM_NEXT();
            }

            VM_CASE(LOP_LOADN)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));

                setnvalue(ra, LUAU_INSN_D(insn));
                VM_NEXT();
            }

            VM_CASE(LOP_LOADK)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                TValue* kv = VM_KV(LUAU_INSN_D(insn));

                setobj2s(L, ra, kv);
                VM_NEXT();
            }

            VM_CASE(LOP_MOVE)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));

                setobj2s(L, ra, rb);
                VM_NEXT();
            }

            VM_CASE(LOP_GETGLOBAL)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                uint32_t aux = *pc++;
                TValue* kv = VM_KV(aux);
                LUAU_ASSERT(ttisstring(kv));

                // fast-path: value is in expected slot
                LuaTable* h = cl->env;
                int slot = LUAU_INSN_C(insn) & h->nodemask8;
                LuaNode* n = &h->node[slot];

                if (LUAU_LIKELY(ttisstring(gkey(n)) && tsvalue(gkey(n)) == tsvalue(kv)) && !ttisnil(gval(n)))
                {
                    setobj2s(L, ra, gval(n));
                    VM_NEXT();
                }
                else
                {
                    // slow-path, may invoke Lua calls via __index metamethod
                    TValue g;
                    sethvalue(L, &g, h);
                    L->cachedslot = slot;
                    VM_PROTECT(luaV_gettable(L, &g, kv, ra));
                    // save cachedslot to accelerate future lookups; patches currently executing instruction since pc-2 rolls back two pc++
                    VM_PATCH_C(pc - 2, L->cachedslot);
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_SETGLOBAL)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                uint32_t aux = *pc++;
                TValue* kv = VM_KV(aux);
                LUAU_ASSERT(ttisstring(kv));

                // fast-path: value is in expected slot
                LuaTable* h = cl->env;
                int slot = LUAU_INSN_C(insn) & h->nodemask8;
                LuaNode* n = &h->node[slot];

                if (LUAU_LIKELY(ttisstring(gkey(n)) && tsvalue(gkey(n)) == tsvalue(kv) && !ttisnil(gval(n)) && !h->readonly))
                {
                    setobj2t(L, gval(n), ra);
                    luaC_barriert(L, h, ra);
                    VM_NEXT();
                }
                else
                {
                    // slow-path, may invoke Lua calls via __newindex metamethod
                    TValue g;
                    sethvalue(L, &g, h);
                    L->cachedslot = slot;
                    VM_PROTECT(luaV_settable(L, &g, kv, ra));
                    // save cachedslot to accelerate future lookups; patches currently executing instruction since pc-2 rolls back two pc++
                    VM_PATCH_C(pc - 2, L->cachedslot);
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_GETUPVAL)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                TValue* ur = VM_UV(LUAU_INSN_B(insn));
                TValue* v = ttisupval(ur) ? upvalue(ur)->v : ur;

                setobj2s(L, ra, v);
                VM_NEXT();
            }

            VM_CASE(LOP_SETUPVAL)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                TValue* ur = VM_UV(LUAU_INSN_B(insn));
                UpVal* uv = upvalue(ur);

                setobj(L, uv->v, ra);
                luaC_barrier(L, uv, ra);
                VM_NEXT();
            }

            VM_CASE(LOP_CLOSEUPVALS)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));

                if (L->openupval && L->openupval->v >= ra)
                    luaF_close(L, ra);
                VM_NEXT();
            }

            VM_CASE(LOP_GETIMPORT)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                TValue* kv = VM_KV(LUAU_INSN_D(insn));

                // fast-path: import resolution was successful and closure environment is "safe" for import
                if (!ttisnil(kv) && cl->env->safeenv)
                {
                    setobj2s(L, ra, kv);
                    pc++; // skip over AUX
                    VM_NEXT();
                }
                else
                {
                    uint32_t aux = *pc++;

                    VM_PROTECT(luaV_getimport(L, cl->env, k, ra, aux, /* propagatenil= */ false));
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_GETTABLEKS)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                uint32_t aux = *pc++;
                TValue* kv = VM_KV(aux);
                LUAU_ASSERT(ttisstring(kv));

                // fast-path: built-in table
                if (LUAU_LIKELY(ttistable(rb)))
                {
                    LuaTable* h = hvalue(rb);

                    int slot = LUAU_INSN_C(insn) & h->nodemask8;
                    LuaNode* n = &h->node[slot];

                    // fast-path: value is in expected slot
                    if (LUAU_LIKELY(ttisstring(gkey(n)) && tsvalue(gkey(n)) == tsvalue(kv) && !ttisnil(gval(n))))
                    {
                        setobj2s(L, ra, gval(n));
                        VM_NEXT();
                    }
                    else if (!h->metatable)
                    {
                        // fast-path: value is not in expected slot, but the table lookup doesn't involve metatable
                        const TValue* res = luaH_getstr(h, tsvalue(kv));

                        if (res != luaO_nilobject)
                        {
                            int cachedslot = gval2slot(h, res);
                            // save cachedslot to accelerate future lookups; patches currently executing instruction since pc-2 rolls back two pc++
                            VM_PATCH_C(pc - 2, cachedslot);
                        }

                        setobj2s(L, ra, res);
                        VM_NEXT();
                    }
                    else
                    {
                        // slow-path, may invoke Lua calls via __index metamethod
                        L->cachedslot = slot;
                        VM_PROTECT(luaV_gettable(L, rb, kv, ra));
                        // save cachedslot to accelerate future lookups; patches currently executing instruction since pc-2 rolls back two pc++
                        VM_PATCH_C(pc - 2, L->cachedslot);
                        VM_NEXT();
                    }
                }
                else
                {
                    // fast-path: user data with C __index TM
                    const TValue* fn = 0;
                    if (ttisuserdata(rb) && (fn = fasttm(L, uvalue(rb)->metatable, TM_INDEX)) && ttisfunction(fn) && clvalue(fn)->isC)
                    {
                        // note: it's safe to push arguments past top for complicated reasons (see top of the file)
                        LUAU_ASSERT(L->top + 3 < L->stack + L->stacksize);
                        StkId top = L->top;
                        setobj2s(L, top + 0, fn);
                        setobj2s(L, top + 1, rb);
                        setobj2s(L, top + 2, kv);
                        L->top = top + 3;

                        L->cachedslot = LUAU_INSN_C(insn);
                        VM_PROTECT(luaV_callTM(L, 2, LUAU_INSN_A(insn)));
                        // save cachedslot to accelerate future lookups; patches currently executing instruction since pc-2 rolls back two pc++
                        VM_PATCH_C(pc - 2, L->cachedslot);
                        VM_NEXT();
                    }
                    else if (ttisvector(rb))
                    {
                        // fast-path: quick case-insensitive comparison with "X"/"Y"/"Z"
                        const char* name = getstr(tsvalue(kv));
                        int ic = (name[0] | ' ') - 'x';

#if LUA_VECTOR_SIZE == 4
                        // 'w' is before 'x' in ascii, so ic is -1 when indexing with 'w'
                        if (ic == -1)
                            ic = 3;
#endif

                        if (unsigned(ic) < LUA_VECTOR_SIZE && name[1] == '\0')
                        {
                            const float* v = vvalue(rb); // silences ubsan when indexing v[]
                            setnvalue(ra, v[ic]);
                            VM_NEXT();
                        }

                        fn = fasttm(L, L->global->mt[LUA_TVECTOR], TM_INDEX);

                        if (fn && ttisfunction(fn) && clvalue(fn)->isC)
                        {
                            // note: it's safe to push arguments past top for complicated reasons (see top of the file)
                            LUAU_ASSERT(L->top + 3 < L->stack + L->stacksize);
                            StkId top = L->top;
                            setobj2s(L, top + 0, fn);
                            setobj2s(L, top + 1, rb);
                            setobj2s(L, top + 2, kv);
                            L->top = top + 3;

                            L->cachedslot = LUAU_INSN_C(insn);
                            VM_PROTECT(luaV_callTM(L, 2, LUAU_INSN_A(insn)));
                            // save cachedslot to accelerate future lookups; patches currently executing instruction since pc-2 rolls back two pc++
                            VM_PATCH_C(pc - 2, L->cachedslot);
                            VM_NEXT();
                        }

                        // fall through to slow path
                    }

                    // fall through to slow path
                }

                // slow-path, may invoke Lua calls via __index metamethod
                VM_PROTECT(luaV_gettable(L, rb, kv, ra));
                VM_NEXT();
            }

            VM_CASE(LOP_SETTABLEKS)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                uint32_t aux = *pc++;
                TValue* kv = VM_KV(aux);
                LUAU_ASSERT(ttisstring(kv));

                // fast-path: built-in table
                if (LUAU_LIKELY(ttistable(rb)))
                {
                    LuaTable* h = hvalue(rb);

                    int slot = LUAU_INSN_C(insn) & h->nodemask8;
                    LuaNode* n = &h->node[slot];

                    // fast-path: value is in expected slot
                    if (LUAU_LIKELY(ttisstring(gkey(n)) && tsvalue(gkey(n)) == tsvalue(kv) && !ttisnil(gval(n)) && !h->readonly))
                    {
                        setobj2t(L, gval(n), ra);
                        luaC_barriert(L, h, ra);
                        VM_NEXT();
                    }
                    else if (fastnotm(h->metatable, TM_NEWINDEX) && !h->readonly)
                    {
                        VM_PROTECT_PC(); // set may fail

                        TValue* res = luaH_setstr(L, h, tsvalue(kv));
                        int cachedslot = gval2slot(h, res);
                        // save cachedslot to accelerate future lookups; patches currently executing instruction since pc-2 rolls back two pc++
                        VM_PATCH_C(pc - 2, cachedslot);
                        setobj2t(L, res, ra);
                        luaC_barriert(L, h, ra);
                        VM_NEXT();
                    }
                    else
                    {
                        // slow-path, may invoke Lua calls via __newindex metamethod
                        L->cachedslot = slot;
                        VM_PROTECT(luaV_settable(L, rb, kv, ra));
                        // save cachedslot to accelerate future lookups; patches currently executing instruction since pc-2 rolls back two pc++
                        VM_PATCH_C(pc - 2, L->cachedslot);
                        VM_NEXT();
                    }
                }
                else
                {
                    // fast-path: user data with C __newindex TM
                    const TValue* fn = 0;
                    if (ttisuserdata(rb) && (fn = fasttm(L, uvalue(rb)->metatable, TM_NEWINDEX)) && ttisfunction(fn) && clvalue(fn)->isC)
                    {
                        // note: it's safe to push arguments past top for complicated reasons (see top of the file)
                        LUAU_ASSERT(L->top + 4 < L->stack + L->stacksize);
                        StkId top = L->top;
                        setobj2s(L, top + 0, fn);
                        setobj2s(L, top + 1, rb);
                        setobj2s(L, top + 2, kv);
                        setobj2s(L, top + 3, ra);
                        L->top = top + 4;

                        L->cachedslot = LUAU_INSN_C(insn);
                        VM_PROTECT(luaV_callTM(L, 3, -1));
                        // save cachedslot to accelerate future lookups; patches currently executing instruction since pc-2 rolls back two pc++
                        VM_PATCH_C(pc - 2, L->cachedslot);
                        VM_NEXT();
                    }
                    else
                    {
                        // slow-path, may invoke Lua calls via __newindex metamethod
                        VM_PROTECT(luaV_settable(L, rb, kv, ra));
                        VM_NEXT();
                    }
                }
            }

            VM_CASE(LOP_GETTABLE)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                StkId rc = VM_REG(LUAU_INSN_C(insn));

                // fast-path: array lookup
                if (ttistable(rb) && ttisnumber(rc))
                {
                    LuaTable* h = hvalue(rb);

                    double indexd = nvalue(rc);
                    int index = int(indexd);

                    // index has to be an exact integer and in-bounds for the array portion
                    if (LUAU_LIKELY(unsigned(index) - 1 < unsigned(h->sizearray) && !h->metatable && double(index) == indexd))
                    {
                        setobj2s(L, ra, &h->array[unsigned(index - 1)]);
                        VM_NEXT();
                    }

                    // fall through to slow path
                }

                // slow-path: handles out of bounds array lookups, non-integer numeric keys, non-array table lookup, __index MT calls
                VM_PROTECT(luaV_gettable(L, rb, rc, ra));
                VM_NEXT();
            }

            VM_CASE(LOP_SETTABLE)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                StkId rc = VM_REG(LUAU_INSN_C(insn));

                // fast-path: array assign
                if (ttistable(rb) && ttisnumber(rc))
                {
                    LuaTable* h = hvalue(rb);

                    double indexd = nvalue(rc);
                    int index = int(indexd);

                    // index has to be an exact integer and in-bounds for the array portion
                    if (LUAU_LIKELY(unsigned(index) - 1 < unsigned(h->sizearray) && !h->metatable && !h->readonly && double(index) == indexd))
                    {
                        setobj2t(L, &h->array[unsigned(index - 1)], ra);
                        luaC_barriert(L, h, ra);
                        VM_NEXT();
                    }

                    // fall through to slow path
                }

                // slow-path: handles out of bounds array assignments, non-integer numeric keys, non-array table access, __newindex MT calls
                VM_PROTECT(luaV_settable(L, rb, rc, ra));
                VM_NEXT();
            }

            VM_CASE(LOP_GETTABLEN)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                int c = LUAU_INSN_C(insn);

                // fast-path: array lookup
                if (ttistable(rb))
                {
                    LuaTable* h = hvalue(rb);

                    if (LUAU_LIKELY(unsigned(c) < unsigned(h->sizearray) && !h->metatable))
                    {
                        setobj2s(L, ra, &h->array[c]);
                        VM_NEXT();
                    }

                    // fall through to slow path
                }

                // slow-path: handles out of bounds array lookups
                TValue n;
                setnvalue(&n, c + 1);
                VM_PROTECT(luaV_gettable(L, rb, &n, ra));
                VM_NEXT();
            }

            VM_CASE(LOP_SETTABLEN)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                int c = LUAU_INSN_C(insn);

                // fast-path: array assign
                if (ttistable(rb))
                {
                    LuaTable* h = hvalue(rb);

                    if (LUAU_LIKELY(unsigned(c) < unsigned(h->sizearray) && !h->metatable && !h->readonly))
                    {
                        setobj2t(L, &h->array[c], ra);
                        luaC_barriert(L, h, ra);
                        VM_NEXT();
                    }

                    // fall through to slow path
                }

                // slow-path: handles out of bounds array lookups
                TValue n;
                setnvalue(&n, c + 1);
                VM_PROTECT(luaV_settable(L, rb, &n, ra));
                VM_NEXT();
            }

            VM_CASE(LOP_NEWCLOSURE)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));

                Proto* pv = cl->l.p->p[LUAU_INSN_D(insn)];
                LUAU_ASSERT(unsigned(LUAU_INSN_D(insn)) < unsigned(cl->l.p->sizep));

                VM_PROTECT_PC(); // luaF_newLclosure may fail due to OOM

                // note: we save closure to stack early in case the code below wants to capture it by value
                Closure* ncl = luaF_newLclosure(L, pv->nups, cl->env, pv);
                setclvalue(L, ra, ncl);

                for (int ui = 0; ui < pv->nups; ++ui)
                {
                    Instruction uinsn = *pc++;
                    LUAU_ASSERT(LUAU_INSN_OP(uinsn) == LOP_CAPTURE);

                    switch (LUAU_INSN_A(uinsn))
                    {
                    case LCT_VAL:
                        setobj(L, &ncl->l.uprefs[ui], VM_REG(LUAU_INSN_B(uinsn)));
                        break;

                    case LCT_REF:
                        setupvalue(L, &ncl->l.uprefs[ui], luaF_findupval(L, VM_REG(LUAU_INSN_B(uinsn))));
                        break;

                    case LCT_UPVAL:
                        setobj(L, &ncl->l.uprefs[ui], VM_UV(LUAU_INSN_B(uinsn)));
                        break;

                    default:
                        LUAU_ASSERT(!"Unknown upvalue capture type");
                        LUAU_UNREACHABLE(); // improves switch() codegen by eliding opcode bounds checks
                    }
                }

                VM_PROTECT(luaC_checkGC(L));
                VM_NEXT();
            }

            VM_CASE(LOP_NAMECALL)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                uint32_t aux = *pc++;
                TValue* kv = VM_KV(aux);
                LUAU_ASSERT(ttisstring(kv));

                if (LUAU_LIKELY(ttistable(rb)))
                {
                    LuaTable* h = hvalue(rb);
                    // note: we can't use nodemask8 here because we need to query the main position of the table, and 8-bit nodemask8 only works
                    // for predictive lookups
                    LuaNode* n = &h->node[tsvalue(kv)->hash & (sizenode(h) - 1)];

                    const TValue* mt = 0;
                    const LuaNode* mtn = 0;

                    // fast-path: key is in the table in expected slot
                    if (ttisstring(gkey(n)) && tsvalue(gkey(n)) == tsvalue(kv) && !ttisnil(gval(n)))
                    {
                        // note: order of copies allows rb to alias ra+1 or ra
                        setobj2s(L, ra + 1, rb);
                        setobj2s(L, ra, gval(n));
                    }
                    // fast-path: key is absent from the base, table has an __index table, and it has the result in the expected slot
                    else if (gnext(n) == 0 && (mt = fasttm(L, hvalue(rb)->metatable, TM_INDEX)) && ttistable(mt) &&
                             (mtn = &hvalue(mt)->node[LUAU_INSN_C(insn) & hvalue(mt)->nodemask8]) && ttisstring(gkey(mtn)) &&
                             tsvalue(gkey(mtn)) == tsvalue(kv) && !ttisnil(gval(mtn)))
                    {
                        // note: order of copies allows rb to alias ra+1 or ra
                        setobj2s(L, ra + 1, rb);
                        setobj2s(L, ra, gval(mtn));
                    }
                    else
                    {
                        // slow-path: handles full table lookup
                        setobj2s(L, ra + 1, rb);
                        L->cachedslot = LUAU_INSN_C(insn);
                        VM_PROTECT(luaV_gettable(L, rb, kv, ra));
                        // save cachedslot to accelerate future lookups; patches currently executing instruction since pc-2 rolls back two pc++
                        VM_PATCH_C(pc - 2, L->cachedslot);
                        // recompute ra since stack might have been reallocated
                        ra = VM_REG(LUAU_INSN_A(insn));
                        if (ttisnil(ra))
                            luaG_methoderror(L, ra + 1, tsvalue(kv));
                    }
                }
                else
                {
                    LuaTable* mt = ttisuserdata(rb) ? uvalue(rb)->metatable : L->global->mt[ttype(rb)];
                    const TValue* tmi = 0;

                    // fast-path: metatable with __namecall
                    if (const TValue* fn = fasttm(L, mt, TM_NAMECALL))
                    {
                        // note: order of copies allows rb to alias ra+1 or ra
                        setobj2s(L, ra + 1, rb);
                        setobj2s(L, ra, fn);

                        L->namecall = tsvalue(kv);
                    }
                    else if ((tmi = fasttm(L, mt, TM_INDEX)) && ttistable(tmi))
                    {
                        LuaTable* h = hvalue(tmi);
                        int slot = LUAU_INSN_C(insn) & h->nodemask8;
                        LuaNode* n = &h->node[slot];

                        // fast-path: metatable with __index that has method in expected slot
                        if (LUAU_LIKELY(ttisstring(gkey(n)) && tsvalue(gkey(n)) == tsvalue(kv) && !ttisnil(gval(n))))
                        {
                            // note: order of copies allows rb to alias ra+1 or ra
                            setobj2s(L, ra + 1, rb);
                            setobj2s(L, ra, gval(n));
                        }
                        else
                        {
                            // slow-path: handles slot mismatch
                            setobj2s(L, ra + 1, rb);
                            L->cachedslot = slot;
                            VM_PROTECT(luaV_gettable(L, rb, kv, ra));
                            // save cachedslot to accelerate future lookups; patches currently executing instruction since pc-2 rolls back two pc++
                            VM_PATCH_C(pc - 2, L->cachedslot);
                            // recompute ra since stack might have been reallocated
                            ra = VM_REG(LUAU_INSN_A(insn));
                            if (ttisnil(ra))
                                luaG_methoderror(L, ra + 1, tsvalue(kv));
                        }
                    }
                    else
                    {
                        // slow-path: handles non-table __index
                        setobj2s(L, ra + 1, rb);
                        VM_PROTECT(luaV_gettable(L, rb, kv, ra));
                        // recompute ra since stack might have been reallocated
                        ra = VM_REG(LUAU_INSN_A(insn));
                        if (ttisnil(ra))
                            luaG_methoderror(L, ra + 1, tsvalue(kv));
                    }
                }

                // intentional fallthrough to CALL
                LUAU_ASSERT(LUAU_INSN_OP(*pc) == LOP_CALL);
            }

            VM_CASE(LOP_CALL)
            {
                VM_INTERRUPT();
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));

                int nparams = LUAU_INSN_B(insn) - 1;
                int nresults = LUAU_INSN_C(insn) - 1;

                StkId argtop = L->top;
                argtop = (nparams == LUA_MULTRET) ? argtop : ra + 1 + nparams;

                // slow-path: not a function call
                if (LUAU_UNLIKELY(!ttisfunction(ra)))
                {
                    VM_PROTECT_PC(); // luaV_tryfuncTM may fail

                    luaV_tryfuncTM(L, ra);
                    argtop++; // __call adds an extra self
                }

                Closure* ccl = clvalue(ra);
                L->ci->savedpc = pc;

                CallInfo* ci = incr_ci(L);
                ci->func = ra;
                ci->base = ra + 1;
                ci->top = argtop + ccl->stacksize; // note: technically UB since we haven't reallocated the stack yet
                ci->savedpc = NULL;
                ci->flags = 0;
                ci->nresults = nresults;

                L->base = ci->base;
                L->top = argtop;

                // note: this reallocs stack, but we don't need to VM_PROTECT this
                // this is because we're going to modify base/savedpc manually anyhow
                // crucially, we can't use ra/argtop after this line
                luaD_checkstackfornewci(L, ccl->stacksize);

                LUAU_ASSERT(ci->top <= L->stack_last);

                if (!ccl->isC)
                {
                    Proto* p = ccl->l.p;

                    // fill unused parameters with nil
                    StkId argi = L->top;
                    StkId argend = L->base + p->numparams;
                    while (argi < argend)
                        setnilvalue(argi++); // complete missing arguments
                    L->top = p->is_vararg ? argi : ci->top;

                    // reentry
                    // codeentry may point to NATIVECALL instruction when proto is compiled to native code
                    // this will result in execution continuing in native code, and is equivalent to if (p->execdata) but has no additional overhead
                    // note that p->codeentry may point *outside* of p->code..p->code+p->sizecode, but that pointer never gets saved to savedpc.
                    pc = SingleStep ? p->code : p->codeentry;
                    cl = ccl;
                    base = L->base;
                    k = p->k;
                    VM_NEXT();
                }
                else
                {
                    lua_CFunction func = ccl->c.f;
                    int n = func(L);

                    // yield
                    if (n < 0)
                        goto exit;

                    // ci is our callinfo, cip is our parent
                    CallInfo* ci = L->ci;
                    CallInfo* cip = ci - 1;

                    // copy return values into parent stack (but only up to nresults!), fill the rest with nil
                    // note: in MULTRET context nresults starts as -1 so i != 0 condition never activates intentionally
                    StkId res = ci->func;
                    StkId vali = L->top - n;
                    StkId valend = L->top;

                    int i;
                    for (i = nresults; i != 0 && vali < valend; i--)
                        setobj2s(L, res++, vali++);
                    while (i-- > 0)
                        setnilvalue(res++);

                    // pop the stack frame
                    L->ci = cip;
                    L->base = cip->base;
                    L->top = (nresults == LUA_MULTRET) ? res : cip->top;

                    base = L->base; // stack may have been reallocated, so we need to refresh base ptr
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_RETURN)
            {
                VM_INTERRUPT();
                Instruction insn = *pc++;
                StkId ra = &base[LUAU_INSN_A(insn)]; // note: this can point to L->top if b == LUA_MULTRET making VM_REG unsafe to use
                int b = LUAU_INSN_B(insn) - 1;

                // ci is our callinfo, cip is our parent
                CallInfo* ci = L->ci;
                CallInfo* cip = ci - 1;

                StkId res = ci->func; // note: we assume CALL always puts func+args and expects results to start at func

                StkId vali = ra;
                StkId valend =
                    (b == LUA_MULTRET) ? L->top : ra + b; // copy as much as possible for MULTRET calls, and only as much as needed otherwise

                int nresults = ci->nresults;

                // copy return values into parent stack (but only up to nresults!), fill the rest with nil
                // note: in MULTRET context nresults starts as -1 so i != 0 condition never activates intentionally
                int i;
                for (i = nresults; i != 0 && vali < valend; i--)
                    setobj2s(L, res++, vali++);
                while (i-- > 0)
                    setnilvalue(res++);

                // pop the stack frame
                L->ci = cip;
                L->base = cip->base;
                L->top = (nresults == LUA_MULTRET) ? res : cip->top;

                // we're done!
                if (LUAU_UNLIKELY(ci->flags & LUA_CALLINFO_RETURN))
                {
                    goto exit;
                }

                LUAU_ASSERT(isLua(L->ci));

                Closure* nextcl = clvalue(cip->func);
                Proto* nextproto = nextcl->l.p;

#if VM_HAS_NATIVE
                if (LUAU_UNLIKELY((cip->flags & LUA_CALLINFO_NATIVE) && !SingleStep))
                {
                    if (L->global->ecb.enter(L, nextproto) == 1)
                        goto reentry;
                    else
                        goto exit;
                }
#endif

                // reentry
                pc = cip->savedpc;
                cl = nextcl;
                base = L->base;
                k = nextproto->k;
                VM_NEXT();
            }

            VM_CASE(LOP_JUMP)
            {
                Instruction insn = *pc++;

                pc += LUAU_INSN_D(insn);
                LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                VM_NEXT();
            }

            VM_CASE(LOP_JUMPIF)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));

                pc += l_isfalse(ra) ? 0 : LUAU_INSN_D(insn);
                LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                VM_NEXT();
            }

            VM_CASE(LOP_JUMPIFNOT)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));

                pc += l_isfalse(ra) ? LUAU_INSN_D(insn) : 0;
                LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                VM_NEXT();
            }

            VM_CASE(LOP_JUMPIFEQ)
            {
                Instruction insn = *pc++;
                uint32_t aux = *pc;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(aux);

                // Note that all jumps below jump by 1 in the "false" case to skip over aux
                if (ttype(ra) == ttype(rb))
                {
                    switch (ttype(ra))
                    {
                    case LUA_TNIL:
                        pc += LUAU_INSN_D(insn);
                        LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                        VM_NEXT();

                    case LUA_TBOOLEAN:
                        pc += bvalue(ra) == bvalue(rb) ? LUAU_INSN_D(insn) : 1;
                        LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                        VM_NEXT();

                    case LUA_TLIGHTUSERDATA:
                        pc += (pvalue(ra) == pvalue(rb) && lightuserdatatag(ra) == lightuserdatatag(rb)) ? LUAU_INSN_D(insn) : 1;
                        LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                        VM_NEXT();

                    case LUA_TNUMBER:
                        pc += nvalue(ra) == nvalue(rb) ? LUAU_INSN_D(insn) : 1;
                        LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                        VM_NEXT();

                    case LUA_TVECTOR:
                        pc += luai_veceq(vvalue(ra), vvalue(rb)) ? LUAU_INSN_D(insn) : 1;
                        LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                        VM_NEXT();

                    case LUA_TSTRING:
                    case LUA_TFUNCTION:
                    case LUA_TTHREAD:
                    case LUA_TBUFFER:
                        pc += gcvalue(ra) == gcvalue(rb) ? LUAU_INSN_D(insn) : 1;
                        LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                        VM_NEXT();

                    case LUA_TTABLE:
                        // fast-path: same metatable, no EQ metamethod
                        if (hvalue(ra)->metatable == hvalue(rb)->metatable)
                        {
                            const TValue* fn = fasttm(L, hvalue(ra)->metatable, TM_EQ);

                            if (!fn)
                            {
                                pc += hvalue(ra) == hvalue(rb) ? LUAU_INSN_D(insn) : 1;
                                LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                                VM_NEXT();
                            }
                        }
                        // slow path after switch()
                        break;

                    case LUA_TUSERDATA:
                        // fast-path: same metatable, no EQ metamethod or C metamethod
                        if (uvalue(ra)->metatable == uvalue(rb)->metatable)
                        {
                            const TValue* fn = fasttm(L, uvalue(ra)->metatable, TM_EQ);

                            if (!fn)
                            {
                                pc += uvalue(ra) == uvalue(rb) ? LUAU_INSN_D(insn) : 1;
                                LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                                VM_NEXT();
                            }
                            else if (ttisfunction(fn) && clvalue(fn)->isC)
                            {
                                // note: it's safe to push arguments past top for complicated reasons (see top of the file)
                                LUAU_ASSERT(L->top + 3 < L->stack + L->stacksize);
                                StkId top = L->top;
                                setobj2s(L, top + 0, fn);
                                setobj2s(L, top + 1, ra);
                                setobj2s(L, top + 2, rb);
                                int res = int(top - base);
                                L->top = top + 3;

                                VM_PROTECT(luaV_callTM(L, 2, res));
                                pc += !l_isfalse(&base[res]) ? LUAU_INSN_D(insn) : 1;
                                LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                                VM_NEXT();
                            }
                        }
                        // slow path after switch()
                        break;

                    default:
                        LUAU_ASSERT(!"Unknown value type");
                        LUAU_UNREACHABLE(); // improves switch() codegen by eliding opcode bounds checks
                    }

                    // slow-path: tables with metatables and userdata values
                    // note that we don't have a fast path for userdata values without metatables, since that's very rare
                    int res;
                    VM_PROTECT(res = luaV_equalval(L, ra, rb));

                    pc += (res == 1) ? LUAU_INSN_D(insn) : 1;
                    LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                    VM_NEXT();
                }
                else
                {
                    pc += 1;
                    LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_JUMPIFNOTEQ)
            {
                Instruction insn = *pc++;
                uint32_t aux = *pc;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(aux);

                // Note that all jumps below jump by 1 in the "true" case to skip over aux
                if (ttype(ra) == ttype(rb))
                {
                    switch (ttype(ra))
                    {
                    case LUA_TNIL:
                        pc += 1;
                        LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                        VM_NEXT();

                    case LUA_TBOOLEAN:
                        pc += bvalue(ra) != bvalue(rb) ? LUAU_INSN_D(insn) : 1;
                        LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                        VM_NEXT();

                    case LUA_TLIGHTUSERDATA:
                        pc += (pvalue(ra) != pvalue(rb) || lightuserdatatag(ra) != lightuserdatatag(rb)) ? LUAU_INSN_D(insn) : 1;
                        LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                        VM_NEXT();

                    case LUA_TNUMBER:
                        pc += nvalue(ra) != nvalue(rb) ? LUAU_INSN_D(insn) : 1;
                        LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                        VM_NEXT();

                    case LUA_TVECTOR:
                        pc += !luai_veceq(vvalue(ra), vvalue(rb)) ? LUAU_INSN_D(insn) : 1;
                        LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                        VM_NEXT();

                    case LUA_TSTRING:
                    case LUA_TFUNCTION:
                    case LUA_TTHREAD:
                    case LUA_TBUFFER:
                        pc += gcvalue(ra) != gcvalue(rb) ? LUAU_INSN_D(insn) : 1;
                        LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                        VM_NEXT();

                    case LUA_TTABLE:
                        // fast-path: same metatable, no EQ metamethod
                        if (hvalue(ra)->metatable == hvalue(rb)->metatable)
                        {
                            const TValue* fn = fasttm(L, hvalue(ra)->metatable, TM_EQ);

                            if (!fn)
                            {
                                pc += hvalue(ra) != hvalue(rb) ? LUAU_INSN_D(insn) : 1;
                                LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                                VM_NEXT();
                            }
                        }
                        // slow path after switch()
                        break;

                    case LUA_TUSERDATA:
                        // fast-path: same metatable, no EQ metamethod or C metamethod
                        if (uvalue(ra)->metatable == uvalue(rb)->metatable)
                        {
                            const TValue* fn = fasttm(L, uvalue(ra)->metatable, TM_EQ);

                            if (!fn)
                            {
                                pc += uvalue(ra) != uvalue(rb) ? LUAU_INSN_D(insn) : 1;
                                LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                                VM_NEXT();
                            }
                            else if (ttisfunction(fn) && clvalue(fn)->isC)
                            {
                                // note: it's safe to push arguments past top for complicated reasons (see top of the file)
                                LUAU_ASSERT(L->top + 3 < L->stack + L->stacksize);
                                StkId top = L->top;
                                setobj2s(L, top + 0, fn);
                                setobj2s(L, top + 1, ra);
                                setobj2s(L, top + 2, rb);
                                int res = int(top - base);
                                L->top = top + 3;

                                VM_PROTECT(luaV_callTM(L, 2, res));
                                pc += l_isfalse(&base[res]) ? LUAU_INSN_D(insn) : 1;
                                LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                                VM_NEXT();
                            }
                        }
                        // slow path after switch()
                        break;

                    default:
                        LUAU_ASSERT(!"Unknown value type");
                        LUAU_UNREACHABLE(); // improves switch() codegen by eliding opcode bounds checks
                    }

                    // slow-path: tables with metatables and userdata values
                    // note that we don't have a fast path for userdata values without metatables, since that's very rare
                    int res;
                    VM_PROTECT(res = luaV_equalval(L, ra, rb));

                    pc += (res == 0) ? LUAU_INSN_D(insn) : 1;
                    LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                    VM_NEXT();
                }
                else
                {
                    pc += LUAU_INSN_D(insn);
                    LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_JUMPIFLE)
            {
                Instruction insn = *pc++;
                uint32_t aux = *pc;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(aux);

                // fast-path: number
                // Note that all jumps below jump by 1 in the "false" case to skip over aux
                if (LUAU_LIKELY(ttisnumber(ra) && ttisnumber(rb)))
                {
                    pc += nvalue(ra) <= nvalue(rb) ? LUAU_INSN_D(insn) : 1;
                    LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                    VM_NEXT();
                }
                // fast-path: string
                else if (ttisstring(ra) && ttisstring(rb))
                {
                    pc += luaV_strcmp(tsvalue(ra), tsvalue(rb)) <= 0 ? LUAU_INSN_D(insn) : 1;
                    LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                    VM_NEXT();
                }
                else
                {
                    int res;
                    VM_PROTECT(res = luaV_lessequal(L, ra, rb));

                    pc += (res == 1) ? LUAU_INSN_D(insn) : 1;
                    LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_JUMPIFNOTLE)
            {
                Instruction insn = *pc++;
                uint32_t aux = *pc;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(aux);

                // fast-path: number
                // Note that all jumps below jump by 1 in the "true" case to skip over aux
                if (LUAU_LIKELY(ttisnumber(ra) && ttisnumber(rb)))
                {
                    pc += !(nvalue(ra) <= nvalue(rb)) ? LUAU_INSN_D(insn) : 1;
                    LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                    VM_NEXT();
                }
                // fast-path: string
                else if (ttisstring(ra) && ttisstring(rb))
                {
                    pc += !(luaV_strcmp(tsvalue(ra), tsvalue(rb)) <= 0) ? LUAU_INSN_D(insn) : 1;
                    LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                    VM_NEXT();
                }
                else
                {
                    int res;
                    VM_PROTECT(res = luaV_lessequal(L, ra, rb));

                    pc += (res == 0) ? LUAU_INSN_D(insn) : 1;
                    LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_JUMPIFLT)
            {
                Instruction insn = *pc++;
                uint32_t aux = *pc;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(aux);

                // fast-path: number
                // Note that all jumps below jump by 1 in the "false" case to skip over aux
                if (LUAU_LIKELY(ttisnumber(ra) && ttisnumber(rb)))
                {
                    pc += nvalue(ra) < nvalue(rb) ? LUAU_INSN_D(insn) : 1;
                    LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                    VM_NEXT();
                }
                // fast-path: string
                else if (ttisstring(ra) && ttisstring(rb))
                {
                    pc += luaV_strcmp(tsvalue(ra), tsvalue(rb)) < 0 ? LUAU_INSN_D(insn) : 1;
                    LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                    VM_NEXT();
                }
                else
                {
                    int res;
                    VM_PROTECT(res = luaV_lessthan(L, ra, rb));

                    pc += (res == 1) ? LUAU_INSN_D(insn) : 1;
                    LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_JUMPIFNOTLT)
            {
                Instruction insn = *pc++;
                uint32_t aux = *pc;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(aux);

                // fast-path: number
                // Note that all jumps below jump by 1 in the "true" case to skip over aux
                if (LUAU_LIKELY(ttisnumber(ra) && ttisnumber(rb)))
                {
                    pc += !(nvalue(ra) < nvalue(rb)) ? LUAU_INSN_D(insn) : 1;
                    LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                    VM_NEXT();
                }
                // fast-path: string
                else if (ttisstring(ra) && ttisstring(rb))
                {
                    pc += !(luaV_strcmp(tsvalue(ra), tsvalue(rb)) < 0) ? LUAU_INSN_D(insn) : 1;
                    LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                    VM_NEXT();
                }
                else
                {
                    int res;
                    VM_PROTECT(res = luaV_lessthan(L, ra, rb));

                    pc += (res == 0) ? LUAU_INSN_D(insn) : 1;
                    LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_ADD)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                StkId rc = VM_REG(LUAU_INSN_C(insn));

                // fast-path
                if (LUAU_LIKELY(ttisnumber(rb) && ttisnumber(rc)))
                {
                    setnvalue(ra, nvalue(rb) + nvalue(rc));
                    VM_NEXT();
                }
                else if (ttisvector(rb) && ttisvector(rc))
                {
                    const float* vb = vvalue(rb);
                    const float* vc = vvalue(rc);
                    setvvalue(ra, vb[0] + vc[0], vb[1] + vc[1], vb[2] + vc[2], vb[3] + vc[3]);
                    VM_NEXT();
                }
                else
                {
                    // fast-path for userdata with C functions
                    const TValue* fn = 0;
                    if (ttisuserdata(rb) && (fn = luaT_gettmbyobj(L, rb, TM_ADD)) && ttisfunction(fn) && clvalue(fn)->isC)
                    {
                        // note: it's safe to push arguments past top for complicated reasons (see top of the file)
                        LUAU_ASSERT(L->top + 3 < L->stack + L->stacksize);
                        StkId top = L->top;
                        setobj2s(L, top + 0, fn);
                        setobj2s(L, top + 1, rb);
                        setobj2s(L, top + 2, rc);
                        L->top = top + 3;

                        VM_PROTECT(luaV_callTM(L, 2, LUAU_INSN_A(insn)));
                        VM_NEXT();
                    }
                    else
                    {
                        // slow-path, may invoke C/Lua via metamethods
                        VM_PROTECT(luaV_doarithimpl<TM_ADD>(L, ra, rb, rc));
                        VM_NEXT();
                    }
                }
            }

            VM_CASE(LOP_SUB)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                StkId rc = VM_REG(LUAU_INSN_C(insn));

                // fast-path
                if (LUAU_LIKELY(ttisnumber(rb) && ttisnumber(rc)))
                {
                    setnvalue(ra, nvalue(rb) - nvalue(rc));
                    VM_NEXT();
                }
                else if (ttisvector(rb) && ttisvector(rc))
                {
                    const float* vb = vvalue(rb);
                    const float* vc = vvalue(rc);
                    setvvalue(ra, vb[0] - vc[0], vb[1] - vc[1], vb[2] - vc[2], vb[3] - vc[3]);
                    VM_NEXT();
                }
                else
                {
                    // fast-path for userdata with C functions
                    const TValue* fn = 0;
                    if (ttisuserdata(rb) && (fn = luaT_gettmbyobj(L, rb, TM_SUB)) && ttisfunction(fn) && clvalue(fn)->isC)
                    {
                        // note: it's safe to push arguments past top for complicated reasons (see top of the file)
                        LUAU_ASSERT(L->top + 3 < L->stack + L->stacksize);
                        StkId top = L->top;
                        setobj2s(L, top + 0, fn);
                        setobj2s(L, top + 1, rb);
                        setobj2s(L, top + 2, rc);
                        L->top = top + 3;

                        VM_PROTECT(luaV_callTM(L, 2, LUAU_INSN_A(insn)));
                        VM_NEXT();
                    }
                    else
                    {
                        // slow-path, may invoke C/Lua via metamethods
                        VM_PROTECT(luaV_doarithimpl<TM_SUB>(L, ra, rb, rc));
                        VM_NEXT();
                    }
                }
            }

            VM_CASE(LOP_MUL)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                StkId rc = VM_REG(LUAU_INSN_C(insn));

                // fast-path
                if (LUAU_LIKELY(ttisnumber(rb) && ttisnumber(rc)))
                {
                    setnvalue(ra, nvalue(rb) * nvalue(rc));
                    VM_NEXT();
                }
                else if (ttisvector(rb) && ttisnumber(rc))
                {
                    const float* vb = vvalue(rb);
                    float vc = cast_to(float, nvalue(rc));
                    setvvalue(ra, vb[0] * vc, vb[1] * vc, vb[2] * vc, vb[3] * vc);
                    VM_NEXT();
                }
                else if (ttisvector(rb) && ttisvector(rc))
                {
                    const float* vb = vvalue(rb);
                    const float* vc = vvalue(rc);
                    setvvalue(ra, vb[0] * vc[0], vb[1] * vc[1], vb[2] * vc[2], vb[3] * vc[3]);
                    VM_NEXT();
                }
                else if (ttisnumber(rb) && ttisvector(rc))
                {
                    float vb = cast_to(float, nvalue(rb));
                    const float* vc = vvalue(rc);
                    setvvalue(ra, vb * vc[0], vb * vc[1], vb * vc[2], vb * vc[3]);
                    VM_NEXT();
                }
                else
                {
                    // fast-path for userdata with C functions
                    StkId rbc = ttisnumber(rb) ? rc : rb;
                    const TValue* fn = 0;
                    if (ttisuserdata(rbc) && (fn = luaT_gettmbyobj(L, rbc, TM_MUL)) && ttisfunction(fn) && clvalue(fn)->isC)
                    {
                        // note: it's safe to push arguments past top for complicated reasons (see top of the file)
                        LUAU_ASSERT(L->top + 3 < L->stack + L->stacksize);
                        StkId top = L->top;
                        setobj2s(L, top + 0, fn);
                        setobj2s(L, top + 1, rb);
                        setobj2s(L, top + 2, rc);
                        L->top = top + 3;

                        VM_PROTECT(luaV_callTM(L, 2, LUAU_INSN_A(insn)));
                        VM_NEXT();
                    }
                    else
                    {
                        // slow-path, may invoke C/Lua via metamethods
                        VM_PROTECT(luaV_doarithimpl<TM_MUL>(L, ra, rb, rc));
                        VM_NEXT();
                    }
                }
            }

            VM_CASE(LOP_DIV)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                StkId rc = VM_REG(LUAU_INSN_C(insn));

                // fast-path
                if (LUAU_LIKELY(ttisnumber(rb) && ttisnumber(rc)))
                {
                    setnvalue(ra, nvalue(rb) / nvalue(rc));
                    VM_NEXT();
                }
                else if (ttisvector(rb) && ttisnumber(rc))
                {
                    const float* vb = vvalue(rb);
                    float vc = cast_to(float, nvalue(rc));
                    setvvalue(ra, vb[0] / vc, vb[1] / vc, vb[2] / vc, vb[3] / vc);
                    VM_NEXT();
                }
                else if (ttisvector(rb) && ttisvector(rc))
                {
                    const float* vb = vvalue(rb);
                    const float* vc = vvalue(rc);
                    setvvalue(ra, vb[0] / vc[0], vb[1] / vc[1], vb[2] / vc[2], vb[3] / vc[3]);
                    VM_NEXT();
                }
                else if (ttisnumber(rb) && ttisvector(rc))
                {
                    float vb = cast_to(float, nvalue(rb));
                    const float* vc = vvalue(rc);
                    setvvalue(ra, vb / vc[0], vb / vc[1], vb / vc[2], vb / vc[3]);
                    VM_NEXT();
                }
                else
                {
                    // fast-path for userdata with C functions
                    StkId rbc = ttisnumber(rb) ? rc : rb;
                    const TValue* fn = 0;
                    if (ttisuserdata(rbc) && (fn = luaT_gettmbyobj(L, rbc, TM_DIV)) && ttisfunction(fn) && clvalue(fn)->isC)
                    {
                        // note: it's safe to push arguments past top for complicated reasons (see top of the file)
                        LUAU_ASSERT(L->top + 3 < L->stack + L->stacksize);
                        StkId top = L->top;
                        setobj2s(L, top + 0, fn);
                        setobj2s(L, top + 1, rb);
                        setobj2s(L, top + 2, rc);
                        L->top = top + 3;

                        VM_PROTECT(luaV_callTM(L, 2, LUAU_INSN_A(insn)));
                        VM_NEXT();
                    }
                    else
                    {
                        // slow-path, may invoke C/Lua via metamethods
                        VM_PROTECT(luaV_doarithimpl<TM_DIV>(L, ra, rb, rc));
                        VM_NEXT();
                    }
                }
            }

            VM_CASE(LOP_IDIV)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                StkId rc = VM_REG(LUAU_INSN_C(insn));

                // fast-path
                if (LUAU_LIKELY(ttisnumber(rb) && ttisnumber(rc)))
                {
                    setnvalue(ra, luai_numidiv(nvalue(rb), nvalue(rc)));
                    VM_NEXT();
                }
                else if (ttisvector(rb) && ttisnumber(rc))
                {
                    const float* vb = vvalue(rb);
                    float vc = cast_to(float, nvalue(rc));
                    setvvalue(
                        ra,
                        float(luai_numidiv(vb[0], vc)),
                        float(luai_numidiv(vb[1], vc)),
                        float(luai_numidiv(vb[2], vc)),
                        float(luai_numidiv(vb[3], vc))
                    );
                    VM_NEXT();
                }
                else
                {
                    // fast-path for userdata with C functions
                    StkId rbc = ttisnumber(rb) ? rc : rb;
                    const TValue* fn = 0;
                    if (ttisuserdata(rbc) && (fn = luaT_gettmbyobj(L, rbc, TM_IDIV)) && ttisfunction(fn) && clvalue(fn)->isC)
                    {
                        // note: it's safe to push arguments past top for complicated reasons (see top of the file)
                        LUAU_ASSERT(L->top + 3 < L->stack + L->stacksize);
                        StkId top = L->top;
                        setobj2s(L, top + 0, fn);
                        setobj2s(L, top + 1, rb);
                        setobj2s(L, top + 2, rc);
                        L->top = top + 3;

                        VM_PROTECT(luaV_callTM(L, 2, LUAU_INSN_A(insn)));
                        VM_NEXT();
                    }
                    else
                    {
                        // slow-path, may invoke C/Lua via metamethods
                        VM_PROTECT(luaV_doarithimpl<TM_IDIV>(L, ra, rb, rc));
                        VM_NEXT();
                    }
                }
            }

            VM_CASE(LOP_MOD)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                StkId rc = VM_REG(LUAU_INSN_C(insn));

                // fast-path
                if (ttisnumber(rb) && ttisnumber(rc))
                {
                    double nb = nvalue(rb);
                    double nc = nvalue(rc);
                    setnvalue(ra, luai_nummod(nb, nc));
                    VM_NEXT();
                }
                else
                {
                    // slow-path, may invoke C/Lua via metamethods
                    VM_PROTECT(luaV_doarithimpl<TM_MOD>(L, ra, rb, rc));
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_POW)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                StkId rc = VM_REG(LUAU_INSN_C(insn));

                // fast-path
                if (ttisnumber(rb) && ttisnumber(rc))
                {
                    setnvalue(ra, pow(nvalue(rb), nvalue(rc)));
                    VM_NEXT();
                }
                else
                {
                    // slow-path, may invoke C/Lua via metamethods
                    VM_PROTECT(luaV_doarithimpl<TM_POW>(L, ra, rb, rc));
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_ADDK)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                TValue* kv = VM_KV(LUAU_INSN_C(insn));

                // fast-path
                if (ttisnumber(rb))
                {
                    setnvalue(ra, nvalue(rb) + nvalue(kv));
                    VM_NEXT();
                }
                else
                {
                    // slow-path, may invoke C/Lua via metamethods
                    VM_PROTECT(luaV_doarithimpl<TM_ADD>(L, ra, rb, kv));
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_SUBK)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                TValue* kv = VM_KV(LUAU_INSN_C(insn));

                // fast-path
                if (ttisnumber(rb))
                {
                    setnvalue(ra, nvalue(rb) - nvalue(kv));
                    VM_NEXT();
                }
                else
                {
                    // slow-path, may invoke C/Lua via metamethods
                    VM_PROTECT(luaV_doarithimpl<TM_SUB>(L, ra, rb, kv));
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_MULK)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                TValue* kv = VM_KV(LUAU_INSN_C(insn));

                // fast-path
                if (LUAU_LIKELY(ttisnumber(rb)))
                {
                    setnvalue(ra, nvalue(rb) * nvalue(kv));
                    VM_NEXT();
                }
                else if (ttisvector(rb))
                {
                    const float* vb = vvalue(rb);
                    float vc = cast_to(float, nvalue(kv));
                    setvvalue(ra, vb[0] * vc, vb[1] * vc, vb[2] * vc, vb[3] * vc);
                    VM_NEXT();
                }
                else
                {
                    // fast-path for userdata with C functions
                    const TValue* fn = 0;
                    if (ttisuserdata(rb) && (fn = luaT_gettmbyobj(L, rb, TM_MUL)) && ttisfunction(fn) && clvalue(fn)->isC)
                    {
                        // note: it's safe to push arguments past top for complicated reasons (see top of the file)
                        LUAU_ASSERT(L->top + 3 < L->stack + L->stacksize);
                        StkId top = L->top;
                        setobj2s(L, top + 0, fn);
                        setobj2s(L, top + 1, rb);
                        setobj2s(L, top + 2, kv);
                        L->top = top + 3;

                        VM_PROTECT(luaV_callTM(L, 2, LUAU_INSN_A(insn)));
                        VM_NEXT();
                    }
                    else
                    {
                        // slow-path, may invoke C/Lua via metamethods
                        VM_PROTECT(luaV_doarithimpl<TM_MUL>(L, ra, rb, kv));
                        VM_NEXT();
                    }
                }
            }

            VM_CASE(LOP_DIVK)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                TValue* kv = VM_KV(LUAU_INSN_C(insn));

                // fast-path
                if (LUAU_LIKELY(ttisnumber(rb)))
                {
                    setnvalue(ra, nvalue(rb) / nvalue(kv));
                    VM_NEXT();
                }
                else if (ttisvector(rb))
                {
                    const float* vb = vvalue(rb);
                    float nc = cast_to(float, nvalue(kv));
                    setvvalue(ra, vb[0] / nc, vb[1] / nc, vb[2] / nc, vb[3] / nc);
                    VM_NEXT();
                }
                else
                {
                    // fast-path for userdata with C functions
                    const TValue* fn = 0;
                    if (ttisuserdata(rb) && (fn = luaT_gettmbyobj(L, rb, TM_DIV)) && ttisfunction(fn) && clvalue(fn)->isC)
                    {
                        // note: it's safe to push arguments past top for complicated reasons (see top of the file)
                        LUAU_ASSERT(L->top + 3 < L->stack + L->stacksize);
                        StkId top = L->top;
                        setobj2s(L, top + 0, fn);
                        setobj2s(L, top + 1, rb);
                        setobj2s(L, top + 2, kv);
                        L->top = top + 3;

                        VM_PROTECT(luaV_callTM(L, 2, LUAU_INSN_A(insn)));
                        VM_NEXT();
                    }
                    else
                    {
                        // slow-path, may invoke C/Lua via metamethods
                        VM_PROTECT(luaV_doarithimpl<TM_DIV>(L, ra, rb, kv));
                        VM_NEXT();
                    }
                }
            }

            VM_CASE(LOP_IDIVK)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                TValue* kv = VM_KV(LUAU_INSN_C(insn));

                // fast-path
                if (LUAU_LIKELY(ttisnumber(rb)))
                {
                    setnvalue(ra, luai_numidiv(nvalue(rb), nvalue(kv)));
                    VM_NEXT();
                }
                else if (ttisvector(rb))
                {
                    const float* vb = vvalue(rb);
                    float vc = cast_to(float, nvalue(kv));
                    setvvalue(
                        ra,
                        float(luai_numidiv(vb[0], vc)),
                        float(luai_numidiv(vb[1], vc)),
                        float(luai_numidiv(vb[2], vc)),
                        float(luai_numidiv(vb[3], vc))
                    );
                    VM_NEXT();
                }
                else
                {
                    // fast-path for userdata with C functions
                    const TValue* fn = 0;
                    if (ttisuserdata(rb) && (fn = luaT_gettmbyobj(L, rb, TM_IDIV)) && ttisfunction(fn) && clvalue(fn)->isC)
                    {
                        // note: it's safe to push arguments past top for complicated reasons (see top of the file)
                        LUAU_ASSERT(L->top + 3 < L->stack + L->stacksize);
                        StkId top = L->top;
                        setobj2s(L, top + 0, fn);
                        setobj2s(L, top + 1, rb);
                        setobj2s(L, top + 2, kv);
                        L->top = top + 3;

                        VM_PROTECT(luaV_callTM(L, 2, LUAU_INSN_A(insn)));
                        VM_NEXT();
                    }
                    else
                    {
                        // slow-path, may invoke C/Lua via metamethods
                        VM_PROTECT(luaV_doarithimpl<TM_IDIV>(L, ra, rb, kv));
                        VM_NEXT();
                    }
                }
            }

            VM_CASE(LOP_MODK)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                TValue* kv = VM_KV(LUAU_INSN_C(insn));

                // fast-path
                if (ttisnumber(rb))
                {
                    double nb = nvalue(rb);
                    double nk = nvalue(kv);
                    setnvalue(ra, luai_nummod(nb, nk));
                    VM_NEXT();
                }
                else
                {
                    // slow-path, may invoke C/Lua via metamethods
                    VM_PROTECT(luaV_doarithimpl<TM_MOD>(L, ra, rb, kv));
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_POWK)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                TValue* kv = VM_KV(LUAU_INSN_C(insn));

                // fast-path
                if (ttisnumber(rb))
                {
                    double nb = nvalue(rb);
                    double nk = nvalue(kv);

                    // pow is very slow so we specialize this for ^2, ^0.5 and ^3
                    double r = (nk == 2.0) ? nb * nb : (nk == 0.5) ? sqrt(nb) : (nk == 3.0) ? nb * nb * nb : pow(nb, nk);

                    setnvalue(ra, r);
                    VM_NEXT();
                }
                else
                {
                    // slow-path, may invoke C/Lua via metamethods
                    VM_PROTECT(luaV_doarithimpl<TM_POW>(L, ra, rb, kv));
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_AND)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                StkId rc = VM_REG(LUAU_INSN_C(insn));

                setobj2s(L, ra, l_isfalse(rb) ? rb : rc);
                VM_NEXT();
            }

            VM_CASE(LOP_OR)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                StkId rc = VM_REG(LUAU_INSN_C(insn));

                setobj2s(L, ra, l_isfalse(rb) ? rc : rb);
                VM_NEXT();
            }

            VM_CASE(LOP_ANDK)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                TValue* kv = VM_KV(LUAU_INSN_C(insn));

                setobj2s(L, ra, l_isfalse(rb) ? rb : kv);
                VM_NEXT();
            }

            VM_CASE(LOP_ORK)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));
                TValue* kv = VM_KV(LUAU_INSN_C(insn));

                setobj2s(L, ra, l_isfalse(rb) ? kv : rb);
                VM_NEXT();
            }

            VM_CASE(LOP_CONCAT)
            {
                Instruction insn = *pc++;
                int b = LUAU_INSN_B(insn);
                int c = LUAU_INSN_C(insn);

                // This call may realloc the stack! So we need to query args further down
                VM_PROTECT(luaV_concat(L, c - b + 1, c));

                StkId ra = VM_REG(LUAU_INSN_A(insn));

                setobj2s(L, ra, base + b);
                VM_PROTECT(luaC_checkGC(L));
                VM_NEXT();
            }

            VM_CASE(LOP_NOT)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));

                int res = l_isfalse(rb);
                setbvalue(ra, res);
                VM_NEXT();
            }

            VM_CASE(LOP_MINUS)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));

                // fast-path
                if (LUAU_LIKELY(ttisnumber(rb)))
                {
                    setnvalue(ra, -nvalue(rb));
                    VM_NEXT();
                }
                else if (ttisvector(rb))
                {
                    const float* vb = vvalue(rb);
                    setvvalue(ra, -vb[0], -vb[1], -vb[2], -vb[3]);
                    VM_NEXT();
                }
                else
                {
                    // fast-path for userdata with C functions
                    const TValue* fn = 0;
                    if (ttisuserdata(rb) && (fn = luaT_gettmbyobj(L, rb, TM_UNM)) && ttisfunction(fn) && clvalue(fn)->isC)
                    {
                        // note: it's safe to push arguments past top for complicated reasons (see top of the file)
                        LUAU_ASSERT(L->top + 2 < L->stack + L->stacksize);
                        StkId top = L->top;
                        setobj2s(L, top + 0, fn);
                        setobj2s(L, top + 1, rb);
                        L->top = top + 2;

                        VM_PROTECT(luaV_callTM(L, 1, LUAU_INSN_A(insn)));
                        VM_NEXT();
                    }
                    else
                    {
                        // slow-path, may invoke C/Lua via metamethods
                        VM_PROTECT(luaV_doarithimpl<TM_UNM>(L, ra, rb, rb));
                        VM_NEXT();
                    }
                }
            }

            VM_CASE(LOP_LENGTH)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = VM_REG(LUAU_INSN_B(insn));

                // fast-path #1: tables
                if (LUAU_LIKELY(ttistable(rb)))
                {
                    LuaTable* h = hvalue(rb);

                    if (fastnotm(h->metatable, TM_LEN))
                    {
                        setnvalue(ra, cast_num(luaH_getn(h)));
                        VM_NEXT();
                    }
                    else
                    {
                        // slow-path, may invoke C/Lua via metamethods
                        VM_PROTECT(luaV_dolen(L, ra, rb));
                        VM_NEXT();
                    }
                }
                // fast-path #2: strings (not very important but easy to do)
                else if (ttisstring(rb))
                {
                    TString* ts = tsvalue(rb);
                    setnvalue(ra, cast_num(ts->len));
                    VM_NEXT();
                }
                else
                {
                    // slow-path, may invoke C/Lua via metamethods
                    VM_PROTECT(luaV_dolen(L, ra, rb));
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_NEWTABLE)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                int b = LUAU_INSN_B(insn);
                uint32_t aux = *pc++;

                VM_PROTECT_PC(); // luaH_new may fail due to OOM

                sethvalue(L, ra, luaH_new(L, aux, b == 0 ? 0 : (1 << (b - 1))));
                VM_PROTECT(luaC_checkGC(L));
                VM_NEXT();
            }

            VM_CASE(LOP_DUPTABLE)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                TValue* kv = VM_KV(LUAU_INSN_D(insn));

                VM_PROTECT_PC(); // luaH_clone may fail due to OOM

                sethvalue(L, ra, luaH_clone(L, hvalue(kv)));
                VM_PROTECT(luaC_checkGC(L));
                VM_NEXT();
            }

            VM_CASE(LOP_SETLIST)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                StkId rb = &base[LUAU_INSN_B(insn)]; // note: this can point to L->top if c == LUA_MULTRET making VM_REG unsafe to use
                int c = LUAU_INSN_C(insn) - 1;
                uint32_t index = *pc++;

                if (c == LUA_MULTRET)
                {
                    c = int(L->top - rb);
                    L->top = L->ci->top;
                }

                LuaTable* h = hvalue(ra);

                // TODO: we really don't need this anymore
                if (!ttistable(ra))
                    return; // temporary workaround to weaken a rather powerful exploitation primitive in case of a MITM attack on bytecode

                int last = index + c - 1;
                if (last > h->sizearray)
                {
                    VM_PROTECT_PC(); // luaH_resizearray may fail due to OOM

                    luaH_resizearray(L, h, last);
                }

                TValue* array = h->array;

                for (int i = 0; i < c; ++i)
                    setobj2t(L, &array[index + i - 1], rb + i);

                luaC_barrierfast(L, h);
                VM_NEXT();
            }

            VM_CASE(LOP_FORNPREP)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));

                if (!ttisnumber(ra + 0) || !ttisnumber(ra + 1) || !ttisnumber(ra + 2))
                {
                    // slow-path: can convert arguments to numbers and trigger Lua errors
                    // Note: this doesn't reallocate stack so we don't need to recompute ra/base
                    VM_PROTECT_PC();

                    luaV_prepareFORN(L, ra + 0, ra + 1, ra + 2);
                }

                double limit = nvalue(ra + 0);
                double step = nvalue(ra + 1);
                double idx = nvalue(ra + 2);

                // Note: make sure the loop condition is exactly the same between this and LOP_FORNLOOP so that we handle NaN/etc. consistently
                pc += (step > 0 ? idx <= limit : limit <= idx) ? 0 : LUAU_INSN_D(insn);
                LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                VM_NEXT();
            }

            VM_CASE(LOP_FORNLOOP)
            {
                VM_INTERRUPT();
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                LUAU_ASSERT(ttisnumber(ra + 0) && ttisnumber(ra + 1) && ttisnumber(ra + 2));

                double limit = nvalue(ra + 0);
                double step = nvalue(ra + 1);
                double idx = nvalue(ra + 2) + step;

                setnvalue(ra + 2, idx);

                // Note: make sure the loop condition is exactly the same between this and LOP_FORNPREP so that we handle NaN/etc. consistently
                if (step > 0 ? idx <= limit : limit <= idx)
                {
                    pc += LUAU_INSN_D(insn);
                    LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                    VM_NEXT();
                }
                else
                {
                    // fallthrough to exit
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_FORGPREP)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));

                if (ttisfunction(ra))
                {
                    // will be called during FORGLOOP
                }
                else
                {
                    LuaTable* mt = ttistable(ra) ? hvalue(ra)->metatable : ttisuserdata(ra) ? uvalue(ra)->metatable : cast_to(LuaTable*, NULL);

                    if (const TValue* fn = fasttm(L, mt, TM_ITER))
                    {
                        setobj2s(L, ra + 1, ra);
                        setobj2s(L, ra, fn);

                        L->top = ra + 2; // func + self arg
                        LUAU_ASSERT(L->top <= L->stack_last);

                        VM_PROTECT(luaD_call(L, ra, 3));
                        L->top = L->ci->top;

                        // recompute ra since stack might have been reallocated
                        ra = VM_REG(LUAU_INSN_A(insn));

                        // protect against __iter returning nil, since nil is used as a marker for builtin iteration in FORGLOOP
                        if (ttisnil(ra))
                        {
                            VM_PROTECT_PC(); // next call always errors
                            luaG_typeerror(L, ra, "call");
                        }
                    }
                    else if (fasttm(L, mt, TM_CALL))
                    {
                        // table or userdata with __call, will be called during FORGLOOP
                        // TODO: we might be able to stop supporting this depending on whether it's used in practice
                    }
                    else if (ttistable(ra))
                    {
                        // set up registers for builtin iteration
                        setobj2s(L, ra + 1, ra);
                        setpvalue(ra + 2, reinterpret_cast<void*>(uintptr_t(0)), LU_TAG_ITERATOR);
                        setnilvalue(ra);
                    }
                    else
                    {
                        VM_PROTECT_PC(); // next call always errors
                        luaG_typeerror(L, ra, "iterate over");
                    }
                }

                pc += LUAU_INSN_D(insn);
                LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                VM_NEXT();
            }

            VM_CASE(LOP_FORGLOOP)
            {
                VM_INTERRUPT();
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                uint32_t aux = *pc;

                // fast-path: builtin table iteration
                // note: ra=nil guarantees ra+1=table and ra+2=userdata because of the setup by FORGPREP* opcodes
                // TODO: remove the table check per guarantee above
                if (ttisnil(ra) && ttistable(ra + 1))
                {
                    LuaTable* h = hvalue(ra + 1);
                    int index = int(reinterpret_cast<uintptr_t>(pvalue(ra + 2)));

                    int sizearray = h->sizearray;

                    // clear extra variables since we might have more than two
                    // note: while aux encodes ipairs bit, when set we always use 2 variables, so it's safe to check this via a signed comparison
                    if (LUAU_UNLIKELY(int(aux) > 2))
                        for (int i = 2; i < int(aux); ++i)
                            setnilvalue(ra + 3 + i);

                    // terminate ipairs-style traversal early when encountering nil
                    if (int(aux) < 0 && (unsigned(index) >= unsigned(sizearray) || ttisnil(&h->array[index])))
                    {
                        pc++;
                        VM_NEXT();
                    }

                    // first we advance index through the array portion
                    while (unsigned(index) < unsigned(sizearray))
                    {
                        TValue* e = &h->array[index];

                        if (!ttisnil(e))
                        {
                            setpvalue(ra + 2, reinterpret_cast<void*>(uintptr_t(index + 1)), LU_TAG_ITERATOR);
                            setnvalue(ra + 3, double(index + 1));
                            setobj2s(L, ra + 4, e);

                            pc += LUAU_INSN_D(insn);
                            LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                            VM_NEXT();
                        }

                        index++;
                    }

                    int sizenode = 1 << h->lsizenode;

                    // then we advance index through the hash portion
                    while (unsigned(index - sizearray) < unsigned(sizenode))
                    {
                        LuaNode* n = &h->node[index - sizearray];

                        if (!ttisnil(gval(n)))
                        {
                            setpvalue(ra + 2, reinterpret_cast<void*>(uintptr_t(index + 1)), LU_TAG_ITERATOR);
                            getnodekey(L, ra + 3, n);
                            setobj2s(L, ra + 4, gval(n));

                            pc += LUAU_INSN_D(insn);
                            LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                            VM_NEXT();
                        }

                        index++;
                    }

                    // fallthrough to exit
                    pc++;
                    VM_NEXT();
                }
                else
                {
                    // note: it's safe to push arguments past top for complicated reasons (see top of the file)
                    setobj2s(L, ra + 3 + 2, ra + 2);
                    setobj2s(L, ra + 3 + 1, ra + 1);
                    setobj2s(L, ra + 3, ra);

                    L->top = ra + 3 + 3; // func + 2 args (state and index)
                    LUAU_ASSERT(L->top <= L->stack_last);

                    VM_PROTECT(luaD_call(L, ra + 3, uint8_t(aux)));
                    L->top = L->ci->top;

                    // recompute ra since stack might have been reallocated
                    ra = VM_REG(LUAU_INSN_A(insn));

                    // copy first variable back into the iteration index
                    setobj2s(L, ra + 2, ra + 3);

                    // note that we need to increment pc by 1 to exit the loop since we need to skip over aux
                    pc += ttisnil(ra + 3) ? 1 : LUAU_INSN_D(insn);
                    LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_FORGPREP_INEXT)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));

                // fast-path: ipairs/inext
                if (cl->env->safeenv && ttistable(ra + 1) && ttisnumber(ra + 2) && nvalue(ra + 2) == 0.0)
                {
                    setnilvalue(ra);
                    // ra+1 is already the table
                    setpvalue(ra + 2, reinterpret_cast<void*>(uintptr_t(0)), LU_TAG_ITERATOR);
                }
                else if (!ttisfunction(ra))
                {
                    VM_PROTECT_PC(); // next call always errors
                    luaG_typeerror(L, ra, "iterate over");
                }

                pc += LUAU_INSN_D(insn);
                LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                VM_NEXT();
            }

            VM_CASE(LOP_FORGPREP_NEXT)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));

                // fast-path: pairs/next
                if (cl->env->safeenv && ttistable(ra + 1) && ttisnil(ra + 2))
                {
                    setnilvalue(ra);
                    // ra+1 is already the table
                    setpvalue(ra + 2, reinterpret_cast<void*>(uintptr_t(0)), LU_TAG_ITERATOR);
                }
                else if (!ttisfunction(ra))
                {
                    VM_PROTECT_PC(); // next call always errors
                    luaG_typeerror(L, ra, "iterate over");
                }

                pc += LUAU_INSN_D(insn);
                LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                VM_NEXT();
            }

            VM_CASE(LOP_NATIVECALL)
            {
                Proto* p = cl->l.p;
                LUAU_ASSERT(p->execdata);

                CallInfo* ci = L->ci;
                ci->flags = LUA_CALLINFO_NATIVE;
                ci->savedpc = p->code;

#if VM_HAS_NATIVE
                if (L->global->ecb.enter(L, p) == 1)
                    goto reentry;
                else
                    goto exit;
#else
                LUAU_ASSERT(!"Opcode is only valid when VM_HAS_NATIVE is defined");
                LUAU_UNREACHABLE();
#endif
            }

            VM_CASE(LOP_GETVARARGS)
            {
                Instruction insn = *pc++;
                int b = LUAU_INSN_B(insn) - 1;
                int n = cast_int(base - L->ci->func) - cl->l.p->numparams - 1;

                if (b == LUA_MULTRET)
                {
                    VM_PROTECT(luaD_checkstack(L, n));
                    StkId ra = VM_REG(LUAU_INSN_A(insn)); // previous call may change the stack

                    for (int j = 0; j < n; j++)
                        setobj2s(L, ra + j, base - n + j);

                    L->top = ra + n;
                    VM_NEXT();
                }
                else
                {
                    StkId ra = VM_REG(LUAU_INSN_A(insn));

                    for (int j = 0; j < b && j < n; j++)
                        setobj2s(L, ra + j, base - n + j);
                    for (int j = n; j < b; j++)
                        setnilvalue(ra + j);
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_DUPCLOSURE)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                TValue* kv = VM_KV(LUAU_INSN_D(insn));

                Closure* kcl = clvalue(kv);

                VM_PROTECT_PC(); // luaF_newLclosure may fail due to OOM

                // clone closure if the environment is not shared
                // note: we save closure to stack early in case the code below wants to capture it by value
                Closure* ncl = (kcl->env == cl->env) ? kcl : luaF_newLclosure(L, kcl->nupvalues, cl->env, kcl->l.p);
                setclvalue(L, ra, ncl);

                // this loop does three things:
                // - if the closure was created anew, it just fills it with upvalues
                // - if the closure from the constant table is used, it fills it with upvalues so that it can be shared in the future
                // - if the closure is reused, it checks if the reuse is safe via rawequal, and falls back to duplicating the closure
                // normally this would use two separate loops, for reuse check and upvalue setup, but MSVC codegen goes crazy if you do that
                for (int ui = 0; ui < kcl->nupvalues; ++ui)
                {
                    Instruction uinsn = pc[ui];
                    LUAU_ASSERT(LUAU_INSN_OP(uinsn) == LOP_CAPTURE);
                    LUAU_ASSERT(LUAU_INSN_A(uinsn) == LCT_VAL || LUAU_INSN_A(uinsn) == LCT_UPVAL);

                    TValue* uv = (LUAU_INSN_A(uinsn) == LCT_VAL) ? VM_REG(LUAU_INSN_B(uinsn)) : VM_UV(LUAU_INSN_B(uinsn));

                    // check if the existing closure is safe to reuse
                    if (ncl == kcl && luaO_rawequalObj(&ncl->l.uprefs[ui], uv))
                        continue;

                    // lazily clone the closure and update the upvalues
                    if (ncl == kcl && kcl->preload == 0)
                    {
                        ncl = luaF_newLclosure(L, kcl->nupvalues, cl->env, kcl->l.p);
                        setclvalue(L, ra, ncl);

                        ui = -1; // restart the loop to fill all upvalues
                        continue;
                    }

                    // this updates a newly created closure, or an existing closure created during preload, in which case we need a barrier
                    setobj(L, &ncl->l.uprefs[ui], uv);
                    luaC_barrier(L, ncl, uv);
                }

                // this is a noop if ncl is newly created or shared successfully, but it has to run after the closure is preloaded for the first time
                ncl->preload = 0;

                if (kcl != ncl)
                    VM_PROTECT(luaC_checkGC(L));

                pc += kcl->nupvalues;
                VM_NEXT();
            }

            VM_CASE(LOP_PREPVARARGS)
            {
                Instruction insn = *pc++;
                int numparams = LUAU_INSN_A(insn);

                // all fixed parameters are copied after the top so we need more stack space
                VM_PROTECT(luaD_checkstack(L, cl->stacksize + numparams));

                // the caller must have filled extra fixed arguments with nil
                LUAU_ASSERT(cast_int(L->top - base) >= numparams);

                // move fixed parameters to final position
                StkId fixed = base; // first fixed argument
                base = L->top;      // final position of first argument

                for (int i = 0; i < numparams; ++i)
                {
                    setobj2s(L, base + i, fixed + i);
                    setnilvalue(fixed + i);
                }

                // rewire our stack frame to point to the new base
                L->ci->base = base;
                L->ci->top = base + cl->stacksize;

                L->base = base;
                L->top = L->ci->top;
                VM_NEXT();
            }

            VM_CASE(LOP_JUMPBACK)
            {
                VM_INTERRUPT();
                Instruction insn = *pc++;

                pc += LUAU_INSN_D(insn);
                LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                VM_NEXT();
            }

            VM_CASE(LOP_LOADKX)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                uint32_t aux = *pc++;
                TValue* kv = VM_KV(aux);

                setobj2s(L, ra, kv);
                VM_NEXT();
            }

            VM_CASE(LOP_JUMPX)
            {
                VM_INTERRUPT();
                Instruction insn = *pc++;

                pc += LUAU_INSN_E(insn);
                LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                VM_NEXT();
            }

            VM_CASE(LOP_FASTCALL)
            {
                Instruction insn = *pc++;
                int bfid = LUAU_INSN_A(insn);
                int skip = LUAU_INSN_C(insn);
                LUAU_ASSERT(unsigned(pc - cl->l.p->code + skip) < unsigned(cl->l.p->sizecode));

                Instruction call = pc[skip];
                LUAU_ASSERT(LUAU_INSN_OP(call) == LOP_CALL);

                StkId ra = VM_REG(LUAU_INSN_A(call));

                int nparams = LUAU_INSN_B(call) - 1;
                int nresults = LUAU_INSN_C(call) - 1;

                nparams = (nparams == LUA_MULTRET) ? int(L->top - ra - 1) : nparams;

                luau_FastFunction f = luauF_table[bfid];
                LUAU_ASSERT(f);

                if (cl->env->safeenv)
                {
                    VM_PROTECT_PC(); // f may fail due to OOM

                    int n = f(L, ra, ra + 1, nresults, ra + 2, nparams);

                    if (n >= 0)
                    {
                        // when nresults != MULTRET, L->top might be pointing to the middle of stack frame if nparams is equal to MULTRET
                        // instead of restoring L->top to L->ci->top if nparams is MULTRET, we do it unconditionally to skip an extra check
                        L->top = (nresults == LUA_MULTRET) ? ra + n : L->ci->top;

                        pc += skip + 1; // skip instructions that compute function as well as CALL
                        LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                        VM_NEXT();
                    }
                    else
                    {
                        // continue execution through the fallback code
                        VM_NEXT();
                    }
                }
                else
                {
                    // continue execution through the fallback code
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_COVERAGE)
            {
                Instruction insn = *pc++;
                int hits = LUAU_INSN_E(insn);

                // update hits with saturated add and patch the instruction in place
                hits = (hits < (1 << 23) - 1) ? hits + 1 : hits;
                VM_PATCH_E(pc - 1, hits);

                VM_NEXT();
            }

            VM_CASE(LOP_CAPTURE)
            {
                LUAU_ASSERT(!"CAPTURE is a pseudo-opcode and must be executed as part of NEWCLOSURE");
                LUAU_UNREACHABLE();
            }

            VM_CASE(LOP_SUBRK)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                TValue* kv = VM_KV(LUAU_INSN_B(insn));
                StkId rc = VM_REG(LUAU_INSN_C(insn));

                // fast-path
                if (ttisnumber(rc))
                {
                    setnvalue(ra, nvalue(kv) - nvalue(rc));
                    VM_NEXT();
                }
                else
                {
                    // slow-path, may invoke C/Lua via metamethods
                    VM_PROTECT(luaV_doarithimpl<TM_SUB>(L, ra, kv, rc));
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_DIVRK)
            {
                Instruction insn = *pc++;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                TValue* kv = VM_KV(LUAU_INSN_B(insn));
                StkId rc = VM_REG(LUAU_INSN_C(insn));

                // fast-path
                if (LUAU_LIKELY(ttisnumber(rc)))
                {
                    setnvalue(ra, nvalue(kv) / nvalue(rc));
                    VM_NEXT();
                }
                else if (ttisvector(rc))
                {
                    float nb = cast_to(float, nvalue(kv));
                    const float* vc = vvalue(rc);
                    setvvalue(ra, nb / vc[0], nb / vc[1], nb / vc[2], nb / vc[3]);
                    VM_NEXT();
                }
                else
                {
                    // slow-path, may invoke C/Lua via metamethods
                    VM_PROTECT(luaV_doarithimpl<TM_DIV>(L, ra, kv, rc));
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_FASTCALL1)
            {
                Instruction insn = *pc++;
                int bfid = LUAU_INSN_A(insn);
                TValue* arg = VM_REG(LUAU_INSN_B(insn));
                int skip = LUAU_INSN_C(insn);

                LUAU_ASSERT(unsigned(pc - cl->l.p->code + skip) < unsigned(cl->l.p->sizecode));

                Instruction call = pc[skip];
                LUAU_ASSERT(LUAU_INSN_OP(call) == LOP_CALL);

                StkId ra = VM_REG(LUAU_INSN_A(call));

                int nparams = 1;
                int nresults = LUAU_INSN_C(call) - 1;

                luau_FastFunction f = luauF_table[bfid];
                LUAU_ASSERT(f);

                if (cl->env->safeenv)
                {
                    VM_PROTECT_PC(); // f may fail due to OOM

                    int n = f(L, ra, arg, nresults, NULL, nparams);

                    if (n >= 0)
                    {
                        if (nresults == LUA_MULTRET)
                            L->top = ra + n;

                        pc += skip + 1; // skip instructions that compute function as well as CALL
                        LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                        VM_NEXT();
                    }
                    else
                    {
                        // continue execution through the fallback code
                        VM_NEXT();
                    }
                }
                else
                {
                    // continue execution through the fallback code
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_FASTCALL2)
            {
                Instruction insn = *pc++;
                int bfid = LUAU_INSN_A(insn);
                int skip = LUAU_INSN_C(insn) - 1;
                uint32_t aux = *pc++;
                TValue* arg1 = VM_REG(LUAU_INSN_B(insn));
                TValue* arg2 = VM_REG(aux);

                LUAU_ASSERT(unsigned(pc - cl->l.p->code + skip) < unsigned(cl->l.p->sizecode));

                Instruction call = pc[skip];
                LUAU_ASSERT(LUAU_INSN_OP(call) == LOP_CALL);

                StkId ra = VM_REG(LUAU_INSN_A(call));

                int nparams = 2;
                int nresults = LUAU_INSN_C(call) - 1;

                luau_FastFunction f = luauF_table[bfid];
                LUAU_ASSERT(f);

                if (cl->env->safeenv)
                {
                    VM_PROTECT_PC(); // f may fail due to OOM

                    int n = f(L, ra, arg1, nresults, arg2, nparams);

                    if (n >= 0)
                    {
                        if (nresults == LUA_MULTRET)
                            L->top = ra + n;

                        pc += skip + 1; // skip instructions that compute function as well as CALL
                        LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                        VM_NEXT();
                    }
                    else
                    {
                        // continue execution through the fallback code
                        VM_NEXT();
                    }
                }
                else
                {
                    // continue execution through the fallback code
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_FASTCALL2K)
            {
                Instruction insn = *pc++;
                int bfid = LUAU_INSN_A(insn);
                int skip = LUAU_INSN_C(insn) - 1;
                uint32_t aux = *pc++;
                TValue* arg1 = VM_REG(LUAU_INSN_B(insn));
                TValue* arg2 = VM_KV(aux);

                LUAU_ASSERT(unsigned(pc - cl->l.p->code + skip) < unsigned(cl->l.p->sizecode));

                Instruction call = pc[skip];
                LUAU_ASSERT(LUAU_INSN_OP(call) == LOP_CALL);

                StkId ra = VM_REG(LUAU_INSN_A(call));

                int nparams = 2;
                int nresults = LUAU_INSN_C(call) - 1;

                luau_FastFunction f = luauF_table[bfid];
                LUAU_ASSERT(f);

                if (cl->env->safeenv)
                {
                    VM_PROTECT_PC(); // f may fail due to OOM

                    int n = f(L, ra, arg1, nresults, arg2, nparams);

                    if (n >= 0)
                    {
                        if (nresults == LUA_MULTRET)
                            L->top = ra + n;

                        pc += skip + 1; // skip instructions that compute function as well as CALL
                        LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                        VM_NEXT();
                    }
                    else
                    {
                        // continue execution through the fallback code
                        VM_NEXT();
                    }
                }
                else
                {
                    // continue execution through the fallback code
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_FASTCALL3)
            {
                Instruction insn = *pc++;
                int bfid = LUAU_INSN_A(insn);
                int skip = LUAU_INSN_C(insn) - 1;
                uint32_t aux = *pc++;
                TValue* arg1 = VM_REG(LUAU_INSN_B(insn));
                TValue* arg2 = VM_REG(aux & 0xff);
                TValue* arg3 = VM_REG((aux >> 8) & 0xff);

                LUAU_ASSERT(unsigned(pc - cl->l.p->code + skip) < unsigned(cl->l.p->sizecode));

                Instruction call = pc[skip];
                LUAU_ASSERT(LUAU_INSN_OP(call) == LOP_CALL);

                StkId ra = VM_REG(LUAU_INSN_A(call));

                int nparams = 3;
                int nresults = LUAU_INSN_C(call) - 1;

                luau_FastFunction f = luauF_table[bfid];
                LUAU_ASSERT(f);

                if (cl->env->safeenv)
                {
                    VM_PROTECT_PC(); // f may fail due to OOM

                    // note: it's safe to push arguments past top for complicated reasons (see top of the file)
                    LUAU_ASSERT(L->top + 2 < L->stack + L->stacksize);
                    StkId top = L->top;
                    setobj2s(L, top, arg2);
                    setobj2s(L, top + 1, arg3);

                    int n = f(L, ra, arg1, nresults, top, nparams);

                    if (n >= 0)
                    {
                        if (nresults == LUA_MULTRET)
                            L->top = ra + n;

                        pc += skip + 1; // skip instructions that compute function as well as CALL
                        LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                        VM_NEXT();
                    }
                    else
                    {
                        // continue execution through the fallback code
                        VM_NEXT();
                    }
                }
                else
                {
                    // continue execution through the fallback code
                    VM_NEXT();
                }
            }

            VM_CASE(LOP_BREAK)
            {
                LUAU_ASSERT(cl->l.p->debuginsn);

                uint8_t op = cl->l.p->debuginsn[unsigned(pc - cl->l.p->code)];
                LUAU_ASSERT(op != LOP_BREAK);

                if (L->global->cb.debugbreak)
                {
                    VM_PROTECT(luau_callhook(L, L->global->cb.debugbreak, NULL));

                    // allow debugbreak hook to put thread into error/yield state
                    if (L->status != 0)
                        goto exit;
                }

                VM_CONTINUE(op);
            }

            VM_CASE(LOP_JUMPXEQKNIL)
            {
                Instruction insn = *pc++;
                uint32_t aux = *pc;
                StkId ra = VM_REG(LUAU_INSN_A(insn));

                static_assert(LUA_TNIL == 0, "we expect type-1 to be negative iff type is nil");
                // condition is equivalent to: int(ttisnil(ra)) != (aux >> 31)
                pc += int((ttype(ra) - 1) ^ aux) < 0 ? LUAU_INSN_D(insn) : 1;
                LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                VM_NEXT();
            }

            VM_CASE(LOP_JUMPXEQKB)
            {
                Instruction insn = *pc++;
                uint32_t aux = *pc;
                StkId ra = VM_REG(LUAU_INSN_A(insn));

                pc += int(ttisboolean(ra) && bvalue(ra) == int(aux & 1)) != (aux >> 31) ? LUAU_INSN_D(insn) : 1;
                LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                VM_NEXT();
            }

            VM_CASE(LOP_JUMPXEQKN)
            {
                Instruction insn = *pc++;
                uint32_t aux = *pc;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                TValue* kv = VM_KV(aux & 0xffffff);
                LUAU_ASSERT(ttisnumber(kv));

#if defined(__aarch64__)
                // On several ARM chips (Apple M1/M2, Neoverse N1), comparing the result of a floating-point comparison is expensive, and a branch
                // is much cheaper; on some 32-bit ARM chips (Cortex A53) the performance is about the same so we prefer less branchy variant there
                if (aux >> 31)
                    pc += !(ttisnumber(ra) && nvalue(ra) == nvalue(kv)) ? LUAU_INSN_D(insn) : 1;
                else
                    pc += (ttisnumber(ra) && nvalue(ra) == nvalue(kv)) ? LUAU_INSN_D(insn) : 1;
#else
                pc += int(ttisnumber(ra) && nvalue(ra) == nvalue(kv)) != (aux >> 31) ? LUAU_INSN_D(insn) : 1;
#endif
                LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                VM_NEXT();
            }

            VM_CASE(LOP_JUMPXEQKS)
            {
                Instruction insn = *pc++;
                uint32_t aux = *pc;
                StkId ra = VM_REG(LUAU_INSN_A(insn));
                TValue* kv = VM_KV(aux & 0xffffff);
                LUAU_ASSERT(ttisstring(kv));

                pc += int(ttisstring(ra) && gcvalue(ra) == gcvalue(kv)) != (aux >> 31) ? LUAU_INSN_D(insn) : 1;
                LUAU_ASSERT(unsigned(pc - cl->l.p->code) < unsigned(cl->l.p->sizecode));
                VM_NEXT();
            }

#if !VM_USE_CGOTO
        default:
            LUAU_ASSERT(!"Unknown opcode");
            LUAU_UNREACHABLE(); // improves switch() codegen by eliding opcode bounds checks
#endif
        }
    }

exit:;
}

void luau_execute(lua_State* L)
{
   using _Luau_Execute = void(__fastcall*)(lua_State*);
    auto Luau_Execute = (_Luau_Execute)Update::LuaVM::Luau_Execute;

    Luau_Execute(L);
}

int luau_precall(lua_State* L, StkId func, int nresults)
{
    if (!ttisfunction(func))
    {
        luaV_tryfuncTM(L, func);
        // L->top is incremented by tryfuncTM
    }

    Closure* ccl = clvalue(func);

    CallInfo* ci = incr_ci(L);
    ci->func = func;
    ci->base = func + 1;
    ci->top = L->top + ccl->stacksize;
    ci->savedpc = NULL;
    ci->flags = 0;
    ci->nresults = nresults;

    L->base = ci->base;
    // Note: L->top is assigned externally

    luaD_checkstackfornewci(L, ccl->stacksize);
    LUAU_ASSERT(ci->top <= L->stack_last);

    if (!ccl->isC)
    {
        Proto* p = ccl->l.p;

        // fill unused parameters with nil
        StkId argi = L->top;
        StkId argend = L->base + p->numparams;
        while (argi < argend)
            setnilvalue(argi++); // complete missing arguments
        L->top = p->is_vararg ? argi : ci->top;

        ci->savedpc = p->code;

#if VM_HAS_NATIVE
        if (p->exectarget != 0 && p->execdata)
            ci->flags = LUA_CALLINFO_NATIVE;
#endif

        return PCRLUA;
    }
    else
    {
        lua_CFunction func = ccl->c.f;
        int n = func(L);

        // yield
        if (n < 0)
            return PCRYIELD;

        // ci is our callinfo, cip is our parent
        CallInfo* ci = L->ci;
        CallInfo* cip = ci - 1;

        // copy return values into parent stack (but only up to nresults!), fill the rest with nil
        // TODO: it might be worthwhile to handle the case when nresults==b explicitly?
        StkId res = ci->func;
        StkId vali = L->top - n;
        StkId valend = L->top;

        int i;
        for (i = nresults; i != 0 && vali < valend; i--)
            setobj2s(L, res++, vali++);
        while (i-- > 0)
            setnilvalue(res++);

        // pop the stack frame
        L->ci = cip;
        L->base = cip->base;
        L->top = res;

        return PCRC;
    }
}

void luau_poscall(lua_State* L, StkId first)
{
    // finish interrupted execution of `OP_CALL'
    // ci is our callinfo, cip is our parent
    CallInfo* ci = L->ci;
    CallInfo* cip = ci - 1;

    // copy return values into parent stack (but only up to nresults!), fill the rest with nil
    // TODO: it might be worthwhile to handle the case when nresults==b explicitly?
    StkId res = ci->func;
    StkId vali = first;
    StkId valend = L->top;

    int i;
    for (i = ci->nresults; i != 0 && vali < valend; i--)
        setobj2s(L, res++, vali++);
    while (i-- > 0)
        setnilvalue(res++);

    // pop the stack frame
    L->ci = cip;
    L->base = cip->base;
    L->top = (ci->nresults == LUA_MULTRET) ? res : cip->top;
}
