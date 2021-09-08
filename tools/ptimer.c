// See links below for details:
// https://www.intel.com/content/dam/www/public/us/en/documents/white-papers/ia-32-ia-64-benchmark-code-execution-paper.pdf
// https://www.ccsl.carleton.ca/~jamuir/rdtscpm1.pdf

#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "util.h"
#include "ptimer.h"


#define TSC_BEGIN(hi, lo)    \
     do { \
       __asm__ volatile ("cpuid\n\t"   \
      "rdtsc\n\t"   \
      "mov %%edx, %0\n\t"  \
      "mov %%eax, %1\n\t"  \
      : "=r" ((hi)), "=r" ((lo))  \
      :: "%rax", "%rbx", "%rcx", "%rdx", "memory");   \
    } while (0)

#define TSC_END(hi, lo)     \
    do { \
      __asm__ volatile ("rdtscp\n\t"  \
      "mov %%edx, %0\n\t"  \
      "mov %%eax, %1\n\t"  \
      "cpuid\n\t" \
      : "=r" ((hi)), "=r" ((lo)) \
      :: "%rax", "%rbx", "%rcx", "%rdx", "memory"); \
    } while (0)


// Read from 64-bit MSR
uint64_t readMSR(uint32_t msr_id) {
  // If we can't read the MSR, we approximate the freq
  uint64_t ret = 2200;
  int fd = open("/dev/cpu/0/msr", O_RDONLY);
  if (fd == -1) {
    pr_error(errno, "readMSR");
    printf("Run as root to read MSR\n");
    return ret;
  }
  ssize_t code = pread(fd, &ret, sizeof(ret), msr_id);
  if (code == -1) {
    pr_error(errno, "pread");
    printf("Pread error in readMSR. Frequency might be wrong.\n");
  }
  close(fd);
  return ret;
}

/*
  Get the frequency (in MHz) of the invariant TSC,
  which should be equal to the non-turbo freq. 
  (IA32 Developer Manual, 2-86, Package Non-Turbo Ratio)
  MSR CEh - 15:8
 */
uint32_t getFrequency() {
  // Get the non-turbo ratio
  uint8_t ratio = (readMSR(0xCE) >> 8) & 0xFF;
  // multiply by 100 to get frequency in MHz
  uint32_t totalFrequency = ratio * 100;
  printf("CPU running at %u\n", totalFrequency);
  return totalFrequency;
}


void ptimer_init(PTimer *pt) {
  pt->_frequency = getFrequency();
  pt->_startT = pt->_stopT = 0;
}



void ptimer_reset(PTimer *pt) {
  pt->_startT = pt->_stopT = 0;
}

void ptimer_start(PTimer *pt) {
  unsigned int hi, lo;
  TSC_BEGIN(hi, lo);

  pt->_startT = (((unsigned long long)(hi) << 32) | (lo) );
}

void ptimer_stop(PTimer *pt) {
  unsigned int hi, lo;
  TSC_END(hi, lo);

  pt->_stopT = (((unsigned long long)(hi) << 32) | (lo) );
}

double getElapsed(PTimer *pt, double unitsRatio) {
  u64 tm;
  tm = pt->_stopT - pt->_startT;
  return (double)tm / (pt->_frequency * unitsRatio);
}

double ptimer_getElapsedSec(PTimer *pt) {
  return getElapsed(pt, 1000000.0);
}

double ptimer_getElapsedMSec(PTimer *pt) {
  return getElapsed(pt, 1000.0);
}

double ptimer_getElapsedUSec(PTimer *pt) {
  return getElapsed(pt, 1.0);
}
