#include "../Shared/CommandAPI.hpp"
#include "../Shared/ConfigAPI.hpp"
#pragma warning(push, 0)
#include "../ThirdParty/Bus/BusSystem.hpp"
#include "../ThirdParty/Bus/BusImpl.cpp"
#include <rang.hpp>
#pragma warning(pop)

namespace fs = std::experimental::filesystem;

int mainImpl(int argc, char** argv, Bus::ModuleSystem& sys) {
    BUS_TRACE_BEGIN("Piper.Main") {
        if(argc < 2)
            BUS_TRACE_THROW(std::logic_error("Need Command Name"));
        std::shared_ptr<Command> command =
            sys.instantiateByName<Command>(argv[1]);
        if(!command)
            BUS_TRACE_THROW(
                std::logic_error("No function called " + std::string(argv[1])));
        std::vector<char*> nargv{ argv[0] };
        for(int i = 2; i < argc; ++i)
            nargv.emplace_back(argv[i]);
        return command->doCommand(static_cast<int>(nargv.size()), nargv.data(),
                                  sys);
    }
    BUS_TRACE_END();
}

Bus::ReportFunction colorOutput(std::ostream& out, rang::fg col,
                                const char* pre) {
    return [&](Bus::ReportLevel, const std::string& message,
               const Bus::SourceLocation& srcLoc) {
        out << col << pre << ':' << message << std::endl;
        out << "module:" << srcLoc.module << std::endl;
        out << "function:" << srcLoc.functionName << std::endl;
        out << "location:" << srcLoc.srcFile << " line " << srcLoc.line
            << std::endl;
        out << rang::fg::reset;
    };
}

void processException(std::exception_ptr ex, const std::string& lastModule) {
    try {
        std::rethrow_exception(ex);
    } catch(const Bus::SourceLocation& src) {
        if(lastModule != src.module)
            std::cerr << "in module " << src.module;
        std::cerr << src.functionName << " at " << src.srcFile << " line "
                  << src.line << std::endl;
        try {
            std::rethrow_if_nested(src);
        } catch(...) {
            processException(std::current_exception(), src.module);
        }
    } catch(const std::exception& exc) {
        std::cerr << exc.what() << std::endl;
        try {
            std::rethrow_if_nested(exc);
        } catch(...) {
            processException(std::current_exception(), "");
        }
    } catch(...) {
        std::cerr << "Unknown Error" << std::endl;
    }
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

int main(int argc, char** argv) {
    auto reporter = std::make_shared<Bus::Reporter>();
    reporter->addAction(ReportLevel::Debug,
                        colorOutput(std::cerr, rang::fg::yellow, "Debug"));
    reporter->addAction(ReportLevel::Error,
                        colorOutput(std::cerr, rang::fg::red, "Error"));
    reporter->addAction(ReportLevel::Info,
                        colorOutput(std::cerr, rang::fg::reset, "Info"));
    reporter->addAction(ReportLevel::Warning,
                        colorOutput(std::cerr, rang::fg::yellow, "Warning"));
    try {
        Bus::ModuleSystem sys(reporter);
        Bus::addModuleSearchPath("sharedDll", *reporter);
        sys.wrapBuiltin([](Bus::ModuleSystem& sys) {
            return std::make_shared<BuiltinFunction>(sys);
        });
        return mainImpl(argc, argv, sys);
    } catch(...) {
        std::cerr << rang::fg::red << "Exception:" << std::endl;
        processException(std::current_exception(), "");
        std::cerr << rang::fg::reset;
    }
    return EXIT_FAILURE;
}
