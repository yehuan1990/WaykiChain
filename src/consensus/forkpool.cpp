// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus/forkpool.h"
#include "persistence/block.h"
#include "main.h"
#include <unordered_map>

extern  string GetMinerAccountFromBlock(CBlock block) ;
extern CChain chainActive ;

extern bool VerifyForkPoolBlock(const CBlock *pBlock, CCacheWrapper &cwIn) ;
extern bool AcceptToMemoryPool(CTxMemPool &pool, CValidationState &state, CBaseTx *pBaseTx, bool fLimitFree, bool fRejectInsaneFee) ;


static bool BlockCompare(CBlock a, CBlock b ){

    return a.GetBlockTime() < b.GetBlockTime() ;
}

bool CForkPool::Init() {

    LOCK(cs_forkpool) ;

    if(!inited){

        if(GetTipBlock(tipBlock)){
            spCW = std::make_shared<CCacheWrapper>(pCdMan) ;
            inited = true ;
        }

    }

    return true;

}

int32_t CForkPool::TipHeight(){

    if(!inited)
        return 0 ;
    return (int32_t)(tipBlock.GetHeight());
}

bool CForkPool::IsInited() {

    return inited;

}

bool CForkPool::OnConsensusFailed(CBlock& block){

    LOCK(cs_forkpool) ;


    for(auto iter = blocks.begin(); iter != blocks.end(); iter++){

        for(const auto& ptx: iter->second.vptx){
            list<std::shared_ptr<CBaseTx> > removed;
            CValidationState stateDummy;


            if(pCdMan->pTxCache->HaveTx(ptx.get()->GetHash()) != uint256()){
                continue ;
            }

            if (!ptx->IsBlockRewardTx()) {

                if (!AcceptToMemoryPool(mempool, stateDummy, ptx.get(), false)) {
                    mempool.Remove(ptx.get(), removed, true);
                }
            } else {
                EraseTransaction(ptx->GetHash());
            }
        }

    }

    blocks.clear() ;
    unCheckedTxHashes.clear() ;


    GetTipBlock(tipBlock);
    spCW = std::make_shared<CCacheWrapper>(pCdMan) ;

    return true ;
}

bool CForkPool::AddBlock(CBlock &block) {

    LOCK(cs_forkpool) ;

    Init() ;

  /*  if(block.GetPrevBlockHash() == tipBlock.GetHash()){

        auto spCW1 = std::make_shared<CCacheWrapper>(*spCW);
        if(VerifyForkPoolBlock(&block,*spCW1)){
            tipBlock = block ;
            spCW1->Flush() ;
            InsertBlock(block) ;
        }

    }else{*/

        vector<CBlock> tempBlocks ;
        tempBlocks.push_back(block) ;
        CBlock tempBlock = block;

        while(blocks.count(tempBlock.GetPrevBlockHash())){
            tempBlock = blocks[tempBlock.GetPrevBlockHash()] ;
            tempBlocks.push_back(tempBlock) ;
        }

        if(tempBlock.GetPrevBlockHash() == chainActive.Tip() ->GetBlockHash()){

            auto activeTipCache = std::make_shared<CCacheWrapper>(pCdMan);

            reverse(tempBlocks.begin(), tempBlocks.end()) ;
            for(auto b: tempBlocks){
                if(!VerifyForkPoolBlock(&b, *activeTipCache)){
                    return ERRORMSG("VerifyForkPoolBlock ERRORERROR block.height=%d", block.GetHeight()) ;
                }
            }


            InsertBlock(block) ;
            if(block.GetHeight()> tipBlock.GetHeight() || block.GetTime() < tipBlock.GetTime()){

                tipBlock = block ;
                *spCW =  *activeTipCache;
            }

        }else{
            LogPrint("INFO", "CForkPool::AddBlock() ERROR: find orphanBlock in forkPool, blockHash=%s,blockHeight =%d",block.GetHash().GetHex(), block.GetHeight()) ;
            return false ;
        }
  //  }

    return true ;
}




bool CForkPool::RemoveBlock(CBlock &block) {
    LOCK(cs_forkpool) ;
    blocks.erase(block.GetHash());
    for( auto tx: block.vptx){
        if(unCheckedTxHashes.count(tx->GetHash())){
            if(unCheckedTxHashes.erase(tx->GetHash()) == 1)
                unCheckedTxHashes.erase(tx->GetHash());
            else
                unCheckedTxHashes.insert({tx->GetHash(),unCheckedTxHashes[tx->GetHash()]-1}) ;
        }

    }
    return true;
}

bool CForkPool::RemoveUnderHeight(const uint32_t height) {

    LOCK(cs_forkpool) ;
    auto iter = blocks.begin() ;
    while(iter != blocks.end()){
        CBlock blk = iter->second ;
        if(blk.GetHeight() <= height){

            CBlock block = iter->second ;
            for( auto tx: block.vptx){
                if(unCheckedTxHashes.count(tx->GetHash())){
                    if(unCheckedTxHashes.erase(tx->GetHash()) == 1)
                        unCheckedTxHashes.erase(tx->GetHash());
                    else
                        unCheckedTxHashes.insert({tx->GetHash(),unCheckedTxHashes[tx->GetHash()]-1}) ;
                }
            }

            iter = blocks.erase(iter) ;
        }else{
            iter++ ;
        }
    }

    return true ;

}

bool CForkPool::HasBlock(const uint256 hash) {
    return blocks.count(hash) > 0;

}

bool CForkPool::GetBlock(const uint256 hash, CBlock& block) {
    if(!HasBlock(hash)){
        return false ;
    }

    block = blocks[hash] ;
    return true ;

}

bool CForkPool::DeterminePreBlock(const int origin, CBlock& block){

    block = tipBlock ;
    return true;

    /*
    LogPrint("INFO", "FORKPOOL SIZE ==%d\n", blocks.size()) ;

    CBlock currentTipBlock ;
    GetTipBlock(currentTipBlock);
   *//*  if(origin == 3 || origin ==5){
         LogPrint("INFO","CHAIN TIP BLOCKINFO: HASH=%s,PREHASH=%s，HEIGHT=%d, MINER=%s\n", currentTipBlock.GetHash().GetHex(),currentTipBlock.GetPrevBlockHash().GetHex(),currentTipBlock.GetHeight(),GetMinerAccountFromBlock( currentTipBlock)) ;
         for(auto iter = blocks.begin(); iter != blocks.end(); iter++){

             auto block = iter->second ;
             LogPrint("INFO","BLOCKINFO: HASH=%s,PREHASH=%s，HEIGHT=%d, MINER=%s\n", block.GetHash().GetHex(),block.GetPrevBlockHash().GetHex(),block.GetHeight(),GetMinerAccountFromBlock( block)) ;
         }
     }*//*

    vector<CBlock> vFixTop ;

    vFixTop.push_back(currentTipBlock) ;
    vector<CBlock> vLongestTop = GetLongestTop(vFixTop) ;
*//*
    vector<CBlock> maxHeightBlocks = FindAllMaxHeightBlocks() ;
    if(maxHeightBlocks.empty()){
        maxHeightBlocks = vFixTop ;
    }

    std::sort(maxHeightBlocks.begin(), maxHeightBlocks.end(), BlockCompare) ;

    for(auto block: maxHeightBlocks){

        if(BlockInVector(block, vLongestTop))
            return block ;
    }
*//*


    std:: sort(vLongestTop.begin(), vLongestTop.end(), BlockCompare) ;
    auto result =  vLongestTop[0];

    LogPrint("INFO", "determinePreResult info,origin =%d ,HEIGHT=%d, hash =%s, preHash = %s \n", origin, result.GetHeight(),result.GetHash().GetHex(), result.GetPrevBlockHash().GetHex() )

    return result ;*/
}

vector<CBlock> CForkPool::GetLongestTop(const vector<CBlock> longtestTops ){

    vector<CBlock> newLongestTops  ;

    unsigned int sameCount = 0 ;
    for( auto block: longtestTops){
        vector<CBlock> tempBlocks;
        for(auto iter = blocks.begin(); iter!= blocks.end(); iter++){
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

    if(sameCount == longtestTops.size() || newLongestTops[0].GetHeight()- chainActive.Tip()->height > 500)
        return newLongestTops;
    else
        return GetLongestTop(newLongestTops);

}