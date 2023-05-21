#include <filesystem>
#include <boost/program_options.hpp>
#include <boost/exception/diagnostic_information.hpp>

#include "public/IExchange.h"
#include "public/ModuleLoader.h"
#include "public/utils/LogHelper.h"
#include "public/utils/JsonHelper.h"

using namespace stelgic;
namespace fs = std::filesystem;
namespace po = boost::program_options;

std::string exchange;
int verbosity = 1;
int NUM_THREADS = 8;
IExchange* connector = nullptr;
std::atomic_bool running = {1};

// close connection on crtl + c
void Terminate()
{
    if(connector)
        connector->Stop();

    ::internal::shutDownLogging();
}

bool getCommandLineArgs(int argc, char** argv)
{
    // parse command line argument and store 
    // key value pair into map
    po::options_description desc("example commandline options");

    desc.add_options()
    ("help,h", "produce help message")
    ("exchange,e", po::value<std::string>(&exchange), "connector/exchange name")
    ("threads,t", po::value<int>(&NUM_THREADS)->default_value(8), "num threads to process data")
    ("verbose,v", po::value<int>(&verbosity)->default_value(1), "Logging verbosity level (0,1,2)");

    // Parse command line arguments
    bool success = false;
    try
    {
        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);
        po::notify(vm);
        success = true;
    }
    catch(boost::exception const& e)
    {
        std::string msg = boost::diagnostic_information_what(e);
        LOG(WARNING) << msg;
    }

    return success;
}


int main(int argc, char** argv)
{
    InitializeLogger();

    LOG(INFO) << "STARTING...";

    atexit(Terminate);
    // Stop processing on SIGINT
    std::signal(SIGINT, [](int) 
    {
        running = {0};
        LOG(INFO) << "Program interrupted...";
        _Exit(EXIT_FAILURE);
    });

    if(!getCommandLineArgs(argc, argv))
    {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        _Exit(EXIT_FAILURE);
    }

    /** ########################################################
     * @brief load shared library connector
     * ########################################################
     */

    bool success = false;
    std::string err;
    std::string filename("../modules/");
#if defined(_WIN32) || defined(_WIN64)
    filename.append(exchange).append(".dll");
#elif defined(__linux__)
    filename.append("lib").append(exchange).append(".so");
#endif

    if(!fs::exists(filename))
    {
        LOG(WARNING) << "Failed to load "<< filename;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        _Exit(EXIT_FAILURE);
    }

    std::string MODULE_PATH = fs::canonical(filename).string();
    std::string CONFIG_PATH = fs::canonical(std::string("../configs/connector.config")).string();

    ModuleLoader<IExchange> module(MODULE_PATH);
    LOG(INFO) << "LODING " << MODULE_PATH << " ...";
    std::tie(success, err) = module.Open();
    if(!success)
    {
        LOG(WARNING) << "Failed to load "<< MODULE_PATH << "! " << err;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        _Exit(EXIT_FAILURE);
    }

    LOG(INFO) << "LOADED " << MODULE_PATH << " successful!";

    // create instance of connector
    IExchange* connector = module.GetInstance();
    if(connector == nullptr)
    {
        LOG(WARNING) << "Failed to create instance of connector " << module.GetName();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        _Exit(EXIT_FAILURE);
    }

    /** ########################################################
     * @brief initialize and connector with configs
     * ########################################################
     */
    // Load exchange json configure file
    Json::Value connParams = LoadJsonFromFile(CONFIG_PATH, err);
    if(connParams.isNull())
    {
        LOG(INFO) << "Failed to load " << CONFIG_PATH;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        _Exit(EXIT_FAILURE);
    }

    // Initiliaze connector
    LOG(INFO) << "Initializing...";
    connector->Init(connParams[exchange], verbosity, logworker.get());
    if(!connector->IsInitialized())
    {
        LOG(INFO) << "Failed to Initialize connector";
        std::this_thread::sleep_for(std::chrono::seconds(2));
        _Exit(EXIT_FAILURE);
    } 


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

    // Cosume json ticker data
    workers.push_back(std::thread([&]()
    {
        ConcurrentQueue<TickerData> tickerQueue;
        connector->BindTickerQueue(&tickerQueue);
        while(running)
        {
            TickerData ticker;
            if(tickerQueue.try_dequeue(ticker))
            {
                LOG(INFO) << ticker;
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
                LOG(INFO) << order.toJson();
            }
        }
    }));

    // Cosume parsed position data
    workers.push_back(std::thread([&]()
    {
        ConcurrentQueue<PositionData> positionQueue;
        connector->BindPositionQueue(&positionQueue);

        while(running)
        {
            PositionData position;
            if(positionQueue.try_dequeue(position))
            {
                LOG(INFO) << position.toJson();
            }
        }
    }));

    /** ########################################################
     * @brief Connect and subscribe to websocket channels
     * ########################################################
     */

    // start connector threads
    workers.push_back(connector->KeepAlive());

    // connect and subscribe to exchange 
    ConnState state = connector->Connect(connParams[exchange]);
    if(state != ConnState::Opened)
    {
        LOG(WARNING) << "Failed to connect exchange " << exchange;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        _Exit(EXIT_FAILURE);
    }

    // test connectivity delay using ping/pong
    connector->TestConnectivity();
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // subscribe to websocket channels
    connector->Subscribe(connParams[exchange]);

    /** ########################################################
     * @brief To create new order check uncomment code below
     * ########################################################
     */
    /*
    // wait few seconds before send first order (not required)
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    // fill order params
    Json::Value openOrder;
    openOrder["instrum"] = "BTCUSDT";
    openOrder["orderType"] = "LIMIT";
    openOrder["timeinforce"] = "GTC";
    openOrder["side"] = "BUY";
    openOrder["posSide"] = "BOTH";
    openOrder["postOnly"] = true;
    openOrder["price"] = 29123.00;
    openOrder["quantity"] = 0.1;
    closeOrder["clOrderId"] = order.id;

    // submit new order
    OrderData order = connector->NewPerpetualOrder(orderParams);

    // cancel order
    if(order.IsValid() && order.state != "CANCELLED")
        connector->CancelFutureOrder(order.instrum, order.id, order.lid);
    */

    for(auto& task: workers)
        task.join();

#if defined(_WIN32) || defined(_WIN64)
    std::cin.get();
#endif
    return EXIT_SUCCESS;
}
