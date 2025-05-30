#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <algorithm>
#include "utils.h"
#include "fl/Headers.h"

#define PROBABILITY_THRESHOLD 0.15f

namespace SystemModel
{
    using namespace fl;

    class FuzzyLoadBalancer
    {
    public:
        FuzzyLoadBalancer();
        float computeProbability(float senderLoad, float receiverLoad);

    private:
        std::shared_ptr<Engine> engine;
        InputVariable *senderLoad;
        InputVariable *receiverLoad;
        OutputVariable *transferProb;
        RuleBlock *rules;
    };

    class PhysicalNode
    {
    public:
        std::string id;
        std::atomic<float> workload;
        bool isDR;
        std::queue<float> loadQueue;
        std::mutex queueMutex;
        std::condition_variable cv;

        PhysicalNode(const std::string &id, float initialLoad = 0.0f);
        ~PhysicalNode();
        std::thread autoDequeueThread;
        std::atomic<bool> stopAutoDequeue_{false};

        void startAutoDequeue(float timeoutSeconds = 5.0f);
        void stopAutoDequeue();
        void addWorkload(float val);
        void subWorkload(float val);
        float getWorkload() const;
        void setWorkload(float val);
        void display(int indent = 0) const;
        void enqueueLoad(float load);
        void enqueueLoads(const std::vector<float> &loads);
        void dequeueLoad();
        bool hasLoads();
        void clearQueue();
        bool canJoinNewGroup();
    };

    class LogicalNode;
    class LogicalGroup;

    class PhysicalGroup
    {
    public:
        std::string groupId;
        std::vector<std::shared_ptr<PhysicalNode>> nodes;
        std::shared_ptr<PhysicalNode> designatedRep;

        std::thread transferThread;
        std::atomic<bool> stopTransfer{false};
        std::mutex transferMutex;
        std::condition_variable transferCv;
        FuzzyLoadBalancer fuzzyBalancer;
        LogicalGroup *parentLogicalGroup = nullptr;

        PhysicalGroup(const std::string &groupId);
        virtual ~PhysicalGroup();

        void addNode(std::shared_ptr<PhysicalNode> node);
        float getTotalWorkload() const;
        void removeNode(const std::string &nodeId);
        void selectDR();
        virtual void display(int indent = 0) const;
        void addNodeWithLoad(std::shared_ptr<PhysicalNode> node, const std::vector<float> &loads);
        void joinNewPhysicalNode(std::shared_ptr<PhysicalNode> node, const std::vector<float> &loads);
        bool handleLeaveRequest(std::shared_ptr<PhysicalNode> &leavingNode, int maxWaitTimeSeconds = 4);
        bool fuzzyMulticastTransfer(std::shared_ptr<PhysicalNode> sender, float load);

        std::thread monitorThread;
        std::atomic<bool> stopMonitor{false};
        float workloadThreshold = 70.0f;

        void startMonitoring();
        void stopMonitoring();
    };

    class LogicalNode : public PhysicalGroup
    {
    public:
        std::string logicalId;
        std::shared_ptr<PhysicalGroup> physicalGroup;

        LogicalNode(const std::string &lid, const std::shared_ptr<PhysicalGroup> &pg);

        void selectDR() = delete;
        void addNode() = delete;
        void display(int indent = 0) const override;
    };

    class LogicalGroup
    {
    public:
        std::string id;
        std::vector<std::shared_ptr<LogicalNode>> logicalNodes;
        size_t roundRobinIndex = 0;

        LogicalGroup(const std::string &id);

        void addLogicalNode(std::shared_ptr<LogicalNode> ln);
        bool handleLeaveRequest(std::shared_ptr<LogicalNode> leavingNode);
        void display(int indent = 0) const;
        void joinNewPhysicalNode(std::shared_ptr<PhysicalNode> node, const std::vector<float> &loads);

        void startMonitoringAll();
        void addRandomWorkload();
        void stopMonitoringAll();
        void logMonitor();
    };
}