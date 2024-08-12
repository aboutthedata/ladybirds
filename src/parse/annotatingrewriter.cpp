// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "annotatingrewriter.h"

#include <numeric>
#include <clang/Basic/SourceManager.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/raw_ostream.h>
#include "tools.h"

using llvm::StringRef;

namespace Ladybirds {
namespace parse {

llvm::raw_ostream &AnnotatingRewriter::WriteWithAnnotations(clang::FileID fid, llvm::raw_ostream& strm) const
{
    auto &sourceman = getSourceMgr();
    auto fent = sourceman.getFileEntryForID(fid);
    strm << "#line 1 \"" << fent->tryGetRealPathName() << "\"\n";

    auto filecontents = sourceman.getBufferData(fid);
    size_t inputlength = filecontents.size();
    
    auto *prb = getRewriteBufferFor(fid);
    if(!prb) return strm.write(filecontents.data(), inputlength); //nothing to rewrite – just copy the input file

    auto firstpiece = prb->begin().piece();
    assert(filecontents.startswith(firstpiece) && "Insertions at the beginning of the file are not supported");
    const char *inputstart = firstpiece.data();
    
    int lastline = 1, hiddenline = 1;
    llvm::SmallString<512> preserved;
    bool atbol = true;
    for (auto it = prb->begin(), itend = prb->end(); it != itend; it.MoveToNextPiece())
    {
        auto piece = it.piece();
        size_t pos = piece.data()-inputstart;
        if(pos < inputlength)
        { // piece is from original input
            auto lastnonwhite = piece.find_last_not_of(" \t\v\f\r");
            if(lastnonwhite == StringRef::npos)
            {
                if(atbol) preserved += piece;
                else strm << piece;
                continue;
            }
            
            int line = sourceman.getLineNumber(fid, pos);
            if(line != lastline)
            {
                if(line == lastline+1) strm << '\n';
                else strm << (atbol ? "#line " : "\n#line ") << line << '\n';
            }
            if(atbol) strm << preserved;
            
            atbol = (piece[lastnonwhite] == '\n');
            if(atbol)
            {
                preserved = piece.substr(lastnonwhite+1);
                piece = piece.substr(0, lastnonwhite+1);
            }
            strm << piece;
            
            lastline = sourceman.getLineNumber(fid, pos+piece.size());
            hiddenline = lastline - (atbol ? 1 : 0);
        }
        else
        {// piece was generated
            size_t eol = piece.find('\n');
            if(eol == StringRef::npos)
            {
                if(atbol) preserved += piece;
                else strm << piece;
                continue;
            }
            
            do
            {
                if(atbol)
                {
                    strm << "#line _LB_HIDDEN(" << hiddenline << ")\n";
                    strm << preserved;
                    preserved = "";
                    lastline = 0; //force line print next time
                }
                else atbol = true;
                
                strm << piece.substr(0, eol+1);
                piece = piece.substr(eol+1);
                eol = piece.find('\n');
            }
            while(eol != StringRef::npos);
            
            if(!piece.empty()) preserved = piece;
        }
    }
    return strm;
}


    
}} //namespace Ladybirds::parse
