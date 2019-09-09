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

 CBlock DeterminePreBlock(const int origin) ;
 bool DetermineIrreversibleList( CBlock preBlock, vector<CBlock>& newIrreversibleBlocks ) ;

 CBlockIndex* preBlockIndex(int origin) ;

 shared_ptr<CBlockIndex> preBlockIndex(int origin, CBlock& preb) ;

 bool  findPreBlock(CBlock& block, const uint256 preHash) ;

 bool findPreBlock(const uint256 preHash) ;


#endif