#include "utils.h"
#include "system_model.h"
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <chrono>

namespace {

// Helper to get current UTC time as string
std::string current_utc_time_str() {
    auto t = std::time(nullptr);
    std::tm tm_utc;
#if defined(_WIN32)
    gmtime_s(&tm_utc, &t);
#else
    gmtime_r(&t, &tm_utc);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%d_%H-%M-%S_UTC");
    return oss.str();
}

} // anonymous namespace

// ----------- FileLogger Implementation -----------
FileLogger& FileLogger::instance()
{
    static FileLogger logger;
    return logger;
}

FileLogger::FileLogger() : initialized_(false)
{
    init();
}

FileLogger::~FileLogger() = default;

void FileLogger::init()
{
    if (!initialized_) {
        std::string log_dir = "log";
        std::filesystem::create_directories(log_dir);
        std::ostringstream oss;
        oss << log_dir << "/plain_log_" << current_utc_time_str() << ".log";
        filename_ = oss.str();
        // Touch the file
        std::ofstream touch(filename_);
        initialized_ = true;
    }
}

void FileLogger::set_filename(const std::string& filename)
{
    std::lock_guard<std::mutex> lock(mtx_);
    filename_ = filename;
    initialized_ = true;
}

void FileLogger::log(const std::string& level, const std::string& msg)
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_) init();
    std::ofstream ofs(filename_, std::ios::app);
    if (!ofs) return;
    // Timestamp
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm_utc;
#if defined(_WIN32)
    gmtime_s(&tm_utc, &now_c);
#else
    gmtime_r(&now_c, &tm_utc);
#endif
    ofs << "[" << std::put_time(&tm_utc, "%Y-%m-%d %H:%M:%S UTC") << "] ";
    ofs << "[" << level << "] " << msg << std::endl;
}

// ----------- Utils Namespace Implementation -----------

namespace Utils
{
    json to_json(const SystemModel::PhysicalNode &node)
    {
        return {
            {"id", node.id},
            {"workload", node.getWorkload()},
            {"isDR", node.isDR},
            {"queue_size", node.loadQueue.size()}
        };
    }

    json to_json(const SystemModel::PhysicalGroup &group)
    {
        json j;
        j["groupId"] = group.groupId;
        j["nodes"] = json::array();
        for (const auto &node : group.nodes)
            j["nodes"].push_back(to_json(*node));
        if (group.designatedRep)
            j["designatedRep"] = group.designatedRep->id;
        else
            j["designatedRep"] = nullptr;
        return j;
    }

    json to_json(const SystemModel::LogicalNode &ln)
    {
        json j;
        j["logicalId"] = ln.logicalId;
        j["groupId"] = ln.physicalGroup ? ln.physicalGroup->groupId : nullptr;
        j["designatedRep"] = ln.physicalGroup && ln.physicalGroup->designatedRep
                                 ? ln.physicalGroup->designatedRep->id
                                 : nullptr;
        j["nodes"] = json::array();
        if (ln.physicalGroup)
        {
            for (const auto &node : ln.physicalGroup->nodes)
                j["nodes"].push_back(to_json(*node));
        }
        return j;
    }

    json to_json(const SystemModel::LogicalGroup &group)
    {
        json j;
        j["id"] = group.id;
        j["logicalNodes"] = json::array();
        for (const auto &ln : group.logicalNodes)
            j["logicalNodes"].push_back(to_json(*ln));
        return j;
    }

    JsonStateLogger &JsonStateLogger::instance()
    {
        static JsonStateLogger logger;
        return logger;
    }

    JsonStateLogger::JsonStateLogger() : first_(true)
    {
        // Generate filename once at construction
        auto t = std::time(nullptr);
        std::tm tm_utc;
#if defined(_WIN32)
        gmtime_s(&tm_utc, &t);
#else
        gmtime_r(&t, &tm_utc);
#endif
        std::string log_dir = "log";
        std::filesystem::create_directories(log_dir);
        std::ostringstream oss;
        oss << log_dir << "/state_log_" << std::put_time(&tm_utc, "%Y-%m-%d_%H-%M-%S_UTC") << ".json";
        filename_ = oss.str();
        {
            std::ofstream touch(filename_); // This creates the file if it doesn't exist
        }
        // Launch the Python script and pass the log filename
        std::ostringstream py_cmd;
        py_cmd << "python3 pyscript/workload_monitor.py " << filename_ << ">> /dev/null 2>&1 &";
        (void)std::system(py_cmd.str().c_str());
    }
    JsonStateLogger::~JsonStateLogger() = default;

    void JsonStateLogger::log(const nlohmann::json &j)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        std::ofstream ofs(filename_, std::ios::app);
        if (!first_)
        {
            ofs << ",\n";
        }
        ofs << j.dump(4);
        first_ = false;
    }

    std::string JsonStateLogger::filename() const
    {
        return filename_;
    }

    void log_state_json(const nlohmann::json &j)
    {
        JsonStateLogger::instance().log(j);
    }
}