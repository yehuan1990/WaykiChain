// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CONSENSUS_CONSENSUS_H
#define CONSENSUS_CONSENSUS_H


#include <consensus/forkpool.h>
#include <persistence/block.h>
#include <vector>

class CChain ;
extern CCacheDBManager *pCdMan ;

extern CChain chainActive ;
extern bool ReadBlockFromDisk(const CBlockIndex *pIndex, CBlock &block) ;
extern CForkPool forkPool ;

extern CBlock currentIrreversibleTop;
extern bool GetTipBlock(CBlock& block);

string GetMinerAccountFromBlock(CBlock block) ;

bool DeterminePreBlock(const int origin, CBlock& preBlock) ;
bool DetermineIrreversibleList( CBlock preBlock, vector<CBlock>& newIrreversibleBlocks ) ;

 CBlockIndex* PreBlockIndex(int origin) ;

 shared_ptr<CBlockIndex> PreBlockIndex(int origin, CBlock& preb) ;

 bool  FindPreBlock(CBlock& block, const uint256 preHash) ;

 bool FindPreBlock(const uint256 preHash) ;


#endif