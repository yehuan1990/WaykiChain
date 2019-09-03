// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "forkpool.h"
#include "persistence/block.h"
#include <unordered_map>

bool CForkPool::AddBlock(CBlock &block) {

    LOCK(cs_forkpool) ;
    blocks.insert({block.GetHash(), block});
    for( auto tx: block.vptx){
        if(!unCheckedTxHashes.count(tx->GetHash()))
            unCheckedTxHashes.insert({tx->GetHash(),1});
        else
            unCheckedTxHashes.insert({tx->GetHash(),unCheckedTxHashes[tx->GetHash()]+1}) ;
    }
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

