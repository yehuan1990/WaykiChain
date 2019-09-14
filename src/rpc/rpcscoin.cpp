// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcscoin.h"

#include "commons/base58.h"
#include "config/const.h"
#include "rpc/core/rpcserver.h"
#include "rpc/core/rpccommons.h"
#include "init.h"
#include "net.h"
#include "miner/miner.h"
#include "commons/util.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "tx/cdptx.h"
#include "tx/dextx.h"
#include "tx/pricefeedtx.h"
#include "tx/assettx.h"

extern CForkPool forkPool ;


Value submitpricefeedtx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 2 || params.size() > 3) {
        throw runtime_error(
            "submitpricefeedtx {price_feeds_json} [\"symbol:fee:unit\"]\n"
            "\nsubmit a Price Feed Tx.\n"
            "\nArguments:\n"
            "1. \"address\" :                   (string, required) Price Feeder's address\n"
            "2. \"pricefeeds\":                 (string, required) A json array of pricefeeds\n"
            " [\n"
            "   {\n"
            "      \"coin\": \"WICC|WGRT\",       (string, required) The coin type\n"
            "      \"currency\": \"USD|CNY\"      (string, required) The currency type\n"
            "      \"price\":                   (number, required) The price (boosted by 10^4) \n"
            "   }\n"
            "       ,...\n"
            " ]\n"
            "3. \"symbol:fee:unit\":            (string:numeric:string, optional) fee paid to miner, default is WICC:10000:sawi\n"
            "\nResult:\n"
            "\"txid\"                           (string) The transaction id.\n"
            "\nExamples:\n" +
            HelpExampleCli("submitpricefeedtx",
                           "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\" "
                           "\"[{\\\"coin\\\": \\\"WICC\\\", \\\"currency\\\": \\\"USD\\\", \\\"price\\\": 2500}]\"") +
            "\nAs json rpc call\n" +
            HelpExampleRpc("submitpricefeedtx",
                           "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\", [{\"coin\": \"WICC\", \"currency\": \"USD\", "
                           "\"price\": 2500}]"));
    }

    EnsureWalletIsUnlocked();

    const CUserID &feedUid = RPC_PARAM::GetUserId(params[0].get_str());

    Array arrPricePoints = params[1].get_array();
    vector<CPricePoint> pricePoints;
    for (auto objPp : arrPricePoints) {
        const Value& coinValue = find_value(objPp.get_obj(), "coin");
        const Value& currencyValue = find_value(objPp.get_obj(), "currency");
        const Value& priceValue = find_value(objPp.get_obj(), "price");
        if (coinValue.type() == null_type || currencyValue.type() == null_type || priceValue.type() == null_type) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "null type not allowed!");
        }

        string coinStr = coinValue.get_str();
        if (!kCoinTypeSet.count(coinStr)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid coin symbol: %s", coinStr));
        }

        string currencyStr = currencyValue.get_str();
        if (!kCurrencyTypeSet.count(currencyStr)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid currency type: %s", currencyStr));
        }

        int64_t price = priceValue.get_int64();
        if (price <= 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid price: %lld", price));
        }

        CoinPricePair cpp(coinStr, currencyStr);
        CPricePoint pp(cpp, uint64_t(price));
        pricePoints.push_back(pp);
    }

    const ComboMoney &cmFee = RPC_PARAM::GetFee(params, 2, PRICE_FEED_TX);

    // Get account for checking balance
    auto spCW = std::make_shared<CCacheWrapper>(*(forkPool.spCW)) ;
    CAccount account = RPC_PARAM::GetUserAccount(spCW->accountCache, feedUid);
    RPC_PARAM::CheckAccountBalance(account, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());

    int32_t validHeight = chainActive.Height();
    CPriceFeedTx tx(feedUid, validHeight, cmFee.symbol, cmFee.GetSawiAmount(), pricePoints);

    return SubmitTx(account.keyid, tx);
}

Value submitcoinstaketx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 2 || params.size() > 3) {
        throw runtime_error(
            "submitcoinstaketx \"addr\" \"coin_symbol\" \"coin_amount\" [\"symbol:fee:unit\"]\n"
            "\nstake fcoins\n"
            "\nArguments:\n"
            "1.\"addr\":            (string, required)\n"
            "2.\"coin_symbol\":     (WICC|WUSD|WGRT, required)\n"
            "2.\"coin_amount\":     (numeric, required) amount of coins to stake (positive) or unstake (negative)\n"
            "3.\"symbol:fee:unit\":  (string:numeric:string, optional) fee paid to miner, default is WICC:10000:sawi\n"
            "\nResult:\n"
            "\"txid\"                (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("submitcoinstaketx", "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\" \"WICC\" 200000000")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("submitcoinstaketx", "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\", \"WICC\", 200000000")
        );
    }

    EnsureWalletIsUnlocked();

    const CUserID& userId   = RPC_PARAM::GetUserId(params[0], true);
    string coinSymbol       = params[1].get_str();
    int64_t coinAmount      = params[2].get_int64();
    ComboMoney cmFee        = RPC_PARAM::GetFee(params, 2, UCOIN_STAKE_TX);
    int32_t validHeight     = chainActive.Height();
    BalanceOpType stakeType = coinAmount >= 0 ? BalanceOpType::STAKE : BalanceOpType::UNSTAKE;

    // Get account for checking balance
    auto spCW = std::make_shared<CCacheWrapper>(*(forkPool.spCW)) ;
    CAccount account = RPC_PARAM::GetUserAccount(spCW->accountCache, userId);
    RPC_PARAM::CheckAccountBalance(account, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());

    CCoinStakeTx tx(userId, validHeight, cmFee.symbol, cmFee.GetSawiAmount(), stakeType, coinSymbol, std::abs(coinAmount));
    return SubmitTx(account.keyid, tx);
}

/*************************************************<< CDP >>**************************************************/
Value submitcdpstaketx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 3 || params.size() > 6) {
        throw runtime_error(
            "submitcdpstaketx \"addr\" stake_combo_money mint_combo_money [\"cdp_id\"] [symbol:fee:unit]\n"
            "\nsubmit a CDP Staking Tx.\n"
            "\nArguments:\n"
            "1. \"addr\":               (string, required) CDP Staker's account address\n"
            "2. \"stake_combo_money\":  (symbol:amount:unit, required) Combo Money to stake into the CDP,"
            " default symbol=WICC, default unit=sawi\n"
            "3. \"mint_combo_money\":   (symbol:amount:unit, required), Combo Money to mint from the CDP,"
            " default symbol=WUSD, default unit=sawi\n"
            "4. \"cdp_id\":             (string, optional) CDP ID (tx hash of the first CDP Stake Tx)\n"
            "5. \"symbol:fee:unit\":    (symbol:amount:unit, optional) fee paid to miner, default is WICC:100000:sawi\n"
            "\nResult:\n"
            "\"txid\"                   (string) The transaction id.\n"
            "\nExamples:\n" +
            HelpExampleCli(
                "submitcdpstaketx",
                "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\" \"WICC:20000000000:sawi\" \"WUSD:3000000:sawi\" "
                "\"b850d88bf1bed66d43552dd724c18f10355e9b6657baeae262b3c86a983bee71\" \"WICC:1000000:sawi\"\n") +
            "\nAs json rpc call\n" +
            HelpExampleRpc(
                "submitcdpstaketx",
                "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\", \"WICC:2000000000:sawi\", \"WUSD:3000000:sawi\", "
                "\"b850d88bf1bed66d43552dd724c18f10355e9b6657baeae262b3c86a983bee71\", \"WICC:1000000:sawi\"\n"));
    }

    EnsureWalletIsUnlocked();

    const CUserID &cdpUid = RPC_PARAM::GetUserId(params[0], true);

    ComboMoney cmBcoinsToStake, cmScoinsToMint;
    if (!ParseRpcInputMoney(params[1].get_str(), cmBcoinsToStake, SYMB::WICC))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "bcoinsToStake ComboMoney format error");

    if (!ParseRpcInputMoney(params[2].get_str(), cmScoinsToMint, SYMB::WUSD))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "scoinsToMint ComboMoney format error");

    int32_t validHeight = forkPool.TipHeight();

    uint256 cdpId;
    if (params.size() > 3) {
        cdpId = RPC_PARAM::GetTxid(params[3], "cdp_id", true);
    }

    const ComboMoney &cmFee = RPC_PARAM::GetFee(params, 4, CDP_STAKE_TX);

    if (cdpId.IsEmpty()) { // new stake cdp
        if (cmBcoinsToStake.amount == 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "stake_amount is zero!");

        if (cmScoinsToMint.amount == 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "mint_amount is zero!");
    }

    auto spCW = std::make_shared<CCacheWrapper>(*(forkPool.spCW)) ;
    CAccount account = RPC_PARAM::GetUserAccount(spCW->accountCache, cdpUid);

    CCDPStakeTx tx(cdpUid, validHeight, cdpId, cmFee, cmBcoinsToStake, cmScoinsToMint);
    return SubmitTx(account.keyid, tx);
}

Value submitcdpredeemtx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 4 || params.size() > 5) {
        throw runtime_error(
            "submitcdpredeemtx \"addr\" \"cdp_id\" repay_amount redeem_amount [\"symbol:fee:unit\"]\n"
            "\nsubmit a CDP Redemption Tx\n"
            "\nArguments:\n"
            "1. \"addr\" :              (string, required) CDP redemptor's address\n"
            "2. \"cdp_id\":             (string, required) ID of existing CDP (tx hash of the first CDP Stake Tx)\n"
            "3. \"repay_amount\":       (numeric, required) scoins (E.g. WUSD) to repay into the CDP, boosted by 10^8\n"
            "4. \"redeem_amount\":      (numeric, required) bcoins (E.g. WICC) to redeem from the CDP, boosted by 10^8\n"
            "5. \"symbol:fee:unit\":    (string:numeric:string, optional) fee paid to miner, default is "
            "WICC:100000:sawi\n"
            "\nResult:\n"
            "\"txid\"                   (string) The transaction id.\n"
            "\nExamples:\n" +
            HelpExampleCli("submitcdpredeemtx",
                           "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\" "
                           "\"b850d88bf1bed66d43552dd724c18f10355e9b6657baeae262b3c86a983bee71\" "
                           "20000000000 40000000000 \"WICC:1000000:sawi\"\n") +
            "\nAs json rpc call\n" +
            HelpExampleRpc("submitcdpredeemtx",
                           "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\", "
                           "\"b850d88bf1bed66d43552dd724c18f10355e9b6657baeae262b3c86a983bee71\", "
                           "20000000000, 40000000000, \"WICC:1000000:sawi\"\n"));
    }

    EnsureWalletIsUnlocked();

    const CUserID& cdpUid   = RPC_PARAM::GetUserId(params[0].get_str(), true);
    uint256 cdpTxId         = uint256S(params[1].get_str());
    uint64_t repayAmount    = AmountToRawValue(params[2]);
    uint64_t redeemAmount   = AmountToRawValue(params[3]);
    const ComboMoney& cmFee = RPC_PARAM::GetFee(params, 4, CDP_STAKE_TX);
    int32_t validHeight     = forkPool.TipHeight();

    auto  spCW = std::make_shared<CCacheWrapper>(*(forkPool.spCW)) ;
    CAccount account = RPC_PARAM::GetUserAccount(spCW->accountCache, cdpUid);

    CCDPRedeemTx tx(cdpUid, cmFee, validHeight, cdpTxId, repayAmount, redeemAmount);
    return SubmitTx(account.keyid, tx);
}

Value submitcdpliquidatetx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 3 || params.size() > 4) {
        throw runtime_error(
            "submitcdpliquidatetx \"addr\" \"cdp_id\" liquidate_amount [symbol:fee:unit]\n"
            "\nsubmit a CDP Liquidation Tx\n"
            "\nArguments:\n"
            "1. \"addr\" :              (string, required) CDP liquidator's address\n"
            "2. \"cdp_id\":             (string, required) ID of existing CDP (tx hash of the first CDP Stake Tx)\n"
            "3. \"liquidate_amount\":   (numeric, required) WUSD coins to repay to CDP, boosted by 10^8 (penalty fees "
            "deducted separately from sender account)\n"
            "4. \"symbol:fee:unit\":    (string:numeric:string, optional) fee paid to miner, default is "
            "WICC:100000:sawi\n"
            "\nResult:\n"
            "\"txid\" (string) The transaction id.\n"
            "\nExamples:\n" +
            HelpExampleCli(
                "submitcdpliquidatetx",
                "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\"  "
                "\"b850d88bf1bed66d43552dd724c18f10355e9b6657baeae262b3c86a983bee71\" 20000000000 \"WICC:1000000:sawi\"\n") +
            "\nAs json rpc call\n" +
            HelpExampleRpc("submitcdpliquidatetx",
                           "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\", "
                           "\"b850d88bf1bed66d43552dd724c18f10355e9b6657baeae262b3c86a983bee71\", 2000000000, "
                           "\"WICC:1000000:sawi\"\n"));
    }

    EnsureWalletIsUnlocked();

    const CUserID& cdpUid    = RPC_PARAM::GetUserId(params[0], true);
    const uint256& cdpTxId   = RPC_PARAM::GetTxid(params[1], "cdp_id");
    uint64_t liquidateAmount = AmountToRawValue(params[2]);
    const ComboMoney& cmFee  = RPC_PARAM::GetFee(params, 3, CDP_STAKE_TX);
    int32_t validHeight      = forkPool.TipHeight();

    auto  spCW = std::make_shared<CCacheWrapper>(*(forkPool.spCW)) ;
    CAccount account = RPC_PARAM::GetUserAccount(spCW->accountCache, cdpUid);

    CCDPLiquidateTx tx(cdpUid, cmFee, validHeight, cdpTxId, liquidateAmount);
    return SubmitTx(account.keyid, tx);
}

Value getscoininfo(const Array& params, bool fHelp){
    if (fHelp || params.size() != 0) {
        throw runtime_error(
            "getscoininfo\n"
            "\nget stable coin info.\n"
            "\nArguments:\n"
            "\nResult:\n"
            "\nExamples:\n" +
            HelpExampleCli("getscoininfo", "") + "\nAs json rpc call\n" + HelpExampleRpc("getscoininfo", ""));
    }

    auto  spCW = std::make_shared<CCacheWrapper>(*(forkPool.spCW)) ;

    int32_t height = forkPool.TipHeight();

    uint64_t slideWindow = 0;
    if (!spCW->sysParamCache.GetParam(SysParamType::MEDIAN_PRICE_SLIDE_WINDOW_BLOCKCOUNT, slideWindow)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Acquire median price slide window blockcount error");
    }

    uint64_t globalCollateralCeiling = 0;
    if (!spCW->sysParamCache.GetParam(SysParamType::GLOBAL_COLLATERAL_CEILING_AMOUNT, globalCollateralCeiling)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Acquire global collateral ceiling error");
    }

    uint64_t globalCollateralRatioFloor = 0;
    if (!spCW->sysParamCache.GetParam(SysParamType::GLOBAL_COLLATERAL_RATIO_MIN, globalCollateralRatioFloor)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Acquire global collateral ratio floor error");
    }

    map<CoinPricePair, uint64_t> medianPricePoints;
    if (!spCW->ppCache.GetBlockMedianPricePoints(height, slideWindow, medianPricePoints)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Acquire median price error");
    }

    // TODO: multi stable coin
    uint64_t bcoinMedianPrice =
            spCW->ppCache.GetMedianPrice(height, slideWindow, CoinPricePair(SYMB::WICC, SYMB::USD));
    uint64_t globalCollateralRatio = spCW->cdpCache.cdpMemCache.GetGlobalCollateralRatio(bcoinMedianPrice);
    bool globalCollateralRatioFloorReached =
        spCW->cdpCache.CheckGlobalCollateralRatioFloorReached(bcoinMedianPrice, globalCollateralRatioFloor);

    uint64_t globalStakedBcoins = 0;
    uint64_t globalOwedScoins   = 0;
    spCW->cdpCache.cdpMemCache.GetGlobalItem(globalStakedBcoins, globalOwedScoins);

    bool global_collateral_ceiling_reached = globalStakedBcoins > globalCollateralCeiling * COIN;

    set<CUserCDP> forceLiquidateCdps;
    uint64_t forceLiquidateRatio = 0;
    if (!spCW->sysParamCache.GetParam(SysParamType::CDP_FORCE_LIQUIDATE_RATIO, forceLiquidateRatio)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Acquire cdp force liquidate ratio error");
    }

    spCW->cdpCache.cdpMemCache.GetCdpListByCollateralRatio(forceLiquidateRatio, bcoinMedianPrice,
                                                               forceLiquidateCdps);

    Object obj;
    Array prices;
    for (auto& item : medianPricePoints) {
        Object price;
        price.push_back(Pair("coin_symbol",                     item.first.first));
        price.push_back(Pair("price_symbol",                    item.first.second));
        price.push_back(Pair("price",                           (double)item.second / PRICE_BOOST));
        prices.push_back(price);
    }

    obj.push_back(Pair("height",                                height));
    obj.push_back(Pair("median_price",                          prices));
    obj.push_back(Pair("slide_window_block_count",              slideWindow));

    obj.push_back(Pair("global_staked_bcoins",                  globalStakedBcoins));
    obj.push_back(Pair("global_owed_scoins",                    globalOwedScoins));
    obj.push_back(Pair("global_collateral_ceiling",             globalCollateralCeiling * COIN));
    obj.push_back(Pair("global_collateral_ceiling_reached",     global_collateral_ceiling_reached));

    obj.push_back(Pair("global_collateral_ratio_floor",         strprintf("%.4f%%", (double)globalCollateralRatioFloor / RATIO_BOOST * 100)));
    obj.push_back(Pair("global_collateral_ratio",               strprintf("%.4f%%", (double)globalCollateralRatio / RATIO_BOOST * 100)));
    obj.push_back(Pair("global_collateral_ratio_floor_reached", globalCollateralRatioFloorReached));

    obj.push_back(Pair("force_liquidate_ratio",                 strprintf("%.4f%%", (double)forceLiquidateRatio / RATIO_BOOST * 100)));
    obj.push_back(Pair("force_liquidate_cdp_amount",            forceLiquidateCdps.size()));

    return obj;
}

Value listcdps(const Array& params, bool fHelp);
Value listcdpstoliquidate(const Array& params, bool fHelp);

Value getusercdp(const Array& params, bool fHelp){
    if (fHelp || params.size() < 1 || params.size() > 2) {
        throw runtime_error(
            "getusercdp \"addr\"\n"
            "\nget account's cdp.\n"
            "\nArguments:\n"
            "1.\"addr\": (string, required) CDP owner's account addr\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("getusercdp", "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\"\n")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("getusercdp", "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\"\n")
        );
    }

    auto pUserId = CUserID::ParseUserId(params[0].get_str());
    if (!pUserId) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid addr");
    }

    CAccount account;
    auto  spCW = std::make_shared<CCacheWrapper>(*(forkPool.spCW)) ;
    if (!spCW->accountCache.GetAccount(*pUserId, account)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("The account not exists! userId=%s", pUserId->ToString()));
    }

    int32_t height = forkPool.TipHeight();
    uint64_t slideWindow;
    spCW->sysParamCache.GetParam(SysParamType::MEDIAN_PRICE_SLIDE_WINDOW_BLOCKCOUNT, slideWindow);
    // TODO: multi stable coin
    uint64_t bcoinMedianPrice = spCW->ppCache.GetMedianPrice(height, slideWindow, CoinPricePair(SYMB::WICC, SYMB::USD));

    Array cdps;
    vector<CUserCDP> userCdps;
    if (spCW->cdpCache.GetCDPList(account.regid, userCdps)) {
        for (auto& cdp : userCdps) {
            cdps.push_back(cdp.ToJson(bcoinMedianPrice));
        }
    }

    Object obj;
    obj.push_back(Pair("user_cdps", cdps));
    return obj;
}

Value getcdp(const Array& params, bool fHelp){
    if (fHelp || params.size() < 1 || params.size() > 2) {
        throw runtime_error(
            "getcdp \"cdp_id\"\n"
            "\nget CDP by its CDP_ID\n"
            "\nArguments:\n"
            "1.\"cdp_id\": (string, required) cdp_id\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("getcdp", "\"c01f0aefeeb25fd6afa596f27ee3a1e861b657d2e1c341bfd1c412e87d9135c8\"\n")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("getcdp", "\"c01f0aefeeb25fd6afa596f27ee3a1e861b657d2e1c341bfd1c412e87d9135c8\"\n")
        );
    }

    int32_t height = forkPool.TipHeight();
    uint64_t slideWindow;
    auto  spCW = std::make_shared<CCacheWrapper>(*(forkPool.spCW)) ;
    spCW->sysParamCache.GetParam(SysParamType::MEDIAN_PRICE_SLIDE_WINDOW_BLOCKCOUNT, slideWindow);
    // TODO: multi stable coin
    uint64_t bcoinMedianPrice = spCW->ppCache.GetMedianPrice(height, slideWindow, CoinPricePair(SYMB::WICC, SYMB::USD));

    uint256 cdpTxId(uint256S(params[0].get_str()));
    CUserCDP cdp;
    if (!spCW->cdpCache.GetCDP(cdpTxId, cdp)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("CDP (%s) does not exist!", cdpTxId.GetHex()));
    }

    Object obj;
    obj.push_back(Pair("cdp", cdp.ToJson(bcoinMedianPrice)));
    return obj;
}

/*************************************************<< DEX >>**************************************************/
Value submitdexbuylimitordertx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 5 || params.size() > 6) {
        throw runtime_error(
            "submitdexbuylimitordertx \"addr\" \"coin_symbol\" \"asset_symbol\" asset_amount price [symbol:fee:unit]\n"
            "\nsubmit a dex buy limit price order tx.\n"
            "\nArguments:\n"
            "1.\"addr\": (string required) order owner address\n"
            "2.\"coin_symbol\": (string required) coin type to pay\n"
            "3.\"asset_symbol\": (string required), asset type to buy\n"
            "4.\"asset_amount\": (numeric, required) amount of target asset to buy\n"
            "5.\"price\": (numeric, required) bidding price willing to buy\n"
            "6.\"symbol:fee:unit\":(string:numeric:string, optional) fee paid for miner, default is WICC:10000:sawi\n"
            "\nResult:\n"
            "\"txid\" (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("submitdexbuylimitordertx", "\"10-3\" \"WUSD\" \"WICC\" 1000000 200000000\n")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("submitdexbuylimitordertx", "\"10-3\" \"WUSD\" \"WICC\" 1000000 200000000\n")
        );
    }

    EnsureWalletIsUnlocked();

    const CUserID& userId          = RPC_PARAM::GetUserId(params[0], true);
    const TokenSymbol& coinSymbol  = RPC_PARAM::GetOrderCoinSymbol(params[1]);
    const TokenSymbol& assetSymbol = RPC_PARAM::GetOrderAssetSymbol(params[2]);
    uint64_t assetAmount           = AmountToRawValue(params[3]);
    uint64_t price                 = RPC_PARAM::GetPrice(params[4]);  // TODO: need to check price?
    ComboMoney cmFee               = RPC_PARAM::GetFee(params, 5, DEX_LIMIT_BUY_ORDER_TX);

    // Get account for checking balance
    auto  spCW = std::make_shared<CCacheWrapper>(*(forkPool.spCW)) ;
    CAccount account = RPC_PARAM::GetUserAccount(spCW->accountCache, userId);
    RPC_PARAM::CheckAccountBalance(account, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());
    uint64_t coinAmount = CDEXOrderBaseTx::CalcCoinAmount(assetAmount, price);
    RPC_PARAM::CheckAccountBalance(account, coinSymbol, FREEZE, coinAmount);

    int32_t validHeight = forkPool.TipHeight();
    CDEXBuyLimitOrderTx tx(userId, validHeight, cmFee.symbol, cmFee.GetSawiAmount(), coinSymbol,
                           assetSymbol, assetAmount, price);
    return SubmitTx(account.keyid, tx);
}

Value submitdexselllimitordertx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 5 || params.size() > 6) {
        throw runtime_error(
            "submitdexselllimitordertx \"addr\" \"coin_symbol\" \"asset_symbol\" asset_amount price [symbol:fee:unit]\n"
            "\nsubmit a dex buy limit price order tx.\n"
            "\nArguments:\n"
            "1.\"addr\": (string required) order owner address\n"
            "2.\"coin_symbol\": (string required) coin type to pay\n"
            "3.\"asset_symbol\": (string required), asset type to buy\n"
            "4.\"asset_amount\": (numeric, required) amount of target asset to buy\n"
            "5.\"price\": (numeric, required) bidding price willing to buy\n"
            "6.\"symbol:fee:unit\":(string:numeric:string, optional) fee paid for miner, default is WICC:10000:sawi\n"
            "\nResult:\n"
            "\"txid\" (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("submitdexselllimitordertx", "\"10-3\" \"WUSD\" \"WICC\" 1000000 200000000\n")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("submitdexselllimitordertx", "\"10-3\" \"WUSD\" \"WICC\" 1000000 200000000\n")
        );
    }

    EnsureWalletIsUnlocked();

    const CUserID& userId          = RPC_PARAM::GetUserId(params[0], true);
    const TokenSymbol& coinSymbol  = RPC_PARAM::GetOrderCoinSymbol(params[1]);
    const TokenSymbol& assetSymbol = RPC_PARAM::GetOrderAssetSymbol(params[2]);
    uint64_t assetAmount           = AmountToRawValue(params[3]);
    uint64_t price                 = RPC_PARAM::GetPrice(params[4]);
    ComboMoney cmFee               = RPC_PARAM::GetFee(params, 5, DEX_LIMIT_SELL_ORDER_TX);

    // Get account for checking balance
    auto  spCW = std::make_shared<CCacheWrapper>(*(forkPool.spCW)) ;
    CAccount account = RPC_PARAM::GetUserAccount(spCW->accountCache, userId);
    RPC_PARAM::CheckAccountBalance(account, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());
    RPC_PARAM::CheckAccountBalance(account, assetSymbol, FREEZE, assetAmount);

    int32_t validHeight = forkPool.TipHeight();
    CDEXSellLimitOrderTx tx(userId, validHeight, cmFee.symbol, cmFee.GetSawiAmount(), coinSymbol, assetSymbol,
                            assetAmount, price);
    return SubmitTx(account.keyid, tx);
}

Value submitdexbuymarketordertx(const Array& params, bool fHelp) {
     if (fHelp || params.size() < 4 || params.size() > 5) {
        throw runtime_error(
            "submitdexbuymarketordertx \"addr\" \"coin_symbol\" coin_amount \"asset_symbol\" [symbol:fee:unit]\n"
            "\nsubmit a dex buy market price order tx.\n"
            "\nArguments:\n"
            "1.\"addr\": (string required) order owner address\n"
            "2.\"coin_symbol\": (string required) coin type to pay\n"
            "3.\"coin_amount\": (numeric, required) amount of target coin to buy\n"
            "4.\"asset_symbol\": (string required), asset type to buy\n"
            "5.\"symbol:fee:unit\":(string:numeric:string, optional) fee paid for miner, default is WICC:10000:sawi\n"
            "\nResult:\n"
            "\"txid\" (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("submitdexbuymarketordertx", "\"10-3\" \"WUSD\" 200000000 \"WICC\"\n")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("submitdexbuymarketordertx", "\"10-3\" \"WUSD\" 200000000 \"WICC\"\n")
        );
    }

    EnsureWalletIsUnlocked();

    const CUserID& userId          = RPC_PARAM::GetUserId(params[0], true);
    const TokenSymbol& coinSymbol  = RPC_PARAM::GetOrderCoinSymbol(params[1]);
    uint64_t coinAmount            = AmountToRawValue(params[2]);
    const TokenSymbol& assetSymbol = RPC_PARAM::GetOrderAssetSymbol(params[3]);
    ComboMoney cmFee               = RPC_PARAM::GetFee(params, 4, DEX_MARKET_BUY_ORDER_TX);

    // Get account for checking balance
    auto  spCW = std::make_shared<CCacheWrapper>(*(forkPool.spCW)) ;
    CAccount account = RPC_PARAM::GetUserAccount(spCW->accountCache, userId);
    RPC_PARAM::CheckAccountBalance(account, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());
    RPC_PARAM::CheckAccountBalance(account, coinSymbol, FREEZE, coinAmount);

    int32_t validHeight = forkPool.TipHeight();
    CDEXBuyMarketOrderTx tx(userId, validHeight, cmFee.symbol, cmFee.GetSawiAmount(), coinSymbol, assetSymbol,
                            coinAmount);
    return SubmitTx(account.keyid, tx);
}

Value submitdexsellmarketordertx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 4 || params.size() > 5) {
        throw runtime_error(
            "submitdexsellmarketordertx \"addr\" \"coin_symbol\" \"asset_symbol\" asset_amount [symbol:fee:unit]\n"
            "\nsubmit a dex sell market price order tx.\n"
            "\nArguments:\n"
            "1.\"addr\": (string required) order owner address\n"
            "2.\"coin_symbol\": (string required) coin type to pay\n"
            "3.\"asset_symbol\": (string required), asset type to buy\n"
            "4.\"asset_amount\": (numeric, required) amount of target asset to buy\n"
            "5.\"symbol:fee:unit\":(string:numeric:string, optional) fee paid for miner, default is WICC:10000:sawi\n"
            "\nResult:\n"
            "\"txid\" (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("submitdexsellmarketordertx", "\"10-3\" \"WUSD\" \"WICC\" 200000000\n")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("submitdexsellmarketordertx", "\"10-3\" \"WUSD\" \"WICC\" 200000000\n")
        );
    }

    const CUserID& userId          = RPC_PARAM::GetUserId(params[0], true);
    const TokenSymbol& coinSymbol  = RPC_PARAM::GetOrderCoinSymbol(params[1]);
    const TokenSymbol& assetSymbol = RPC_PARAM::GetOrderAssetSymbol(params[2]);
    uint64_t assetAmount           = AmountToRawValue(params[3]);
    ComboMoney cmFee               = RPC_PARAM::GetFee(params, 4, DEX_MARKET_SELL_ORDER_TX);

    // Get account for checking balance
    auto  spCW = std::make_shared<CCacheWrapper>(*(forkPool.spCW)) ;
    CAccount account = RPC_PARAM::GetUserAccount(spCW->accountCache, userId);
    RPC_PARAM::CheckAccountBalance(account, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());
    RPC_PARAM::CheckAccountBalance(account, assetSymbol, FREEZE, assetAmount);

    int32_t validHeight = forkPool.TipHeight();
    CDEXSellMarketOrderTx tx(userId, validHeight, cmFee.symbol, cmFee.GetSawiAmount(), coinSymbol, assetSymbol,
                             assetAmount);
    return SubmitTx(account.keyid, tx);
}

Value submitdexcancelordertx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 2 || params.size() > 3) {
        throw runtime_error(
            "submitdexcancelordertx \"addr\" \"txid\" [symbol:fee:unit]\n"
            "\nsubmit a dex cancel order tx.\n"
            "\nArguments:\n"
            "1.\"addr\": (string required) order owner address\n"
            "2.\"txid\": (string required) order tx want to cancel\n"
            "3.\"symbol:fee:unit\":(string:numeric:string, optional) fee paid for miner, default is WICC:10000:sawi\n"
            "\nResult:\n"
            "\"txid\" (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("submitdexcancelordertx", "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\" "
                             "\"c5287324b89793fdf7fa97b6203dfd814b8358cfa31114078ea5981916d7a8ac\" ")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("submitdexcancelordertx", "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\" "\
                             "\"c5287324b89793fdf7fa97b6203dfd814b8358cfa31114078ea5981916d7a8ac\"")
        );
    }

    EnsureWalletIsUnlocked();

    const CUserID& userId = RPC_PARAM::GetUserId(params[0], true);
    const uint256& txid   = RPC_PARAM::GetTxid(params[1], "txid");
    ComboMoney cmFee      = RPC_PARAM::GetFee(params, 2, DEX_MARKET_SELL_ORDER_TX);

    // Get account for checking balance
    auto  spCW = std::make_shared<CCacheWrapper>(*(forkPool.spCW)) ;
    CAccount account = RPC_PARAM::GetUserAccount(spCW->accountCache, userId);
    RPC_PARAM::CheckAccountBalance(account, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());

    // check active order tx
    RPC_PARAM::CheckActiveOrderExisted(spCW->dexCache, txid);

    int32_t validHeight = forkPool.TipHeight();
    CDEXCancelOrderTx tx(userId, validHeight, cmFee.symbol, cmFee.GetSawiAmount(), txid);
    return SubmitTx(account.keyid, tx);
}

Value submitdexsettletx(const Array& params, bool fHelp) {
     if (fHelp || params.size() < 2 || params.size() > 3) {
        throw runtime_error(
            "submitdexsettletx \"addr\" \"deal_items\" [symbol:fee:unit]\n"
            "\nsubmit a dex settle tx.\n"
            "\nArguments:\n"
            "1.\"addr\": (string required) settle owner address\n"
            "2.\"deal_items\": (string required) deal items in json format\n"
            " [\n"
            "   {\n"
            "      \"buy_order_id\":\"txid\", (string, required) order txid of buyer\n"
            "      \"sell_order_id\":\"txid\", (string, required) order txid of seller\n"
            "      \"deal_price\":n (numeric, required) deal price\n"
            "      \"deal_coin_amount\":n (numeric, required) deal amount of coin\n"
            "      \"deal_asset_amount\":n (numeric, required) deal amount of asset\n"
            "   }\n"
            "       ,...\n"
            " ]\n"
            "3.\"symbol:fee:unit\":(string:numeric:string, optional) fee paid for miner, default is WICC:10000:sawi\n"
            "\nResult:\n"
            "\"txid\" (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("submitdexsettletx", "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\" "
                           "\"[{\\\"buy_order_id\\\":\\\"c5287324b89793fdf7fa97b6203dfd814b8358cfa31114078ea5981916d7a8ac\\\", "
                           "\\\"sell_order_id\\\":\\\"c5287324b89793fdf7fa97b6203dfd814b8358cfa31114078ea5981916d7a8a1\\\", "
                           "\\\"deal_price\\\":100000000,"
                           "\\\"deal_coin_amount\\\":100000000,"
                           "\\\"deal_asset_amount\\\":100000000}]\" ")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("submitdexsettletx", "\"WiZx6rrsBn9sHjwpvdwtMNNX2o31s3DEHH\" "\
                           "[{\"buy_order_id\":\"c5287324b89793fdf7fa97b6203dfd814b8358cfa31114078ea5981916d7a8ac\", "
                           "\"sell_order_id\":\"c5287324b89793fdf7fa97b6203dfd814b8358cfa31114078ea5981916d7a8a1\", "
                           "\"deal_price\":100000000,"
                           "\"deal_coin_amount\":100000000,"
                           "\"deal_asset_amount\":100000000}]")
        );
    }

    EnsureWalletIsUnlocked();

    const CUserID &userId = RPC_PARAM::GetUserId(params[0], true);
    Array dealItemArray = params[1].get_array();
    ComboMoney fee = RPC_PARAM::GetFee(params, 2, DEX_LIMIT_BUY_ORDER_TX);

    vector<DEXDealItem> dealItems;
    for (auto dealItemObj : dealItemArray) {
        DEXDealItem dealItem;
        const Value& buy_order_id      = JSON::GetObjectFieldValue(dealItemObj, "buy_order_id");
        dealItem.buyOrderId            = RPC_PARAM::GetTxid(buy_order_id, "buy_order_id");
        const Value& sell_order_id     = JSON::GetObjectFieldValue(dealItemObj, "sell_order_id");
        dealItem.sellOrderId           = RPC_PARAM::GetTxid(sell_order_id.get_str(), "sell_order_id");
        const Value& deal_price        = JSON::GetObjectFieldValue(dealItemObj, "deal_price");
        dealItem.dealPrice             = RPC_PARAM::GetPrice(deal_price);
        const Value& deal_coin_amount  = JSON::GetObjectFieldValue(dealItemObj, "deal_coin_amount");
        dealItem.dealCoinAmount        = AmountToRawValue(deal_coin_amount);
        const Value& deal_asset_amount = JSON::GetObjectFieldValue(dealItemObj, "deal_asset_amount");
        dealItem.dealAssetAmount       = AmountToRawValue(deal_asset_amount);
        dealItems.push_back(dealItem);
    }

    // Get account for checking balance
    auto  spCW = std::make_shared<CCacheWrapper>(*(forkPool.spCW)) ;
    CAccount account = RPC_PARAM::GetUserAccount(spCW->accountCache, userId);
    RPC_PARAM::CheckAccountBalance(account, fee.symbol, SUB_FREE, fee.GetSawiAmount());

    int32_t validHeight = forkPool.TipHeight();
    CDEXSettleTx tx(userId, validHeight, fee.symbol, fee.GetSawiAmount(), dealItems);
    return SubmitTx(account.keyid, tx);
}

Value getdexorder(const Array& params, bool fHelp) {
     if (fHelp || params.size() != 1) {
        throw runtime_error(
            "getdexorder \"order_id\"\n"
            "\nget dex order detail.\n"
            "\nArguments:\n"
            "1.\"order_id\":    (string, required) order txid\n"
            "\nResult: object of order detail\n"
            "\nExamples:\n"
            + HelpExampleCli("getdexorder", "\"c5287324b89793fdf7fa97b6203dfd814b8358cfa31114078ea5981916d7a8ac\"")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("getdexorder", "\"c5287324b89793fdf7fa97b6203dfd814b8358cfa31114078ea5981916d7a8ac\"")
        );
    }
    const uint256 &orderId = RPC_PARAM::GetTxid(params[0], "order_id");

    auto  spCW = std::make_shared<CCacheWrapper>(*(forkPool.spCW)) ;
    auto pDexCache = spCW->dexCache;
    CDEXOrderDetail orderDetail;
    if (!pDexCache.GetActiveOrder(orderId, orderDetail))
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("The order not exists or inactive! order_id=%s", orderId.ToString()));

    Object obj;
    DEX_DB::OrderToJson(orderId, orderDetail, obj);
    return obj;
}

extern Value getdexsysorders(const Array& params, bool fHelp) {
     if (fHelp || params.size() > 1) {
        throw runtime_error(
            "getdexsysorders [\"height\"]\n"
            "\nget dex system-generated active orders by block height.\n"
            "\nArguments:\n"
            "1.\"height\":  (numeric, optional) block height, default is current tip block height\n"
            "\nResult:\n"
            "\"height\"     (string) the specified block height.\n"
            "\"orders\"     (string) a list of system-generated DEX orders.\n"
            "\nExamples:\n"
            + HelpExampleCli("getdexsysorders", "10")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("getdexsysorders", "10")
        );
    }

    int64_t tipHeight = chainActive.Height();
    int64_t height    = tipHeight;
    if (params.size() > 0)
        height = params[0].get_int64();

    if (height < 0 || height > tipHeight) {
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("height=%d must >= 0 and <= tip_height=%d", height, tipHeight));
    }

    auto  spCW = std::make_shared<CCacheWrapper>(*(forkPool.spCW)) ;
    auto pGetter = spCW->dexCache.CreateSysOrdersGetter();
    if (!pGetter->Execute(height)) {
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("get system-generated orders error! height=%d", height));
    }

    Object obj;
    obj.push_back(Pair("height", height));
    pGetter->ToJson(obj);

    return obj;
}

extern Value getdexorders(const Array& params, bool fHelp) {
     if (fHelp || params.size() > 4) {
        throw runtime_error(
            "getdexorders [\"begin_height\"] [\"end_height\"] [\"max_count\"] [\"last_pos_info\"]\n"
            "\nget dex all active orders by block height range.\n"
            "\nArguments:\n"
            "1.\"begin_height\":    (numeric, optional) the begin block height, default is 0\n"
            "2.\"end_height\":      (numeric, optional) the end block height, default is current tip block height\n"
            "3.\"max_count\":       (numeric, optional) the max order count to get, default is 500\n"
            "4.\"last_pos_info\":   (string, optional) the last position info to get more orders, default is empty\n"
            "\nResult:\n"
            "\"begin_height\"       (numeric) the begin block height of returned orders.\n"
            "\"end_height\"         (numeric) the end block height of returned orders.\n"
            "\"has_more\"           (bool) has more orders in db.\n"
            "\"last_pos_info\"      (string) the last position info to get more orders.\n"
            "\"count\"              (numeric) the count of returned orders.\n"
            "\"orders\"             (string) a list of system-generated DEX orders.\n"
            "\nExamples:\n"
            + HelpExampleCli("getdexorders", "0 100 500")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("getdexorders", "0, 100, 500")
        );
    }

    int64_t tipHeight = chainActive.Height();
    int64_t beginHeight = 0;
    if (params.size() > 0)
        beginHeight = params[0].get_int64();
    if (beginHeight < 0 || beginHeight > tipHeight) {
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("begin_height=%d must >= 0 and <= tip_height=%d", beginHeight, tipHeight));
    }

    int64_t endHeight = tipHeight;
    if (params.size() > 1)
        endHeight = params[1].get_int64();
    if (endHeight < beginHeight || endHeight > tipHeight) {
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("end_height=%d must >= begin_height=%d and <= tip_height=%d",
            endHeight, beginHeight, tipHeight));
    }


    int64_t maxCount = 500;
    if (params.size() > 2) {
        maxCount = params[2].get_int64();
        if (maxCount < 0)
            throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("max_count=%d must >= 0", maxCount));
    }

    DEXBlockOrdersCache::KeyType lastKey;
    if (params.size() > 3) {
        string lastPosInfo = RPC_PARAM::GetBinStrFromHex(params[3], "last_pos_info");
        auto err = DEX_DB::ParseLastPos(lastPosInfo, lastKey);
        if (err)
            throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("Invalid last_pos_info! %s", *err));
        uint32_t lastHeight = DEX_DB::GetHeight(lastKey);
        if (lastHeight < beginHeight || lastHeight > endHeight)
            throw JSONRPCError(RPC_INVALID_PARAMS,
                               strprintf("Invalid last_pos_info! height of last_pos_info is not in "
                                         "range(begin=%d,end=%d) ",
                                         beginHeight, endHeight));
    }

    auto  spCW = std::make_shared<CCacheWrapper>(*(forkPool.spCW)) ;
    auto pGetter = spCW->dexCache.CreateOrdersGetter();
    if (!pGetter->Execute(beginHeight, endHeight, maxCount, lastKey)) {
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("get all active orders error! begin_height=%d, end_height=%d",
            beginHeight, endHeight));
    }

    string newLastPosInfo;
    if (pGetter->has_more) {
        auto err = DEX_DB::MakeLastPos(pGetter->last_key, newLastPosInfo);
        if (err)
            throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("Make new last_pos_info error! %s", *err));
    }
    Object obj;
    obj.push_back(Pair("begin_height", (int64_t)pGetter->begin_height));
    obj.push_back(Pair("end_height", (int64_t)pGetter->end_height));
    obj.push_back(Pair("has_more", pGetter->has_more));
    obj.push_back(Pair("last_pos_info", HexStr(newLastPosInfo)));
    pGetter->ToJson(obj);
    return obj;
}

///////////////////////////////////////////////////////////////////////////////
// asset tx rpc

Value submitassetissuetx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 6 || params.size() > 7) {
        throw runtime_error(
            "submitassetissuetx \"addr\" \"asset_symbol\" \"asset_owner_addr\" \"asset_name\" total_supply mintable [symbol:fee:unit]\n"
            "\nsubmit an asset issue tx.\n"
            "\nthe tx creator must have enough WICC for issued fee(550 WICC).\n"
            "\nArguments:\n"
            "1.\"addr\":            (string, required) tx owner address\n"
            "2.\"asset_symbol\":    (string, required) asset symbol, E.g WICC | WUSD\n"
            "3.\"asset_owner_addr\":(string, required) asset owner address, can be same as tx owner address\n"
            "4.\"asset_name\":      (string, required) asset long name, E.g WaykiChain coin\n"
            "5.\"total_supply\":    (numeric, required) asset total supply\n"
            "6.\"mintable\":        (boolean, required) whether this asset token can be minted in the future\n"
            "7.\"symbol:fee:unit\": (string:numeric:string, optional) fee paid for miner, default is WICC:10000:sawi\n"
            "\nResult:\n"
            "\"txid\"               (string) The new transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("submitassetissuetx", "\"10-2\" \"CNY\" \"10-2\" \"RMB\" 1000000000000000 true")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("submitassetissuetx", "\"10-2\", \"CNY\", \"10-2\", \"RMB\", 1000000000000000, true")
        );
    }

    EnsureWalletIsUnlocked();

    const CUserID& uid             = RPC_PARAM::GetUserId(params[0]);
    const TokenSymbol& assetSymbol = RPC_PARAM::GetAssetIssueSymbol(params[1]);
    const CUserID& assetOwnerUid   = RPC_PARAM::GetUserId(params[2]);
    const TokenName& assetName     = RPC_PARAM::GetAssetName(params[3]);
    int64_t totalSupply            = params[4].get_int64();
    if (totalSupply <= 0 || (uint64_t)totalSupply > MAX_ASSET_TOTAL_SUPPLY)
        throw JSONRPCError(RPC_INVALID_PARAMS,
                           strprintf("asset total_supply=%lld can not <= 0 or > %llu", totalSupply, MAX_ASSET_TOTAL_SUPPLY));
    bool mintable    = params[5].get_bool();
    ComboMoney cmFee = RPC_PARAM::GetFee(params, 6, TxType::ASSET_ISSUE_TX);

    // Get account for checking balance
    auto  spCW = std::make_shared<CCacheWrapper>(*(forkPool.spCW)) ;
    CAccount account = RPC_PARAM::GetUserAccount(spCW->accountCache, uid);
    RPC_PARAM::CheckAccountBalance(account, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());

    uint64_t assetIssueFee; //550 WICC
    if (!spCW->sysParamCache.GetParam(ASSET_ISSUE_FEE, assetIssueFee))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "read system param ASSET_ISSUE_FEE error");
    RPC_PARAM::CheckAccountBalance(account, SYMB::WICC, SUB_FREE, assetIssueFee);

    int32_t validHeight = chainActive.Height();
    CAccount ownerAccount;
    CRegID *pOwnerRegid;
    if (account.IsMyUid(assetOwnerUid)) {
        pOwnerRegid = &account.regid;
    } else {
        ownerAccount = RPC_PARAM::GetUserAccount(spCW->accountCache, assetOwnerUid);
        pOwnerRegid = &ownerAccount.regid;
    }

    if (pOwnerRegid->IsEmpty() || !pOwnerRegid->IsMature(validHeight)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("owner regid=%s is not registerd or not mature",
            pOwnerRegid->ToString()));
    }

    CBaseAsset asset(assetSymbol, CUserID(*pOwnerRegid), assetName, (uint64_t)totalSupply, mintable);
    CAssetIssueTx tx(uid, validHeight, cmFee.symbol, cmFee.GetSawiAmount(), asset);
    return SubmitTx(account.keyid, tx);
}

Value submitassetupdatetx(const Array& params, bool fHelp) {
    if (fHelp || params.size() < 4 || params.size() > 5) {
        throw runtime_error(
            "submitassetupdatetx \"addr\" \"asset_symbol\" \"update_type\" \"update_value\" [symbol:fee:unit]\n"
            "\nsubmit an asset update tx.\n"
            "\nthe tx creator must have enough WICC for asset update fee(200 WICC).\n"
            "\nArguments:\n"
            "1.\"addr\":            (string, required) tx owner address\n"
            "2.\"asset_symbol\":    (string, required) asset symbol, E.g WICC | WUSD\n"
            "3.\"update_type\":     (string, required) asset update type, can be (owner_addr, name, mint_amount)\n"
            "4.\"update_value\":    (string, required) update the value specified by update_type, value format see the submitassetissuetx\n"
            "5.\"symbol:fee:unit\": (string:numeric:string, optional) fee paid for miner, default is WICC:10000:sawi\n"
            "\nResult:\n"
            "\"txid\"               (string) The new transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("submitassetupdatetx", "\"10-2\" \"CNY\" \"mint_amount\" \"100000000\"")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("submitassetupdatetx", "\"10-2\", \"CNY\", \"mint_amount\", \"100000000\"")
        );
    }

    EnsureWalletIsUnlocked();

    const CUserID& uid             = RPC_PARAM::GetUserId(params[0]);
    const TokenSymbol& assetSymbol = RPC_PARAM::GetAssetIssueSymbol(params[1]);
    const string &updateTypeStr = params[2].get_str();
    const Value &jsonUpdateValue = params[3].get_str();

    auto pUpdateType = CAssetUpdateData::ParseUpdateType(updateTypeStr);
    if (!pUpdateType)
        throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("Invalid update_type=%s", updateTypeStr));

    CAssetUpdateData updateData;
    switch(*pUpdateType) {
        case CAssetUpdateData::OWNER_UID: {
            const string &valueStr = jsonUpdateValue.get_str();
            auto pNewOwnerUid = CUserID::ParseUserId(valueStr);
            if (!pNewOwnerUid || pNewOwnerUid->IsEmpty()) {
                throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("invalid updated owner user id=%s",
                    valueStr.size()));
            }
            updateData.Set(*pNewOwnerUid);
            break;
        }
        case CAssetUpdateData::NAME: {
            const string &valueStr = jsonUpdateValue.get_str();
            if (valueStr.size() == 0 || valueStr.size() > MAX_ASSET_NAME_LEN) {
                throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("invalid asset name! empty, or length=%d greater than %d",
                    valueStr.size(), MAX_ASSET_NAME_LEN));
            }
            updateData.Set(valueStr);
            break;
        }
        case CAssetUpdateData::MINT_AMOUNT: {
            uint64_t mintAmount;
            if (jsonUpdateValue.type() == json_spirit::Value_type::int_type ) {
                int64_t v = jsonUpdateValue.get_int64();
                if (v < 0)
                    throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("invalid mint amount=%lld as uint64_t type",
                        v, MAX_ASSET_NAME_LEN));
                mintAmount = v;
            } else if (jsonUpdateValue.type() == json_spirit::Value_type::str_type) {
                const string &valueStr = jsonUpdateValue.get_str();
                if (!ParseUint64(valueStr, mintAmount))
                    throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("invalid mint_amount=%s as uint64_t type",
                        valueStr, MAX_ASSET_NAME_LEN));
            } else
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid json value type: %s",
                    JSON::GetValueTypeName(jsonUpdateValue.type())));

            if (mintAmount == 0 || mintAmount > MAX_ASSET_TOTAL_SUPPLY)
                throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("Invalid asset mint_amount=%llu, cannot be 0, or greater than %llu",
                    mintAmount, MAX_ASSET_TOTAL_SUPPLY));

            updateData.Set(mintAmount);
            break;
        }
        default: {
            throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("unsupported updated_key=%s", updateTypeStr));
        }
    }

    ComboMoney cmFee = RPC_PARAM::GetFee(params, 4, TxType::ASSET_UPDATE_TX);

    // Get account for checking balance
    auto  spCW = std::make_shared<CCacheWrapper>(*(forkPool.spCW)) ;
    CAccount account = RPC_PARAM::GetUserAccount(spCW->accountCache, uid);
    RPC_PARAM::CheckAccountBalance(account, cmFee.symbol, SUB_FREE, cmFee.GetSawiAmount());

    uint64_t assetUpdateFee;
    if (!spCW->sysParamCache.GetParam(ASSET_UPDATE_FEE, assetUpdateFee))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "read system param ASSET_UPDATE_FEE error");
    RPC_PARAM::CheckAccountBalance(account, SYMB::WICC, SUB_FREE, assetUpdateFee);

    int32_t validHeight = chainActive.Height();

    if (*pUpdateType == CAssetUpdateData::OWNER_UID) {
        CUserID &ownerUid = updateData.get<CUserID>();
        if (account.IsMyUid(ownerUid))
            return JSONRPCError(RPC_INVALID_PARAMS, strprintf("the new owner uid=%s is belong to old owner account",
                    ownerUid.ToDebugString()));

        CAccount newAccount = RPC_PARAM::GetUserAccount(spCW->accountCache, ownerUid);
        if (!newAccount.IsRegistered())
            return JSONRPCError(RPC_INVALID_PARAMS, strprintf("the new owner account is not registered! new uid=%s",
                    ownerUid.ToDebugString()));
        if (!newAccount.regid.IsMature(validHeight))
            return JSONRPCError(RPC_INVALID_PARAMS, strprintf("the new owner regid is not matured! new uid=%s",
                ownerUid.ToDebugString()));
        ownerUid = newAccount.regid;
    }

    CAssetUpdateTx tx(uid, validHeight, cmFee.symbol, cmFee.GetSawiAmount(), assetSymbol, updateData);
    return SubmitTx(account.keyid, tx);
}

extern Value getassets(const Array& params, bool fHelp) {
     if (fHelp || params.size() > 4) {
        throw runtime_error(
            "getassets\n"
            "\nget all assets, include active orders by block height range.\n"
            "\nArguments:\n"
            "\nResult: a list of assets\n"
            "\nExamples:\n"
            + HelpExampleCli("getassets", "")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("getassets", "")
        );
    }

    auto  spCW = std::make_shared<CCacheWrapper>(*(forkPool.spCW)) ;
    auto pGetter = spCW->assetCache.CreateUserAssetsGetter();
    if (!pGetter || !pGetter->Execute()) {
        throw JSONRPCError(RPC_INVALID_PARAMS, "get all user issued assets error!");
    }

    Array assetArray;
    for (auto &item : pGetter->data_list) {
        const CAsset &asset = item.second;
        assetArray.push_back(AssetToJson(spCW->accountCache, asset));
    }

    Object obj;
    obj.push_back(Pair("count", (int64_t)pGetter->data_list.size()));
    obj.push_back(Pair("assets", assetArray));
    return obj;
}