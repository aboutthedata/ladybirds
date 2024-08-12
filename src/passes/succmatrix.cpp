// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "graph/graph-extra.h"
#include "lua/pass.h"
#include "program.h"


namespace {

using Ladybirds::impl::Program;
using Ladybirds::lua::Pass;


bool CalcSuccessorMatrix(Program & prog);
Pass CalcSuccessorMatrixPass("CalcSuccessorMatrix", &CalcSuccessorMatrix);

//! Calculates a matrix of strict successors for every task. Strict successors are important because they can never
//! run at the same time.
bool CalcSuccessorMatrix(Program & prog)
{
    prog.TaskReachability = Ladybirds::graph::PruneEdges(prog.TaskGraph);
    return true;
}
} //namespace ::
