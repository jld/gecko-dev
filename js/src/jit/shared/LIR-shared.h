/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_shared_LIR_shared_h
#define jit_shared_LIR_shared_h

#include "mozilla/Maybe.h"
#include "jit/AtomicOp.h"
#include "jit/shared/Assembler-shared.h"
#include "util/Memory.h"

// This file declares LIR instructions that are common to every platform.

namespace js {
namespace jit {

LIR_OPCODE_CLASS_GENERATED

class LBox : public LInstructionHelper<BOX_PIECES, 1, 0> {
  MIRType type_;

 public:
  LIR_HEADER(Box);

  LBox(const LAllocation& payload, MIRType type)
      : LInstructionHelper(classOpcode), type_(type) {
    setOperand(0, payload);
  }

  MIRType type() const { return type_; }
  const char* extraName() const { return StringFromMIRType(type_); }
};

template <size_t Temps, size_t ExtraUses = 0>
class LBinaryMath : public LInstructionHelper<1, 2 + ExtraUses, Temps> {
 protected:
  explicit LBinaryMath(LNode::Opcode opcode)
      : LInstructionHelper<1, 2 + ExtraUses, Temps>(opcode) {}

 public:
  const LAllocation* lhs() { return this->getOperand(0); }
  const LAllocation* rhs() { return this->getOperand(1); }
};

// An LOsiPoint captures a snapshot after a call and ensures enough space to
// patch in a call to the invalidation mechanism.
//
// Note: LSafepoints are 1:1 with LOsiPoints, so it holds a reference to the
// corresponding LSafepoint to inform it of the LOsiPoint's masm offset when it
// gets GC'd.
class LOsiPoint : public LInstructionHelper<0, 0, 0> {
  LSafepoint* safepoint_;

 public:
  LOsiPoint(LSafepoint* safepoint, LSnapshot* snapshot)
      : LInstructionHelper(classOpcode), safepoint_(safepoint) {
    MOZ_ASSERT(safepoint && snapshot);
    assignSnapshot(snapshot);
  }

  LSafepoint* associatedSafepoint() { return safepoint_; }

  LIR_HEADER(OsiPoint)
};

class LMove {
  LAllocation from_;
  LAllocation to_;
  LDefinition::Type type_;

 public:
  LMove(LAllocation from, LAllocation to, LDefinition::Type type)
      : from_(from), to_(to), type_(type) {}

  LAllocation from() const { return from_; }
  LAllocation to() const { return to_; }
  LDefinition::Type type() const { return type_; }
};

class LMoveGroup : public LInstructionHelper<0, 0, 0> {
  js::Vector<LMove, 2, JitAllocPolicy> moves_;

#ifdef JS_CODEGEN_X86
  // Optional general register available for use when executing moves.
  LAllocation scratchRegister_;
#endif

  explicit LMoveGroup(TempAllocator& alloc)
      : LInstructionHelper(classOpcode), moves_(alloc) {}

 public:
  LIR_HEADER(MoveGroup)

  static LMoveGroup* New(TempAllocator& alloc) {
    return new (alloc) LMoveGroup(alloc);
  }

  void printOperands(GenericPrinter& out);

  // Add a move which takes place simultaneously with all others in the group.
  bool add(LAllocation from, LAllocation to, LDefinition::Type type);

  // Add a move which takes place after existing moves in the group.
  bool addAfter(LAllocation from, LAllocation to, LDefinition::Type type);

  size_t numMoves() const { return moves_.length(); }
  const LMove& getMove(size_t i) const { return moves_[i]; }

#ifdef JS_CODEGEN_X86
  void setScratchRegister(Register reg) { scratchRegister_ = LGeneralReg(reg); }
  LAllocation maybeScratchRegister() { return scratchRegister_; }
#endif

  bool uses(Register reg) {
    for (size_t i = 0; i < numMoves(); i++) {
      LMove move = getMove(i);
      if (move.from() == LGeneralReg(reg) || move.to() == LGeneralReg(reg)) {
        return true;
      }
    }
    return false;
  }
};

// A constant Value.
class LValue : public LInstructionHelper<BOX_PIECES, 0, 0> {
  Value v_;

 public:
  LIR_HEADER(Value)

  explicit LValue(const Value& v) : LInstructionHelper(classOpcode), v_(v) {}

  Value value() const { return v_; }
};

class LNewArray : public LInstructionHelper<1, 0, 1> {
 public:
  LIR_HEADER(NewArray)

  explicit LNewArray(const LDefinition& temp)
      : LInstructionHelper(classOpcode) {
    setTemp(0, temp);
  }

  const char* extraName() const {
    return mir()->isVMCall() ? "VMCall" : nullptr;
  }

  const LDefinition* temp() { return getTemp(0); }

  MNewArray* mir() const { return mir_->toNewArray(); }
};

class LNewObject : public LInstructionHelper<1, 0, 1> {
 public:
  LIR_HEADER(NewObject)

  explicit LNewObject(const LDefinition& temp)
      : LInstructionHelper(classOpcode) {
    setTemp(0, temp);
  }

  const char* extraName() const {
    return mir()->isVMCall() ? "VMCall" : nullptr;
  }

  const LDefinition* temp() { return getTemp(0); }

  MNewObject* mir() const { return mir_->toNewObject(); }
};

namespace details {
template <size_t Defs, size_t Ops, size_t Temps>
class RotateBase : public LInstructionHelper<Defs, Ops, Temps> {
  using Base = LInstructionHelper<Defs, Ops, Temps>;

 protected:
  explicit RotateBase(LNode::Opcode opcode) : Base(opcode) {}

 public:
  MRotate* mir() { return Base::mir_->toRotate(); }
};
}  // namespace details

class LRotate : public details::RotateBase<1, 2, 0> {
 public:
  LIR_HEADER(Rotate);

  LRotate() : RotateBase(classOpcode) {}

  const LAllocation* input() { return getOperand(0); }
  LAllocation* count() { return getOperand(1); }
};

class LRotateI64
    : public details::RotateBase<INT64_PIECES, INT64_PIECES + 1, 1> {
 public:
  LIR_HEADER(RotateI64);

  LRotateI64() : RotateBase(classOpcode) {
    setTemp(0, LDefinition::BogusTemp());
  }

  static const size_t Input = 0;
  static const size_t Count = INT64_PIECES;

  const LInt64Allocation input() { return getInt64Operand(Input); }
  const LDefinition* temp() { return getTemp(0); }
  LAllocation* count() { return getOperand(Count); }
};

// Allocate a new arguments object for an inlined frame.
class LCreateInlinedArgumentsObject : public LVariadicInstruction<1, 2> {
 public:
  LIR_HEADER(CreateInlinedArgumentsObject)

  static const size_t CallObj = 0;
  static const size_t Callee = 1;
  static const size_t NumNonArgumentOperands = 2;
  static size_t ArgIndex(size_t i) {
    return NumNonArgumentOperands + BOX_PIECES * i;
  }

  LCreateInlinedArgumentsObject(uint32_t numOperands, const LDefinition& temp1,
                                const LDefinition& temp2)
      : LVariadicInstruction(classOpcode, numOperands) {
    setIsCall();
    setTemp(0, temp1);
    setTemp(1, temp2);
  }

  const LAllocation* getCallObject() { return getOperand(CallObj); }
  const LAllocation* getCallee() { return getOperand(Callee); }

  const LDefinition* temp1() { return getTemp(0); }
  const LDefinition* temp2() { return getTemp(1); }

  MCreateInlinedArgumentsObject* mir() const {
    return mir_->toCreateInlinedArgumentsObject();
  }
};

class LGetInlinedArgument : public LVariadicInstruction<BOX_PIECES, 0> {
 public:
  LIR_HEADER(GetInlinedArgument)

  static const size_t Index = 0;
  static const size_t NumNonArgumentOperands = 1;
  static size_t ArgIndex(size_t i) {
    return NumNonArgumentOperands + BOX_PIECES * i;
  }

  explicit LGetInlinedArgument(uint32_t numOperands)
      : LVariadicInstruction(classOpcode, numOperands) {}

  const LAllocation* getIndex() { return getOperand(Index); }

  MGetInlinedArgument* mir() const { return mir_->toGetInlinedArgument(); }
};

class LGetInlinedArgumentHole : public LVariadicInstruction<BOX_PIECES, 0> {
 public:
  LIR_HEADER(GetInlinedArgumentHole)

  static const size_t Index = 0;
  static const size_t NumNonArgumentOperands = 1;
  static size_t ArgIndex(size_t i) {
    return NumNonArgumentOperands + BOX_PIECES * i;
  }

  explicit LGetInlinedArgumentHole(uint32_t numOperands)
      : LVariadicInstruction(classOpcode, numOperands) {}

  const LAllocation* getIndex() { return getOperand(Index); }

  MGetInlinedArgumentHole* mir() const {
    return mir_->toGetInlinedArgumentHole();
  }
};

class LInlineArgumentsSlice : public LVariadicInstruction<1, 1> {
 public:
  LIR_HEADER(InlineArgumentsSlice)

  static const size_t Begin = 0;
  static const size_t Count = 1;
  static const size_t NumNonArgumentOperands = 2;
  static size_t ArgIndex(size_t i) {
    return NumNonArgumentOperands + BOX_PIECES * i;
  }

  explicit LInlineArgumentsSlice(uint32_t numOperands, const LDefinition& temp)
      : LVariadicInstruction(classOpcode, numOperands) {
    setTemp(0, temp);
  }

  const LAllocation* begin() { return getOperand(Begin); }
  const LAllocation* count() { return getOperand(Count); }
  const LDefinition* temp() { return getTemp(0); }

  MInlineArgumentsSlice* mir() const { return mir_->toInlineArgumentsSlice(); }
};

// Common code for LIR descended from MCall.
template <size_t Defs, size_t Operands, size_t Temps>
class LJSCallInstructionHelper
    : public LCallInstructionHelper<Defs, Operands, Temps> {
 protected:
  explicit LJSCallInstructionHelper(LNode::Opcode opcode)
      : LCallInstructionHelper<Defs, Operands, Temps>(opcode) {}

 public:
  MCall* mir() const { return this->mir_->toCall(); }

  bool hasSingleTarget() const { return getSingleTarget() != nullptr; }
  WrappedFunction* getSingleTarget() const { return mir()->getSingleTarget(); }

  // Does not include |this|.
  uint32_t numActualArgs() const { return mir()->numActualArgs(); }

  bool isConstructing() const { return mir()->isConstructing(); }
  bool ignoresReturnValue() const { return mir()->ignoresReturnValue(); }
};

// Generates a polymorphic callsite, wherein the function being called is
// unknown and anticipated to vary.
class LCallGeneric : public LJSCallInstructionHelper<BOX_PIECES, 1, 1> {
 public:
  LIR_HEADER(CallGeneric)

  LCallGeneric(const LAllocation& callee, const LDefinition& argc)
      : LJSCallInstructionHelper(classOpcode) {
    setOperand(0, callee);
    setTemp(0, argc);
  }

  const LAllocation* getCallee() { return getOperand(0); }
  const LDefinition* getArgc() { return getTemp(0); }
};

// Generates a hardcoded callsite for a known, non-native target.
class LCallKnown : public LJSCallInstructionHelper<BOX_PIECES, 1, 1> {
 public:
  LIR_HEADER(CallKnown)

  LCallKnown(const LAllocation& func, const LDefinition& tmpobjreg)
      : LJSCallInstructionHelper(classOpcode) {
    setOperand(0, func);
    setTemp(0, tmpobjreg);
  }

  const LAllocation* getFunction() { return getOperand(0); }
  const LDefinition* getTempObject() { return getTemp(0); }
};

// Generates a hardcoded callsite for a known, native target.
class LCallNative : public LJSCallInstructionHelper<BOX_PIECES, 0, 4> {
 public:
  LIR_HEADER(CallNative)

  LCallNative(const LDefinition& argContext, const LDefinition& argUintN,
              const LDefinition& argVp, const LDefinition& tmpreg)
      : LJSCallInstructionHelper(classOpcode) {
    // Registers used for callWithABI().
    setTemp(0, argContext);
    setTemp(1, argUintN);
    setTemp(2, argVp);

    // Temporary registers.
    setTemp(3, tmpreg);
  }

  const LDefinition* getArgContextReg() { return getTemp(0); }
  const LDefinition* getArgUintNReg() { return getTemp(1); }
  const LDefinition* getArgVpReg() { return getTemp(2); }
  const LDefinition* getTempReg() { return getTemp(3); }
};

class LCallClassHook : public LCallInstructionHelper<BOX_PIECES, 1, 4> {
 public:
  LIR_HEADER(CallClassHook)

  LCallClassHook(const LAllocation& callee, const LDefinition& argContext,
                 const LDefinition& argUintN, const LDefinition& argVp,
                 const LDefinition& tmpreg)
      : LCallInstructionHelper(classOpcode) {
    setOperand(0, callee);

    // Registers used for callWithABI().
    setTemp(0, argContext);
    setTemp(1, argUintN);
    setTemp(2, argVp);

    // Temporary registers.
    setTemp(3, tmpreg);
  }

  MCallClassHook* mir() const { return mir_->toCallClassHook(); }

  const LAllocation* getCallee() { return this->getOperand(0); }

  const LDefinition* getArgContextReg() { return getTemp(0); }
  const LDefinition* getArgUintNReg() { return getTemp(1); }
  const LDefinition* getArgVpReg() { return getTemp(2); }
  const LDefinition* getTempReg() { return getTemp(3); }
};

// Generates a hardcoded callsite for a known, DOM-native target.
class LCallDOMNative : public LJSCallInstructionHelper<BOX_PIECES, 0, 4> {
 public:
  LIR_HEADER(CallDOMNative)

  LCallDOMNative(const LDefinition& argJSContext, const LDefinition& argObj,
                 const LDefinition& argPrivate, const LDefinition& argArgs)
      : LJSCallInstructionHelper(classOpcode) {
    setTemp(0, argJSContext);
    setTemp(1, argObj);
    setTemp(2, argPrivate);
    setTemp(3, argArgs);
  }

  const LDefinition* getArgJSContext() { return getTemp(0); }
  const LDefinition* getArgObj() { return getTemp(1); }
  const LDefinition* getArgPrivate() { return getTemp(2); }
  const LDefinition* getArgArgs() { return getTemp(3); }
};

class LUnreachable : public LControlInstructionHelper<0, 0, 0> {
 public:
  LIR_HEADER(Unreachable)

  LUnreachable() : LControlInstructionHelper(classOpcode) {}
};

class LUnreachableResultV : public LInstructionHelper<BOX_PIECES, 0, 0> {
 public:
  LIR_HEADER(UnreachableResultV)

  LUnreachableResultV() : LInstructionHelper(classOpcode) {}
};

// Generates a polymorphic callsite, wherein the function being called is
// unknown and anticipated to vary.
class LApplyArgsGeneric
    : public LCallInstructionHelper<BOX_PIECES, BOX_PIECES + 2, 2> {
 public:
  LIR_HEADER(ApplyArgsGeneric)

  LApplyArgsGeneric(const LAllocation& func, const LAllocation& argc,
                    const LBoxAllocation& thisv, const LDefinition& tmpObjReg,
                    const LDefinition& tmpCopy)
      : LCallInstructionHelper(classOpcode) {
    setOperand(0, func);
    setOperand(1, argc);
    setBoxOperand(ThisIndex, thisv);
    setTemp(0, tmpObjReg);
    setTemp(1, tmpCopy);
  }

  MApplyArgs* mir() const { return mir_->toApplyArgs(); }

  bool hasSingleTarget() const { return getSingleTarget() != nullptr; }
  WrappedFunction* getSingleTarget() const { return mir()->getSingleTarget(); }

  uint32_t numExtraFormals() const { return mir()->numExtraFormals(); }

  const LAllocation* getFunction() { return getOperand(0); }
  const LAllocation* getArgc() { return getOperand(1); }
  static const size_t ThisIndex = 2;

  const LDefinition* getTempObject() { return getTemp(0); }
  const LDefinition* getTempForArgCopy() { return getTemp(1); }
};

class LApplyArgsObj
    : public LCallInstructionHelper<BOX_PIECES, BOX_PIECES + 2, 2> {
 public:
  LIR_HEADER(ApplyArgsObj)

  LApplyArgsObj(const LAllocation& func, const LAllocation& argsObj,
                const LBoxAllocation& thisv, const LDefinition& tmpObjReg,
                const LDefinition& tmpCopy)
      : LCallInstructionHelper(classOpcode) {
    setOperand(0, func);
    setOperand(1, argsObj);
    setBoxOperand(ThisIndex, thisv);
    setTemp(0, tmpObjReg);
    setTemp(1, tmpCopy);
  }

  MApplyArgsObj* mir() const { return mir_->toApplyArgsObj(); }

  bool hasSingleTarget() const { return getSingleTarget() != nullptr; }
  WrappedFunction* getSingleTarget() const { return mir()->getSingleTarget(); }

  const LAllocation* getFunction() { return getOperand(0); }
  const LAllocation* getArgsObj() { return getOperand(1); }
  // All registers are calltemps. argc is mapped to the same register as
  // ArgsObj. argc becomes live as ArgsObj is dying.
  const LAllocation* getArgc() { return getOperand(1); }
  static const size_t ThisIndex = 2;

  const LDefinition* getTempObject() { return getTemp(0); }
  const LDefinition* getTempForArgCopy() { return getTemp(1); }
};

class LApplyArrayGeneric
    : public LCallInstructionHelper<BOX_PIECES, BOX_PIECES + 2, 2> {
 public:
  LIR_HEADER(ApplyArrayGeneric)

  LApplyArrayGeneric(const LAllocation& func, const LAllocation& elements,
                     const LBoxAllocation& thisv, const LDefinition& tmpObjReg,
                     const LDefinition& tmpCopy)
      : LCallInstructionHelper(classOpcode) {
    setOperand(0, func);
    setOperand(1, elements);
    setBoxOperand(ThisIndex, thisv);
    setTemp(0, tmpObjReg);
    setTemp(1, tmpCopy);
  }

  MApplyArray* mir() const { return mir_->toApplyArray(); }

  bool hasSingleTarget() const { return getSingleTarget() != nullptr; }
  WrappedFunction* getSingleTarget() const { return mir()->getSingleTarget(); }

  const LAllocation* getFunction() { return getOperand(0); }
  const LAllocation* getElements() { return getOperand(1); }
  // argc is mapped to the same register as elements: argc becomes
  // live as elements is dying, all registers are calltemps.
  const LAllocation* getArgc() { return getOperand(1); }
  static const size_t ThisIndex = 2;

  const LDefinition* getTempObject() { return getTemp(0); }
  const LDefinition* getTempForArgCopy() { return getTemp(1); }
};

class LConstructArgsGeneric
    : public LCallInstructionHelper<BOX_PIECES, BOX_PIECES + 3, 1> {
 public:
  LIR_HEADER(ConstructArgsGeneric)

  LConstructArgsGeneric(const LAllocation& func, const LAllocation& argc,
                        const LAllocation& newTarget,
                        const LBoxAllocation& thisv,
                        const LDefinition& tmpObjReg)
      : LCallInstructionHelper(classOpcode) {
    setOperand(0, func);
    setOperand(1, argc);
    setOperand(2, newTarget);
    setBoxOperand(ThisIndex, thisv);
    setTemp(0, tmpObjReg);
  }

  MConstructArgs* mir() const { return mir_->toConstructArgs(); }

  bool hasSingleTarget() const { return getSingleTarget() != nullptr; }
  WrappedFunction* getSingleTarget() const { return mir()->getSingleTarget(); }

  uint32_t numExtraFormals() const { return mir()->numExtraFormals(); }

  const LAllocation* getFunction() { return getOperand(0); }
  const LAllocation* getArgc() { return getOperand(1); }
  const LAllocation* getNewTarget() { return getOperand(2); }

  static const size_t ThisIndex = 3;

  const LDefinition* getTempObject() { return getTemp(0); }

  // tempForArgCopy is mapped to the same register as newTarget:
  // tempForArgCopy becomes live as newTarget is dying, all registers are
  // calltemps.
  const LAllocation* getTempForArgCopy() { return getOperand(2); }
};

class LConstructArrayGeneric
    : public LCallInstructionHelper<BOX_PIECES, BOX_PIECES + 3, 1> {
 public:
  LIR_HEADER(ConstructArrayGeneric)

  LConstructArrayGeneric(const LAllocation& func, const LAllocation& elements,
                         const LAllocation& newTarget,
                         const LBoxAllocation& thisv,
                         const LDefinition& tmpObjReg)
      : LCallInstructionHelper(classOpcode) {
    setOperand(0, func);
    setOperand(1, elements);
    setOperand(2, newTarget);
    setBoxOperand(ThisIndex, thisv);
    setTemp(0, tmpObjReg);
  }

  MConstructArray* mir() const { return mir_->toConstructArray(); }

  bool hasSingleTarget() const { return getSingleTarget() != nullptr; }
  WrappedFunction* getSingleTarget() const { return mir()->getSingleTarget(); }

  const LAllocation* getFunction() { return getOperand(0); }
  const LAllocation* getElements() { return getOperand(1); }
  const LAllocation* getNewTarget() { return getOperand(2); }

  static const size_t ThisIndex = 3;

  const LDefinition* getTempObject() { return getTemp(0); }

  // argc is mapped to the same register as elements: argc becomes
  // live as elements is dying, all registers are calltemps.
  const LAllocation* getArgc() { return getOperand(1); }

  // tempForArgCopy is mapped to the same register as newTarget:
  // tempForArgCopy becomes live as newTarget is dying, all registers are
  // calltemps.
  const LAllocation* getTempForArgCopy() { return getOperand(2); }
};

class LApplyArgsNative
    : public LCallInstructionHelper<BOX_PIECES, BOX_PIECES + 1, 3> {
 public:
  LIR_HEADER(ApplyArgsNative)

  LApplyArgsNative(const LAllocation& argc, const LBoxAllocation& thisv,
                   const LDefinition& tmpObjReg, const LDefinition& tmpCopy,
                   const LDefinition& tmpExtra)
      : LCallInstructionHelper(classOpcode) {
    setOperand(0, argc);
    setBoxOperand(ThisIndex, thisv);
    setTemp(0, tmpObjReg);
    setTemp(1, tmpCopy);
    setTemp(2, tmpExtra);
  }

  static constexpr bool isConstructing() { return false; }

  MApplyArgs* mir() const { return mir_->toApplyArgs(); }

  uint32_t numExtraFormals() const { return mir()->numExtraFormals(); }

  const LAllocation* getArgc() { return getOperand(0); }

  static const size_t ThisIndex = 1;

  const LDefinition* getTempObject() { return getTemp(0); }
  const LDefinition* getTempForArgCopy() { return getTemp(1); }
  const LDefinition* getTempExtra() { return getTemp(2); }
};

class LApplyArgsObjNative
    : public LCallInstructionHelper<BOX_PIECES, BOX_PIECES + 1, 3> {
 public:
  LIR_HEADER(ApplyArgsObjNative)

  LApplyArgsObjNative(const LAllocation& argsObj, const LBoxAllocation& thisv,
                      const LDefinition& tmpObjReg, const LDefinition& tmpCopy,
                      const LDefinition& tmpExtra)
      : LCallInstructionHelper(classOpcode) {
    setOperand(0, argsObj);
    setBoxOperand(ThisIndex, thisv);
    setTemp(0, tmpObjReg);
    setTemp(1, tmpCopy);
    setTemp(2, tmpExtra);
  }

  static constexpr bool isConstructing() { return false; }

  MApplyArgsObj* mir() const { return mir_->toApplyArgsObj(); }

  const LAllocation* getArgsObj() { return getOperand(0); }

  static const size_t ThisIndex = 1;

  const LDefinition* getTempObject() { return getTemp(0); }
  const LDefinition* getTempForArgCopy() { return getTemp(1); }
  const LDefinition* getTempExtra() { return getTemp(2); }

  // argc is mapped to the same register as argsObj: argc becomes live as
  // argsObj is dying, all registers are calltemps.
  const LAllocation* getArgc() { return getOperand(0); }
};

class LApplyArrayNative
    : public LCallInstructionHelper<BOX_PIECES, BOX_PIECES + 1, 3> {
 public:
  LIR_HEADER(ApplyArrayNative)

  LApplyArrayNative(const LAllocation& elements, const LBoxAllocation& thisv,
                    const LDefinition& tmpObjReg, const LDefinition& tmpCopy,
                    const LDefinition& tmpExtra)
      : LCallInstructionHelper(classOpcode) {
    setOperand(0, elements);
    setBoxOperand(ThisIndex, thisv);
    setTemp(0, tmpObjReg);
    setTemp(1, tmpCopy);
    setTemp(2, tmpExtra);
  }

  static constexpr bool isConstructing() { return false; }

  MApplyArray* mir() const { return mir_->toApplyArray(); }

  const LAllocation* getElements() { return getOperand(0); }

  static const size_t ThisIndex = 1;

  const LDefinition* getTempObject() { return getTemp(0); }
  const LDefinition* getTempForArgCopy() { return getTemp(1); }
  const LDefinition* getTempExtra() { return getTemp(2); }

  // argc is mapped to the same register as elements: argc becomes live as
  // elements is dying, all registers are calltemps.
  const LAllocation* getArgc() { return getOperand(0); }
};

class LConstructArgsNative : public LCallInstructionHelper<BOX_PIECES, 2, 3> {
 public:
  LIR_HEADER(ConstructArgsNative)

  LConstructArgsNative(const LAllocation& argc, const LAllocation& newTarget,
                       const LDefinition& tmpObjReg, const LDefinition& tmpCopy,
                       const LDefinition& tmpExtra)
      : LCallInstructionHelper(classOpcode) {
    setOperand(0, argc);
    setOperand(1, newTarget);
    setTemp(0, tmpObjReg);
    setTemp(1, tmpCopy);
    setTemp(2, tmpExtra);
  }

  static constexpr bool isConstructing() { return true; }

  MConstructArgs* mir() const { return mir_->toConstructArgs(); }

  uint32_t numExtraFormals() const { return mir()->numExtraFormals(); }

  const LAllocation* getArgc() { return getOperand(0); }
  const LAllocation* getNewTarget() { return getOperand(1); }

  const LDefinition* getTempObject() { return getTemp(0); }
  const LDefinition* getTempForArgCopy() { return getTemp(1); }
  const LDefinition* getTempExtra() { return getTemp(2); }
};

class LConstructArrayNative : public LCallInstructionHelper<BOX_PIECES, 2, 3> {
 public:
  LIR_HEADER(ConstructArrayNative)

  LConstructArrayNative(const LAllocation& elements,
                        const LAllocation& newTarget,
                        const LDefinition& tmpObjReg,
                        const LDefinition& tmpCopy, const LDefinition& tmpExtra)
      : LCallInstructionHelper(classOpcode) {
    setOperand(0, elements);
    setOperand(1, newTarget);
    setTemp(0, tmpObjReg);
    setTemp(1, tmpCopy);
    setTemp(2, tmpExtra);
  }

  static constexpr bool isConstructing() { return true; }

  MConstructArray* mir() const { return mir_->toConstructArray(); }

  const LAllocation* getElements() { return getOperand(0); }
  const LAllocation* getNewTarget() { return getOperand(1); }

  const LDefinition* getTempObject() { return getTemp(0); }
  const LDefinition* getTempForArgCopy() { return getTemp(1); }
  const LDefinition* getTempExtra() { return getTemp(2); }

  // argc is mapped to the same register as elements: argc becomes live as
  // elements is dying, all registers are calltemps.
  const LAllocation* getArgc() { return getOperand(0); }
};

// Compares two integral values of the same JS type, either integer or object.
// For objects, both operands are in registers.
class LCompare : public LInstructionHelper<1, 2, 0> {
  JSOp jsop_;

 public:
  LIR_HEADER(Compare)
  LCompare(JSOp jsop, const LAllocation& left, const LAllocation& right)
      : LInstructionHelper(classOpcode), jsop_(jsop) {
    setOperand(0, left);
    setOperand(1, right);
  }

  JSOp jsop() const { return jsop_; }
  const LAllocation* left() { return getOperand(0); }
  const LAllocation* right() { return getOperand(1); }
  MCompare* mir() { return mir_->toCompare(); }
  const char* extraName() const { return CodeName(jsop_); }
};

class LCompareI64 : public LInstructionHelper<1, 2 * INT64_PIECES, 0> {
  JSOp jsop_;

 public:
  LIR_HEADER(CompareI64)

  static const size_t Lhs = 0;
  static const size_t Rhs = INT64_PIECES;

  LCompareI64(JSOp jsop, const LInt64Allocation& left,
              const LInt64Allocation& right)
      : LInstructionHelper(classOpcode), jsop_(jsop) {
    setInt64Operand(Lhs, left);
    setInt64Operand(Rhs, right);
  }

  JSOp jsop() const { return jsop_; }
  LInt64Allocation left() const { return getInt64Operand(LCompareI64::Lhs); }
  LInt64Allocation right() const { return getInt64Operand(LCompareI64::Rhs); }
  MCompare* mir() { return mir_->toCompare(); }
  const char* extraName() const { return CodeName(jsop_); }
};

class LCompareI64AndBranch
    : public LControlInstructionHelper<2, 2 * INT64_PIECES, 0> {
  MCompare* cmpMir_;
  JSOp jsop_;

 public:
  LIR_HEADER(CompareI64AndBranch)

  static const size_t Lhs = 0;
  static const size_t Rhs = INT64_PIECES;

  LCompareI64AndBranch(MCompare* cmpMir, JSOp jsop,
                       const LInt64Allocation& left,
                       const LInt64Allocation& right, MBasicBlock* ifTrue,
                       MBasicBlock* ifFalse)
      : LControlInstructionHelper(classOpcode), cmpMir_(cmpMir), jsop_(jsop) {
    setInt64Operand(Lhs, left);
    setInt64Operand(Rhs, right);
    setSuccessor(0, ifTrue);
    setSuccessor(1, ifFalse);
  }

  JSOp jsop() const { return jsop_; }
  MBasicBlock* ifTrue() const { return getSuccessor(0); }
  MBasicBlock* ifFalse() const { return getSuccessor(1); }
  LInt64Allocation left() const {
    return getInt64Operand(LCompareI64AndBranch::Lhs);
  }
  LInt64Allocation right() const {
    return getInt64Operand(LCompareI64AndBranch::Rhs);
  }
  MTest* mir() const { return mir_->toTest(); }
  MCompare* cmpMir() const { return cmpMir_; }
  const char* extraName() const { return CodeName(jsop_); }
};

// Compares two integral values of the same JS type, either integer or object.
// For objects, both operands are in registers.
class LCompareAndBranch : public LControlInstructionHelper<2, 2, 0> {
  MCompare* cmpMir_;
  JSOp jsop_;

 public:
  LIR_HEADER(CompareAndBranch)
  LCompareAndBranch(MCompare* cmpMir, JSOp jsop, const LAllocation& left,
                    const LAllocation& right, MBasicBlock* ifTrue,
                    MBasicBlock* ifFalse)
      : LControlInstructionHelper(classOpcode), cmpMir_(cmpMir), jsop_(jsop) {
    setOperand(0, left);
    setOperand(1, right);
    setSuccessor(0, ifTrue);
    setSuccessor(1, ifFalse);
  }

  JSOp jsop() const { return jsop_; }
  MBasicBlock* ifTrue() const { return getSuccessor(0); }
  MBasicBlock* ifFalse() const { return getSuccessor(1); }
  const LAllocation* left() { return getOperand(0); }
  const LAllocation* right() { return getOperand(1); }
  MTest* mir() const { return mir_->toTest(); }
  MCompare* cmpMir() const { return cmpMir_; }
  const char* extraName() const { return CodeName(jsop_); }
};

// Bitwise not operation, takes a 32-bit integer as input and returning
// a 32-bit integer result as an output.
class LBitNotI : public LInstructionHelper<1, 1, 0> {
 public:
  LIR_HEADER(BitNotI)

  LBitNotI() : LInstructionHelper(classOpcode) {}
};

class LBitNotI64 : public LInstructionHelper<INT64_PIECES, INT64_PIECES, 0> {
 public:
  LIR_HEADER(BitNotI64)

  LBitNotI64() : LInstructionHelper(classOpcode) {}
};

// Binary bitwise operation, taking two 32-bit integers as inputs and returning
// a 32-bit integer result as an output.
class LBitOpI : public LInstructionHelper<1, 2, 0> {
  JSOp op_;

 public:
  LIR_HEADER(BitOpI)

  explicit LBitOpI(JSOp op) : LInstructionHelper(classOpcode), op_(op) {}

  const char* extraName() const {
    if (bitop() == JSOp::Ursh && mir_->toUrsh()->bailoutsDisabled()) {
      return "ursh:BailoutsDisabled";
    }
    return CodeName(op_);
  }

  JSOp bitop() const { return op_; }
};

class LBitOpI64 : public LInstructionHelper<INT64_PIECES, 2 * INT64_PIECES, 0> {
  JSOp op_;

 public:
  LIR_HEADER(BitOpI64)

  static const size_t Lhs = 0;
  static const size_t Rhs = INT64_PIECES;

  explicit LBitOpI64(JSOp op) : LInstructionHelper(classOpcode), op_(op) {}

  const char* extraName() const { return CodeName(op_); }

  JSOp bitop() const { return op_; }
};

// Shift operation, taking two 32-bit integers as inputs and returning
// a 32-bit integer result as an output.
class LShiftI : public LBinaryMath<0> {
  JSOp op_;

 public:
  LIR_HEADER(ShiftI)

  explicit LShiftI(JSOp op) : LBinaryMath(classOpcode), op_(op) {}

  JSOp bitop() { return op_; }

  MInstruction* mir() { return mir_->toInstruction(); }

  const char* extraName() const { return CodeName(op_); }
};

class LShiftI64 : public LInstructionHelper<INT64_PIECES, INT64_PIECES + 1, 0> {
  JSOp op_;

 public:
  LIR_HEADER(ShiftI64)

  explicit LShiftI64(JSOp op) : LInstructionHelper(classOpcode), op_(op) {}

  static const size_t Lhs = 0;
  static const size_t Rhs = INT64_PIECES;

  JSOp bitop() { return op_; }

  MInstruction* mir() { return mir_->toInstruction(); }

  const char* extraName() const { return CodeName(op_); }
};

class LSignExtendInt64
    : public LInstructionHelper<INT64_PIECES, INT64_PIECES, 0> {
 public:
  LIR_HEADER(SignExtendInt64)

  explicit LSignExtendInt64(const LInt64Allocation& input)
      : LInstructionHelper(classOpcode) {
    setInt64Operand(0, input);
  }

  const MSignExtendInt64* mir() const { return mir_->toSignExtendInt64(); }

  MSignExtendInt64::Mode mode() const { return mir()->mode(); }
};

class LUrshD : public LBinaryMath<1> {
 public:
  LIR_HEADER(UrshD)

  LUrshD(const LAllocation& lhs, const LAllocation& rhs,
         const LDefinition& temp)
      : LBinaryMath(classOpcode) {
    setOperand(0, lhs);
    setOperand(1, rhs);
    setTemp(0, temp);
  }
  const LDefinition* temp() { return getTemp(0); }
};

// Returns from the function being compiled (not used in inlined frames). The
// input must be a box.
class LReturn : public LInstructionHelper<0, BOX_PIECES, 0> {
  bool isGenerator_;

 public:
  LIR_HEADER(Return)

  explicit LReturn(bool isGenerator)
      : LInstructionHelper(classOpcode), isGenerator_(isGenerator) {}

  bool isGenerator() { return isGenerator_; }
};

class LMinMaxBase : public LInstructionHelper<1, 2, 0> {
 protected:
  LMinMaxBase(LNode::Opcode opcode, const LAllocation& first,
              const LAllocation& second)
      : LInstructionHelper(opcode) {
    setOperand(0, first);
    setOperand(1, second);
  }

 public:
  const LAllocation* first() { return this->getOperand(0); }
  const LAllocation* second() { return this->getOperand(1); }
  const LDefinition* output() { return this->getDef(0); }
  MMinMax* mir() const { return mir_->toMinMax(); }
  const char* extraName() const { return mir()->isMax() ? "Max" : "Min"; }
};

class LMinMaxI : public LMinMaxBase {
 public:
  LIR_HEADER(MinMaxI)
  LMinMaxI(const LAllocation& first, const LAllocation& second)
      : LMinMaxBase(classOpcode, first, second) {}
};

class LMinMaxD : public LMinMaxBase {
 public:
  LIR_HEADER(MinMaxD)
  LMinMaxD(const LAllocation& first, const LAllocation& second)
      : LMinMaxBase(classOpcode, first, second) {}
};

class LMinMaxF : public LMinMaxBase {
 public:
  LIR_HEADER(MinMaxF)
  LMinMaxF(const LAllocation& first, const LAllocation& second)
      : LMinMaxBase(classOpcode, first, second) {}
};

class LMinMaxArrayI : public LInstructionHelper<1, 1, 3> {
 public:
  LIR_HEADER(MinMaxArrayI);
  LMinMaxArrayI(const LAllocation& array, const LDefinition& temp0,
                const LDefinition& temp1, const LDefinition& temp2)
      : LInstructionHelper(classOpcode) {
    setOperand(0, array);
    setTemp(0, temp0);
    setTemp(1, temp1);
    setTemp(2, temp2);
  }

  const LAllocation* array() { return getOperand(0); }
  const LDefinition* temp1() { return getTemp(0); }
  const LDefinition* temp2() { return getTemp(1); }
  const LDefinition* temp3() { return getTemp(2); }

  bool isMax() const { return mir_->toMinMaxArray()->isMax(); }
};

class LMinMaxArrayD : public LInstructionHelper<1, 1, 3> {
 public:
  LIR_HEADER(MinMaxArrayD);
  LMinMaxArrayD(const LAllocation& array, const LDefinition& floatTemp,
                const LDefinition& temp1, const LDefinition& temp2)
      : LInstructionHelper(classOpcode) {
    setOperand(0, array);
    setTemp(0, floatTemp);
    setTemp(1, temp1);
    setTemp(2, temp2);
  }

  const LAllocation* array() { return getOperand(0); }
  const LDefinition* floatTemp() { return getTemp(0); }
  const LDefinition* temp1() { return getTemp(1); }
  const LDefinition* temp2() { return getTemp(2); }

  bool isMax() const { return mir_->toMinMaxArray()->isMax(); }
};

// Copysign for doubles.
class LCopySignD : public LInstructionHelper<1, 2, 2> {
 public:
  LIR_HEADER(CopySignD)
  explicit LCopySignD() : LInstructionHelper(classOpcode) {}
};

// Copysign for float32.
class LCopySignF : public LInstructionHelper<1, 2, 2> {
 public:
  LIR_HEADER(CopySignF)
  explicit LCopySignF() : LInstructionHelper(classOpcode) {}
};

class LHypot : public LCallInstructionHelper<1, 4, 0> {
  uint32_t numOperands_;

 public:
  LIR_HEADER(Hypot)
  LHypot(const LAllocation& x, const LAllocation& y)
      : LCallInstructionHelper(classOpcode), numOperands_(2) {
    setOperand(0, x);
    setOperand(1, y);
  }

  LHypot(const LAllocation& x, const LAllocation& y, const LAllocation& z)
      : LCallInstructionHelper(classOpcode), numOperands_(3) {
    setOperand(0, x);
    setOperand(1, y);
    setOperand(2, z);
  }

  LHypot(const LAllocation& x, const LAllocation& y, const LAllocation& z,
         const LAllocation& w)
      : LCallInstructionHelper(classOpcode), numOperands_(4) {
    setOperand(0, x);
    setOperand(1, y);
    setOperand(2, z);
    setOperand(3, w);
  }

  uint32_t numArgs() const { return numOperands_; }

  const LAllocation* x() { return getOperand(0); }

  const LAllocation* y() { return getOperand(1); }

  const LDefinition* output() { return getDef(0); }
};

class LMathFunctionD : public LCallInstructionHelper<1, 1, 0> {
 public:
  LIR_HEADER(MathFunctionD)
  explicit LMathFunctionD(const LAllocation& input)
      : LCallInstructionHelper(classOpcode) {
    setOperand(0, input);
  }

  MMathFunction* mir() const { return mir_->toMathFunction(); }
  const char* extraName() const {
    return MMathFunction::FunctionName(mir()->function());
  }
};

class LMathFunctionF : public LCallInstructionHelper<1, 1, 0> {
 public:
  LIR_HEADER(MathFunctionF)
  explicit LMathFunctionF(const LAllocation& input)
      : LCallInstructionHelper(classOpcode) {
    setOperand(0, input);
  }

  MMathFunction* mir() const { return mir_->toMathFunction(); }
  const char* extraName() const {
    return MMathFunction::FunctionName(mir()->function());
  }
};

// Adds two integers, returning an integer value.
class LAddI : public LBinaryMath<0> {
  bool recoversInput_;

 public:
  LIR_HEADER(AddI)

  LAddI() : LBinaryMath(classOpcode), recoversInput_(false) {}

  const char* extraName() const {
    return snapshot() ? "OverflowCheck" : nullptr;
  }

  bool recoversInput() const { return recoversInput_; }
  void setRecoversInput() { recoversInput_ = true; }

  MAdd* mir() const { return mir_->toAdd(); }
};

class LAddI64 : public LInstructionHelper<INT64_PIECES, 2 * INT64_PIECES, 0> {
 public:
  LIR_HEADER(AddI64)

  LAddI64() : LInstructionHelper(classOpcode) {}

  static const size_t Lhs = 0;
  static const size_t Rhs = INT64_PIECES;
};

// Subtracts two integers, returning an integer value.
class LSubI : public LBinaryMath<0> {
  bool recoversInput_;

 public:
  LIR_HEADER(SubI)

  LSubI() : LBinaryMath(classOpcode), recoversInput_(false) {}

  const char* extraName() const {
    return snapshot() ? "OverflowCheck" : nullptr;
  }

  bool recoversInput() const { return recoversInput_; }
  void setRecoversInput() { recoversInput_ = true; }
  MSub* mir() const { return mir_->toSub(); }
};

inline bool LNode::recoversInput() const {
  switch (op()) {
    case Opcode::AddI:
      return toAddI()->recoversInput();
    case Opcode::SubI:
      return toSubI()->recoversInput();
    default:
      return false;
  }
}

class LSubI64 : public LInstructionHelper<INT64_PIECES, 2 * INT64_PIECES, 0> {
 public:
  LIR_HEADER(SubI64)

  static const size_t Lhs = 0;
  static const size_t Rhs = INT64_PIECES;

  LSubI64() : LInstructionHelper(classOpcode) {}
};

class LMulI64 : public LInstructionHelper<INT64_PIECES, 2 * INT64_PIECES, 1> {
 public:
  LIR_HEADER(MulI64)

  explicit LMulI64() : LInstructionHelper(classOpcode) {
    setTemp(0, LDefinition());
  }

  const LDefinition* temp() { return getTemp(0); }

  static const size_t Lhs = 0;
  static const size_t Rhs = INT64_PIECES;
};

// Performs an add, sub, mul, or div on two double values.
class LMathD : public LBinaryMath<0> {
  JSOp jsop_;

 public:
  LIR_HEADER(MathD)

  explicit LMathD(JSOp jsop) : LBinaryMath(classOpcode), jsop_(jsop) {}

  JSOp jsop() const { return jsop_; }

  const char* extraName() const { return CodeName(jsop_); }
};

// Performs an add, sub, mul, or div on two double values.
class LMathF : public LBinaryMath<0> {
  JSOp jsop_;

 public:
  LIR_HEADER(MathF)

  explicit LMathF(JSOp jsop) : LBinaryMath(classOpcode), jsop_(jsop) {}

  JSOp jsop() const { return jsop_; }

  const char* extraName() const { return CodeName(jsop_); }
};

class LModD : public LBinaryMath<1> {
 public:
  LIR_HEADER(ModD)

  LModD(const LAllocation& lhs, const LAllocation& rhs)
      : LBinaryMath(classOpcode) {
    setOperand(0, lhs);
    setOperand(1, rhs);
    setIsCall();
  }
  MMod* mir() const { return mir_->toMod(); }
};

// Passed the BaselineFrame address in the OsrFrameReg via the IonOsrTempData
// populated by PrepareOsrTempData.
//
// Forwards this object to the LOsrValues for Value materialization.
class LOsrEntry : public LInstructionHelper<1, 0, 1> {
 protected:
  Label label_;
  uint32_t frameDepth_;

 public:
  LIR_HEADER(OsrEntry)

  explicit LOsrEntry(const LDefinition& temp)
      : LInstructionHelper(classOpcode), frameDepth_(0) {
    setTemp(0, temp);
  }

  void setFrameDepth(uint32_t depth) { frameDepth_ = depth; }
  uint32_t getFrameDepth() { return frameDepth_; }
  Label* label() { return &label_; }
  const LDefinition* temp() { return getTemp(0); }
};

// Store a boxed value to a dense array's element vector.
class LStoreElementV : public LInstructionHelper<0, 2 + BOX_PIECES, 0> {
 public:
  LIR_HEADER(StoreElementV)

  LStoreElementV(const LAllocation& elements, const LAllocation& index,
                 const LBoxAllocation& value)
      : LInstructionHelper(classOpcode) {
    setOperand(0, elements);
    setOperand(1, index);
    setBoxOperand(Value, value);
  }

  const char* extraName() const {
    return mir()->needsHoleCheck() ? "HoleCheck" : nullptr;
  }

  static const size_t Value = 2;

  const MStoreElement* mir() const { return mir_->toStoreElement(); }
  const LAllocation* elements() { return getOperand(0); }
  const LAllocation* index() { return getOperand(1); }
};

// Store a typed value to a dense array's elements vector. Compared to
// LStoreElementV, this instruction can store doubles and constants directly,
// and does not store the type tag if the array is monomorphic and known to
// be packed.
class LStoreElementT : public LInstructionHelper<0, 3, 0> {
 public:
  LIR_HEADER(StoreElementT)

  LStoreElementT(const LAllocation& elements, const LAllocation& index,
                 const LAllocation& value)
      : LInstructionHelper(classOpcode) {
    setOperand(0, elements);
    setOperand(1, index);
    setOperand(2, value);
  }

  const char* extraName() const {
    return mir()->needsHoleCheck() ? "HoleCheck" : nullptr;
  }

  const MStoreElement* mir() const { return mir_->toStoreElement(); }
  const LAllocation* elements() { return getOperand(0); }
  const LAllocation* index() { return getOperand(1); }
  const LAllocation* value() { return getOperand(2); }
};

class LArrayPopShift : public LInstructionHelper<BOX_PIECES, 1, 2> {
 public:
  LIR_HEADER(ArrayPopShift)

  LArrayPopShift(const LAllocation& object, const LDefinition& temp0,
                 const LDefinition& temp1)
      : LInstructionHelper(classOpcode) {
    setOperand(0, object);
    setTemp(0, temp0);
    setTemp(1, temp1);
  }

  const char* extraName() const {
    return mir()->mode() == MArrayPopShift::Pop ? "Pop" : "Shift";
  }

  const MArrayPopShift* mir() const { return mir_->toArrayPopShift(); }
  const LAllocation* object() { return getOperand(0); }
  const LDefinition* temp0() { return getTemp(0); }
  const LDefinition* temp1() { return getTemp(1); }
};

template <size_t Defs, size_t Ops>
class LWasmSelectBase : public LInstructionHelper<Defs, Ops, 0> {
  using Base = LInstructionHelper<Defs, Ops, 0>;

 protected:
  explicit LWasmSelectBase(LNode::Opcode opcode) : Base(opcode) {}

 public:
  MWasmSelect* mir() const { return Base::mir_->toWasmSelect(); }
};

class LWasmSelect : public LWasmSelectBase<1, 3> {
 public:
  LIR_HEADER(WasmSelect);

  static const size_t TrueExprIndex = 0;
  static const size_t FalseExprIndex = 1;
  static const size_t CondIndex = 2;

  LWasmSelect(const LAllocation& trueExpr, const LAllocation& falseExpr,
              const LAllocation& cond)
      : LWasmSelectBase(classOpcode) {
    setOperand(TrueExprIndex, trueExpr);
    setOperand(FalseExprIndex, falseExpr);
    setOperand(CondIndex, cond);
  }

  const LAllocation* trueExpr() { return getOperand(TrueExprIndex); }
  const LAllocation* falseExpr() { return getOperand(FalseExprIndex); }
  const LAllocation* condExpr() { return getOperand(CondIndex); }
};

class LWasmSelectI64
    : public LWasmSelectBase<INT64_PIECES, 2 * INT64_PIECES + 1> {
 public:
  LIR_HEADER(WasmSelectI64);

  static const size_t TrueExprIndex = 0;
  static const size_t FalseExprIndex = INT64_PIECES;
  static const size_t CondIndex = INT64_PIECES * 2;

  LWasmSelectI64(const LInt64Allocation& trueExpr,
                 const LInt64Allocation& falseExpr, const LAllocation& cond)
      : LWasmSelectBase(classOpcode) {
    setInt64Operand(TrueExprIndex, trueExpr);
    setInt64Operand(FalseExprIndex, falseExpr);
    setOperand(CondIndex, cond);
  }

  const LInt64Allocation trueExpr() { return getInt64Operand(TrueExprIndex); }
  const LInt64Allocation falseExpr() { return getInt64Operand(FalseExprIndex); }
  const LAllocation* condExpr() { return getOperand(CondIndex); }
};

class LWasmCompareAndSelect : public LWasmSelectBase<1, 4> {
  MCompare::CompareType compareType_;
  JSOp jsop_;

 public:
  LIR_HEADER(WasmCompareAndSelect);

  static const size_t LeftExprIndex = 0;
  static const size_t RightExprIndex = 1;
  static const size_t IfTrueExprIndex = 2;
  static const size_t IfFalseExprIndex = 3;

  LWasmCompareAndSelect(const LAllocation& leftExpr,
                        const LAllocation& rightExpr,
                        MCompare::CompareType compareType, JSOp jsop,
                        const LAllocation& ifTrueExpr,
                        const LAllocation& ifFalseExpr)
      : LWasmSelectBase(classOpcode), compareType_(compareType), jsop_(jsop) {
    setOperand(LeftExprIndex, leftExpr);
    setOperand(RightExprIndex, rightExpr);
    setOperand(IfTrueExprIndex, ifTrueExpr);
    setOperand(IfFalseExprIndex, ifFalseExpr);
  }

  const LAllocation* leftExpr() { return getOperand(LeftExprIndex); }
  const LAllocation* rightExpr() { return getOperand(RightExprIndex); }
  const LAllocation* ifTrueExpr() { return getOperand(IfTrueExprIndex); }
  const LAllocation* ifFalseExpr() { return getOperand(IfFalseExprIndex); }

  MCompare::CompareType compareType() { return compareType_; }
  JSOp jsop() { return jsop_; }
};

class LWasmBoundsCheck64
    : public LInstructionHelper<INT64_PIECES, 2 * INT64_PIECES, 0> {
 public:
  LIR_HEADER(WasmBoundsCheck64);
  explicit LWasmBoundsCheck64(const LInt64Allocation& ptr,
                              const LInt64Allocation& boundsCheckLimit)
      : LInstructionHelper(classOpcode) {
    setInt64Operand(0, ptr);
    setInt64Operand(INT64_PIECES, boundsCheckLimit);
  }
  MWasmBoundsCheck* mir() const { return mir_->toWasmBoundsCheck(); }
  LInt64Allocation ptr() { return getInt64Operand(0); }
  LInt64Allocation boundsCheckLimit() { return getInt64Operand(INT64_PIECES); }
};

namespace details {

// This is a base class for LWasmLoad/LWasmLoadI64.
template <size_t Defs, size_t Temp>
class LWasmLoadBase : public LInstructionHelper<Defs, 2, Temp> {
 public:
  using Base = LInstructionHelper<Defs, 2, Temp>;
  explicit LWasmLoadBase(LNode::Opcode opcode, const LAllocation& ptr,
                         const LAllocation& memoryBase)
      : Base(opcode) {
    Base::setOperand(0, ptr);
    Base::setOperand(1, memoryBase);
  }
  MWasmLoad* mir() const { return Base::mir_->toWasmLoad(); }
  const LAllocation* ptr() { return Base::getOperand(0); }
  const LAllocation* memoryBase() { return Base::getOperand(1); }
};

}  // namespace details

class LWasmLoad : public details::LWasmLoadBase<1, 1> {
 public:
  explicit LWasmLoad(const LAllocation& ptr, const LAllocation& memoryBase)
      : LWasmLoadBase(classOpcode, ptr, memoryBase) {
    setTemp(0, LDefinition::BogusTemp());
  }

  const LDefinition* ptrCopy() { return Base::getTemp(0); }

  LIR_HEADER(WasmLoad);
};

class LWasmLoadI64 : public details::LWasmLoadBase<INT64_PIECES, 2> {
 public:
  explicit LWasmLoadI64(const LAllocation& ptr, const LAllocation& memoryBase)
      : LWasmLoadBase(classOpcode, ptr, memoryBase) {
    setTemp(0, LDefinition::BogusTemp());
    setTemp(1, LDefinition::BogusTemp());
  }

  const LDefinition* ptrCopy() { return Base::getTemp(0); }
  const LDefinition* memoryBaseCopy() { return Base::getTemp(1); }

  LIR_HEADER(WasmLoadI64);
};

class LWasmStore : public LInstructionHelper<0, 3, 1> {
 public:
  LIR_HEADER(WasmStore);

  static const size_t PtrIndex = 0;
  static const size_t ValueIndex = 1;
  static const size_t MemoryBaseIndex = 2;

  LWasmStore(const LAllocation& ptr, const LAllocation& value,
             const LAllocation& memoryBase)
      : LInstructionHelper(classOpcode) {
    setOperand(PtrIndex, ptr);
    setOperand(ValueIndex, value);
    setOperand(MemoryBaseIndex, memoryBase);
    setTemp(0, LDefinition::BogusTemp());
  }
  MWasmStore* mir() const { return mir_->toWasmStore(); }
  const LAllocation* ptr() { return getOperand(PtrIndex); }
  const LDefinition* ptrCopy() { return getTemp(0); }
  const LAllocation* value() { return getOperand(ValueIndex); }
  const LAllocation* memoryBase() { return getOperand(MemoryBaseIndex); }
};

class LWasmStoreI64 : public LInstructionHelper<0, INT64_PIECES + 2, 1> {
 public:
  LIR_HEADER(WasmStoreI64);

  static const size_t PtrIndex = 0;
  static const size_t MemoryBaseIndex = 1;
  static const size_t ValueIndex = 2;

  LWasmStoreI64(const LAllocation& ptr, const LInt64Allocation& value,
                const LAllocation& memoryBase)
      : LInstructionHelper(classOpcode) {
    setOperand(PtrIndex, ptr);
    setOperand(MemoryBaseIndex, memoryBase);
    setInt64Operand(ValueIndex, value);
    setTemp(0, LDefinition::BogusTemp());
  }
  MWasmStore* mir() const { return mir_->toWasmStore(); }
  const LAllocation* ptr() { return getOperand(PtrIndex); }
  const LAllocation* memoryBase() { return getOperand(MemoryBaseIndex); }
  const LDefinition* ptrCopy() { return getTemp(0); }
  const LInt64Allocation value() { return getInt64Operand(ValueIndex); }
};

class LWasmCompareExchangeHeap : public LInstructionHelper<1, 4, 4> {
 public:
  LIR_HEADER(WasmCompareExchangeHeap);

  // ARM, ARM64, x86, x64
  LWasmCompareExchangeHeap(const LAllocation& ptr, const LAllocation& oldValue,
                           const LAllocation& newValue,
                           const LAllocation& memoryBase)
      : LInstructionHelper(classOpcode) {
    setOperand(0, ptr);
    setOperand(1, oldValue);
    setOperand(2, newValue);
    setOperand(3, memoryBase);
    setTemp(0, LDefinition::BogusTemp());
  }
  // MIPS32, MIPS64, LoongArch64
  LWasmCompareExchangeHeap(const LAllocation& ptr, const LAllocation& oldValue,
                           const LAllocation& newValue,
                           const LDefinition& valueTemp,
                           const LDefinition& offsetTemp,
                           const LDefinition& maskTemp,
                           const LAllocation& memoryBase)
      : LInstructionHelper(classOpcode) {
    setOperand(0, ptr);
    setOperand(1, oldValue);
    setOperand(2, newValue);
    setOperand(3, memoryBase);
    setTemp(0, LDefinition::BogusTemp());
    setTemp(1, valueTemp);
    setTemp(2, offsetTemp);
    setTemp(3, maskTemp);
  }

  const LAllocation* ptr() { return getOperand(0); }
  const LAllocation* oldValue() { return getOperand(1); }
  const LAllocation* newValue() { return getOperand(2); }
  const LAllocation* memoryBase() { return getOperand(3); }
  const LDefinition* addrTemp() { return getTemp(0); }

  void setAddrTemp(const LDefinition& addrTemp) { setTemp(0, addrTemp); }

  // Temp that may be used on LL/SC platforms for extract/insert bits of word.
  const LDefinition* valueTemp() { return getTemp(1); }
  const LDefinition* offsetTemp() { return getTemp(2); }
  const LDefinition* maskTemp() { return getTemp(3); }

  MWasmCompareExchangeHeap* mir() const {
    return mir_->toWasmCompareExchangeHeap();
  }
};

class LWasmAtomicExchangeHeap : public LInstructionHelper<1, 3, 4> {
 public:
  LIR_HEADER(WasmAtomicExchangeHeap);

  // ARM, ARM64, x86, x64
  LWasmAtomicExchangeHeap(const LAllocation& ptr, const LAllocation& value,
                          const LAllocation& memoryBase)
      : LInstructionHelper(classOpcode) {
    setOperand(0, ptr);
    setOperand(1, value);
    setOperand(2, memoryBase);
    setTemp(0, LDefinition::BogusTemp());
  }
  // MIPS32, MIPS64, LoongArch64
  LWasmAtomicExchangeHeap(const LAllocation& ptr, const LAllocation& value,
                          const LDefinition& valueTemp,
                          const LDefinition& offsetTemp,
                          const LDefinition& maskTemp,
                          const LAllocation& memoryBase)
      : LInstructionHelper(classOpcode) {
    setOperand(0, ptr);
    setOperand(1, value);
    setOperand(2, memoryBase);
    setTemp(0, LDefinition::BogusTemp());
    setTemp(1, valueTemp);
    setTemp(2, offsetTemp);
    setTemp(3, maskTemp);
  }

  const LAllocation* ptr() { return getOperand(0); }
  const LAllocation* value() { return getOperand(1); }
  const LAllocation* memoryBase() { return getOperand(2); }
  const LDefinition* addrTemp() { return getTemp(0); }

  void setAddrTemp(const LDefinition& addrTemp) { setTemp(0, addrTemp); }

  // Temp that may be used on LL/SC platforms for extract/insert bits of word.
  const LDefinition* valueTemp() { return getTemp(1); }
  const LDefinition* offsetTemp() { return getTemp(2); }
  const LDefinition* maskTemp() { return getTemp(3); }

  MWasmAtomicExchangeHeap* mir() const {
    return mir_->toWasmAtomicExchangeHeap();
  }
};

class LWasmAtomicBinopHeap : public LInstructionHelper<1, 3, 6> {
 public:
  LIR_HEADER(WasmAtomicBinopHeap);

  static const int32_t valueOp = 1;

  // ARM, ARM64, x86, x64
  LWasmAtomicBinopHeap(const LAllocation& ptr, const LAllocation& value,
                       const LDefinition& temp, const LDefinition& flagTemp,
                       const LAllocation& memoryBase)
      : LInstructionHelper(classOpcode) {
    setOperand(0, ptr);
    setOperand(1, value);
    setOperand(2, memoryBase);
    setTemp(0, temp);
    setTemp(1, LDefinition::BogusTemp());
    setTemp(2, flagTemp);
  }
  // MIPS32, MIPS64, LoongArch64
  LWasmAtomicBinopHeap(const LAllocation& ptr, const LAllocation& value,
                       const LDefinition& valueTemp,
                       const LDefinition& offsetTemp,
                       const LDefinition& maskTemp,
                       const LAllocation& memoryBase)
      : LInstructionHelper(classOpcode) {
    setOperand(0, ptr);
    setOperand(1, value);
    setOperand(2, memoryBase);
    setTemp(0, LDefinition::BogusTemp());
    setTemp(1, LDefinition::BogusTemp());
    setTemp(2, LDefinition::BogusTemp());
    setTemp(3, valueTemp);
    setTemp(4, offsetTemp);
    setTemp(5, maskTemp);
  }
  const LAllocation* ptr() { return getOperand(0); }
  const LAllocation* value() {
    MOZ_ASSERT(valueOp == 1);
    return getOperand(1);
  }
  const LAllocation* memoryBase() { return getOperand(2); }
  const LDefinition* temp() { return getTemp(0); }

  // Temp that may be used on some platforms to hold a computed address.
  const LDefinition* addrTemp() { return getTemp(1); }
  void setAddrTemp(const LDefinition& addrTemp) { setTemp(1, addrTemp); }

  // Temp that may be used on LL/SC platforms for the flag result of the store.
  const LDefinition* flagTemp() { return getTemp(2); }
  // Temp that may be used on LL/SC platforms for extract/insert bits of word.
  const LDefinition* valueTemp() { return getTemp(3); }
  const LDefinition* offsetTemp() { return getTemp(4); }
  const LDefinition* maskTemp() { return getTemp(5); }

  MWasmAtomicBinopHeap* mir() const { return mir_->toWasmAtomicBinopHeap(); }
};

// Atomic binary operation where the result is discarded.
class LWasmAtomicBinopHeapForEffect : public LInstructionHelper<0, 3, 5> {
 public:
  LIR_HEADER(WasmAtomicBinopHeapForEffect);
  // ARM, ARM64, x86, x64
  LWasmAtomicBinopHeapForEffect(const LAllocation& ptr,
                                const LAllocation& value,
                                const LDefinition& flagTemp,
                                const LAllocation& memoryBase)
      : LInstructionHelper(classOpcode) {
    setOperand(0, ptr);
    setOperand(1, value);
    setOperand(2, memoryBase);
    setTemp(0, LDefinition::BogusTemp());
    setTemp(1, flagTemp);
  }
  // MIPS32, MIPS64, LoongArch64
  LWasmAtomicBinopHeapForEffect(const LAllocation& ptr,
                                const LAllocation& value,
                                const LDefinition& valueTemp,
                                const LDefinition& offsetTemp,
                                const LDefinition& maskTemp,
                                const LAllocation& memoryBase)
      : LInstructionHelper(classOpcode) {
    setOperand(0, ptr);
    setOperand(1, value);
    setOperand(2, memoryBase);
    setTemp(0, LDefinition::BogusTemp());
    setTemp(1, LDefinition::BogusTemp());
    setTemp(2, valueTemp);
    setTemp(3, offsetTemp);
    setTemp(4, maskTemp);
  }
  const LAllocation* ptr() { return getOperand(0); }
  const LAllocation* value() { return getOperand(1); }
  const LAllocation* memoryBase() { return getOperand(2); }

  // Temp that may be used on some platforms to hold a computed address.
  const LDefinition* addrTemp() { return getTemp(0); }
  void setAddrTemp(const LDefinition& addrTemp) { setTemp(0, addrTemp); }

  // Temp that may be used on LL/SC platforms for the flag result of the store.
  const LDefinition* flagTemp() { return getTemp(1); }
  // Temp that may be used on LL/SC platforms for extract/insert bits of word.
  const LDefinition* valueTemp() { return getTemp(2); }
  const LDefinition* offsetTemp() { return getTemp(3); }
  const LDefinition* maskTemp() { return getTemp(4); }

  MWasmAtomicBinopHeap* mir() const { return mir_->toWasmAtomicBinopHeap(); }
};

class LWasmDerivedPointer : public LInstructionHelper<1, 1, 0> {
 public:
  LIR_HEADER(WasmDerivedPointer);
  explicit LWasmDerivedPointer(const LAllocation& base)
      : LInstructionHelper(classOpcode) {
    setOperand(0, base);
  }
  const LAllocation* base() { return getOperand(0); }
  uint32_t offset() { return mirRaw()->toWasmDerivedPointer()->offset(); }
};

class LWasmDerivedIndexPointer : public LInstructionHelper<1, 2, 0> {
 public:
  LIR_HEADER(WasmDerivedIndexPointer);
  explicit LWasmDerivedIndexPointer(const LAllocation& base,
                                    const LAllocation& index)
      : LInstructionHelper(classOpcode) {
    setOperand(0, base);
    setOperand(1, index);
  }
  const LAllocation* base() { return getOperand(0); }
  const LAllocation* index() { return getOperand(1); }
  Scale scale() { return mirRaw()->toWasmDerivedIndexPointer()->scale(); }
};

class LWasmParameterI64 : public LInstructionHelper<INT64_PIECES, 0, 0> {
 public:
  LIR_HEADER(WasmParameterI64);

  LWasmParameterI64() : LInstructionHelper(classOpcode) {}
};

// This is used only with LWasmCall.
class LWasmCallIndirectAdjunctSafepoint : public LInstructionHelper<0, 0, 0> {
  CodeOffset offs_;
  uint32_t framePushedAtStackMapBase_;

 public:
  LIR_HEADER(WasmCallIndirectAdjunctSafepoint);

  LWasmCallIndirectAdjunctSafepoint()
      : LInstructionHelper(classOpcode),
        offs_(0),
        framePushedAtStackMapBase_(0) {
    // Ensure that the safepoint does not get live registers associated with it.
    setIsCall();
  }

  CodeOffset safepointLocation() const {
    MOZ_ASSERT(offs_.offset() != 0);
    return offs_;
  }
  uint32_t framePushedAtStackMapBase() const {
    MOZ_ASSERT(offs_.offset() != 0);
    return framePushedAtStackMapBase_;
  }
  void recordSafepointInfo(CodeOffset offs, uint32_t framePushed) {
    offs_ = offs;
    framePushedAtStackMapBase_ = framePushed;
  }
};

// LWasmCall may be generated into two function calls in the case of
// call_indirect, one for the fast path and one for the slow path.  In that
// case, the node carries a pointer to a companion node, the "adjunct
// safepoint", representing the safepoint for the second of the two calls.  The
// dual-call construction is only meaningful for wasm because wasm has no
// invalidation of code; this is not a pattern to be used generally.
class LWasmCall : public LVariadicInstruction<0, 0> {
  bool needsBoundsCheck_;
  mozilla::Maybe<uint32_t> tableSize_;
  LWasmCallIndirectAdjunctSafepoint* adjunctSafepoint_;

 public:
  LIR_HEADER(WasmCall);

  LWasmCall(uint32_t numOperands, bool needsBoundsCheck,
            mozilla::Maybe<uint32_t> tableSize = mozilla::Nothing())
      : LVariadicInstruction(classOpcode, numOperands),
        needsBoundsCheck_(needsBoundsCheck),
        tableSize_(tableSize),
        adjunctSafepoint_(nullptr) {
    this->setIsCall();
  }

  MWasmCallBase* callBase() const {
    if (isCatchable() && !isReturnCall()) {
      return static_cast<MWasmCallBase*>(mirCatchable());
    }
    if (isReturnCall()) {
      return static_cast<MWasmReturnCall*>(mirReturnCall());
    }
    return static_cast<MWasmCallBase*>(mirUncatchable());
  }
  bool isCatchable() const { return mir_->isWasmCallCatchable(); }
  bool isReturnCall() const { return mir_->isWasmReturnCall(); }
  MWasmCallCatchable* mirCatchable() const {
    return mir_->toWasmCallCatchable();
  }
  MWasmCallUncatchable* mirUncatchable() const {
    return mir_->toWasmCallUncatchable();
  }
  MWasmReturnCall* mirReturnCall() const { return mir_->toWasmReturnCall(); }

  static bool isCallPreserved(AnyRegister reg) {
    // All MWasmCalls preserve the TLS register:
    //  - internal/indirect calls do by the internal wasm ABI
    //  - import calls do by explicitly saving/restoring at the callsite
    //  - builtin calls do because the TLS reg is non-volatile
    // See also CodeGeneratorShared::emitWasmCall.
    //
    // All other registers are not preserved. This is is relied upon by
    // MWasmCallCatchable which needs all live registers to be spilled before
    // a call.
    return !reg.isFloat() && reg.gpr() == InstanceReg;
  }

  bool needsBoundsCheck() const { return needsBoundsCheck_; }
  mozilla::Maybe<uint32_t> tableSize() const { return tableSize_; }
  LWasmCallIndirectAdjunctSafepoint* adjunctSafepoint() const {
    MOZ_ASSERT(adjunctSafepoint_ != nullptr);
    return adjunctSafepoint_;
  }
  void setAdjunctSafepoint(LWasmCallIndirectAdjunctSafepoint* asp) {
    adjunctSafepoint_ = asp;
  }
};

class LWasmRegisterResult : public LInstructionHelper<1, 0, 0> {
 public:
  LIR_HEADER(WasmRegisterResult);

  LWasmRegisterResult() : LInstructionHelper(classOpcode) {}

  MWasmRegisterResult* mir() const {
    if (!mir_->isWasmRegisterResult()) {
      return nullptr;
    }
    return mir_->toWasmRegisterResult();
  }
};

class LWasmRegisterPairResult : public LInstructionHelper<2, 0, 0> {
 public:
  LIR_HEADER(WasmRegisterPairResult);

  LWasmRegisterPairResult() : LInstructionHelper(classOpcode) {}

  MDefinition* mir() const { return mirRaw(); }
};

inline uint32_t LStackArea::base() const {
  return ins()->toWasmStackResultArea()->mir()->base();
}
inline void LStackArea::setBase(uint32_t base) {
  ins()->toWasmStackResultArea()->mir()->setBase(base);
}
inline uint32_t LStackArea::size() const {
  return ins()->toWasmStackResultArea()->mir()->byteSize();
}

inline bool LStackArea::ResultIterator::done() const {
  return idx_ == alloc_.ins()->toWasmStackResultArea()->mir()->resultCount();
}
inline void LStackArea::ResultIterator::next() {
  MOZ_ASSERT(!done());
  idx_++;
}
inline LAllocation LStackArea::ResultIterator::alloc() const {
  MOZ_ASSERT(!done());
  MWasmStackResultArea* area = alloc_.ins()->toWasmStackResultArea()->mir();
  return LStackSlot(area->base() - area->result(idx_).offset());
}
inline bool LStackArea::ResultIterator::isWasmAnyRef() const {
  MOZ_ASSERT(!done());
  MWasmStackResultArea* area = alloc_.ins()->toWasmStackResultArea()->mir();
  MIRType type = area->result(idx_).type();
#ifndef JS_PUNBOX64
  // LDefinition::TypeFrom isn't defined for MIRType::Int64 values on
  // this platform, so here we have a special case.
  if (type == MIRType::Int64) {
    return false;
  }
#endif
  return LDefinition::TypeFrom(type) == LDefinition::WASM_ANYREF;
}

class LWasmStackResult : public LInstructionHelper<1, 1, 0> {
 public:
  LIR_HEADER(WasmStackResult);

  LWasmStackResult() : LInstructionHelper(classOpcode) {}

  MWasmStackResult* mir() const { return mir_->toWasmStackResult(); }
  LStackSlot result(uint32_t base) const {
    return LStackSlot(base - mir()->result().offset());
  }
};

class LWasmStackResult64 : public LInstructionHelper<INT64_PIECES, 1, 0> {
 public:
  LIR_HEADER(WasmStackResult64);

  LWasmStackResult64() : LInstructionHelper(classOpcode) {}

  MWasmStackResult* mir() const { return mir_->toWasmStackResult(); }
  LStackSlot result(uint32_t base, LDefinition* def) {
    uint32_t offset = base - mir()->result().offset();
#if defined(JS_NUNBOX32)
    if (def == getDef(INT64LOW_INDEX)) {
      offset -= INT64LOW_OFFSET;
    } else {
      MOZ_ASSERT(def == getDef(INT64HIGH_INDEX));
      offset -= INT64HIGH_OFFSET;
    }
#else
    MOZ_ASSERT(def == getDef(0));
#endif
    return LStackSlot(offset);
  }
};

inline LStackSlot LStackArea::resultAlloc(LInstruction* lir,
                                          LDefinition* def) const {
  if (lir->isWasmStackResult64()) {
    return lir->toWasmStackResult64()->result(base(), def);
  }
  MOZ_ASSERT(def == lir->getDef(0));
  return lir->toWasmStackResult()->result(base());
}

inline bool LNode::isCallPreserved(AnyRegister reg) const {
  return isWasmCall() && LWasmCall::isCallPreserved(reg);
}

class LMemoryBarrier : public LInstructionHelper<0, 0, 0> {
 private:
  const MemoryBarrierBits type_;

 public:
  LIR_HEADER(MemoryBarrier)

  // The parameter 'type' is a bitwise 'or' of the barrier types needed,
  // see AtomicOp.h.
  explicit LMemoryBarrier(MemoryBarrierBits type)
      : LInstructionHelper(classOpcode), type_(type) {
    MOZ_ASSERT((type_ & ~MembarAllbits) == MembarNobits);
  }

  MemoryBarrierBits type() const { return type_; }
};

template <size_t NumDefs>
class LIonToWasmCallBase : public LVariadicInstruction<NumDefs, 1> {
  using Base = LVariadicInstruction<NumDefs, 1>;

 public:
  explicit LIonToWasmCallBase(LNode::Opcode classOpcode, uint32_t numOperands,
                              const LDefinition& temp)
      : Base(classOpcode, numOperands) {
    this->setIsCall();
    this->setTemp(0, temp);
  }
  MIonToWasmCall* mir() const { return this->mir_->toIonToWasmCall(); }
  const LDefinition* temp() { return this->getTemp(0); }
};

class LIonToWasmCall : public LIonToWasmCallBase<1> {
 public:
  LIR_HEADER(IonToWasmCall);
  LIonToWasmCall(uint32_t numOperands, const LDefinition& temp)
      : LIonToWasmCallBase<1>(classOpcode, numOperands, temp) {}
};

class LIonToWasmCallV : public LIonToWasmCallBase<BOX_PIECES> {
 public:
  LIR_HEADER(IonToWasmCallV);
  LIonToWasmCallV(uint32_t numOperands, const LDefinition& temp)
      : LIonToWasmCallBase<BOX_PIECES>(classOpcode, numOperands, temp) {}
};

class LIonToWasmCallI64 : public LIonToWasmCallBase<INT64_PIECES> {
 public:
  LIR_HEADER(IonToWasmCallI64);
  LIonToWasmCallI64(uint32_t numOperands, const LDefinition& temp)
      : LIonToWasmCallBase<INT64_PIECES>(classOpcode, numOperands, temp) {}
};

// Wasm SIMD.

// (v128, v128, v128) -> v128 effect-free operation.
// temp is FPR.
class LWasmTernarySimd128 : public LInstructionHelper<1, 3, 1> {
  wasm::SimdOp op_;

 public:
  LIR_HEADER(WasmTernarySimd128)

  static constexpr uint32_t V0 = 0;
  static constexpr uint32_t V1 = 1;
  static constexpr uint32_t V2 = 2;

  LWasmTernarySimd128(wasm::SimdOp op, const LAllocation& v0,
                      const LAllocation& v1, const LAllocation& v2)
      : LInstructionHelper(classOpcode), op_(op) {
    setOperand(V0, v0);
    setOperand(V1, v1);
    setOperand(V2, v2);
  }

  LWasmTernarySimd128(wasm::SimdOp op, const LAllocation& v0,
                      const LAllocation& v1, const LAllocation& v2,
                      const LDefinition& temp)
      : LInstructionHelper(classOpcode), op_(op) {
    setOperand(V0, v0);
    setOperand(V1, v1);
    setOperand(V2, v2);
    setTemp(0, temp);
  }

  const LAllocation* v0() { return getOperand(V0); }
  const LAllocation* v1() { return getOperand(V1); }
  const LAllocation* v2() { return getOperand(V2); }
  const LDefinition* temp() { return getTemp(0); }

  wasm::SimdOp simdOp() const { return op_; }
};

// (v128, v128) -> v128 effect-free operations
// lhs and dest are the same.
// temps (if in use) are FPR.
// The op may differ from the MIR node's op.
class LWasmBinarySimd128 : public LInstructionHelper<1, 2, 2> {
  wasm::SimdOp op_;

 public:
  LIR_HEADER(WasmBinarySimd128)

  static constexpr uint32_t Lhs = 0;
  static constexpr uint32_t LhsDest = 0;
  static constexpr uint32_t Rhs = 1;

  LWasmBinarySimd128(wasm::SimdOp op, const LAllocation& lhs,
                     const LAllocation& rhs, const LDefinition& temp0,
                     const LDefinition& temp1)
      : LInstructionHelper(classOpcode), op_(op) {
    setOperand(Lhs, lhs);
    setOperand(Rhs, rhs);
    setTemp(0, temp0);
    setTemp(1, temp1);
  }

  const LAllocation* lhs() { return getOperand(Lhs); }
  const LAllocation* lhsDest() { return getOperand(LhsDest); }
  const LAllocation* rhs() { return getOperand(Rhs); }
  wasm::SimdOp simdOp() const { return op_; }

  static bool SpecializeForConstantRhs(wasm::SimdOp op);
};

class LWasmBinarySimd128WithConstant : public LInstructionHelper<1, 1, 1> {
  SimdConstant rhs_;

 public:
  LIR_HEADER(WasmBinarySimd128WithConstant)

  static constexpr uint32_t Lhs = 0;
  static constexpr uint32_t LhsDest = 0;

  LWasmBinarySimd128WithConstant(const LAllocation& lhs,
                                 const SimdConstant& rhs,
                                 const LDefinition& temp)
      : LInstructionHelper(classOpcode), rhs_(rhs) {
    setOperand(Lhs, lhs);
    setTemp(0, temp);
  }

  const LAllocation* lhs() { return getOperand(Lhs); }
  const LAllocation* lhsDest() { return getOperand(LhsDest); }
  const SimdConstant& rhs() { return rhs_; }
  wasm::SimdOp simdOp() const {
    return mir_->toWasmBinarySimd128WithConstant()->simdOp();
  }
};

// (v128, i32) -> v128 effect-free variable-width shift operations
// lhs and dest are the same.
// temp is an FPR (if in use).
class LWasmVariableShiftSimd128 : public LInstructionHelper<1, 2, 1> {
 public:
  LIR_HEADER(WasmVariableShiftSimd128)

  static constexpr uint32_t Lhs = 0;
  static constexpr uint32_t LhsDest = 0;
  static constexpr uint32_t Rhs = 1;

  LWasmVariableShiftSimd128(const LAllocation& lhs, const LAllocation& rhs,
                            const LDefinition& temp)
      : LInstructionHelper(classOpcode) {
    setOperand(Lhs, lhs);
    setOperand(Rhs, rhs);
    setTemp(0, temp);
  }

  const LAllocation* lhs() { return getOperand(Lhs); }
  const LAllocation* lhsDest() { return getOperand(LhsDest); }
  const LAllocation* rhs() { return getOperand(Rhs); }
  wasm::SimdOp simdOp() const { return mir_->toWasmShiftSimd128()->simdOp(); }
};

// (v128, i32) -> v128 effect-free constant-width shift operations
class LWasmConstantShiftSimd128 : public LInstructionHelper<1, 1, 0> {
  int32_t shift_;

 public:
  LIR_HEADER(WasmConstantShiftSimd128)

  static constexpr uint32_t Src = 0;

  LWasmConstantShiftSimd128(const LAllocation& src, int32_t shift)
      : LInstructionHelper(classOpcode), shift_(shift) {
    setOperand(Src, src);
  }

  const LAllocation* src() { return getOperand(Src); }
  int32_t shift() { return shift_; }
  wasm::SimdOp simdOp() const { return mir_->toWasmShiftSimd128()->simdOp(); }
};

// (v128) -> v128 sign replication operation.
class LWasmSignReplicationSimd128 : public LInstructionHelper<1, 1, 0> {
 public:
  LIR_HEADER(WasmSignReplicationSimd128)

  static constexpr uint32_t Src = 0;

  explicit LWasmSignReplicationSimd128(const LAllocation& src)
      : LInstructionHelper(classOpcode) {
    setOperand(Src, src);
  }

  const LAllocation* src() { return getOperand(Src); }
  wasm::SimdOp simdOp() const { return mir_->toWasmShiftSimd128()->simdOp(); }
};

// (v128, v128, imm_simd) -> v128 effect-free operation.
// temp is FPR (and always in use).
class LWasmShuffleSimd128 : public LInstructionHelper<1, 2, 1> {
 private:
  SimdShuffleOp op_;
  SimdConstant control_;

 public:
  LIR_HEADER(WasmShuffleSimd128)

  static constexpr uint32_t Lhs = 0;
  static constexpr uint32_t LhsDest = 0;
  static constexpr uint32_t Rhs = 1;

  LWasmShuffleSimd128(const LAllocation& lhs, const LAllocation& rhs,
                      const LDefinition& temp, SimdShuffleOp op,
                      SimdConstant control)
      : LInstructionHelper(classOpcode), op_(op), control_(control) {
    setOperand(Lhs, lhs);
    setOperand(Rhs, rhs);
    setTemp(0, temp);
  }

  const LAllocation* lhs() { return getOperand(Lhs); }
  const LAllocation* lhsDest() { return getOperand(LhsDest); }
  const LAllocation* rhs() { return getOperand(Rhs); }
  const LDefinition* temp() { return getTemp(0); }
  SimdShuffleOp op() { return op_; }
  SimdConstant control() { return control_; }
};

// (v128, imm_simd) -> v128 effect-free operation.
class LWasmPermuteSimd128 : public LInstructionHelper<1, 1, 0> {
 private:
  SimdPermuteOp op_;
  SimdConstant control_;

 public:
  LIR_HEADER(WasmPermuteSimd128)

  static constexpr uint32_t Src = 0;

  LWasmPermuteSimd128(const LAllocation& src, SimdPermuteOp op,
                      SimdConstant control)
      : LInstructionHelper(classOpcode), op_(op), control_(control) {
    setOperand(Src, src);
  }

  const LAllocation* src() { return getOperand(Src); }
  SimdPermuteOp op() { return op_; }
  SimdConstant control() { return control_; }
};

class LWasmReplaceLaneSimd128 : public LInstructionHelper<1, 2, 0> {
 public:
  LIR_HEADER(WasmReplaceLaneSimd128)

  static constexpr uint32_t Lhs = 0;
  static constexpr uint32_t LhsDest = 0;
  static constexpr uint32_t Rhs = 1;

  LWasmReplaceLaneSimd128(const LAllocation& lhs, const LAllocation& rhs)
      : LInstructionHelper(classOpcode) {
    setOperand(Lhs, lhs);
    setOperand(Rhs, rhs);
  }

  const LAllocation* lhs() { return getOperand(Lhs); }
  const LAllocation* lhsDest() { return getOperand(LhsDest); }
  const LAllocation* rhs() { return getOperand(Rhs); }
  uint32_t laneIndex() const {
    return mir_->toWasmReplaceLaneSimd128()->laneIndex();
  }
  wasm::SimdOp simdOp() const {
    return mir_->toWasmReplaceLaneSimd128()->simdOp();
  }
};

class LWasmReplaceInt64LaneSimd128
    : public LInstructionHelper<1, INT64_PIECES + 1, 0> {
 public:
  LIR_HEADER(WasmReplaceInt64LaneSimd128)

  static constexpr uint32_t Lhs = 0;
  static constexpr uint32_t LhsDest = 0;
  static constexpr uint32_t Rhs = 1;

  LWasmReplaceInt64LaneSimd128(const LAllocation& lhs,
                               const LInt64Allocation& rhs)
      : LInstructionHelper(classOpcode) {
    setOperand(Lhs, lhs);
    setInt64Operand(Rhs, rhs);
  }

  const LAllocation* lhs() { return getOperand(Lhs); }
  const LAllocation* lhsDest() { return getOperand(LhsDest); }
  const LInt64Allocation rhs() { return getInt64Operand(Rhs); }
  const LDefinition* output() { return this->getDef(0); }
  uint32_t laneIndex() const {
    return mir_->toWasmReplaceLaneSimd128()->laneIndex();
  }
  wasm::SimdOp simdOp() const {
    return mir_->toWasmReplaceLaneSimd128()->simdOp();
  }
};

// (scalar) -> v128 effect-free operations, scalar != int64
class LWasmScalarToSimd128 : public LInstructionHelper<1, 1, 0> {
 public:
  LIR_HEADER(WasmScalarToSimd128)

  static constexpr uint32_t Src = 0;

  explicit LWasmScalarToSimd128(const LAllocation& src)
      : LInstructionHelper(classOpcode) {
    setOperand(Src, src);
  }

  const LAllocation* src() { return getOperand(Src); }
  wasm::SimdOp simdOp() const {
    return mir_->toWasmScalarToSimd128()->simdOp();
  }
};

// (int64) -> v128 effect-free operations
class LWasmInt64ToSimd128 : public LInstructionHelper<1, INT64_PIECES, 0> {
 public:
  LIR_HEADER(WasmInt64ToSimd128)

  static constexpr uint32_t Src = 0;

  explicit LWasmInt64ToSimd128(const LInt64Allocation& src)
      : LInstructionHelper(classOpcode) {
    setInt64Operand(Src, src);
  }

  const LInt64Allocation src() { return getInt64Operand(Src); }
  wasm::SimdOp simdOp() const {
    return mir_->toWasmScalarToSimd128()->simdOp();
  }
};

// (v128) -> v128 effect-free operations
// temp is FPR (if in use).
class LWasmUnarySimd128 : public LInstructionHelper<1, 1, 1> {
 public:
  LIR_HEADER(WasmUnarySimd128)

  static constexpr uint32_t Src = 0;

  LWasmUnarySimd128(const LAllocation& src, const LDefinition& temp)
      : LInstructionHelper(classOpcode) {
    setOperand(Src, src);
    setTemp(0, temp);
  }

  const LAllocation* src() { return getOperand(Src); }
  const LDefinition* temp() { return getTemp(0); }
  wasm::SimdOp simdOp() const { return mir_->toWasmUnarySimd128()->simdOp(); }
};

// (v128, imm) -> scalar effect-free operations.
// temp is FPR (if in use).
class LWasmReduceSimd128 : public LInstructionHelper<1, 1, 1> {
 public:
  LIR_HEADER(WasmReduceSimd128)

  static constexpr uint32_t Src = 0;

  explicit LWasmReduceSimd128(const LAllocation& src, const LDefinition& temp)
      : LInstructionHelper(classOpcode) {
    setOperand(Src, src);
    setTemp(0, temp);
  }

  const LAllocation* src() { return getOperand(Src); }
  uint32_t imm() const { return mir_->toWasmReduceSimd128()->imm(); }
  wasm::SimdOp simdOp() const { return mir_->toWasmReduceSimd128()->simdOp(); }
};

// (v128, imm) -> i64 effect-free operations
class LWasmReduceSimd128ToInt64
    : public LInstructionHelper<INT64_PIECES, 1, 0> {
 public:
  LIR_HEADER(WasmReduceSimd128ToInt64)

  static constexpr uint32_t Src = 0;

  explicit LWasmReduceSimd128ToInt64(const LAllocation& src)
      : LInstructionHelper(classOpcode) {
    setOperand(Src, src);
  }

  const LAllocation* src() { return getOperand(Src); }
  uint32_t imm() const { return mir_->toWasmReduceSimd128()->imm(); }
  wasm::SimdOp simdOp() const { return mir_->toWasmReduceSimd128()->simdOp(); }
};

class LWasmLoadLaneSimd128 : public LInstructionHelper<1, 3, 1> {
 public:
  LIR_HEADER(WasmLoadLaneSimd128);

  static constexpr uint32_t Src = 2;

  explicit LWasmLoadLaneSimd128(const LAllocation& ptr, const LAllocation& src,
                                const LDefinition& temp,
                                const LAllocation& memoryBase)
      : LInstructionHelper(classOpcode) {
    setOperand(0, ptr);
    setOperand(1, memoryBase);
    setOperand(Src, src);
    setTemp(0, temp);
  }

  const LAllocation* ptr() { return getOperand(0); }
  const LAllocation* memoryBase() { return getOperand(1); }
  const LAllocation* src() { return getOperand(Src); }
  const LDefinition* temp() { return getTemp(0); }
  MWasmLoadLaneSimd128* mir() const { return mir_->toWasmLoadLaneSimd128(); }
  uint32_t laneSize() const {
    return mir_->toWasmLoadLaneSimd128()->laneSize();
  }
  uint32_t laneIndex() const {
    return mir_->toWasmLoadLaneSimd128()->laneIndex();
  }
};

class LWasmStoreLaneSimd128 : public LInstructionHelper<1, 3, 1> {
 public:
  LIR_HEADER(WasmStoreLaneSimd128);

  static constexpr uint32_t Src = 2;

  explicit LWasmStoreLaneSimd128(const LAllocation& ptr, const LAllocation& src,
                                 const LDefinition& temp,
                                 const LAllocation& memoryBase)
      : LInstructionHelper(classOpcode) {
    setOperand(0, ptr);
    setOperand(1, memoryBase);
    setOperand(Src, src);
    setTemp(0, temp);
  }

  const LAllocation* ptr() { return getOperand(0); }
  const LAllocation* memoryBase() { return getOperand(1); }
  const LAllocation* src() { return getOperand(Src); }
  const LDefinition* temp() { return getTemp(0); }
  MWasmStoreLaneSimd128* mir() const { return mir_->toWasmStoreLaneSimd128(); }
  uint32_t laneSize() const {
    return mir_->toWasmStoreLaneSimd128()->laneSize();
  }
  uint32_t laneIndex() const {
    return mir_->toWasmStoreLaneSimd128()->laneIndex();
  }
};

// End Wasm SIMD

// End Wasm Exception Handling

}  // namespace jit
}  // namespace js

#endif /* jit_shared_LIR_shared_h */
