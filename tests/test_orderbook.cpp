#include <gtest/gtest.h>
#include "OrderBook.h"
#include "Database.h"

namespace
{
    // Fresh in-memory DB + a tiny fixture CSV per test, so tests can't leak
    // state into each other via a shared file on disk.
    std::unique_ptr<Database> makeDb()
    {
        return std::make_unique<Database>(":memory:");
    }

    OrderBookEntry makeOrder(double price, double amount, OrderBookType type,
                              const std::string& product, const std::string& username)
    {
        return OrderBookEntry{price, amount, "2020-01-02 00:00:00.000", product, type, username};
    }
}

class OrderBookMatchingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        db = makeDb();
        book = std::make_unique<OrderBook>(std::string(FIXTURE_DIR) + "/tiny_orderbook.csv", *db);
    }

    std::unique_ptr<Database> db;
    std::unique_ptr<OrderBook> book;
};

// A resting ask larger than the incoming bid should only be partially
// consumed, and the remainder should keep resting in the book.
TEST_F(OrderBookMatchingTest, IncomingOrderSmallerThanRestingOrder_PartiallyFillsRestingOrder)
{
    OrderBookEntry restingAsk = makeOrder(100.0, 5.0, OrderBookType::ask, "ETH/USDT", "alice");
    book->insertOrder(restingAsk);

    OrderBookEntry incomingBid = makeOrder(100.0, 2.0, OrderBookType::bid, "ETH/USDT", "bob");
    std::vector<OrderBookEntry> sales = book->matchNewOrder(incomingBid);

    ASSERT_EQ(sales.size(), 2u); // one leg for bob, one leg for alice
    EXPECT_DOUBLE_EQ(incomingBid.amount, 0.0); // bob's order fully filled

    std::vector<OrderBookEntry> resting = book->getOrdersByProduct(OrderBookType::ask, "ETH/USDT");
    ASSERT_EQ(resting.size(), 1u);
    EXPECT_DOUBLE_EQ(resting[0].amount, 3.0); // 5 - 2 remaining for alice
}

// An incoming order bigger than the resting liquidity should fully consume
// the resting order and leave the remainder of the incoming order unfilled
// (it's the caller's job to decide whether that remainder rests or is dropped).
TEST_F(OrderBookMatchingTest, IncomingOrderLargerThanRestingOrder_FullyConsumesRestingOrder)
{
    OrderBookEntry restingAsk = makeOrder(100.0, 2.0, OrderBookType::ask, "ETH/USDT", "alice");
    book->insertOrder(restingAsk);

    OrderBookEntry incomingBid = makeOrder(100.0, 5.0, OrderBookType::bid, "ETH/USDT", "bob");
    std::vector<OrderBookEntry> sales = book->matchNewOrder(incomingBid);

    ASSERT_EQ(sales.size(), 2u);
    EXPECT_DOUBLE_EQ(incomingBid.amount, 3.0); // 5 - 2 filled, 3 left over

    std::vector<OrderBookEntry> resting = book->getOrdersByProduct(OrderBookType::ask, "ETH/USDT");
    EXPECT_EQ(resting.size(), 0u); // alice's ask fully consumed and removed
}

// A single incoming order that crosses multiple price levels should walk the
// book best-price-first, partially or fully filling each level in turn.
TEST_F(OrderBookMatchingTest, IncomingOrderCrossesMultiplePriceLevels_FillsBestPriceFirst)
{
    OrderBookEntry cheapAsk = makeOrder(100.0, 1.0, OrderBookType::ask, "ETH/USDT", "alice");
    OrderBookEntry pricierAsk = makeOrder(101.0, 1.0, OrderBookType::ask, "ETH/USDT", "carol");
    book->insertOrder(cheapAsk);
    book->insertOrder(pricierAsk);

    OrderBookEntry incomingBid = makeOrder(101.0, 2.0, OrderBookType::bid, "ETH/USDT", "bob");
    std::vector<OrderBookEntry> sales = book->matchNewOrder(incomingBid);

    ASSERT_EQ(sales.size(), 4u); // 2 fills x 2 legs each
    EXPECT_DOUBLE_EQ(incomingBid.amount, 0.0);

    // both resting asks should be fully consumed
    EXPECT_EQ(book->getOrdersByProduct(OrderBookType::ask, "ETH/USDT").size(), 0u);

    // the cheaper level should have been filled at its own price (100), not bob's limit
    bool sawFillAtHundred = false;
    for (const OrderBookEntry& s : sales)
    {
        if (s.price == 100.0) sawFillAtHundred = true;
    }
    EXPECT_TRUE(sawFillAtHundred);
}

// Historical CSV rows are attributed to the synthetic "dataset" user and have
// no real wallet to settle against, so matching against one should not
// produce a sale leg on the dataset side.
TEST_F(OrderBookMatchingTest, MatchingAgainstHistoricalDatasetRow_ProducesNoSaleForDatasetSide)
{
    OrderBookEntry historicalAsk = makeOrder(100.0, 5.0, OrderBookType::ask, "ETH/USDT", "dataset");
    book->insertOrder(historicalAsk);

    OrderBookEntry incomingBid = makeOrder(100.0, 2.0, OrderBookType::bid, "ETH/USDT", "bob");
    std::vector<OrderBookEntry> sales = book->matchNewOrder(incomingBid);

    ASSERT_EQ(sales.size(), 1u); // only bob's leg, not "dataset"'s
    EXPECT_EQ(sales[0].username, "bob");
}

// No crossing counterparty at all: the order should come back untouched.
TEST_F(OrderBookMatchingTest, NoOppositeSideLiquidity_LeavesIncomingOrderUnfilled)
{
    OrderBookEntry incomingBid = makeOrder(100.0, 2.0, OrderBookType::bid, "SOL/USDT", "bob");
    std::vector<OrderBookEntry> sales = book->matchNewOrder(incomingBid);

    EXPECT_TRUE(sales.empty());
    EXPECT_DOUBLE_EQ(incomingBid.amount, 2.0);
}