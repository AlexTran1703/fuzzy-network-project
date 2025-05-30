#include <fl/Headers.h>
#include "system_model.h"
#include <random>
#include <thread>
#include <atomic>
#include <boost/asio.hpp>
#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <iostream>

using namespace SystemModel;

int main()
{
    srand(static_cast<unsigned>(time(nullptr)));

    // Create a logical group
    auto logicalGroup = std::make_shared<LogicalGroup>("LG1");

    // Create multiple physical groups (4-6 nodes each)
    std::vector<std::shared_ptr<PhysicalGroup>> physicalGroups;
    std::vector<std::shared_ptr<LogicalNode>> logicalNodes;

    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> nodeDist(4, 6);
    std::uniform_real_distribution<float> loadDist(10.0f, 20.0f);

    for (int i = 0; i < 3; ++i)
    {
        std::string groupId = "PG" + std::to_string(i + 1);
        auto pg = std::make_shared<PhysicalGroup>(groupId);

        int numNodes = nodeDist(rng);
        for (int j = 0; j < numNodes; ++j)
        {
            std::string nodeId = groupId + "_N" + std::to_string(j + 1);
            float initialLoad = loadDist(rng);
            auto node = std::make_shared<PhysicalNode>(nodeId, initialLoad);
            node->startAutoDequeue(20.0f); // Free one load every 20 seconds
            pg->addNode(node);
        }
        physicalGroups.push_back(pg);

        std::string logicalNodeId = "LN" + std::to_string(i + 1);
        auto ln = std::make_shared<LogicalNode>(logicalNodeId, pg);
        logicalNodes.push_back(ln);
        logicalGroup->addLogicalNode(ln);

        pg->parentLogicalGroup = logicalGroup.get();
    }

    // Start monitoring all physical groups via logical group
    logicalGroup->startMonitoringAll();

    // --- Boost.Asio thread pool setup ---
    boost::asio::io_context io_context;
    auto work_guard = boost::asio::make_work_guard(io_context); // Keeps io_context alive
    std::vector<std::thread> thread_pool;
    const std::size_t pool_size = std::thread::hardware_concurrency();
    std::cout << "Starting thread pool with " << pool_size << " threads.\n";
    for (std::size_t i = 0; i < pool_size; ++i)
        thread_pool.emplace_back([&io_context]() { io_context.run(); });

    // Detached thread to periodically post workload jobs
    std::atomic<bool> stopRandom{false};
    std::thread([&]() {
        while (!stopRandom) {
            boost::asio::post(io_context, [&]() {
                logicalGroup->addRandomWorkload();
            });
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }).detach();

    // Let the system run for a while to observe transfers
    std::this_thread::sleep_for(std::chrono::seconds(240));

    // Stop random workload thread and monitoring
    stopRandom = true;

    // Release the work guard so io_context can exit
    work_guard.reset();
    io_context.stop();
    for (auto &t : thread_pool)
        if (t.joinable())
            t.join();

    // Stop monitoring in all groups
    for (auto &pg : physicalGroups)
    {
        pg->stopMonitoring();
    }

    // Display final state
    logicalGroup->display();
    return 0;
}