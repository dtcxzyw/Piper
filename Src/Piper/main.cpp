#include "../Shared/CommandAPI.hpp"
#include "../Shared/ConfigAPI.hpp"
#pragma warning(push, 0)
#include "../ThirdParty/Bus/BusSystem.hpp"
#include <rang.hpp>
#pragma warning(pop)
#include <sstream>

namespace fs = std::experimental::filesystem;

BUS_MODULE_NAME("Piper.Main");

static int mainImpl(int argc, char** argv, Bus::ModuleSystem& sys) {
    BUS_TRACE_BEG() {
        if(argc < 2)
            BUS_TRACE_THROW(std::invalid_argument("Need Command Name"));
        std::shared_ptr<Command> command =
            sys.instantiateByName<Command>(argv[1]);
        if(!command)
            BUS_TRACE_THROW(std::invalid_argument("No function called " +
                                                  std::string(argv[1])));
        std::vector<char*> nargv{ argv[0] };
        for(int i = 2; i < argc; ++i)
            nargv.emplace_back(argv[i]);
        return command->doCommand(static_cast<int>(nargv.size()), nargv.data(),
                                  sys);
    }
    BUS_TRACE_END();
}

static Bus::ReportFunction colorOutput(std::ostream& out, rang::fg col,
                                       const char* pre, bool inDetail = false) {
    return [&](Bus::ReportLevel, const std::string& message,
               const Bus::SourceLocation& srcLoc) {
        /*
        if(pre == std::string("Error")) {
            try {
                throw std::runtime_error(message);
            } catch(...) {
                std::throw_with_nested(srcLoc);
            }
        }
        */

        out << col;
        if(inDetail) {
            out << pre << ':' << message << std::endl;
            out << "module:" << srcLoc.module << std::endl;
            out << "function:" << srcLoc.functionName << std::endl;
            out << "location:" << srcLoc.srcFile << " line " << srcLoc.line
                << std::endl;
        } else {
            out << pre << "[" << srcLoc.module << "]:" << message << std::endl;
        }
        out << std::endl << rang::fg::reset;
    };
}

static void processException(const std::exception& ex,
                             const std::string& lastModule);
static void processException(const Bus::SourceLocation& ex,
                             const std::string& lastModule);

template <typename T>
static void nestedException(const T& exc, const std::string& lastModule) {
    try {
        std::rethrow_if_nested(exc);
    } catch(const std::exception& ex) {
        if(&ex)
            processException(ex, lastModule);
        else {
            std::cerr << "Unrecognizable Exception." << std::endl;
        }
    } catch(const Bus::SourceLocation& ex) {
        if(&ex)
            processException(ex, lastModule);
        else {
            std::cerr << "Unrecognizable SourceLocation." << std::endl;
        }
    } catch(...) {
        std::cerr << "Unknown Error" << std::endl;
    }
}

static void processException(const std::exception& ex,
                             const std::string& lastModule) {
    std::cerr << ex.what() << std::endl;
    nestedException(ex, lastModule);
}

static void processException(const Bus::SourceLocation& src,
                             const std::string& lastModule) {
    if(lastModule != src.module)
        std::cerr << "in module " << src.module << std::endl;
    std::cerr << src.functionName << " at " << src.srcFile << " line "
              << src.line << std::endl;
    nestedException(src, src.module);
}

std::shared_ptr<Bus::ModuleFunctionBase>
makeJsonConfig(Bus::ModuleInstance& instance);
std::shared_ptr<Bus::ModuleFunctionBase>
makeRenderer(Bus::ModuleInstance& instance);

class BuiltinFunction final : public Bus::ModuleInstance {
public:
    explicit BuiltinFunction(Bus::ModuleSystem& sys)
        : Bus::ModuleInstance("builtin", sys) {}
    Bus::ModuleInfo info() const override {
        Bus::ModuleInfo res;
        res.name = "Piper.Builtin";
        res.guid = Bus::str2GUID("{9EAF8BBA-3C9B-46B9-971F-1C4F18670F74}");
        res.busVersion = BUS_VERSION;
        res.version = "0.0.1";
        res.description = "Piper utilities";
        res.copyright = "Copyright (c) 2019 Zheng Yingwei";
        return res;
    }
    std::vector<Bus::Name> list(Bus::Name api) const override {
        if(api == Config::getInterface())
            return { "JsonConfig" };
        if(api == Command::getInterface())
            return { "Renderer" };
        return {};
    }
    std::shared_ptr<Bus::ModuleFunctionBase> instantiate(Name name) override {
        if(name == "JsonConfig")
            return makeJsonConfig(*this);
        if(name == "Renderer")
            return makeRenderer(*this);
        return nullptr;
    }
};

#define PROCEXC()                                                \
    catch(const Bus::SourceLocation& ex) {                       \
        std::cerr << rang::fg::red << "Exception:" << std::endl; \
        processException(ex, "");                                \
        std::cerr << rang::fg::reset;                            \
    }                                                            \
    catch(const std::exception& ex) {                            \
        std::cerr << rang::fg::red << "Exception:" << std::endl; \
        processException(ex, "");                                \
        std::cerr << rang::fg::reset;                            \
    }

static void loadPlugins(Bus::ModuleSystem& sys) {
    for(auto p : fs::directory_iterator("Plugins")) {
        if(p.status().type() == fs::file_type::directory) {
            auto dir = p.path();
            auto dll = dir / dir.filename().replace_extension(".dll");
            if(fs::exists(dll)) {
                try {
                    sys.getReporter().apply(ReportLevel::Info,
                                            "Loading module " +
                                                dir.filename().string(),
                                            BUS_DEFSRCLOC());
                    sys.loadModuleFile(dll);
                }
                PROCEXC();
            }
        }
    }
    std::stringstream ss;
    ss << "Loaded Module:" << std::endl;
    for(auto mod : sys.listModules()) {
        ss << mod.name << " " << mod.version << " " << Bus::GUID2Str(mod.guid)
           << std::endl;
    }
    sys.getReporter().apply(ReportLevel::Info, ss.str(), BUS_DEFSRCLOC());
}

int main(int argc, char** argv) {
    auto reporter = std::make_shared<Bus::Reporter>();
    reporter->addAction(ReportLevel::Debug,
                        colorOutput(std::cerr, rang::fg::yellow, "Debug"));
    reporter->addAction(ReportLevel::Error,
                        colorOutput(std::cerr, rang::fg::red, "Error", true));
    reporter->addAction(ReportLevel::Info,
                        colorOutput(std::cerr, rang::fg::reset, "Info"));
    reporter->addAction(ReportLevel::Warning,
                        colorOutput(std::cerr, rang::fg::yellow, "Warning"));
    try {
        Bus::ModuleSystem sys(reporter);
        try {
            auto shared = fs::path(argv[0]).parent_path() / "SharedDll";
            Bus::addModuleSearchPath(shared, *reporter);
            sys.wrapBuiltin([](Bus::ModuleSystem& sys) {
                return std::make_shared<BuiltinFunction>(sys);
            });
            loadPlugins(sys);
            return mainImpl(argc, argv, sys);
        }
        PROCEXC();
    }
    PROCEXC();
    return EXIT_FAILURE;
}
