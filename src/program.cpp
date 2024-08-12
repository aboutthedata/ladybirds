// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "program.h"
#include "taskgroup.h"

namespace Ladybirds { namespace impl {

Program::Program(){}
Program::~Program(){}

bool Program::Definition::LoadStoreMembers(loadstore::LoadStore& ls)
{
    return ls.IO("id", Identifier) & ls.IO("definition", Value);
}

bool Program::LoadStoreMembers(loadstore::LoadStore& ld)
{
    assert(ld.IsStoring()); //loading not supported
    return ld.IO("definitions", Definitions)
        & ld.IO_Register("nativekernels", NativeKernels)
        & ld.IO_Register("metakernels", MetaKernels)
        & ld.IO_Register("externalbuffers", graph::ContainerRange<graph::PresDeque<Buffer>>(ExternalBuffers))
        & ld.IO_Register("tasks", GetTasks())
        & ld.IO_Register("maintask", MainTask)
        & ld.IO("dependencies", Dependencies)
        & ld.IO_Register("groups", Groups)
        & ld.IO("divisions", Divisions)
        & ld.IO("channels", Channels)
        & ld.IO("codefiles", CodeFiles)
        & ld.IO("auxfiles", AuxFiles);
    
}

}} //namespace Ladybirds::impl
