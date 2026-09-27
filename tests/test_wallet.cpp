#include <gtest/gtest.h>
#include "Wallet.h"

namespace
{
    OrderBookEntry makeSale(double price, double amount, const std::string& product, OrderBookType type)
    {
        return OrderBookEntry{price, amount, "2020-01-02 00:00:00.000", product, type, "user"};
    }
}

// ---- Multi-asset conversions ----------------------------------------------

TEST(WalletMultiAssetConversion, AskSaleOnNonUsdtPair_ConvertsBaseToQuoteCorrectly)
{
    Wallet w;
    w.insertCurrency("ETH", 5.0);

    // sold 2 ETH at 0.05 BTC each -> lose 2 ETH, gain 0.1 BTC
    OrderBookEntry sale = makeSale(0.05, 2.0, "ETH/BTC", OrderBookType::asksale);
    w.processSale(sale);

    EXPECT_DOUBLE_EQ(w.getBalances().at("ETH"), 3.0);
    EXPECT_NEAR(w.getBalances().at("BTC"), 0.1, 1e-9);
}

TEST(WalletMultiAssetConversion, BidSaleOnNonUsdtPair_ConvertsQuoteToBaseCorrectly)
{
    Wallet w;
    w.insertCurrency("BTC", 1.0);

    // bought 1000 DOGE at 0.000002 BTC each -> lose 0.002 BTC, gain 1000 DOGE
    OrderBookEntry sale = makeSale(0.000002, 1000.0, "DOGE/BTC", OrderBookType::bidsale);
    w.processSale(sale);

    EXPECT_DOUBLE_EQ(w.getBalances().at("DOGE"), 1000.0);
    EXPECT_NEAR(w.getBalances().at("BTC"), 0.998, 1e-9);
}

TEST(WalletMultiAssetConversion, ChainedConversionsAcrossThreeAssets_EachLegIndependentlyCorrect)
{
    // ETH -> BTC -> USDT, exercising three distinct currencies in one wallet.
    Wallet w;
    w.insertCurrency("ETH", 10.0);
    w.insertCurrency("USDT", 0.0);

    OrderBookEntry ethForBtc = makeSale(0.05, 4.0, "ETH/BTC", OrderBookType::asksale);
    w.processSale(ethForBtc); // -4 ETH, +0.2 BTC

    OrderBookEntry btcForUsdt = makeSale(60000.0, 0.2, "BTC/USDT", OrderBookType::asksale);
    w.processSale(btcForUsdt); // -0.2 BTC, +12000 USDT

    EXPECT_DOUBLE_EQ(w.getBalances().at("ETH"), 6.0);
    EXPECT_NEAR(w.getBalances().at("BTC"), 0.0, 1e-9);
    EXPECT_NEAR(w.getBalances().at("USDT"), 12000.0, 1e-6);
}

// ---- Overdraft protection ---------------------------------------------

TEST(WalletOverdraftProtection, RemoveCurrency_InsufficientBalance_FailsAndLeavesBalanceUnchanged)
{
    Wallet w;
    w.insertCurrency("BTC", 0.5);

    bool ok = w.removeCurrency("BTC", 1.0);

    EXPECT_FALSE(ok);
    EXPECT_DOUBLE_EQ(w.getBalances().at("BTC"), 0.5); // untouched, not driven negative
}

TEST(WalletOverdraftProtection, RemoveCurrency_CurrencyNeverHeld_FailsCleanly)
{
    Wallet w; // no ETH ever inserted

    bool ok = w.removeCurrency("ETH", 0.01);

    EXPECT_FALSE(ok);
    EXPECT_EQ(w.getBalances().count("ETH"), 0u); // no phantom negative entry created
}

TEST(WalletOverdraftProtection, RemoveCurrency_ExactBalance_SucceedsAndZeroesOut)
{
    Wallet w;
    w.insertCurrency("USDT", 100.0);

    bool ok = w.removeCurrency("USDT", 100.0);

    EXPECT_TRUE(ok);
    EXPECT_DOUBLE_EQ(w.getBalances().at("USDT"), 0.0);
}

TEST(WalletOverdraftProtection, CanFulfillOrder_BidExceedingQuoteBalance_ReturnsFalse)
{
    Wallet w;
    w.insertCurrency("USDT", 100.0); // only 100 USDT

    // trying to buy 1 BTC at 9000 USDT needs 9000 USDT we don't have
    OrderBookEntry bid{9000.0, 1.0, "2020-01-02 00:00:00.000", "BTC/USDT", OrderBookType::bid, "user"};

    EXPECT_FALSE(w.canFulfillOrder(bid));
}

TEST(WalletOverdraftProtection, CanFulfillOrder_AskExceedingBaseBalance_ReturnsFalse)
{
    Wallet w;
    w.insertCurrency("BTC", 0.1); // only 0.1 BTC

    // trying to sell 1 whole BTC we don't have
    OrderBookEntry ask{9000.0, 1.0, "2020-01-02 00:00:00.000", "BTC/USDT", OrderBookType::ask, "user"};

    EXPECT_FALSE(w.canFulfillOrder(ask));
}

TEST(WalletOverdraftProtection, InsertCurrency_NegativeAmount_Throws)
{
    Wallet w;
    EXPECT_THROW(w.insertCurrency("BTC", -1.0), std::exception);
}