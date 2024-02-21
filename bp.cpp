#include "bp_api.h"
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>
#include <bitset>

enum stateFSM
{
    SNT = 0,
    WNT = 1,
    WT = 2,
    ST = 3
};

enum shareStatus
{
    not_using_share = 0,
    using_share_lsb = 1,
    using_share_mid = 2
};

class BTBEntry
{
protected:
    uint32_t m_tag;
    uint32_t m_target;
    uint32_t m_localHistory;
    
public:
    BTBEntry(){}
    
    uint32_t getTag() {return m_tag;}
    
    uint32_t getTarget() {return m_target;}
    
    uint32_t getLocalHistory() {return m_localHistory;}
    
    virtual void updateFsm(bool branchTaken, int historyIndex)
    {}
};

//This class implements a unique vector of FSM for every branch (PC) (LOCAL)
class LocalBTBEntry : public BTBEntry
{
private:
    std::vector<std::shared_ptr<stateFSM>> m_FsmVector;

public:
    explicit LocalBTBEntry(uint32_t tag, uint32_t target, uint32_t localHistory, stateFSM defaultState, int sizeOfHistoryReg)
    {
        m_tag = tag;
        m_target = target;
        m_localHistory = localHistory;
        int FsmVectorSize = std::pow(2, sizeOfHistoryReg);
        m_FsmVector.resize(FsmVectorSize);
    }
    
    virtual void updateFsm(bool branchTaken, int historyIndex) // the responsibility of correct history index calculation (local/global) is on the BTB
    {
        if(branchTaken)
            *m_FsmVector[historyIndex].get() = (stateFSM)std::min((int)(*m_FsmVector[historyIndex].get())+1, 3);
        else
            *m_FsmVector[historyIndex].get() = (stateFSM)std::max((int)(*m_FsmVector[historyIndex].get())-1, 0);
    }
};

class BTB;

//This class has one (GLOBAL) collective vector of FSM for all branches, can be found in BTB class
class GlobalBTBEntry : public BTBEntry
{
public:
    virtual void updateFsm(bool branchTaken, int historyIndex); // the responsibility of correct history index calculation (local/global) is on the BTB
};

class BTB
{
private:
    unsigned m_tableSize;
    unsigned m_historyRegSize; // in bits
    unsigned m_tagSize; //in bits
    unsigned m_defaultFsmState; //0-3
    bool m_isGlobalHist;
    bool m_isGlobalTable;
    int m_isShare;
    int m_numBitsForIndex;
    unsigned m_globalHistoryReg;
    
    std::vector<std::shared_ptr<BTBEntry>> m_BTB{};
    
    static std::vector<std::shared_ptr<stateFSM>> m_globalFsmVector; //all instances of GlobalBTBEntry will be able to access/modify this
    
public:
    explicit BTB(unsigned btbSize, unsigned historySize, unsigned tagSize, unsigned fsmState,
                 bool isGlobalHist, bool isGlobalTable, int isShare) : m_tableSize(btbSize), m_historyRegSize(historySize), m_tagSize(tagSize), m_defaultFsmState(fsmState), m_isGlobalHist(isGlobalHist), m_isGlobalTable(isGlobalTable), m_isShare(isShare), m_globalHistoryReg(0)
    {
        m_BTB.resize(m_tableSize);
        
        if(isGlobalTable)
        {
            int FsmVectorSize = std::pow(2, m_historyRegSize);
            m_globalFsmVector.resize(FsmVectorSize);
        }
        else
            m_globalFsmVector.resize(0);
        
        m_numBitsForIndex = log(m_tableSize) / log(2.0);
    }
    
    int calcBTBEntryIndex(uint32_t pc)
    {
        if(m_tableSize == 1)
            return 0;
        pc >>= 2; //shift pc to get rid of the two lsb
        uint32_t bitmask = (1 << m_numBitsForIndex) - 1; //create a mask of 1s of appropriate size
        uint32_t result = pc & bitmask; // perform bitwise and
        return (int)result;
    }
    
    uint32_t calcBTBEntryTag(uint32_t pc)
    {
        int totalShift = 2 + m_numBitsForIndex;
        pc >>= totalShift; //shift pc to get rid of the two lsb + index bits
        uint32_t bitmask = (1 << m_tagSize) - 1; //create a mask of 1s of appropriate size
        return pc & bitmask; // perform bitwise and
    }
    
    friend class GlobalBTBEntry;
    
};

std::vector<std::shared_ptr<stateFSM>> BTB::m_globalFsmVector; //define outside so linker can see

void GlobalBTBEntry::updateFsm(bool branchTaken, int historyIndex) // the responsibility of correct history index calculation (local/global) is on the BTB
{
    if(branchTaken)
        *BTB::m_globalFsmVector[historyIndex].get() = (stateFSM)std::min((int)(*BTB::m_globalFsmVector[historyIndex].get())+1, 3);
    else
        *BTB::m_globalFsmVector[historyIndex].get() = (stateFSM)std::max((int)(*BTB::m_globalFsmVector[historyIndex].get())-1, 0);
}

static std::shared_ptr<BTB> BranchTargetBuffer;

int BP_init(unsigned btbSize, unsigned historySize, unsigned tagSize, unsigned fsmState,
			bool isGlobalHist, bool isGlobalTable, int isShare)
{
    BranchTargetBuffer = std::make_shared<BTB>(btbSize, historySize, tagSize, fsmState, isGlobalHist, isGlobalTable, isShare);
	return -1;
}

bool BP_predict(uint32_t pc, uint32_t *dst){
	return false;
}

void BP_update(uint32_t pc, uint32_t targetPc, bool taken, uint32_t pred_dst){
	return;
}

void BP_GetStats(SIM_stats *curStats){
	return;
}

