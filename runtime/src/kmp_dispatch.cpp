/*
 * kmp_dispatch.cpp: dynamic scheduling - iteration initialization and dispatch.
 */

/*
 * File modified by: Akan Yilmaz, Jonas H. Müller Korndörfer, Ahmed Eleliemy, Ali Mohammed, Florina M. Ciorba
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

/* Dynamic scheduling initialization and dispatch.
 *
 * NOTE: __kmp_nth is a constant inside of any dispatch loop, however
 *       it may change values between parallel regions.  __kmp_max_nth
 *       is the largest value __kmp_nth may take, 1 is the smallest.
 */

#include "kmp.h"
#include <iostream>
#include <fstream>
#include <list>
#include <unordered_map>
#include "kmp_error.h"
#include "kmp_i18n.h"
#include "kmp_itt.h"
#include "kmp_stats.h"
#include "kmp_str.h"
#if KMP_USE_X87CONTROL
#include <float.h>
#endif
#include "kmp_lock.h"
#include "kmp_dispatch.h"
#if KMP_USE_HIER_SCHED
#include "kmp_dispatch_hier.h"
#endif

#if OMPT_SUPPORT
#include "ompt-specific.h"
#endif

/* ----------------------------LB4OMP_extensions------------------------------- */
#include <math.h>
#include <shared_mutex>
#include <deque> // container for mutexes

std::deque<std::shared_timed_mutex> mutexes;
/* ----------------------------LB4OMP_extensions------------------------------- */

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */
#define LOOP_TIME_MEASURE_START if (getenv("KMP_TIME_LOOPS") !=NULL) { init_loop_timer(loc->psource, ub); } 
#define LOOP_TIME_MEASURE_END if (getenv("KMP_TIME_LOOPS") !=NULL) { print_loop_timer((int) tid); } 

#define INIT_CHUNK_RECORDING if (getenv("KMP_PRINT_CHUNKS") !=NULL) { init_chunk_sizes((int) tc); }
#define STORE_CHUNK_INFO if (getenv("KMP_PRINT_CHUNKS") !=NULL) { store_chunk_sizes((int) *p_lb, (int) *p_ub, (int) tid); } 

std::chrono::high_resolution_clock::time_point timeInit;
std::chrono::high_resolution_clock::time_point timeEnd;
std::unordered_map<std::string, std::vector<double> > means_sigmas;


// ------------------------------------- AWF data -----------------------------------------------------
typedef struct 
{
int timeStep;
std::vector<int> workPerStep;
std::vector<int> sumWorkPerStep;
std::vector<double> executionTimes;
std::vector<double> sumExecutionTimes;
std::vector<double> weights;

} AWFDataRecord;

std::unordered_map<std::string, AWFDataRecord > AWFData; // AWF weights

const char* cLoopName; //current loop name

std::atomic<int> AWFEnter = 0;
std::atomic<int> AWFWait = 1;
std::atomic<int> AWFCounter = 0;
// ...........................................................................................................

std::unordered_map<std::string, std::atomic<int> > current_index; //current+1 for mean
std::atomic<int> profilingDataReady=0;
double currentMu;
std::atomic<int> timeUpdates = 0;
std::atomic<int> chunkUpdates = 0;
//std::list<std::string> calculatedChunks;
int * chunkSizeInfo;
std::atomic<int> currentChunkIndex=-1;

std::string globalLoopline;
long globalNIterations;

std::atomic<int> chunkStart = 0;


double t1;
double t2;
double t3;

int countLoops = 0;

std::unordered_map<std::string, std::atomic<int> > currentLoopMap;

// void init_chunk_sizes(int iterations)
// {
//    // printf("iterations is %d\n", iterations);
//   int count = 0;
//   int count2 = 0;
//   count = std::atomic_fetch_add(&chunkUpdates, 1);
//   if (count == 0){
//     // chunkStart = 0;
//     chunkSizeInfo = (int *) malloc( sizeof(int) * (int)iterations * 5);
//     std::atomic_fetch_add(&currentChunkIndex, 1);
//     //currentChunkIndex++;

//    // count2 = std::atomic_fetch_add(&chunkStart, 1);
//   }
//   else{
//     //while(count2 == 0){
//       // printf("tst: %d\n", chunkStart);
//       //count2 = std::atomic_fetch_add(&chunkStart, 0);
//     //}
//     while(currentChunkIndex==-1)
//     {
      
//     }
//   }
//    // printf("Count %d, %d\n", count, currentChunkIndex);
  
// }

void init_chunk_sizes(int iterations)
{
	 // printf("iterations is %d\n", iterations);
  int count = 0;
  int count2 = 0;
  count = std::atomic_fetch_add(&chunkUpdates, 1);
  if (count == 0){
    // chunkStart = 0;
    chunkSizeInfo = (int *) malloc( sizeof(int) * (int)iterations * 5);
    // currentChunkIndex = std::atomic_fetch_add(&currentChunkIndex, 1);
    currentChunkIndex++;
    count2 = std::atomic_fetch_add(&chunkStart, 1);
  }
  else{
    while(count2 == 0){
      // printf("tst: %d\n", chunkStart);
      count2 = std::atomic_fetch_add(&chunkStart, 0);
    }
  }
	 // printf("Count %d, %d\n", count, currentChunkIndex);
  
}
void store_chunk_sizes(int p_lb, int p_ub, int tid){
	//return;
	int tindex= std::atomic_fetch_add(&currentChunkIndex, 4);
	chunkSizeInfo[tindex] = p_lb;
	chunkSizeInfo[tindex+1] = p_ub;
	chunkSizeInfo[tindex+2] = p_ub - p_lb + 1;
	chunkSizeInfo[tindex+3] = tid;
}


void read_profiling_data(std::string loopLocation){
	std::vector<std::string> loopNames;
	char *fileData= std::getenv("KMP_PROFILE_DATA");
	std::ifstream infile(fileData);
	std::string s1;
	std::string loopName;
	std::string s3;
	double mean;
	std::string s4;
	double sigma;
	std::string s5;
	int iterations;

	std::string s6;
	while(infile >> s1 >> loopName >> s3 >> mean >> s4 >> sigma >> s5 >> iterations)
	{
		 if(means_sigmas.find(loopName)==means_sigmas.end())
		 {
		 	std::vector<double> values;
		 	values.push_back(mean);
		 	values.push_back(sigma);
		    means_sigmas.insert(std::pair<std::string,std::vector<double>>(loopName, values));
		    current_index.insert( std::pair<std::string,int>(loopName, 0));
		 }
		 else
		 {
		 	means_sigmas.at(loopName).push_back(mean);
		 	means_sigmas.at(loopName).push_back(sigma);
		 }
	}
	profilingDataReady++;

}

void init_loop_timer(const char* loopLine, long ub){
  // printf("init loop timer\n");
		std::chrono::high_resolution_clock::time_point mytime;
		int count = 0;
		count = std::atomic_fetch_add(&timeUpdates, 1);
    // printf("Count init timer: %d\n", count);
		if (count == 0)
  		{
			char* fileData = std::getenv("KMP_TIME_LOOPS");
	    	if(fileData==NULL || strcmp(fileData,"")==0)
	    	{
	    		std::cout << "Please export KMP_TIME_LOOPS in your environment with the path for the storing the loop times\n";
	    		exit(-1);
	    	}
	    	// std::fstream ofs;
	    	// ofs.open(fileData, std::ofstream::out | std::ofstream::app);
	 		  // ofs << "Location: "<< loopLine << " #iterations "<< (ub+1) <<" ";
	 		  globalLoopline = loopLine;
        globalNIterations = ub+1;
	  		// ofs.close();
	  		mytime = std::chrono::high_resolution_clock::now();
        timeInit = mytime;
        // countLoops++;
        // auto searchLoopLoc = currentLoopMap.find(loopLine);
        if(currentLoopMap.find(loopLine) == currentLoopMap.end()){
          currentLoopMap.insert( std::pair<std::string,int>(loopLine, 1));
        }
        else{
          currentLoopMap.at(loopLine)++;
          // searchLoopLoc->second
          // searchLoopLoc->second = searchLoopLoc->second+1;
        }
	  	}
}

void print_loop_timer(int tid_for_timer){
	    std::chrono::high_resolution_clock::time_point mytime;
      mytime = std::chrono::high_resolution_clock::now();
	    int count = 0;
      std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(mytime - timeInit);
      char* fileData = std::getenv("KMP_TIME_LOOPS");

      if(fileData==NULL || strcmp(fileData,"")==0){
        std::cout << "Please export KMP_TIME_LOOPS in your environment with the path for the storing the loop times\n";
        exit(-1);
      }

      std::fstream ofs;
      ofs.open(fileData, std::ofstream::out | std::ofstream::app);
      ofs << "LoopOccurrence: " << currentLoopMap.at(globalLoopline) << " Location: " << globalLoopline << " #iterations " << globalNIterations << " threadID: " << tid_for_timer << " threadTime: " << time_span.count() << std::endl;
      ofs.close();  

	    count = std::atomic_fetch_sub(&timeUpdates, 1);
	    
      if ( count == 1){
		  //mytime = std::chrono::high_resolution_clock::now();
  			timeEnd = mytime;
  			std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(timeEnd - timeInit);
  			char* fileData = std::getenv("KMP_TIME_LOOPS");

          if(fileData==NULL || strcmp(fileData,"")==0){
    	    		std::cout << "Please export KMP_TIME_LOOPS in your environment with the path for the storing the loop times\n";
    	    		exit(-1);
    	    }

  	    	ofs.open(fileData, std::ofstream::out | std::ofstream::app);
  	      ofs << "Location: "<< globalLoopline << " #iterations "<< globalNIterations << " LoopTime: " << time_span.count() << std::endl;

  	      if(currentChunkIndex != -1 && chunkSizeInfo != NULL){
  				    for(int i = 0 ; i < currentChunkIndex ; i += 4){
  					      ofs << "chunkLocation: " << globalLoopline << " lower " << chunkSizeInfo[i] << " upper " << chunkSizeInfo[i+1] <<  " chunksize " << chunkSizeInfo[i+2] << " tid " << chunkSizeInfo[i+3]<< std::endl;
  				}

        	currentChunkIndex = -1;
          chunkUpdates = 0;
          chunkStart = 0;
        	free(chunkSizeInfo);
  	        	
  	    	}
  	        ofs.close();	
      }     
}


void __kmp_dispatch_deo_error(int *gtid_ref, int *cid_ref, ident_t *loc_ref) {
  kmp_info_t *th;

  KMP_DEBUG_ASSERT(gtid_ref);

  if (__kmp_env_consistency_check) {
    th = __kmp_threads[*gtid_ref];
    if (th->th.th_root->r.r_active &&
        (th->th.th_dispatch->th_dispatch_pr_current->pushed_ws != ct_none)) {
#if KMP_USE_DYNAMIC_LOCK
      __kmp_push_sync(*gtid_ref, ct_ordered_in_pdo, loc_ref, NULL, 0);
#else
      __kmp_push_sync(*gtid_ref, ct_ordered_in_pdo, loc_ref, NULL);
#endif
    }
  }
}

void __kmp_dispatch_dxo_error(int *gtid_ref, int *cid_ref, ident_t *loc_ref) {
  kmp_info_t *th;

  if (__kmp_env_consistency_check) {
    th = __kmp_threads[*gtid_ref];
    if (th->th.th_dispatch->th_dispatch_pr_current->pushed_ws != ct_none) {
      __kmp_pop_sync(*gtid_ref, ct_ordered_in_pdo, loc_ref);
    }
  }
}

// Initialize a dispatch_private_info_template<T> buffer for a particular
// type of schedule,chunk.  The loop description is found in lb (lower bound),
// ub (upper bound), and st (stride).  nproc is the number of threads relevant
// to the scheduling (often the number of threads in a team, but not always if
// hierarchical scheduling is used).  tid is the id of the thread calling
// the function within the group of nproc threads.  It will have a value
// between 0 and nproc - 1.  This is often just the thread id within a team, but
// is not necessarily the case when using hierarchical scheduling.
// loc is the source file location of the corresponding loop
// gtid is the global thread id
template <typename T>
void __kmp_dispatch_init_algorithm(ident_t *loc, int gtid,
                                   dispatch_private_info_template<T> *pr,
                                   //-----LB4OMP_extensions------
                                   dispatch_shared_info_template<T> *sh,
                                   //-----LB4OMP_extensions------
                                   enum sched_type schedule, T lb, T ub,
                                   typename traits_t<T>::signed_t st,
#if USE_ITT_BUILD
                                   kmp_uint64 *cur_chunk,
#endif
                                   typename traits_t<T>::signed_t chunk,
                                   T nproc, T tid) {
  typedef typename traits_t<T>::unsigned_t UT;
  typedef typename traits_t<T>::floating_t DBL;

  int active;
  T tc;
  
  kmp_info_t *th;
  kmp_team_t *team;
// timeUpdates = 0;
LOOP_TIME_MEASURE_START


#ifdef KMP_DEBUG
  typedef typename traits_t<T>::signed_t ST;
  {
    char *buff;
    // create format specifiers before the debug output
    buff = __kmp_str_format("__kmp_dispatch_init_algorithm: T#%%d called "
                            "pr:%%p lb:%%%s ub:%%%s st:%%%s "
                            "schedule:%%d chunk:%%%s nproc:%%%s tid:%%%s\n",
                            traits_t<T>::spec, traits_t<T>::spec,
                            traits_t<ST>::spec, traits_t<ST>::spec,
                            traits_t<T>::spec, traits_t<T>::spec);
    KD_TRACE(10, (buff, gtid, pr, lb, ub, st, schedule, chunk, nproc, tid));
    __kmp_str_free(&buff);
  }
#endif
  /* setup data */
  th = __kmp_threads[gtid];
  team = th->th.th_team;
  active = !team->t.t_serialized;

#if USE_ITT_BUILD
  int itt_need_metadata_reporting = __itt_metadata_add_ptr &&
                                    __kmp_forkjoin_frames_mode == 3 &&
                                    KMP_MASTER_GTID(gtid) &&
#if OMP_40_ENABLED
                                    th->th.th_teams_microtask == NULL &&
#endif
                                    team->t.t_active_level == 1;
#endif
#if (KMP_STATIC_STEAL_ENABLED)
  if (SCHEDULE_HAS_NONMONOTONIC(schedule))
    // AC: we now have only one implementation of stealing, so use it
    schedule = kmp_sch_static_steal;
  else
#endif
    schedule = SCHEDULE_WITHOUT_MODIFIERS(schedule);

  /* Pick up the nomerge/ordered bits from the scheduling type */
  if ((schedule >= kmp_nm_lower) && (schedule < kmp_nm_upper)) {
    pr->flags.nomerge = TRUE;
    schedule =
        (enum sched_type)(((int)schedule) - (kmp_nm_lower - kmp_sch_lower));
  } else {
    pr->flags.nomerge = FALSE;
  }
  pr->type_size = traits_t<T>::type_size; // remember the size of variables
  if (kmp_ord_lower & schedule) {
    pr->flags.ordered = TRUE;
    schedule =
        (enum sched_type)(((int)schedule) - (kmp_ord_lower - kmp_sch_lower));
  } else {
    pr->flags.ordered = FALSE;
  }


  if (schedule == kmp_sch_static) {
    schedule = __kmp_static;
  } else {
    if (schedule == kmp_sch_runtime) {
      // Use the scheduling specified by OMP_SCHEDULE (or __kmp_sch_default if
      // not specified)
      schedule = team->t.t_sched.r_sched_type;
      // Detail the schedule if needed (global controls are differentiated
      // appropriately)
      if (schedule == kmp_sch_guided_chunked) {
        schedule = __kmp_guided;
      } else if (schedule == kmp_sch_static) {
        schedule = __kmp_static;
      }
      // Use the chunk size specified by OMP_SCHEDULE (or default if not
      // specified)
      chunk = team->t.t_sched.chunk;
#if USE_ITT_BUILD
      if (cur_chunk)
        *cur_chunk = chunk;
#endif
#ifdef KMP_DEBUG
      {
        char *buff;
        // create format specifiers before the debug output
        buff = __kmp_str_format("__kmp_dispatch_init_algorithm: T#%%d new: "
                                "schedule:%%d chunk:%%%s\n",
                                traits_t<ST>::spec);
        KD_TRACE(10, (buff, gtid, schedule, chunk));
        __kmp_str_free(&buff);
      }
#endif
    } else {
      if (schedule == kmp_sch_guided_chunked) {
        schedule = __kmp_guided;
      }
      if (chunk <= 0) {
        chunk = KMP_DEFAULT_CHUNK;
      }
    }
  
    //------------------------LB4OMP_extensions------------------------
    // initialize the min chunk switcher
    pr->u.p.min_chunk = chunk;
    //------------------------LB4OMP_extensions------------------------
    

    if (schedule == kmp_sch_auto) {
      // mapping and differentiation: in the __kmp_do_serial_initialize()
      schedule = __kmp_auto;
#ifdef KMP_DEBUG
      {
        char *buff;
        // create format specifiers before the debug output
        buff = __kmp_str_format(
            "__kmp_dispatch_init_algorithm: kmp_sch_auto: T#%%d new: "
            "schedule:%%d chunk:%%%s\n",
            traits_t<ST>::spec);
        KD_TRACE(10, (buff, gtid, schedule, chunk));
        __kmp_str_free(&buff);
      }
#endif
    }

    /* guided analytical not safe for too many threads */
    if (schedule == kmp_sch_guided_analytical_chunked && nproc > 1 << 20) {
      schedule = kmp_sch_guided_iterative_chunked;
      KMP_WARNING(DispatchManyThreads);
    }
#if OMP_45_ENABLED
    if (schedule == kmp_sch_runtime_simd) {
      // compiler provides simd_width in the chunk parameter
      schedule = team->t.t_sched.r_sched_type;
      // Detail the schedule if needed (global controls are differentiated
      // appropriately)
      if (schedule == kmp_sch_static || schedule == kmp_sch_auto ||
          schedule == __kmp_static) {
        schedule = kmp_sch_static_balanced_chunked;
      } else {
        if (schedule == kmp_sch_guided_chunked || schedule == __kmp_guided) {
          schedule = kmp_sch_guided_simd;
        }
        chunk = team->t.t_sched.chunk * chunk;
      }
#if USE_ITT_BUILD
      if (cur_chunk)
        *cur_chunk = chunk;
#endif
#ifdef KMP_DEBUG
      {
        char *buff;
        // create format specifiers before the debug output
        buff = __kmp_str_format("__kmp_dispatch_init: T#%%d new: schedule:%%d"
                                " chunk:%%%s\n",
                                traits_t<ST>::spec);
        KD_TRACE(10, (buff, gtid, schedule, chunk));
        __kmp_str_free(&buff);
      }
#endif
    }
#endif // OMP_45_ENABLED
    pr->u.p.parm1 = chunk;
  }
  KMP_ASSERT2((kmp_sch_lower < schedule && schedule < kmp_sch_upper),
              "unknown scheduling type");

  pr->u.p.count = 0;

  if (__kmp_env_consistency_check) {
    if (st == 0) {
      __kmp_error_construct(kmp_i18n_msg_CnsLoopIncrZeroProhibited,
                            (pr->flags.ordered ? ct_pdo_ordered : ct_pdo), loc);
    }
  }
  // compute trip count
  if (st == 1) { // most common case
    if (ub >= lb) {
      tc = ub - lb + 1;
    } else { // ub < lb
      tc = 0; // zero-trip
    }
  } else if (st < 0) {
    if (lb >= ub) {
      // AC: cast to unsigned is needed for loops like (i=2B; i>-2B; i-=1B),
      // where the division needs to be unsigned regardless of the result type
      tc = (UT)(lb - ub) / (-st) + 1;
    } else { // lb < ub
      tc = 0; // zero-trip
    }
  } else { // st > 0
    if (ub >= lb) {
      // AC: cast to unsigned is needed for loops like (i=-2B; i<2B; i+=1B),
      // where the division needs to be unsigned regardless of the result type
      tc = (UT)(ub - lb) / st + 1;
    } else { // ub < lb
      tc = 0; // zero-trip
    }
  }
  	//printf("HELLO I AM HERE before\n");
	// Initialize array for storing the chunk sizes information
  	// if(tid==0)
  	// {
    	INIT_CHUNK_RECORDING
    	//init_chunk_sizes((int) tc);
  	// }
  	
    //printf("HELLO I AM HERE after\n");

  pr->u.p.lb = lb;
  pr->u.p.ub = ub;
  pr->u.p.st = st;
  pr->u.p.tc = tc;

#if KMP_OS_WINDOWS
  pr->u.p.last_upper = ub + st;
#endif /* KMP_OS_WINDOWS */

  /* NOTE: only the active parallel region(s) has active ordered sections */

  if (active) {
    if (pr->flags.ordered) {
      pr->ordered_bumped = 0;
      pr->u.p.ordered_lower = 1;
      pr->u.p.ordered_upper = 0;
    }
  }

  switch (schedule) {
#if (KMP_STATIC_STEAL_ENABLED)
  case kmp_sch_static_steal: {
    T ntc, init;

    KD_TRACE(100,
             ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_static_steal case\n",
              gtid));

    ntc = (tc % chunk ? 1 : 0) + tc / chunk;
    if (nproc > 1 && ntc >= nproc) {
      KMP_COUNT_BLOCK(OMP_LOOP_STATIC_STEAL);
      T id = tid;
      T small_chunk, extras;

      small_chunk = ntc / nproc;
      extras = ntc % nproc;

      init = id * small_chunk + (id < extras ? id : extras);
      pr->u.p.count = init;
      pr->u.p.ub = init + small_chunk + (id < extras ? 1 : 0);

      pr->u.p.parm2 = lb;
      // pr->pfields.parm3 = 0; // it's not used in static_steal
      pr->u.p.parm4 = (id + 1) % nproc; // remember neighbour tid
      pr->u.p.st = st;
      if (traits_t<T>::type_size > 4) {
        // AC: TODO: check if 16-byte CAS available and use it to
        // improve performance (probably wait for explicit request
        // before spending time on this).
        // For now use dynamically allocated per-thread lock,
        // free memory in __kmp_dispatch_next when status==0.
        KMP_DEBUG_ASSERT(th->th.th_dispatch->th_steal_lock == NULL);
        th->th.th_dispatch->th_steal_lock =
            (kmp_lock_t *)__kmp_allocate(sizeof(kmp_lock_t));
        __kmp_init_lock(th->th.th_dispatch->th_steal_lock);
      }
      break;
    } else {
      KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d falling-through to "
                     "kmp_sch_static_balanced\n",
                     gtid));
      schedule = kmp_sch_static_balanced;
      /* too few iterations: fall-through to kmp_sch_static_balanced */
    } // if
    /* FALL-THROUGH to static balanced */
  } // case
#endif
  case kmp_sch_static_balanced: {
    // if (tid == 0) {
    //     timeInit = __kmp_get_ticks();
    //     printf("Location: %s ",loc->psource);
    // 	// std::cout << "testTime: " << testTime << std::endl;
    // 	}
    T init, limit;

    KD_TRACE(
        100,
        ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_static_balanced case\n",
         gtid));

    if (nproc > 1) {
      T id = tid;

      if (tc < nproc) {
        if (id < tc) {
          init = id;
          limit = id;
          pr->u.p.parm1 = (id == tc - 1); /* parm1 stores *plastiter */
        } else {
          pr->u.p.count = 1; /* means no more chunks to execute */
          pr->u.p.parm1 = FALSE;
          break;
        }
      } else {
        T small_chunk = tc / nproc;
        T extras = tc % nproc;
        init = id * small_chunk + (id < extras ? id : extras);
        limit = init + small_chunk - (id < extras ? 0 : 1);
        pr->u.p.parm1 = (id == nproc - 1);
      }
    } else {
      if (tc > 0) {
        init = 0;
        limit = tc - 1;
        pr->u.p.parm1 = TRUE;
      } else {
        // zero trip count
        pr->u.p.count = 1; /* means no more chunks to execute */
        pr->u.p.parm1 = FALSE;
        break;
      }
    }
#if USE_ITT_BUILD
    // Calculate chunk for metadata report
    if (itt_need_metadata_reporting)
      if (cur_chunk)
        *cur_chunk = limit - init + 1;
#endif
    if (st == 1) {
      pr->u.p.lb = lb + init;
      pr->u.p.ub = lb + limit;
    } else {
      // calculated upper bound, "ub" is user-defined upper bound
      T ub_tmp = lb + limit * st;
      pr->u.p.lb = lb + init * st;
      // adjust upper bound to "ub" if needed, so that MS lastprivate will match
      // it exactly
      if (st > 0) {
        pr->u.p.ub = (ub_tmp + st > ub ? ub : ub_tmp);
      } else {
        pr->u.p.ub = (ub_tmp + st < ub ? ub : ub_tmp);
      }
    }
    if (pr->flags.ordered) {
      pr->u.p.ordered_lower = init;
      pr->u.p.ordered_upper = limit;
    }
    break;
  } // case
#if OMP_45_ENABLED
  case kmp_sch_static_balanced_chunked: {
    // similar to balanced, but chunk adjusted to multiple of simd width
    T nth = nproc;
    KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d runtime(simd:static)"
                   " -> falling-through to static_greedy\n",
                   gtid));
    schedule = kmp_sch_static_greedy;
    if (nth > 1)
      pr->u.p.parm1 = ((tc + nth - 1) / nth + chunk - 1) & ~(chunk - 1);
    else
      pr->u.p.parm1 = tc;
    break;
  } // case
  case kmp_sch_guided_simd:
#endif // OMP_45_ENABLED
  case kmp_sch_guided_iterative_chunked: {
    KD_TRACE(
        100,
        ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_guided_iterative_chunked"
         " case\n",
         gtid));

    if (nproc > 1) {
      if ((2L * chunk + 1) * nproc >= tc) {
        /* chunk size too large, switch to dynamic */
        schedule = kmp_sch_dynamic_chunked;
      } else {
        // when remaining iters become less than parm2 - switch to dynamic
        pr->u.p.parm2 = guided_int_param * nproc * (chunk + 1);
        *(double *)&pr->u.p.parm3 =
            guided_flt_param / nproc; // may occupy parm3 and parm4
      }
    } else {
      KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d falling-through to "
                     "kmp_sch_static_greedy\n",
                     gtid));
      schedule = kmp_sch_static_greedy;
      /* team->t.t_nproc == 1: fall-through to kmp_sch_static_greedy */
      KD_TRACE(
          100,
          ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_static_greedy case\n",
           gtid));
      pr->u.p.parm1 = tc;
    } // if
  } // case
  break;
  case kmp_sch_guided_analytical_chunked: {
    KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d "
                   "kmp_sch_guided_analytical_chunked case\n",
                   gtid));

    if (nproc > 1) {
      if ((2L * chunk + 1) * nproc >= tc) {
        /* chunk size too large, switch to dynamic */
        schedule = kmp_sch_dynamic_chunked;
      } else {
        /* commonly used term: (2 nproc - 1)/(2 nproc) */
        DBL x;

#if KMP_USE_X87CONTROL
        /* Linux* OS already has 64-bit computation by default for long double,
           and on Windows* OS on Intel(R) 64, /Qlong_double doesn't work. On
           Windows* OS on IA-32 architecture, we need to set precision to 64-bit
           instead of the default 53-bit. Even though long double doesn't work
           on Windows* OS on Intel(R) 64, the resulting lack of precision is not
           expected to impact the correctness of the algorithm, but this has not
           been mathematically proven. */
        // save original FPCW and set precision to 64-bit, as
        // Windows* OS on IA-32 architecture defaults to 53-bit
        unsigned int oldFpcw = _control87(0, 0);
        _control87(_PC_64, _MCW_PC); // 0,0x30000
#endif
        /* value used for comparison in solver for cross-over point */
        long double target = ((long double)chunk * 2 + 1) * nproc / tc;

        /* crossover point--chunk indexes equal to or greater than
           this point switch to dynamic-style scheduling */
        UT cross;

        /* commonly used term: (2 nproc - 1)/(2 nproc) */
        x = (long double)1.0 - (long double)0.5 / nproc;

#ifdef KMP_DEBUG
        { // test natural alignment
          struct _test_a {
            char a;
            union {
              char b;
              DBL d;
            };
          } t;
          ptrdiff_t natural_alignment =
              (ptrdiff_t)&t.b - (ptrdiff_t)&t - (ptrdiff_t)1;
          //__kmp_warn( " %llx %llx %lld", (long long)&t.d, (long long)&t, (long
          // long)natural_alignment );
          KMP_DEBUG_ASSERT(
              (((ptrdiff_t)&pr->u.p.parm3) & (natural_alignment)) == 0);
        }
#endif // KMP_DEBUG

        /* save the term in thread private dispatch structure */
        *(DBL *)&pr->u.p.parm3 = x;

        /* solve for the crossover point to the nearest integer i for which C_i
           <= chunk */
        {
          UT left, right, mid;
          long double p;

          /* estimate initial upper and lower bound */

          /* doesn't matter what value right is as long as it is positive, but
             it affects performance of the solver */
          right = 229;
          p = __kmp_pow<UT>(x, right);
          if (p > target) {
            do {
              p *= p;
              right <<= 1;
            } while (p > target && right < (1 << 27));
            /* lower bound is previous (failed) estimate of upper bound */
            left = right >> 1;
          } else {
            left = 0;
          }

          /* bisection root-finding method */
          while (left + 1 < right) {
            mid = (left + right) / 2;
            if (__kmp_pow<UT>(x, mid) > target) {
              left = mid;
            } else {
              right = mid;
            }
          } // while
          cross = right;
        }
        /* assert sanity of computed crossover point */
        KMP_ASSERT(cross && __kmp_pow<UT>(x, cross - 1) > target &&
                   __kmp_pow<UT>(x, cross) <= target);

        /* save the crossover point in thread private dispatch structure */
        pr->u.p.parm2 = cross;

// C75803
#if ((KMP_OS_LINUX || KMP_OS_WINDOWS) && KMP_ARCH_X86) && (!defined(KMP_I8))
#define GUIDED_ANALYTICAL_WORKAROUND (*(DBL *)&pr->u.p.parm3)
#else
#define GUIDED_ANALYTICAL_WORKAROUND (x)
#endif
        /* dynamic-style scheduling offset */
        pr->u.p.count = tc - __kmp_dispatch_guided_remaining(
                                 tc, GUIDED_ANALYTICAL_WORKAROUND, cross) -
                        cross * chunk;
#if KMP_USE_X87CONTROL
        // restore FPCW
        _control87(oldFpcw, _MCW_PC);
#endif
      } // if
    } else {
      KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d falling-through to "
                     "kmp_sch_static_greedy\n",
                     gtid));
      schedule = kmp_sch_static_greedy;
      /* team->t.t_nproc == 1: fall-through to kmp_sch_static_greedy */
      pr->u.p.parm1 = tc;
    } // if
  } // case
  break;
  case kmp_sch_static_greedy:
    KD_TRACE(
        100,
        ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_static_greedy case\n",
         gtid));
    pr->u.p.parm1 = (nproc > 1) ? (tc + nproc - 1) / nproc : tc;
    break;
  case kmp_sch_static_chunked:
  case kmp_sch_dynamic_chunked:
    if (pr->u.p.parm1 <= 0) {
      pr->u.p.parm1 = KMP_DEFAULT_CHUNK;
    }
    KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d "
                   "kmp_sch_static_chunked/kmp_sch_dynamic_chunked cases\n",
                   gtid));
    break;
  case kmp_sch_trapezoidal: {
    /* TSS: trapezoid self-scheduling, minimum chunk_size = parm1 */

    T parm1, parm2, parm3, parm4;
    KD_TRACE(100,
             ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_trapezoidal case\n",
              gtid));

    parm1 = chunk;

    /* F : size of the first cycle */
    parm2 = (tc / (2 * nproc));

    if (parm2 < 1) {
      parm2 = 1;
    }

    /* L : size of the last cycle.  Make sure the last cycle is not larger
       than the first cycle. */
    if (parm1 < 1) {
      parm1 = 1;
    } else if (parm1 > parm2) {
      parm1 = parm2;
    }

    /* N : number of cycles */
    parm3 = (parm2 + parm1);
    parm3 = (2 * tc + parm3 - 1) / parm3;

    if (parm3 < 2) {
      parm3 = 2;
    }

    /* sigma : decreasing incr of the trapezoid */
    parm4 = (parm3 - 1);
    parm4 = (parm2 - parm1) / parm4;

    // pointless check, because parm4 >= 0 always
    // if ( parm4 < 0 ) {
    //    parm4 = 0;
    //}

    pr->u.p.parm1 = parm1;
    pr->u.p.parm2 = parm2;
    pr->u.p.parm3 = parm3;
    pr->u.p.parm4 = parm4;
  } // case
  break;
    /* --------------------------LB4OMP_extensions----------------------------- */
  case kmp_sch_fsc: {
	if (tid == 0 && means_sigmas.find(loc->psource)==means_sigmas.end()){
		read_profiling_data(loc->psource);
	}

	while(profilingDataReady != 1){

	}
	t1 = 0;
	t2 = 0;
	double sigma = means_sigmas.at(loc->psource).at(2*(current_index.at(loc->psource)/(int)nproc)+1);
	current_index.at(loc->psource)++;
	// if(tid==0)
	// {
	
	//std::cout << "SIGMA-VALUE: " << sigma << std::endl;
	// printf("SIGMA-VALUE %lf  nproc %d tid %d \n", sigma, (int)nproc, tid) ;
	// }
	// DBL sigma = std::stod(getenv("KMP_SIGMA"));
    /* Fixed Size Chunking (FSC) using sigma and overhead inputs */
    int parm1;
    KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_fsc case\n",
                   gtid));

    T P = nproc;
    T N = tc;
    // DBL sigma = __kmp_env_sigma;
    
    T h = __kmp_env_overhead;

    double u = sqrt(2.0) * N * h;
    double l = sigma * P * sqrt(log((double)P));
    parm1 = pow(u / l, 2.0 / 3.0); // chunk size
    // std::cout << "CHUNK-SIZE: " << parm1 << std::endl;
    if (chunk > 0 && parm1 < chunk) { // min chunk size
      parm1 = chunk;
    }
    pr->u.p.parm1 = parm1;
    if (pr->u.p.parm1 <= 0) {
      pr->u.p.parm1 = KMP_DEFAULT_CHUNK;
    }
  } // case
  break;
  case kmp_sch_tap: {
    /* Taper (TAP) using alpha as a scaling factor */
    if (tid == 0 && means_sigmas.find(loc->psource)==means_sigmas.end()){
		read_profiling_data(loc->psource);
	}

	while(profilingDataReady != 1){

	}
	double sigma = means_sigmas.at(loc->psource).at(2*(current_index.at(loc->psource)/(int)nproc)+1);
	T mu = means_sigmas.at(loc->psource).at(2*(current_index.at(loc->psource)/(int)nproc));
	current_index.at(loc->psource)++;
	// if(tid==0)
	// {
	
	// // std::cout << "SIGMA-VALUE: " << sigma << std::endl;
	// printf("MU-VALUE %d nproc %d tid %d \n", mu, (int)nproc, tid) ;
	// }
    KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_tap case\n",
                   gtid));

    T P = nproc;
    // DBL sigma = __kmp_env_sigma;
    // DBL sigma = std::stod(getenv("KMP_SIGMA"));
    // std::cout << std::endl << "SIGMA-VALUE: " << sigma << std::endl;
    // T mu = __kmp_env_mu;
    // T mu = std::stoi(getenv("KMP_MU")); 
    // std::cout << std::endl << "MU-VALUE: " << mu << std::endl;

    double alpha = __kmp_env_alpha;
    double v = alpha * sigma / mu;

    pr->u.p.parm1 = chunk; // min chunk size
    pr->u.p.dbl_parm1 = v; // scaling factor
    if (P > 0) {
      pr->u.p.dbl_parm2 = 1.0 / (double)P; // decreasing factor
    }
    pr->u.p.dbl_parm3 = pow(v, 2.0) / 2.0; // v^2/2
    pr->u.p.dbl_parm4 = pow(v, 2.0) / 4.0; // v^2/4
  } // case
  break;
  case kmp_sch_fac: {
    if (tid == 0 && means_sigmas.find(loc->psource)==means_sigmas.end()){
		read_profiling_data(loc->psource);
	}

	while(profilingDataReady != 1){

	}
	double sigma = means_sigmas.at(loc->psource).at(2*(current_index.at(loc->psource)/(int)nproc)+1);
	T mu = means_sigmas.at(loc->psource).at(2*(current_index.at(loc->psource)/(int)nproc));
	current_index.at(loc->psource)++;
    /* Factoring (FAC) using batched scheduling */
    T parm1; // current chunk size
    T parm2 = 0; // current batch index
    T parm3 = 0; // debug local chunk index
    DBL dbl_parm1; // b_factor to be multiplied by 1/sqrt(R)

    KD_TRACE(100,
             ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_fac case\n", gtid));

    T P = nproc;
    T N = tc;
    // DBL sigma = __kmp_env_sigma;
    // DBL sigma = std::stod(getenv("KMP_SIGMA"));
      // std::cout << std::endl << "SIGMA-VALUE: " << sigma << std::endl;
    // T mu = __kmp_env_mu;
    // T mu = std::stoi(getenv("KMP_MU")); 
      // std::cout << std::endl << "MU-VALUE: " << mu << std::endl;


    dbl_parm1 = ((double)P * sigma) / (2.0 * mu);
    double b_0 = dbl_parm1 * 1 / sqrt(N); // initial b
    double x_0 = 1 + pow(b_0, 2.0) + b_0 * sqrt(pow(b_0, 2.0) + 2);
    parm1 = ceil(N / (x_0 * P)); // initial chunk size

    if (parm1 > N) { // chunk size too large
      parm1 = N;
    }

    pr->u.p.parm1 = parm1;
    pr->u.p.parm2 = parm2;
    pr->u.p.parm3 = parm3;
    pr->u.p.parm4 = chunk;
    pr->u.p.dbl_parm1 = dbl_parm1;

    // init shared variables first time
    std::lock_guard<std::mutex> lg(sh->u.s.mtx);
    if (!sh->u.s.initialized) { // I'm the first
      sh->u.s.counter = 0;
      sh->u.s.q_counter = P;
      sh->u.s.chunk_size = parm1;
      sh->u.s.batch = 0;
      sh->u.s.initialized = true; // reset done
    }
  } // case
  break;
  case kmp_sch_faca: {
    /* Factoring variant a (FACa) using redundant chunk calculation */
    if (tid == 0 && means_sigmas.find(loc->psource)==means_sigmas.end()){
		read_profiling_data(loc->psource);
	}

	while(profilingDataReady != 1){

	}
	double sigma = means_sigmas.at(loc->psource).at(2*(current_index.at(loc->psource)/(int)nproc)+1);
	T mu = means_sigmas.at(loc->psource).at(2*(current_index.at(loc->psource)/(int)nproc));
	current_index.at(loc->psource)++;

    T parm1; // current chunk size
    T parm2 = 0; // current batch index
    T parm3 = tc; // local remaining
    DBL dbl_parm1; // b_factor to be multiplied by 1/sqrt(R)

    KD_TRACE(100,
             ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_faca case\n", gtid));

    T P = nproc;
    T N = tc;

    // DBL sigma = __kmp_env_sigma;
    // DBL sigma = std::stod(getenv("KMP_SIGMA"));
     // std::cout << std::endl << "SIGMA-VALUE: " << sigma << std::endl;
    // T mu = __kmp_env_mu;
    // T mu = std::stoi(getenv("KMP_MU")); 
     // std::cout << std::endl << "MU-VALUE: " << mu << std::endl;

    dbl_parm1 = ((double)P * sigma) / (2.0 * mu);

    double b_0 = dbl_parm1 * 1 / sqrt(N); // initial b
    double x_0 = 1 + pow(b_0, 2.0) + b_0 * sqrt(pow(b_0, 2.0) + 2);
    parm1 = ceil(N / (x_0 * P)); // initial chunk size

    if (parm1 > N) { // chunk size too large
      parm1 = N;
    }

    pr->u.p.parm1 = parm1;
    pr->u.p.parm2 = parm2;
    pr->u.p.parm3 = parm3;
    pr->u.p.parm4 = chunk;
    pr->u.p.dbl_parm1 = dbl_parm1;
  } // case
  break;
  case kmp_sch_fac2: {
    /* FAC2 similar to FACa (formula version) */
    // double testTime  = 0;
    // LOOP_TIME_MEASURE_START
    DBL dbl_parm1; // factor to be multiplied by R

    KD_TRACE(100,
             ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_fac2 case\n", gtid));

    T P = nproc;
    T N = tc;
    dbl_parm1 = (double)N / (double)P;

    pr->u.p.parm1 = chunk;
    pr->u.p.dbl_parm1 = dbl_parm1;
  } // case
  break;
  case kmp_sch_fac2a: {
    /* FAC2a very similar to FACa (loop version) */
    T parm1; // current chunk size
    T parm2 = 0; // current batch index
    DBL dbl_parm1; // factor to be multiplied by chunk

    KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_fac2a case\n",
                   gtid));

    T P = nproc;
    T N = tc;
    dbl_parm1 = 1.0 / 2.0;

    parm1 = ceil(dbl_parm1 * N / (double)P); // initial chunk size

    if (parm1 > N) { // chunk size too large
      parm1 = N;
    }

    pr->u.p.parm1 = parm1;
    pr->u.p.parm2 = parm2;
    pr->u.p.parm3 = chunk;
    pr->u.p.dbl_parm1 = dbl_parm1;
  } // case
  break;
  case kmp_sch_wf: {
    /* Weighted Factoring same as FAC2a but weighted */
    T parm1; // current chunk size
    T parm2 = 0; // current batch index
    DBL dbl_parm1; // factor to be multiplied by chunk
    DBL dbl_parm2; // my weight

    KD_TRACE(100,
             ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_wf case\n", gtid));

    T P = nproc;
    T N = tc;
    dbl_parm1 = 1.0 / 2.0;

    // init the weight
    std::vector<double> weights = __kmp_env_weights;
    // don't compare sum to P, user can decide
    if ((T)weights.size() == P) {
      dbl_parm2 = weights[tid];
    } else {
      dbl_parm2 = 1.0;
    }

    parm1 = ceil(dbl_parm1 * N / (double)P); // initial chunk size

    if (parm1 > N) { // chunk size too large
      parm1 = N;
    }

    pr->u.p.parm1 = parm1;
    pr->u.p.parm2 = parm2;
    pr->u.p.parm3 = chunk;
    pr->u.p.dbl_parm1 = dbl_parm1;
    pr->u.p.dbl_parm2 = dbl_parm2;
  } // case
  break;
case kmp_sch_fac2b:{
	/* fac2b with truly DCA**/
        KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_fac2b case\n",gtid));
	if(chunk<=0)
		chunk=1;
        pr->u.p.parm1= chunk;
}
break;
case kmp_sch_rnd:{
	/* random between start_range or (1)   and min_chunk*/
	 KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_rnd case\n", gtid));
	int start_range=1;
        if(getenv("KMP_RND_START") !=NULL)
		start_range=atoi(getenv("KMP_RND_START"));
	if(chunk>0)
		 pr->u.p.parm2=chunk;
	else
		 pr->u.p.parm2 = tc-1;

	 pr->u.p.parm1= start_range;
}
break;
case kmp_sch_viss:{
        /* variable increase size chunk*/
        KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_viss case\n", gtid));
        int viss_param=nproc;
	if(getenv("KMP_VISS_PARAM") !=NULL)
                viss_param= atoi(getenv("KMP_VISS_PARAM"));
	 pr->u.p.parm1 = ceil(tc/(viss_param*nproc));
}
break;
case kmp_sch_fiss:{
	/* fixed increase size chunk*/
 	KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_fiss case\n", gtid));
	int fiss_stages=nproc-1;
      if(getenv("KMP_FISS_STAGES") !=NULL)
                fiss_stages= atoi(getenv("KMP_FISS_STAGES"));
	double X = 2.0+fiss_stages;
        double fiss_chunk = floor(tc/(X*nproc));
	double temp1= 2* tc * (1 - (fiss_stages/X));
    	double temp2 = (nproc * fiss_stages * (fiss_stages-1));
    	double fiss_delta= floor(temp1/temp2);
    	if(fiss_delta<0)
        	fiss_delta=0;
	if (fiss_chunk <chunk)
	{
		fiss_chunk=chunk;
	}
        pr->u.p.parm1 = fiss_delta;
	pr->u.p.parm2 = fiss_chunk;
}
break;
case kmp_sch_mfsc:{
	/* modified fsc*/
        KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_mfsc case\n", gtid));
	int temp = (tc+nproc-1)/nproc;
    	int chunk_mfsc = (int) (0.55+temp*log(2.0)/log((1.0*temp)));
         if(chunk<=0)
                chunk=1;
	if(chunk_mfsc<chunk)
		chunk_mfsc= chunk;
	pr->u.p.parm1 = chunk_mfsc;
}

break;
case kmp_sch_tfss:{
	/* trapezoid factoring self-scheduling*/
	KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_tfss case\n", gtid));
	double tss_chunk = ceil((double) tc / ((double) 2*nproc)); 
        double steps  = ceil(2.0*tc/(tss_chunk+1)); //n=2N/f+l
        double tss_delta = (double) (tss_chunk - 1)/(double) (steps-1);
	pr->u.p.parm1 = tss_chunk;
	pr->u.p.parm2 = tss_delta;
	 if(chunk<=0)
                chunk=1;
	pr->u.p.parm3 = chunk;
	pr->u.p.parm4 = (ceil(2.0*tc/(tss_chunk+1)))-1;
	
}
break;
  case kmp_sch_pls: {
     /* performance-based loop scheduling it divides the entire space into two parts
      one part to be assigned statically and the other part will be assigned dynamically and following as in gss*/
      KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_pls case\n", gtid));
      double ratio=0.5;
      if(getenv("KMP_PLS_SWR") !=NULL)
     		ratio= atof(getenv("KMP_PLS_SWR"));
      pr->u.p.parm3=ratio;
      //P =(ST) nproc; // number of threads
      int static_part=(int)ceil(tc*ratio/nproc); // hard coded for the moment
      pr->u.p.parm1 = static_part;
	if(chunk<=0)
		chunk=1;
      pr->u.p.parm2 = chunk; // minimum chunk size
      pr->u.p.parm3=ratio;
     // printf("minimum is %d ratio %lf static part %d\n",pr->u.p.parm2, ratio,static_part );
  }
  break;
  case kmp_sch_awf: {
    /* Adaptive Weighted Factoring same as WF but adaptive for time-stepping applications */
    //set the loop name
    cLoopName = loc->psource;
   
    T parm1; // current chunk size
    T parm2 = 0; // current batch index
    DBL dbl_parm1; // factor to be multiplied by chunk
    DBL dbl_parm2; // my weight
    t1 = __kmp_get_ticks2();
    KD_TRACE(100,
             ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_awf case\n", gtid));

    T P = nproc;
    T N = tc;
    dbl_parm1 = 1.0 / 2.0;

    if (std::atomic_fetch_add(&AWFEnter,1) == 0)
    {
        // init the weight ....check loop record for adaptive weights
        if (AWFData.find(cLoopName) == AWFData.end()) //if no data about this loop
        {

            std::vector<int> wS(nproc,0);
            std::vector<int> sWS(nproc, 0);
            std::vector<double> eT(nproc,0.0);
            std::vector<double> sET(nproc, 0.0);
            std::vector<double> w;
            //check initial weights
            if (__kmp_env_weights.size() == nproc)
            { w =  __kmp_env_weights;}
            else
            { 
               std::vector<double> defaultweights(nproc, 1.0);
               w = defaultweights;
            }

            //set a new loop record and set autoSearch to 1
            AWFDataRecord data = 
            {
                0, //timeStep
               wS, //workPerStep
              sWS, //sumWorkPerStep
               eT, //excutionTimes
              sET, //sumExecutionTimes
                w //weights   
            }; 

            //create a new record
            AWFData.insert(std::pair<std::string,AWFDataRecord>(cLoopName, data));
         }
        
         //increment time-step
         AWFData.at(cLoopName).timeStep++;

         std::atomic_fetch_sub(&AWFWait,1); //allow other threads to continue execution

     }
     else  //others should wait until we update the selected schedule
     {
          while(std::atomic_fetch_sub(&AWFWait,0) == 1)
          ;

          //printf("tid: %d ...waited \n", tid);
       
     }    


    // Read thread weight
    dbl_parm2 = AWFData.at(cLoopName).weights[tid];
 
    //record the starting time for each thread
    AWFData.at(cLoopName).executionTimes[tid] = __kmp_get_ticks2();
 

    parm1 = ceil(dbl_parm1 * N / (double)P); // initial chunk size

    if (parm1 > N) { // chunk size too large
      parm1 = N;
    }

    pr->u.p.parm1 = parm1;
    pr->u.p.parm2 = parm2;
    pr->u.p.parm3 = chunk;
    pr->u.p.dbl_parm1 = dbl_parm1;
    pr->u.p.dbl_parm2 = dbl_parm2;
  } // case
  break;
  case kmp_sch_bold: {
    if (tid == 0 && means_sigmas.find(loc->psource)==means_sigmas.end()){
		read_profiling_data(loc->psource);
		
	}

	while(profilingDataReady != 1){

	}
	double sigma = means_sigmas.at(loc->psource).at(2*(current_index.at(loc->psource)/(int)nproc)+1);
	T mu = means_sigmas.at(loc->psource).at(2*(current_index.at(loc->psource)/(int)nproc));
	currentMu = mu;
	current_index.at(loc->psource)++;
    /* The bold strategy */
    T my_bold = 0; // current chunk size of this PU
    DBL my_speed = 0; // current speed of this PU (iters/microseconds)
    kmp_int64 my_time = 0; // starting time of a chunk of this PU
    DBL a; // a
    DBL b; // b
    DBL ln_b = 0; // ln_b
    DBL p_inv; // p_inv
    DBL c1; // c1
    DBL c2; // c2
    DBL c3; // c3

    KD_TRACE(100,
             ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_bold case\n", gtid));

    T P = nproc;
    T N = tc;
    // DBL sigma = __kmp_env_sigma;
    // DBL sigma = std::stod(getenv("KMP_SIGMA"));
     // std::cout << std::endl << "SIGMA-VALUE: " << sigma << std::endl;
    // T mu = __kmp_env_mu;
    // T mu = std::stoi(getenv("KMP_MU")); 
     // std::cout << std::endl << "MU-VALUE: " << mu << std::endl;
    T h = __kmp_env_overhead;

    // algorithm 1, initialization
    a = 2.0 * pow((double)sigma / mu, 2.0);
    b = 8.0 * a * log(8 * a);
    if (b > 0) {
      ln_b = log(b);
    }
    p_inv = 1.0 / P;
    c1 = h / (mu * log(2));
    c2 = sqrt(2 * M_PI) * c1;
    c3 = log(c2);

    //my_time = __kmp_get_micros();
    //my_time = __kmp_get_nanos();
    my_time = __kmp_get_ticks();

    pr->u.p.parm1 = my_bold;
    pr->u.p.l_parm1 = my_time;
    pr->u.p.parm3 = chunk;
    pr->u.p.dbl_parm1 = a;
    pr->u.p.dbl_parm2 = b;
    pr->u.p.dbl_parm3 = ln_b;
    pr->u.p.dbl_parm4 = p_inv;
    pr->u.p.dbl_parm5 = c1;
    pr->u.p.dbl_parm6 = c2;
    pr->u.p.dbl_parm7 = c3;
    pr->u.p.dbl_parm8 = my_speed;

    // reset shared variables first time
    std::lock_guard<std::mutex> lg(sh->u.s.mtx);
    if (!sh->u.s.initialized) { // I'm the first
      sh->u.s.bold_m = N;
      sh->u.s.bold_n = N;
      sh->u.s.total_speed = 0;
      sh->u.s.bold_time = my_time;
      sh->u.s.initialized = true; // reset done
    }
  } // case
  break;
  case kmp_sch_awf_b: {
    /* Adaptive Weighted Factoring similar to WF but adaptive */
    /* This variation schedules remaining iterations by batches. */
    T parm1; // current chunk size
    T parm2 = 0; // current batch index
    T parm3 = 0; // my chunk index
    DBL dbl_parm1; // factor to be multiplied by chunk
    DBL dbl_parm2; // my weight
    DBL dbl_parm3 = 0; // total time spent (micro s.) for my executed chunks
    DBL dbl_parm4 = 0; // all my executed iterations of past chunks
    kmp_int64 my_time; // starting time of a chunk of this PU

    KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_awf_b case\n",
                   gtid));

    T P = nproc;
    T N = tc;
    dbl_parm1 = 1.0 / 2.0;

    // init the weight
    std::vector<double> weights = __kmp_env_weights;
    if ((T)weights.size() == P) {
      dbl_parm2 = weights[tid];
    } else {
      dbl_parm2 = 1.0;
    }

    parm1 = ceil(dbl_parm1 * N / (double)P); // initial chunk size

    if (parm1 > N) { // chunk size too large
      parm1 = N;
    }

    // my_time = __kmp_get_micros();
    my_time = __kmp_get_ticks();

    pr->u.p.parm1 = parm1;
    pr->u.p.parm2 = parm2;
    pr->u.p.parm3 = parm3;
    pr->u.p.parm4 = chunk;
    pr->u.p.l_parm1 = my_time;
    pr->u.p.dbl_parm1 = dbl_parm1;
    pr->u.p.dbl_parm2 = dbl_parm2;
    pr->u.p.dbl_parm3 = dbl_parm3;
    pr->u.p.dbl_parm4 = dbl_parm4;

    // reset shared variables first time
    std::lock_guard<std::mutex> lg(sh->u.s.mtx);
    if (!sh->u.s.initialized) { // I'm the first
      if ((T)mutexes.size() != P)
        mutexes.resize(P);
      if ((T)sh->u.s.dbl_vector.size() != P)
        sh->u.s.dbl_vector.resize(P);
      std::fill(sh->u.s.dbl_vector.begin(), sh->u.s.dbl_vector.end(), 0);
      sh->u.s.initialized = true; // reset done
    }
  } // case
  break;
  case kmp_sch_awf_c: {
    /* Adaptive Weighted Factoring similar to WF but adaptive */
    /* This variation schedules remaining iterations by chunks. */
    T parm1; // current chunk size
    T parm3 = 0; // my chunk index
    DBL dbl_parm1; // factor to be multiplied by remaining
    DBL dbl_parm2; // my weight
    DBL dbl_parm3 = 0; // total time spent (micro s.) for my executed chunks
    DBL dbl_parm4 = 0; // all my executed iterations of past chunks
    kmp_int64 my_time; // starting time of a chunk of this PU

    KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_awf_c case\n",
                   gtid));

    T P = nproc;
    T N = tc;
    dbl_parm1 = 1.0 / (2.0 * P);

    // init the weight
    std::vector<double> weights = __kmp_env_weights;
    if ((T)weights.size() == P) {
      dbl_parm2 = weights[tid];
    } else {
      dbl_parm2 = 1.0;
    }

    parm1 = ceil(dbl_parm1 * N); // initial chunk size

    if (parm1 > N) { // chunk size too large
      parm1 = N;
    }

    // my_time = __kmp_get_micros();
    my_time = __kmp_get_ticks();

    pr->u.p.parm1 = parm1;
    pr->u.p.parm2 = chunk;
    pr->u.p.parm3 = parm3;
    pr->u.p.l_parm1 = my_time;
    pr->u.p.dbl_parm1 = dbl_parm1;
    pr->u.p.dbl_parm2 = dbl_parm2;
    pr->u.p.dbl_parm3 = dbl_parm3;
    pr->u.p.dbl_parm4 = dbl_parm4;

    // reset shared variables first time
    std::lock_guard<std::mutex> lg(sh->u.s.mtx);
    if (!sh->u.s.initialized) { // I'm the first
      if ((T)mutexes.size() != P)
        mutexes.resize(P);
      if ((T)sh->u.s.dbl_vector.size() != P)
        sh->u.s.dbl_vector.resize(P);
      std::fill(sh->u.s.dbl_vector.begin(), sh->u.s.dbl_vector.end(), 0);
      sh->u.s.initialized = true; // reset done
    }
  } // case
  break;
  case kmp_sch_awf_d: {
    /* Adaptive Weighted Factoring similar to WF but adaptive */
    /* This variation schedules remaining iterations by batches. */
    T parm1; // current chunk size
    T parm2 = 0; // current batch index
    T parm3 = 0; // my chunk index
    DBL dbl_parm1; // factor to be multiplied by chunk
    DBL dbl_parm2; // my weight
    DBL dbl_parm3 = 0; // total time spent (micro s.) for my executed chunks
    DBL dbl_parm4 = 0; // all my executed iterations of past chunks
    kmp_int64 my_time; // starting time of a chunk of this PU

    KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_awf_d case\n",
                   gtid));

    T P = nproc;
    T N = tc;
    dbl_parm1 = 1.0 / 2.0;

    // init the weight
    std::vector<double> weights = __kmp_env_weights;
    if ((T)weights.size() == P) {
      dbl_parm2 = weights[tid];
    } else {
      dbl_parm2 = 1.0;
    }

    parm1 = ceil(dbl_parm1 * N / (double)P); // initial chunk size

    if (parm1 > N) { // chunk size too large
      parm1 = N;
    }

    // my_time = __kmp_get_micros();
    my_time = __kmp_get_ticks();

    pr->u.p.parm1 = parm1;
    pr->u.p.parm2 = parm2;
    pr->u.p.parm3 = parm3;
    pr->u.p.parm4 = chunk;
    pr->u.p.l_parm1 = my_time;
    pr->u.p.dbl_parm1 = dbl_parm1;
    pr->u.p.dbl_parm2 = dbl_parm2;
    pr->u.p.dbl_parm3 = dbl_parm3;
    pr->u.p.dbl_parm4 = dbl_parm4;

    // reset shared variables first time
    std::lock_guard<std::mutex> lg(sh->u.s.mtx);
    if (!sh->u.s.initialized) { // I'm the first
      if ((T)mutexes.size() != P)
        mutexes.resize(P);
      if ((T)sh->u.s.dbl_vector.size() != P)
        sh->u.s.dbl_vector.resize(P);
      std::fill(sh->u.s.dbl_vector.begin(), sh->u.s.dbl_vector.end(), 0);
      sh->u.s.initialized = true; // reset done
    }
  } // case
  break;
  case kmp_sch_awf_e: {
    /* Adaptive Weighted Factoring similar to WF but adaptive */
    /* This variation schedules remaining iterations by chunks. */
    T parm1; // current chunk size
    T parm3 = 0; // my chunk index
    DBL dbl_parm1; // factor to be multiplied by remaining
    DBL dbl_parm2; // my weight
    DBL dbl_parm3 = 0; // total time spent (micro s.) for my executed chunks
    DBL dbl_parm4 = 0; // all my executed iterations of past chunks
    kmp_int64 my_time; // starting time of a chunk of this PU

    KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_awf_e case\n",
                   gtid));

    T P = nproc;
    T N = tc;
    dbl_parm1 = 1.0 / (2.0 * P);

    // init the weight
    std::vector<double> weights = __kmp_env_weights;
    if ((T)weights.size() == P) {
      dbl_parm2 = weights[tid];
    } else {
      dbl_parm2 = 1.0;
    }

    parm1 = ceil(dbl_parm1 * N); // initial chunk size

    if (parm1 > N) { // chunk size too large
      parm1 = N;
    }

    // my_time = __kmp_get_micros();
    my_time = __kmp_get_ticks();

    pr->u.p.parm1 = parm1;
    pr->u.p.parm2 = chunk;
    pr->u.p.parm3 = parm3;
    pr->u.p.l_parm1 = my_time;
    pr->u.p.dbl_parm1 = dbl_parm1;
    pr->u.p.dbl_parm2 = dbl_parm2;
    pr->u.p.dbl_parm3 = dbl_parm3;
    pr->u.p.dbl_parm4 = dbl_parm4;

    
    // reset shared variables first time
    std::lock_guard<std::mutex> lg(sh->u.s.mtx);
    if (!sh->u.s.initialized) { // I'm the first
      if ((T)mutexes.size() != P)
        mutexes.resize(P);
      if ((T)sh->u.s.dbl_vector.size() != P)
        sh->u.s.dbl_vector.resize(P);
      std::fill(sh->u.s.dbl_vector.begin(), sh->u.s.dbl_vector.end(), 0);
      sh->u.s.initialized = true; // reset done
    }
  } // case
  break;
  case kmp_sch_af: {
    /* Adaptive Factoring */
    T parm1 = 1; // my current chunk index
    T parm2 = 100; // current chunk size
    T parm3; // current sub-chunk size
    T parm4 = 0; // last executed sub-chunk size
    T parm7 = 1; // min chunk size
    DBL dbl_parm2; // divider of current chunk
    DBL dbl_parm3 = 0; // sum of my measured avg iteration times
    DBL dbl_parm4 = 0; // sum of square of avg iteration times
    DBL dbl_parm5 = 0; // my total (sub-)chunk counter
    DBL dbl_parm6; // number of sub-chunks
    parm7 = chunk; 
    KD_TRACE(100,
             ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_af case\n", gtid));

    T divider = __kmp_env_divider;
    T P = nproc;
    T N = tc;
    dbl_parm2 = 1.0 / (DBL)divider;
    dbl_parm6 = (DBL)divider;
    parm3 = parm2 * dbl_parm2;

    if (parm1 > N) { // chunk size too large
      parm2 = N;
    }

    pr->u.p.parm1 = parm1;
    pr->u.p.parm2 = parm2;
    pr->u.p.parm3 = parm3;
    pr->u.p.parm4 = parm4;
    pr->u.p.dbl_parm1 = parm2;
    pr->u.p.dbl_parm2 = dbl_parm2;
    pr->u.p.dbl_parm3 = dbl_parm3;
    pr->u.p.dbl_parm4 = dbl_parm4;
    pr->u.p.dbl_parm5 = dbl_parm5;
    pr->u.p.dbl_parm6 = dbl_parm6;
    pr->u.p.dbl_parm7 = parm7;
    // pr->u.p.dbl_parm7 = min_chunk;

    
    // reset shared variables first time
    std::lock_guard<std::mutex> lg(sh->u.s.mtx);
    if (!sh->u.s.initialized) { // I'm the first
      if ((T)mutexes.size() != P)
        mutexes.resize(P);
      if ((T)sh->u.s.dbl_vector.size() != P)
        sh->u.s.dbl_vector.resize(P);
      if ((T)sh->u.s.dbl_vector2.size() != P)
        sh->u.s.dbl_vector2.resize(P);
      std::fill(sh->u.s.dbl_vector.begin(), sh->u.s.dbl_vector.end(), 0);
      std::fill(sh->u.s.dbl_vector2.begin(), sh->u.s.dbl_vector2.end(), 0);
      sh->u.s.initialized = true; // reset done
    }
  } // case
  break;
  case kmp_sch_af_a: {
    /* Adaptive Factoring with total times */
    T parm1 = 1; // my current chunk index
    T parm2 = 100; // current chunk size
    T parm3; // current sub-chunk size
    T parm4 = 0; // last executed sub-chunk size
    T parm7 = 1; // min chunk size
    DBL dbl_parm2; // divider of current chunk
    DBL dbl_parm3 = 0; // sum of my measured avg iteration times
    DBL dbl_parm4 = 0; // sum of square of avg iteration times
    DBL dbl_parm5 = 0; // my total (sub-)chunk counter
    DBL dbl_parm6; // number of sub-chunks
    parm7 = chunk; 
	

    KD_TRACE(100,
             ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_af_a case\n", gtid));

    T divider = __kmp_env_divider;
    T P = nproc;
    T N = tc;
    dbl_parm2 = 1.0 / (DBL)divider;
    dbl_parm6 = (DBL)divider;
    parm3 = parm2 * dbl_parm2;

    if (parm1 > N) { // chunk size too large
      parm2 = N;
    }

    pr->u.p.parm1 = parm1;
    pr->u.p.parm2 = parm2;
    pr->u.p.parm3 = parm3;
    pr->u.p.parm4 = parm4;
    pr->u.p.dbl_parm1 = parm2;
    pr->u.p.dbl_parm2 = dbl_parm2;
    pr->u.p.dbl_parm3 = dbl_parm3;
    pr->u.p.dbl_parm4 = dbl_parm4;
    pr->u.p.dbl_parm5 = dbl_parm5;
    pr->u.p.dbl_parm6 = dbl_parm6;
    pr->u.p.dbl_parm7 = parm7;

    // Initialize array for storing the chunk sizes information
    
    std::lock_guard<std::mutex> lg(sh->u.s.mtx);
    if (!sh->u.s.initialized) { // I'm the first
      if ((T)mutexes.size() != P)
        mutexes.resize(P);
      if ((T)sh->u.s.dbl_vector.size() != P)
        sh->u.s.dbl_vector.resize(P);
      if ((T)sh->u.s.dbl_vector2.size() != P)
        sh->u.s.dbl_vector2.resize(P);
      std::fill(sh->u.s.dbl_vector.begin(), sh->u.s.dbl_vector.end(), 0);
      std::fill(sh->u.s.dbl_vector2.begin(), sh->u.s.dbl_vector2.end(), 0);
      sh->u.s.initialized = true; // reset done
    }
  } // case
  break;
  case kmp_sch_profiling: {

  	
    /* This is for profiling only */
    T parm1 = 1; // chunk size    
    parm1  = chunk;

    KD_TRACE(
        100,
        ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_profiling case\n", gtid));

    pr->u.p.parm1 = parm1;
    pr->u.p.l_parm1 = 0;
    if (tid == 0) 
    {
    	char* fileData = std::getenv("KMP_PROFILE_DATA");
    	if(fileData==NULL || strcmp(fileData,"")==0)
    	{
    		std::cout<<"Please export KMP_PROFILE_DATA in your environment with the path for the profile output data\n";
    		exit(-1);
    	}
    	std::fstream ofs;
    	ofs.open(fileData, std::ofstream::out | std::ofstream::app);
 		ofs << "Location: " << loc->psource << " ";
  		ofs.close();
    }
    // reset shared variables first time
    std::lock_guard<std::mutex> lg(sh->u.s.mtx);
    if (!sh->u.s.initialized) { // I'm the first
      sh->u.s.dbl_vector.clear();
      sh->u.s.initialized = true; // reset done
    }
  } // case
  break;
    /* --------------------------LB4OMP_extensions-----------------------------*/

  default: {
    __kmp_fatal(KMP_MSG(UnknownSchedTypeDetected), // Primary message
                KMP_HNT(GetNewerLibrary), // Hint
                __kmp_msg_null // Variadic argument list terminator
                );
  } break;
  } // switch
  pr->schedule = schedule;

  /* --------------------------LB4OMP_extensions-----------------------------*/
#if KMP_DEBUG
  /* Debug prints for correctness checks, comment out else */
  pr->u.p.chunks.clear();
#endif
  /* --------------------------LB4OMP_extensions-----------------------------*/
}

#if KMP_USE_HIER_SCHED
template <typename T>
inline void __kmp_dispatch_init_hier_runtime(ident_t *loc, T lb, T ub,
                                             typename traits_t<T>::signed_t st);
template <>
inline void
__kmp_dispatch_init_hier_runtime<kmp_int32>(ident_t *loc, kmp_int32 lb,
                                            kmp_int32 ub, kmp_int32 st) {
  __kmp_dispatch_init_hierarchy<kmp_int32>(
      loc, __kmp_hier_scheds.size, __kmp_hier_scheds.layers,
      __kmp_hier_scheds.scheds, __kmp_hier_scheds.small_chunks, lb, ub, st);
}
template <>
inline void
__kmp_dispatch_init_hier_runtime<kmp_uint32>(ident_t *loc, kmp_uint32 lb,
                                             kmp_uint32 ub, kmp_int32 st) {
  __kmp_dispatch_init_hierarchy<kmp_uint32>(
      loc, __kmp_hier_scheds.size, __kmp_hier_scheds.layers,
      __kmp_hier_scheds.scheds, __kmp_hier_scheds.small_chunks, lb, ub, st);
}
template <>
inline void
__kmp_dispatch_init_hier_runtime<kmp_int64>(ident_t *loc, kmp_int64 lb,
                                            kmp_int64 ub, kmp_int64 st) {
  __kmp_dispatch_init_hierarchy<kmp_int64>(
      loc, __kmp_hier_scheds.size, __kmp_hier_scheds.layers,
      __kmp_hier_scheds.scheds, __kmp_hier_scheds.large_chunks, lb, ub, st);
}
template <>
inline void
__kmp_dispatch_init_hier_runtime<kmp_uint64>(ident_t *loc, kmp_uint64 lb,
                                             kmp_uint64 ub, kmp_int64 st) {
  __kmp_dispatch_init_hierarchy<kmp_uint64>(
      loc, __kmp_hier_scheds.size, __kmp_hier_scheds.layers,
      __kmp_hier_scheds.scheds, __kmp_hier_scheds.large_chunks, lb, ub, st);
}

// free all the hierarchy scheduling memory associated with the team
void __kmp_dispatch_free_hierarchies(kmp_team_t *team) {
  int num_disp_buff = team->t.t_max_nproc > 1 ? __kmp_dispatch_num_buffers : 2;
  for (int i = 0; i < num_disp_buff; ++i) {
    // type does not matter here so use kmp_int32
    auto sh =
        reinterpret_cast<dispatch_shared_info_template<kmp_int32> volatile *>(
            &team->t.t_disp_buffer[i]);
    if (sh->hier) {
      sh->hier->deallocate();
      __kmp_free(sh->hier);
    }
  }
}
#endif

// UT - unsigned flavor of T, ST - signed flavor of T,
// DBL - double if sizeof(T)==4, or long double if sizeof(T)==8
template <typename T>
static void
__kmp_dispatch_init(ident_t *loc, int gtid, enum sched_type schedule, T lb,
                    T ub, typename traits_t<T>::signed_t st,
                    typename traits_t<T>::signed_t chunk, int push_ws) {
  typedef typename traits_t<T>::unsigned_t UT;

  int active;
  kmp_info_t *th;
  kmp_team_t *team;
  kmp_uint32 my_buffer_index;
  dispatch_private_info_template<T> *pr;
  dispatch_shared_info_template<T> /*LB4OMP_extensions volatile*/ *sh;

  KMP_BUILD_ASSERT(sizeof(dispatch_private_info_template<T>) ==
                   sizeof(dispatch_private_info));
  KMP_BUILD_ASSERT(sizeof(dispatch_shared_info_template<UT>) ==
                   sizeof(dispatch_shared_info));

  if (!TCR_4(__kmp_init_parallel))
    __kmp_parallel_initialize();

#if INCLUDE_SSC_MARKS
  SSC_MARK_DISPATCH_INIT();
#endif
#ifdef KMP_DEBUG
  typedef typename traits_t<T>::signed_t ST;
  {
    char *buff;
    // create format specifiers before the debug output
    buff = __kmp_str_format("__kmp_dispatch_init: T#%%d called: schedule:%%d "
                            "chunk:%%%s lb:%%%s ub:%%%s st:%%%s\n",
                            traits_t<ST>::spec, traits_t<T>::spec,
                            traits_t<T>::spec, traits_t<ST>::spec);
    KD_TRACE(10, (buff, gtid, schedule, chunk, lb, ub, st));
    __kmp_str_free(&buff);
  }
#endif
  /* setup data */
  th = __kmp_threads[gtid];
  team = th->th.th_team;
  active = !team->t.t_serialized;
  th->th.th_ident = loc;

  // Any half-decent optimizer will remove this test when the blocks are empty
  // since the macros expand to nothing
  // when statistics are disabled.
  if (schedule == __kmp_static) {
    KMP_COUNT_BLOCK(OMP_LOOP_STATIC);
  } else {
    KMP_COUNT_BLOCK(OMP_LOOP_DYNAMIC);
  }

#if KMP_USE_HIER_SCHED
  // Initialize the scheduling hierarchy if requested in OMP_SCHEDULE envirable
  // Hierarchical scheduling does not work with ordered, so if ordered is
  // detected, then revert back to threaded scheduling.
  bool ordered;
  enum sched_type my_sched = schedule;
  my_buffer_index = th->th.th_dispatch->th_disp_index;
  pr = reinterpret_cast<dispatch_private_info_template<T> *>(
      &th->th.th_dispatch
           ->th_disp_buffer[my_buffer_index % __kmp_dispatch_num_buffers]);
  my_sched = SCHEDULE_WITHOUT_MODIFIERS(my_sched);
  if ((my_sched >= kmp_nm_lower) && (my_sched < kmp_nm_upper))
    my_sched =
        (enum sched_type)(((int)my_sched) - (kmp_nm_lower - kmp_sch_lower));
  ordered = (kmp_ord_lower & my_sched);
  if (pr->flags.use_hier) {
    if (ordered) {
      KD_TRACE(100, ("__kmp_dispatch_init: T#%d ordered loop detected.  "
                     "Disabling hierarchical scheduling.\n",
                     gtid));
      pr->flags.use_hier = FALSE;
    }
  }
  if (schedule == kmp_sch_runtime && __kmp_hier_scheds.size > 0) {
    // Don't use hierarchical for ordered parallel loops and don't
    // use the runtime hierarchy if one was specified in the program
    if (!ordered && !pr->flags.use_hier)
      __kmp_dispatch_init_hier_runtime<T>(loc, lb, ub, st);
  }
#endif // KMP_USE_HIER_SCHED

#if USE_ITT_BUILD
  kmp_uint64 cur_chunk = chunk;
  int itt_need_metadata_reporting = __itt_metadata_add_ptr &&
                                    __kmp_forkjoin_frames_mode == 3 &&
                                    KMP_MASTER_GTID(gtid) &&
#if OMP_40_ENABLED
                                    th->th.th_teams_microtask == NULL &&
#endif
                                    team->t.t_active_level == 1;
#endif
  if (!active) {
    pr = reinterpret_cast<dispatch_private_info_template<T> *>(
        th->th.th_dispatch->th_disp_buffer); /* top of the stack */
  } else {
    KMP_DEBUG_ASSERT(th->th.th_dispatch ==
                     &th->th.th_team->t.t_dispatch[th->th.th_info.ds.ds_tid]);

    my_buffer_index = th->th.th_dispatch->th_disp_index++;

    /* What happens when number of threads changes, need to resize buffer? */
    pr = reinterpret_cast<dispatch_private_info_template<T> *>(
        &th->th.th_dispatch
             ->th_disp_buffer[my_buffer_index % __kmp_dispatch_num_buffers]);
    sh = reinterpret_cast<
        dispatch_shared_info_template<T> /*LB4OMP_extensions volatile*/ *>(
        &team->t.t_disp_buffer[my_buffer_index % __kmp_dispatch_num_buffers]);
    KD_TRACE(10, ("__kmp_dispatch_init: T#%d my_buffer_index:%d\n", gtid,
                  my_buffer_index));
  }

  __kmp_dispatch_init_algorithm(loc, gtid, pr,
                                //------------LB4OMP_extensions----------------
                                sh,
                                //------------LB4OMP_extensions----------------
                                schedule, lb, ub, st,
#if USE_ITT_BUILD
                                &cur_chunk,
#endif
                                chunk, (T)th->th.th_team_nproc,
                                (T)th->th.th_info.ds.ds_tid);
  if (active) {
    if (pr->flags.ordered == 0) {
      th->th.th_dispatch->th_deo_fcn = __kmp_dispatch_deo_error;
      th->th.th_dispatch->th_dxo_fcn = __kmp_dispatch_dxo_error;
    } else {
      th->th.th_dispatch->th_deo_fcn = __kmp_dispatch_deo<UT>;
      th->th.th_dispatch->th_dxo_fcn = __kmp_dispatch_dxo<UT>;
    }
  }

  if (active) {
    /* The name of this buffer should be my_buffer_index when it's free to use
     * it */

    KD_TRACE(100, ("__kmp_dispatch_init: T#%d before wait: my_buffer_index:%d "
                   "sh->buffer_index:%d\n",
                   gtid, my_buffer_index, sh->buffer_index));
    __kmp_wait_yield<kmp_uint32>(&sh->buffer_index, my_buffer_index,
                                 __kmp_eq<kmp_uint32> USE_ITT_BUILD_ARG(NULL));
    // Note: KMP_WAIT_YIELD() cannot be used there: buffer index and
    // my_buffer_index are *always* 32-bit integers.
    KMP_MB(); /* is this necessary? */
    KD_TRACE(100, ("__kmp_dispatch_init: T#%d after wait: my_buffer_index:%d "
                   "sh->buffer_index:%d\n",
                   gtid, my_buffer_index, sh->buffer_index));

    th->th.th_dispatch->th_dispatch_pr_current = (dispatch_private_info_t *)pr;
    th->th.th_dispatch->th_dispatch_sh_current =
        CCAST(dispatch_shared_info_t *,
              (/* LB4OMP_extensions volatile*/ dispatch_shared_info_t *)sh);
#if USE_ITT_BUILD
    if (pr->flags.ordered) {
      __kmp_itt_ordered_init(gtid);
    }
    // Report loop metadata
    if (itt_need_metadata_reporting) {
      // Only report metadata by master of active team at level 1
      kmp_uint64 schedtype = 0;
      switch (schedule) {
      case kmp_sch_static_chunked:
      case kmp_sch_static_balanced: // Chunk is calculated in the switch above
        break;
      case kmp_sch_static_greedy:
        cur_chunk = pr->u.p.parm1;
        break;
      case kmp_sch_dynamic_chunked:
        schedtype = 1;
        break;
      case kmp_sch_guided_iterative_chunked:
      case kmp_sch_guided_analytical_chunked:
#if OMP_45_ENABLED
      case kmp_sch_guided_simd:
#endif
        schedtype = 2;
        break;
      default:
        // Should we put this case under "static"?
        // case kmp_sch_static_steal:
        schedtype = 3;
        break;
      }
      __kmp_itt_metadata_loop(loc, schedtype, pr->u.p.tc, cur_chunk);
    }
#if KMP_USE_HIER_SCHED
    if (pr->flags.use_hier) {
      pr->u.p.count = 0;
      pr->u.p.ub = pr->u.p.lb = pr->u.p.st = pr->u.p.tc = 0;
    }
#endif // KMP_USER_HIER_SCHED
#endif /* USE_ITT_BUILD */
  }

#ifdef KMP_DEBUG
  {
    char *buff;
    // create format specifiers before the debug output
    buff = __kmp_str_format(
        "__kmp_dispatch_init: T#%%d returning: schedule:%%d ordered:%%%s "
        "lb:%%%s ub:%%%s"
        " st:%%%s tc:%%%s count:%%%s\n\tordered_lower:%%%s ordered_upper:%%%s"
        " parm1:%%%s parm2:%%%s parm3:%%%s parm4:%%%s\n",
        traits_t<UT>::spec, traits_t<T>::spec, traits_t<T>::spec,
        traits_t<ST>::spec, traits_t<UT>::spec, traits_t<UT>::spec,
        traits_t<UT>::spec, traits_t<UT>::spec, traits_t<T>::spec,
        traits_t<T>::spec, traits_t<T>::spec, traits_t<T>::spec);
    KD_TRACE(10, (buff, gtid, pr->schedule, pr->flags.ordered, pr->u.p.lb,
                  pr->u.p.ub, pr->u.p.st, pr->u.p.tc, pr->u.p.count,
                  pr->u.p.ordered_lower, pr->u.p.ordered_upper, pr->u.p.parm1,
                  pr->u.p.parm2, pr->u.p.parm3, pr->u.p.parm4));
    __kmp_str_free(&buff);
  }
#endif
#if (KMP_STATIC_STEAL_ENABLED)
  // It cannot be guaranteed that after execution of a loop with some other
  // schedule kind all the parm3 variables will contain the same value. Even if
  // all parm3 will be the same, it still exists a bad case like using 0 and 1
  // rather than program life-time increment. So the dedicated variable is
  // required. The 'static_steal_counter' is used.
  if (schedule == kmp_sch_static_steal) {
    // Other threads will inspect this variable when searching for a victim.
    // This is a flag showing that other threads may steal from this thread
    // since then.
    volatile T *p = &pr->u.p.static_steal_counter;
    *p = *p + 1;
  }
#endif // ( KMP_STATIC_STEAL_ENABLED )

#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.ompt_callback_work) {
    ompt_team_info_t *team_info = __ompt_get_teaminfo(0, NULL);
    ompt_task_info_t *task_info = __ompt_get_task_info_object(0);
    ompt_callbacks.ompt_callback(ompt_callback_work)(
        ompt_work_loop, ompt_scope_begin, &(team_info->parallel_data),
        &(task_info->task_data), pr->u.p.tc, OMPT_LOAD_RETURN_ADDRESS(gtid));
  }
#endif
  KMP_PUSH_PARTITIONED_TIMER(OMP_loop_dynamic);
}

/* For ordered loops, either __kmp_dispatch_finish() should be called after
 * every iteration, or __kmp_dispatch_finish_chunk() should be called after
 * every chunk of iterations.  If the ordered section(s) were not executed
 * for this iteration (or every iteration in this chunk), we need to set the
 * ordered iteration counters so that the next thread can proceed. */
template <typename UT>
static void __kmp_dispatch_finish(int gtid, ident_t *loc) {
  typedef typename traits_t<UT>::signed_t ST;
  kmp_info_t *th = __kmp_threads[gtid];
  KD_TRACE(100, ("__kmp_dispatch_finish: T#%d called\n", gtid));
  if (!th->th.th_team->t.t_serialized) {

    dispatch_private_info_template<UT> *pr =
        reinterpret_cast<dispatch_private_info_template<UT> *>(
            th->th.th_dispatch->th_dispatch_pr_current);
    dispatch_shared_info_template<UT> /*LB4OMP_extensions volatile*/ *sh =
        reinterpret_cast<
            dispatch_shared_info_template<UT> /*LB4OMP_extensions volatile*/ *>(
            th->th.th_dispatch->th_dispatch_sh_current);
    KMP_DEBUG_ASSERT(pr);
    KMP_DEBUG_ASSERT(sh);
    KMP_DEBUG_ASSERT(th->th.th_dispatch ==
                     &th->th.th_team->t.t_dispatch[th->th.th_info.ds.ds_tid]);

    if (pr->ordered_bumped) {
      KD_TRACE(
          1000,
          ("__kmp_dispatch_finish: T#%d resetting ordered_bumped to zero\n",
           gtid));
      pr->ordered_bumped = 0;
    } else {
      UT lower = pr->u.p.ordered_lower;

#ifdef KMP_DEBUG
      {
        char *buff;
        // create format specifiers before the debug output
        buff = __kmp_str_format("__kmp_dispatch_finish: T#%%d before wait: "
                                "ordered_iteration:%%%s lower:%%%s\n",
                                traits_t<UT>::spec, traits_t<UT>::spec);
        KD_TRACE(1000, (buff, gtid, sh->u.s.ordered_iteration, lower));
        __kmp_str_free(&buff);
      }
#endif

      __kmp_wait_yield<UT>(&sh->u.s.ordered_iteration, lower,
                           __kmp_ge<UT> USE_ITT_BUILD_ARG(NULL));
      KMP_MB(); /* is this necessary? */
#ifdef KMP_DEBUG
      {
        char *buff;
        // create format specifiers before the debug output
        buff = __kmp_str_format("__kmp_dispatch_finish: T#%%d after wait: "
                                "ordered_iteration:%%%s lower:%%%s\n",
                                traits_t<UT>::spec, traits_t<UT>::spec);
        KD_TRACE(1000, (buff, gtid, sh->u.s.ordered_iteration, lower));
        __kmp_str_free(&buff);
      }
#endif

      test_then_inc<ST>((volatile ST *)&sh->u.s.ordered_iteration);
    } // if
  } // if
  KD_TRACE(100, ("__kmp_dispatch_finish: T#%d returned\n", gtid));
}

#ifdef KMP_GOMP_COMPAT

template <typename UT>
static void __kmp_dispatch_finish_chunk(int gtid, ident_t *loc) {
  typedef typename traits_t<UT>::signed_t ST;
  kmp_info_t *th = __kmp_threads[gtid];

  KD_TRACE(100, ("__kmp_dispatch_finish_chunk: T#%d called\n", gtid));
  if (!th->th.th_team->t.t_serialized) {
    //        int cid;
    dispatch_private_info_template<UT> *pr =
        reinterpret_cast<dispatch_private_info_template<UT> *>(
            th->th.th_dispatch->th_dispatch_pr_current);
    dispatch_shared_info_template<UT> /* LB4OMP_extensions volatile*/ *sh =
        reinterpret_cast<
            dispatch_shared_info_template<UT> /*LB4OMP_extensions volatile*/ *>(
            th->th.th_dispatch->th_dispatch_sh_current);
    KMP_DEBUG_ASSERT(pr);
    KMP_DEBUG_ASSERT(sh);
    KMP_DEBUG_ASSERT(th->th.th_dispatch ==
                     &th->th.th_team->t.t_dispatch[th->th.th_info.ds.ds_tid]);

    //        for (cid = 0; cid < KMP_MAX_ORDERED; ++cid) {
    UT lower = pr->u.p.ordered_lower;
    UT upper = pr->u.p.ordered_upper;
    UT inc = upper - lower + 1;

    if (pr->ordered_bumped == inc) {
      KD_TRACE(
          1000,
          ("__kmp_dispatch_finish: T#%d resetting ordered_bumped to zero\n",
           gtid));
      pr->ordered_bumped = 0;
    } else {
      inc -= pr->ordered_bumped;

#ifdef KMP_DEBUG
      {
        char *buff;
        // create format specifiers before the debug output
        buff = __kmp_str_format(
            "__kmp_dispatch_finish_chunk: T#%%d before wait: "
            "ordered_iteration:%%%s lower:%%%s upper:%%%s\n",
            traits_t<UT>::spec, traits_t<UT>::spec, traits_t<UT>::spec);
        KD_TRACE(1000, (buff, gtid, sh->u.s.ordered_iteration, lower, upper));
        __kmp_str_free(&buff);
      }
#endif

      __kmp_wait_yield<UT>(&sh->u.s.ordered_iteration, lower,
                           __kmp_ge<UT> USE_ITT_BUILD_ARG(NULL));

      KMP_MB(); /* is this necessary? */
      KD_TRACE(1000, ("__kmp_dispatch_finish_chunk: T#%d resetting "
                      "ordered_bumped to zero\n",
                      gtid));
      pr->ordered_bumped = 0;
//!!!!! TODO check if the inc should be unsigned, or signed???
#ifdef KMP_DEBUG
      {
        char *buff;
        // create format specifiers before the debug output
        buff = __kmp_str_format(
            "__kmp_dispatch_finish_chunk: T#%%d after wait: "
            "ordered_iteration:%%%s inc:%%%s lower:%%%s upper:%%%s\n",
            traits_t<UT>::spec, traits_t<UT>::spec, traits_t<UT>::spec,
            traits_t<UT>::spec);
        KD_TRACE(1000,
                 (buff, gtid, sh->u.s.ordered_iteration, inc, lower, upper));
        __kmp_str_free(&buff);
      }
#endif

      test_then_add<ST>((volatile ST *)&sh->u.s.ordered_iteration, inc);
    }
    //        }
  }
  KD_TRACE(100, ("__kmp_dispatch_finish_chunk: T#%d returned\n", gtid));
}

#endif /* KMP_GOMP_COMPAT */

template <typename T>
int __kmp_dispatch_next_algorithm(
    int gtid, dispatch_private_info_template<T> *pr,
    dispatch_shared_info_template<T> /* LB4OMP_extensions volatile*/ *sh,
    kmp_int32 *p_last, T *p_lb, T *p_ub, typename traits_t<T>::signed_t *p_st,
    T nproc, T tid) 
{
  typedef typename traits_t<T>::unsigned_t UT;
  typedef typename traits_t<T>::signed_t ST;
  typedef typename traits_t<T>::floating_t DBL;
  int status = 0;
  kmp_int32 last = 0;
  T start;
  ST incr;
  UT limit, trip, init;
  kmp_info_t *th = __kmp_threads[gtid];
  kmp_team_t *team = th->th.th_team;

  KMP_DEBUG_ASSERT(th->th.th_dispatch ==
                   &th->th.th_team->t.t_dispatch[th->th.th_info.ds.ds_tid]);
  KMP_DEBUG_ASSERT(pr);
  KMP_DEBUG_ASSERT(sh);
  KMP_DEBUG_ASSERT(tid >= 0 && tid < nproc);
#ifdef KMP_DEBUG
  {
    char *buff;
    // create format specifiers before the debug output
    buff =
        __kmp_str_format("__kmp_dispatch_next_algorithm: T#%%d called pr:%%p "
                         "sh:%%p nproc:%%%s tid:%%%s\n",
                         traits_t<T>::spec, traits_t<T>::spec);
    KD_TRACE(10, (buff, gtid, pr, sh, nproc, tid));
    __kmp_str_free(&buff);
  }
#endif

  // zero trip count
  if (pr->u.p.tc == 0) {
    KD_TRACE(10,
             ("__kmp_dispatch_next_algorithm: T#%d early exit trip count is "
              "zero status:%d\n",
              gtid, status));
    return 0;
  }

  //--------------------------LB4OMP_extensions---------------------------
  // Check if user has set min chunk size and switch to dynamic
  // if the specified size is reached. Use only with advanced schedules.
//   if (pr->u.p.chunk_spec > 0 &&
//       (pr->schedule == kmp_sch_tap ||
//        pr->schedule == kmp_sch_faca || pr->schedule == kmp_sch_fac2 ||
//        pr->schedule == kmp_sch_fac2a || pr->schedule == kmp_sch_wf ||
//        pr->schedule == kmp_sch_bold || pr->schedule == kmp_sch_awf_b ||
//        pr->schedule == kmp_sch_awf_c || pr->schedule == kmp_sch_awf_d ||
//        pr->schedule == kmp_sch_awf_e || pr->schedule == kmp_sch_af ||
//        pr->schedule == kmp_sch_af_a)) {
//     //---------------------min chunk switcher-------------------------
//     // compare with nproc*(chunk+1)
//     ST rem; // remaining is signed, because can be < 0
//     T min_chunk = pr->u.p.min_chunk;
//     T chunk_spec = pr->u.p.chunk_spec;
//     trip = pr->u.p.tc;
//     init = sh->u.s.iteration; // shared value
//     rem = trip - init;
//     if ((T)rem < chunk_spec) {
//       // below minimum chunk size
//       // use dynamic-style schedule
//       // atomically increment iterations, get old value
//       init = test_then_add<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
//                                (ST)min_chunk);
//       rem = trip - init;
//       if (rem <= 0) {
//         status = 0; // all iterations got by other threads
//       } else {
//         // got some iterations to work on
//         status = 1;
//         if ((T)rem > min_chunk) {
//           limit = init + min_chunk - 1;
//         } else {
//           last = 1; // the last chunk
//           limit = init + rem - 1;
//         } // if
//       } // if
//       if (status != 0) {
//         start = pr->u.p.lb;
//         incr = pr->u.p.st;
//         if (p_st != nullptr)
//           *p_st = incr;
//         *p_lb = start + init * incr;
//         *p_ub = start + limit * incr;
//         if (pr->flags.ordered) {
//           pr->u.p.ordered_lower = init;
//           pr->u.p.ordered_upper = limit;
//         } // if
//       } else {
//         *p_lb = 0;
//         *p_ub = 0;
//         if (p_st != nullptr)
//           *p_st = 0;
//       } // if
//       if (p_last)
//         *p_last = last;
// #if KMP_DEBUG
//       // Debug print. Comment out else.
//       print_chunks(status, p_lb, p_ub, tid, pr);
// #endif
//       return status;
//     } // if
//     //---------------------min chunk switcher-------------------------
//   }
  //--------------------------LB4OMP_extensions---------------------------

  switch (pr->schedule) {
#if (KMP_STATIC_STEAL_ENABLED)
  case kmp_sch_static_steal: {
    T chunk = pr->u.p.parm1;

    KD_TRACE(100,
             ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_static_steal case\n",
              gtid));

    trip = pr->u.p.tc - 1;

    if (traits_t<T>::type_size > 4) {
      // use lock for 8-byte and CAS for 4-byte induction
      // variable. TODO (optional): check and use 16-byte CAS
      kmp_lock_t *lck = th->th.th_dispatch->th_steal_lock;
      KMP_DEBUG_ASSERT(lck != NULL);
      if (pr->u.p.count < (UT)pr->u.p.ub) {
        __kmp_acquire_lock(lck, gtid);
        // try to get own chunk of iterations
        init = (pr->u.p.count)++;
        status = (init < (UT)pr->u.p.ub);
        __kmp_release_lock(lck, gtid);
      } else {
        status = 0; // no own chunks
      }
      if (!status) { // try to steal
        kmp_info_t **other_threads = team->t.t_threads;
        int while_limit = nproc; // nproc attempts to find a victim
        int while_index = 0;
        // TODO: algorithm of searching for a victim
        // should be cleaned up and measured
        while ((!status) && (while_limit != ++while_index)) {
          T remaining;
          T victimIdx = pr->u.p.parm4;
          T oldVictimIdx = victimIdx ? victimIdx - 1 : nproc - 1;
          dispatch_private_info_template<T> *victim =
              reinterpret_cast<dispatch_private_info_template<T> *>(
                  other_threads[victimIdx]
                      ->th.th_dispatch->th_dispatch_pr_current);
          while ((victim == NULL || victim == pr ||
                  (*(volatile T *)&victim->u.p.static_steal_counter !=
                   *(volatile T *)&pr->u.p.static_steal_counter)) &&
                 oldVictimIdx != victimIdx) {
            victimIdx = (victimIdx + 1) % nproc;
            victim = reinterpret_cast<dispatch_private_info_template<T> *>(
                other_threads[victimIdx]
                    ->th.th_dispatch->th_dispatch_pr_current);
          }
          if (!victim || (*(volatile T *)&victim->u.p.static_steal_counter !=
                          *(volatile T *)&pr->u.p.static_steal_counter)) {
            continue; // try once more (nproc attempts in total)
            // no victim is ready yet to participate in stealing
            // because all victims are still in kmp_init_dispatch
          }
          if (victim->u.p.count + 2 > (UT)victim->u.p.ub) {
            pr->u.p.parm4 = (victimIdx + 1) % nproc; // shift start tid
            continue; // not enough chunks to steal, goto next victim
          }

          lck = other_threads[victimIdx]->th.th_dispatch->th_steal_lock;
          KMP_ASSERT(lck != NULL);
          __kmp_acquire_lock(lck, gtid);
          limit = victim->u.p.ub; // keep initial ub
          if (victim->u.p.count >= limit ||
              (remaining = limit - victim->u.p.count) < 2) {
            __kmp_release_lock(lck, gtid);
            pr->u.p.parm4 = (victimIdx + 1) % nproc; // next victim
            continue; // not enough chunks to steal
          }
          // stealing succeded, reduce victim's ub by 1/4 of undone chunks or
          // by 1
          if (remaining > 3) {
            // steal 1/4 of remaining
            KMP_COUNT_DEVELOPER_VALUE(FOR_static_steal_stolen, remaining >> 2);
            init = (victim->u.p.ub -= (remaining >> 2));
          } else {
            // steal 1 chunk of 2 or 3 remaining
            KMP_COUNT_DEVELOPER_VALUE(FOR_static_steal_stolen, 1);
            init = (victim->u.p.ub -= 1);
          }
          __kmp_release_lock(lck, gtid);

          KMP_DEBUG_ASSERT(init + 1 <= limit);
          pr->u.p.parm4 = victimIdx; // remember victim to steal from
          status = 1;
          while_index = 0;
          // now update own count and ub with stolen range but init chunk
          __kmp_acquire_lock(th->th.th_dispatch->th_steal_lock, gtid);
          pr->u.p.count = init + 1;
          pr->u.p.ub = limit;
          __kmp_release_lock(th->th.th_dispatch->th_steal_lock, gtid);
        } // while (search for victim)
      } // if (try to find victim and steal)
    } else {
      // 4-byte induction variable, use 8-byte CAS for pair (count, ub)
      typedef union {
        struct {
          UT count;
          T ub;
        } p;
        kmp_int64 b;
      } union_i4;
      // All operations on 'count' or 'ub' must be combined atomically
      // together.
      {
        union_i4 vold, vnew;
        vold.b = *(volatile kmp_int64 *)(&pr->u.p.count);
        vnew = vold;
        vnew.p.count++;
        while (!KMP_COMPARE_AND_STORE_ACQ64(
            (volatile kmp_int64 *)&pr->u.p.count,
            *VOLATILE_CAST(kmp_int64 *) & vold.b,
            *VOLATILE_CAST(kmp_int64 *) & vnew.b)) {
          KMP_CPU_PAUSE();
          vold.b = *(volatile kmp_int64 *)(&pr->u.p.count);
          vnew = vold;
          vnew.p.count++;
        }
        vnew = vold;
        init = vnew.p.count;
        status = (init < (UT)vnew.p.ub);
      }

      if (!status) {
        kmp_info_t **other_threads = team->t.t_threads;
        int while_limit = nproc; // nproc attempts to find a victim
        int while_index = 0;

        // TODO: algorithm of searching for a victim
        // should be cleaned up and measured
        while ((!status) && (while_limit != ++while_index)) {
          union_i4 vold, vnew;
          kmp_int32 remaining;
          T victimIdx = pr->u.p.parm4;
          T oldVictimIdx = victimIdx ? victimIdx - 1 : nproc - 1;
          dispatch_private_info_template<T> *victim =
              reinterpret_cast<dispatch_private_info_template<T> *>(
                  other_threads[victimIdx]
                      ->th.th_dispatch->th_dispatch_pr_current);
          while ((victim == NULL || victim == pr ||
                  (*(volatile T *)&victim->u.p.static_steal_counter !=
                   *(volatile T *)&pr->u.p.static_steal_counter)) &&
                 oldVictimIdx != victimIdx) {
            victimIdx = (victimIdx + 1) % nproc;
            victim = reinterpret_cast<dispatch_private_info_template<T> *>(
                other_threads[victimIdx]
                    ->th.th_dispatch->th_dispatch_pr_current);
          }
          if (!victim || (*(volatile T *)&victim->u.p.static_steal_counter !=
                          *(volatile T *)&pr->u.p.static_steal_counter)) {
            continue; // try once more (nproc attempts in total)
            // no victim is ready yet to participate in stealing
            // because all victims are still in kmp_init_dispatch
          }
          pr->u.p.parm4 = victimIdx; // new victim found
          while (1) { // CAS loop if victim has enough chunks to steal
            vold.b = *(volatile kmp_int64 *)(&victim->u.p.count);
            vnew = vold;

            KMP_DEBUG_ASSERT((vnew.p.ub - 1) * (UT)chunk <= trip);
            if (vnew.p.count >= (UT)vnew.p.ub ||
                (remaining = vnew.p.ub - vnew.p.count) < 2) {
              pr->u.p.parm4 = (victimIdx + 1) % nproc; // shift start victim id
              break; // not enough chunks to steal, goto next victim
            }
            if (remaining > 3) {
              vnew.p.ub -= (remaining >> 2); // try to steal 1/4 of remaining
            } else {
              vnew.p.ub -= 1; // steal 1 chunk of 2 or 3 remaining
            }
            KMP_DEBUG_ASSERT((vnew.p.ub - 1) * (UT)chunk <= trip);
            // TODO: Should this be acquire or release?
            if (KMP_COMPARE_AND_STORE_ACQ64(
                    (volatile kmp_int64 *)&victim->u.p.count,
                    *VOLATILE_CAST(kmp_int64 *) & vold.b,
                    *VOLATILE_CAST(kmp_int64 *) & vnew.b)) {
              // stealing succedded
              KMP_COUNT_DEVELOPER_VALUE(FOR_static_steal_stolen,
                                        vold.p.ub - vnew.p.ub);
              status = 1;
              while_index = 0;
              // now update own count and ub
              init = vnew.p.ub;
              vold.p.count = init + 1;
#if KMP_ARCH_X86
              KMP_XCHG_FIXED64((volatile kmp_int64 *)(&pr->u.p.count), vold.b);
#else
              *(volatile kmp_int64 *)(&pr->u.p.count) = vold.b;
#endif
              break;
            } // if (check CAS result)
            KMP_CPU_PAUSE(); // CAS failed, repeate attempt
          } // while (try to steal from particular victim)
        } // while (search for victim)
      } // if (try to find victim and steal)
    } // if (4-byte induction variable)
    if (!status) {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != NULL)
        *p_st = 0;
    } else {
      start = pr->u.p.parm2;
      init *= chunk;
      limit = chunk + init - 1;
      incr = pr->u.p.st;
      KMP_COUNT_DEVELOPER_VALUE(FOR_static_steal_chunks, 1);

      KMP_DEBUG_ASSERT(init <= trip);
      if ((last = (limit >= trip)) != 0)
        limit = trip;
      if (p_st != NULL)
        *p_st = incr;

      if (incr == 1) {
        *p_lb = start + init;
        *p_ub = start + limit;
      } else {
        *p_lb = start + init * incr;
        *p_ub = start + limit * incr;
      }

      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } // if
        

    break;
  } // case
#endif // ( KMP_STATIC_STEAL_ENABLED )
  case kmp_sch_static_balanced: {
    KD_TRACE(
        10,
        ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_static_balanced case\n",
         gtid));
    /* check if thread has any iteration to do */
    if ((status = !pr->u.p.count) != 0) {
      pr->u.p.count = 1;
      *p_lb = pr->u.p.lb;
      *p_ub = pr->u.p.ub;
      last = pr->u.p.parm1;
      if (p_st != NULL)
        *p_st = pr->u.p.st;
    } else { /* no iterations to do */
    	// if (tid == 0) {
     //    timeEnd = __kmp_get_ticks();
    	// std::cout << "LoopTime: " << timeEnd - timeInit << std::endl;
    	// }
      pr->u.p.lb = pr->u.p.ub + pr->u.p.st;
    }
        
  } // case
  break;
  case kmp_sch_static_greedy: /* original code for kmp_sch_static_greedy was
                                 merged here */
  case kmp_sch_static_chunked: {
    T parm1;

    KD_TRACE(100, ("__kmp_dispatch_next_algorithm: T#%d "
                   "kmp_sch_static_[affinity|chunked] case\n",
                   gtid));
    parm1 = pr->u.p.parm1;

    trip = pr->u.p.tc - 1;
    init = parm1 * (pr->u.p.count + tid);

    if ((status = (init <= trip)) != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      limit = parm1 + init - 1;

      if ((last = (limit >= trip)) != 0)
        limit = trip;

      if (p_st != NULL)
        *p_st = incr;

      pr->u.p.count += nproc;

      if (incr == 1) {
        *p_lb = start + init;
        *p_ub = start + limit;
      } else {
        *p_lb = start + init * incr;
        *p_ub = start + limit * incr;
      }

      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } // if
    
  } // case
  break;

  case kmp_sch_dynamic_chunked: {
    T chunk = pr->u.p.parm1;

    KD_TRACE(
        100,
        ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_dynamic_chunked case\n",
         gtid));

    init = chunk * test_then_inc_acq<ST>((volatile ST *)&sh->u.s.iteration);
    trip = pr->u.p.tc - 1;

    if ((status = (init <= trip)) == 0) {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != NULL)
        *p_st = 0;
    } else {
      start = pr->u.p.lb;
      limit = chunk + init - 1;
      incr = pr->u.p.st;

      if ((last = (limit >= trip)) != 0)
        limit = trip;

      if (p_st != NULL)
        *p_st = incr;

      if (incr == 1) {
        *p_lb = start + init;
        *p_ub = start + limit;
      } else {
        *p_lb = start + init * incr;
        *p_ub = start + limit * incr;
      }

      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } // if
    
  } // case
  break;

  case kmp_sch_guided_iterative_chunked: {
    T chunkspec = pr->u.p.parm1;
    KD_TRACE(100, ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_guided_chunked "
                   "iterative case\n",
                   gtid));
    trip = pr->u.p.tc;
    // Start atomic part of calculations
    while (1) {
      ST remaining; // signed, because can be < 0
      init = sh->u.s.iteration; // shared value
      remaining = trip - init;
      if (remaining <= 0) { // AC: need to compare with 0 first
        // nothing to do, don't try atomic op
        status = 0;
        break;
      }
      if ((T)remaining <
          pr->u.p.parm2) { // compare with K*nproc*(chunk+1), K=2 by default
        // use dynamic-style shcedule
        // atomically inrement iterations, get old value
        init = test_then_add<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                                 (ST)chunkspec);
        remaining = trip - init;
        if (remaining <= 0) {
          status = 0; // all iterations got by other threads
        } else {
          // got some iterations to work on
          status = 1;
          if ((T)remaining > chunkspec) {
            limit = init + chunkspec - 1;
          } else {
            last = 1; // the last chunk
            limit = init + remaining - 1;
          } // if
        } // if
        break;
      } // if
      limit = init +
              (UT)(remaining * *(double *)&pr->u.p.parm3); // divide by K*nproc
      if (compare_and_swap<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                               (ST)init, (ST)limit)) {
        // CAS was successful, chunk obtained
        status = 1;
        --limit;
        break;
      } // if
    } // while
    if (status != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      if (p_st != NULL)
        *p_st = incr;
      *p_lb = start + init * incr;
      *p_ub = start + limit * incr;
      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } else {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != NULL)
        *p_st = 0;
    } // if
    
  } // case
  break;

#if OMP_45_ENABLED
  case kmp_sch_guided_simd: {
    // same as iterative but curr-chunk adjusted to be multiple of given
    // chunk
    T chunk = pr->u.p.parm1;
    KD_TRACE(100,
             ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_guided_simd case\n",
              gtid));
    trip = pr->u.p.tc;
    // Start atomic part of calculations
    while (1) {
      ST remaining; // signed, because can be < 0
      init = sh->u.s.iteration; // shared value
      remaining = trip - init;
      if (remaining <= 0) { // AC: need to compare with 0 first
        status = 0; // nothing to do, don't try atomic op
        break;
      }
      KMP_DEBUG_ASSERT(init % chunk == 0);
      // compare with K*nproc*(chunk+1), K=2 by default
      if ((T)remaining < pr->u.p.parm2) {
        // use dynamic-style shcedule
        // atomically inrement iterations, get old value
        init = test_then_add<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                                 (ST)chunk);
        remaining = trip - init;
        if (remaining <= 0) {
          status = 0; // all iterations got by other threads
        } else {
          // got some iterations to work on
          status = 1;
          if ((T)remaining > chunk) {
            limit = init + chunk - 1;
          } else {
            last = 1; // the last chunk
            limit = init + remaining - 1;
          } // if
        } // if
        break;
      } // if
      // divide by K*nproc
      UT span = remaining * (*(double *)&pr->u.p.parm3);
      UT rem = span % chunk;
      if (rem) // adjust so that span%chunk == 0
        span += chunk - rem;
      limit = init + span;
      if (compare_and_swap<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                               (ST)init, (ST)limit)) {
        // CAS was successful, chunk obtained
        status = 1;
        --limit;
        break;
      } // if
    } // while
    if (status != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      if (p_st != NULL)
        *p_st = incr;
      *p_lb = start + init * incr;
      *p_ub = start + limit * incr;
      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } else {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != NULL)
        *p_st = 0;
    } // if
    
  } // case
  break;
#endif // OMP_45_ENABLED

  case kmp_sch_guided_analytical_chunked: {
    T chunkspec = pr->u.p.parm1;
    UT chunkIdx;
#if KMP_USE_X87CONTROL
    /* for storing original FPCW value for Windows* OS on
       IA-32 architecture 8-byte version */
    unsigned int oldFpcw;
    unsigned int fpcwSet = 0;
#endif
    KD_TRACE(100, ("__kmp_dispatch_next_algorithm: T#%d "
                   "kmp_sch_guided_analytical_chunked case\n",
                   gtid));

    trip = pr->u.p.tc;

    KMP_DEBUG_ASSERT(nproc > 1);
    KMP_DEBUG_ASSERT((2UL * chunkspec + 1) * (UT)nproc < trip);

    while (1) { /* this while loop is a safeguard against unexpected zero
                   chunk sizes */
      chunkIdx = test_then_inc_acq<ST>((volatile ST *)&sh->u.s.iteration);
      if (chunkIdx >= (UT)pr->u.p.parm2) {
        --trip;
        /* use dynamic-style scheduling */
        init = chunkIdx * chunkspec + pr->u.p.count;
        /* need to verify init > 0 in case of overflow in the above
         * calculation */
        if ((status = (init > 0 && init <= trip)) != 0) {
          limit = init + chunkspec - 1;

          if ((last = (limit >= trip)) != 0)
            limit = trip;
        }
        break;
      } else {
/* use exponential-style scheduling */
/* The following check is to workaround the lack of long double precision on
   Windows* OS.
   This check works around the possible effect that init != 0 for chunkIdx == 0.
 */
#if KMP_USE_X87CONTROL
        /* If we haven't already done so, save original
           FPCW and set precision to 64-bit, as Windows* OS
           on IA-32 architecture defaults to 53-bit */
        if (!fpcwSet) {
          oldFpcw = _control87(0, 0);
          _control87(_PC_64, _MCW_PC);
          fpcwSet = 0x30000;
        }
#endif
        if (chunkIdx) {
          init = __kmp_dispatch_guided_remaining<T>(
              trip, *(DBL *)&pr->u.p.parm3, chunkIdx);
          KMP_DEBUG_ASSERT(init);
          init = trip - init;
        } else
          init = 0;
        limit = trip - __kmp_dispatch_guided_remaining<T>(
                           trip, *(DBL *)&pr->u.p.parm3, chunkIdx + 1);
        KMP_ASSERT(init <= limit);
        if (init < limit) {
          KMP_DEBUG_ASSERT(limit <= trip);
          --limit;
          status = 1;
          break;
        } // if
      } // if
    } // while (1)
#if KMP_USE_X87CONTROL
    /* restore FPCW if necessary
       AC: check fpcwSet flag first because oldFpcw can be uninitialized here
    */
    if (fpcwSet && (oldFpcw & fpcwSet))
      _control87(oldFpcw, _MCW_PC);
#endif
    if (status != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      if (p_st != NULL)
        *p_st = incr;
      *p_lb = start + init * incr;
      *p_ub = start + limit * incr;
      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      }
    } else {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != NULL)
        *p_st = 0;
    }
    
  } // case
  break;

  case kmp_sch_trapezoidal: {
    UT index;
    T parm2 = pr->u.p.parm2;
    T parm3 = pr->u.p.parm3;
    T parm4 = pr->u.p.parm4;
    KD_TRACE(100,
             ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_trapezoidal case\n",
              gtid));

    index = test_then_inc<ST>((volatile ST *)&sh->u.s.iteration);

    init = (index * ((2 * parm2) - (index - 1) * parm4)) / 2;
    trip = pr->u.p.tc - 1;

    if ((status = ((T)index < parm3 && init <= trip)) ==
        0) { // check if chunk and init not below max
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != NULL)
        *p_st = 0;
    } else { // if below max, go further
      start = pr->u.p.lb;
      limit = ((index + 1) * (2 * parm2 - index * parm4)) / 2 - 1;
      incr = pr->u.p.st;

      if ((last = (limit >= trip)) != 0) // if last chunk
        limit = trip;

      if (p_st != NULL)
        *p_st = incr;

      if (incr == 1) {
        *p_lb = start + init;
        *p_ub = start + limit;
      } else {
        *p_lb = start + init * incr;
        *p_ub = start + limit * incr;
      }

      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } // if
    
  } // case
  break;
/* --------------------------LB4OMP_extensions----------------------------- */
  case kmp_sch_fsc: {
    T chunk = pr->u.p.parm1; // the chunk size

    KD_TRACE(
        100,
        ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_fsc case\n",
         gtid));
#if KMP_DEBUG
    // Scheduling overhead
if((int)tid == 0){
    double h;
    t2=__kmp_get_ticks2();
    h = t2 - t1 - (t2 - t3);
    t1 = t2;
    printf("Thread %i: Scheduling took %lf microseconds.\n", (int)tid, h);
   // if(t1!=0)
   // {
   // 	t1=__kmp_get_ticks2();
   // 	h=(t2-t1)-t3;
    	    
    // }
    t1 = __kmp_get_ticks2();

    t2 = t1;
}
#endif

    // get old and then increase chunk index atomically
    init = chunk * test_then_inc_acq<ST>((volatile ST *)&sh->u.s.iteration);
    trip = pr->u.p.tc - 1;

    if ((status = (init <= trip)) == 0) {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != NULL)
        *p_st = 0;
    } else {
      start = pr->u.p.lb;
      limit = chunk + init - 1;
      incr = pr->u.p.st;

      if ((last = (limit >= trip)) != 0)
        limit = trip;

      if (p_st != NULL)
        *p_st = incr;

      if (incr == 1) {
        *p_lb = start + init;
        *p_ub = start + limit;
      } else {
        *p_lb = start + init * incr;
        *p_ub = start + limit * incr;
      }
#if KMP_DEBUG
      // t2 = __kmp_get_ticks2();
    if((int)tid == 0){
    	t3 = __kmp_get_ticks2();
  	}
      // printf("Thread %i: Scheduling took %lf microseconds.\n", (int)tid, t2 - t1);
#endif

      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } // if
    
  } // case
  break;

  case kmp_sch_tap: {
    T min_chunk = pr->u.p.parm1; // minimum chunk size
    double v = pr->u.p.dbl_parm1;
    KD_TRACE(100,
             ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_tap case\n", gtid));
    trip = pr->u.p.tc;
    // Start atomic part of calculations
    while (true) {
      ST remaining; // signed, because can be < 0
      init = sh->u.s.iteration; // shared value
      remaining = trip - init;
      if (remaining <= 0) { // AC: need to compare with 0 first
        // nothing to do, don't try atomic op
        status = 0;
        break;
      }

      double t = remaining * pr->u.p.dbl_parm2; // remaining / nproc
      T cs = ceil(t + pr->u.p.dbl_parm3 -
             v * sqrt(2 * t + pr->u.p.dbl_parm4)); // chunk size
      if (min_chunk > cs) { // choose the max
        cs = min_chunk;
      }
      limit = init + cs;
      if (compare_and_swap<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                               (ST)init, (ST)limit)) {
        // CAS was successful, chunk obtained
        status = 1;
        if ((last = (limit >= trip)) != 0) // if last chunk
          limit = trip;
        --limit;
        break;
      } // if
    } // while
    if (status != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      if (p_st != nullptr)
        *p_st = incr;
      *p_lb = start + init * incr;
      *p_ub = start + limit * incr;
      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } else {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != nullptr)
        *p_st = 0;
    } // if
    
  } // case
  break;

  case kmp_sch_fac: {
    T min_chunk = pr->u.p.parm4; // minimum chunk size
    double b_factor = pr->u.p.dbl_parm1;
    KD_TRACE(100,
             ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_fac case\n", gtid));
    trip = pr->u.p.tc;

    { // start of critical part
      std::lock_guard<std::mutex> lg(sh->u.s.mtx);
      T cs; // chunk size
      bool new_batch = false;
      ST remaining; // signed, because can be < 0
      init = sh->u.s.iteration; // shared value
      remaining = trip - init;
      if (remaining > 0) { // AC: need to compare with 0 first
        if (sh->u.s.q_counter > 0) { // take available chunk from queue
          cs = sh->u.s.chunk_size;
        } else { // calculate next batch chunk size
          new_batch = true;
          double b = b_factor * 1 / sqrt(remaining); // b
          double x = 2 + pow(b, 2.0) + b * sqrt(pow(b, 2.0) + 4); // x
          cs = ceil(remaining / (x * nproc)); // chunk size
        }
        if (min_chunk > cs) { // check min chunk
          cs = min_chunk;
        }

        // chunk size calculated, now obtain chunk
        // and update queue
        limit = init + cs;
        sh->u.s.iteration = limit;
        if (new_batch) {
          pr->u.p.parm2 = ++sh->u.s.batch; // next batch
          sh->u.s.q_counter = nproc - 1; // add P-1 chunks to queue
        } else {
          pr->u.p.parm2 = sh->u.s.batch;
          sh->u.s.q_counter--; // one chunk obtained
        }
        pr->u.p.parm3++;
        sh->u.s.counter++;

        sh->u.s.chunk_size = cs;
        status = 1;
        if ((last = (limit >= trip)) != 0) // if last chunk
          limit = trip;
        --limit;
      } else { // nothing to do
        status = 0;
      }
    } // end of critical part

    if (status != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      if (p_st != nullptr)
        *p_st = incr;
      *p_lb = start + init * incr;
      *p_ub = start + limit * incr;
      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } else {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != nullptr)
        *p_st = 0;
    } // if
    
  } // case
  break;

  case kmp_sch_faca: {
    UT counter; // factoring counter
    UT batch; // batch index
    T min_chunk = pr->u.p.parm4; // minimum chunk size
    double b_factor = pr->u.p.dbl_parm1;
    KD_TRACE(100,
             ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_faca case\n", gtid));
    trip = pr->u.p.tc;

    // atomically increase factoring counter
    counter = test_then_inc<ST>((volatile ST *)&sh->u.s.counter);

    // calculate current batch index
    batch = counter / nproc;

    while (true) {
      T cs; // chunk size
      ST remaining; // signed, because can be < 0
      init = sh->u.s.iteration; // shared value
      remaining = trip - init;
      if (remaining <= 0) { // AC: need to compare with 0 first
        // nothing to do, don't try atomic op
        status = 0;
        break;
      }

      // calculate current chunk size
      T old_remaining = pr->u.p.parm3;
      T old_chunk = pr->u.p.parm1;
      UT old_batch = pr->u.p.parm2;
      if (batch == old_batch) { // old batch, already calculated
        cs = old_chunk;
      } else {
        // thread must calculate through to current batch
        // no synchronization needed, though
        bool stop = false;
        for (UT i = 0; i < batch - old_batch; i++) {
          // update remaining
          old_remaining = old_remaining - nproc * old_chunk;
          // check if it is a valid batch and not "superfluous"
          // this could only happen at the end
          if (old_remaining <= 0) { // no more needed
            stop = true;
            break;
          }
          // update chunk with updated R
          double b = b_factor * 1 / sqrt(old_remaining); // b
          double x = 2 + pow(b, 2.0) + b * sqrt(pow(b, 2.0) + 4); // x
          old_chunk = ceil(old_remaining / (x * nproc)); // chunk size
        }
        if (stop) {
          status = 0;
          break;
        }
        cs = old_chunk; // updated chunk now
        pr->u.p.parm1 = cs;
        pr->u.p.parm2 = batch;
        pr->u.p.parm3 = old_remaining; // updated remaining now
      }

      if (min_chunk > cs) { // check min chunk
        cs = min_chunk;
      }
      limit = init + cs;
      if (compare_and_swap<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                               (ST)init, (ST)limit)) {
        // CAS was successful, chunk obtained
        status = 1;
        if ((last = (limit >= trip)) != 0) // if last chunk
          limit = trip;
        --limit;
        break;
      } // if
    } // while

    if (status != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      if (p_st != nullptr)
        *p_st = incr;
      *p_lb = start + init * incr;
      *p_ub = start + limit * incr;
      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } else {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != nullptr)
        *p_st = 0;
    } // if
    
  } // case
  break;

  case kmp_sch_fac2: {
    UT counter; // factoring counter
    UT batch; // batch index
    T min_chunk = pr->u.p.parm1; // minimum chunk size
    double factor = pr->u.p.dbl_parm1;
    KD_TRACE(100,
             ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_fac2 case\n", gtid));
    trip = pr->u.p.tc;

    // atomically increase factoring counter
    counter = test_then_inc<ST>((volatile ST *)&sh->u.s.counter);
    //printf("min chunk %d\n", min_chunk);
    // calculate current batch index
    batch = counter / nproc;

    while (true) {
      T cs; // chunk size
      ST remaining; // signed, because can be < 0
      init = sh->u.s.iteration; // shared value
      remaining = trip - init;
      if (remaining <= 0) { // AC: need to compare with 0 first
        // nothing to do, don't try atomic op
        status = 0;
        break;
      }

      // calculate current batch's chunk size
      cs = ceil(pow(0.5, batch + 1) * factor);

      if (min_chunk > cs) { // check min chunk
        cs = min_chunk;
      }
      limit = init + cs;
      if (compare_and_swap<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                               (ST)init, (ST)limit)) {
        // CAS was successful, chunk obtained
        status = 1;
        if ((last = (limit >= trip)) != 0) // if last chunk
          limit = trip;
        --limit;
        break;
      } // if
    } // while

    if (status != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      if (p_st != nullptr)
        *p_st = incr;
      *p_lb = start + init * incr;
      *p_ub = start + limit * incr;
      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } else {

      *p_lb = 0;
      *p_ub = 0;
      if (p_st != nullptr)
        *p_st = 0;
    } // if
    
  } // case
  break;

  case kmp_sch_fac2a: {
    UT counter; // factoring counter
    UT batch; // batch index
    T min_chunk = pr->u.p.parm3; // minimum chunk size
    double factor = pr->u.p.dbl_parm1;
    KD_TRACE(100, ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_fac2a case\n",
                   gtid));
    trip = pr->u.p.tc;

    // atomically increase factoring counter
    counter = test_then_inc<ST>((volatile ST *)&sh->u.s.counter);

    // calculate current batch index
    batch = counter / nproc;

    while (true) {
      T cs; // chunk size
      ST remaining; // signed, because can be < 0
      init = sh->u.s.iteration; // shared value
      remaining = trip - init;
      if (remaining <= 0) { // AC: need to compare with 0 first
        // nothing to do, don't try atomic op
        status = 0;
        break;
      }

      // calculate current batch's chunk size
      T old_chunk = pr->u.p.parm1;
      UT old_batch = pr->u.p.parm2;
      if (batch == old_batch) { // old batch, already calculated
        cs = old_chunk;
      } else {
        // thread must calculate through to current batch
        // no synchronization needed, though
        for (UT i = 0; i < batch - old_batch; i++) {
          // update chunk with updated chunk
          old_chunk = ceil(old_chunk * factor); // chunk size
        }
        cs = old_chunk; // updated chunk now
        pr->u.p.parm1 = cs;
        pr->u.p.parm2 = batch;
      }

      if (min_chunk > cs) { // check min chunk
        cs = min_chunk;
      }
      limit = init + cs;
      if (compare_and_swap<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                               (ST)init, (ST)limit)) {
        // CAS was successful, chunk obtained
        status = 1;
        if ((last = (limit >= trip)) != 0) // if last chunk
          limit = trip;
        --limit;
        break;
      } // if
    } // while

    if (status != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      if (p_st != nullptr)
        *p_st = incr;
      *p_lb = start + init * incr;
      *p_ub = start + limit * incr;
      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } else {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != nullptr)
        *p_st = 0;
    } // if
    
  } // case
  break;

  case kmp_sch_wf: {
    UT counter; // factoring counter
    UT batch; // batch index
    T min_chunk = pr->u.p.parm3; // minimum chunk size
    double factor = pr->u.p.dbl_parm1;
    double weight = pr->u.p.dbl_parm2;
    KD_TRACE(100, ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_wf case\n",
                   gtid));
    trip = pr->u.p.tc;
    printf("min chunk %d\n", min_chunk);
    // atomically increase factoring counter
    counter = test_then_inc<ST>((volatile ST *)&sh->u.s.counter);

    // calculate current batch index
    batch = counter / nproc;

    while (true) {
      T cs; // chunk size
      ST remaining; // signed, because can be < 0
      init = sh->u.s.iteration; // shared value
      remaining = trip - init;
      if (remaining <= 0) { // AC: need to compare with 0 first
        // nothing to do, don't try atomic op
        status = 0;
        break;
      }

      // calculate current batch's chunk size
      T old_chunk = pr->u.p.parm1;
      UT old_batch = pr->u.p.parm2;
      if (batch == old_batch) { // old batch, already calculated
        cs = old_chunk;
      } else {
        // thread must calculate through to current batch
        // no synchronization needed, though
        for (UT i = 0; i < batch - old_batch; i++) {
          // update chunk with updated chunk
          old_chunk = ceil(old_chunk * factor); // chunk size
        }
        cs = old_chunk; // updated chunk now
        pr->u.p.parm1 = cs;
        pr->u.p.parm2 = batch;
      }

      // apply weight to chunk
      cs = (T)ceil(weight * cs);

      if (min_chunk > cs) { // check min chunk
        cs = min_chunk;
      }
      limit = init + cs;
      if (compare_and_swap<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                               (ST)init, (ST)limit)) {
        // CAS was successful, chunk obtained
        status = 1;
        if ((last = (limit >= trip)) != 0) // if last chunk
          limit = trip;
        --limit;
        break;
      } // if
    } // while

    if (status != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      if (p_st != nullptr)
        *p_st = incr;
      *p_lb = start + init * incr;
      *p_ub = start + limit * incr;
      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } else {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != nullptr)
        *p_st = 0;
    } // if
    
  } // case
  break;
case kmp_sch_fac2b:{
        KD_TRACE(100, ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_fac2b case\n",gtid));
        ST total_iterations = (ST) pr->u.p.tc;
        ST total_threads= (ST)nproc;
	ST counter = (ST) test_then_inc<ST>((volatile ST *)&sh->u.s.counter);
	ST min_chunk= pr->u.p.parm1;
	counter = (int)counter/(int)total_threads;
	ST calculated_chunk = ceil(pow(0.5,counter+1)*total_iterations/total_threads);
	if(calculated_chunk<min_chunk)
		calculated_chunk=min_chunk;
	ST start_index =test_then_add<ST>(RCAST(volatile ST *, &sh->u.s.iteration), (ST)calculated_chunk);
        ST end_index = calculated_chunk+start_index-1;
        if(end_index > total_iterations-1)
                end_index=total_iterations-1;
        if(start_index <= end_index)
        {
                status=1;
                init=start_index;
                limit=end_index;
                start = pr->u.p.lb;
                incr = pr->u.p.st;
                if (p_st != nullptr)
                        *p_st = incr;
		*p_lb = start + init * incr;
      		*p_ub = start + limit * incr; 
                //printf("start %d end %d\n", *p_lb, *p_ub);
                if (pr->flags.ordered) {
                        pr->u.p.ordered_lower = init;
                        pr->u.p.ordered_upper = limit;
                }
        }
        else
        {
                status=0;
                //printf("end***********\n");
                *p_lb = 0;
                *p_ub = 0;
                if (p_st != nullptr)
                        *p_st = 0;
        }
}
break;
case kmp_sch_rnd:{
        KD_TRACE(100, ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_rnd case\n",gtid));
        int start_range =(int)pr->u.p.parm1;
	int min_chunk = (int)pr->u.p.parm2;
 	ST total_iterations = (ST) pr->u.p.tc;
	unsigned int seed= (unsigned int) time(NULL)*gtid;
	int calculated_chunk=(rand_r(&seed)%(min_chunk-start_range+1))+start_range;
   	ST start_index =test_then_add<ST>(RCAST(volatile ST *, &sh->u.s.iteration), (ST)calculated_chunk);
        ST end_index = calculated_chunk+start_index-1;
        if(end_index > total_iterations-1)
                end_index=total_iterations-1;
        if(start_index <= end_index)
        {
                status=1;
                init=start_index;
                limit=end_index;
                start = pr->u.p.lb;
                incr = pr->u.p.st;
                if (p_st != nullptr)
                        *p_st = incr;
                *p_lb = start + init * incr;
                *p_ub = start + limit * incr; 
                //printf("start %d end %d\n", *p_lb, *p_ub);
                if (pr->flags.ordered) {
                        pr->u.p.ordered_lower = init;
                        pr->u.p.ordered_upper = limit;
                }
        }
        else
        {
                status=0;
                //printf("end***********\n");
                *p_lb = 0;
                *p_ub = 0;
                if (p_st != nullptr)
                        *p_st = 0;
        }
	
	
}
break;
case kmp_sch_viss:{
        KD_TRACE(100, ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_viss case\n",gtid));
        ST viss_constant=pr->u.p.parm1;
        ST total_iterations = (ST) pr->u.p.tc;
        ST total_threads= (ST)nproc;
        ST counter = (ST) test_then_inc<ST>((volatile ST *)&sh->u.s.counter);
        counter = (int)counter/(int)total_threads;
	ST calculated_chunk=0;
        if(counter>0)
         {
              calculated_chunk  = floor(viss_constant  * ((1- pow(0.5,counter+1)) / (1-0.5) ) );
         }
         else
         {
              calculated_chunk =ceil(viss_constant);
	      //printf("one case %d\n", calculated_chunk);
         }
 	ST start_index =test_then_add<ST>(RCAST(volatile ST *, &sh->u.s.iteration), (ST)calculated_chunk);
        ST end_index = calculated_chunk+start_index-1;
        if(end_index > total_iterations-1)
                end_index=total_iterations-1;
        if(start_index <= end_index)
        {
                status=1;
                init=start_index;
                limit=end_index;
                start = pr->u.p.lb;
                incr = pr->u.p.st;
                if (p_st != nullptr)
                        *p_st = incr;
                *p_lb = start + init * incr;
                *p_ub = start + limit * incr;                 
		//printf("start %d end %d\n", *p_lb, *p_ub);
                if (pr->flags.ordered) {
                        pr->u.p.ordered_lower = init;
                        pr->u.p.ordered_upper = limit;
                }
        }
        else
        {
                status=0;
                //printf("end***********\n");
                *p_lb = 0;
                *p_ub = 0;
                if (p_st != nullptr)
                        *p_st = 0;
        }

}
break;
case kmp_sch_fiss:{
	KD_TRACE(100, ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_fiss case\n",gtid));
	ST fiss_delta=pr->u.p.parm1;
	ST fiss_chunk=pr->u.p.parm2;
	ST total_iterations = (ST) pr->u.p.tc;
	ST total_threads= (ST)nproc;
	ST counter = (ST) test_then_inc<ST>((volatile ST *)&sh->u.s.counter);
        counter = (int)counter /(int)total_threads;
	ST calculated_chunk = fiss_chunk + (counter*fiss_delta);
	ST start_index =test_then_add<ST>(RCAST(volatile ST *, &sh->u.s.iteration), (ST)calculated_chunk);
        ST end_index = calculated_chunk+start_index-1;
        if(end_index > total_iterations-1)
                end_index=total_iterations-1;
        if(start_index <= end_index)
        {
                status=1;
                init=start_index;
                limit=end_index;
                start = pr->u.p.lb;
                incr = pr->u.p.st;
                if (p_st != nullptr)
                        *p_st = incr;
                *p_lb = start + init * incr;
                *p_ub = start + limit * incr; 
		//printf("start %d end %d\n", *p_lb, *p_ub);
                if (pr->flags.ordered) {
                        pr->u.p.ordered_lower = init;
                        pr->u.p.ordered_upper = limit;
                }
        }
        else
        {
                status=0;
                //printf("end***********\n");
                *p_lb = 0;
                *p_ub = 0;
                if (p_st != nullptr)
                        *p_st = 0;
        }

}
break;
case kmp_sch_mfsc:{
	KD_TRACE(100, ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_mfsc case\n",gtid));
	ST calculated_chunk = (ST) pr->u.p.parm1;
	ST start_index =test_then_add<ST>(RCAST(volatile ST *, &sh->u.s.iteration), (ST)calculated_chunk);
        ST end_index = calculated_chunk+start_index-1;
	ST total_iterations = (ST) pr->u.p.tc;
        if(end_index > total_iterations-1)
                end_index=total_iterations-1;
	if(start_index <= end_index)
        {
                status=1;
                init=start_index;
                limit=end_index;
                start = pr->u.p.lb;
                incr = pr->u.p.st;
                if (p_st != nullptr)
                        *p_st = incr;
                
		*p_lb = start + init * incr;
                *p_ub = start + limit * incr;
		//printf("start %d end %d\n", *p_lb, *p_ub);
                if (pr->flags.ordered) {
                        pr->u.p.ordered_lower = init;
                        pr->u.p.ordered_upper = limit;
                }
        }
        else
        {
                status=0;
                //printf("end***********\n");
                *p_lb = 0;
                *p_ub = 0;
                if (p_st != nullptr)
                        *p_st = 0;
        }
	
}
break;
case kmp_sch_tfss:{
	KD_TRACE(100, ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_tfss case\n",gtid));
	ST tss_chunk = (ST) pr->u.p.parm1;
	ST tss_delta = (ST) pr->u.p.parm2;
	ST min_chunk = (ST) pr->u.p.parm3;
	ST steps = (ST) pr->u.p.parm4;
	ST total_threads= (ST) nproc;
	ST total_iterations = (ST) pr->u.p.tc;
	ST accum_chunk= 0;
	ST calculated_chunk=0;
	ST counter = (ST) test_then_inc<ST>((volatile ST *)&sh->u.s.counter);
	counter = (int)counter /(int)total_threads;
        ST temp_chunk= (tss_chunk - ((counter) * total_threads* tss_delta ));
	if(temp_chunk >= 0)
        {
                calculated_chunk = temp_chunk ;
        }
	else
	{
                calculated_chunk = tss_chunk - (steps  * tss_delta);
	}
	temp_chunk=calculated_chunk;
	accum_chunk=temp_chunk;
	for(int i=1; i<total_threads;i++)
        {       

                    accum_chunk+=temp_chunk- tss_delta;
                    temp_chunk-=tss_delta;
        }
        accum_chunk/=total_threads;
        calculated_chunk=ceil(accum_chunk);
	if(calculated_chunk < min_chunk)
                calculated_chunk = min_chunk;
      	ST start_index =test_then_add<ST>(RCAST(volatile ST *, &sh->u.s.iteration), (ST)calculated_chunk);
        ST end_index = calculated_chunk+start_index-1;
        if(end_index > total_iterations-1)
                end_index=total_iterations-1;
//      printf("start %d end %d calculated chunk %d\n", start_index, end_index, calculated_chunk);
        if(start_index <= end_index)
        {
                status=1;
                init=start_index;
                limit=end_index;
                start = pr->u.p.lb;
                incr = pr->u.p.st;
                if (p_st != nullptr)
                        *p_st = incr;
                *p_lb = start + init * incr;
                *p_ub = start + limit * incr;                 
		//printf("start %d end %d\n", *p_lb, *p_ub);
                if (pr->flags.ordered) {
                        pr->u.p.ordered_lower = init;
                        pr->u.p.ordered_upper = limit;
                }
        }
        else
        {
                status=0;
                //printf("end***********\n");
                *p_lb = 0;
                *p_ub = 0;
                if (p_st != nullptr)
                        *p_st = 0;
        } 
}
break;
 case kmp_sch_pls:{
 	ST static_part= (ST)pr->u.p.parm1; // static part;
	ST  min_chunk = (ST)pr->u.p.parm2; // minimum chunk size
	ST counter = (ST) test_then_inc<ST>((volatile ST *)&sh->u.s.counter);
	ST total_threads= (ST)nproc;
	ST calculated_chunk=0;
	ST total_iterations=pr->u.p.tc;
	ST new_total_iterations = total_iterations - (total_threads *static_part);
	KD_TRACE(100, ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_pls case\n",gtid));
	if(counter >= total_threads)
	{
		//follow gss
		ST scheduling_step= counter-total_threads;
		calculated_chunk = (int) ceil(pow((1-(1.0/total_threads)),scheduling_step)*new_total_iterations/total_threads );
	//	printf("calculated chunk %d step %d\n", calculated_chunk, scheduling_step);
	}
	else
	{
		//follow static
		calculated_chunk=static_part;
	}
	if(calculated_chunk < min_chunk)
		calculated_chunk = min_chunk;
	//printf("calculated chunk %d minimum %d \n", calculated_chunk, min_chunk);
	ST start_index =test_then_add<ST>(RCAST(volatile ST *, &sh->u.s.iteration), (ST)calculated_chunk); 
	ST end_index = calculated_chunk+start_index-1;
	if(end_index > total_iterations-1)
		end_index=total_iterations-1;
//	printf("start %d end %d calculated chunk %d\n", start_index, end_index, calculated_chunk);
	if(start_index <= end_index)
	{
		status=1;
		init=start_index;
		limit=end_index;
		start = pr->u.p.lb;
      		incr = pr->u.p.st;
      		if (p_st != nullptr)
        		*p_st = incr;
                *p_lb = start + init * incr;
                *p_ub = start + limit * incr; 		
		//printf("start %d end %d\n", *p_lb, *p_ub);
      		if (pr->flags.ordered) {
        		pr->u.p.ordered_lower = init;
        		pr->u.p.ordered_upper = limit;
		}
	}
	else
	{
		status=0;
		//printf("end***********\n");
		*p_lb = 0;
      		*p_ub = 0;
      		if (p_st != nullptr)
        		*p_st = 0;
	}
 }
break;
 case kmp_sch_awf: {
    UT counter; // factoring counter
    UT batch; // batch index
    T min_chunk = pr->u.p.parm3; // minimum chunk size
    double factor = pr->u.p.dbl_parm1;
    double weight = pr->u.p.dbl_parm2;
    
    KD_TRACE(100, ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_awf case\n",
                   gtid));
    trip = pr->u.p.tc;

    // atomically increase factoring counter
    counter = test_then_inc<ST>((volatile ST *)&sh->u.s.counter);

    // calculate current batch index
    batch = counter / nproc;

    while (true) {
      T cs; // chunk size
      ST remaining; // signed, because can be < 0
      init = sh->u.s.iteration; // shared value
      remaining = trip - init;
      if (remaining <= 0) { // AC: need to compare with 0 first
        // nothing to do, don't try atomic op
        status = 0;
        break;
      }

      // calculate current batch's chunk size
      T old_chunk = pr->u.p.parm1;
      UT old_batch = pr->u.p.parm2;
      if (batch == old_batch) { // old batch, already calculated
        cs = old_chunk;
      } else {
        // thread must calculate through to current batch
        // no synchronization needed, though
        for (UT i = 0; i < batch - old_batch; i++) {
          // update chunk with updated chunk
          old_chunk = ceil(old_chunk * factor); // chunk size
        }
        cs = old_chunk; // updated chunk now
        pr->u.p.parm1 = cs;
        pr->u.p.parm2 = batch;
      }

      // apply weight to chunk
      cs = (T)ceil(weight * cs);

      if (min_chunk > cs) { // check min chunk
        cs = min_chunk;
      }
      limit = init + cs;
      if (compare_and_swap<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                               (ST)init, (ST)limit)) {
        // CAS was successful, chunk obtained
        status = 1;
        if ((last = (limit >= trip)) != 0) // if last chunk
          limit = trip;
        --limit;
        break;
      } // if
    } // while

    if (status != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      if (p_st != nullptr)
        *p_st = incr;
      *p_lb = start + init * incr;
      *p_ub = start + limit * incr;

     // record chunk for this thread
    AWFData.at(cLoopName).workPerStep[tid] += *p_ub - *p_lb + 1;
    //{if (tid == 0)printf("chunksize: %d \n", *p_ub - *p_lb +1);}
 
      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } else 
    {
       
       // record thread finishing time
       double temp = __kmp_get_ticks2();
       AWFData.at(cLoopName).executionTimes[tid]  = temp - AWFData.at(cLoopName).executionTimes[tid];



       //add weighted work done per thread
       AWFData.at(cLoopName).sumWorkPerStep[tid] += AWFData.at(cLoopName).timeStep * AWFData.at(cLoopName).workPerStep[tid];

       //printf("%d, chunks: %d \n", tid, AWFData.at(cLoopName).workPerStep[tid]);

       // reset work per step
       AWFData.at(cLoopName).workPerStep[tid] = 0;


       //add weighted time per step
       AWFData.at(cLoopName).sumExecutionTimes[tid] += AWFData.at(cLoopName).timeStep * AWFData.at(cLoopName).executionTimes[tid];

       // last thread to enter
       int count = std::atomic_fetch_add(&AWFCounter,1);
       if(count == (nproc - 1))
       {
          AWFEnter   = 0; //reset flag
          AWFWait    = 1; //reset flag
          AWFCounter = 0; //reset flag


         // printf("[AWF] status == 0, step %d,  thread %d, weight %lf, time %lf\n", AWFData.at(cLoopName).timeStep,tid, AWFData.at(cLoopName).weights[tid],  AWFData.at(cLoopName).executionTimes[tid]);

          double awap = 0.0;

          for(T i=0; i< nproc; i++)
          {
             awap += (AWFData.at(cLoopName).sumExecutionTimes[i] / AWFData.at(cLoopName).sumWorkPerStep[i] ); 
          } 
          awap = awap / nproc;
          //printf("tid: %d, awap = %lf \n ",tid,awap);

          double trw = 0.0;
          for(T i=0; i< nproc; i++)
          {
            trw += awap/ AWFData.at(cLoopName).sumExecutionTimes[i] * AWFData.at(cLoopName).sumWorkPerStep[i] ;
          }

          for(T i=0; i< nproc; i++)
          {
            AWFData.at(cLoopName).weights[i] = awap / AWFData.at(cLoopName).sumExecutionTimes[i] * AWFData.at(cLoopName).sumWorkPerStep[i] * nproc/trw ;
          //  printf("%d, %lf \n", i, AWFData.at(cLoopName).weights[i]);
          }
             
         // printf("[AWF] status == 0, step %d,  thread %d, weight %lf, time %lf\n", AWFData.at(cLoopName).timeStep,tid, AWFData.at(cLoopName).weights[tid],  AWFData.at(cLoopName).executionTimes[tid]);
       }
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != nullptr)
        *p_st = 0;
    } // if
    
  } // case
  break;

  case kmp_sch_bold: {
    kmp_int64 t; // current value of bold_time
    kmp_int64 t2; // current time, start time of this chunk (including h)
    T my_bold = pr->u.p.parm1; // (last) chunk size of this PU
    DBL my_speed =
        pr->u.p.dbl_parm8; // current speed of this PU (iters/microseconds)
    kmp_int64 my_time = pr->u.p.l_parm1; // start time of last chunk of this PU
    T min_chunk = pr->u.p.parm3; // minimum chunk size

    DBL a = pr->u.p.dbl_parm1;
    DBL b = pr->u.p.dbl_parm2;
    DBL ln_b = pr->u.p.dbl_parm3;
    DBL p_inv = pr->u.p.dbl_parm4;
    DBL c1 = pr->u.p.dbl_parm5;
    DBL c2 = pr->u.p.dbl_parm6;
    DBL c3 = pr->u.p.dbl_parm7;
    KD_TRACE(100,
             ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_bold case\n", gtid));
    trip = pr->u.p.tc;

    // T mu = __kmp_env_mu;
    // T mu = std::stoi(getenv("KMP_MU"));
    T mu = currentMu;
    // std::cout << std::endl << "MU-VALUE: " << mu << std::endl;
    T h = __kmp_env_overhead;

#if KMP_DEBUG
    // Measure scheduling overhead
    double starttime;
    double endtime;
    starttime = __kmp_get_ticks2();
#endif
    // init and update times
    t = sh->u.s.bold_time;
    // microseconds are precise enough. If you prefer nano,
    // then change to nano everywhere, including user input!
    // kmp_int64 current_time = __kmp_get_micros();
    // kmp_int64 current_time = __kmp_get_nanos();
    kmp_int64 current_time = __kmp_get_ticks();
    sh->u.s.bold_time = current_time; // atomic not critical here
    t2 = current_time;

    // update bold_m, bold_n & total_speed (atomic might be good)
    DBL total_speed = (DBL)sh->u.s.total_speed / 1e6; // (iters/microseconds)

    ST bold_n_sub = 0;
    if (my_bold > 0) {
      bold_n_sub = (t2 - t) * total_speed + my_bold -
                   ((t2 - my_time) * (double)my_bold / (my_bold * mu + h));
      test_then_sub<ST>(&sh->u.s.bold_n, bold_n_sub);
      test_then_sub<ST>(&sh->u.s.bold_m, my_bold);
    }
    test_then_sub<ST>(&sh->u.s.total_speed, (ST)(my_speed * 1e6));

    while (true) {
      T cs; // chunk size
      ST remaining; // signed, because can be < 0
      init = sh->u.s.iteration; // shared value
      DBL bold_m = sh->u.s.bold_m;
      DBL bold_n = sh->u.s.bold_n;
      bold_n = bold_n < 0 ? 0 : bold_n;
      remaining = trip - init;
      if (remaining <= 0) { // AC: need to compare with 0 first
        // nothing to do, don't try atomic op
        status = 0;
        break;
      }

      // calculate chunk size with algorithm 2
      double q = (double)remaining / nproc;
      if (q <= 1) {
        cs = 1;
      } else {
        DBL r = (remaining > bold_n) ? (remaining) : bold_n;
        DBL bold_t = p_inv * r;
        DBL ln_q = log(q);
        DBL v = q / (b + q);
        DBL d = remaining / (1 + (1 / ln_q) - v);
        if (d <= c2) {
          cs = bold_t;
        } else {
          DBL s = a * (log(d) - c3) * (1 + bold_m / (r * nproc));
          DBL w = 0;
          if (b > 0) {
            w = log(v * ln_q) + ln_b;
          } else {
            w = log(ln_q);
          }
          DBL r1 = bold_t + s / 2 - sqrt(s * (bold_t + s / 4));
          DBL c1w = c1 * w;
          r1 += (0 > c1w) ? 0 : (c1w);
          cs = (r1 < bold_t) ? (r1) : (bold_t);
        }
      }

      if (min_chunk > cs) { // check min chunk
        cs = min_chunk;
      }

      limit = init + cs;
      if (compare_and_swap<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                               (ST)init, (ST)limit)) {
        // CAS was successful, chunk obtained
        // update variables
        pr->u.p.parm1 = cs;
        my_speed = (DBL)cs / (cs * mu + h);
        test_then_add<ST>(&sh->u.s.total_speed, (ST)(my_speed * 1e6));
#if KMP_DEBUG
        // printf("Thread %i: Bold_n=%f, Bold_m=%f, totalspeed=%f, myspeed=%f, "
        //        "next_chunk=%i, Q=%f, t2-t=%f, t2-t1=%f, t2=%lld, t1=%lld, "
        //        "bold_n_sub=%i\n",
        //        (int)tid, (double)bold_n, (double)bold_m, (double)total_speed,
        //        (double)my_speed, (int)cs, q, (double)(t2 - t),
        //        (double)(t2 - my_time), t2, my_time, (int)bold_n_sub);
#endif
        pr->u.p.dbl_parm8 = my_speed;
        pr->u.p.l_parm1 = t2;

        status = 1;
        if ((last = (limit >= trip)) != 0) // if last chunk
          limit = trip;
        --limit;
        break;
      } // if
    } // while

    if (status != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      if (p_st != nullptr)
        *p_st = incr;
      *p_lb = start + init * incr;
      *p_ub = start + limit * incr;
      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } else {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != nullptr)
        *p_st = 0;
    } // if
    
#if KMP_DEBUG
    endtime = __kmp_get_ticks2();
    printf("Thread %i: Scheduling took %lf microseconds.\n", (int)tid,
           endtime - starttime);
#endif
  } // case
  break;

  case kmp_sch_awf_b: {
    /* Timers are chunk execution time only without overhead. */
    UT counter; // factoring counter
    UT batch; // batch index
    UT chunk_index = pr->u.p.parm3;
    T min_chunk = pr->u.p.parm4; // minimum chunk size
    double factor = pr->u.p.dbl_parm1;
    double weight = pr->u.p.dbl_parm2;
    double sum_time = pr->u.p.dbl_parm3;
    double sum_iters = pr->u.p.dbl_parm4;
    double pi = 0; // my current weighted average ratio
    kmp_int64 t; // current time (start of new chunk + h)
    kmp_int64 my_time = pr->u.p.l_parm1; // start time of last chunk of this PU

    KD_TRACE(100, ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_awf_b case\n",
                   gtid));
    trip = pr->u.p.tc;
    // t = __kmp_get_micros();
    t = __kmp_get_ticks();
    kmp_int64 diff = t - my_time; // last chunk's execution time (only)

    // atomically increase factoring counter
    counter = test_then_inc<ST>((volatile ST *)&sh->u.s.counter);

    // update my weight after first chunk
    if (sum_iters != 0.0) {
      // update the accumulated time
      diff = diff > 0 ? diff : 1;
      sum_time += chunk_index * diff;
      pi = sum_time / sum_iters;
      { // protect the vector to avoid corruptness
        std::lock_guard<std::shared_timed_mutex> lg(mutexes[tid]);
        sh->u.s.dbl_vector[tid] = pi;
      }
      double pi_sum = 0;
      double pis[nproc]; // copy of shared vector
      T sum_p = 1; // the # of PUs who already finished >0 chunks
      for (std::size_t i = 0; i != sh->u.s.dbl_vector.size(); ++i) {
        double n = 0;
        { // protect the vector to avoid corruptness
          std::shared_lock<std::shared_timed_mutex> lg(mutexes[i]);
          n = sh->u.s.dbl_vector[i];
        }
        if ((T)i == tid) { // to be sure it is the updated pi
          pis[i] = pi;
          pi_sum += pi;
        } else {
          pis[i] = n;
          pi_sum += n;
          if (n != 0) {
            sum_p++;
          }
        }
      }
      double pi_ = pi_sum / sum_p; // avg weighted avg ratio
      double raw = pi_ / pi; // my raw weight
      double raw_sum = 0;
      for (const auto &n : pis) {
        if (n != 0) {
          raw_sum += pi_ / n;
        }
      }
      weight = raw * sum_p / raw_sum;
#if KMP_DEBUG
      printf("Thread %i: pi_sum=%f, pi_=%f, raw=%f, raw_sum=%f\n", (int)tid,
             pi_sum, pi_, raw, raw_sum);
#endif
    }
#if KMP_DEBUG
    printf("Thread %i: My chunk index = %i, My weight = %f, pi=%f, "
           "sum_iters=%f, sum_time=%f, diff=%lld\n",
           (int)tid, (int)chunk_index, weight, pi, sum_iters, sum_time, diff);
#endif

    // calculate current batch index
    batch = counter / nproc;

    while (true) {
      T cs; // chunk size
      ST remaining; // signed, because can be < 0
      init = sh->u.s.iteration; // shared value
      remaining = trip - init;
      if (remaining <= 0) { // AC: need to compare with 0 first
        // nothing to do, don't try atomic op
        status = 0;
        break;
      }

      // calculate current batch's chunk size
      T old_chunk = pr->u.p.parm1;
      UT old_batch = pr->u.p.parm2;
      if (batch == old_batch) { // old batch, already calculated
        cs = old_chunk;
      } else {
        // thread must calculate through to current batch
        // no synchronization needed, though
        for (UT i = 0; i < batch - old_batch; i++) {
          // update chunk with updated chunk
          old_chunk = ceil(old_chunk * factor); // chunk size
        }
        cs = old_chunk; // updated chunk now
        pr->u.p.parm1 = cs;
        pr->u.p.parm2 = batch;
      }

      // apply weight to chunk
      cs = (T)ceil(weight * cs);

      if (min_chunk > cs) { // check min chunk
        cs = min_chunk;
      }

      limit = init + cs;
      if (compare_and_swap<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                               (ST)init, (ST)limit)) {
        // CAS was successful, chunk obtained
        status = 1;

        // update accumulated iterations
        pr->u.p.parm3 = ++chunk_index;
        sum_iters += chunk_index * cs;
        pr->u.p.dbl_parm3 = sum_time;
        pr->u.p.dbl_parm4 = sum_iters;
        pr->u.p.l_parm1 = __kmp_get_ticks();

        if ((last = (limit >= trip)) != 0) // if last chunk
          limit = trip;
        --limit;
        break;
      } // if
    } // while

    if (status != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      if (p_st != nullptr)
        *p_st = incr;
      *p_lb = start + init * incr;
      *p_ub = start + limit * incr;
      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } else {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != nullptr)
        *p_st = 0;
    } // if
    
  } // case
  break;

  case kmp_sch_awf_c: {
    /* Very similar to AWF-B but chunked instead of batched. */
    /* Timers are chunk execution time only without overhead. */
    T min_chunk = pr->u.p.parm2; // minimum chunk size
    UT chunk_index = pr->u.p.parm3;
    double factor = pr->u.p.dbl_parm1;
    double weight = pr->u.p.dbl_parm2;
    double sum_time = pr->u.p.dbl_parm3;
    double sum_iters = pr->u.p.dbl_parm4;
    double pi = 0; // my current weighted average ratio
    kmp_int64 t; // current time (start of new chunk + h)
    kmp_int64 my_time = pr->u.p.l_parm1; // start time of last chunk of this PU

    KD_TRACE(100, ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_awf_c case\n",
                   gtid));
    trip = pr->u.p.tc;
    // t = __kmp_get_micros();
    t = __kmp_get_ticks();
    kmp_int64 diff = t - my_time; // last chunk's execution time (only)

    // atomically increase factoring counter
    test_then_inc<ST>((volatile ST *)&sh->u.s.counter);

    // update my weight after first chunk
    if (sum_iters != 0.0) {
      // update the accumulated time
      diff = diff > 0 ? diff : 1;
      sum_time += chunk_index * diff;
      pi = sum_time / sum_iters;
      { // protect the vector to avoid corruptness
        std::lock_guard<std::shared_timed_mutex> lg(mutexes[tid]);
        sh->u.s.dbl_vector[tid] = pi;
      }
      double pi_sum = 0;
      double pis[nproc]; // copy of shared vector
      T sum_p = 1; // the # of PUs who already finished >0 chunks
      for (std::size_t i = 0; i != sh->u.s.dbl_vector.size(); ++i) {
        double n = 0;
        { // protect the vector to avoid corruptness
          std::shared_lock<std::shared_timed_mutex> lg(mutexes[i]);
          n = sh->u.s.dbl_vector[i];
        }
        if ((T)i == tid) { // to be sure it is the updated pi
          pis[i] = pi;
          pi_sum += pi;
        } else {
          pis[i] = n;
          pi_sum += n;
          if (n != 0) {
            sum_p++;
          }
        }
      }
      double pi_ = pi_sum / sum_p; // avg weighted avg ratio
      double raw = pi_ / pi; // my raw weight
      double raw_sum = 0;
      for (const auto &n : pis) {
        if (n != 0) {
          raw_sum += pi_ / n;
        }
      }
      weight = raw * sum_p / raw_sum;
#if KMP_DEBUG
      printf("Thread %i: pi_sum=%f, pi_=%f, raw=%f, raw_sum=%f\n", (int)tid,
             pi_sum, pi_, raw, raw_sum);
#endif
    }
#if KMP_DEBUG
    printf("Thread %i: My chunk index = %i, My weight = %f, pi=%f, "
           "sum_iters=%f, sum_time=%f, diff=%lld\n",
           (int)tid, (int)chunk_index, weight, pi, sum_iters, sum_time, diff);
#endif

    while (true) {
      T cs; // chunk size
      ST remaining; // signed, because can be < 0
      init = sh->u.s.iteration; // shared value
      remaining = trip - init;
      if (remaining <= 0) { // AC: need to compare with 0 first
        // nothing to do, don't try atomic op
        status = 0;
        break;
      }

      // Calculate current chunk size
      // AWF-C always calculates a new chunk for each request.
      if (chunk_index == 0) { // first chunk, already calculated
        cs = pr->u.p.parm1;
      } else {
        cs = ceil(remaining * factor); // chunk size
        pr->u.p.parm1 = cs;
      }

      // apply weight to chunk
      cs = (T)ceil(weight * cs);

      if (min_chunk > cs) { // check min chunk
        cs = min_chunk;
      }

      limit = init + cs;
      if (compare_and_swap<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                               (ST)init, (ST)limit)) {
        // CAS was successful, chunk obtained
        status = 1;

        // update accumulated iterations
        pr->u.p.parm3 = ++chunk_index;
        sum_iters += chunk_index * cs;
        pr->u.p.dbl_parm3 = sum_time;
        pr->u.p.dbl_parm4 = sum_iters;
        pr->u.p.l_parm1 = __kmp_get_ticks();

        if ((last = (limit >= trip)) != 0) // if last chunk
          limit = trip;
        --limit;
        break;
      } // if
    } // while

    if (status != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      if (p_st != nullptr)
        *p_st = incr;
      *p_lb = start + init * incr;
      *p_ub = start + limit * incr;
      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } else {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != nullptr)
        *p_st = 0;
    } // if
    
  } // case
  break;

  case kmp_sch_awf_d: {
    /* Identical to AWF-B but with total times. */
    UT counter; // factoring counter
    UT batch; // batch index
    UT chunk_index = pr->u.p.parm3;
    T min_chunk = pr->u.p.parm4; // minimum chunk size
    double factor = pr->u.p.dbl_parm1;
    double weight = pr->u.p.dbl_parm2;
    double sum_time = pr->u.p.dbl_parm3;
    double sum_iters = pr->u.p.dbl_parm4;
    double pi = 0; // my current weighted average ratio
    kmp_int64 t; // current time (start of new chunk + h)
    kmp_int64 my_time = pr->u.p.l_parm1; // start time of last chunk of this PU

    KD_TRACE(100, ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_awf_d case\n",
                   gtid));
    trip = pr->u.p.tc;
    //t = __kmp_get_micros();
    t = __kmp_get_ticks();
    kmp_int64 diff = t - my_time; // last chunk's execution time (+h)

    // atomically increase factoring counter
    counter = test_then_inc<ST>((volatile ST *)&sh->u.s.counter);

    // update my weight after first chunk
    if (sum_iters != 0.0) {
      // update the accumulated time
      diff = diff > 0 ? diff : 1;
      sum_time += chunk_index * diff;
      pi = sum_time / sum_iters;
      { // protect the vector to avoid corruptness
        std::lock_guard<std::shared_timed_mutex> lg(mutexes[tid]);
        sh->u.s.dbl_vector[tid] = pi;
      }
      double pi_sum = 0;
      double pis[nproc]; // copy of shared vector
      T sum_p = 1; // the # of PUs who already finished >0 chunks
      for (std::size_t i = 0; i != sh->u.s.dbl_vector.size(); ++i) {
        double n = 0;
        { // protect the vector to avoid corruptness
          std::shared_lock<std::shared_timed_mutex> lg(mutexes[i]);
          n = sh->u.s.dbl_vector[i];
        }
        if ((T)i == tid) { // to be sure it is the updated pi
          pis[i] = pi;
          pi_sum += pi;
        } else {
          pis[i] = n;
          pi_sum += n;
          if (n != 0) {
            sum_p++;
          }
        }
      }
      double pi_ = pi_sum / sum_p; // avg weighted avg ratio
      double raw = pi_ / pi; // my raw weight
      double raw_sum = 0;
      for (const auto &n : pis) {
        if (n != 0) {
          raw_sum += pi_ / n;
        }
      }
      weight = raw * sum_p / raw_sum;
#if KMP_DEBUG
      printf("Thread %i: pi_sum=%f, pi_=%f, raw=%f, raw_sum=%f\n", (int)tid,
             pi_sum, pi_, raw, raw_sum);
#endif
    }
#if KMP_DEBUG
    printf("Thread %i: My chunk index = %i, My weight = %f, pi=%f, "
           "sum_iters=%f, sum_time=%f, diff=%lld\n",
           (int)tid, (int)chunk_index, weight, pi, sum_iters, sum_time, diff);
#endif

    // calculate current batch index
    batch = counter / nproc;

    while (true) {
      T cs; // chunk size
      ST remaining; // signed, because can be < 0
      init = sh->u.s.iteration; // shared value
      remaining = trip - init;
      if (remaining <= 0) { // AC: need to compare with 0 first
        // nothing to do, don't try atomic op
        status = 0;
        break;
      }

      // calculate current batch's chunk size
      T old_chunk = pr->u.p.parm1;
      UT old_batch = pr->u.p.parm2;
      if (batch == old_batch) { // old batch, already calculated
        cs = old_chunk;
      } else {
        // thread must calculate through to current batch
        // no synchronization needed, though
        for (UT i = 0; i < batch - old_batch; i++) {
          // update chunk with updated chunk
          old_chunk = ceil(old_chunk * factor); // chunk size
        }
        cs = old_chunk; // updated chunk now
        pr->u.p.parm1 = cs;
        pr->u.p.parm2 = batch;
      }

      // apply weight to chunk
      cs = (T)ceil(weight * cs);

      if (min_chunk > cs) { // check min chunk
        cs = min_chunk;
      }

      limit = init + cs;
      if (compare_and_swap<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                               (ST)init, (ST)limit)) {
        // CAS was successful, chunk obtained
        status = 1;

        // update accumulated iterations
        pr->u.p.parm3 = ++chunk_index;
        sum_iters += chunk_index * cs;
        pr->u.p.dbl_parm3 = sum_time;
        pr->u.p.dbl_parm4 = sum_iters;
        pr->u.p.l_parm1 = t;

        if ((last = (limit >= trip)) != 0) // if last chunk
          limit = trip;
        --limit;
        break;
      } // if
    } // while

    if (status != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      if (p_st != nullptr)
        *p_st = incr;
      *p_lb = start + init * incr;
      *p_ub = start + limit * incr;
      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } else {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != nullptr)
        *p_st = 0;
    } // if
    
  } // case
  break;

  case kmp_sch_awf_e: {
    /* Identical to AWF-C but with total times (+h). */
    T min_chunk = pr->u.p.parm2; // minimum chunk size
    UT chunk_index = pr->u.p.parm3;
    double factor = pr->u.p.dbl_parm1;
    double weight = pr->u.p.dbl_parm2;
    double sum_time = pr->u.p.dbl_parm3;
    double sum_iters = pr->u.p.dbl_parm4;
    double pi = 0; // my current weighted average ratio
    kmp_int64 t; // current time (start of new chunk + h)
    kmp_int64 my_time = pr->u.p.l_parm1; // start time of last chunk of this PU

    KD_TRACE(100, ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_awf_e case\n",
                   gtid));
    trip = pr->u.p.tc;
    // t = __kmp_get_micros();
    t = __kmp_get_ticks();
    kmp_int64 diff = t - my_time; // last chunk's execution time (+h)

    // atomically increase factoring counter
    test_then_inc<ST>((volatile ST *)&sh->u.s.counter);

    // update my weight after first chunk
    if (sum_iters != 0.0) {
      // update the accumulated time
      diff = diff > 0 ? diff : 1;
      sum_time += chunk_index * diff;
      pi = sum_time / sum_iters;
      { // protect the vector to avoid corruptness
        std::lock_guard<std::shared_timed_mutex> lg(mutexes[tid]);
        sh->u.s.dbl_vector[tid] = pi;
      }
      double pi_sum = 0;
      double pis[nproc]; // copy of shared vector
      T sum_p = 1; // the # of PUs who already finished >0 chunks
      for (std::size_t i = 0; i != sh->u.s.dbl_vector.size(); ++i) {
        double n = 0;
        { // protect the vector to avoid corruptness
          std::shared_lock<std::shared_timed_mutex> lg(mutexes[i]);
          n = sh->u.s.dbl_vector[i];
        }
        if ((T)i == tid) { // to be sure it is the updated pi
          pis[i] = pi;
          pi_sum += pi;
        } else {
          pis[i] = n;
          pi_sum += n;
          if (n != 0) {
            sum_p++;
          }
        }
      }
      double pi_ = pi_sum / sum_p; // avg weighted avg ratio
      double raw = pi_ / pi; // my raw weight
      double raw_sum = 0;
      for (const auto &n : pis) {
        if (n != 0) {
          raw_sum += pi_ / n;
        }
      }
      weight = raw * sum_p / raw_sum;
#if KMP_DEBUG
      printf("Thread %i: pi_sum=%f, pi_=%f, raw=%f, raw_sum=%f\n", (int)tid,
             pi_sum, pi_, raw, raw_sum);
#endif
    }
#if KMP_DEBUG
    printf("Thread %i: My chunk index = %i, My weight = %f, pi=%f, "
           "sum_iters=%f, sum_time=%f, diff=%lld\n",
           (int)tid, (int)chunk_index, weight, pi, sum_iters, sum_time, diff);
#endif
    // printf("Thread %i: My chunk index = %i, My weight = %f, pi=%f, " "sum_iters=%f, sum_time=%f, diff=%lld\n", (int)tid, (int)chunk_index, weight, pi, sum_iters, sum_time, diff);

    while (true) {
      T cs; // chunk size
      ST remaining; // signed, because can be < 0
      init = sh->u.s.iteration; // shared value
      remaining = trip - init;
      if (remaining <= 0) { // AC: need to compare with 0 first
        // nothing to do, don't try atomic op
        status = 0;
        break;
      }

      // Calculate current chunk size
      // AWF-C always calculates a new chunk for each request.
      if (chunk_index == 0) { // first chunk, already calculated
        cs = pr->u.p.parm1;
      } else {
        cs = ceil(remaining * factor); // chunk size
        pr->u.p.parm1 = cs;
      }

      // apply weight to chunk
      cs = (T)ceil(weight * cs);

      if (min_chunk > cs) { // check min chunk
        cs = min_chunk;
      }
      limit = init + cs;
      if (compare_and_swap<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                               (ST)init, (ST)limit)) {
        // CAS was successful, chunk obtained
        status = 1;

        // update accumulated iterations
        pr->u.p.parm3 = ++chunk_index;
        sum_iters += chunk_index * cs;
        pr->u.p.dbl_parm3 = sum_time;
        pr->u.p.dbl_parm4 = sum_iters;
        pr->u.p.l_parm1 = t;

        if ((last = (limit >= trip)) != 0) // if last chunk
          limit = trip;
        --limit;
        break;
      } // if
    } // while
 
    if (status != 0) {

      start = pr->u.p.lb;
      incr = pr->u.p.st;
      if (p_st != nullptr)
        *p_st = incr;
      *p_lb = start + init * incr;
      *p_ub = start + limit * incr;
        
      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } else {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != nullptr)
        *p_st = 0;
    } // if
    
	
  } // case
  break;

  case kmp_sch_af: {
  	// printf("Decrement Count %d \n", count);
    /* Default divider is 10. */
    UT chunk_index = pr->u.p.parm1;
    UT chunk_size = pr->u.p.parm2;
    T sub_chunk_size = pr->u.p.parm3;
    UT last_sub_chunk = pr->u.p.parm4;
    DBL chunk_remaining = pr->u.p.dbl_parm1;
    DBL divider = pr->u.p.dbl_parm2;
    DBL sum_avg = pr->u.p.dbl_parm3;
    DBL sum2_avg = pr->u.p.dbl_parm4;
    DBL counter = pr->u.p.dbl_parm5;
    T num_sub_chunks = pr->u.p.dbl_parm6;
    T min_chunk = pr->u.p.dbl_parm7;

    kmp_int64 t; // current time (start of new chunk + h)
    kmp_int64 my_time = pr->u.p.l_parm1; // start time of last chunk of this PU

    KD_TRACE(100,
             ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_af case\n", gtid));
    trip = pr->u.p.tc;
    // t = __kmp_get_micros();
    t = __kmp_get_ticks();
    DBL diff = t - my_time; // last sub-chunk's execution time (no h)

    // save the avg iteration time of executed sub-chunk
    DBL avg_time;
    DBL mu = 0;
    DBL sigma = 0;
    bool finished = false;
    if (last_sub_chunk > 0) { // a sub-chunk was executed
      avg_time = diff / last_sub_chunk;
      sum_avg += avg_time;
      sum2_avg += avg_time * avg_time;
    }
    if (chunk_remaining == 0) { // it was the last sub-chunk
      finished = true;
      // calculate the mean and sigma and share it
      mu = sum_avg / counter;
      if (counter == 1) {
        sigma = 0;
      } else {
        sigma = pow((sum2_avg - counter * mu * mu) / (counter - 1), 0.5);
      }
      { // protect the vector to avoid corruptness
        std::lock_guard<std::shared_timed_mutex> lg(mutexes[tid]);
        sh->u.s.dbl_vector[tid] = mu;
        sh->u.s.dbl_vector2[tid] = sigma;
      }
    }
#if KMP_DEBUG
    printf("Thread %i: Counter = %f, last_sub=%i, chunk_remaining=%f, My "
           "mean=%f, sigma=%f, diff=%f.\n",
           (int)tid, (double)counter, (int)last_sub_chunk,
           (double)chunk_remaining, (double)mu, (double)sigma, (double)diff);
#endif
    while (true) {

      T cs; // sub-chunk size
      ST remaining; // signed, because can be < 0
      init = sh->u.s.iteration; // shared value
      remaining = trip - init;
      if (remaining <= 0) { // AC: need to compare with 0 first
        // nothing to do, don't try atomic op
        status = 0;
        break;
      }

      // proceed with next sub-chunk
      if (!finished) {
        T new_r = chunk_remaining - sub_chunk_size;
        if (new_r > 0 && new_r < sub_chunk_size) {
          cs = chunk_remaining;
        } else {
          cs = sub_chunk_size;
        }
      } else {
        // calculate new chunk size and its sub-chunk
        DBL dn = 0;
        DBL tn = 0;
        for (int i = 0; i < (int)nproc; i++) {
          DBL mui = 0;
          DBL sdi = 0;
          if (i == (int)tid) { // take my mu and sigma directly
            mui = mu;
            sdi = sigma;
          } else { // take the other's shared values
            { // protect the vector to avoid corruptness
              std::shared_lock<std::shared_timed_mutex> lg(mutexes[i]);
              mui = sh->u.s.dbl_vector[i];
              sdi = sh->u.s.dbl_vector2[i];
            }
          }
          if (mui > 0) {
            dn += pow(sdi, 2.0) / mui;
            tn += 1 / mui;
          } else { // take my own values since other's are not ready yet
            dn += pow(sigma, 2.0) / mu;
            tn += 1 / mu;
          }
        }
        tn = pow(tn, -1.0);
        cs = ceil((dn + 2 * tn * remaining -
                   sqrt(dn * dn + 4 * dn * tn * remaining)) /
                  (2.0 * mu));
        chunk_size = cs;
        chunk_remaining = chunk_size;

        if (cs >= num_sub_chunks) { // produce sub-chunks only if big enough
          cs *= divider;
        }
        if (min_chunk > cs) { // check min chunk
        	cs = min_chunk;
      	}

        sub_chunk_size = cs;
#if KMP_DEBUG
        printf("Thread %i: New chunk with d=%f, t=%f, remaining=%i and "
               "chunk=%i.\n",
               (int)tid, (double)dn, (double)tn, (int)remaining,
               (int)chunk_size);
#endif
      }
       

      limit = init + cs;
      if (compare_and_swap<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                               (ST)init, (ST)limit)) {
        // CAS was successful, sub-chunk obtained
        status = 1;

        // update variables
        if (finished) {
          pr->u.p.parm1 = ++chunk_index;
        }
        pr->u.p.parm2 = chunk_size;
        pr->u.p.parm3 = sub_chunk_size;
        pr->u.p.parm4 = cs;
        pr->u.p.dbl_parm1 = chunk_remaining - cs;
        pr->u.p.dbl_parm3 = sum_avg;
        pr->u.p.dbl_parm4 = sum2_avg;
        pr->u.p.dbl_parm5 = ++counter;
        // pr->u.p.l_parm1 = __kmp_get_micros();
        pr->u.p.l_parm1 = __kmp_get_ticks();

        if ((last = (limit >= trip)) != 0) // if last chunk
          limit = trip;
        --limit;
        break;
      } // if
    } // while
    if (status != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      if (p_st != nullptr)
        *p_st = incr;
      *p_lb = start + init * incr;
      *p_ub = start + limit * incr;
      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } else {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != nullptr)
        *p_st = 0;
    } // if
    // printf("CHEGUEIII7%d \n", tid); 
    // 
  } // case
  break;

  case kmp_sch_af_a: {
    /* Identical to original AF but with total times (+h). */
    /* And 10 as divider --> 10 sub chunks. */
    UT chunk_index = pr->u.p.parm1;
    UT chunk_size = pr->u.p.parm2;
    T sub_chunk_size = pr->u.p.parm3;
    UT last_sub_chunk = pr->u.p.parm4;
    DBL chunk_remaining = pr->u.p.dbl_parm1;
    DBL divider = pr->u.p.dbl_parm2;
    DBL sum_avg = pr->u.p.dbl_parm3;
    DBL sum2_avg = pr->u.p.dbl_parm4;
    DBL counter = pr->u.p.dbl_parm5;
    T num_sub_chunks = pr->u.p.dbl_parm6;
    T min_chunk = pr->u.p.dbl_parm7;
    kmp_int64 t; // current time (start of new chunk + h)
    kmp_int64 my_time = pr->u.p.l_parm1; // start time of last chunk of this PU

    KD_TRACE(100,
             ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_af_a case\n", gtid));
    trip = pr->u.p.tc;
    // t = __kmp_get_micros();
    t = __kmp_get_ticks();
    DBL diff = t - my_time; // last sub-chunk's execution time (+ h)

    // save the avg iteration time of executed sub-chunk
    DBL avg_time;
    DBL mu = 0;
    DBL sigma = 0;
    bool finished = false;
    if (last_sub_chunk > 0) { // a sub-chunk was executed
      avg_time = diff / last_sub_chunk;
      sum_avg += avg_time;
      sum2_avg += avg_time * avg_time;
    }
    if (chunk_remaining == 0) { // it was the last sub-chunk
      finished = true;
      // calculate the mean and sigma and share it
      mu = sum_avg / counter;
      if (counter == 1) {
        sigma = 0;
      } else {
        sigma = pow((sum2_avg - counter * mu * mu) / (counter - 1), 0.5);
      }
      { // protect the vector to avoid corruptness
        std::lock_guard<std::shared_timed_mutex> lg(mutexes[tid]);
        sh->u.s.dbl_vector[tid] = mu;
        sh->u.s.dbl_vector2[tid] = sigma;
      }
    }
#if KMP_DEBUG
    printf("Thread %i: Counter = %f, last_sub=%i, chunk_remaining=%f, My "
           "mean=%f, sigma=%f, diff=%f.\n",
           (int)tid, (double)counter, (int)last_sub_chunk,
           (double)chunk_remaining, (double)mu, (double)sigma, (double)diff);
#endif

    while (true) {
      T cs; // sub-chunk size
      ST remaining; // signed, because can be < 0
      init = sh->u.s.iteration; // shared value
      remaining = trip - init;
      if (remaining <= 0) { // AC: need to compare with 0 first
        // nothing to do, don't try atomic op
        status = 0;
        break;
      }

      // proceed with next sub-chunk
      if (!finished) {
        T new_r = chunk_remaining - sub_chunk_size;
        if (new_r > 0 && new_r < sub_chunk_size) {
          cs = chunk_remaining;
        } else {
          cs = sub_chunk_size;
        }
      } else {
        // calculate new chunk size and its sub-chunk
        DBL dn = 0;
        DBL tn = 0;
        for (int i = 0; i < (int)nproc; i++) {
          DBL mui = 0;
          DBL sdi = 0;
          if (i == (int)tid) { // take my mu and sigma directly
            mui = mu;
            sdi = sigma;
          } else { // take the other's shared values
            { // protect the vector to avoid corruptness
              std::shared_lock<std::shared_timed_mutex> lg(mutexes[i]);
              mui = sh->u.s.dbl_vector[i];
              sdi = sh->u.s.dbl_vector2[i];
            }
          }
          if (mui > 0) {
            dn += pow(sdi, 2.0) / mui;
            tn += 1 / mui;
          } else { // take my own values since other's are not ready yet
            dn += pow(sigma, 2.0) / mu;
            tn += 1 / mu;
          }
        }
        tn = pow(tn, -1.0);
        cs = ceil((dn + 2 * tn * remaining -
                   sqrt(dn * dn + 4 * dn * tn * remaining)) /
                  (2.0 * mu));
        chunk_size = cs;
        chunk_remaining = chunk_size;

        if (cs >= num_sub_chunks) { // produce sub-chunks only if big enough
          cs *= divider;
        }

        if (min_chunk > cs) { // check min chunk
        	cs = min_chunk;
      	}

        sub_chunk_size = cs;
#if KMP_DEBUG
        printf("Thread %i: New chunk with d=%f, t=%f, remaining=%i and "
               "chunk=%i.\n",
               (int)tid, (double)dn, (double)tn, (int)remaining,
               (int)chunk_size);
#endif
      }

      limit = init + cs;
      if (compare_and_swap<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                               (ST)init, (ST)limit)) {
        // CAS was successful, sub-chunk obtained
        status = 1;

        // update variables
        if (finished) {
          pr->u.p.parm1 = ++chunk_index;
        }
        pr->u.p.parm2 = chunk_size;
        pr->u.p.parm3 = sub_chunk_size;
        pr->u.p.parm4 = cs;
        pr->u.p.dbl_parm1 = chunk_remaining - cs;
        pr->u.p.dbl_parm3 = sum_avg;
        pr->u.p.dbl_parm4 = sum2_avg;
        pr->u.p.dbl_parm5 = ++counter;
        // pr->u.p.l_parm1 = __kmp_get_micros();
        pr->u.p.l_parm1 = t;

        if ((last = (limit >= trip)) != 0) // if last chunk
          limit = trip;
        --limit;
        break;
      } // if
    } // while

    if (status != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      if (p_st != nullptr)
        *p_st = incr;
      *p_lb = start + init * incr;
      *p_ub = start + limit * incr;
      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } else {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != nullptr)
        *p_st = 0;
    } // if
    
  } // case
  break;

  case kmp_sch_profiling: {
    // if (tid == 0) {
    // T chunk = pr->u.p.parm1; // the chunk size
      T chunk = 1;
    kmp_int64 t; // current time (start of new chunk)
    kmp_int64 my_time = pr->u.p.l_parm1; // start time of last chunk of this PU

    t = __kmp_get_ticks();
    DBL diff = t - my_time; // last sub-chunk's execution time (no h)
    DBL avg_iteration_time = diff / chunk;
    // put the iteration execution time to shared vector
    if (my_time != 0) { // not before first chunk
      std::lock_guard<std::mutex> lg(sh->u.s.mtx);
      sh->u.s.dbl_vector.push_back(avg_iteration_time);
    }

    KD_TRACE(
        100,
        ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_profiling case\n", gtid));

    // get old and then increase chunk index atomically
    init = chunk * test_then_inc_acq<ST>((volatile ST *)&sh->u.s.iteration);
    trip = pr->u.p.tc - 1;

    if ((status = (init <= trip)) == 0) { // no iterations left...
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != nullptr)
        *p_st = 0;
      // No more iterations left. It's time to calculate mean and sigma
      if (tid == 0) {
        std::lock_guard<std::mutex> lg(sh->u.s.mtx);
        T n = sh->u.s.dbl_vector.size();
        DBL mean;
        DBL sigma;
        DBL sum = 0;
        DBL sum_squares = 0;
        for (auto const &value : sh->u.s.dbl_vector) {
          sum += value;
        }
        mean = sum / n;
        for (auto const &value : sh->u.s.dbl_vector) {
          sum_squares += pow(value - mean, 2);
        }
        sigma = sqrt(sum_squares / (n - 1));
        char* fileData = std::getenv("KMP_PROFILE_DATA");
        if(fileData==NULL || strcmp(fileData,"")==0)
    	{
    		std::cout<<"Please export KMP_PROFILE_DATA in your environment with the path for the profile output data\n";
    		exit(-1);
    	}
    	std::fstream ofs;
    	ofs.open(fileData, std::ofstream::out | std::ofstream::app);
        ofs <<"Mean "<< mean << " Sigma " << sigma <<" iterations " << n << std::endl;
        ofs.close();
      }
    } else {
      start = pr->u.p.lb;
      limit = chunk + init - 1;
      incr = pr->u.p.st;

      if ((last = (limit >= trip)) != 0)
        limit = trip;

      if (p_st != nullptr)
        *p_st = incr;

      if (incr == 1) {
        *p_lb = start + init;
        *p_ub = start + limit;
      } else {
        *p_lb = start + init * incr;
        *p_ub = start + limit * incr;
      }

      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } // if
    t = __kmp_get_ticks();
    pr->u.p.l_parm1 = t; // stop time again for next iteration
    // }
  } // case
  break;
    /* --------------------------LB4OMP_extensions-----------------------------*/
  default: {
    status = 0; // to avoid complaints on uninitialized variable use
    __kmp_fatal(KMP_MSG(UnknownSchedTypeDetected), // Primary message
                KMP_HNT(GetNewerLibrary), // Hint
                __kmp_msg_null // Variadic argument list terminator
                );
  } break;
  } // switch
  //printf("status %d tid %d\n", (int)status, tid);
  
  if (p_last)
    *p_last = last;
#ifdef KMP_DEBUG
  if (pr->flags.ordered) {
    char *buff;
    // create format specifiers before the debug output
    buff = __kmp_str_format("__kmp_dispatch_next_algorithm: T#%%d "
                            "ordered_lower:%%%s ordered_upper:%%%s\n",
                            traits_t<UT>::spec, traits_t<UT>::spec);
    KD_TRACE(1000, (buff, gtid, pr->u.p.ordered_lower, pr->u.p.ordered_upper));
    __kmp_str_free(&buff);
  }
  {
    char *buff;
    // create format specifiers before the debug output
    buff = __kmp_str_format(
        "__kmp_dispatch_next_algorithm: T#%%d exit status:%%d p_last:%%d "
        "p_lb:%%%s p_ub:%%%s p_st:%%%s\n",
        traits_t<T>::spec, traits_t<T>::spec, traits_t<ST>::spec);
    KD_TRACE(10, (buff, gtid, status, *p_last, *p_lb, *p_ub, *p_st));
    __kmp_str_free(&buff);
  }
#endif
  /* --------------------------LB4OMP_extensions-----------------------------*/
#if KMP_DEBUG
  /* Debug prints for correctness checks. Comment out else. */
  print_chunks(status, p_lb, p_ub, tid, pr);
#endif
  /* --------------------------LB4OMP_extensions-----------------------------*/
  if(status == 0){
    LOOP_TIME_MEASURE_END
  }else{
    STORE_CHUNK_INFO
  }
  return status;
}

/* Define a macro for exiting __kmp_dispatch_next(). If status is 0 (no more
   work), then tell OMPT the loop is over. In some cases kmp_dispatch_fini()
   is not called. */
#if OMPT_SUPPORT && OMPT_OPTIONAL
#define OMPT_LOOP_END                                                          \
  if (status == 0) {                                                           \
    if (ompt_enabled.ompt_callback_work) {                                     \
      ompt_team_info_t *team_info = __ompt_get_teaminfo(0, NULL);              \
      ompt_task_info_t *task_info = __ompt_get_task_info_object(0);            \
      ompt_callbacks.ompt_callback(ompt_callback_work)(                        \
          ompt_work_loop, ompt_scope_end, &(team_info->parallel_data),         \
          &(task_info->task_data), 0, codeptr);                                \
    }                                                                          \
  }
// TODO: implement count
#else
#define OMPT_LOOP_END // no-op
#endif

#if KMP_STATS_ENABLED
#define KMP_STATS_LOOP_END                                                     \
  {                                                                            \
    kmp_int64 u, l, t, i;                                                      \
    l = (kmp_int64)(*p_lb);                                                    \
    u = (kmp_int64)(*p_ub);                                                    \
    i = (kmp_int64)(pr->u.p.st);                                               \
    if (status == 0) {                                                         \
      t = 0;                                                                   \
      KMP_POP_PARTITIONED_TIMER();                                             \
    } else if (i == 1) {                                                       \
      if (u >= l)                                                              \
        t = u - l + 1;                                                         \
      else                                                                     \
        t = 0;                                                                 \
    } else if (i < 0) {                                                        \
      if (l >= u)                                                              \
        t = (l - u) / (-i) + 1;                                                \
      else                                                                     \
        t = 0;                                                                 \
    } else {                                                                   \
      if (u >= l)                                                              \
        t = (u - l) / i + 1;                                                   \
      else                                                                     \
        t = 0;                                                                 \
    }                                                                          \
    KMP_COUNT_VALUE(OMP_loop_dynamic_iterations, t);                           \
  }
#else
#define KMP_STATS_LOOP_END /* Nothing */
#endif

template <typename T>
static int __kmp_dispatch_next(ident_t *loc, int gtid, kmp_int32 *p_last,
                               T *p_lb, T *p_ub,
                               typename traits_t<T>::signed_t *p_st
#if OMPT_SUPPORT && OMPT_OPTIONAL
                               ,
                               void *codeptr
#endif
                               ) {

  typedef typename traits_t<T>::unsigned_t UT;
  typedef typename traits_t<T>::signed_t ST;
  // This is potentially slightly misleading, schedule(runtime) will appear here
  // even if the actual runtme schedule is static. (Which points out a
  // disadavantage of schedule(runtime): even when static scheduling is used it
  // costs more than a compile time choice to use static scheduling would.)
  KMP_TIME_PARTITIONED_BLOCK(OMP_loop_dynamic_scheduling);

  int status;
  dispatch_private_info_template<T> *pr;
  kmp_info_t *th = __kmp_threads[gtid];
  kmp_team_t *team = th->th.th_team;

  KMP_DEBUG_ASSERT(p_lb && p_ub && p_st); // AC: these cannot be NULL
  KD_TRACE(
      1000,
      ("__kmp_dispatch_next: T#%d called p_lb:%p p_ub:%p p_st:%p p_last: %p\n",
       gtid, p_lb, p_ub, p_st, p_last));
//printf("lala:%s",loc->psource);
  if (team->t.t_serialized) {
    /* NOTE: serialize this dispatch becase we are not at the active level */
    pr = reinterpret_cast<dispatch_private_info_template<T> *>(
        th->th.th_dispatch->th_disp_buffer); /* top of the stack */
    KMP_DEBUG_ASSERT(pr);

    if ((status = (pr->u.p.tc != 0)) == 0) {
      *p_lb = 0;
      *p_ub = 0;
      //            if ( p_last != NULL )
      //                *p_last = 0;
      if (p_st != NULL)
        *p_st = 0;
      if (__kmp_env_consistency_check) {
        if (pr->pushed_ws != ct_none) {
          pr->pushed_ws = __kmp_pop_workshare(gtid, pr->pushed_ws, loc);
        }
      }
    } else if (pr->flags.nomerge) {
      kmp_int32 last;
      T start;
      UT limit, trip, init;
      ST incr;
      T chunk = pr->u.p.parm1;

      KD_TRACE(100, ("__kmp_dispatch_next: T#%d kmp_sch_dynamic_chunked case\n",
                     gtid));

      init = chunk * pr->u.p.count++;
      trip = pr->u.p.tc - 1;

      if ((status = (init <= trip)) == 0) {
        *p_lb = 0;
        *p_ub = 0;
        //                if ( p_last != NULL )
        //                    *p_last = 0;
        if (p_st != NULL)
          *p_st = 0;
        if (__kmp_env_consistency_check) {
          if (pr->pushed_ws != ct_none) {
            pr->pushed_ws = __kmp_pop_workshare(gtid, pr->pushed_ws, loc);
          }
        }
      } else {
        start = pr->u.p.lb;
        limit = chunk + init - 1;
        incr = pr->u.p.st;

        if ((last = (limit >= trip)) != 0) {
          limit = trip;
#if KMP_OS_WINDOWS
          pr->u.p.last_upper = pr->u.p.ub;
#endif /* KMP_OS_WINDOWS */
        }
        if (p_last != NULL)
          *p_last = last;
        if (p_st != NULL)
          *p_st = incr;
        if (incr == 1) {
          *p_lb = start + init;
          *p_ub = start + limit;
        } else {
          *p_lb = start + init * incr;
          *p_ub = start + limit * incr;
        }

        if (pr->flags.ordered) {
          pr->u.p.ordered_lower = init;
          pr->u.p.ordered_upper = limit;
#ifdef KMP_DEBUG
          {
            char *buff;
            // create format specifiers before the debug output
            buff = __kmp_str_format("__kmp_dispatch_next: T#%%d "
                                    "ordered_lower:%%%s ordered_upper:%%%s\n",
                                    traits_t<UT>::spec, traits_t<UT>::spec);
            KD_TRACE(1000, (buff, gtid, pr->u.p.ordered_lower,
                            pr->u.p.ordered_upper));
            __kmp_str_free(&buff);
          }
#endif
        } // if
      } // if
    } else {
      pr->u.p.tc = 0;
      *p_lb = pr->u.p.lb;
      *p_ub = pr->u.p.ub;
#if KMP_OS_WINDOWS
      pr->u.p.last_upper = *p_ub;
#endif /* KMP_OS_WINDOWS */
      if (p_last != NULL)
        *p_last = TRUE;
      if (p_st != NULL)
        *p_st = pr->u.p.st;
    } // if
#ifdef KMP_DEBUG
    {
      char *buff;
      // create format specifiers before the debug output
      buff = __kmp_str_format(
          "__kmp_dispatch_next: T#%%d serialized case: p_lb:%%%s "
          "p_ub:%%%s p_st:%%%s p_last:%%p %%d  returning:%%d\n",
          traits_t<T>::spec, traits_t<T>::spec, traits_t<ST>::spec);
      KD_TRACE(10, (buff, gtid, *p_lb, *p_ub, *p_st, p_last, *p_last, status));
      __kmp_str_free(&buff);
    }
#endif
#if INCLUDE_SSC_MARKS
    SSC_MARK_DISPATCH_NEXT();
#endif
    OMPT_LOOP_END;
    KMP_STATS_LOOP_END;
    return status;
  } else {
    kmp_int32 last = 0;
    dispatch_shared_info_template<T> /*LB4OMP_extensions volatile*/ *sh;

    KMP_DEBUG_ASSERT(th->th.th_dispatch ==
                     &th->th.th_team->t.t_dispatch[th->th.th_info.ds.ds_tid]);

    pr = reinterpret_cast<dispatch_private_info_template<T> *>(
        th->th.th_dispatch->th_dispatch_pr_current);
    KMP_DEBUG_ASSERT(pr);
    sh = reinterpret_cast<
        dispatch_shared_info_template<T> /* LB4OMP_extensions volatile*/ *>(
        th->th.th_dispatch->th_dispatch_sh_current);
    KMP_DEBUG_ASSERT(sh);

#if KMP_USE_HIER_SCHED
    if (pr->flags.use_hier)
      status = sh->hier->next(loc, gtid, pr, &last, p_lb, p_ub, p_st);
    else
#endif // KMP_USE_HIER_SCHED
      status = __kmp_dispatch_next_algorithm<T>(gtid, pr, sh, &last, p_lb, p_ub,
                                                p_st, th->th.th_team_nproc,
                                                th->th.th_info.ds.ds_tid);
    // status == 0: no more iterations to execute
    if (status == 0) {
      UT num_done;

      num_done = test_then_inc<ST>((volatile ST *)&sh->u.s.num_done);
#ifdef KMP_DEBUG
      {
        char *buff;
        // create format specifiers before the debug output
        buff = __kmp_str_format(
            "__kmp_dispatch_next: T#%%d increment num_done:%%%s\n",
            traits_t<UT>::spec);
        KD_TRACE(10, (buff, gtid, sh->u.s.num_done));
        __kmp_str_free(&buff);
      }
#endif

#if KMP_USE_HIER_SCHED
      pr->flags.use_hier = FALSE;
#endif
      if ((ST)num_done == th->th.th_team_nproc - 1) {
#if (KMP_STATIC_STEAL_ENABLED)
        if (pr->schedule == kmp_sch_static_steal &&
            traits_t<T>::type_size > 4) {
          int i;
          kmp_info_t **other_threads = team->t.t_threads;
          // loop complete, safe to destroy locks used for stealing
          for (i = 0; i < th->th.th_team_nproc; ++i) {
            kmp_lock_t *lck = other_threads[i]->th.th_dispatch->th_steal_lock;
            KMP_ASSERT(lck != NULL);
            __kmp_destroy_lock(lck);
            __kmp_free(lck);
            other_threads[i]->th.th_dispatch->th_steal_lock = NULL;
          }
        }
#endif
        /* NOTE: release this buffer to be reused */

        KMP_MB(); /* Flush all pending memory write invalidates.  */

        sh->u.s.num_done = 0;
        sh->u.s.iteration = 0;
        //-------------------LB4OMP_extensions---------------------
        sh->u.s.chunk_size = 1;
        sh->u.s.batch = 0;
        sh->u.s.counter = 0;
        sh->u.s.total_speed = 0;
        sh->u.s.bold_n = 0;
        sh->u.s.bold_m = 0;
        sh->u.s.bold_time = 0;
        sh->u.s.initialized = false;
        //sh->u.s.dbl_vector.clear(); // looks like not needed
        //-------------------LB4OMP_extensions---------------------

        /* TODO replace with general release procedure? */
        if (pr->flags.ordered) {
          sh->u.s.ordered_iteration = 0;
        }

        KMP_MB(); /* Flush all pending memory write invalidates.  */

        sh->buffer_index += __kmp_dispatch_num_buffers;
        KD_TRACE(100, ("__kmp_dispatch_next: T#%d change buffer_index:%d\n",
                       gtid, sh->buffer_index));

        KMP_MB(); /* Flush all pending memory write invalidates.  */

      } // if
      if (__kmp_env_consistency_check) {
        if (pr->pushed_ws != ct_none) {
          pr->pushed_ws = __kmp_pop_workshare(gtid, pr->pushed_ws, loc);
        }
      }

      th->th.th_dispatch->th_deo_fcn = NULL;
      th->th.th_dispatch->th_dxo_fcn = NULL;
      th->th.th_dispatch->th_dispatch_sh_current = NULL;
      th->th.th_dispatch->th_dispatch_pr_current = NULL;
    } // if (status == 0)
#if KMP_OS_WINDOWS
    else if (last) {
      pr->u.p.last_upper = pr->u.p.ub;
    }
#endif /* KMP_OS_WINDOWS */
    if (p_last != NULL && status != 0)
      *p_last = last;
  } // if

#ifdef KMP_DEBUG
  {
    char *buff;
    // create format specifiers before the debug output
    buff = __kmp_str_format(
        "__kmp_dispatch_next: T#%%d normal case: "
        "p_lb:%%%s p_ub:%%%s p_st:%%%s p_last:%%p (%%d) returning:%%d\n",
        traits_t<T>::spec, traits_t<T>::spec, traits_t<ST>::spec);
    KD_TRACE(10, (buff, gtid, *p_lb, *p_ub, p_st ? *p_st : 0, p_last,
                  (p_last ? *p_last : 0), status));
    __kmp_str_free(&buff);
  }
#endif
#if INCLUDE_SSC_MARKS
  SSC_MARK_DISPATCH_NEXT();
#endif
  OMPT_LOOP_END;
  KMP_STATS_LOOP_END;
  return status;
}

template <typename T>
static void __kmp_dist_get_bounds(ident_t *loc, kmp_int32 gtid,
                                  kmp_int32 *plastiter, T *plower, T *pupper,
                                  typename traits_t<T>::signed_t incr) {
  typedef typename traits_t<T>::unsigned_t UT;
  kmp_uint32 team_id;
  kmp_uint32 nteams;
  UT trip_count;
  kmp_team_t *team;
  kmp_info_t *th;

  KMP_DEBUG_ASSERT(plastiter && plower && pupper);
  KE_TRACE(10, ("__kmpc_dist_get_bounds called (%d)\n", gtid));
#ifdef KMP_DEBUG
  typedef typename traits_t<T>::signed_t ST;
  {
    char *buff;
    // create format specifiers before the debug output
    buff = __kmp_str_format("__kmpc_dist_get_bounds: T#%%d liter=%%d "
                            "iter=(%%%s, %%%s, %%%s) signed?<%s>\n",
                            traits_t<T>::spec, traits_t<T>::spec,
                            traits_t<ST>::spec, traits_t<T>::spec);
    KD_TRACE(100, (buff, gtid, *plastiter, *plower, *pupper, incr));
    __kmp_str_free(&buff);
  }
#endif

  if (__kmp_env_consistency_check) {
    if (incr == 0) {
      __kmp_error_construct(kmp_i18n_msg_CnsLoopIncrZeroProhibited, ct_pdo,
                            loc);
    }
    if (incr > 0 ? (*pupper < *plower) : (*plower < *pupper)) {
      // The loop is illegal.
      // Some zero-trip loops maintained by compiler, e.g.:
      //   for(i=10;i<0;++i) // lower >= upper - run-time check
      //   for(i=0;i>10;--i) // lower <= upper - run-time check
      //   for(i=0;i>10;++i) // incr > 0       - compile-time check
      //   for(i=10;i<0;--i) // incr < 0       - compile-time check
      // Compiler does not check the following illegal loops:
      //   for(i=0;i<10;i+=incr) // where incr<0
      //   for(i=10;i>0;i-=incr) // where incr<0
      __kmp_error_construct(kmp_i18n_msg_CnsLoopIncrIllegal, ct_pdo, loc);
    }
  }
  th = __kmp_threads[gtid];
  team = th->th.th_team;
#if OMP_40_ENABLED
  KMP_DEBUG_ASSERT(th->th.th_teams_microtask); // we are in the teams construct
  nteams = th->th.th_teams_size.nteams;
#endif
  team_id = team->t.t_master_tid;
  KMP_DEBUG_ASSERT(nteams == (kmp_uint32)team->t.t_parent->t.t_nproc);

  // compute global trip count
  if (incr == 1) {
    trip_count = *pupper - *plower + 1;
  } else if (incr == -1) {
    trip_count = *plower - *pupper + 1;
  } else if (incr > 0) {
    // upper-lower can exceed the limit of signed type
    trip_count = (UT)(*pupper - *plower) / incr + 1;
  } else {
    trip_count = (UT)(*plower - *pupper) / (-incr) + 1;
  }

  if (trip_count <= nteams) {
    KMP_DEBUG_ASSERT(
        __kmp_static == kmp_sch_static_greedy ||
        __kmp_static ==
            kmp_sch_static_balanced); // Unknown static scheduling type.
    // only some teams get single iteration, others get nothing
    if (team_id < trip_count) {
      *pupper = *plower = *plower + team_id * incr;
    } else {
      *plower = *pupper + incr; // zero-trip loop
    }
    if (plastiter != NULL)
      *plastiter = (team_id == trip_count - 1);
  } else {
    if (__kmp_static == kmp_sch_static_balanced) {
      UT chunk = trip_count / nteams;
      UT extras = trip_count % nteams;
      *plower +=
          incr * (team_id * chunk + (team_id < extras ? team_id : extras));
      *pupper = *plower + chunk * incr - (team_id < extras ? 0 : incr);
      if (plastiter != NULL)
        *plastiter = (team_id == nteams - 1);
    } else {
      T chunk_inc_count =
          (trip_count / nteams + ((trip_count % nteams) ? 1 : 0)) * incr;
      T upper = *pupper;
      KMP_DEBUG_ASSERT(__kmp_static == kmp_sch_static_greedy);
      // Unknown static scheduling type.
      *plower += team_id * chunk_inc_count;
      *pupper = *plower + chunk_inc_count - incr;
      // Check/correct bounds if needed
      if (incr > 0) {
        if (*pupper < *plower)
          *pupper = traits_t<T>::max_value;
        if (plastiter != NULL)
          *plastiter = *plower <= upper && *pupper > upper - incr;
        if (*pupper > upper)
          *pupper = upper; // tracker C73258
      } else {
        if (*pupper > *plower)
          *pupper = traits_t<T>::min_value;
        if (plastiter != NULL)
          *plastiter = *plower >= upper && *pupper < upper - incr;
        if (*pupper < upper)
          *pupper = upper; // tracker C73258
      }
    }
  }
}

//-----------------------------------------------------------------------------
// Dispatch routines
//    Transfer call to template< type T >
//    __kmp_dispatch_init( ident_t *loc, int gtid, enum sched_type schedule,
//                         T lb, T ub, ST st, ST chunk )
extern "C" {

/*!
@ingroup WORK_SHARING
@{
@param loc Source location
@param gtid Global thread id
@param schedule Schedule type
@param lb  Lower bound
@param ub  Upper bound
@param st  Step (or increment if you prefer)
@param chunk The chunk size to block with

This function prepares the runtime to start a dynamically scheduled for loop,
saving the loop arguments.
These functions are all identical apart from the types of the arguments.
*/

void __kmpc_dispatch_init_4(ident_t *loc, kmp_int32 gtid,
                            enum sched_type schedule, kmp_int32 lb,
                            kmp_int32 ub, kmp_int32 st, kmp_int32 chunk) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmp_dispatch_init<kmp_int32>(loc, gtid, schedule, lb, ub, st, chunk, true);
}
/*!
See @ref __kmpc_dispatch_init_4
*/
void __kmpc_dispatch_init_4u(ident_t *loc, kmp_int32 gtid,
                             enum sched_type schedule, kmp_uint32 lb,
                             kmp_uint32 ub, kmp_int32 st, kmp_int32 chunk) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmp_dispatch_init<kmp_uint32>(loc, gtid, schedule, lb, ub, st, chunk, true);
}

/*!
See @ref __kmpc_dispatch_init_4
*/
void __kmpc_dispatch_init_8(ident_t *loc, kmp_int32 gtid,
                            enum sched_type schedule, kmp_int64 lb,
                            kmp_int64 ub, kmp_int64 st, kmp_int64 chunk) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmp_dispatch_init<kmp_int64>(loc, gtid, schedule, lb, ub, st, chunk, true);
}

/*!
See @ref __kmpc_dispatch_init_4
*/
void __kmpc_dispatch_init_8u(ident_t *loc, kmp_int32 gtid,
                             enum sched_type schedule, kmp_uint64 lb,
                             kmp_uint64 ub, kmp_int64 st, kmp_int64 chunk) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmp_dispatch_init<kmp_uint64>(loc, gtid, schedule, lb, ub, st, chunk, true);
}

/*!
See @ref __kmpc_dispatch_init_4

Difference from __kmpc_dispatch_init set of functions is these functions
are called for composite distribute parallel for construct. Thus before
regular iterations dispatching we need to calc per-team iteration space.

These functions are all identical apart from the types of the arguments.
*/
void __kmpc_dist_dispatch_init_4(ident_t *loc, kmp_int32 gtid,
                                 enum sched_type schedule, kmp_int32 *p_last,
                                 kmp_int32 lb, kmp_int32 ub, kmp_int32 st,
                                 kmp_int32 chunk) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmp_dist_get_bounds<kmp_int32>(loc, gtid, p_last, &lb, &ub, st);
  __kmp_dispatch_init<kmp_int32>(loc, gtid, schedule, lb, ub, st, chunk, true);
}

void __kmpc_dist_dispatch_init_4u(ident_t *loc, kmp_int32 gtid,
                                  enum sched_type schedule, kmp_int32 *p_last,
                                  kmp_uint32 lb, kmp_uint32 ub, kmp_int32 st,
                                  kmp_int32 chunk) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmp_dist_get_bounds<kmp_uint32>(loc, gtid, p_last, &lb, &ub, st);
  __kmp_dispatch_init<kmp_uint32>(loc, gtid, schedule, lb, ub, st, chunk, true);
}

void __kmpc_dist_dispatch_init_8(ident_t *loc, kmp_int32 gtid,
                                 enum sched_type schedule, kmp_int32 *p_last,
                                 kmp_int64 lb, kmp_int64 ub, kmp_int64 st,
                                 kmp_int64 chunk) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmp_dist_get_bounds<kmp_int64>(loc, gtid, p_last, &lb, &ub, st);
  __kmp_dispatch_init<kmp_int64>(loc, gtid, schedule, lb, ub, st, chunk, true);
}

void __kmpc_dist_dispatch_init_8u(ident_t *loc, kmp_int32 gtid,
                                  enum sched_type schedule, kmp_int32 *p_last,
                                  kmp_uint64 lb, kmp_uint64 ub, kmp_int64 st,
                                  kmp_int64 chunk) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmp_dist_get_bounds<kmp_uint64>(loc, gtid, p_last, &lb, &ub, st);
  __kmp_dispatch_init<kmp_uint64>(loc, gtid, schedule, lb, ub, st, chunk, true);
}

/*!
@param loc Source code location
@param gtid Global thread id
@param p_last Pointer to a flag set to one if this is the last chunk or zero
otherwise
@param p_lb   Pointer to the lower bound for the next chunk of work
@param p_ub   Pointer to the upper bound for the next chunk of work
@param p_st   Pointer to the stride for the next chunk of work
@return one if there is work to be done, zero otherwise

Get the next dynamically allocated chunk of work for this thread.
If there is no more work, then the lb,ub and stride need not be modified.
*/
int __kmpc_dispatch_next_4(ident_t *loc, kmp_int32 gtid, kmp_int32 *p_last,
                           kmp_int32 *p_lb, kmp_int32 *p_ub, kmp_int32 *p_st) {
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  return __kmp_dispatch_next<kmp_int32>(loc, gtid, p_last, p_lb, p_ub, p_st
#if OMPT_SUPPORT && OMPT_OPTIONAL
                                        ,
                                        OMPT_LOAD_RETURN_ADDRESS(gtid)
#endif
                                            );
}

/*!
See @ref __kmpc_dispatch_next_4
*/
int __kmpc_dispatch_next_4u(ident_t *loc, kmp_int32 gtid, kmp_int32 *p_last,
                            kmp_uint32 *p_lb, kmp_uint32 *p_ub,
                            kmp_int32 *p_st) {
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  return __kmp_dispatch_next<kmp_uint32>(loc, gtid, p_last, p_lb, p_ub, p_st
#if OMPT_SUPPORT && OMPT_OPTIONAL
                                         ,
                                         OMPT_LOAD_RETURN_ADDRESS(gtid)
#endif
                                             );
}

/*!
See @ref __kmpc_dispatch_next_4
*/
int __kmpc_dispatch_next_8(ident_t *loc, kmp_int32 gtid, kmp_int32 *p_last,
                           kmp_int64 *p_lb, kmp_int64 *p_ub, kmp_int64 *p_st) {
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  return __kmp_dispatch_next<kmp_int64>(loc, gtid, p_last, p_lb, p_ub, p_st
#if OMPT_SUPPORT && OMPT_OPTIONAL
                                        ,
                                        OMPT_LOAD_RETURN_ADDRESS(gtid)
#endif
                                            );
}

/*!
See @ref __kmpc_dispatch_next_4
*/
int __kmpc_dispatch_next_8u(ident_t *loc, kmp_int32 gtid, kmp_int32 *p_last,
                            kmp_uint64 *p_lb, kmp_uint64 *p_ub,
                            kmp_int64 *p_st) {
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  return __kmp_dispatch_next<kmp_uint64>(loc, gtid, p_last, p_lb, p_ub, p_st
#if OMPT_SUPPORT && OMPT_OPTIONAL
                                         ,
                                         OMPT_LOAD_RETURN_ADDRESS(gtid)
#endif
                                             );
}

/*!
@param loc Source code location
@param gtid Global thread id

Mark the end of a dynamic loop.
*/
void __kmpc_dispatch_fini_4(ident_t *loc, kmp_int32 gtid) {
  __kmp_dispatch_finish<kmp_uint32>(gtid, loc);
}

/*!
See @ref __kmpc_dispatch_fini_4
*/
void __kmpc_dispatch_fini_8(ident_t *loc, kmp_int32 gtid) {
  __kmp_dispatch_finish<kmp_uint64>(gtid, loc);
}

/*!
See @ref __kmpc_dispatch_fini_4
*/
void __kmpc_dispatch_fini_4u(ident_t *loc, kmp_int32 gtid) {
  __kmp_dispatch_finish<kmp_uint32>(gtid, loc);
}

/*!
See @ref __kmpc_dispatch_fini_4
*/
void __kmpc_dispatch_fini_8u(ident_t *loc, kmp_int32 gtid) {
  __kmp_dispatch_finish<kmp_uint64>(gtid, loc);
}
/*! @} */

//-----------------------------------------------------------------------------
// Non-template routines from kmp_dispatch.cpp used in other sources

kmp_uint32 __kmp_eq_4(kmp_uint32 value, kmp_uint32 checker) {
  return value == checker;
}

kmp_uint32 __kmp_neq_4(kmp_uint32 value, kmp_uint32 checker) {
  return value != checker;
}

kmp_uint32 __kmp_lt_4(kmp_uint32 value, kmp_uint32 checker) {
  return value < checker;
}

kmp_uint32 __kmp_ge_4(kmp_uint32 value, kmp_uint32 checker) {
  return value >= checker;
}

kmp_uint32 __kmp_le_4(kmp_uint32 value, kmp_uint32 checker) {
  return value <= checker;
}

kmp_uint32
__kmp_wait_yield_4(volatile kmp_uint32 *spinner, kmp_uint32 checker,
                   kmp_uint32 (*pred)(kmp_uint32, kmp_uint32),
                   void *obj // Higher-level synchronization object, or NULL.
                   ) {
  // note: we may not belong to a team at this point
  volatile kmp_uint32 *spin = spinner;
  kmp_uint32 check = checker;
  kmp_uint32 spins;
  kmp_uint32 (*f)(kmp_uint32, kmp_uint32) = pred;
  kmp_uint32 r;

  KMP_FSYNC_SPIN_INIT(obj, CCAST(kmp_uint32 *, spin));
  KMP_INIT_YIELD(spins);
  // main wait spin loop
  while (!f(r = TCR_4(*spin), check)) {
    KMP_FSYNC_SPIN_PREPARE(obj);
    /* GEH - remove this since it was accidentally introduced when kmp_wait was
       split. It causes problems with infinite recursion because of exit lock */
    /* if ( TCR_4(__kmp_global.g.g_done) && __kmp_global.g.g_abort)
        __kmp_abort_thread(); */

    /* if we have waited a bit, or are oversubscribed, yield */
    /* pause is in the following code */
    KMP_YIELD(TCR_4(__kmp_nth) > __kmp_avail_proc);
    KMP_YIELD_SPIN(spins);
  }
  KMP_FSYNC_SPIN_ACQUIRED(obj);
  return r;
}

void __kmp_wait_yield_4_ptr(
    void *spinner, kmp_uint32 checker, kmp_uint32 (*pred)(void *, kmp_uint32),
    void *obj // Higher-level synchronization object, or NULL.
    ) {
  // note: we may not belong to a team at this point
  void *spin = spinner;
  kmp_uint32 check = checker;
  kmp_uint32 spins;
  kmp_uint32 (*f)(void *, kmp_uint32) = pred;

  KMP_FSYNC_SPIN_INIT(obj, spin);
  KMP_INIT_YIELD(spins);
  // main wait spin loop
  while (!f(spin, check)) {
    KMP_FSYNC_SPIN_PREPARE(obj);
    /* if we have waited a bit, or are oversubscribed, yield */
    /* pause is in the following code */
    KMP_YIELD(TCR_4(__kmp_nth) > __kmp_avail_proc);
    KMP_YIELD_SPIN(spins);
  }
  KMP_FSYNC_SPIN_ACQUIRED(obj);
}

} // extern "C"

#ifdef KMP_GOMP_COMPAT

void __kmp_aux_dispatch_init_4(ident_t *loc, kmp_int32 gtid,
                               enum sched_type schedule, kmp_int32 lb,
                               kmp_int32 ub, kmp_int32 st, kmp_int32 chunk,
                               int push_ws) {
  __kmp_dispatch_init<kmp_int32>(loc, gtid, schedule, lb, ub, st, chunk,
                                 push_ws);
}

void __kmp_aux_dispatch_init_4u(ident_t *loc, kmp_int32 gtid,
                                enum sched_type schedule, kmp_uint32 lb,
                                kmp_uint32 ub, kmp_int32 st, kmp_int32 chunk,
                                int push_ws) {
  __kmp_dispatch_init<kmp_uint32>(loc, gtid, schedule, lb, ub, st, chunk,
                                  push_ws);
}

void __kmp_aux_dispatch_init_8(ident_t *loc, kmp_int32 gtid,
                               enum sched_type schedule, kmp_int64 lb,
                               kmp_int64 ub, kmp_int64 st, kmp_int64 chunk,
                               int push_ws) {
  __kmp_dispatch_init<kmp_int64>(loc, gtid, schedule, lb, ub, st, chunk,
                                 push_ws);
}

void __kmp_aux_dispatch_init_8u(ident_t *loc, kmp_int32 gtid,
                                enum sched_type schedule, kmp_uint64 lb,
                                kmp_uint64 ub, kmp_int64 st, kmp_int64 chunk,
                                int push_ws) {
  __kmp_dispatch_init<kmp_uint64>(loc, gtid, schedule, lb, ub, st, chunk,
                                  push_ws);
}

void __kmp_aux_dispatch_fini_chunk_4(ident_t *loc, kmp_int32 gtid) {
  __kmp_dispatch_finish_chunk<kmp_uint32>(gtid, loc);
}

void __kmp_aux_dispatch_fini_chunk_8(ident_t *loc, kmp_int32 gtid) {
  __kmp_dispatch_finish_chunk<kmp_uint64>(gtid, loc);
}

void __kmp_aux_dispatch_fini_chunk_4u(ident_t *loc, kmp_int32 gtid) {
  __kmp_dispatch_finish_chunk<kmp_uint32>(gtid, loc);
}

void __kmp_aux_dispatch_fini_chunk_8u(ident_t *loc, kmp_int32 gtid) {
  __kmp_dispatch_finish_chunk<kmp_uint64>(gtid, loc);
}

#endif /* KMP_GOMP_COMPAT */

/* ------------------------------------------------------------------------ */
