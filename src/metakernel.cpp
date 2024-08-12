// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "metakernel.h"

#include <algorithm>
#include <deque>
#include <memory>
#include <unordered_map>
#include <utility>

#include "spacedivision.h"
#include "tools.h"

namespace Ladybirds { namespace spec {

using gen::Space;
using std::unordered_map;

using D2DMap = unordered_map<const Iface*, Iface*>;
    
template<class Iterator> Iterator begin(std::pair<Iterator, Iterator> p) { return p.first; }
template<class Iterator> Iterator end(std::pair<Iterator, Iterator> p) { return p.second; }


namespace {
Space IndicesAbsToRel(const Space & abs, const Space & absfull, const Space & relfull)
{
    Space rel = relfull;
    int iabs = -1;
    for(gen::Range & rg : rel)
    {
        if(rg.size() == 1) continue;
        
        while(absfull[++iabs].size() == 1);
        rg = abs[iabs] + (rg.first() - absfull[iabs].first());
    }
    return rel;
}

template<typename T> T & deref(T & arg) { return arg; }
template<typename T> T & deref(T * arg) { return *arg; }
template<typename T> struct SelfMap { T at(T arg) const {return arg;} };


/// \internal Given a dependency \p use transporting data away from a interface and a set of dependencies \p def providing
/// the interface with this data, creates a set of dependencies from the data origin (given by the elements in \p def)
/// to the destination (given by \p use.To) such that the interface \p use.From and \p defs[].To is bypassed.
/// These "direct data connections" are then appended to \p results.
template<typename T, typename TUseMap, typename TDefMap>
void ExpandDependency(const Dependency & use, const std::vector<T> & defs, MetaKernel::DepList & results,
                      TUseMap & usemap, TDefMap & defmap)
{
    gen::SpaceDivision<const Dependency*> sdiv(use.From.Index);
    sdiv.AssignSection(use.From.Index, nullptr);
    for(auto & elem : defs)
    {
        auto & def = deref(elem);
        sdiv.AssignSection(def.To.Index, &def);
        for(auto & secpair : sdiv.GetSections().equal_range(&def))
        {
            results.emplace_back(
                Dependency::Anchor(usemap.at(def.From.TheIface), 
                                   IndicesAbsToRel(secpair.second, def.To.Index, def.From.Index)),
                Dependency::Anchor(defmap.at(use.To.TheIface), 
                                   IndicesAbsToRel(secpair.second, use.From.Index, use.To.Index)));
        }
        sdiv.Unassign(&def); if(sdiv.GetSections().empty()) break;
    }
    assert(sdiv.GetSections().empty());
}

void FillD2D(unordered_map<const Iface*, Iface*> &d2d, const Task & taskFrom, Task & TaskTo)
{
    auto & fromifaces = taskFrom.Ifaces;
    auto & toifaces = TaskTo.Ifaces;
    
    assert(fromifaces.size() == toifaces.size());
    
    for(auto i = fromifaces.size(); i-- > 0; ) d2d[&fromifaces[i]] = &toifaces[i];
}

D2DMap CreateD2DMap(const MetaKernel & mkFrom, MetaKernel & mkTo)
{
    assert(mkFrom.Tasks.size() == mkTo.Tasks.size());
    D2DMap d2d;
    for(auto i = mkTo.Tasks.size(); i-- > 0; )
        FillD2D(d2d, *mkFrom.Tasks[i], *mkTo.Tasks[i]);
    FillD2D(d2d, *mkFrom.Inputs, *mkTo.Inputs);
    FillD2D(d2d, *mkFrom.Outputs, *mkTo.Outputs);
    return d2d;
}
} //namespace ::

MetaKernel::MetaKernel ( const MetaKernel& other )
 : Kernel(other),
   Tasks(Clone(other.Tasks)),
   Inputs(std::make_unique<Task>(this, "<meta-kernel inputs>",
                                 other.Inputs->GetParameters(), other.Inputs->GetDerivedParameters())),
   Outputs(std::make_unique<Task>(this, "<meta-kernel outputs>", 
                                  other.Outputs->GetParameters(), other.Outputs->GetDerivedParameters()))
{
    // Now copy dependencies. Problem here: Have to update iface pointers
    D2DMap d2d = CreateD2DMap(other, *this);
    
    Dependencies.reserve(other.Dependencies.size());
    for(const Dependency & dep : other.Dependencies)
    {
        Dependencies.emplace_back(Dependency::Anchor(d2d.at(dep.From.TheIface), dep.From.Index),
                                  Dependency::Anchor(d2d.at(dep.To.TheIface), dep.To.Index));
    }
}


void MetaKernel::InitInterface()
{
    //solution below is valid as long as parameters are not supported for metakernels
    Inputs = std::make_unique<Task>(this, "<meta-kernel inputs>", std::vector<int>(), std::vector<int>());
    Outputs = std::make_unique<Task>(this, "<meta-kernel outputs>", std::vector<int>(), std::vector<int>());
}

void AdjustBufferHints(Task & target, Task & source, Task & parent)
{
    assert(target.Ifaces.size() == source.Ifaces.size());
    for(auto itt = target.Ifaces.begin(), its = source.Ifaces.begin(), itend = target.Ifaces.end();
        itt != itend;
        ++itt, ++its)
    {
        Iface &tiface = *itt, &siface = *its;
        
        if(siface.BufferHint < 0)
        {
            tiface.PosHint = siface.PosHint;
            continue;
        }
        
        Iface & piface = parent.Ifaces[siface.BufferHint];
        tiface.BufferHint = piface.BufferHint;
        tiface.PosHint.AsVector().clear();
        tiface.PosHint.reserve(piface.PosHint.Dimensions());
        assert(piface.PosHint.Dimensions() >= siface.PosHint.Dimensions());
        
        auto itsidx = siface.PosHint.begin();
        auto & pdims = piface.GetDimensions();
        auto itpdim = pdims.begin(), itpdimend = pdims.end();
        for(const auto & pidx : piface.PosHint)
        {
            if(pidx.size() == 1)
            {
                tiface.PosHint.push_back(pidx);
                if(itpdim != itpdimend && *itpdim == 1) ++itpdim, ++itsidx;
            }
            else
            {
                assert(itpdim != itpdimend);
                assert(pidx.size() == *itpdim);
                assert(itsidx->size() <= *itpdim);
                tiface.PosHint.push_back(*itsidx + pidx.begin());
                ++itpdim, ++itsidx;
            }
        }
    }
}

MetaKernel::TaskList::iterator MetaKernel::Expand(TaskList::iterator itTask)
{
    auto uptask = std::move(*itTask);
    auto retpos = itTask - Tasks.begin();
    Tasks.erase(itTask);
    
    Task * ptask = uptask.get();
    assert(ptask);
    const MetaKernel & mk = *static_cast<MetaKernel*>(ptask->GetKernel());
    assert(mk.IsMetaKernel());
    
    //Copy all tasks from expansion target to this meta-kernel, adjusting the buffer position hints
    //Cannot move because we can only destroy the task, but not the meta-kernel it instantiates
    std::string nameprefix = ptask->Name + '.';
    auto oldsize = Tasks.size();
    Tasks.reserve(oldsize + mk.Tasks.size());
    for(auto & upTask : mk.Tasks)
    {
        Tasks.push_back(std::make_unique<Task>(*upTask));
        Tasks.back()->Name.insert(0, nameprefix);
        AdjustBufferHints(*Tasks.back(), *upTask, *ptask);
    }
    
    //Correspondency maps for interfaces
    D2DMap d2d;
    for(auto i = mk.Tasks.size(); i-- > 0; )
        FillD2D(d2d, *mk.Tasks[i], *Tasks[oldsize+i]);

    unordered_map<const Iface*, const Iface*> outifaces, inifaces;
    int nifaces = ptask->Ifaces.size();
    outifaces.reserve(nifaces); inifaces.reserve(nifaces);
    for(int i = 0; i < nifaces; ++i)
    {
        outifaces[&mk.Outputs->Ifaces[i]] = &ptask->Ifaces[i];
        inifaces[&ptask->Ifaces[i]]  = &mk.Inputs->Ifaces[i];
    }
    
    //Move all dependencies between this metakernel and the target inputs/outputs to separate containers
    unordered_map<const Iface*, std::vector<Dependency>> outer_inputs;
    std::deque<Dependency> outer_outputs;
    auto newend = std::remove_if(Dependencies.begin(), Dependencies.end(), [&](auto & dep)
        {
            Iface *fromiface = dep.From.TheIface, *toiface = dep.To.TheIface;
            if(toiface->GetTask() == ptask)
            {
                assert(fromiface->GetTask() != ptask);
                outer_inputs[inifaces.at(toiface)].push_back(std::move(dep));
                return true;
            }
            if(fromiface->GetTask() == ptask)
            {
                outer_outputs.push_back(std::move(dep));
                return true;
            }
            return false;
        });
    Dependencies.erase(newend, Dependencies.end());
    
    //Now process dependencies of mk
    unordered_map<const Iface*, std::vector<const Dependency*>> inner_outputs;
    unordered_map<const Iface*, DepList> inner_outputs_extra; //list of temporary dependencies
    
    Dependencies.reserve(Dependencies.size() + mk.Dependencies.size());
    for(const Dependency & innerdep : mk.Dependencies)
    {
        if(innerdep.From.TheIface->GetTask() == mk.Inputs.get())
        { // dependencies using input packets: extract and add to this object's dependencies
            SelfMap<Iface*> selfmap;
            if(innerdep.To.TheIface->GetTask() == mk.Outputs.get())
            {   // special case: Input goes directly to output. This happens for untouched inouts.
                // In this case, we need two extractions, one for the input and one for the output interface.
                // The first extraction happens here, to a special container for the resulting (temporary) dependencies.
                ExpandDependency(innerdep, outer_inputs.at(innerdep.From.TheIface),
                                 inner_outputs_extra[outifaces.at(innerdep.To.TheIface)], selfmap, selfmap);
            }
            else ExpandDependency(innerdep, outer_inputs.at(innerdep.From.TheIface), Dependencies, selfmap, d2d);
        }
        else if(innerdep.To.TheIface->GetTask() == mk.Outputs.get())
        { //dependencies defining values of output packets: store in a container for later extraction
            inner_outputs[outifaces.at(innerdep.To.TheIface)].push_back(&innerdep);
        }
        else
        { // internal dependencies (i.e. between different tasks of mk): just copy to this object's dependencies
            Dependencies.emplace_back(Dependency::Anchor(d2d.at(innerdep.From.TheIface), innerdep.From.Index),
                                      Dependency::Anchor(d2d.at(innerdep.To.TheIface), innerdep.To.Index));
        }
    }
    
    for(auto & pair : inner_outputs_extra)
    {
        auto & lst = inner_outputs[pair.first];
        lst.reserve(lst.size()+pair.second.size());
        for(auto & dep : pair.second)
        {
            lst.push_back(&dep);
            d2d[dep.From.TheIface] = dep.From.TheIface;
        }
    }
    
    // now extract output dependencies:
    // do this separately because it is easier to iterate over all possible defs for one use than the other way round
    for(const Dependency & use : outer_outputs)
    {
        SelfMap<Iface*> selfmap;
        ExpandDependency(use, inner_outputs.at(use.From.TheIface), Dependencies, d2d, selfmap);
    }
    
    return Tasks.begin()+retpos;
}

void MetaKernel::Flatten()
{
    for(auto it = Tasks.begin(); it != Tasks.end(); )
    {
        if((*it)->GetKernel()->IsMetaKernel()) it = Expand(it);
        else ++it;
    }
}

bool MetaKernel::LoadStoreMembers(loadstore::LoadStore &ls)
{
    return Kernel::LoadStoreMembers(ls)
        & ls.IO_Register("tasks", Tasks)
        & ls.IO_Register("inputs", *Inputs)
        & ls.IO_Register("outputs", *Outputs)
        & ls.IO("dependencies", Dependencies);
}


}} // namespace Ladybirds::spec

