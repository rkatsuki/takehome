Concurrency and Locking Strategy

This currently implements a Sharded Locking strategy designed to maximize throughput by separating global "routing" lookups from symbol-specific "matching" logic.

However, we can further refine the lock granularity to ensure that "Read" operations (like checking if a book exists or getting a snapshot) don't block "Write" operations (like placing an order) on other symbols.

1. The Multi-Level Locking Strategy
We are using a "Read-Write" lock pattern (via std::shared_mutex) at the Engine level and "Exclusive" locks at the OrderBook level.

Component,Mutex Type,Lock Used,Purpose
Bookshelf Map,std::shared_mutex,shared_lock,Allows multiple threads to simultaneously find the BTC book without blocking each other.
Bookshelf Map,std::shared_mutex,unique_lock,"Only blocks everything when a new symbol (e.g., first SOL order) is being initialized."
OrderBook,std::mutex,lock_guard,Protects the matching logic for a specific symbol. BTC matching never blocks ETH matching.
ID-to-Symbol Map,std::shared_mutex,shared_lock,Allows multiple threads to resolve Order IDs to Symbols for cancellation.