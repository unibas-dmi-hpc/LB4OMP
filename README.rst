======
LB4OMP
======
**LB4OMP** is an extended LLVM OpenMP runtime library that supports thirteen dynamic and adaptive loop scheduling techniques from the literature. LB4OMP is a load balancing performance portfolio that can offer improved performance by adapting to the unpredictable variations in application and system during execution. LB4OMP is used to improve applications performance, assess the effectiveness of loop scheduling techniques, and support loop scheduling research in multithreaded applications.

.. image:: https://zenodo.org/badge/DOI/10.5281/zenodo.3872907.svg
   :target: https://doi.org/10.5281/zenodo.3872907


**Paper references**
 

- [J. H. M. Korndörfer, A. Eleliemy, A. Mohammed and F. M. Ciorba, LB4OMP: A Dynamic Load Balancing Library for Multithreaded Applications, in IEEE Transactions on Parallel and Distributed Systems, vol. 33, no. 4, pp. 830-841, doi: 10.1109/TPDS.2021.3107775.](https://ieeexplore.ieee.org/document/9524500/)




**LB4OMP contains the following loop scheduling techniques:**

**OpenMP standard**

| ``static``  
| ``dynamic``  
``guided``

**Dynamic** and **non-adaptive** loop scheduling techniques **OpenMP non-standard**

``Trapezoid self scheduling (TSS)``

**Dynamic** and **non-adaptive** loop scheduling techniques **newly implemented in LB4OMP**

| ``Fixed size chunk (FSC)``  
| ``Factoring (FAC)``  
| ``Improved implementation of Factoring (mFAC)``  
| ``Practical variant of factoring (FAC2)`` 
| ``Practical variant of weighted factoring (WF2)``
| ``Tapering (TAP)``
| ``Modified Fixed size chunk (mFSC)`` 
| ``Trapezoid factoring self scheduling  (TFSS)``
| ``Fixed increase self scheduling (FISS)``
| ``Variable increase self scheduling (FISS)``
``Random (RND)``



**Dynamic** and **adaptive** loop scheduling techniques **newly implemented in LB4OMP**

| ``BOLD`` 
| ``Adaptive weighted factoring (AWF) for time-stepping applications``
| ``Four variants of adaptive weighted factoring (AWF-B,C,D,E)``  
| ``Adaptive factoring (AF)``  
``Improved implementation of Adaptive factoring (mAF)``

**LB4OMP contains the following features for performance measurement:**

| ``profiling``  
The ``profiling`` tool works similar to ``dynamic,1``, yet with timers that capture the average iteration execution time and its standard deviation for the target loops. The collected information is stored in a file, which is read later by the ``FSC``, ``FAC``, ``TAP``, and ``BOLD`` during execution. 
   
=================
How to use LB4OMP
=================
The first step is to compile the runtime library. LB4OMP is compiled using the same guidelines provided by the LLVM OpenMP runtime library as shown in the next section of this README (How to Build LB4OMP, the extended LLVM* OpenMP* Libraries).

One must ensure that the target OpenMP loops in the application contain the ``schedule(runtime)`` clause. If that is the case, no other changes are required and there is no need to recompile the application code.

Sequentially, one needs to add the path to the compiled LB4OMP runtime library to the environment variable that the linker uses to load dynamic and shared libraries. For instance, in Linux/Unix environments, one adds the path for the compiled LB4OMP to the ``LD_LIBRARY_PATH`` environment variable.


**LB4OMP introduces specific environment variables that need to be defined to select and control the provided scheduling techniques. These are:**

``KMP_CPU_SPEED`` informs LB4OMP about the host CPU speed in MHz. Defining this variable is mandatory, because the timing functions of LB4OMP use it. Only integer values are expected for this environment variable.

``KMP_PROFILE_DATA`` defines the path for the profiling information. Four techniques: ``FSC``, ``FAC``, ``TAP``, and ``BOLD`` require profiling information, such as the average of loop iteration execution times, the standard deviation of loop iteration execution times, and the scheduling overhead of self-scheduling (individual) loop iterations.
LB4OMP implements a profiling tool that collects such information. The profiling tool uses ``KMP_PROFILE_DATA`` to determine the path where the collected profiling information will be stored.

``KMP_WEIGHTS`` can be used by ``WF2``, ``AWF``, or ``AWF-B,C,D,E``. It expects a sequence of values separated by commas. These values denote the weights that each core will receive. The weights are used, for example, for heterogeneous systems if one wants to give fewer iterations to slower cores. For instance, if the weights are set as ``{1,0.5}``, the second core will receive half of the chunk size that the first core received. The default ``KMP_WEIGHTS`` value is ``1`` for all cores.

``KMP_TIME_LOOPS`` defines the path where LB4OMP stores the collected execution time of all OpenMP parallel loops associated with the ``schedule(runtime)`` clause. When this environment variable is not defined, LB4OMP will not collect loops' execution time.

``KMP_PRINT_CHUNKS`` when this environment variable is set to one, the LB4OMP collects and stores the calculated chunk size for each thread for each scheduling round. This information is stored in the same file as defined by ``KMP_TIME_LOOPS``. This data may be very large depending on the scheduling technique and the size of the OpenMP loop.

==================================
LB4OMP with Reinforcement Learning
==================================

LB4OMP also provides reinforcement learning implementation of
common learning algorithm for RL agents to select a dynamic loop
scheduling algorithm automatically during runtime. The following
learning algorithms and other combinations are supported:

+--------------+---+---------------+---+----------------+---+-------------+
| Learning     |   | Value         |   | Selection      |   | Reward      |
| Method       |   | Initializer   |   | Policy         |   | Signal      |
+==============+===+===============+===+================+===+=============+
| Q-Learning   |   | Zero          |   | Explore First  |   | Looptime    |
+--------------+---+---------------+---+----------------+---+-------------+
| Doub         |   | Random        |   | Epsilon Greedy |   | Loa         |
| leQ-Learning |   |               |   |                |   | d-Imbalance |
+--------------+---+---------------+---+----------------+---+-------------+
| SARSA        |   | Optimistic    |   | Softmax        |   | Robustness  |
|              |   |               |   | (Boltzman)     |   |             |
+--------------+---+---------------+---+----------------+---+-------------+
| Ex           |   |               |   |                |   |             |
| pected-SARSA |   |               |   |                |   |             |
+--------------+---+---------------+---+----------------+---+-------------+
| QV-Learning  |   |               |   |                |   |             |
+--------------+---+---------------+---+----------------+---+-------------+
| R-Learning   |   |               |   |                |   |             |
+--------------+---+---------------+---+----------------+---+-------------+
| DQN-Learning |   |               |   |                |   |             |
+--------------+---+---------------+---+----------------+---+-------------+
| Direct-Chunk |   |               |   |                |   |             |
+--------------+---+---------------+---+----------------+---+-------------+

.. figure:: runtime/src/reinforcement-learning/mathod-combinations.drawio.png
   :alt: Possible Mathod Combinations

+---------------------------+--------------------------------------------------------------+----------------------+--------------------+
| Environment Variable      | Description                                                  | Possible Values      | Default            |
+===========================+==============================================================+======================+====================+
| OMP_SCHEDULE              | Sets the schedule for loops annotated with the keyword       | auto, [6..15]        | NA                 |
|                           | “runtime”. Only values related to RL methods are listed.     |                      |                    |
+---------------------------+--------------------------------------------------------------+----------------------+--------------------+
| KMP_RL_INIT               | Sets the initialization method for the tabular data          | 'zero', 'random',    | 'zero'             |
|                           | structure of the agent.                                      | 'optimistic'         |                    |
+---------------------------+--------------------------------------------------------------+----------------------+--------------------+
| KMP_RL_POLICY             | Sets the action selection policy of the agent.               | 'explore_first',     | 'explore_first'    |
|                           |                                                              | 'epsilon_greedy',    |                    |
|                           |                                                              | 'softmax'            |                    |
+---------------------------+--------------------------------------------------------------+----------------------+--------------------+
| KMP_REWARD                | Sets the metric that the agent uses to derive the reward.    | 'looptime',          | 'looptime'         |
|                           |                                                              | 'load_imbalance',    |                    |
|                           |                                                              | 'robustness'         |                    |
+---------------------------+--------------------------------------------------------------+----------------------+--------------------+
| KMP_ALPHA                 | The learning rate α determines how new info overrides old.   | [0 .. 1]             | 0.85               |
+---------------------------+--------------------------------------------------------------+----------------------+--------------------+
| KMP_ALPHA_DECAY           | Factor by which to decay the learning rate α each timestep.  | [0 .. 1]             | 0.90               |
+---------------------------+--------------------------------------------------------------+----------------------+--------------------+
| KMP_ALPHA_MIN             | Minimum value for α where the decay stops.                   | [0 .. 1]             | 0.10               |
+---------------------------+--------------------------------------------------------------+----------------------+--------------------+
| KMP_GAMMA                 | Discount factor γ; importance of future rewards.             | [0 .. 1]             | 0.95               |
+---------------------------+--------------------------------------------------------------+----------------------+--------------------+
| KMP_EPSILON               | Probability of taking a random action (exploration).         | [0 .. 1]             | 0.9                |
+---------------------------+--------------------------------------------------------------+----------------------+--------------------+
| KMP_EPSILON_DECAY         | Factor to decay exploration probability after each step.     | [0 .. 1]             | 0.9                |
+---------------------------+--------------------------------------------------------------+----------------------+--------------------+
| KMP_EPSILON_MIN           | Minimum value for exploration probability (1 - ε).           | [0 .. 1]             | 0.10               |
+---------------------------+--------------------------------------------------------------+----------------------+--------------------+





=========================================================
How to Build LB4OMP, the extended LLVM* OpenMP* Libraries
=========================================================

****************************************************
Adopted from the official LLVM repository README.rst
****************************************************

This repository requires `CMake <http://www.cmake.org/>`_ v2.8.0 or later.  LLVM
and Clang need a more recent version which also applies for in-tree builds.  For
more information than available in this document please see
`LLVM's CMake documentation <http://llvm.org/docs/CMake.html>`_ and the
`official documentation <https://cmake.org/cmake/help/v2.8.0/cmake.html>`_.

.. contents::
   :local:

How to Call CMake Initially, then Repeatedly
============================================
- When calling CMake for the first time, all needed compiler options must be
  specified on the command line.  After this initial call to CMake, the compiler
  definitions must not be included for further calls to CMake.  Other options
  can be specified on the command line multiple times including all definitions
  in the build options section below.
- Example of configuring, building, reconfiguring, rebuilding:
  
  .. code-block:: console
  
    $ mkdir build
    $ cd build
    $ cmake -DCMAKE_C_COMPILER=icc -DCMAKE_CXX_COMPILER=icpc -DLIBOMP_HAVE___RDTSC=ON -DLIBOMP_HAVE_X86INTRIN_H=ON ..
    $ make

  .. code-block:: console

    $ mkdir build
    $ cd build
    $ cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ..  # Initial configuration
    $ make
    ...
    $ make clean
    $ cmake -DCMAKE_BUILD_TYPE=Debug ..                               # Second configuration
    $ make
    ...
    $ rm -rf *
    $ cmake -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ ..        # Third configuration
    $ make

- Notice in the example how the compiler definitions are only specified for an
  empty build directory, but other build options are used at any time.
- The file ``CMakeCache.txt`` which is created after the first call to CMake is
  a configuration file which holds all values for the build options.  These
  values can be changed using a text editor to modify ``CMakeCache.txt`` as
  opposed to using definitions on the command line.
- To have CMake create a particular type of build generator file simply include
  the ``-G <Generator name>`` option:

  .. code-block:: console

    $ cmake -G "Unix Makefiles" ...

  You can see a list of generators CMake supports by executing the cmake command
  with no arguments.

Instructions to Build
=====================
.. code-block:: console

 $ cd openmp_top_level/ [ this directory with libomptarget/, runtime/, etc. ]
 $ mkdir build
 $ cd build

 [ Unix* Libraries ]
 $ cmake -DCMAKE_C_COMPILER=<C Compiler> -DCMAKE_CXX_COMPILER=<C++ Compiler> ..

 [ Windows* Libraries ]
 $ cmake -G <Generator Type> -DCMAKE_C_COMPILER=<C Compiler> -DCMAKE_CXX_COMPILER=<C++ Compiler> -DCMAKE_ASM_MASM_COMPILER=[ml | ml64] -DCMAKE_BUILD_TYPE=Release ..

 $ make
 $ make install

CMake Options
=============
Builds with CMake can be customized by means of options as already seen above.
One possibility is to pass them via the command line:

.. code-block:: console

  $ cmake -DOPTION=<value> path/to/source

.. note:: The first value listed is the respective default for that option.

Generic Options
---------------
For full documentation consult the CMake manual or execute
``cmake --help-variable VARIABLE_NAME`` to get information about a specific
variable.

**CMAKE_BUILD_TYPE** = ``Release|Debug|RelWithDebInfo``
  Build type can be ``Release``, ``Debug``, or ``RelWithDebInfo`` which chooses
  the optimization level and presence of debugging symbols.

**CMAKE_C_COMPILER** = <C compiler name>
  Specify the C compiler.

**CMAKE_CXX_COMPILER** = <C++ compiler name>
  Specify the C++ compiler.

**CMAKE_Fortran_COMPILER** = <Fortran compiler name>
  Specify the Fortran compiler. This option is only needed when
  **LIBOMP_FORTRAN_MODULES** is ``ON`` (see below).  So typically, a Fortran
  compiler is not needed during the build.

**CMAKE_ASM_MASM_COMPILER** = ``ml|ml64``
  This option is only relevant for Windows*.

Options for all Libraries
-------------------------

**OPENMP_ENABLE_WERROR** = ``OFF|ON``
  Treat warnings as errors and fail, if a compiler warning is triggered.

**OPENMP_LIBDIR_SUFFIX** = ``""``
  Extra suffix to append to the directory where libraries are to be installed.

**OPENMP_TEST_C_COMPILER** = ``${CMAKE_C_COMPILER}``
  Compiler to use for testing. Defaults to the compiler that was also used for
  building.

**OPENMP_TEST_CXX_COMPILER** = ``${CMAKE_CXX_COMPILER}``
  Compiler to use for testing. Defaults to the compiler that was also used for
  building.

**OPENMP_LLVM_TOOLS_DIR** = ``/path/to/built/llvm/tools``
  Additional path to search for LLVM tools needed by tests.

**OPENMP_LLVM_LIT_EXECUTABLE** = ``/path/to/llvm-lit``
  Specify full path to ``llvm-lit`` executable for running tests.  The default
  is to search the ``PATH`` and the directory in **OPENMP_LLVM_TOOLS_DIR**.

**OPENMP_FILECHECK_EXECUTABLE** = ``/path/to/FileCheck``
  Specify full path to ``FileCheck`` executable for running tests.  The default
  is to search the ``PATH`` and the directory in **OPENMP_LLVM_TOOLS_DIR**.

Options for ``libomp``
----------------------

**LIBOMP_ARCH** = ``aarch64|arm|i386|mic|mips|mips64|ppc64|ppc64le|x86_64``
  The default value for this option is chosen based on probing the compiler for
  architecture macros (e.g., is ``__x86_64__`` predefined by compiler?).

**LIBOMP_MIC_ARCH** = ``knc|knf``
  Intel(R) Many Integrated Core Architecture (Intel(R) MIC Architecture) to
  build for.  This value is ignored if **LIBOMP_ARCH** does not equal ``mic``.

**LIBOMP_OMP_VERSION** = ``50|45|40|30``
  OpenMP version to build for.  Older versions will disable certain
  functionality and entry points.

**LIBOMP_LIB_TYPE** = ``normal|profile|stubs``
  Library type can be ``normal``, ``profile``, or ``stubs``.

**LIBOMP_USE_VERSION_SYMBOLS** = ``ON|OFF``
  Use versioned symbols for building the library.  This option only makes sense
  for ELF based libraries where version symbols are supported (Linux*, some BSD*
  variants).  It is ``OFF`` by default for Windows* and macOS*, but ``ON`` for
  other Unix based operating systems.

**LIBOMP_ENABLE_SHARED** = ``ON|OFF``
  Build a shared library.  If this option is ``OFF``, static OpenMP libraries
  will be built instead of dynamic ones.

  .. note::

    Static libraries are not supported on Windows*.

**LIBOMP_FORTRAN_MODULES** = ``OFF|ON``
  Create the Fortran modules (requires Fortran compiler).

macOS* Fat Libraries
""""""""""""""""""""
On macOS* machines, it is possible to build universal (or fat) libraries which
include both i386 and x86_64 architecture objects in a single archive.

.. code-block:: console

  $ cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_OSX_ARCHITECTURES='i386;x86_64' ..
  $ make

There is also an option **LIBOMP_OSX_ARCHITECTURES** which can be set in case
this is an LLVM source tree build. It will only apply for the ``libomp`` library
avoids having the entire LLVM/Clang build produce universal binaries.

Optional Features
"""""""""""""""""

**LIBOMP_USE_ADAPTIVE_LOCKS** = ``ON|OFF``
  Include adaptive locks, based on Intel(R) Transactional Synchronization
  Extensions (Intel(R) TSX).  This feature is x86 specific and turned ``ON``
  by default for IA-32 architecture and Intel(R) 64 architecture.

**LIBOMP_USE_INTERNODE_ALIGNMENT** = ``OFF|ON``
  Align certain data structures on 4096-byte.  This option is useful on
  multi-node systems where a small ``CACHE_LINE`` setting leads to false sharing.

**LIBOMP_OMPT_SUPPORT** = ``ON|OFF``
  Include support for the OpenMP Tools Interface (OMPT). 
  This option is supported and ``ON`` by default for x86, x86_64, AArch64, and 
  PPC64 on Linux* and macOS*.
  This option is ``OFF`` if this feature is not supported for the platform.

**LIBOMP_OMPT_OPTIONAL** = ``ON|OFF``
  Include support for optional OMPT functionality.  This option is ignored if
  **LIBOMP_OMPT_SUPPORT** is ``OFF``.

**LIBOMP_STATS** = ``OFF|ON``
  Include stats-gathering code.

**LIBOMP_USE_DEBUGGER** = ``OFF|ON``
  Include the friendly debugger interface.

**LIBOMP_USE_HWLOC** = ``OFF|ON``
  Use `OpenMPI's hwloc library <https://www.open-mpi.org/projects/hwloc/>`_ for
  topology detection and affinity.

**LIBOMP_HWLOC_INSTALL_DIR** = ``/path/to/hwloc/install/dir``
  Specify install location of hwloc.  The configuration system will look for
  ``hwloc.h`` in ``${LIBOMP_HWLOC_INSTALL_DIR}/include`` and the library in
  ``${LIBOMP_HWLOC_INSTALL_DIR}/lib``.  The default is ``/usr/local``.
  This option is only used if **LIBOMP_USE_HWLOC** is ``ON``.

Additional Compiler Flags
"""""""""""""""""""""""""

These flags are **appended**, they do not overwrite any of the preset flags.

**LIBOMP_CPPFLAGS** = <space-separated flags>
  Additional C preprocessor flags.

**LIBOMP_CFLAGS** = <space-separated flags>
  Additional C compiler flags.

**LIBOMP_CXXFLAGS** = <space-separated flags>
  Additional C++ compiler flags.

**LIBOMP_ASMFLAGS** = <space-separated flags>
  Additional assembler flags.

**LIBOMP_LDFLAGS** = <space-separated flags>
  Additional linker flags.

**LIBOMP_LIBFLAGS** = <space-separated flags>
  Additional libraries to link.

**LIBOMP_FFLAGS** = <space-separated flags>
  Additional Fortran compiler flags.

Options for ``libomptarget``
----------------------------

**LIBOMPTARGET_OPENMP_HEADER_FOLDER** = ``""``
  Path of the folder that contains ``omp.h``.  This is required for testing
  out-of-tree builds.

**LIBOMPTARGET_OPENMP_HOST_RTL_FOLDER** = ``""``
  Path of the folder that contains ``libomp.so``.  This is required for testing
  out-of-tree builds.

Options for ``NVPTX device RTL``
--------------------------------

**LIBOMPTARGET_NVPTX_ENABLE_BCLIB** = ``ON|OFF``
  Enable CUDA LLVM bitcode offloading device RTL. This is used for link time
  optimization of the OMP runtime and application code. This option is enabled
  by default if the build system determines that `CMAKE_C_COMPILER` is able to
  compile and link the library.

**LIBOMPTARGET_NVPTX_CUDA_COMPILER** = ``""``
  Location of a CUDA compiler capable of emitting LLVM bitcode. Currently only
  the Clang compiler is supported. This is only used when building the CUDA LLVM
  bitcode offloading device RTL. If unspecified and the CMake C compiler is
  Clang, then Clang is used.

**LIBOMPTARGET_NVPTX_BC_LINKER** = ``""``
  Location of a linker capable of linking LLVM bitcode objects. This is only
  used when building the CUDA LLVM bitcode offloading device RTL. If unspecified
  and the CMake C compiler is Clang and there exists a llvm-link binary in the
  directory containing Clang, then this llvm-link binary is used.

**LIBOMPTARGET_NVPTX_ALTERNATE_HOST_COMPILER** = ``""``
  Host compiler to use with NVCC. This compiler is not going to be used to
  produce any binary. Instead, this is used to overcome the input compiler
  checks done by NVCC. E.g. if using a default host compiler that is not
  compatible with NVCC, this option can be use to pass to NVCC a valid compiler
  to avoid the error.

 **LIBOMPTARGET_NVPTX_COMPUTE_CAPABILITIES** = ``35``
  List of CUDA compute capabilities that should be supported by the NVPTX
  device RTL. E.g. for compute capabilities 6.0 and 7.0, the option "60,70"
  should be used. Compute capability 3.5 is the minimum required.

 **LIBOMPTARGET_NVPTX_DEBUG** = ``OFF|ON``
  Enable printing of debug messages from the NVPTX device RTL.

Example Usages of CMake
=======================

Typical Invocations
-------------------

.. code-block:: console

  $ cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ..
  $ cmake -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ ..
  $ cmake -DCMAKE_C_COMPILER=icc -DCMAKE_CXX_COMPILER=icpc ..

Advanced Builds with Various Options
------------------------------------

- Build the i386 Linux* library using GCC*

  .. code-block:: console

    $ cmake -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DLIBOMP_ARCH=i386 ..

- Build the x86_64 debug Mac library using Clang*

  .. code-block:: console

    $ cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DLIBOMP_ARCH=x86_64 -DCMAKE_BUILD_TYPE=Debug ..

- Build the library (architecture determined by probing compiler) using the
  Intel(R) C Compiler and the Intel(R) C++ Compiler.  Also, create Fortran
  modules with the Intel(R) Fortran Compiler.

  .. code-block:: console

    $ cmake -DCMAKE_C_COMPILER=icc -DCMAKE_CXX_COMPILER=icpc -DCMAKE_Fortran_COMPILER=ifort -DLIBOMP_FORTRAN_MODULES=on ..

- Have CMake find the C/C++ compiler and specify additional flags for the C
  compiler, preprocessor, and C++ compiler.

  .. code-blocks:: console

    $ cmake -DLIBOMP_CFLAGS='-specific-flag' -DLIBOMP_CPPFLAGS='-DNEW_FEATURE=1 -DOLD_FEATURE=0' -DLIBOMP_CXXFLAGS='--one-specific-flag --two-specific-flag' ..

- Build the stubs library

  .. code-blocks:: console

    $ cmake -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DLIBOMP_LIB_TYPE=stubs ..

**Footnotes**

.. [*] Other names and brands may be claimed as the property of others.
