C++ Coding standard
===================

Source: [1] C++ Coding standards by Sutter/Alexandrescu http://sfx.ethz.ch/sfx_locater?sid=ALEPH:EBI01&genre=book&isbn=9780321113580

All these are guidelines: You should follow them in general but you can break them in individual cases if you have concrete, specific reasons. If you don't agree with one of the points, please ask.

If you edit a file, follow the style adopted there.

* Indentation: Use spaces, not tabs. You can decide on the number (we recommend 4), but be consistent.
* Line length: Usually, a code line will be short, easily below 80 characters. Occasionally, longer lines are necessary; you can go up to 120 characters. Don't enforce it though by putting multiple statements into one line if they are not semantically related.

* Naming: "Name classes, functions, and enums *LikeThis*; name variables *likeThis*; name private member variables *likeThis_*; and name macros *LIKE_THIS*" [1]. Name public member variables of classes or variables in structures LikeThis.
* Use the following prefixes:
    * *g* for global Variables (however, try to keep their number as small as possible)
    * *Get*/*Set* for getter/setter functions
    * *Num*/*num* or *n* means "number of" in the sense of "count of"
    * *p* means pointer, *up* means unique pointer
    * Do not use Hungarian type notation, i.e. type names as part of variables (e.g. fVal for a float, strText for a string, etc.)
* Use nullptr instead of NULL or even 0

* Commenting:
    * Place comments above class, structure and function definitions, in which you shortly explain what the class/structure is for or what the function does (don't repeat the code though, a high-level description is much more useful).
    * Document function parameters if they are not obvious. Also document requirements these parameters have to fulfil.
    * Inside a longer function, consider inserting comments to split it into "sections" and describe in each comment what the next section does.
    * Explain tricky parts of your code or things you had to think about.

* Assertions: Put lots of them all over the code. In particular, use them to verify the correctness of function arguments. This also holds for static_assert.
* Warnings: Compile with -Wall and write your code such that no warning occurs.

* Use const and constexpr whenever possible. Overload getters (put a "const val & GetVal() const;" by default, and a "val & Get Val();" only if it is absolutely required). Use constexpr instead of precompiler marcros for defining constants.
* Use the *override* keyword
* Declare variables as locally as possible. In particular, in a function, declare them when you need them rather at the beginning of the function.
* Prefer forward declarations for classes to including headers.

* Copy/move constructors, assignment operators for classes:
    * Write them if they are required. Prefer *= default*.
    * Disable them if they should not exist by using *= delete*. Add a clear comment about why. At the very least, write "//not allowed".
    * When unsure or as a default, disable them using *= delete* and add a comment "//routinely disabled", indicating that these methods can be added later.
* Error handling (exceptions, functions returning error codes, etc.):
    * For every error that may occur, think about how and where to handle it. Don't just blindly ignore it or lazily pass it through to the main function to deal with it.
    * Use whatever error handling mechanism is most appropriate. Be consistent within a class hierarchy / a functional unit.
* Make extensive use of std algorithms
* Use C++-style casts instead of C-style casts (e.g. *dynamic_cast<T\*>(x)* instead of *(T\*)x* ).
* Do not use *std::pair* and *std::tuple* unless it is dictated by other standard library interfaces (e.g. std::map). You can use structs instead (even anonymous or locally defined ones)!
