#include "bp_api.h"
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>
#include <bitset>
#include <iostream>

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
    
    virtual ~BTBEntry() {}
    
    uint32_t getTag() {return m_tag;}
    
    uint32_t getTarget() {return m_target;}
    
    uint32_t getLocalHistory() {return m_localHistory;}
    
    //This will place the most recent result in the lsb of the history register and get rid of the oldest result
    //mostRecentResult should be 0 (branch not taken) or 1 (branch taken)
    void updateLocalHistory(int mostRecentResult, int sizeOfHistory)
    {
        m_localHistory <<= 1;
        uint32_t bitmask = (1 << sizeOfHistory) - 1;
        m_localHistory = m_localHistory & bitmask;
        m_localHistory = m_localHistory + mostRecentResult;
    }
    
    virtual void updateFsm(bool branchTaken, int historyIndex)
    {}
    
    virtual bool getPrediction(int index)
    {
        return false;
    }
};

//This class implements a unique vector of FSM for every branch (PC) (LOCAL)
class LocalBTBEntry : public BTBEntry
{
private:
    std::vector<std::unique_ptr<stateFSM>> m_FsmVector;

public:
    explicit LocalBTBEntry(uint32_t tag, uint32_t target, stateFSM defaultState, int sizeOfHistoryReg)
    {
        m_tag = tag;
        m_target = target;
        m_localHistory = 0;
        int FsmVectorSize = std::pow(2, sizeOfHistoryReg);
        m_FsmVector.reserve(FsmVectorSize);

        // Emplace defaultState into each element of the vector
        for (int i = 0; i < FsmVectorSize; ++i) {
            m_FsmVector.emplace_back(std::make_unique<stateFSM>(defaultState));
        }
    }
    
    virtual void updateFsm(bool branchTaken, int historyIndex) // the responsibility of correct history index calculation (local/global and XOR) is on the BTB
    {
        if(branchTaken)
            *m_FsmVector[historyIndex].get() = (stateFSM)std::min((int)(*m_FsmVector[historyIndex].get())+1, 3);
        else
            *m_FsmVector[historyIndex].get() = (stateFSM)std::max((int)(*m_FsmVector[historyIndex].get())-1, 0);
    }
    
    bool getPrediction(int index)
    {
        if(*m_FsmVector[index].get() == stateFSM::ST || *m_FsmVector[index].get() == stateFSM::WT)
            return true;
        
        return false;
    }
};

class BTB;

//This class has one (GLOBAL) collective vector of FSM for all branches, can be found in BTB class
class GlobalBTBEntry : public BTBEntry
{
public:
    explicit GlobalBTBEntry(uint32_t tag, uint32_t target)
    {
        m_tag = tag;
        m_target = target;
        m_localHistory = 0;
    }
    virtual void updateFsmGlobal(bool branchTaken, int historyIndex, BTB& tableObj) ; // the responsibility of correct history index calculation (local/global and XOR) is on the BTB
};

class BTB
{
private:
    unsigned m_tableSize;
    unsigned m_historyRegSize; // in bits
    unsigned m_tagSize; //in bits
    stateFSM m_defaultFsmState; //0-3
    bool m_isGlobalHist;
    bool m_isGlobalTable;
    shareStatus m_shareStatus;
    int m_numBitsForIndex;
    unsigned m_globalHistoryReg;
    
    int m_updateCount;
    int m_flushCount;
    
    std::vector<std::unique_ptr<BTBEntry>> m_BTB{};
    
    static std::vector<std::unique_ptr<stateFSM>> m_globalFsmVector; //all instances of GlobalBTBEntry will be able to access/modify this
    
public:
    explicit BTB(unsigned btbSize, unsigned historySize, unsigned tagSize, unsigned fsmState,
                 bool isGlobalHist, bool isGlobalTable, int isShare) : m_tableSize(btbSize), m_historyRegSize(historySize), m_tagSize(tagSize), m_defaultFsmState(stateFSM(fsmState)), m_isGlobalHist(isGlobalHist), m_isGlobalTable(isGlobalTable), m_shareStatus((shareStatus)isShare), m_globalHistoryReg(0), m_updateCount(0), m_flushCount(0)
    {
        m_BTB.reserve(m_tableSize);

        // Emplace defaultState into each element of the vector
        for (int i = 0; i < m_tableSize; ++i) {
            m_BTB.emplace_back(nullptr);
        }
        
        if(isGlobalTable)
        {
            int FsmVectorSize = std::pow(2, m_historyRegSize);
            m_globalFsmVector.reserve(FsmVectorSize);

            // Emplace defaultState into each element of the vector
            for (int i = 0; i < FsmVectorSize; ++i) {
                m_globalFsmVector.emplace_back(std::make_unique<stateFSM>(m_defaultFsmState));
            }
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
    
    uint32_t calcHistoryXOR(uint32_t historyReg, uint32_t pc)
    {
        if(m_shareStatus == shareStatus::not_using_share || !m_isGlobalTable)
            return historyReg;
       
        else if(m_shareStatus == shareStatus::using_share_lsb)
            pc >>= 2;
        
        else if(m_shareStatus == shareStatus::using_share_mid)
            pc >>= 16;
        
        uint32_t bitmask = (1 << m_historyRegSize) - 1;
        
        uint32_t resultXOR = historyReg ^ pc;
        
        return (resultXOR & bitmask);
    }
    
    void incrementUpdateCount()
    {
        m_updateCount++;
    }
    
    void incrementFlushCount()
    {
        m_flushCount++;
    }
    
    int getUpdateCount()
    {
        return m_updateCount;
    }
    
    int getFlushCount()
    {
        return m_flushCount;
    }
    
    bool branchExists(uint32_t pc, int BTBIndex, uint32_t tag)
    {
        //BTB entries are initialised to nullptr upon BTB construction, so this means the index is available and the branch has not been seen yet
        if(m_BTB[BTBIndex].get() == nullptr)
            return false;
        
        if(m_BTB[BTBIndex].get()->getTag() == tag)
            return true;
        
        return false;
    }
    
    void updateHistory(int BTBEntryIndex, int mostRecentResult)
    {
        if(m_isGlobalHist)
        {
            m_globalHistoryReg <<= 1;
            uint32_t bitmask = (1 << m_historyRegSize) - 1;
            m_globalHistoryReg = m_globalHistoryReg & bitmask;
            m_globalHistoryReg = m_globalHistoryReg + mostRecentResult;
            return;
        }
        
        m_BTB[BTBEntryIndex].get()->updateLocalHistory(mostRecentResult, m_historyRegSize);
    }
    
    uint32_t getHistory(int BTBEntryIndex)
    {
        if(m_isGlobalHist)
            return m_globalHistoryReg;
        
        return m_BTB[BTBEntryIndex].get()->getLocalHistory();
    }
    
    uint32_t getFsmIndex(int BTBEntryIndex, uint32_t pc)
    {
        return calcHistoryXOR(getHistory(BTBEntryIndex), pc);
    }
    
    void updateFSM(int BTBEntryIndex, bool branchTaken, uint32_t pc)
    {
        uint32_t fsmIndex = getFsmIndex(BTBEntryIndex, pc);
        if(!m_isGlobalTable)
        {
            m_BTB[BTBEntryIndex].get()->updateFsm(branchTaken, fsmIndex);
            return;
        }
        if(branchTaken)
            *m_globalFsmVector[fsmIndex].get() = (stateFSM)std::min((int)(*m_globalFsmVector[fsmIndex].get())+1, 3);
        else
            *m_globalFsmVector[fsmIndex].get() = (stateFSM)std::max((int)(*m_globalFsmVector[fsmIndex].get())-1, 0);
        /*int i = 0;
        for(auto& x : m_globalFsmVector)
        {
            if(*x == SNT)
                std::cout << "state of index " << i << "is SNT" << std::endl;
            else if(*x == WNT)
                std::cout << "state of index " << i << "is WNT" << std::endl;
            else if(*x == WT)
                std::cout << "state of index " << i << "is WT" << std::endl;
            else if(*x == ST)
                std::cout << "state of index " << i << "is ST" << std::endl;
            i++; 
        }*/
    }
    
    void addBranch(int index, uint32_t tag, uint32_t target)
    {
        if(m_isGlobalTable)
            m_BTB[index] = std::make_unique<GlobalBTBEntry>(tag, target);
        else
            m_BTB[index] = std::make_unique<LocalBTBEntry>(tag, target, m_defaultFsmState, (int)m_historyRegSize);
    }
    
    unsigned calcSize()
    {
        unsigned size = m_tableSize * (m_tagSize + 31); //unsure about the 31, check tests
        if(m_isGlobalHist)
            size += m_historyRegSize;
        else
            size += m_tableSize * m_historyRegSize;
        if(m_isGlobalTable)
            size += std::pow(2, m_historyRegSize + 1); //2 bit FSM * 2^historysize
        else   //each entry in BTB has 2 bit FSM * 2^historysize
            size += m_tableSize * std::pow(2, m_historyRegSize + 1);
        
        return size;
    }
    
    bool predict(uint32_t pc, uint32_t *dst)
    {
        int index = calcBTBEntryIndex(pc);
        uint32_t tag = calcBTBEntryTag(pc);
        if(!branchExists(pc, index, tag))
        {
            *dst = pc+4;
            return false;
        }
        uint32_t fsmIndex = getFsmIndex(index, pc);
        uint32_t target = m_BTB[index].get()->getTarget();
        if(m_isGlobalTable)
        {
            if(*m_globalFsmVector[fsmIndex].get() == stateFSM::ST || *m_globalFsmVector[fsmIndex].get() == stateFSM::WT)
            {
                *dst = target;
                return true;
            }
            
            *dst = pc+4;
            return false;
        }
        else
        {
            if(m_BTB[index].get()->getPrediction(fsmIndex))
            {
                *dst = target;
                return true;
            }
            *dst = pc+4;
            return false;
        }
        
        return false;
    }
    
    friend class GlobalBTBEntry;
    
};

std::vector<std::unique_ptr<stateFSM>> BTB::m_globalFsmVector; //define outside so linker can see

void GlobalBTBEntry::updateFsmGlobal(bool branchTaken, int historyIndex, BTB& tableObj) // the responsibility of correct history index calculation (local/global) is on the BTB
{
    if(branchTaken)
        *tableObj.m_globalFsmVector[historyIndex].get() = (stateFSM)std::min((int)(*tableObj.m_globalFsmVector[historyIndex].get())+1, 3);
    else
        *tableObj.m_globalFsmVector[historyIndex].get() = (stateFSM)std::max((int)(*tableObj.m_globalFsmVector[historyIndex].get())-1, 0);
}

static std::unique_ptr<BTB> BranchTargetBuffer;

int BP_init(unsigned btbSize, unsigned historySize, unsigned tagSize, unsigned fsmState,
			bool isGlobalHist, bool isGlobalTable, int isShare)
{
    try{
        BranchTargetBuffer.reset(new BTB(btbSize, historySize, tagSize, fsmState, isGlobalHist, isGlobalTable, isShare));
        return 0;
    } catch (...) {
        return -1;
    }

}

bool BP_predict(uint32_t pc, uint32_t *dst)
{
    return BranchTargetBuffer.get()->predict(pc, dst);
}

void BP_update(uint32_t pc, uint32_t targetPc, bool taken, uint32_t pred_dst)
{
    BranchTargetBuffer.get()->incrementUpdateCount();
    uint32_t tag = BranchTargetBuffer.get()->calcBTBEntryTag(pc);
    int index = BranchTargetBuffer.get()->calcBTBEntryIndex(pc);
    
    //If the branch does not exist in the BTB add it
    if(!BranchTargetBuffer.get()->branchExists(pc, index, tag))
        BranchTargetBuffer.get()->addBranch(index, tag, targetPc);
    
    BranchTargetBuffer.get()->updateFSM(index, taken, pc);
    BranchTargetBuffer.get()->updateHistory(index, (int)taken);
        //double check this - do we update FSM and History in every case? Even if branch doesnt exist?
    
    //In the case of misprediction increment flush count
    if((pred_dst == targetPc) && !taken)
        BranchTargetBuffer.get()->incrementFlushCount();
    else if((pred_dst != targetPc) && taken)
        BranchTargetBuffer.get()->incrementFlushCount();
    
	return;
}

void BP_GetStats(SIM_stats *curStats)
{
    curStats->flush_num = BranchTargetBuffer.get()->getFlushCount();
    curStats->br_num = BranchTargetBuffer.get()->getUpdateCount();
    curStats->size = BranchTargetBuffer.get()->calcSize();
	return;
}

