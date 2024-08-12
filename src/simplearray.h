// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef SIMPLEARRAY_H
#define SIMPLEARRAY_H

#include <utility>

//! Simple array class that provides an interface similar to that of standard library containers.
/** Upon construction, the array is not initialised. Also, the size of the array is fixed only during construction.
 ** Afterwards, it cannot be changed any more. **/
template<typename T> class SimpleArray
{
private:
    T * Data_;
    int Size_;
    
public:
    //! Constructs an empty array object with a null pointer as data
    inline explicit SimpleArray() : Data_(nullptr), Size_(0) {}
    //! if size is negative, the array data will be a null pointer, but the size will be taken over
    inline explicit SimpleArray(int size) : Data_(size >= 0 ? new T[size] : nullptr), Size_(size) {}
    inline ~SimpleArray() { delete [] Data_; }
    
    SimpleArray(const SimpleArray&) = delete; //to be implemented
    inline SimpleArray(SimpleArray&& other) { movehere(std::move(other)); }
    SimpleArray & operator=(const SimpleArray &) = delete; //to be imlemented
    inline SimpleArray & operator=(SimpleArray&& other) { clear(); movehere(std::move(other)); return *this; }
    
    inline T* data() { return Data_; }
    inline const T* data() const { return Data_; }
    inline int size() const { return Size_; }
    
    inline T* begin() {return Data_;}
    inline const T* begin() const {return Data_;}
    inline const T* cbegin() const {return Data_;}
    inline T* end() {return Data_+Size_;}
    inline const T* end() const {return Data_+Size_;}
    inline const T* cend() const {return Data_+Size_;}
    
    inline T& operator[](int i) { return Data_[i]; }
    
    inline void clear() { delete [] Data_; Data_ = nullptr; Size_ = 0; }
    
private:
    inline void movehere(SimpleArray&& other)
        { Data_ = other.Data_; other.Data_ = nullptr; Size_ = other.Size_; other.Size_ = 0; }
};






#endif //ndef SIMPLEARRAY_H
