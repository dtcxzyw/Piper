#include "MDLShared.hpp"
#include <iomanip>

BUS_MODULE_NAME("Piper.BuiltinMaterial.MDL.Base");

void configure(MDL::INeuray* neuray,
               const std::vector<std::string>& searchPath) {
    BUS_TRACE_BEG() {
        Handle<MDL::IMdl_compiler> compiler(
            neuray->get_api_component<MDL::IMdl_compiler>());
        // Set the module and texture search path.
        for(auto&& path : searchPath) {
            checkMDLError(compiler->add_module_path(path.c_str()) == 0);
        }
        /*
        // Load the FreeImage plugin.
        checkMDLError(compiler->load_plugin_library(
                          "nv_freeimage" MI_BASE_DLL_FILE_EXT) == 0);
                          */
    }
    BUS_TRACE_END();
}

using Bus::ReportLevel;
// Returns a string-representation of the given message severity
static ReportLevel severity2Str(mi::base::Message_severity severity) {
    switch(severity) {
        case mi::base::MESSAGE_SEVERITY_ERROR:
            return ReportLevel::Error;
        case mi::base::MESSAGE_SEVERITY_WARNING:
            return ReportLevel::Warning;
        case mi::base::MESSAGE_SEVERITY_INFO:
            return ReportLevel::Info;
        case mi::base::MESSAGE_SEVERITY_VERBOSE:
            return ReportLevel::Warning;
        case mi::base::MESSAGE_SEVERITY_DEBUG:
            return ReportLevel::Debug;
        default:
            break;
    }
    return ReportLevel::Info;
}
// Returns a string-representation of the given message category
static const char* kind2Str(MDL::IMessage::Kind kind) {
    switch(kind) {
        case MDL::IMessage::MSG_INTEGRATION:
            return "MDL SDK";
        case MDL::IMessage::MSG_IMP_EXP:
            return "Importer/Exporter";
        case MDL::IMessage::MSG_COMILER_BACKEND:
            return "Compiler Backend";
        case MDL::IMessage::MSG_COMILER_CORE:
            return "Compiler Core";
        case MDL::IMessage::MSG_COMPILER_ARCHIVE_TOOL:
            return "Compiler Archive Tool";
        case MDL::IMessage::MSG_COMPILER_DAG:
            return "Compiler DAG generator";
        default:
            break;
    }
    return "";
}
// Prints the messages of the given context.
// Returns true, if the context does not contain any error messages, false
// otherwise.
bool printMessages(Bus::Reporter& reporter,
                   MDL::IMdl_execution_context* context) {
    BUS_TRACE_BEG() {
        for(mi::Size i = 0; i < context->get_messages_count(); ++i) {
            Handle<const MDL::IMessage> message(context->get_message(i));
            reporter.apply(severity2Str(message->get_severity()),
                           message->get_string(),
                           BUS_SRCLOC(kind2Str(message->get_kind())));
        }
        return context->get_error_messages_count() == 0;
    }
    BUS_TRACE_END();
}

static void printWin32Errror(const std::string& pre, Bus::Reporter& reporter,
                             const Bus::SourceLocation& src) {
    LPSTR buffer = 0;
    LPCSTR message = "unknown failure";
    DWORD errorCode = GetLastError();
    if(FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                          FORMAT_MESSAGE_FROM_SYSTEM |
                          FORMAT_MESSAGE_IGNORE_INSERTS,
                      0, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                      (LPSTR)&buffer, 0, 0))
        message = buffer;
    reporter.apply(ReportLevel::Error,
                   pre + " (" + std::to_string(errorCode) + "): " + message,
                   src);
    if(buffer)
        LocalFree(buffer);
}

MDL::INeuray* MDLInit(HMODULE& handle, const fs::path& path,
                      Bus::Reporter& reporter) {
    fs::path libName = path.parent_path() / "libmdl_sdk" MI_BASE_DLL_FILE_EXT;
    handle = LoadLibraryExW(libName.c_str(), NULL,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!handle) {
        printWin32Errror("Failed to load library", reporter, BUS_DEFSRCLOC());
        return nullptr;
    }
    void* symbol = GetProcAddress(handle, "mi_factory");
    if(!symbol) {
        printWin32Errror("GetProcAddress error", reporter, BUS_DEFSRCLOC());
        return nullptr;
    }
    MDL::INeuray* neuray = MDL::mi_factory<MDL::INeuray>(symbol);
    if(!neuray) {
        Handle<MDL::IVersion> version(MDL::mi_factory<MDL::IVersion>(symbol));
        if(!version) {
            reporter.apply(ReportLevel::Error, "Incompatible library",
                           BUS_DEFSRCLOC());
        } else {
            reporter.apply(ReportLevel::Error,
                           std::string("Library version ") +
                               version->get_product_version() +
                               "does not match header "
                               "version " MI_NEURAYLIB_PRODUCT_VERSION_STRING,
                           BUS_DEFSRCLOC());
        }
        return nullptr;
    }
    {
        Handle<const MDL::IVersion> version(
            neuray->get_api_component<const mi::neuraylib::IVersion>());
        std::stringstream ss;
        ss << "MDL SDK Information:\n";
        ss << "MDL SDK header version          "
              "= " MI_NEURAYLIB_PRODUCT_VERSION_STRING "\n";
        ss << "MDL SDK library product name    = "
           << version->get_product_name() << '\n';
        ss << "MDL SDK library product version = "
           << version->get_product_version() << '\n';
        ss << "MDL SDK library build number    = "
           << version->get_build_number() << '\n';
        ss << "MDL SDK library build date      = " << version->get_build_date()
           << '\n';
        ss << "MDL SDK library build platform  = "
           << version->get_build_platform() << '\n';
        ss << "MDL SDK library version string  = " << version->get_string()
           << '\n';
        mi::base::Uuid idLibrary = version->get_neuray_iid();
        mi::base::Uuid idInterface = MDL::INeuray::IID();
        ss << std::hex << std::uppercase;
        ss << "MDL SDK header interface ID     = <" << std::setw(2)
           << idInterface.m_id1 << ", " << std::setw(2) << idInterface.m_id2
           << ", " << std::setw(2) << idInterface.m_id3 << ", " << std::setw(2)
           << idInterface.m_id4 << ">\n";
        ss << "MDL SDK library interface ID    = <" << std::setw(2)
           << idLibrary.m_id1 << ", " << std::setw(2) << idLibrary.m_id2 << ", "
           << std::setw(2) << idLibrary.m_id3 << ", " << std::setw(2)
           << idLibrary.m_id4 << ">\n";
        reporter.apply(ReportLevel::Info, ss.str(), BUS_DEFSRCLOC());
    }
    return neuray;
}
// Unloads the MDL SDK.
void MDLUninit(HMODULE handle, Bus::Reporter& reporter) {
    int result = FreeLibrary(handle);
    if(result == 0) {
        printWin32Errror("Failed to unload library", reporter, BUS_DEFSRCLOC());
    }
}
