#include "system_model.h"
#include <random>
#include "utils.h"
namespace SystemModel
{
    // --- FuzzyLoadBalancer ---
    FuzzyLoadBalancer::FuzzyLoadBalancer()
    {
        engine = std::make_shared<Engine>();
        engine->setName("LoadBalancingFLC");

        senderLoad = new InputVariable;
        senderLoad->setName("SenderLoad");
        senderLoad->setRange(0.0, 100.0);
        senderLoad->addTerm(new Trapezoid("VL", 0.0, 0.0, 10.0, 15.0));
        senderLoad->addTerm(new Triangle("L", 10.0, 25.0, 40.0));
        senderLoad->addTerm(new Triangle("M", 30.0, 50.0, 70.0));
        senderLoad->addTerm(new Triangle("H", 60.0, 75.0, 90.0));
        senderLoad->addTerm(new Trapezoid("VH", 80.0, 90.0, 100.0, 100.0));
        engine->addInputVariable(senderLoad);


        receiverLoad = new InputVariable;
        receiverLoad->setName("ReceiverLoad");
        receiverLoad->setRange(0.0, 100.0);
        receiverLoad->addTerm(new Trapezoid("VL", 0.0, 0.0, 10.0, 15.0));
        receiverLoad->addTerm(new Triangle("L", 10.0, 25.0, 40.0));
        receiverLoad->addTerm(new Triangle("M", 30.0, 50.0, 70.0));
        receiverLoad->addTerm(new Triangle("H", 60.0, 75.0, 90.0));
        receiverLoad->addTerm(new Trapezoid("VH", 80.0, 90.0, 100.0, 100.0));
        engine->addInputVariable(receiverLoad);


        transferProb = new OutputVariable;
        transferProb->setName("TransferProbability");
        transferProb->setRange(0.0, 1.0);
        transferProb->setDefaultValue(fl::nan);
        transferProb->setDefuzzifier(new Centroid(100));
        transferProb->setAggregation(new Maximum);
        // VL and VH changed to Trapezoid
        transferProb->addTerm(new Trapezoid("VL", 0.0, 0.0, 0.15, 0.2));
        transferProb->addTerm(new Triangle("L", 0.1, 0.25, 0.4));
        transferProb->addTerm(new Triangle("M", 0.3, 0.5, 0.7));
        transferProb->addTerm(new Triangle("H", 0.6, 0.75, 0.9));
        transferProb->addTerm(new Trapezoid("VH", 0.8, 0.9, 1.0, 1.0));
        engine->addOutputVariable(transferProb);


        auto *rules = new RuleBlock;
        rules->setName("Rules");
        rules->setEnabled(true);
        rules->setConjunction(new Minimum);
        rules->setDisjunction(new Maximum);
        rules->setImplication(new Minimum);
        rules->setActivation(new General);

        const std::vector<std::string> levels = {"VL", "L", "M", "H", "VH"};
        const std::string table[5][5] = {
            {"VL", "L", "M", "H", "VH"},
            {"VL", "VL", "L", "M", "H"},
            {"VL", "VL", "VL", "L", "M"},
            {"VL", "VL", "VL", "VL", "L"},
            {"VL", "VL", "VL", "VL", "VL"}};

        for (int i = 0; i < 5; ++i)
        {
            for (int j = 0; j < 5; ++j)
            {
                std::string rule = "if SenderLoad is " + levels[j] + " and ReceiverLoad is " + levels[i] +
                                   " then TransferProbability is " + table[i][j];
                rules->addRule(Rule::parse(rule, engine.get()));
            }
        }

        engine->addRuleBlock(rules);
    }

    float FuzzyLoadBalancer::computeProbability(float senderLoad, float receiverLoad)
    {
        engine->getInputVariable("SenderLoad")->setValue(senderLoad);
        engine->getInputVariable("ReceiverLoad")->setValue(receiverLoad);
        engine->process();
        return engine->getOutputVariable("TransferProbability")->getValue();
    }

    // --- PhysicalNode ---
    PhysicalNode::PhysicalNode(const std::string &id, float initialLoad)
        : id(id), workload(0.0f), isDR(false)
    {
        if (initialLoad > 0.0f)
        {
            addWorkload(initialLoad);
        }
    }

    void PhysicalNode::addWorkload(float val)
    {
        if (isDR)
            return;
        float old, desired;
        do
        {
            old = workload.load();
            desired = std::min(old + val, 100.0f);
        } while (!workload.compare_exchange_weak(old, desired));
        enqueueLoad(val);
    }

    void PhysicalNode::subWorkload(float val)
    {
        if (isDR)
            return;
        float old, desired;
        do
        {
            old = workload.load();
            desired = old - val;
        } while (!workload.compare_exchange_weak(old, desired));
    }

    float PhysicalNode::getWorkload() const
    {
        return workload.load();
    }

    void PhysicalNode::setWorkload(float val)
    {
        if (isDR)
            return;
        workload.store(val);
    }

    void PhysicalNode::startAutoDequeue(float timeoutSeconds)
    {
        stopAutoDequeue_ = false;
        autoDequeueThread = std::thread([this, timeoutSeconds]()
                                        {
        while (!stopAutoDequeue_) {
            std::this_thread::sleep_for(std::chrono::duration<float>(timeoutSeconds));
            if (stopAutoDequeue_) break;
            dequeueLoad(); // Remove one load from the queue and update workload
            LOG_INFO("[AUTO-DEQUEUE] Node " << id << " performed auto dequeue. Current workload: " << getWorkload());
        } });
    }

    void PhysicalNode::stopAutoDequeue()
    {
        stopAutoDequeue_ = true;
        if (autoDequeueThread.joinable())
            autoDequeueThread.join();
    }

    // In the PhysicalNode destructor, ensure the thread is stopped:
    PhysicalNode::~PhysicalNode()
    {
        stopAutoDequeue();
    }

    void PhysicalNode::display(int indent) const
    {
        std::string prefix(indent, ' ');
        std::cout << prefix << "Node ID: " << id
                  << ", Workload: " << getWorkload()
                  << ", isDR: " << (isDR ? "Yes" : "No")
                  << ", Queue size: " << loadQueue.size() << "\n";
    }

    void PhysicalNode::enqueueLoad(float load)
    {
        if (isDR)
            return; // Designated Replicas do not accept new loads
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            loadQueue.push(load);
        }
        cv.notify_one();
    }

    void PhysicalNode::enqueueLoads(const std::vector<float> &loads)
    {
        for (const float &load : loads)
        {
            addWorkload(load);
            display();
        }
    }

    void PhysicalNode::dequeueLoad()
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (!loadQueue.empty())
        {
            subWorkload(loadQueue.front());
            loadQueue.pop();
        }
    }

    bool PhysicalNode::hasLoads()
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        return !loadQueue.empty();
    }

    void PhysicalNode::clearQueue()
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        while (!loadQueue.empty())
            loadQueue.pop();
        setWorkload(0.0f);
    }

    bool PhysicalNode::canJoinNewGroup()
    {
        return getWorkload() == 0.0f;
    }

    // --- PhysicalGroup ---
    PhysicalGroup::PhysicalGroup(const std::string &groupId) : groupId(groupId) {}

    PhysicalGroup::~PhysicalGroup()
    {
        stopTransfer = true;
        transferCv.notify_all();
        stopMonitoring();
        if (transferThread.joinable())
            transferThread.join();
    }

    void PhysicalGroup::addNode(std::shared_ptr<PhysicalNode> node)
    {
        nodes.push_back(node);
        selectDR();
        LOG_INFO("Node " << node->id << " joined " << groupId);
    }

    float PhysicalGroup::getTotalWorkload() const
    {
        float total = 0.0f;
        for (const auto &node : nodes)
        {
            total += node->getWorkload();
        }
        return total;
    }

    void PhysicalGroup::removeNode(const std::string &nodeId)
    {
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                                   [&](const std::shared_ptr<PhysicalNode> &node)
                                   {
                                       return node->id == nodeId;
                                   }),
                    nodes.end());
        selectDR();
    }

    void PhysicalGroup::selectDR()
    {
        float minWorkload = std::numeric_limits<float>::max();
        std::shared_ptr<PhysicalNode> newDR = nullptr;

        for (auto &node : nodes)
        {
            node->isDR = false;
            float w = node->getWorkload();
            if (w < minWorkload)
            {
                minWorkload = w;
                newDR = node;
            }
        }

        if (newDR)
        {
            newDR->isDR = true;
            designatedRep = newDR;
            LOG_INFO("New DR selected for " << groupId << ": " << newDR->id);
        }
        else
        {
            designatedRep = nullptr;
        }
    }

    void PhysicalGroup::display(int indent) const
    {
        std::string prefix(indent, ' ');
        std::cout << prefix << "PhysicalGroup (LogicalNode) ID: " << groupId << "\n";
        for (const auto &node : nodes)
        {
            node->display(indent + 4);
        }
    }

    void PhysicalGroup::addNodeWithLoad(std::shared_ptr<PhysicalNode> node, const std::vector<float> &loads)
    {
        node->enqueueLoads(loads);
        addNode(node);
    }

    void PhysicalGroup::joinNewPhysicalNode(std::shared_ptr<PhysicalNode> node, const std::vector<float> &loads)
    {
        node->display();
        if (node->canJoinNewGroup())
        {
            addNodeWithLoad(node, loads);
            LOG_INFO("[PhysicalGroup] Node " << node->id << " joined PhysicalGroup " << groupId);
        }
        else
        {
            LOG_WARN("[PhysicalGroup] Node " << node->id << " not eligible to join");
        }
    }

    bool PhysicalGroup::handleLeaveRequest(std::shared_ptr<PhysicalNode> &leavingNode, int maxWaitTimeSeconds)
    {
        LOG_INFO("Node " << leavingNode->id << " requests to leave " << groupId);

        if (!designatedRep || designatedRep == leavingNode)
        {
            LOG_ERROR("No valid DR to handle leave or leaving node is DR.");
            return false;
        }

        std::vector<std::shared_ptr<PhysicalNode>> receivers;
        for (auto &node : nodes)
        {
            if (node != leavingNode)
                receivers.push_back(node);
        }
        if (receivers.empty())
        {
            LOG_WARN("No other nodes available to receive load.");
            return false;
        }

        stopTransfer = false;

        transferThread = std::thread([&, receivers, leavingNode]()
                                     {
            size_t receiverIndex = 0;
            while (!stopTransfer) {
                std::unique_lock<std::mutex> lock(leavingNode->queueMutex);

                leavingNode->cv.wait(lock, [&]() { return stopTransfer || !leavingNode->loadQueue.empty(); });

                if (stopTransfer) break;
                if (leavingNode->loadQueue.empty()) break;

                float loadJob = leavingNode->loadQueue.front();
                leavingNode->loadQueue.pop();

                lock.unlock();
                leavingNode->subWorkload(loadJob);

                std::this_thread::sleep_for(std::chrono::milliseconds(500));

                auto receiver = receivers[receiverIndex % receivers.size()];
                receiver->addWorkload(loadJob);
                LOG_INFO("Transferred load " << loadJob << " from " << leavingNode->id << " to " << receiver->id);

                receiverIndex++;
            }

            LOG_INFO("Load transfer thread ended for " << leavingNode->id); });

        {
            std::unique_lock<std::mutex> lock(leavingNode->queueMutex);
            bool completed = leavingNode->cv.wait_for(lock, std::chrono::seconds(maxWaitTimeSeconds), [&]()
                                                      { return leavingNode->loadQueue.empty(); });

            if (!completed)
            {
                size_t remaining = leavingNode->loadQueue.size();
                LOG_WARN("Timeout! Dropped " << remaining << " remaining load jobs of " << leavingNode->id);
            }
        }

        stopTransfer = true;
        leavingNode->cv.notify_all();
        if (transferThread.joinable())
            transferThread.join();
        leavingNode->clearQueue();
        removeNode(leavingNode->id);
        LOG_INFO("Node " << leavingNode->id << " has left group " << groupId);
        return true;
    }

    bool PhysicalGroup::fuzzyMulticastTransfer(std::shared_ptr<PhysicalNode> sender, float load)
    {
        // 1. Multicast to all nodes in group except sender
        std::vector<std::shared_ptr<PhysicalNode>> candidates;
        for (auto &node : nodes)
        {
            if (node != sender && !node->isDR && !sender->isDR)
                candidates.push_back(node);
        }
        if (candidates.empty())
            return false;

        // 2. Find least loaded node
        auto receiver = *std::min_element(candidates.begin(), candidates.end(),
                                          [](const auto &a, const auto &b)
                                          {
                                              return a->getWorkload() < b->getWorkload();
                                          });

        float senderLoad = sender->getWorkload();
        float receiverLoad = receiver->getWorkload();

        // 3. Fuzzy logic: compute probability
        float P = fuzzyBalancer.computeProbability(senderLoad, receiverLoad);
        LOG_INFO("Fuzzy transfer: Sender " << sender->id << " (Load=" << senderLoad
                                           << "), Receiver " << receiver->id << " (Load=" << receiverLoad
                                           << "), P=" << P);
        // 4. If P is high enough, transfer M loads
        bool escalate_flag = true;

        if (P > PROBABILITY_THRESHOLD)
        { // threshold can be tuned
            escalate_flag = false;
            float M = P * (senderLoad - receiverLoad) / 2.0f;
            if (M > 0.0f)
            {
                sender->subWorkload(M);
                receiver->addWorkload(M);
                LOG_INFO("[LOGICAL] Fuzzy transfer: " << M << " from " << sender->id << " to " << receiver->id << " (P=" << P << ")");
                return true;
            }
        }
        if (designatedRep && designatedRep != sender)
        {
            LOG_INFO("Escalating to DR " << designatedRep->id);
            // You can implement cross-group transfer here, e.g.:
            // designatedRep->group->fuzzyMulticastTransfer(sender, load);
        }
        if (escalate_flag && designatedRep && parentLogicalGroup)
        {
            // Escalate: find another PhysicalGroup and apply fuzzy logic to all its nodes
            for (auto &ln : parentLogicalGroup->logicalNodes)
            {
                auto otherGroup = ln->physicalGroup;
                if (otherGroup.get() != this && !otherGroup->nodes.empty())
                {
                    std::shared_ptr<PhysicalNode> bestReceiver = nullptr;
                    float bestP = 0.0f;
                    float minWorkload = std::numeric_limits<float>::max();
                    float senderLoad = sender->getWorkload();

                    for (auto &candidate : otherGroup->nodes)
                    {
                        float receiverLoad = candidate->getWorkload();
                        float P = fuzzyBalancer.computeProbability(senderLoad, receiverLoad);
                        if (P > PROBABILITY_THRESHOLD && receiverLoad < minWorkload)
                        {
                            minWorkload = receiverLoad;
                            bestP = P;
                            bestReceiver = candidate;
                        }
                    }

                    if (bestReceiver)
                    {
                        float M = bestP * (senderLoad - minWorkload) / 2.0f;
                        if (M > 0.0f)
                        {
                            sender->subWorkload(M);
                            bestReceiver->addWorkload(M);
                            LOG_INFO("[CROSS-LOGICAL] Escalated fuzzy transfer: " << M << " from " << sender->id
                                                                                  << " (LogicalNode: " << groupId << ") to " << bestReceiver->id
                                                                                  << " (LogicalNode: " << otherGroup->groupId << ") (P=" << bestP << ")");
                            return true;
                        }
                    }
                }
            }
            LOG_WARN("No suitable node found for escalation to another logical node.");
            return false;
        }
        return false;
    }

    void PhysicalGroup::startMonitoring()
    {
        stopMonitor = false;
        monitorThread = std::thread([this]()
                                    {
            while (!stopMonitor) {
                for (auto& node : nodes) {
                    float wl = node->getWorkload();
                    if (wl > workloadThreshold) {
                        LOG_INFO("Node " << node->id << " workload " << wl << " exceeds threshold " << workloadThreshold);
                        fuzzyMulticastTransfer(node, wl);
                    }
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
                display();
            } });
    }

    void PhysicalGroup::stopMonitoring()
    {
        stopMonitor = true;
        if (monitorThread.joinable())
            monitorThread.join();
    }

    // --- LogicalNode ---
    LogicalNode::LogicalNode(const std::string &lid, const std::shared_ptr<PhysicalGroup> &pg)
        : PhysicalGroup(pg->groupId), logicalId(lid), physicalGroup(pg) {}

    void LogicalNode::display(int indent) const
    {
        std::string prefix(indent, ' ');
        std::cout << prefix << "LogicalNode ID: " << logicalId << " (PhysicalGroup ID: " << groupId << ")\n";
        if (physicalGroup)
        {
            for (const auto &node : physicalGroup->nodes)
            {
                node->display(indent + 4);
            }
        }
    }

    // --- LogicalGroup ---
    LogicalGroup::LogicalGroup(const std::string &id) : id(id) {}

    void LogicalGroup::addLogicalNode(std::shared_ptr<LogicalNode> ln)
    {
        logicalNodes.push_back(ln);
        LOG_INFO("[LogicalGroup] Added LogicalNode " << ln->groupId);
    }

    bool LogicalGroup::handleLeaveRequest(std::shared_ptr<LogicalNode> leavingNode)
    {
        LOG_INFO("[LogicalGroup] LogicalNode " << leavingNode->groupId << " is leaving");

        std::vector<std::shared_ptr<LogicalNode>> candidates;
        for (auto &ln : logicalNodes)
        {
            if (ln != leavingNode)
                candidates.push_back(ln);
        }

        if (candidates.empty())
        {
            LOG_WARN("[LogicalGroup] No other LogicalNodes available");
            return false;
        }

        size_t index = 0;
        for (auto &pnode : leavingNode->nodes)
        {
            auto receiverLN = candidates[index % candidates.size()];
            receiverLN->handleLeaveRequest(pnode);
            index++;
        }

        logicalNodes.erase(std::remove(logicalNodes.begin(), logicalNodes.end(), leavingNode), logicalNodes.end());
        LOG_INFO("[LogicalGroup] LogicalNode " << leavingNode->groupId << " removed");
        return true;
    }

    void LogicalGroup::display(int indent) const
    {
        std::string prefix(indent, ' ');
        std::cout << prefix << "LogicalGroup ID: " << id << "\n";
        for (const auto &logicalNode : logicalNodes)
        {
            logicalNode->display(indent + 4);
        }
    }

    void LogicalGroup::joinNewPhysicalNode(std::shared_ptr<PhysicalNode> node, const std::vector<float> &loads)
    {
        if (logicalNodes.empty())
        {
            LOG_WARN("[LogicalGroup] No LogicalNodes available for joining");
            return;
        }

        size_t startIndex = roundRobinIndex;
        for (size_t i = 0; i < logicalNodes.size(); ++i)
        {
            auto &ln = logicalNodes[roundRobinIndex];
            roundRobinIndex = (roundRobinIndex + 1) % logicalNodes.size();
            if (node->canJoinNewGroup())
            {
                ln->addNodeWithLoad(node, loads);
                LOG_INFO("[LogicalGroup] Node " << node->id << " joined LogicalNode " << ln->groupId);
                return;
            }
        }
        LOG_WARN("[LogicalGroup] No suitable LogicalNode found for " << node->id);
    }
    void LogicalGroup::startMonitoringAll()
    {
        for (auto &ln : logicalNodes)
        {
            if (ln->physicalGroup)
                ln->physicalGroup->startMonitoring();
        }
        logMonitor();
    }
    void LogicalGroup::logMonitor()
    {
        std::thread([this]()
                    {
        while (true) {
            Utils::log_state_json(Utils::to_json(*this));
            std::this_thread::sleep_for(std::chrono::seconds(1)); // log every 5 seconds
        } })
            .detach();
    }

    void LogicalGroup::addRandomWorkload()
    {
        if (logicalNodes.empty())
            return;

        // Use modern C++ random facilities
        static thread_local std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<size_t> groupDist(0, logicalNodes.size() - 1);
        size_t groupIdx = groupDist(rng);

        auto &pg = logicalNodes[groupIdx]->physicalGroup;
        if (!pg || pg->nodes.empty())
            return;

        std::uniform_int_distribution<size_t> nodeDist(0, pg->nodes.size() - 1);
        size_t nodeIdx = nodeDist(rng);

        std::uniform_real_distribution<float> loadDist(20.0f, 50.0f);
        float addLoad = loadDist(rng);

        pg->nodes[nodeIdx]->addWorkload(addLoad);
        LOG_INFO("[LogicalGroup] Randomly added " << addLoad << " workload to node " << pg->nodes[nodeIdx]->id
                                                  << " in group " << pg->groupId);
    }
    void LogicalGroup::stopMonitoringAll()
    {
        for (auto &ln : logicalNodes)
        {
            if (ln->physicalGroup)
                ln->physicalGroup->stopMonitoring();
        }
    }
}