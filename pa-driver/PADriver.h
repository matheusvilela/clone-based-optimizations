
#include <sstream>
#include <ios>
#include <fstream>
#include <string>
#include <iostream>

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/Statistic.h"

#include "PointerAnalysis.h"

//#include <sstream>
//#include <sys/time.h>
//#include <sys/resource.h>
//#include <string>
//#include <vector>
//#include <set>
//#include <map>
//#include <iomanip>
//#include <fstream>
//#include <cstdlib>

//#include "llvm/Function.h"
//#include "llvm/Constants.h"
//#include "llvm/Analysis/DebugInfo.h"
//#include <llvm/Support/CommandLine.h>

using namespace llvm;

STATISTIC(PABaseCt,  "Counts number of base constraints");
STATISTIC(PAAddrCt,  "Counts number of address constraints");
STATISTIC(PALoadCt,  "Counts number of load constraints");
STATISTIC(PAStoreCt, "Counts number of store constraints");
STATISTIC(PANumVert, "Counts number of vertices");
STATISTIC(PAMerges,  "Counts number of merged vertices");
STATISTIC(PARemoves, "Counts number of calls to remove cycle");
STATISTIC(PAMemUsage, "kB of memory");

class PADriver : public ModulePass {
	public:
	// +++++ FIELDS +++++ //
	// Used to assign a int ID to Values and store names
	int currInd;
	int nextMemoryBlock;
	std::map<Value*, int> value2int;
	//std::map<int, Value*> int2value;

	std::map<Value*, int> valMap;
	std::map<Value*, std::vector<int> > valMem;
	std::map<int, std::string> nameMap;

	std::map<Value*, std::vector<int> > memoryBlock;
	std::map<int, std::vector<int> > memoryBlock2;
	std::map<Value*, std::vector<Value*> > phiValues;
	std::map<Value*, std::vector<std::vector<int> > > memoryBlocks;

	static char ID;
	PointerAnalysis* pointerAnalysis;

	PADriver() : ModulePass(ID) {
		pointerAnalysis = new PointerAnalysis();
		currInd = 0;
		nextMemoryBlock = 1;

		PAAddrCt = 0;
		PABaseCt = 0;
		PALoadCt = 0;
		PAStoreCt = 0;
		PANumVert = 0;
		PARemoves = 0;
		PAMerges = 0;
		PAMemUsage = 0;
	}

	// +++++ METHODS +++++ //

	bool runOnModule(Module &M);
	int Value2Int(Value* v);
	int getNewMem(std::string name);
	int getNewInt(); 
	int getNewMemoryBlock();
	void handleNestedStructs(const Type *Ty, int parent);
	void handleAlloca(Instruction *I);
	//Value* Int2Value(int);
	virtual void print(raw_ostream& O, const Module* M) const;
	std::string intToStr(int v);
#ifndef _WIN32
	void process_mem_usage(double& vm_usage, double& resident_set);
#endif
	void addConstraints(Function &F);
	void matchFormalWithActualParameters(Function &F);
	void matchReturnValueWithReturnVariable(Function &F);

};
