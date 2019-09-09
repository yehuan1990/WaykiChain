// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "luavmrunenv.h"
#include "commons/SafeInt3.hpp"
#include "tx/tx.h"
#include "commons/util.h"
#include "vm/luavm/lua/lua.hpp"
#include "vm/luavm/lua/lburner.h"

#define MAX_OUTPUT_COUNT 100

CLuaVMRunEnv::CLuaVMRunEnv() {
    rawAppUserAccount.clear();
    newAppUserAccount.clear();
    receipts.clear();
    vmOperateOutput.clear();
    mapAppFundOperate.clear();

    runtimeHeight   = 0;
    pAccountCache   = nullptr;
    pContractCache  = nullptr;
    isCheckAccount  = false;
}

vector<shared_ptr<CAppUserAccount>>& CLuaVMRunEnv::GetNewAppUserAccount() { return newAppUserAccount; }
vector<shared_ptr<CAppUserAccount>>& CLuaVMRunEnv::GetRawAppUserAccount() { return rawAppUserAccount; }

bool CLuaVMRunEnv::Initialize(std::shared_ptr<CBaseTx>& tx, CCacheWrapper &cwIn, int32_t height) {
    pBaseTx         = tx;
    runtimeHeight   = height;
    pCw = &cwIn;
    pAccountCache   = &cwIn.accountCache;
    pContractCache  = &cwIn.contractCache;

    if (tx.get()->nTxType != LCONTRACT_INVOKE_TX) {
        LogPrint("ERROR", "unsupported tx type\n");
        return false;
    }

    CUniversalContract contract;
    CLuaContractInvokeTx* contractTx = static_cast<CLuaContractInvokeTx*>(tx.get());
    if (!pContractCache->GetContract(contractTx->app_uid.get<CRegID>(), contract)) {
        LogPrint("ERROR", "contract not found: %s\n", contractTx->app_uid.get<CRegID>().ToString());
        return false;
    }

    if (contractTx->arguments.size() >= MAX_CONTRACT_ARGUMENT_SIZE) {
        LogPrint("ERROR", "CVmScriptRun::Initialize() arguments context size too large\n");
        return false;
    }

    try {
        pLua = std::make_shared<CLuaVM>(contract.code, contractTx->arguments);
    } catch (exception& e) {
        LogPrint("ERROR", "CVmScriptRun::Initialize() CLuaVM init error\n");
        return false;
    }

    LogPrint("vm", "CVmScriptRun::Initialize() CLuaVM init success\n");

    return true;
}

CLuaVMRunEnv::~CLuaVMRunEnv() {}

tuple<bool, uint64_t, string> CLuaVMRunEnv::ExecuteContract(shared_ptr<CBaseTx>& pBaseTx, int32_t height, CCacheWrapper& cw,
                                                         uint64_t nBurnFactor, uint64_t& uRunStep) {
    if (nBurnFactor == 0)
        return std::make_tuple(false, 0, string("VmScript nBurnFactor == 0"));

    CLuaContractInvokeTx* tx = static_cast<CLuaContractInvokeTx*>(pBaseTx.get());
    if (tx->llFees < CBaseTx::nMinTxFee)
        return std::make_tuple(false, 0, string("CLuaVMRunEnv: Contract pBaseTx fee too small"));

    uint64_t fuelLimit = ((tx->llFees - CBaseTx::nMinTxFee) / nBurnFactor) * 100;
    if (fuelLimit > MAX_BLOCK_RUN_STEP) {
        fuelLimit = MAX_BLOCK_RUN_STEP;
    }

    LogPrint("vm", "tx hash:%s fees=%lld fuelrate=%lld fuelLimit:%d\n", pBaseTx->GetHash().GetHex(), tx->llFees,
             nBurnFactor, fuelLimit);

    if (fuelLimit == 0) {
        return std::make_tuple(false, 0, string("CLuaVMRunEnv::ExecuteContract, fees too low"));
    }

    if (!Initialize(pBaseTx, cw, height)) {
        return std::make_tuple(false, 0, string("VmScript inital Failed"));
    }

    tuple<uint64_t, string> ret = pLua.get()->Run(fuelLimit, this);
    LogPrint("vm", "CVmScriptRun::ExecuteContract() LUA\n");

    int64_t step = std::get<0>(ret);
    if (0 == step) {
        return std::make_tuple(false, 0, string("VmScript run Failed"));
    } else if (-1 == step) {
        return std::make_tuple(false, 0, std::get<1>(ret));
    } else {
        uRunStep = step;
    }

    LogPrint("vm", "tx:%s,step:%ld\n", tx->ToString(*pAccountCache), uRunStep);

    if (!CheckOperate(vmOperateOutput)) {
        return std::make_tuple(false, 0, string("VmScript CheckOperate Failed"));
    }

    if (!OperateAccount(vmOperateOutput)) {
        return std::make_tuple(false, 0, string("VmScript OperateAccount Failed"));
    }

    LogPrint("vm", "isCheckAccount: %d\n", isCheckAccount);
    if (isCheckAccount && !CheckAppAcctOperate(tx)) {
            return std::make_tuple(false, 0, string("VmScript CheckAppAcct Failed"));
    }

    if (!OperateAppAccount(mapAppFundOperate)) {
        return std::make_tuple(false, 0, string("OperateAppAccount Account Failed"));
    }

    uint64_t spend = 0;
    if (!SafeMultiply(uRunStep, nBurnFactor, spend)) {
        return std::make_tuple(false, 0, string("mul error"));
    }

    return std::make_tuple(true, spend, string("VmScript Success"));
}

UnsignedCharArray CLuaVMRunEnv::GetAccountID(const CVmOperate& value) {
    UnsignedCharArray accountId;
    if (value.accountType == AccountType::REGID) {
        accountId.assign(value.accountId, value.accountId + 6);
    } else if (value.accountType == AccountType::BASE58ADDR) {
        string addr(value.accountId, value.accountId + sizeof(value.accountId));
        CKeyID keyid = CKeyID(addr);
        CRegID regid;
        if (pAccountCache->GetRegId(CUserID(keyid), regid)) {
            accountId.assign(regid.GetRegIdRaw().begin(), regid.GetRegIdRaw().end());
        } else {
            accountId.assign(value.accountId, value.accountId + 34);
        }
    }

    return accountId;
}

shared_ptr<CAppUserAccount> CLuaVMRunEnv::GetAppAccount(shared_ptr<CAppUserAccount>& appAccount) {
    if (rawAppUserAccount.size() == 0)
        return nullptr;

    vector<shared_ptr<CAppUserAccount>>::iterator Iter;
    for (Iter = rawAppUserAccount.begin(); Iter != rawAppUserAccount.end(); Iter++) {
        shared_ptr<CAppUserAccount> temp = *Iter;
        if (appAccount.get()->GetAccUserId() == temp.get()->GetAccUserId())
            return temp;
    }
    return nullptr;
}

bool CLuaVMRunEnv::CheckOperate(const vector<CVmOperate>& operates) {
    // judge contract rule
    uint64_t addMoney = 0, minusMoney = 0;
    uint64_t operValue = 0;
    if (operates.size() > MAX_OUTPUT_COUNT)
        return false;

    for (auto& it : operates) {
        if (it.accountType != REGID && it.accountType != AccountType::BASE58ADDR)
            return false;

        if (it.opType == BalanceOpType::ADD_FREE) {
            memcpy(&operValue, it.money, sizeof(it.money));
            /*
            uint64_t temp = addMoney;
            temp += operValue;
            if(temp < operValue || temp<addMoney) {
                return false;
            }
            */
            uint64_t temp = 0;
            if (!SafeAdd(addMoney, operValue, temp)) {
                return false;
            }
            addMoney = temp;
        } else if (it.opType == BalanceOpType::SUB_FREE) {
            // vector<unsigned char > accountId(it.accountId,it.accountId+sizeof(it.accountId));
            UnsignedCharArray accountId = GetAccountID(it);
            if (accountId.size() != 6)
                return false;
            CRegID regId(accountId);
            CLuaContractInvokeTx* tx = static_cast<CLuaContractInvokeTx*>(pBaseTx.get());
            // allow to minus current contract's account only.
            if (!pContractCache->HaveContract(regId) || regId != tx->app_uid.get<CRegID>()) {
                return false;
            }

            memcpy(&operValue, it.money, sizeof(it.money));
            /*
            uint64_t temp = minusMoney;
            temp += operValue;
            if(temp < operValue || temp < minusMoney) {
                return false;
            }
            */
            uint64_t temp = 0;
            if (!SafeAdd(minusMoney, operValue, temp))
                return false;

            minusMoney = temp;
        } else {
            // Assert(0);
            return false;  // 输入数据错误
        }
    }

    if (addMoney != minusMoney)
        return false;

    return true;
}

bool CLuaVMRunEnv::CheckAppAcctOperate(CLuaContractInvokeTx* tx) {
    int64_t addValue(0), minusValue(0), sumValue(0);
    for (auto vOpItem : mapAppFundOperate) {
        for (auto appFund : vOpItem.second) {
            if (ADD_FREE_OP == appFund.opType || ADD_TAG_OP == appFund.opType) {
                /*
                int64_t temp = appFund.mMoney;
                temp += addValue;
                if(temp < addValue || temp<appFund.mMoney) {
                    return false;
                }
                */
                int64_t temp = 0;
                if (!SafeAdd(appFund.mMoney, addValue, temp))
                    return false;

                addValue = temp;
            } else if (SUB_FREE_OP == appFund.opType || SUB_TAG_OP == appFund.opType) {
                /*
                int64_t temp = appFund.mMoney;
                temp += minusValue;
                if(temp < minusValue || temp<appFund.mMoney) {
                    return false;
                }
                */
                int64_t temp = 0;
                if (!SafeAdd(appFund.mMoney, minusValue, temp))
                    return false;

                minusValue = temp;
            }
        }
    }
    /*
    sumValue = addValue - minusValue;
    if(sumValue > addValue) {
        return false;
    }
    */
    if (!SafeSubtract(addValue, minusValue, sumValue))
        return false;

    uint64_t sysContractAcct(0);
    for (auto item : vmOperateOutput) {
        UnsignedCharArray vAccountId = GetAccountID(item);
        if (vAccountId == tx->app_uid.get<CRegID>().GetRegIdRaw() &&
            item.opType == BalanceOpType::SUB_FREE) {
            uint64_t value;
            memcpy(&value, item.money, sizeof(item.money));
            int64_t temp = value;
            if (temp < 0)
                return false;

            /*
            temp += sysContractAcct;
            if(temp < sysContractAcct || temp < (int64_t)value)
                return false;
            */
            uint64_t tempValue = temp;
            uint64_t tempOut   = 0;
            if (!SafeAdd(tempValue, sysContractAcct, tempOut))
                return false;

            sysContractAcct = tempOut;
        }
    }
    /*
        int64_t sysAcctSum = tx->coin_amount - sysContractAcct;
        if(sysAcctSum > (int64_t)tx->coin_amount) {
            return false;
        }
    */
    int64_t sysAcctSum = 0;
    if (!SafeSubtract((int64_t)tx->coin_amount, (int64_t)sysContractAcct, sysAcctSum))
        return false;

    if (sumValue != sysAcctSum) {
        LogPrint("vm",
                 "CheckAppAcctOperate: addValue=%lld, minusValue=%lld, txValue=%lld, "
                 "sysContractAcct=%lld, sumValue=%lld, sysAcctSum=%lld\n",
                 addValue, minusValue, tx->coin_amount, sysContractAcct, sumValue, sysAcctSum);

        return false;
    }

    return true;
}

bool CLuaVMRunEnv::OperateAccount(const vector<CVmOperate>& operates) {

    for (auto& operate : operates) {
        uint64_t value;
        memcpy(&value, operate.money, sizeof(operate.money));

        auto pAccount = std::make_shared<CAccount>();
        UnsignedCharArray accountId = GetAccountID(operate);
        CUserID uid;

        if (accountId.size() == 6) {
            CRegID regid;
            regid.SetRegID(accountId);
            if (!pAccountCache->GetAccount(CUserID(regid), *pAccount)) {
                LogPrint("vm", "CLuaVMRunEnv::OperateAccount(), account not exist! regid=%s", regid.ToString());
                return false;
            }
            uid = regid;
        } else {
            CKeyID keyid;
            keyid = CKeyID(string(accountId.begin(), accountId.end()));
            if (!pAccountCache->GetAccount(keyid, *pAccount)) {
                pAccount = make_shared<CAccount>(keyid);
                // TODO: new account fuel
            }
            uid = keyid;
        }

        LogPrint("vm", "uid=%s\nbefore account: %s\n", uid.ToString(),
                 pAccount->ToString());

        if (!pAccount->OperateBalance(SYMB::WICC, operate.opType, value)) {
            LogPrint("vm", "CLuaVMRunEnv::OperateAccount(), operate account failed! uid=%s, operate=%s",
                uid.ToString(), GetBalanceOpTypeName(operate.opType));
            return false;
        }

        if (operate.opType == BalanceOpType::ADD_FREE) {
            receipts.emplace_back(nullId, uid, SYMB::WICC, value, "operate ADD_FREE bcoins of account in contract");
        } else if (operate.opType == BalanceOpType::SUB_FREE) {
            receipts.emplace_back(uid, nullId, SYMB::WICC, value, "operate SUB_FREE bcoins of account in contract");
        }

        if (!pCw->accountCache.SetAccount(pAccount->keyid, *pAccount)) {
            LogPrint("vm",
                     "CLuaVMRunEnv::OperateAccount(), save account failed, uid=%s", uid.ToString());
            return false;
        }

        LogPrint("vm", "after account:%s\n", pAccount->ToString());
    }

    return true;
}

bool CLuaVMRunEnv::TransferAccountAsset(lua_State *L, const vector<AssetTransfer> &transfers) {

    // TODO: count
    bool ret = true;
    for (auto& transfer : transfers) {
        bool isNewAccount = false;
        CUserID fromUid;
        if (transfer.isContractAccount) {
            fromUid = GetContractRegID();
        } else {
            fromUid = GetTxAccount();
        }

        // TODO: need to cache the from account?
        auto pFromAccount = make_shared<CAccount>();
        if (!pAccountCache->GetAccount(fromUid, *pFromAccount)) {
            LogPrint("vm", "CLuaVMRunEnv::TransferAccountAsset(), get from_account failed! from_uid=%s, "
                     "isContractAccount=%d", transfer.toUid.ToString(), (int)transfer.isContractAccount);
            ret = false; break;
        }

        auto pToAccount = make_shared<CAccount>();
        if (!pAccountCache->GetAccount(transfer.toUid, *pToAccount)) {
            if (!transfer.toUid.is<CKeyID>()) {
                LogPrint("vm", "CLuaVMRunEnv::TransferAccountAsset(), get to_account failed! to_uid=%s",
                    transfer.toUid.ToString());
                ret = false; break;
            }
            // create new user
            pToAccount = make_shared<CAccount>(transfer.toUid.get<CKeyID>());
            isNewAccount = true;
        }

        if (!pFromAccount->OperateBalance(transfer.tokenType, SUB_FREE, transfer.tokenAmount)) {
            LogPrint("vm", "CLuaVMRunEnv::TransferAccountAsset(), operate SUB_FREE in from_account failed! "
                "from_uid=%s, isContractAccount=%d, symbol=%s, amount=%llu",
                fromUid.ToString(), (int)transfer.isContractAccount, transfer.tokenType,
                transfer.tokenAmount);
            ret = false; break;
        }

        if (!pToAccount->OperateBalance(transfer.tokenType, ADD_FREE, transfer.tokenAmount)) {
            LogPrint("vm", "CLuaVMRunEnv::TransferAccountAsset(), operate ADD_FREE in to_account failed! "
                     "to_uid=%s, symbol=%s, amount=%llu",
                     transfer.toUid.ToString(), transfer.tokenType, transfer.tokenAmount);
            ret = false; break;
        }

        if (!pCw->accountCache.SetAccount(fromUid, *pFromAccount)) {
            LogPrint("vm",
                     "CLuaVMRunEnv::TransferAccountAsset(), save from_account failed, from_uid=%s, "
                     "isContractAccount=%d",
                     fromUid.ToString(), (int)transfer.isContractAccount);
            ret = false; break;
        }

        if (!pCw->accountCache.SetAccount(transfer.toUid, *pToAccount)) {
            LogPrint("vm",
                     "CLuaVMRunEnv::TransferAccountAsset(), save to_account failed, to_uid=%s",
                     transfer.toUid.ToString(), (int)transfer.isContractAccount);
            ret = false; break;
        }

        receipts.emplace_back(fromUid, transfer.toUid, transfer.tokenType, transfer.tokenAmount,
            "transfer account asset in contract");

        if (isNewAccount) {
            LUA_BurnAccount(L, FUEL_ACCOUNT_NEW, BURN_VER_R2);

        } else {
            LUA_BurnAccount(L, FUEL_ACCOUNT_OPERATE, BURN_VER_R2);
        }
    }
    if (!ret) {
        LUA_BurnAccount(L, FUEL_ACCOUNT_GET_VALUE, BURN_VER_R2);
    }
    return ret;
}

const CRegID& CLuaVMRunEnv::GetContractRegID() {
    CLuaContractInvokeTx* tx = static_cast<CLuaContractInvokeTx*>(pBaseTx.get());
    return tx->app_uid.get<CRegID>();
}

const CRegID& CLuaVMRunEnv::GetTxAccount() {
    CLuaContractInvokeTx* tx = static_cast<CLuaContractInvokeTx*>(pBaseTx.get());
    return tx->txUid.get<CRegID>();
}

uint64_t CLuaVMRunEnv::GetValue() const {
    CLuaContractInvokeTx* tx = static_cast<CLuaContractInvokeTx*>(pBaseTx.get());
    return tx->coin_amount;
}

const string& CLuaVMRunEnv::GetTxContract() {
    CLuaContractInvokeTx* tx = static_cast<CLuaContractInvokeTx*>(pBaseTx.get());
    return tx->arguments;
}

int32_t CLuaVMRunEnv::GetConfirmHeight() { return runtimeHeight; }

int32_t CLuaVMRunEnv::GetBurnVersion() {
    // the burn version belong to the Feature Fork Version
    return GetFeatureForkVersion(runtimeHeight);
}

uint256 CLuaVMRunEnv::GetCurTxHash() { return pBaseTx.get()->GetHash(); }


CCacheWrapper* CLuaVMRunEnv::GetCw() {
    return pCw;
}

CContractDBCache* CLuaVMRunEnv::GetScriptDB() { return pContractCache; }

CAccountDBCache* CLuaVMRunEnv::GetCatchView() { return pAccountCache; }

void CLuaVMRunEnv::InsertOutAPPOperte(const vector<uint8_t>& userId,
                                   const CAppFundOperate& source) {
    if (mapAppFundOperate.count(userId)) {
        mapAppFundOperate[userId].push_back(source);
    } else {
        vector<CAppFundOperate> it;
        it.push_back(source);
        mapAppFundOperate[userId] = it;
    }
}

bool CLuaVMRunEnv::InsertOutputData(const vector<CVmOperate>& source) {
    vmOperateOutput.insert(vmOperateOutput.end(), source.begin(), source.end());
    if (vmOperateOutput.size() < MAX_OUTPUT_COUNT)
        return true;

    return false;
}

/**
 * 从脚本数据库中，取指定账户的应用账户信息, 同时解冻冻结金额到自由金额
 * @param vAppUserId   账户地址或regId
 * @param pAppUserAccount
 * @return
 */
bool CLuaVMRunEnv::GetAppUserAccount(const vector<uint8_t>& vAppUserId, shared_ptr<CAppUserAccount>& pAppUserAccount) {
    assert(pContractCache);
    shared_ptr<CAppUserAccount> tem = std::make_shared<CAppUserAccount>();
    string appUserId(vAppUserId.begin(), vAppUserId.end());
    if (!pContractCache->GetContractAccount(GetContractRegID(), appUserId, *tem.get())) {
        tem             = std::make_shared<CAppUserAccount>(appUserId);
        pAppUserAccount = tem;

        return true;
    }

    if (!tem.get()->AutoMergeFreezeToFree(runtimeHeight)) {
        return false;
    }

    pAppUserAccount = tem;

    return true;
}

bool CLuaVMRunEnv::OperateAppAccount(const map<vector<uint8_t>, vector<CAppFundOperate>> opMap) {
    newAppUserAccount.clear();

    if (!mapAppFundOperate.empty()) {
        for (auto const tem : opMap) {
            shared_ptr<CAppUserAccount> pAppUserAccount;
            if (!GetAppUserAccount(tem.first, pAppUserAccount)) {
                LogPrint("vm", "GetAppUserAccount(tem.first, pAppUserAccount, true) failed \n appuserid :%s\n",
                         HexStr(tem.first));
                return false;
            }

            if (!pAppUserAccount.get()->AutoMergeFreezeToFree(runtimeHeight)) {
                LogPrint("vm", "AutoMergeFreezeToFree failed\nappuser :%s\n", pAppUserAccount.get()->ToString());
                return false;
            }

            shared_ptr<CAppUserAccount> vmAppAccount = GetAppAccount(pAppUserAccount);
            if (vmAppAccount.get() == nullptr) {
                rawAppUserAccount.push_back(pAppUserAccount);
            }

            LogPrint("vm", "before user: %s\n", pAppUserAccount.get()->ToString());

            vector<CReceipt> appOperateReceipts;
            if (!pAppUserAccount.get()->Operate(tem.second, appOperateReceipts)) {
                int32_t i = 0;
                for (auto const appFundOperate : tem.second) {
                    LogPrint("vm", "Operate failed\nOperate %d: %s\n", i++, appFundOperate.ToString());
                }
                LogPrint("vm", "GetAppUserAccount(tem.first, pAppUserAccount, true) failed\nappuserid: %s\n",
                         HexStr(tem.first));
                return false;
            }

            receipts.insert(receipts.end(), appOperateReceipts.begin(), appOperateReceipts.end());

            newAppUserAccount.push_back(pAppUserAccount);

            LogPrint("vm", "after user: %s\n", pAppUserAccount.get()->ToString());

            pContractCache->SetContractAccount(GetContractRegID(), *pAppUserAccount.get());
        }
    }

    return true;
}

void CLuaVMRunEnv::SetCheckAccount(bool bCheckAccount) { isCheckAccount = bCheckAccount; }
