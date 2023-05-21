### LOW-LATENCY CONNECTOR (Shared Library)

This read-to-use connector is part of multi-exchanges connectors with 
unified market data and order execution to make easier development of 
automated trading strategy and deployment on multiple exchanges/brokers.
The connectors are independent shared library in which can be loaded in
runtime and access it's functionalities via module interface.  
We aim to provide connectors with most common funtionalities 
already handled such as below:

* multi endpoint connection
* data parsing and tagging
    * price, candle, market depth, order, position
* multi-threaded processing
* order and position update
* get, create and cancel orders

##### Supported OS

* Linux, Windows

##### Available connectors (exchanges/brokers)

* Binance
* Bybit

##### BUILD Dependencies

[Linux](BUILD_LIBRARIES_LINUX.md)

[Windows](BUILD_LIBRARIES_WIN.md)

##### Build SPMAKER

* On Linux terminal
```
scl enable devtoolset-9 bash
git clone https://github.com/silvadom/spmaker.git
cd spmaker
cmake . -DG3LOG_ROOT=/usr/local/g3log -DJSON_ROOT=/usr/local/json
make install
```

* On Windows cmd
```
git clone https://github.com/stelgic/qcraftor.git
cd qcraftor
cmake . -DG3LOG_ROOT=/PATH/TO/g3log -DJSON_ROOT=/PATH/TO/json
```

With visual studio 2019 or 2022

    * Open solution qcraftor.sln 
    * set Solution Configuration to "RelWithdebugInfo"

* Others CMAKE libraries root path options:
```
-DJSON_ROOT=path_to_json
-DOPENSSL_ROOT=path_to_openssl
-DG3LOG_ROOT=path_to_g3log
-DONETBB_ROOT=path_to_onetbb
-DBOOST_ROOT=path_to_boost
-DCPR_ROOT=path_to_cpr
```

##### RUN EXAMPLE

- add binance API KEY and SECRET in configs/connector.config

```
cd connectors/build/bin
./example
```

##### Consume parsed data - example/main.cpp

```
/** ########################################################
* @brief adding thread to consume data for each data type
* ########################################################
*/
std::vector<std::thread> workers;

// Cosume parsed price data
workers.push_back(std::thread([&]()
{
    ConcurrentQueue<PriceData> priceQueue;
    connector->BindTradesQueue(&priceQueue);

    while(running)
    {
        PriceData pxData;
        if(priceQueue.try_dequeue(pxData))
        {
            LOG(INFO) << pxData;
        }
    }
}));

// Cosume parsed order data
workers.push_back(std::thread([&]()
{
    ConcurrentQueue<OrderData> orderQueue;
    connector->BindOrderQueue(&orderQueue);

    while(running)
    {
        OrderData order;
        if(orderQueue.try_dequeue(order))
        {
            LOG(INFO) << order;
        }
    }
}));
```


##### Submit or cancel orders - example/main.cpp
```c++
/** ########################################################
* @brief To create new order check code below
* ########################################################
*/

 
// fill order params
Json::Value orderParams;
orderParams["symbol"] = "BTCUSDT";
orderParams["orderType"] = "LIMIT";
orderParams["timeinforce"] = "GTC";
orderParams["side"] = "BUY";
orderParams["posSide"] = "BOTH";
orderParams["price"] = 29234.00;
orderParams["quantity"] = 0.1;

// submit new order
OrderData order = connector->NewPerpatualOrder(orderParams);

// cancel order
if(order.IsValid() && order.state != "CANCELLED")
    connector->CancelPerpetualOrder(order.instrum, order.id, order.lid);
```

#### License

* Licensed under [CC BY-NC-ND 4.0](LICENSE.md)

