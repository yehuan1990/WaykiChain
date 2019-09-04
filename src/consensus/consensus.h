// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CONSENSUS_CONSENSUS_H
#define CONSENSUS_CONSENSUS_H


#include <consensus/forkpool.h>
#include <persistence/block.h>
#include <vector>



extern CCacheDBManager *pCdMan ;

extern CChain chainActive ;
extern bool ReadBlockFromDisk(const CBlockIndex *pIndex, CBlock &block) ;
extern CForkPool forkPool ;

extern CBlock currentIrreversibleTop;
extern CBlock GetTipBlock();



static string GetMinerAccountFromBlock(CBlock block){

    auto spCW = std::make_shared<CCacheWrapper>(pCdMan);
    CAccount minerAccount ;
    if (spCW->accountCache.GetAccount(block.vptx[0]->txUid, minerAccount)){
        return minerAccount.keyid.ToAddress();
    } else{
        return "notFindMinerAddress" ;
    }
}



static CBlock DeterminePreBlock(const int origin){

    return forkPool.DeterminePreBlock(origin) ;
}

static bool DetermineIrreversibleList( CBlock preBlock, vector<CBlock>& newIrreversibleBlocks ){

    vector<CBlock> tempBlocks  ;
    tempBlocks.push_back(preBlock) ;

    forkPool.blocks.erase(chainActive.Tip()->GetBlockHash()) ;
    while(forkPool.blocks.count(tempBlocks.back().GetPrevBlockHash())>0){
        tempBlocks.push_back(forkPool.blocks[tempBlocks.back().GetPrevBlockHash()]) ;
    }

    unordered_map<string, int> minerMap ;
    int confirmMiners = 4 ;
    if(preBlock.GetHeight()<(int32_t)SysCfg().GetStableCoinGenesisHeight()-10000  && SysCfg().NetworkID() == MAIN_NET){
        confirmMiners = 1 ;
    } else{
        confirmMiners = 8 ;
    }

    for(const auto &block: tempBlocks){
        if(minerMap.size() >=confirmMiners){
            newIrreversibleBlocks.insert(newIrreversibleBlocks.begin(),block) ;
        } else{
            minerMap.insert({GetMinerAccountFromBlock(block), 1}) ;
        }
    }

    return true ;

}



static CBlockIndex* preBlockIndex(int origin){

    CBlock preb = DeterminePreBlock(origin) ;
    CBlockIndex* idx =  new CBlockIndex(preb) ;
    auto hash = preb.GetHash() ;
    const uint256* pHash = &hash ;

    idx->pBlockHash = pHash ;
    idx->height = preb.GetHeight() ;

    LogPrint("INFO", "findPreBlock:  height=%d", preb.GetHeight()) ;
    return idx ;
}



static shared_ptr<CBlockIndex> preBlockIndex(int origin, CBlock& preb){


    preb = DeterminePreBlock(origin) ;
    shared_ptr<CBlockIndex> blockIndex =  std::make_shared<CBlockIndex>(preb) ;
    auto hash = preb.GetHash() ;
    const uint256* pHash = &hash ;

    blockIndex->pBlockHash = pHash ;
    blockIndex->height = preb.GetHeight() ;

    LogPrint("INFO", "findPreBlock:  height=%d", preb.GetHeight()) ;

    return blockIndex ;
}

static bool  findPreBlock(CBlock& block, const uint256 preHash){

    if(preHash == chainActive.Tip()->GetBlockHash()){
        auto idx = chainActive.Tip() ;
        ReadBlockFromDisk(idx, block) ;
        return true ;
    }

    if(forkPool.HasBlock(preHash)){
        block = forkPool.blocks[preHash] ;
        return true ;
    }

    return false ;


}

static bool findPreBlock(const uint256 preHash){

    CBlock b;
    return findPreBlock(b, preHash);
}


#endif