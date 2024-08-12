// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "cmdlineoptions.h"

#include <algorithm>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/ADT/SmallString.h"

#include "msgui.h"

using std::string;

std::string gResourceDir;
std::string gUserDir;

Ladybirds::tools::CmdLineOptions Ladybirds::tools::gCmdLineOptions;

namespace {
void Setfile(string & target, const llvm::cl::opt<string> & source)
{
    int crop = source.compare(0, 7, "file://") == 0 ? 7 : 0;
    target.assign(source, crop, string::npos);
}


string GetResourceDir(char ** argv)
{
    string s = llvm::sys::fs::getMainExecutable(argv[0], &gResourceDir/*just need some symbol from the executable*/);

    do
    {
        if(llvm::sys::fs::exists(s + "/share/ladybirds"))
        {
            return (s += '/');
        }
        
        s = llvm::sys::path::parent_path(s).str();
    }
    while(!s.empty());
    
    return "";
}

string GetUserDir()
{
    llvm::SmallString<64> s;
    if (!llvm::sys::path::home_directory(s))  return "";

    llvm::sys::path::append(s, ".ladybirds/");
    return std::string(s);
}

/** \internal Finds the backend named \p name and store its path in \p path. Returns true on success.
 *  \p path may also be overwritten if the function is not successful. **/
bool FindBackend(const string & name, /*out*/ string & path)
{
    //the common directory contains some shared initialisation scripts and can thus not be used as a backend
    if(name == "common") return false;
    
    //first look in the user resource director (~/.ladybirds/codegen) for the backend directory
    path = gUserDir + "codegen/" + name;
    if(llvm::sys::fs::exists(path)) return true;

    //then look in the global resource directory (usually /usr/share/ladybirds/codegen) for the backend directory
    path = gResourceDir + "share/ladybirds/codegen/" + name;
    return llvm::sys::fs::exists(path);
}

void FindBackends(string path, std::vector<string> & result)
{
    std::error_code ec;
    for (llvm::sys::fs::directory_iterator dir(path, ec), dirend;
        dir != dirend && !ec;
        dir.increment(ec))
    {
        auto str = llvm::sys::path::filename(dir->path());
        if(str != "common" && str != "list") result.push_back(str.str());
    }
}

void ListBackends()
{
    std::vector<string> backends;

    FindBackends(gResourceDir + "share/ladybirds/codegen", backends);
    if(!gUserDir.empty()) FindBackends(gUserDir + "codegen", backends);
    std::sort(backends.begin(), backends.end(), std::locale("en_US.UTF-8"));
    backends.erase(std::unique(backends.begin(), backends.end()), backends.end());
    
    std::cerr << "Supported backends:" << std::endl;
    for(auto & str : backends) std::cerr << " * " << str << std::endl;
}
} //namespace ::

namespace Ladybirds{ namespace tools {
    
void CmdLineOptions::Initialize(int argc, char* argv[])
{
    gUserDir = GetUserDir();
    gResourceDir = GetResourceDir(argv);
    if(gResourceDir.empty())
    {
        gMsgUI.Fatal("Cannot find resource directory for application.\n");
        exit(1);
    }

    using namespace llvm::cl;
	
    SubCommand sc("ladybirds");

    opt<string> backend("b", desc("Specify code generation backend. Say 'list' to get a list of supported backends."),
                        value_desc("backend"), sub(sc));
    opt<string> costfile("c", desc("Give the path to a cost specification file"), value_desc("cost spec"), sub(sc));
    opt<string> mappingfile("m", desc("Give the path to a mapping specification file"), value_desc("mapping spec"), sub(sc));
    opt<string> projectinfofile("p", desc("Give the path to a project information file"), value_desc("project info"), sub(sc));
    opt<string> timingfile("t", desc("Give the path to a timing information file"), value_desc("timing info"), sub(sc));
    opt<string> accesscountfile("a", desc("Give the path to a timing information file"), value_desc("timing info"), sub(sc));
    opt<bool>   verbose("v", desc("Print out more information"), sub(sc));
    opt<bool>   stupidbanks("stupidbanks", desc("Purely load-balancing based bank assignment"), sub(sc));
    opt<bool>   instrumentation("i", desc("Generate C++ code with inbuilt instrumentation"), sub(sc));
    opt<string> clang_passthrough("clang-args", desc("Additional arguments to be passed on to the clang compiler"), sub(sc));
    opt<string> inputfile(Positional, desc("<specification file>"), sub(sc));
    
    std::vector<const char*> argvPlus = {"", "ladybirds"};
    argvPlus.insert(argvPlus.end(), argv+1, argv+argc);
    ParseCommandLineOptions(argvPlus.size(), argvPlus.data());

    Setfile(ProgramSpec,  inputfile);
    Setfile(MappingSpec,  mappingfile);
    Setfile(CostSpec,     costfile);
    Setfile(ProjectInfo,  projectinfofile);
    Setfile(TimingInfo,   timingfile);
    Setfile(AccessCounts, accesscountfile);
    Verbose = verbose;
    StupidBankAssign = stupidbanks;
    Instrumentation = instrumentation;

    std::istringstream iss(clang_passthrough);
    ClangParams = {
        "-xc++", "-std=c++14", //Handle all files as C++14 source code
        "-Wno-unused-value", //suppress the unused value warning (needed for subarrays like "myblock[0,50]")
        "-D__LADYBIDRS_PARSER_AT_WORK__=1", //define precompiler constant such that ladybirds.h knows it's the
                                            //translation tool (and not the normal compiler, like later)
        "-I" + gResourceDir + "share/ladybirds/include/" // location of ladybirds.h
    };
    ClangParams.insert(ClangParams.end(), std::istream_iterator<string>(iss), std::istream_iterator<string>());

    if(backend == "list")
    {
        ListBackends();
        exit(0);
    }

    if(!backend.empty() && !FindBackend(backend, Backend))
    {
        gMsgUI.Fatal("Backend '%s' not supported.", backend.c_str());
        ListBackends();
        exit(1);
    }
    
    if(ProgramSpec.empty())
    {
        gMsgUI.Fatal("No input files!");
        exit(1);
    }
}

///\internal loads/stores a string like LoadStore::IO, but does not store the empty string (will be NULL or so instead)
static bool LsStringOrNull(loadstore::LoadStore & ls, const char * name, std::string & str)
{
    if(ls.IsStoring() && str.empty()) return true;
    else return ls.IO(name, str, false);
}

bool CmdLineOptions::LoadStoreMembers(loadstore::LoadStore & ls)
{
    return ls.IO("lbfile", ProgramSpec)
         & LsStringOrNull(ls, "projinfo", ProjectInfo)
         & LsStringOrNull(ls, "mapping", MappingSpec)
         & LsStringOrNull(ls, "costs", CostSpec)
         & LsStringOrNull(ls, "timings", TimingInfo)
         & LsStringOrNull(ls, "accesscounts", AccessCounts)
         & ls.IO("verbose", Verbose, false)
         & ls.IO("instrumentation", Instrumentation, false)
         & ls.IO("stupidbanks", StupidBankAssign, false);
}

}} //namespace Ladybirds::tools
