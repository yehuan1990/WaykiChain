// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef CONSENSUS_FORKPOOL_H
#define CONSENSUS_FORKPOOL_H

#include <vector>
#include <unordered_map>
#include "commons/uint256.h"
#include <string>
#include "sync.h"
#include "persistence/block.h"

using namespace std ;

static CCriticalSection cs_forkpool;

class CForkPool {

private:

public:

    unordered_map<uint256,CBlock,CUint256Hasher> blocks ;

public:



    bool AddBlock(CBlock& block) ;

    bool RemoveBlock(CBlock& block) ;

    bool RemoveUnderHeight(const uint32_t height) ;

    bool HasBlock(const uint256 hash) ;

    bool GetBlock(const uint256 hash, CBlock& block) ;

};

#endif //WAYKICHAIN_FORKPOOL_H
