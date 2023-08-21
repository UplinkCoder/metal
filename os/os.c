/// Small platfrom abstraction
#include "os.h"
#define DEFAULT_PAGESIZE 4096
os_error_t PageAlloc(uint32_t minSize, uint32_t* allocatedSize, void** outMemory);
os_error_t GetTimeStamp(uint32_t* tsp);
os_error_t SetStartTime(void);


struct OS OS =
{
    PageAlloc,
    GetTimeStamp,
    SetStartTime,

    DEFAULT_PAGESIZE
};

os_error_t PageAlloc(uint32_t minSize, uint32_t* allocatedSize, void** outMemory)
{
    void* result;

    uint32_t allocated =
        (minSize + (OS.PageSize - 1)) & ~(OS.PageSize - 1);

#if defined(WINDOWS)
   result = VirtualAlloc(0, allocated,
                         MEM_RESERVE | MEM_COMMIT,
                         PAGE_READWRITE);
#elif defined(POSIX)
#  if !defined(MAP_ANONYMOUS)
#    define MAP_ANONYMOUS MAP_ANON
#  endif
    result = mmap(0, allocated,
                  PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE,
                  -1, 0);
#else
#  error "OS not supported"
#endif
    if (result)
    {
        (*allocatedSize) = allocated;
        (*outMemory) = result;
    }

    return Error_Success;
}
/// Get's a 32bit time stamp where the upper 16 bit are second resolution and the lower 16 bit are in increments of 16 microseconds.
os_error_t GetTimeStamp(uint32_t* tsp)
{
#if defined(POSIX)
    struct timeval now;
    gettimeofday(&now, 0);
    (*tsp) = (((now.tv_sec & 0xffff) << 16) | ((now.tv_usec >> 4) & 0xffff));
#elif defined(WINDOWS)
    uint64_t now;
    GetSystemTimeAsFileTime((FILETIME*)&now);
    (*tsp) = ((((now >> 32) & 0xffff) << 16) | ((now >> 5) & 0xffff));
#endif
    return Error_Success;
}

/// to be called at startup so we have a timestamp to compare to.
os_error_t SetStartTime(void)
{

    struct tm* startTime = gmtime(NULL);

    os_date_t now = {
        startTime->tm_sec,
        startTime->tm_min,
        startTime->tm_hour,
        startTime->tm_mday,
        startTime->tm_mon + 1,
        startTime->tm_year + 1900
    } ;

    GetTimeStamp(&OS.StartTimeStamp);

    OS.StartDate = now;

    return Error_Success;
}

const char* PrintTime(uint32_t tsp)
{
    uint32_t elapsedSeconds;
    uint32_t elapsedMicroseconds;

    if (tsp > OS.StartTimeStamp)
    {
        elapsedSeconds = (OS.StartTimeStamp >> 16) - (tsp >> 16);
        elapsedMicroseconds = ((OS.StartTimeStamp & 0xFFFF) - (tsp & 0xFFFF)) << 4;
    }
    else
    {
        elapsedSeconds = (tsp >> 16) - (OS.StartTimeStamp >> 16);
        elapsedMicroseconds = ((tsp & 0xFFFF) - (OS.StartTimeStamp & 0xFFFF)) << 4;
    }

    {
        uint32_t el_sec = elapsedSeconds % 60;
        uint32_t el_min = (elapsedSeconds / 60) % 60;
        uint32_t el_hr = (elapsedSeconds / 3600) % 24;

        int32_t year;
        int32_t month;
        int32_t day;
        int32_t hour;
        int32_t minute;
        int32_t second;


        printf("%d-%d-%d:%d:%d:%d",
            year, month, day,
            hour, minute, second);
    }
}
