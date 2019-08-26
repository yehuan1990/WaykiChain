// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CONSENSUS_CONSENSUS_H
#define CONSENSUS_CONSENSUS_H


#include <consensus/forkpool.h>
#include <persistence/block.h>
#include <vector>
#include <main.h>

extern CCacheDBManager *pCdMan;

CForkPool forkPool ;

CBlock currentIrreversibleTop ;

string GetMinerAccountFromBlock(CBlock block){

    auto spCW = std::make_shared<CCacheWrapper>(pCdMan);
    CAccount minerAccount ;
    if (spCW->accountCache.GetAccount(block.vptx[0]->txUid, minerAccount)){
        return minerAccount.keyid.ToAddress();
    } else{
        return "notFindMinerAddress" ;
    }
}


bool BlockCompare(CBlock a, CBlock b ){

    return a.GetBlockTime() < b.GetBlockTime() ;

}

bool BlockInVector(const CBlock block, const vector<CBlock> blocks){

    for(auto b: blocks){
        if(b.GetHash() == block.GetHash())
            return true ;
    }

    return false ;
}


//find all top block in forkpool
bool GetAllReversibleTop(vector<CBlock>& tops){

    for(auto blockPair: forkPool.blocks){
        bool  isTop = true ;

        for(auto blockPair1: forkPool.blocks){

            if(blockPair.second.GetHash() == blockPair1.second.GetPrevBlockHash()){
                isTop = false;
                break ;
            }
        }
        if(isTop){
            tops.push_back(blockPair.second);
        }
    }
    return true ;
}


bool GetAllOrphanTop(const vector<CBlock> allTops, vector<CBlock> &orphanTops ){

    for (auto top : allTops) {
        CBlock block = top;

        while(forkPool.HasBlock(block.GetPrevBlockHash())){
            forkPool.GetBlock(block.GetPrevBlockHash(), block) ;
        }

        if(top.GetHeight() - block.GetHeight() > 1)
            orphanTops.push_back(top) ;
    }

    return !orphanTops.empty();
}


vector<CBlock> GetLongestTop( const vector<CBlock> longtestTops ){

    vector<CBlock> newLongestTops  ;

    unsigned int sameCount = 0 ;
    for(const auto block: longtestTops){
        vector<CBlock> tempBlocks;
        for(auto iter = forkPool.blocks.begin(); iter!=forkPool.blocks.end(); iter++){
            if(iter->second.GetPrevBlockHash() == block.GetHash()){
                tempBlocks.push_back(iter->second);
            }
        }

        if(!tempBlocks.empty()){
            for(const auto &blk: tempBlocks)
                newLongestTops.push_back(blk) ;

        }else{
            newLongestTops.push_back(block) ;
            sameCount++ ;
        }

    }

    if(sameCount == longtestTops.size() || newLongestTops[0].GetHeight()- currentIrreversibleTop.GetHeight() > 500)
        return newLongestTops;
    else
        return GetLongestTop(newLongestTops);

}

vector<CBlock> FindAllMaxHeightBlocks(){


    uint32_t maxHeight = 0 ;

    vector<CBlock> maxHeightBlocks ;

   for(auto iter = forkPool.blocks.begin(); iter != forkPool.blocks.end(); iter++){

       if(iter->second.GetHeight()> maxHeight){
           maxHeightBlocks.clear();
           maxHeight = iter->second.GetHeight() ;
           maxHeightBlocks.push_back(iter->second);

       } else if(iter->second.GetHeight() == maxHeight){
           maxHeightBlocks.push_back(iter->second);
       }
   }

   return maxHeightBlocks ;

}


CBlock DeterminePreBlock(){

    vector<CBlock> vFixTop ;
    vFixTop.push_back(currentIrreversibleTop) ;
    vector<CBlock> vLongestTop = GetLongestTop(vFixTop) ;

    vector<CBlock> maxHeightBlocks = FindAllMaxHeightBlocks() ;
    if(maxHeightBlocks.empty()){
        maxHeightBlocks = vFixTop ;
    }

    std::sort(maxHeightBlocks.begin(), maxHeightBlocks.end(), BlockCompare) ;

    for(auto block: maxHeightBlocks){

        if(BlockInVector(block, vLongestTop))
            return block ;
    }

    std:: sort(vLongestTop.begin(), vLongestTop.end(), BlockCompare) ;
    return vLongestTop[0];

}

vector<CBlock> DetermineIrreversibleList( CBlock preBlock){


    vector<CBlock> newIrreversibleBlocks  ;
    vector<CBlock> tempBlocks  ;
    tempBlocks.push_back(preBlock) ;


    while(forkPool.blocks.count(tempBlocks.back().GetPrevBlockHash())>0){
        tempBlocks.push_back(forkPool.blocks[tempBlocks.back().GetPrevBlockHash()]) ;
    }

    unordered_map<string, int> minerMap ;
    for(const auto &block: tempBlocks){
        if(minerMap.size() >=8){
            newIrreversibleBlocks.push_back(block) ;
        } else{
            minerMap.insert({GetMinerAccountFromBlock(block), 1}) ;
        }
    }

    return newIrreversibleBlocks ;
}

vector<CBlock> DetermineIrreversibleList(){

    return DetermineIrreversibleList( DeterminePreBlock()) ;
}


CBlockIndex* preBlockIndex(){
    CBlock preb = DeterminePreBlock() ;
    return new CBlockIndex(preb) ;
}



#endif