#include "bp_api.h"
#include <memory>
#include <vector>

class BTB_entry
{
private:
    uint32_t m_tag;
    uint32_t m_target;
    uint32_t m_localHistory;
    
public:
    explicit BTB_entry(uint32_t tag, uint32_t target, uint32_t localHistory): m_tag(tag), m_target(target), m_localHistory(localHistory){}
    
    uint32_t getTag() {return m_tag;}
    
    uint32_t getTarget() {return m_target;}
    
    uint32_t getLocalHistory() {return m_localHistory;}
};

class BTB
{
private:
    unsigned m_tableSize;
    unsigned m_historyRegSize; // in bits
    unsigned m_tagSize; //in bits
    unsigned m_fsmState; //0-3
    bool m_isGlobalHist;
    bool m_isGlobalTable;
    int m_isShare;
    
    std::vector<std::shared_ptr<BTB_entry>> table;
    // if there is tag and index collision do we update target? do we reset localHistory? 
    
public:
    explicit BTB(unsigned btbSize, unsigned historySize, unsigned tagSize, unsigned fsmState,
                 bool isGlobalHist, bool isGlobalTable, int isShare) : m_tableSize(btbSize), m_historyRegSize(historySize), m_tagSize(tagSize), m_fsmState(fsmState), m_isGlobalHist(isGlobalHist), m_isGlobalTable(isGlobalTable), m_isShare(isShare), table(btbSize)
    {
        
    }
};

static std::shared_ptr<BTB> branchPredictor;

int BP_init(unsigned btbSize, unsigned historySize, unsigned tagSize, unsigned fsmState,
			bool isGlobalHist, bool isGlobalTable, int isShare)
{
    branchPredictor = std::make_shared<BTB>(btbSize, historySize, tagSize, fsmState, isGlobalHist, isGlobalTable, isShare);
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

