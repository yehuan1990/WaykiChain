// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus/consensus.h"
#include "main.h"
#include "persistence/cachewrapper.h"

 string GetMinerAccountFromBlock(CBlock block){

/*    auto spCW = std::make_shared<CCacheWrapper>(pCdMan);
    CAccount minerAccount ;
    if (spCW->accountCache.GetAccount(, minerAccount)){
        return minerAccount.keyid.ToAddress();
    } else{
        return "notFindMinerAddress" ;
    }*/

    return block.vptx[0]->txUid.ToString() ;
}



bool DeterminePreBlock(const int origin, CBlock& block){

    return  forkPool.DeterminePreBlock(origin, block) ;
}

 bool DetermineIrreversibleList( CBlock preBlock, vector<CBlock>& newIrreversibleBlocks ){

    vector<CBlock> tempBlocks  ;
    tempBlocks.push_back(preBlock) ;

    forkPool.blocks.erase(chainActive.Tip()->GetBlockHash()) ;
    while(forkPool.blocks.count(tempBlocks.back().GetPrevBlockHash())>0){
        tempBlocks.push_back(forkPool.blocks[tempBlocks.back().GetPrevBlockHash()]) ;
    }

    unordered_map<string, int> minerMap ;
    int confirmMiners = 4 ;
    if(preBlock.GetHeight()<(int32_t)SysCfg().GetStableCoinGenesisHeight()-100  && SysCfg().NetworkID() != REGTEST_NET){
        confirmMiners = 1 ;
    } else{
        confirmMiners = 8 ;
    }


    for(const auto &block: tempBlocks){
        minerMap.erase(GetMinerAccountFromBlock(block)) ;
        if(minerMap.size() >=confirmMiners ){
            newIrreversibleBlocks.insert(newIrreversibleBlocks.begin(),block) ;
        }
        minerMap.insert({GetMinerAccountFromBlock(block), 1}) ;
    }


     for(const auto &block: tempBlocks){
         LogPrint("INFO","forkPool find irreblock from: HASH=%s,PREHASH=%s，HEIGHT=%d, MINER=%s\n", block.GetHash().GetHex(),block.GetPrevBlockHash().GetHex(),block.GetHeight(),GetMinerAccountFromBlock( block)) ;
     }

    if(newIrreversibleBlocks.size() >0){

        for(const auto &block: newIrreversibleBlocks){
            LogPrint("INFO","forkPool find irreblock ,that's: HASH=%s,PREHASH=%s，HEIGHT=%d, MINER=%s\n", block.GetHash().GetHex(),block.GetPrevBlockHash().GetHex(),block.GetHeight(),GetMinerAccountFromBlock( block)) ;
        }



    }

    return true ;

}



 CBlockIndex* PreBlockIndex(int origin){

    CBlock preb ;
    DeterminePreBlock(origin,preb) ;
    CBlockIndex* idx =  new CBlockIndex(preb) ;
    auto hash = preb.GetHash() ;
    const uint256* pHash = &hash ;

    idx->pBlockHash = pHash ;
    idx->height = preb.GetHeight() ;

    LogPrint("INFO", "findPreBlock:  height=%d", preb.GetHeight()) ;
    return idx ;
}

 shared_ptr<CBlockIndex> PreBlockIndex(int origin, CBlock& preb){


    DeterminePreBlock(origin, preb) ;
    shared_ptr<CBlockIndex> blockIndex =  std::make_shared<CBlockIndex>(preb) ;
    auto hash = preb.GetHash() ;
    const uint256* pHash = &hash ;

    blockIndex->pBlockHash = pHash ;
    blockIndex->height = preb.GetHeight() ;

    LogPrint("INFO", "findPreBlock:  height=%d", preb.GetHeight()) ;

    return blockIndex ;
}

 bool  FindPreBlock(CBlock& block, const uint256 preHash){

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

 bool FindPreBlock(const uint256 preHash){

    CBlock b;
    return FindPreBlock(b, preHash);
}

