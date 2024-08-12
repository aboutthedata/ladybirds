// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_TOOLS_CMDLINEOPTIONS_H
#define LADYBIRDS_TOOLS_CMDLINEOPTIONS_H

#include <string>
#include <vector>

#include "loadstore.h"

extern std::string gResourceDir;//!< The resource directory path of the application
extern std::string gUserDir; //!< The Ladybirds settings directory in the user's home folder (usually ~/.ladybirds)

namespace Ladybirds {
namespace tools {

//! Stores all arguments that were obtained from the command line.
struct CmdLineOptions : public loadstore::LoadStorableCompound
{
    std::string ProgramSpec;
    std::string ProjectInfo;
    std::string MappingSpec;
    std::string CostSpec;
    std::string AccessCounts;
    std::string TimingInfo;
    std::string Backend;
    std::vector<std::string> ClangParams;
    bool Verbose;
    bool StupidBankAssign;
    bool Instrumentation;
    
    //! Parses the command line and stores the results in this structure. Also sets gResourceDir.
    void Initialize(int argc, char * argv[]);
    
    //! Serialization (used for passing command line info on to back-end
    bool LoadStoreMembers(class loadstore::LoadStore & ls) override;
};

extern CmdLineOptions gCmdLineOptions;

}} // namespace Ladybirds::tools

#endif // LADYBIRDS_TOOLS_CMDLINEOPTIONS_H
