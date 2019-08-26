// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "forkpool.h"
#include "persistence/block.h"

bool CForkPool::AddBlock(CBlock &block) {

    LOCK(cs_forkpool) ;
    blocks.insert({block.GetHash(), block});
    return true ;
}

bool CForkPool::RemoveBlock(CBlock &block) {
    LOCK(cs_forkpool) ;
    blocks.erase(block.GetHash());
    return true;
}

bool CForkPool::RemoveUnderHeight(const uint32_t height) {

    LOCK(cs_forkpool) ;

    for(auto iter = blocks.begin() ; iter != blocks.end(); iter++){
        CBlock blk = iter->second ;
        if(blk.GetHeight() <= height){
            iter = blocks.erase(iter) ;
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

