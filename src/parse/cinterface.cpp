// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "cinterface.h"

#include <memory>
#include <unordered_map>
#include <clang/Config/config.h>
#include <clang/Driver/Driver.h>
#include <clang/Tooling/Tooling.h>
#include <clang/Tooling/CommonOptionsParser.h>

#include "graph/itemmap.h"
#include "graph/itemset.h"
#include "cmdlineoptions.h"
#include "clanghandlerfactory.h"
#include "dependency.h"
#include "metakernel.h"
#include "msgui.h"
#include "program.h"
#include "tools.h"

#if CFG_HAVE_DLFCN
#ifndef _GNU_SOURCE
#define _GNU_SOURCE //Needed in linux to "unlock" dladdr
#endif//_GNU_SOURCE
#include <dlfcn.h>
#endif //CFG_HAVE_DLFCN


using std::string;
using std::vector;
using Ladybirds::graph::ItemMap;
using Ladybirds::graph::ItemSet;
using Ladybirds::spec::Dependency;
using Ladybirds::spec::Task;
using Ladybirds::spec::Iface;
using Ladybirds::spec::Kernel;
using Ladybirds::spec::MetaKernel;
using Ladybirds::spec::TaskGraph;
using Ladybirds::impl::Program;

/// \internal Constructs a task graph in \p tg from \p mk, filling also an adjacency matrix in \p adjacency
static void BuildTaskGraph(MetaKernel & mk, /*out*/ TaskGraph & tg)
{
    assert(tg.IsEmpty());
    
    for(auto & uptask : mk.Tasks) tg.EmplaceNode(std::move(*uptask));
    
    auto adjacency = tg.GetNodeMap(tg.GetNodeSet()); //remember which edges we have already inserted
    for(auto & dep : mk.Dependencies)
    {
        Task *t1 = dep.From.TheIface->GetTask(), *t2 = dep.To.TheIface->GetTask();
        if(t1 == mk.Inputs.get() || t2 == mk.Outputs.get()) continue;
        if(adjacency[t1].Contains(t2)) continue;
        
        tg.EmplaceEdge(t1, t2);
        adjacency[t1].Insert(t2);
    }
}

/// Tries to find the Clang resource directory by looking, unlike Clang code, not in the path of our
/// executable but in that of the Clang library. Currently only works for UNIX systems with dladdr support.
static std::string GetClangResourceDir()
{
#if CFG_HAVE_DLFCN
    Dl_info info;
    dladdr((void*) &clang::driver::Driver::GetResourcesPath, &info);
    return clang::driver::Driver::GetResourcesPath(info.dli_fname, CLANG_RESOURCE_DIR);
#else
    return "";
#endif //CFG_HAVE_DLFCN
}

namespace Ladybirds { namespace parse {

/// Loads a Ladybirds C specification from the .lb file given in \p prog.Source into \p prog.
/// Fills kernels, meta-kernels, task graph, dependencies, reachability matrix, types, main task
bool LoadCSpec(CSpecOptions & opts, impl::Program &prog)
{
    // "Command line arguments" for our compiler frontend
    vector<string> args;
    auto &clangparams = Ladybirds::tools::gCmdLineOptions.ClangParams;
    args.reserve(clangparams.size() + 2);
    // We have to help a bit with the Clang resource path; clang currently looks in our directory, not in theirs.
    // If this fails, clang may have problems finding headers like stddef.h.
    string clangresdir = GetClangResourceDir();
    if(!clangresdir.empty())
    {
        args.push_back("-resource-dir");
        args.push_back(GetClangResourceDir());
    }
    // Now insert the other arguments. We do that after the resource dir so it can be overridden by the command line
    args.insert(args.end(), clangparams.begin(), clangparams.end());

    clang::tooling::FixedCompilationDatabase compilation(".", args);
    vector<string> sources = { opts.SpecificationFile };

    clang::tooling::ClangTool tool(compilation, sources);

    // Start extraction of information
    Ladybirds::parse::ClangHandlerFactory fact(opts, prog);
    auto upaction = clang::tooling::newFrontendActionFactory(&fact);
    if(tool.run(upaction.get()) != 0) return false;
    
    if(opts.OnlyParse) return true;
    
    Kernel * pkernel = prog.MainTask.GetKernel();
    if(pkernel && pkernel->IsMetaKernel())
    {
         //create and flatten duplicate of main kernel
        MetaKernel mk = *static_cast<MetaKernel*>(pkernel);
        mk.Flatten();

        //construct task graph and reachability matrix
        BuildTaskGraph(mk, prog.TaskGraph);
        
        //translate interface dependencies
        prog.Dependencies = std::move(mk.Dependencies);
        std::unordered_map<Iface*, Iface*> d2d;
        for(auto i = prog.MainTask.Ifaces.size(); i-- > 0; )
        {
            d2d[&mk.Inputs->Ifaces[i]] = &prog.MainTask.Ifaces[i];
            d2d[&mk.Outputs->Ifaces[i]] = &prog.MainTask.Ifaces[i];
        }
        for(Dependency & dep : prog.Dependencies)
        {
            if(dep.From.TheIface->GetTask() == mk.Inputs.get()) dep.From.TheIface = d2d.at(dep.From.TheIface);
            if(dep.To.TheIface->GetTask() == mk.Outputs.get()) dep.To.TheIface = d2d.at(dep.To.TheIface);
        }
    }

    return true;
}

}} //namespace Ladybirds::parse
