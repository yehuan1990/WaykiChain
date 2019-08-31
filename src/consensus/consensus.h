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


static CBlock GetTipBlock(){

    //if(currentIrreversibleTop.GetTime() == 0 ){
        auto idx = chainActive.Tip() ;
        ReadBlockFromDisk(idx, currentIrreversibleTop) ;
    //}


    return currentIrreversibleTop ;
}



static string GetMinerAccountFromBlock(CBlock block){

    auto spCW = std::make_shared<CCacheWrapper>(pCdMan);
    CAccount minerAccount ;
    if (spCW->accountCache.GetAccount(block.vptx[0]->txUid, minerAccount)){
        return minerAccount.keyid.ToAddress();
    } else{
        return "notFindMinerAddress" ;
    }
}


static bool BlockCompare(CBlock a, CBlock b ){

    return a.GetBlockTime() < b.GetBlockTime() ;

}

static bool BlockInVector(const CBlock block, const vector<CBlock> blocks){

    for(auto b: blocks){
        if(b.GetHash() == block.GetHash())
            return true ;
    }

    return false ;
}


//find all top block in forkpool
static bool GetAllReversibleTop(vector<CBlock>& tops){

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


static bool GetAllOrphanTop(const vector<CBlock> allTops, vector<CBlock> &orphanTops ){

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


static vector<CBlock> GetLongestTop(const vector<CBlock> longtestTops ){

    vector<CBlock> newLongestTops  ;

    unsigned int sameCount = 0 ;
    for( auto block: longtestTops){
        vector<CBlock> tempBlocks;
        for(auto iter = forkPool.blocks.begin(); iter!=forkPool.blocks.end(); iter++){
            if(iter->second.GetPrevBlockHash() == block.GetHash()){
                tempBlocks.push_back(iter->second);
            }
        }

        if(tempBlocks.size() >0){
            for(auto blk: tempBlocks)
                newLongestTops.push_back(blk) ;

        }else{
            newLongestTops.push_back(block) ;
            sameCount++ ;
        }

    }

    if(sameCount == longtestTops.size()/* || newLongestTops[0].GetHeight()- chainActive.Tip()->height > 500*/)
        return newLongestTops;
    else
        return GetLongestTop(newLongestTops);

}

static vector<CBlock> FindAllMaxHeightBlocks(){


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


static CBlock DeterminePreBlock(const int origin){

    LogPrint("INFO", "FORKPOOL SIZE ==%d", forkPool.blocks.size())


    if(origin == 3 || origin ==5){
        auto block= GetTipBlock();
        LogPrint("INFO","CHAIN TIP BLOCKINFO: HASH=%s,PREHASH=%s，HEIGHT=%d, MINNER=%s\n", block.GetHash().GetHex(),block.GetPrevBlockHash().GetHex(),block.GetHeight(),GetMinerAccountFromBlock(block)) ;
        for(auto iter = forkPool.blocks.begin(); iter != forkPool.blocks.end(); iter++){


            block = iter->second ;

            LogPrint("INFO","BLOCKINFO: HASH=%s,PREHASH=%s，HEIGHT=%d, MINNER=%s\n", block.GetHash().GetHex(),block.GetPrevBlockHash().GetHex(),block.GetHeight(),GetMinerAccountFromBlock(block)) ;


        }
    }

    vector<CBlock> vFixTop ;
    CBlock block ;

    vFixTop.push_back(GetTipBlock()) ;
    vector<CBlock> vLongestTop = GetLongestTop(vFixTop) ;


/*
    vector<CBlock> maxHeightBlocks = FindAllMaxHeightBlocks() ;
    if(maxHeightBlocks.empty()){
        maxHeightBlocks = vFixTop ;
    }

    std::sort(maxHeightBlocks.begin(), maxHeightBlocks.end(), BlockCompare) ;

    for(auto block: maxHeightBlocks){

        if(BlockInVector(block, vLongestTop))
            return block ;
    }
*/


    std:: sort(vLongestTop.begin(), vLongestTop.end(), BlockCompare) ;
    auto result =  vLongestTop[0];

    LogPrint("INFO", "detearminePreResult info,origin =%d ,HEIGHT=%d, hash =%s, preHash = %s \n", origin, result.GetHeight(),result.GetHash().GetHex(), result.GetPrevBlockHash().GetHex() )

    return result ;
}

static vector<CBlock> DetermineIrreversibleList( CBlock preBlock){


    vector<CBlock> newIrreversibleBlocks  ;
    vector<CBlock> tempBlocks  ;
    tempBlocks.push_back(preBlock) ;

    forkPool.blocks.erase(chainActive.Tip()->GetBlockHash()) ;
    while(forkPool.blocks.count(tempBlocks.back().GetPrevBlockHash())>0){
        tempBlocks.push_back(forkPool.blocks[tempBlocks.back().GetPrevBlockHash()]) ;
    }

    unordered_map<string, int> minerMap ;
    for(const auto &block: tempBlocks){
        if(minerMap.size() >=8){
            newIrreversibleBlocks.insert(newIrreversibleBlocks.begin(),block) ;
        } else{
            minerMap.insert({GetMinerAccountFromBlock(block), 1}) ;
        }
    }


    return newIrreversibleBlocks ;
}

static vector<CBlock> DetermineIrreversibleList(){

    return DetermineIrreversibleList( DeterminePreBlock(1)) ;
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