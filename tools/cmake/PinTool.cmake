set(PIN_ROOT ${CMAKE_SOURCE_DIR}/../pin CACHE PATH "Path to Intel Pin installation")

function(add_pin_tool name)
  add_library(${name} SHARED ${ARGN})
  target_compile_options(${name} PRIVATE
    # -Wall
    # -Werror
    -Wno-unknown-pragmas
    -fno-stack-protector
    -fno-exceptions
    -funwind-tables
    -fasynchronous-unwind-tables
    -fno-rtti
    -fPIC
    -fabi-version=2
    -faligned-new
    # -O3
    -fomit-frame-pointer
    -fno-strict-aliasing
    -Wno-dangling-pointer
  )

  target_include_directories(${name} BEFORE PRIVATE
    ${PIN_ROOT}/source/include/pin
    ${PIN_ROOT}/source/include/pin/gen
    ${PIN_ROOT}/extras/components/include
    ${PIN_ROOT}/extras/xed-intel64/include/xed
    ${PIN_TOOL}/source/tools/Utils
    ${PIN_TOOL}/source/tools/InstLib
  )

  target_include_directories(${name} SYSTEM BEFORE PRIVATE
    ${PIN_ROOT}/extras/cxx/include
    ${PIN_ROOT}/extras/crt/include
    ${PIN_ROOT}/extras/crt/include/arch-x86_64
    ${PIN_ROOT}/extras/crt/include/kernel/uapi
    ${PIN_ROOT}/extras/crt/include/kernel/uapi/asm-x86
  )
  
  target_compile_definitions(${name} PRIVATE
    PIN_CRT=1
    TARGET_IA32E
    HOST_IA32E
    TARGET_LINUX
  )

  target_link_options(${name} PRIVATE
    -Wl,--hash-style=sysv
    -Wl,-Bsymbolic
    -Wl,--version-script=${PIN_ROOT}/source/include/pin/pintool.ver
    -fabi-version=2
    -nostdlib
  )

  target_link_directories(${name} BEFORE PRIVATE
    ${PIN_ROOT}/intel64/runtime/pincrt
    ${PIN_ROOT}/intel64/lib
    ${PIN_ROOT}/intel64/lib-ext
    ${PIN_ROOT}/extras/xed-intel64/lib
  )

  target_link_libraries(${name} PRIVATE
    ${PIN_ROOT}/intel64/runtime/pincrt/crtbeginS.o
    pin
    xed
    ${PIN_ROOT}/intel64/runtime/pincrt/crtendS.o
    pindwarf
    dl-dynamic
    c++
    c++abi
    m-dynamic
    c-dynamic
    unwind-dynamic
  )
  
endfunction()


# /usr/bin/c++ -shared -Wl,--hash-style=sysv /afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/intel64/runtime/pincrt/crtbeginS.o -Wl,-Bsymbolic -Wl,--version-script=/afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/source/include/pin/pintool.ver -fabi-version=2   -o obj-intel64/Declassify.so obj-intel64/Declassify.o -L/afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/intel64/runtime/pincrt -L/afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/intel64/lib -L/afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/intel64/lib-ext -L/afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/extras/xed-intel64/lib -lpin -lxed /afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/intel64/runtime/pincrt/crtendS.o -lpindwarf -ldl-dynamic -nostdlib -lc++ -lc++abi -lm-dynamic -lc-dynamic -lunwind-dynamic


# /usr/bin/c++ -DDeclassify_EXPORTS -DPIN_CRT=1 -DTARGET_IA32E -DTARGET_LINUX -I/afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/source/include/pin -I/afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/source/include/pin/gen -I/afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/extras/components/include -I/afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/extras/xed-intel64/include/xed -I/source/tools/Utils -I/source/tools/InstLib -isystem /afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/extras/cxx/include -isystem /afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/extras/crt/include -isystem /afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/extras/crt/include/arch-x86_64 -isystem /afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/extras/crt/include/kernel/uapi -isystem /afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/extras/crt/include/kernel/uapi/asm-x86 -O3 -DNDEBUG -std=gnu++20 -fPIC -Wall -Werror -Wno-unknown-pragmas -fno-stack-protector -fno-exceptions -funwind-tables -fasynchronous-unwind-tables -fno-rtti -fPIC -fabi-version=2 -faligned-new -O3 -fomit-frame-pointer -fno-strict-aliasing -Wno-dangling-pointer -MD -MT CMakeFiles/Declassify.dir/Declassify.cpp.o -MF CMakeFiles/Declassify.dir/Declassify.cpp.o.d -o CMakeFiles/Declassify.dir/Declassify.cpp.o -c /afs/cs.stanford.edu/u/nmosier/llsct2/tools/Declassify.cpp

# /usr/bin/c++ -Wall -Werror -Wno-unknown-pragmas -DPIN_CRT=1 -fno-stack-protector -fno-exceptions -funwind-tables -fasynchronous-unwind-tables -fno-rtti -DTARGET_IA32E -DHOST_IA32E -fPIC -DTARGET_LINUX -fabi-version=2 -faligned-new -I/afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/source/include/pin -I/afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/source/include/pin/gen -isystem /home/nmosier/llsct2/pin/extras/cxx/include -isystem /home/nmosier/llsct2/pin/extras/crt/include -isystem /home/nmosier/llsct2/pin/extras/crt/include/arch-x86_64 -isystem /home/nmosier/llsct2/pin/extras/crt/include/kernel/uapi -isystem /home/nmosier/llsct2/pin/extras/crt/include/kernel/uapi/asm-x86 -I/afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/extras/components/include -I/afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/extras/xed-intel64/include/xed -I/afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/source/tools/Utils -I/afs/cs.stanford.edu/u/nmosier/llsct2/tools/../pin/source/tools/InstLib -O3 -fomit-frame-pointer -fno-strict-aliasing  -Wno-dangling-pointer -c -o obj-intel64/Declassify.o Declassify.cpp
