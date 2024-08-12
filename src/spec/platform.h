// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_SPEC_PLATFORM_H
#define LADYBIRDS_SPEC_PLATFORM_H

#include <deque>
#include <string>
#include <vector>

#include "graph/graph.h"
#include "loadstore.h"

namespace Ladybirds {
namespace spec {

//!Describes the details of a hardware platform
struct Platform : public loadstore::Referenceable
{
    ADD_CLASS_SIGNATURE(Platform);
    virtual bool LoadStoreMembers(loadstore::LoadStore & ls) override
        { assert(false && "not implemented"); return false; }
    //! Details concerning a cluster
    struct Cluster
    {
        int nCores;     //!< number of cores in the cluster
        int nBanks;     //!< number of memory banks in the cluster
        int BankSize;   //!< size of one memory bank (in bytes) in the cluster
    };
    //! Details about a cache configuration
    struct CacheConfig
    {
        int WordSize;      //!< size of one cache word in bytes (how many bytes are fetched and stored for/in one entry?)
        int Associativity; //!< number of different cache entries that can have the same cache index
        int LineCount;     //!< number of cache indices the cache handles (LineCount*Associativity=total number of entries)
    };
    
    class ComponentNode;
    struct Core;
    struct Memory;
    
    class Group : public loadstore::Referenceable
    {
        ADD_CLASS_SIGNATURE(Group);
    private:
        std::vector<Core*> Cores_;
        std::vector<Memory*> Memories_;
        long TotalMem_ = 0;
        
    public:
        int Index = -1;
        
    public:
        Group() = default;
        
        const auto &GetCores() const { return Cores_; }
        const auto &GetMemories() const { return Memories_; }
        long GetTotalMem() const { return TotalMem_; }
        
        void AddCore(Core *pcore) { pcore->Index = Cores_.size(); Cores_.push_back(pcore); }
        void AddMemory(Memory *pmem)
            { pmem->Index = Memories_.size(); Memories_.push_back(pmem); TotalMem_ += pmem->Size; }
        
        virtual bool LoadStoreMembers(loadstore::LoadStore & ls) override;
    };
    
    struct CoreType : public loadstore::Referenceable
    {
        ADD_CLASS_SIGNATURE(CoreType);
        std::string Name;

        virtual bool LoadStoreMembers(loadstore::LoadStore & ls) override;
    };
    
    struct Core : public loadstore::Referenceable
    {
        ADD_CLASS_SIGNATURE(Core);
        std::string Name;
        CoreType *Type;
        ComponentNode *pNode;
        std::deque<Group*> Groups;
        int Index = -1;

        virtual bool LoadStoreMembers(loadstore::LoadStore & ls) override;
    };
    
    struct DmaController : public loadstore::Referenceable
    {
        ADD_CLASS_SIGNATURE(DmaController);
        std::string Name;
        int Index = -1;

        virtual bool LoadStoreMembers(loadstore::LoadStore & ls) override;
    };
    
    struct Memory : public loadstore::Referenceable
    {
        ADD_CLASS_SIGNATURE(Memory);
        std::string Name;
        int Size;
        ComponentNode *pNode;
        std::deque<Group*> Groups;
        int Index = -1;

        virtual bool LoadStoreMembers(loadstore::LoadStore & ls) override;
    };
    
    class HwConnection;
    
    class ComponentNode : public graph::Node<graph::Graph<ComponentNode, graph::Version>, HwConnection>
    {
    public:
        Core *pCore = nullptr;
        Memory *pMem = nullptr;

    public:
        ComponentNode(Core *pcore) : pCore(pcore) {}
        ComponentNode(Memory *pmem) : pMem(pmem) {}
    };
    
    class HwConnection : public graph::Edge<ComponentNode>
    {
    public:
        int FixCost = 0; ///< Fix costs for DMA transfer (initialisation etc.). Zero for core to mem.
        int ReadCost = 0; ///< Cost for one read access (from mem to PE). Zero for DMA.
        int WriteCost = -1; ///< Cost for one write access (from PE to mem) or for transferring one byte (DMA)
        //! DMA controllers used for transferring data.
        /** Empty for core to mem. 1 element for a DMA transfer. 2 elements if there need to be sender and receiver.
         *  More elements are currently not supported. */
        std::vector<DmaController*> Controllers;
        
        int DmaCost(int nbytes) const {  return FixCost + WriteCost*nbytes; }
        int AccessCost(int nread, int nwrite) const { return ReadCost*nread + WriteCost*nwrite; }
    };
    
    using ConnMap = graph::ItemMap<graph::ItemMap<const spec::Platform::HwConnection *>>;
    
private:
    std::deque<CoreType> CoreTypes_;
    std::deque<Core> Cores_;
    std::deque<DmaController> DmaControllers_;
    std::deque<Memory> Memories_;
    std::deque<Group> Groups_;
    graph::Graph<ComponentNode, graph::Version> Graph_;
    mutable ConnMap ConnMap_;
    mutable graph::Version ConnMapVersion_;
    
public:
    const auto &GetCoreTypes() const { return CoreTypes_;}
    const auto &GetCores() const { return Cores_;}
    const auto &GetDmaControllers() const { return DmaControllers_;};
    const auto &GetMemories() const { return Memories_;};
    const auto &GetGroups() const { return Groups_;};
    const auto &GetGraph() const { return Graph_;};
    
    CoreType *AddCoreType(CoreType &&ct)
        { CoreTypes_.emplace_back(ct); return &CoreTypes_.back(); }
    DmaController *AddDmaController(DmaController &&dc)
        { dc.Index = DmaControllers_.size(); DmaControllers_.emplace_back(dc); return &DmaControllers_.back(); }
    Core *AddCore(Core &&c)
    {
        c.Index = Cores_.size();
        Cores_.emplace_back(c);
        Core *pcore = &Cores_.back();
        pcore->pNode = Graph_.EmplaceNode(pcore);
        return pcore;
    }
    Memory *AddMemory(Memory &&mem)
    {
        mem.Index = Memories_.size(); 
        Memories_.emplace_back(mem);
        auto *pmem = &Memories_.back(); 
        pmem->pNode = Graph_.EmplaceNode(pmem);
        return pmem;
    }
    Group *AddGroup(Group &&grptmpl)
    {
        grptmpl.Index = Groups_.size();
        Groups_.emplace_back(grptmpl);
        auto &grp = Groups_.back();
        for(auto &pcore : grp.GetCores()) pcore->Groups.push_back(&grp);
        for(auto &pmem : grp.GetMemories()) pmem->Groups.push_back(&grp);
        return &grp;
    }
    void AddEdge(Core *pcore, Memory *pmem, int readcost, int writecost)
    {
        auto *pconn = Graph_.EmplaceEdge(pcore->pNode, pmem->pNode);
        pconn->ReadCost = readcost;
        pconn->WriteCost = writecost;
    }
    void AddEdge(Memory *pfrommem, Memory *ptomem, int fixcost, int writecost, std::vector<DmaController *> dmas)
    {
        auto *pconn = Graph_.EmplaceEdge(pfrommem->pNode, ptomem->pNode);
        pconn->FixCost = fixcost;
        pconn->WriteCost = writecost;
        pconn->Controllers = std::move(dmas);
    }
    
    /// Returns a reference to a map for easily looking up connections from one node to another
    /** The returned reference is valid until this platform object is modified. **/
    const ConnMap & GetConnMap() const;
    
};

}} //namespace Ladybirds::spec

#endif // LADYBIRDS_SPEC_PLATFORM_H
