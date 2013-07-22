//===- ConstraintsGraph.cpp - Build a Module's constraints graph ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the ConstraintsGraph class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ConstraintsGraph.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Target/TargetLibraryInfo.h"

namespace {

// Position of the function return node relative to the function node.
static const unsigned CallReturnPos = 1;

// Position of the function call node relative to the function node.
static const unsigned CallFirstArgPos = 2;

} // empty namespace

namespace llvm {

//===----------------------------------------------------------------------===//
// ConstraintsGraph class definition
//

// Compute the constraints graph for the specified module.
void ConstraintsGraph::Initialize(Module &M, Pass *P) {
  assert(Mod == 0 && "Double initialization, call destroy first");
  Mod = &M;

  TLI = P->getAnalysisIfAvailable<TargetLibraryInfo>();

  identifyObjects(M);
  collectConstraints(M);

  // Now clear temp maps, maintaining only
  // the constraints and the codes.

  ValueNodes.clear();
  ObjectNodes.clear();
  ReturnNodes.clear();
  VarargNodes.clear();
}

void ConstraintsGraph::dump() const {
  print(errs());
}

void ConstraintsGraph::print(raw_ostream &OS) const {
  OS << "graph G {\n";
  for (unsigned n = 0; n < getNumNodes(); ++n) {
    Value *V = getNode(n).getValue();
    OS << n << " [label=\"";
    if (n == UniversalSet) OS << "<universal>";
    else if (n == NullPtr) OS << "<nullptr>";
    else if (n == NullObject) OS << "<nullobj>";
    else if (V) V->print(OS);
    else OS << "(unnamed)";
    OS << "\"];\n";
  }
  for (unsigned c = 0; c < getNumConstraints(); ++c) {
    const Constraint &C = getConstraint(c);
    OS << C.getSource()
       << " -> "
       << C.getTarget()
       << " [label=\"";
    if (C.getType() == AddressOf) OS << "addressof";
    else if (C.getType() == Copy) OS << "copy";
    else if (C.getType() == Load) OS << "load";
    else if (C.getType() == Store) OS << "store";
    OS << "\"];\n";
  }
  OS << "}";
}

void ConstraintsGraph::destroy() {
  Nodes.clear();
  Constraints.clear();
  Mod = 0;
}

unsigned ConstraintsGraph::getNode(Value *V) {
  if (Constant *C = dyn_cast<Constant>(V)) {
    if (!isa<GlobalValue>(C))
      return getNodeForConstantPointer(C);
  }

  DenseMap<Value*, unsigned>::iterator I = ValueNodes.find(V);
  if (I == ValueNodes.end()) {
#ifndef NDEBUG
    V->dump();
#endif
    llvm_unreachable("Value does not have a node in the points-to graph!");
  }
  return I->second;
}

unsigned ConstraintsGraph::getObject(Value *V) const {
  DenseMap<Value*, unsigned>::const_iterator I = ObjectNodes.find(V);
  assert(I != ObjectNodes.end() &&
          "Value does not have an object in the points-to graph!");
  return I->second;
}

unsigned ConstraintsGraph::getReturnNode(Function *F) const {
  DenseMap<Function*, unsigned>::const_iterator I = ReturnNodes.find(F);
  assert(I != ReturnNodes.end() && "Function does not return a value!");
  return I->second;
}

unsigned ConstraintsGraph::getVarargNode(Function *F) const {
  DenseMap<Function*, unsigned>::const_iterator I = VarargNodes.find(F);
  assert(I != VarargNodes.end() && "Function does not take var args!");
  return I->second;
}

unsigned ConstraintsGraph::getNodeValue(Value &V) {
  unsigned Index = getNode(&V);
  Nodes[Index].setValue(&V);
  return Index;
}

unsigned ConstraintsGraph::addConstraint(ConstraintType Type, unsigned Target,
                                         unsigned Source, unsigned Offset) {
  unsigned Idx = Constraints.size();
  Constraints.push_back(Constraint(Type, Target, Source, Offset));
  getNode(Source).addConstraint(Idx);
  return Idx;
}

unsigned ConstraintsGraph::getNodeForConstantPointer(Constant *C) {
  assert(C->getType()->isPointerTy() && "Not a constant pointer!");

  if (isa<ConstantPointerNull>(C) || isa<UndefValue>(C))
    return NullPtr;
  else if (GlobalValue *GV = dyn_cast<GlobalValue>(C))
    return getNode(GV);
  else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(C)) {
    switch (CE->getOpcode()) {
    case Instruction::GetElementPtr:
      return getNodeForConstantPointer(CE->getOperand(0));
    case Instruction::IntToPtr:
      return UniversalSet;
    case Instruction::BitCast:
      return getNodeForConstantPointer(CE->getOperand(0));
    default:
      errs() << "Constant Expr not yet handled: " << *CE << "\n";
      llvm_unreachable(0);
    }
  } else {
    llvm_unreachable("Unknown constant pointer!");
  }
  return 0;
}

void ConstraintsGraph::addGlobalInitializerConstraints(unsigned NodeIndex,
                                                       Constant *C) {
  if (C->getType()->isSingleValueType()) {
    if (C->getType()->isPointerTy())
      addConstraint(Copy, NodeIndex, getNodeForConstantPointer(C));
  } else if (C->isNullValue()) {
    addConstraint(Copy, NodeIndex, NullObject);
    return;
  } else if (!isa<UndefValue>(C)) {
    // If this is an array or struct, include constraints for each element.
    assert(isa<ConstantArray>(C) || isa<ConstantStruct>(C));
    for (unsigned i = 0, e = C->getNumOperands(); i != e; ++i) {
      addGlobalInitializerConstraints(NodeIndex,
                                      cast<Constant>(C->getOperand(i)));
    }
  }
}

void ConstraintsGraph::addConstraintsForNonInternalLinkage(Function *F) {
  for (Function::arg_iterator I = F->arg_begin(), E = F->arg_end(); I != E; ++I) {
    if (I->getType()->isPointerTy()) {
      // If this is an argument of an externally accessible function, the
      // incoming pointer might point to anything.
      addConstraint(Copy, getNode(I), UniversalSet);
    }
  }
}

void ConstraintsGraph::visitReturnInst(ReturnInst &RI) {
  if (RI.getNumOperands() && RI.getOperand(0)->getType()->isPointerTy())
    // return V   -->   <Copy/retval{F}/v>
    addConstraint(Copy, getReturnNode(RI.getParent()->getParent()),
                        getNode(RI.getOperand(0)));
}

void ConstraintsGraph::visitCallInst(CallInst &CI) { 
  if (isMallocLikeFn(&CI, TLI) || isCallocLikeFn(&CI, TLI)) {
    visitAlloc(CI);
  }
  else {
    visitCallSite(CallSite(&CI));
  }
}

void ConstraintsGraph::visitCallSite(CallSite CS) {
  if (CS.getType()->isPointerTy())
    getNodeValue(*CS.getInstruction());

  if (Function *F = CS.getCalledFunction()) {
    addConstraintsForCall(CS, F);
  } else {
    addConstraintsForCall(CS, NULL);
  }
}

void ConstraintsGraph::visitAllocaInst(AllocaInst &I) {
  visitAlloc(I);
}

void ConstraintsGraph::visitLoadInst(LoadInst &LI) {
  if (LI.getType()->isPointerTy())
    // P1 = load P2  -->  <Load/P1/P2>
    addConstraint(Load, getNodeValue(LI),
                        getNode(LI.getOperand(0)));
}

void ConstraintsGraph::visitStoreInst(StoreInst &SI) {
  if (SI.getOperand(0)->getType()->isPointerTy())
    // store P1, P2  -->  <Store/P2/P1>
    addConstraint(Store, getNode(SI.getOperand(1)),
                         getNode(SI.getOperand(0)));
}

void ConstraintsGraph::visitGetElementPtrInst(GetElementPtrInst &GEP) {
  // P1 = getelementptr P2, ... --> <Copy/P1/P2>
  addConstraint(Copy, getNodeValue(GEP),
                      getNode(GEP.getOperand(0)));
}

void ConstraintsGraph::visitPHINode(PHINode &PN) {
  if (PN.getType()->isPointerTy()) {
    unsigned PNN = getNodeValue(PN);
    for (unsigned i = 0, e = PN.getNumIncomingValues(); i != e; ++i)
      // P1 = phi P2, P3  -->  <Copy/P1/P2>, <Copy/P1/P3>, ...
      addConstraint(Copy, PNN,
                          getNode(PN.getIncomingValue(i)));
  }
}

void ConstraintsGraph::visitCastInst(CastInst &CI) {
  Value *Op = CI.getOperand(0);
  if (CI.getType()->isPointerTy()) {
    if (Op->getType()->isPointerTy()) {
      // P1 = cast P2  --> <Copy/P1/P2>
      addConstraint(Copy, getNodeValue(CI),
                          getNode(CI.getOperand(0)));
    }
    else {
      // P1 = cast int --> <Copy/P1/Univ>
      // XXX: dangerous cast
      getNodeValue(CI);
    }
  } else if (Op->getType()->isPointerTy()) {
    // int = cast P1 --> <Copy/Univ/P1>
    // XXX: not so dangerous cast
    getNode(CI.getOperand(0));
  }
}

void ConstraintsGraph::visitSelectInst(SelectInst &SI) {
  if (SI.getType()->isPointerTy()) {
    unsigned SIN = getNodeValue(SI);
    // P1 = select C, P2, P3   ---> <Copy/P1/P2>, <Copy/P1/P3>
    addConstraint(Copy, SIN,
                        getNode(SI.getOperand(1)));
    addConstraint(Copy, SIN,
                        getNode(SI.getOperand(2)));
  }
}

void ConstraintsGraph::visitVAArg(VAArgInst &I) {
  // XXX: not handled yet!
}

void ConstraintsGraph::visitInstruction(Instruction &I) {
  // XXX: ignore unknown instructions for now
}

void ConstraintsGraph::visitAlloc(Instruction &I) {
  unsigned ObjectIndex = getObject(&I);
  setNodeValue(ObjectIndex, &I);
  addConstraint(AddressOf, getNodeValue(I), ObjectIndex);
}

// Add constraints for a call with actual arguments specified by CS to the
// function specified by F.  Note that the types of arguments might not match
// up in the case where this is an indirect call and the function pointer has
// been casted.  If this is the case, do something reasonable.
void ConstraintsGraph::addConstraintsForCall(CallSite CS, Function *F) {
  Value *CallValue = CS.getCalledValue();
  bool IsDeref = F == NULL;

  // If this is a call to an external function, try to handle it directly to get
  // some taste of context sensitivity.
  if (F && F->isDeclaration() && addConstraintsForExternalCall(CS, F))
    return;

  if (CS.getType()->isPointerTy()) {
    unsigned CSN = getNode(CS.getInstruction());
    if (!F || F->getFunctionType()->getReturnType()->isPointerTy()) {
      if (IsDeref)
        addConstraint(Load, CSN, getNode(CallValue), CallReturnPos);
      else
        addConstraint(Copy, CSN, getNode(CallValue) + CallReturnPos);
    } else {
      // If the function returns a non-pointer value, handle this just like we
      // treat a nonpointer cast to pointer.
      addConstraint(Copy, CSN, UniversalSet);
    }
  } else if (F && F->getFunctionType()->getReturnType()->isPointerTy()) {
    addConstraint(Copy, getNode(CallValue) + CallReturnPos, UniversalSet);
  }

  CallSite::arg_iterator ArgI = CS.arg_begin(), ArgE = CS.arg_end();
  bool external = !F ||  F->isDeclaration();
  if (F) {
    // Direct Call
    Function::arg_iterator AI = F->arg_begin(), AE = F->arg_end();
    for (; AI != AE && ArgI != ArgE; ++AI, ++ArgI) {
      if (external && (*ArgI)->getType()->isPointerTy()) {
        // Add constraint that ArgI can now point to anything due to
        // escaping, as can everything it points to. The second portion of
        // this should be taken care of by universal = *universal
        addConstraint(Copy, getNode(*ArgI), UniversalSet);
      }
      if (AI->getType()->isPointerTy()) {
        if ((*ArgI)->getType()->isPointerTy()) {
          // Copy the actual argument into the formal argument.
          addConstraint(Copy, getNode(AI), getNode(*ArgI));
        } else {
          addConstraint(Copy, getNode(AI), UniversalSet);
        }
      } else if ((*ArgI)->getType()->isPointerTy()) {
        addConstraint(Copy, getNode(*ArgI), UniversalSet);
      }
    }
  } else {
    // Indirect Call
    unsigned ArgPos = CallFirstArgPos;
    for (; ArgI != ArgE; ++ArgI) {
      if ((*ArgI)->getType()->isPointerTy()) {
        // Copy the actual argument into the formal argument.
        addConstraint(Store, getNode(CallValue), getNode(*ArgI), ArgPos++);
      } else {
        addConstraint(Store, getNode(CallValue), UniversalSet, ArgPos++);
      }
    }
  }
  // Copy all pointers passed through the varargs section to the varargs node.
  if (F && F->getFunctionType()->isVarArg()) {
    for (; ArgI != ArgE; ++ArgI) {
      if ((*ArgI)->getType()->isPointerTy())
        addConstraint(Copy, getVarargNode(F), getNode(*ArgI));
    }
  }
  // If more arguments are passed in than we track, just drop them on the floor.
}

// If this is a call to a "known" function, add the constraints and return
// true.  If this is a call to an unknown function, return false.
bool ConstraintsGraph::addConstraintsForExternalCall(CallSite CS, Function *F) {
  assert(F->isDeclaration() && "Not an external function!");

  // These functions don't induce any points-to constraints.
  if (F->getName() == "atoi" || F->getName() == "atof" ||
      F->getName() == "atol" || F->getName() == "atoll" ||
      F->getName() == "remove" || F->getName() == "unlink" ||
      F->getName() == "rename" || F->getName() == "memcmp" ||
      F->getName() == "llvm.memset" ||
      F->getName() == "strcmp" || F->getName() == "strncmp" ||
      F->getName() == "execl" || F->getName() == "execlp" ||
      F->getName() == "execle" || F->getName() == "execv" ||
      F->getName() == "execvp" || F->getName() == "chmod" ||
      F->getName() == "puts" || F->getName() == "write" ||
      F->getName() == "open" || F->getName() == "create" ||
      F->getName() == "truncate" || F->getName() == "chdir" ||
      F->getName() == "mkdir" || F->getName() == "rmdir" ||
      F->getName() == "read" || F->getName() == "pipe" ||
      F->getName() == "wait" || F->getName() == "time" ||
      F->getName() == "stat" || F->getName() == "fstat" ||
      F->getName() == "lstat" || F->getName() == "strtod" ||
      F->getName() == "strtof" || F->getName() == "strtold" ||
      F->getName() == "fopen" || F->getName() == "fdopen" ||
      F->getName() == "freopen" ||
      F->getName() == "fflush" || F->getName() == "feof" ||
      F->getName() == "fileno" || F->getName() == "clearerr" ||
      F->getName() == "rewind" || F->getName() == "ftell" ||
      F->getName() == "ferror" || F->getName() == "fgetc" ||
      F->getName() == "fgetc" || F->getName() == "_IO_getc" ||
      F->getName() == "fwrite" || F->getName() == "fread" ||
      F->getName() == "fgets" || F->getName() == "ungetc" ||
      F->getName() == "fputc" ||
      F->getName() == "fputs" || F->getName() == "putc" ||
      F->getName() == "ftell" || F->getName() == "rewind" ||
      F->getName() == "_IO_putc" || F->getName() == "fseek" ||
      F->getName() == "fgetpos" || F->getName() == "fsetpos" ||
      F->getName() == "printf" || F->getName() == "fprintf" ||
      F->getName() == "sprintf" || F->getName() == "vprintf" ||
      F->getName() == "vfprintf" || F->getName() == "vsprintf" ||
      F->getName() == "scanf" || F->getName() == "fscanf" ||
      F->getName() == "sscanf" || F->getName() == "__assert_fail" ||
      F->getName() == "modf")
    return true;

  // These functions do induce points-to edges.
  if (F->getName() == "llvm.memcpy" ||
      F->getName() == "llvm.memmove" ||
      F->getName() == "memcpy" ||
      F->getName() == "memmove") {

    const FunctionType *FTy = F->getFunctionType();
    if (FTy->getNumParams() > 1 && 
        FTy->getParamType(0)->isPointerTy() &&
        FTy->getParamType(1)->isPointerTy()) {

      // *Dest = *Src, which requires an artificial graph node to represent the
      // constraint.  It is broken up into *Dest = temp, temp = *Src
      unsigned FirstArg = getNode(CS.getArgument(0));
      unsigned SecondArg = getNode(CS.getArgument(1));
      unsigned TempArg = Nodes.size();
      Nodes.push_back(Node());
      addConstraint(Store, FirstArg, TempArg);
      addConstraint(Load, TempArg, SecondArg);
      return true;
    }
  }

  // Result = Arg0
  if (F->getName() == "realloc" || F->getName() == "strchr" ||
      F->getName() == "strrchr" || F->getName() == "strstr" ||
      F->getName() == "strtok") {
    const FunctionType *FTy = F->getFunctionType();
    if (FTy->getNumParams() > 0 && 
        FTy->getParamType(0)->isPointerTy()) {
      addConstraint(Copy, getNode(CS.getInstruction()),
                          getNode(CS.getArgument(0)));
      return true;
    }
  }

  return false;
}

void ConstraintsGraph::identifyObjects(Module &M) {
  unsigned NumObjects = 0;

  // Object #0 is always the universal set: the object that we don't know
  // anything about.
  ++NumObjects;

  // Object #1 always represents the null pointer.
  ++NumObjects;

  // Object #2 always represents the null object (the object pointed to by null)
  ++NumObjects;

  // Add all the globals first.
  for (Module::global_iterator I = M.global_begin(), E = M.global_end();
       I != E; ++I) {
    ObjectNodes[I] = NumObjects++;
    ValueNodes[I] = NumObjects++;
  }

  // Add nodes for all of the functions and the instructions inside of them.
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    // The function itself is a memory object.
    unsigned First = NumObjects;
    ValueNodes[F] = NumObjects++;
    if (F->getFunctionType()->getReturnType()->isPointerTy())
      ReturnNodes[F] = NumObjects++;
    if (F->getFunctionType()->isVarArg())
      VarargNodes[F] = NumObjects++;

    // Add nodes for all of the incoming pointer arguments.
    for (Function::arg_iterator I = F->arg_begin(), E = F->arg_end();
         I != E; ++I) {
      if (I->getType()->isPointerTy())
        ValueNodes[I] = NumObjects++;
    }

    // Scan the function body, creating a memory object for each heap/stack
    // allocation in the body of the function and a node to represent all
    // pointer values defined by instructions and used as operands.
    for (inst_iterator II = inst_begin(F), E = inst_end(F); II != E; ++II) {
      // If this is an heap or stack allocation, create a node for the memory
      // object.
      if (II->getType()->isPointerTy()) {
        ValueNodes[&*II] = NumObjects++;
        if (AllocaInst *AI = dyn_cast<AllocaInst>(&*II))
          ObjectNodes[AI] = NumObjects++;
        else if (isMallocLikeFn(&*II, TLI))
          ObjectNodes[&*II] = NumObjects++;
      }

      // Calls to inline asm need to be added as well because the callee isn't
      // referenced anywhere else.
      if (CallInst *CI = dyn_cast<CallInst>(&*II)) {
        Value *Callee = CI->getCalledValue();
        if (isa<InlineAsm>(Callee))
          ValueNodes[Callee] = NumObjects++;
      }
    }
  }

  // Now that we know how many objects to create, make them all now!
  allocateNodes(NumObjects);
}

void ConstraintsGraph::collectConstraints(Module &M) {
  // First, the universal set points to itself.
  addConstraint(AddressOf, UniversalSet, UniversalSet);
  addConstraint(Store, UniversalSet, UniversalSet);

  // Next, the null pointer points to the null object.
  addConstraint(AddressOf, NullPtr, NullObject);

  // Next, add any constraints on global variables and their initializers.
  for (Module::global_iterator I = M.global_begin(), E = M.global_end();
       I != E; ++I) {
    // Associate the address of the global object as pointing to the memory for
    // the global: &G = <G memory>
    unsigned ObjectIndex = getObject(I);
    Node *Object = &Nodes[ObjectIndex];
    Object->setValue(I);
    addConstraint(AddressOf, getNodeValue(*I), ObjectIndex);

    if (I->hasDefinitiveInitializer()) {
      addGlobalInitializerConstraints(ObjectIndex, I->getInitializer());
    } else {
      // If it doesn't have an initializer (i.e. it's defined in another
      // translation unit), it points to the universal set.
      addConstraint(Copy, ObjectIndex, UniversalSet);
    }
  }

  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    // Set up the return value node.
    if (F->getFunctionType()->getReturnType()->isPointerTy())
      Nodes[getReturnNode(F)].setValue(F);
    if (F->getFunctionType()->isVarArg())
      Nodes[getVarargNode(F)].setValue(F);

    // Set up incoming argument nodes.
    for (Function::arg_iterator I = F->arg_begin(), E = F->arg_end();
         I != E; ++I)
      if (I->getType()->isPointerTy())
        getNodeValue(*I);

    // At some point we should just add constraints for the escaping functions
    // at solve time, but this slows down solving. For now, we simply mark
    // address taken functions as escaping and treat them as external.
    if (!F->hasLocalLinkage() || analyzeUsesOfFunction(F))
      addConstraintsForNonInternalLinkage(F);

    if (!F->isDeclaration()) {
      // Scan the function body, creating a memory object for each heap/stack
      // allocation in the body of the function and a node to represent all
      // pointer values defined by instructions and used as operands.
      visit(F);
    } else {
      // External functions that return pointers return the universal set.
      if (F->getFunctionType()->getReturnType()->isPointerTy())
        addConstraint(Copy, getReturnNode(F), UniversalSet);

      // Any pointers that are passed into the function have the universal set
      // stored into them.
      for (Function::arg_iterator I = F->arg_begin(), E = F->arg_end();
           I != E; ++I) {
        if (I->getType()->isPointerTy()) {
          // Pointers passed into external functions could have anything stored
          // through them.
          addConstraint(Store, getNode(I), UniversalSet);

          // Memory objects passed into external function calls can have the
          // universal set point to them.
          addConstraint(Copy, getNode(I), UniversalSet);
        }
      }

      // If this is an external varargs function, it can also store pointers
      // into any pointers passed through the varargs section.
      if (F->getFunctionType()->isVarArg())
        addConstraint(Store, getVarargNode(F), UniversalSet);
    }
  }
}

bool ConstraintsGraph::analyzeUsesOfFunction(Value *V) {
  if (!V->getType()->isPointerTy())
    return true;

  for (Value::use_iterator UI = V->use_begin(), E = V->use_end(); UI != E; ++UI) {
    if (isa<LoadInst>(*UI)) {
      return false;
    } else if (StoreInst *SI = dyn_cast<StoreInst>(*UI)) {
      if (V == SI->getOperand(1)) {
        return false;
      } else if (SI->getOperand(1)) {
        return true;  // Storing the pointer
      }
    } else if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(*UI)) {
      if (analyzeUsesOfFunction(GEP))
        return true;
    } else if (isFreeCall(*UI, TLI)) {
      return false;
    } else if (CallInst *CI = dyn_cast<CallInst>(*UI)) {
      // Make sure that this is just the function being called, not that it is
      // passing into the function.
      for (unsigned i = 1, e = CI->getNumOperands(); i != e; ++i)
        if (CI->getOperand(i) == V) return true;
    } else if (InvokeInst *II = dyn_cast<InvokeInst>(*UI)) {
      // Make sure that this is just the function being called, not that it is
      // passing into the function.
      for (unsigned i = 3, e = II->getNumOperands(); i != e; ++i)
        if (II->getOperand(i) == V) return true;
    } else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(*UI)) {
      if (CE->getOpcode() == Instruction::GetElementPtr ||
          CE->getOpcode() == Instruction::BitCast) {
        if (analyzeUsesOfFunction(CE)) {
          return true;
        }
      } else {
        return true;
      }
    } else if (ICmpInst *ICI = dyn_cast<ICmpInst>(*UI)) {
      if (!isa<ConstantPointerNull>(ICI->getOperand(1)))
        return true;  // Allow comparison against null.
    } else {
      return true;
    }
  }
  return false;
}

} // End of llvm namespace.

// Enuse that users of ConstraintsGraph.h also link with this file
DEFINING_FILE_FOR(ConstraintsGraph)
