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

class CCacheDBManager ;
class CValidationState ;
class CCacheWrapper ;

static CCriticalSection cs_forkpool;
extern bool GetTipBlock(CBlock& block) ;
extern CCacheDBManager *pCdMan ;

class CForkPool {

private:

    bool inited = false  ;
public:

    unordered_map<uint256,CBlock,CUint256Hasher> blocks ;

    unordered_map<uint256,int, CUint256Hasher> unCheckedTxHashes ;
    unordered_map<uint256, std::shared_ptr<CBaseTx>, CUint256Hasher> unCheckedTx ;

    CBlock tipBlock  ;
    shared_ptr<CCacheWrapper> spCW = nullptr ;

    CBlock newestBlock ;
    shared_ptr<CCacheWrapper> newestspCW = nullptr ;


private:

    bool InsertBlock(CBlock& block){
        blocks.insert({block.GetHash(), block});
        for( auto tx: block.vptx){
            if(!unCheckedTxHashes.count(tx->GetHash())){
                unCheckedTxHashes.insert({tx->GetHash(),1});
                unCheckedTx.insert({tx->GetHash(), tx}) ;
            }

            else
                unCheckedTxHashes.insert({tx->GetHash(),unCheckedTxHashes[tx->GetHash()]+1}) ;
        }
        return true ;
    }

public:

    bool IsInited() ;
    bool Init();
    bool AddBlock(CBlock& block) ;

    int32_t TipHeight() ;
    bool RemoveBlock(CBlock& block) ;

    bool RemoveUnderHeight(const uint32_t height) ;

    bool HasBlock(const uint256 hash) ;

    bool GetBlock(const uint256 hash, CBlock& block) ;

    bool DeterminePreBlock(const int origin, CBlock& block) ;

    bool OnConsensusFailed(CBlock& block) ;
    vector<CBlock> GetLongestTop(const vector<CBlock> longtestTops );

    bool GetTops(vector<CBlock>& tops) ;

    bool UpdateAfterConsensus(bool needUpdate) ;

    CBlockLocator GetLocator() const ;

    bool ReScanMempool(CBlock& block);
};

#endif //WAYKICHAIN_FORKPOOL_H
